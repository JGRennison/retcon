//  retcon
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TPANEL_WID
#define HGUARD_SRC_TPANEL_WID

#include "univdefs.h"

enum {	//window IDs
	TPPWID_DETACH = 100,
	TPPWID_DUP,
	TPPWID_DETACHDUP,
	TPPWID_CLOSE,
	TPPWID_TOPBTN,
	TPPWID_SPLIT,
	TPPWID_MARKALLREADBTN,
	TPPWID_NEWESTUNREADBTN,
	TPPWID_OLDESTUNREADBTN,
	TPPWID_NEWESTHIGHLIGHTEDBTN,
	TPPWID_OLDESTHIGHLIGHTEDBTN,
	TPPWID_NEXT_NEWESTUNREADBTN,
	TPPWID_NEXT_OLDESTUNREADBTN,
	TPPWID_NEXT_NEWESTHIGHLIGHTEDBTN,
	TPPWID_NEXT_OLDESTHIGHLIGHTEDBTN,
	TPPWID_UNHIGHLIGHTALLBTN,
	TPPWID_MOREBTN,
	TPPWID_JUMPTONUM,
	TPPWID_JUMPTOID,
	TPPWID_JUMPTOTIME,
	TPPWID_TOGGLEHIDDEN,
	TPPWID_TOGGLEHIDEDELETED,
	TPPWID_TIMER_BATCHMODE,
	TPPWID_FILTERDLGBTN,
	TPPWID_TOGGLE_INTERSECT_UNREAD,
	TPPWID_TOGGLE_INTERSECT_HIGHLIGHTED,
	TPPWID_HIDE_ALL_THUMBS,
	TPPWID_SHOW_HIDE_THUMBS_NORMAL,
	TPPWID_SHOW_ALL_THUMBS,
};

#endif
