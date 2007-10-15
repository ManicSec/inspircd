/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2007 InspIRCd Development Team
 * See: http://www.inspircd.org/wiki/index.php/Credits
 *
 * This program is free but copyrighted software; see
 *      the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#ifndef __CMD_RELOADMODULE_H__
#define __CMD_RELOADMODULE_H__

// include the common header files

#include "users.h"
#include "channels.h"

/** Handle /RELOADMODULE. These command handlers can be reloaded by the core,
 * and handle basic RFC1459 commands. Commands within modules work
 * the same way, however, they can be fully unloaded, where these
 * may not.
 */
class cmd_reloadmodule : public Command
{
 public:
	/** Constructor for reloadmodule.
	 */
	cmd_reloadmodule (InspIRCd* Instance) : Command(Instance,"RELOADMODULE",'o',1) { syntax = "<modulename>"; }
	/** Handle command.
	 * @param parameters The parameters to the comamnd
	 * @param pcnt The number of parameters passed to teh command
	 * @param user The user issuing the command
	 * @return A value from CmdResult to indicate command success or failure.
	 */
	CmdResult Handle(const char** parameters, int pcnt, User *user);
};

#endif
