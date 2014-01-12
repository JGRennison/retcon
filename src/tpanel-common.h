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
#include "flags.h"
#include <vector>
#include <string>
#include <memory>
#include <wx/gdicmn.h>

struct taccount;

enum class TPF {
	DELETEONWINCLOSE      = 1<<0,
	SAVETODB              = 1<<1,
	USER_TIMELINE         = 1<<2,
	MASK                  = 0xFF,

	AUTO_DM               = 1<<8,
	AUTO_TW               = 1<<9,
	AUTO_MN               = 1<<10,
	AUTO_ALLACCS          = 1<<11,
	AUTO_MASK             = 0xFF00,

	INTL_CUSTOMAUTO       = 1<<24,
};
template<> struct enum_traits<TPF> { static constexpr bool flags = true; };

enum {
	TPF_AUTO_SHIFT        = 8,
};

struct tpanel_auto {
	flagwrapper<TPF> autoflags;
	std::shared_ptr<taccount> acc;
};

struct twin_layout_desc {
	unsigned int mainframeindex;
	unsigned int splitindex;
	unsigned int tabindex;
	std::vector<tpanel_auto> tpautos;
	std::string name;
	std::string dispname;
	flagwrapper<TPF> flags;
};

struct mf_layout_desc {
	unsigned int mainframeindex;
	wxPoint pos;
	wxSize size;
	bool maximised;
};

enum class PUSHFLAGS {	//for pushflags
	DEFAULT              = 0,
	ABOVE                = 1<<0,
	BELOW                = 1<<1,
	USERTL               = 1<<2,
	SETNOUPDATEFLAG      = 1<<3,
	NOINCDISPOFFSET      = 1<<4,
	CHECKSCROLLTOID      = 1<<5,
};
template<> struct enum_traits<PUSHFLAGS> { static constexpr bool flags = true; };

enum class TPPWF {	//for tppw_flags
	NOUPDATEONPUSH        = 1<<0,
	CANALWAYSSCROLLDOWN   = 1<<1,
	CLABELUPDATEPENDING   = 1<<2,
	SHOWHIDDEN            = 1<<3,
	SHOWDELETED           = 1<<4,
	FROZEN                = 1<<5,
	BATCHTIMERMODE        = 1<<6,
};
template<> struct enum_traits<TPPWF> { static constexpr bool flags = true; };

#endif

