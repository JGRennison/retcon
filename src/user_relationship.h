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

#ifndef HGUARD_SRC_USER_RELATIONSHIP
#define HGUARD_SRC_USER_RELATIONSHIP

#include "univdefs.h"

//flags for user_relationship::ur_flags
enum {
	URF_FOLLOWSME_KNOWN      = 1<<0,
	URF_FOLLOWSME_TRUE       = 1<<1,
	URF_IFOLLOW_KNOWN        = 1<<2,
	URF_IFOLLOW_TRUE         = 1<<3,
	URF_FOLLOWSME_PENDING    = 1<<4,
	URF_IFOLLOW_PENDING      = 1<<5,
	URF_QUERY_PENDING        = 1<<6,
};

struct user_relationship {
	unsigned int ur_flags;
	time_t followsme_updtime;	//if these are 0 and the corresponding known flag is set, then the value is known to be correct whilst the stream is still up
	time_t ifollow_updtime;
	user_relationship() : ur_flags(0), followsme_updtime(0), ifollow_updtime(0) { }
};

#endif
