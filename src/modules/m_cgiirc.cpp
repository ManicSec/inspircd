/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2009 InspIRCd Development Team
 * See: http://wiki.inspircd.org/Credits
 *
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "inspircd.h"

#ifndef WINDOWS
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

/* $ModDesc: Change user's hosts connecting from known CGI:IRC hosts */

enum CGItype { INVALID, PASS, IDENT, PASSFIRST, IDENTFIRST, WEBIRC };


/** Holds a CGI site's details
 */
class CGIhost : public classbase
{
public:
	std::string hostmask;
	CGItype type;
	std::string password;

	CGIhost(const std::string &mask = "", CGItype t = IDENTFIRST, const std::string &spassword ="")
	: hostmask(mask), type(t), password(spassword)
	{
	}
};
typedef std::vector<CGIhost> CGIHostlist;

/*
 * WEBIRC
 *  This is used for the webirc method of CGIIRC auth, and is (really) the best way to do these things.
 *  Syntax: WEBIRC password client hostname ip
 *  Where password is a shared key, client is the name of the "client" and version (e.g. cgiirc), hostname
 *  is the resolved host of the client issuing the command and IP is the real IP of the client.
 *
 * How it works:
 *  To tie in with the rest of cgiirc module, and to avoid race conditions, /webirc is only processed locally
 *  and simply sets metadata on the user, which is later decoded on full connect to give something meaningful.
 */
class CommandWebirc : public Command
{
	bool notify;
 public:
	StringExtItem realhost;
	StringExtItem realip;
	LocalStringExt webirc_hostname;
	LocalStringExt webirc_ip;

	CGIHostlist Hosts;
	CommandWebirc(InspIRCd* Instance, Module* Creator, bool bnotify)
		: Command(Instance, Creator, "WEBIRC", 0, 4, true), notify(bnotify),
		  realhost("cgiirc_realhost", Creator), realip("cgiirc_realip", Creator),
		  webirc_hostname("cgiirc_webirc_hostname", Creator), webirc_ip("cgiirc_webirc_ip", Creator)
		{
			this->syntax = "password client hostname ip";
		}
		CmdResult Handle(const std::vector<std::string> &parameters, User *user)
		{
			if(user->registered == REG_ALL)
				return CMD_FAILURE;

			for(CGIHostlist::iterator iter = Hosts.begin(); iter != Hosts.end(); iter++)
			{
				if(InspIRCd::Match(user->host, iter->hostmask, ascii_case_insensitive_map) || InspIRCd::MatchCIDR(user->GetIPString(), iter->hostmask, ascii_case_insensitive_map))
				{
					if(iter->type == WEBIRC && parameters[0] == iter->password)
					{
						realhost.set(user, user->host);
						realip.set(user, user->GetIPString());
						if (notify)
							ServerInstance->SNO->WriteGlobalSno('a', "Connecting user %s detected as using CGI:IRC (%s), changing real host to %s from %s", user->nick.c_str(), user->host.c_str(), parameters[2].c_str(), user->host.c_str());
						webirc_hostname.set(user, parameters[2]);
						webirc_ip.set(user, parameters[3]);
						return CMD_SUCCESS;
					}
				}
			}

			ServerInstance->SNO->WriteGlobalSno('a', "Connecting user %s tried to use WEBIRC, but didn't match any configured webirc blocks.", user->GetFullRealHost().c_str());
			return CMD_FAILURE;
		}
};


/** Resolver for CGI:IRC hostnames encoded in ident/GECOS
 */
class CGIResolver : public Resolver
{
	std::string typ;
	int theirfd;
	User* them;
	bool notify;
 public:
	CGIResolver(Module* me, InspIRCd* Instance, bool NotifyOpers, const std::string &source, bool forward, User* u, int userfd, const std::string &type, bool &cached)
		: Resolver(Instance, source, forward ? DNS_QUERY_A : DNS_QUERY_PTR4, cached, me), typ(type), theirfd(userfd), them(u), notify(NotifyOpers) { }

	virtual void OnLookupComplete(const std::string &result, unsigned int ttl, bool cached)
	{
		/* Check the user still exists */
		if ((them) && (them == ServerInstance->SE->GetRef(theirfd)))
		{
			if (notify)
				ServerInstance->SNO->WriteGlobalSno('a', "Connecting user %s detected as using CGI:IRC (%s), changing real host to %s from %s", them->nick.c_str(), them->host.c_str(), result.c_str(), typ.c_str());

			them->host.assign(result,0, 64);
			them->dhost.assign(result, 0, 64);
			if (querytype)
				them->SetClientIP(result.c_str());
			them->ident.assign("~cgiirc", 0, 8);
			them->InvalidateCache();
			them->CheckLines(true);
		}
	}

	virtual void OnError(ResolverError e, const std::string &errormessage)
	{
		if ((them) && (them == ServerInstance->SE->GetRef(theirfd)))
		{
			if (notify)
				ServerInstance->SNO->WriteToSnoMask('a', "Connecting user %s detected as using CGI:IRC (%s), but their host can't be resolved from their %s!", them->nick.c_str(), them->host.c_str(), typ.c_str());
		}
	}

	virtual ~CGIResolver()
	{
	}
};

class ModuleCgiIRC : public Module
{
	CommandWebirc cmd;
	bool NotifyOpers;
public:
	ModuleCgiIRC(InspIRCd* Me) : Module(Me), cmd(Me, this, NotifyOpers)
	{
		OnRehash(NULL);
		ServerInstance->AddCommand(&cmd);
		Extensible::Register(&cmd.realhost);
		Extensible::Register(&cmd.realip);
		Extensible::Register(&cmd.webirc_hostname);
		Extensible::Register(&cmd.webirc_ip);

		Implementation eventlist[] = { I_OnRehash, I_OnUserRegister, I_OnCleanup, I_OnSyncUser, I_OnDecodeMetaData, I_OnUserDisconnect, I_OnUserConnect };
		ServerInstance->Modules->Attach(eventlist, this, 7);
	}


	virtual void Prioritize()
	{
		ServerInstance->Modules->SetPriority(this, I_OnUserConnect, PRIORITY_FIRST);
	}

	virtual void OnRehash(User* user)
	{
		ConfigReader Conf(ServerInstance);
		cmd.Hosts.clear();

		NotifyOpers = Conf.ReadFlag("cgiirc", "opernotice", 0);	// If we send an oper notice when a CGI:IRC has their host changed.

		if(Conf.GetError() == CONF_VALUE_NOT_FOUND)
			NotifyOpers = true;

		for(int i = 0; i < Conf.Enumerate("cgihost"); i++)
		{
			std::string hostmask = Conf.ReadValue("cgihost", "mask", i); // An allowed CGI:IRC host
			std::string type = Conf.ReadValue("cgihost", "type", i); // What type of user-munging we do on this host.
			std::string password = Conf.ReadValue("cgihost", "password", i);

			if(hostmask.length())
			{
				if (type == "webirc" && !password.length()) {
						ServerInstance->Logs->Log("CONFIG",DEFAULT, "m_cgiirc: Missing password in config: %s", hostmask.c_str());
				}
				else
				{
					CGItype cgitype = INVALID;
					if (type == "pass")
						cgitype = PASS;
					else if (type == "ident")
						cgitype = IDENT;
					else if (type == "passfirst")
						cgitype = PASSFIRST;
					else if (type == "webirc")
					{
						cgitype = WEBIRC;
					}

					if (cgitype == INVALID)
						cgitype = PASS;

					cmd.Hosts.push_back(CGIhost(hostmask,cgitype, password.length() ? password : "" ));
				}
			}
			else
			{
				ServerInstance->Logs->Log("CONFIG",DEFAULT, "m_cgiirc.so: Invalid <cgihost:mask> value in config: %s", hostmask.c_str());
				continue;
			}
		}
	}

	virtual ModResult OnUserRegister(User* user)
	{
		for(CGIHostlist::iterator iter = cmd.Hosts.begin(); iter != cmd.Hosts.end(); iter++)
		{
			if(InspIRCd::Match(user->host, iter->hostmask, ascii_case_insensitive_map) || InspIRCd::MatchCIDR(user->GetIPString(), iter->hostmask, ascii_case_insensitive_map))
			{
				// Deal with it...
				if(iter->type == PASS)
				{
					CheckPass(user); // We do nothing if it fails so...
					user->CheckLines(true);
				}
				else if(iter->type == PASSFIRST && !CheckPass(user))
				{
					// If the password lookup failed, try the ident
					CheckIdent(user);	// If this fails too, do nothing
					user->CheckLines(true);
				}
				else if(iter->type == IDENT)
				{
					CheckIdent(user); // Nothing on failure.
					user->CheckLines(true);
				}
				else if(iter->type == IDENTFIRST && !CheckIdent(user))
				{
					// If the ident lookup fails, try the password.
					CheckPass(user);
					user->CheckLines(true);
				}
				else if(iter->type == WEBIRC)
				{
					// We don't need to do anything here
				}
				return MOD_RES_PASSTHRU;
			}
		}
		return MOD_RES_PASSTHRU;
	}

	virtual void OnUserConnect(User* user)
	{
		std::string *webirc_hostname = cmd.webirc_hostname.get(user);
		std::string *webirc_ip = cmd.webirc_ip.get(user);
		if (webirc_hostname)
		{
			user->host.assign(*webirc_hostname, 0, 64);
			user->dhost.assign(*webirc_hostname, 0, 64);
			user->InvalidateCache();
			cmd.webirc_hostname.unset(user);
		}
		if (webirc_ip)
		{
			ServerInstance->Users->RemoveCloneCounts(user);
			user->SetClientIP(webirc_ip->c_str());
			user->InvalidateCache();
			cmd.webirc_ip.unset(user);
			ServerInstance->Users->AddLocalClone(user);
			ServerInstance->Users->AddGlobalClone(user);
			user->CheckClass();
			user->CheckLines(true);
		}
	}

	bool CheckPass(User* user)
	{
		if(IsValidHost(user->password))
		{
			cmd.realhost.set(user, user->host);
			cmd.realip.set(user, user->GetIPString());
			user->host.assign(user->password, 0, 64);
			user->dhost.assign(user->password, 0, 64);
			user->InvalidateCache();

			bool valid = false;
			ServerInstance->Users->RemoveCloneCounts(user);
			valid = user->SetClientIP(user->password.c_str());
			ServerInstance->Users->AddLocalClone(user);
			ServerInstance->Users->AddGlobalClone(user);
			user->CheckClass();

			if (valid)
			{
				/* We were given a IP in the password, we don't do DNS so they get this is as their host as well. */
				if(NotifyOpers)
					ServerInstance->SNO->WriteGlobalSno('a', "Connecting user %s detected as using CGI:IRC (%s), changing real host to %s from PASS", user->nick.c_str(), user->host.c_str(), user->password.c_str());
			}
			else
			{
				/* We got as resolved hostname in the password. */
				try
				{

					bool cached;
					CGIResolver* r = new CGIResolver(this, ServerInstance, NotifyOpers, user->password, false, user, user->GetFd(), "PASS", cached);
					ServerInstance->AddResolver(r, cached);
				}
				catch (...)
				{
					if (NotifyOpers)
						ServerInstance->SNO->WriteToSnoMask('a', "Connecting user %s detected as using CGI:IRC (%s), but I could not resolve their hostname!", user->nick.c_str(), user->host.c_str());
				}
			}

			user->password.clear();
			return true;
		}

		return false;
	}

	bool CheckIdent(User* user)
	{
		const char* ident;
		int len = user->ident.length();
		in_addr newip;

		if(len == 8)
			ident = user->ident.c_str();
		else if(len == 9 && user->ident[0] == '~')
			ident = user->ident.c_str() + 1;
		else
			return false;

		errno = 0;
		unsigned long ipaddr = strtoul(ident, NULL, 16);
		if (errno)
			return false;
		newip.s_addr = htonl(ipaddr);
		char* newipstr = inet_ntoa(newip);

		cmd.realhost.set(user, user->host);
		cmd.realip.set(user, user->GetIPString());
		ServerInstance->Users->RemoveCloneCounts(user);
		user->SetClientIP(newipstr);
		ServerInstance->Users->AddLocalClone(user);
		ServerInstance->Users->AddGlobalClone(user);
		user->CheckClass();
		user->host = newipstr;
		user->dhost = newipstr;
		user->ident.assign("~cgiirc", 0, 8);
		try
		{

			bool cached;
			CGIResolver* r = new CGIResolver(this, ServerInstance, NotifyOpers, newipstr, false, user, user->GetFd(), "IDENT", cached);
			ServerInstance->AddResolver(r, cached);
		}
		catch (...)
		{
			user->InvalidateCache();

			if(NotifyOpers)
				 ServerInstance->SNO->WriteToSnoMask('a', "Connecting user %s detected as using CGI:IRC (%s), but I could not resolve their hostname!", user->nick.c_str(), user->host.c_str());
		}

		return true;
	}

	bool IsValidHost(const std::string &host)
	{
		if(!host.size())
			return false;

		for(unsigned int i = 0; i < host.size(); i++)
		{
			if(	((host[i] >= '0') && (host[i] <= '9')) ||
					((host[i] >= 'A') && (host[i] <= 'Z')) ||
					((host[i] >= 'a') && (host[i] <= 'z')) ||
					((host[i] == '-') && (i > 0) && (i+1 < host.size()) && (host[i-1] != '.') && (host[i+1] != '.')) ||
					((host[i] == '.') && (i > 0) && (i+1 < host.size())) )

				continue;
			else
				return false;
		}

		return true;
	}

	bool IsValidIP(const std::string &ip)
	{
		if(ip.size() < 7 || ip.size() > 15)
			return false;

		short sincedot = 0;
		short dots = 0;

		for(unsigned int i = 0; i < ip.size(); i++)
		{
			if((dots <= 3) && (sincedot <= 3))
			{
				if((ip[i] >= '0') && (ip[i] <= '9'))
				{
					sincedot++;
				}
				else if(ip[i] == '.')
				{
					sincedot = 0;
					dots++;
				}
			}
			else
			{
				return false;

			}
		}

		if(dots != 3)
			return false;

		return true;
	}

	virtual ~ModuleCgiIRC()
	{
	}

	virtual Version GetVersion()
	{
		return Version("$Id$",VF_VENDOR,API_VERSION);
	}

};

MODULE_INIT(ModuleCgiIRC)
