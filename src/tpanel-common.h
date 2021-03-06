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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TPANEL_COMMON
#define HGUARD_SRC_TPANEL_COMMON

#include "univdefs.h"
#include "flags.h"
#include "ptr_types.h"
#include <vector>
#include <string>
#include <memory>
#include <wx/gdicmn.h>

struct taccount;
struct tweet;

enum class TPF {
	DELETEONWINCLOSE      = 1<<0,
	SAVETODB              = 1<<1,
	USER_TIMELINE         = 1<<2,
	MANUAL                = 1<<3,
	MASK                  = 0xFF,

	AUTO_DM               = 1<<8,
	AUTO_TW               = 1<<9,
	AUTO_MN               = 1<<10,
	AUTO_ALLACCS          = 1<<11,
	AUTO_NOACC            = 1<<12,
	AUTO_HIGHLIGHTED      = 1<<13,
	AUTO_UNREAD           = 1<<14,
	AUTO_MASK             = 0xFF00,
};
template<> struct enum_traits<TPF> { static constexpr bool flags = true; };

enum class TPF_INTERSECT {
	UNREAD                = 1<<0,
	HIGHLIGHTED           = 1<<1,
};
template<> struct enum_traits<TPF_INTERSECT> { static constexpr bool flags = true; };

enum {
	TPF_AUTO_SHIFT        = 8,
};

struct tpanel_auto {
	flagwrapper<TPF> autoflags;
	std::shared_ptr<taccount> acc;
};

enum class TPFU {
	DMSET                 = 1<<0,
};
template<> struct enum_traits<TPFU> { static constexpr bool flags = true; };

struct tpanel_auto_udc {
	flagwrapper<TPFU> autoflags;
	udc_ptr u;
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
	HIDEALLTHUMBS         = 1<<7,
	SHOWALLTHUMBS         = 1<<8,
	SHOWTIMELINEHIDDEN    = 1<<9,

	DB_SAVE_MASK          = SHOWHIDDEN | SHOWDELETED | SHOWTIMELINEHIDDEN,
};
template<> struct enum_traits<TPPWF> { static constexpr bool flags = true; };

struct twin_layout_desc {
	unsigned int mainframeindex;
	unsigned int splitindex;
	unsigned int tabindex;
	std::vector<tpanel_auto> tpautos;
	std::vector<tpanel_auto_udc> tpudcautos;
	std::string name;
	std::string dispname;
	flagwrapper<TPF> flags;
	flagwrapper<TPF_INTERSECT> intersect_flags;
	flagwrapper<TPPWF> tppw_flags;
};

enum class TPANEL_IS_ACC_TIMELINE {
	YES                   = 1<<0,
	NO                    = 1<<1,

	PARTIAL               = YES | NO,
};
template<> struct enum_traits<TPANEL_IS_ACC_TIMELINE> { static constexpr bool flags = true; };

void UpdateTweet(const tweet &t, bool redrawimg = false);
void UpdateTweet(uint64_t id, bool redrawimg = false);
void UpdateAllTweets(bool redrawimg = false, bool resethighlight = false);
void UpdateUsersTweet(uint64_t userid, bool redrawimg = false);

#endif

