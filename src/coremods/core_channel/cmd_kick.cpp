/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007 Robin Burchell <robin+git@viroteck.net>
 *
 * This file is part of InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "inspircd.h"
#include "core_channel.h"

CommandKick::CommandKick(Module* parent)
	: Command(parent, "KICK", 2, 3)
{
	syntax = "<channel> <nick>{,<nick>} [<reason>]";
}

/** Handle /KICK
 */
CmdResult CommandKick::Handle (const std::vector<std::string>& parameters, User *user)
{
	std::string reason;
	Channel* c = ServerInstance->FindChan(parameters[0]);
	User* u;

	if (CommandParser::LoopCall(user, this, parameters, 1))
		return CMD_SUCCESS;

	if (IS_LOCAL(user))
		u = ServerInstance->FindNickOnly(parameters[1]);
	else
		u = ServerInstance->FindNick(parameters[1]);

	if ((!u) || (!c) || (u->registered != REG_ALL))
	{
		user->WriteNumeric(ERR_NOSUCHNICK, "%s :No such nick/channel", c ? parameters[1].c_str() : parameters[0].c_str());
		return CMD_FAILURE;
	}

	Membership* srcmemb = NULL;
	if (IS_LOCAL(user))
	{
		srcmemb = c->GetUser(user);
		if (!srcmemb)
		{
			user->WriteNumeric(ERR_NOTONCHANNEL, "%s :You're not on that channel!", parameters[0].c_str());
			return CMD_FAILURE;
		}

		if (u->server->IsULine())
		{
			user->WriteNumeric(ERR_CHANOPRIVSNEEDED, "%s :You may not kick a u-lined client", c->name.c_str());
			return CMD_FAILURE;
		}
	}

	Membership* const memb = c->GetUser(u);
	if (!memb)
	{
		user->WriteNumeric(ERR_USERNOTINCHANNEL, "%s %s :They are not on that channel", u->nick.c_str(), c->name.c_str());
		return CMD_FAILURE;
	}

	if (parameters.size() > 2)
	{
		reason.assign(parameters[2], 0, ServerInstance->Config->Limits.MaxKick);
	}
	else
	{
		reason.assign(user->nick, 0, ServerInstance->Config->Limits.MaxKick);
	}

	// Do the following checks only if the KICK is done by a local user;
	// each server enforces its own rules.
	if (srcmemb)
	{
		// Modules are allowed to explicitly allow or deny kicks done by local users
		ModResult res;
		FIRST_MOD_RESULT(OnUserPreKick, res, (user, memb, reason));
		if (res == MOD_RES_DENY)
			return CMD_FAILURE;

		if (res == MOD_RES_PASSTHRU)
		{
			unsigned int them = srcmemb->getRank();
			unsigned int req = HALFOP_VALUE;
			for (std::string::size_type i = 0; i < memb->modes.length(); i++)
			{
				ModeHandler* mh = ServerInstance->Modes->FindMode(memb->modes[i], MODETYPE_CHANNEL);
				if (mh && mh->GetLevelRequired() > req)
					req = mh->GetLevelRequired();
			}

			if (them < req)
			{
				user->WriteNumeric(ERR_CHANOPRIVSNEEDED, "%s :You must be a channel %soperator",
					this->name.c_str(), req > HALFOP_VALUE ? "" : "half-");
				return CMD_FAILURE;
			}
		}
	}

	c->KickUser(user, u, reason);

	return CMD_SUCCESS;
}

RouteDescriptor CommandKick::GetRouting(User* user, const std::vector<std::string>& parameters)
{
	return (IS_LOCAL(user) ? ROUTE_LOCALONLY : ROUTE_BROADCAST);
}