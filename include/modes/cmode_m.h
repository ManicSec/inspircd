/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  InspIRCd: (C) 2002-2007 InspIRCd Development Team
 * See: http://www.inspircd.org/wiki/index.php/Credits
 *
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

#include "mode.h"

class InspIRCd;

/** Channel mode +m
 */
class ModeChannelModerated : public ModeHandler
{
 public:
	ModeChannelModerated(InspIRCd* Instance);
	ModeAction OnModeChange(User* source, User* dest, Channel* channel, std::string &parameter, bool adding);
};
