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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_LOG
#define HGUARD_SRC_LOG

#include "univdefs.h"
#include "flags.h"
#include <wx/log.h>

enum class LOGT : unsigned int;
template<> struct enum_traits<LOGT> { static constexpr bool flags = true; };

enum class LOGT : unsigned int {
	ZERO               = 0,
	CURLVERB           = 1<<0,
	PARSE              = 1<<1,
	PARSEERR           = 1<<2,
	SOCKTRACE          = 1<<3,
	SOCKERR            = 1<<4,
	TPANEL             = 1<<5,
	NETACT             = 1<<6,
	DBTRACE            = 1<<7,
	DBERR              = 1<<8,
	ZLIBTRACE          = 1<<9,
	ZLIBERR            = 1<<10,
	OTHERTRACE         = 1<<11,
	OTHERERR           = 1<<12,
	USERREQ            = 1<<13,
	PENDTRACE          = 1<<14,
	WXLOG              = 1<<15,
	WXVERBOSE          = 1<<16,
	FILTERERR          = 1<<17,
	FILTERTRACE        = 1<<18,

	GROUP_ALL          = CURLVERB | PARSE | PARSEERR | SOCKTRACE | SOCKERR | TPANEL | NETACT | DBTRACE | DBERR | ZLIBTRACE | ZLIBERR |
	                     OTHERTRACE | OTHERERR | USERREQ | PENDTRACE | WXLOG | WXVERBOSE | FILTERERR | FILTERTRACE,
	GROUP_STR          = GROUP_ALL,
	GROUP_ERR          = SOCKERR | DBERR | ZLIBERR | PARSEERR | OTHERERR | WXLOG | FILTERERR,
	GROUP_LOGWINDEF    = GROUP_ERR | USERREQ,
};

extern LOGT currentlogflags;

void LogMsgRaw(LOGT logflags, const wxString &str);
void LogMsgProcess(LOGT logflags, const wxString &str);
void Update_currentlogflags();

#define LogMsg(l, s) if( currentlogflags & (l) ) LogMsgProcess(l, s)
#define LogMsgFormat(l, ...) if( currentlogflags & (l) ) LogMsgProcess(l, wxString::Format(__VA_ARGS__))

#endif
