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

#ifndef HGUARD_SRC_TWITCURLEXT_COMMON
#define HGUARD_SRC_TWITCURLEXT_COMMON

#include "univdefs.h"
#include "flags.h"

//for post_action_flags
enum class PAF {
	RESOLVE_PENDINGS            = 1<<0,
	STREAM_CONN_READ_BACKFILL   = 1<<1,
};
template<> struct enum_traits<PAF> { static constexpr bool flags = true; };

#endif
