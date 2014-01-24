/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2009 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2007-2008 Craig Edwards <craigedwards@brainbox.cc>
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


#include <vector>
#include <string>
#include <map>
#include "inspircd.h"
#include "exitcodes.h"
#include "socketengine.h"
#include <sys/epoll.h>
#include <ulimit.h>
#include <iostream>
#define EP_DELAY 5

/** A specialisation of the SocketEngine class, designed to use linux 2.6 epoll().
 */
class EPollEngine : public SocketEngine
{
private:
	/** These are used by epoll() to hold socket events
	 */
	std::vector<struct epoll_event> events;
	int EngineHandle;
public:
	/** Create a new EPollEngine
	 */
	EPollEngine();
	/** Delete an EPollEngine
	 */
	virtual ~EPollEngine();
	virtual bool AddFd(EventHandler* eh, int event_mask);
	virtual void OnSetEvent(EventHandler* eh, int old_mask, int new_mask);
	virtual void DelFd(EventHandler* eh);
	virtual int DispatchEvents();
	virtual std::string GetName();
};

EPollEngine::EPollEngine() : events(1)
{
	int max = ulimit(4, 0);
	if (max > 0)
	{
		MAX_DESCRIPTORS = max;
	}
	else
	{
		ServerInstance->Logs->Log("SOCKET", LOG_DEFAULT, "ERROR: Can't determine maximum number of open sockets!");
		std::cout << "ERROR: Can't determine maximum number of open sockets!" << std::endl;
		ServerInstance->QuickExit(EXIT_STATUS_SOCKETENGINE);
	}

	// This is not a maximum, just a hint at the eventual number of sockets that may be polled.
	EngineHandle = epoll_create(GetMaxFds() / 4);

	if (EngineHandle == -1)
	{
		ServerInstance->Logs->Log("SOCKET", LOG_DEFAULT, "ERROR: Could not initialize socket engine: %s", strerror(errno));
		ServerInstance->Logs->Log("SOCKET", LOG_DEFAULT, "ERROR: Your kernel probably does not have the proper features. This is a fatal error, exiting now.");
		std::cout << "ERROR: Could not initialize epoll socket engine: " << strerror(errno) << std::endl;
		std::cout << "ERROR: Your kernel probably does not have the proper features. This is a fatal error, exiting now." << std::endl;
		ServerInstance->QuickExit(EXIT_STATUS_SOCKETENGINE);
	}
}

EPollEngine::~EPollEngine()
{
	this->Close(EngineHandle);
}

static unsigned mask_to_epoll(int event_mask)
{
	unsigned rv = 0;
	if (event_mask & (FD_WANT_POLL_READ | FD_WANT_POLL_WRITE | FD_WANT_SINGLE_WRITE))
	{
		// we need to use standard polling on this FD
		if (event_mask & (FD_WANT_POLL_READ | FD_WANT_FAST_READ))
			rv |= EPOLLIN;
		if (event_mask & (FD_WANT_POLL_WRITE | FD_WANT_FAST_WRITE | FD_WANT_SINGLE_WRITE))
			rv |= EPOLLOUT;
	}
	else
	{
		// we can use edge-triggered polling on this FD
		rv = EPOLLET;
		if (event_mask & (FD_WANT_FAST_READ | FD_WANT_EDGE_READ))
			rv |= EPOLLIN;
		if (event_mask & (FD_WANT_FAST_WRITE | FD_WANT_EDGE_WRITE))
			rv |= EPOLLOUT;
	}
	return rv;
}

bool EPollEngine::AddFd(EventHandler* eh, int event_mask)
{
	int fd = eh->GetFd();
	if ((fd < 0) || (fd > GetMaxFds() - 1))
	{
		ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "AddFd out of range: (fd: %d, max: %d)", fd, GetMaxFds());
		return false;
	}

	if (!SocketEngine::AddFd(eh))
	{
		ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "Attempt to add duplicate fd: %d", fd);
		return false;
	}

	struct epoll_event ev;
	memset(&ev,0,sizeof(ev));
	ev.events = mask_to_epoll(event_mask);
	ev.data.fd = fd;
	int i = epoll_ctl(EngineHandle, EPOLL_CTL_ADD, fd, &ev);
	if (i < 0)
	{
		ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "Error adding fd: %d to socketengine: %s", fd, strerror(errno));
		return false;
	}

	ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "New file descriptor: %d", fd);

	SocketEngine::SetEventMask(eh, event_mask);
	CurrentSetSize++;
	ResizeDouble(events);

	return true;
}

void EPollEngine::OnSetEvent(EventHandler* eh, int old_mask, int new_mask)
{
	unsigned old_events = mask_to_epoll(old_mask);
	unsigned new_events = mask_to_epoll(new_mask);
	if (old_events != new_events)
	{
		// ok, we actually have something to tell the kernel about
		struct epoll_event ev;
		memset(&ev,0,sizeof(ev));
		ev.events = new_events;
		ev.data.fd = eh->GetFd();
		epoll_ctl(EngineHandle, EPOLL_CTL_MOD, eh->GetFd(), &ev);
	}
}

void EPollEngine::DelFd(EventHandler* eh)
{
	int fd = eh->GetFd();
	if ((fd < 0) || (fd > GetMaxFds() - 1))
	{
		ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "DelFd out of range: (fd: %d, max: %d)", fd, GetMaxFds());
		return;
	}

	struct epoll_event ev;
	memset(&ev,0,sizeof(ev));
	ev.data.fd = fd;
	int i = epoll_ctl(EngineHandle, EPOLL_CTL_DEL, fd, &ev);

	if (i < 0)
	{
		ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "epoll_ctl can't remove socket: %s", strerror(errno));
	}

	SocketEngine::DelFd(eh);

	ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "Remove file descriptor: %d", fd);
	CurrentSetSize--;
}

int EPollEngine::DispatchEvents()
{
	socklen_t codesize = sizeof(int);
	int errcode;
	int i = epoll_wait(EngineHandle, &events[0], events.size(), 1000);
	ServerInstance->UpdateTime();

	TotalEvents += i;

	for (int j = 0; j < i; j++)
	{
		struct epoll_event& ev = events[j];

		EventHandler* eh = GetRef(ev.data.fd);
		if (!eh)
		{
			ServerInstance->Logs->Log("SOCKET", LOG_DEBUG, "Got event on unknown fd: %d", events[j].data.fd);
			epoll_ctl(EngineHandle, EPOLL_CTL_DEL, events[j].data.fd, &events[j]);
			continue;
		}

		if (ev.events & EPOLLHUP)
		{
			ErrorEvents++;
			eh->HandleEvent(EVENT_ERROR, 0);
			continue;
		}

		if (ev.events & EPOLLERR)
		{
			ErrorEvents++;
			/* Get error number */
			if (getsockopt(ev.data.fd, SOL_SOCKET, SO_ERROR, &errcode, &codesize) < 0)
				errcode = errno;
			eh->HandleEvent(EVENT_ERROR, errcode);
			continue;
		}

		int mask = eh->GetEventMask();
		if (ev.events & EPOLLIN)
			mask &= ~FD_READ_WILL_BLOCK;
		if (ev.events & EPOLLOUT)
		{
			mask &= ~FD_WRITE_WILL_BLOCK;
			if (mask & FD_WANT_SINGLE_WRITE)
			{
				int nm = mask & ~FD_WANT_SINGLE_WRITE;
				OnSetEvent(eh, mask, nm);
				mask = nm;
			}
		}
		SetEventMask(eh, mask);
		if (ev.events & EPOLLIN)
		{
			ReadEvents++;
			eh->HandleEvent(EVENT_READ);
			if (eh != GetRef(ev.data.fd))
				// whoa! we got deleted, better not give out the write event
				continue;
		}
		if (ev.events & EPOLLOUT)
		{
			WriteEvents++;
			eh->HandleEvent(EVENT_WRITE);
		}
	}

	return i;
}

std::string EPollEngine::GetName()
{
	return "epoll";
}

SocketEngine* CreateSocketEngine()
{
	return new EPollEngine;
}
