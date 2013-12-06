//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TPANEL_COMMON
#define HGUARD_SRC_TPANEL_COMMON

#include "univdefs.h"
#include <vector>
#include <string>
#include <memory>
#include <wx/gdicmn.h>

struct taccount;

enum {
	TPAF_DM                   = 1<<8,
	TPAF_TW                   = 1<<9,
	TPAF_MN                   = 1<<10,
	TPAF_ALLACCS              = 1<<11,
	TPAF_MASK                 = 0xFF00,
};

struct tpanel_auto {
	unsigned int autoflags;
	std::shared_ptr<taccount> acc;
};

struct twin_layout_desc {
	unsigned int mainframeindex;
	unsigned int splitindex;
	unsigned int tabindex;
	std::vector<tpanel_auto> tpautos;
	std::string name;
	std::string dispname;
	unsigned int flags;
};

struct mf_layout_desc {
	unsigned int mainframeindex;
	wxPoint pos;
	wxSize size;
	bool maximised;
};

#endif

