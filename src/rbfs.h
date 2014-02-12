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

#ifndef HGUARD_SRC_RBFS
#define HGUARD_SRC_RBFS

#include "univdefs.h"

typedef enum {			//do not change these values, they are saved/loaded to/from the DB
	RBFS_NULL = 0,
	RBFS_MIN = 1,
	RBFS_TWEETS = 1,
	RBFS_MENTIONS,
	RBFS_RECVDM,
	RBFS_SENTDM,
	RBFS_USER_TIMELINE,
	RBFS_USER_FAVS,
	RBFS_MAX = RBFS_USER_FAVS,
} RBFS_TYPE;

struct restbackfillstate {
	uint64_t start_tweet_id;	//exclusive limit
	uint64_t end_tweet_id;		//inclusive limit
	uint64_t userid;
	unsigned int max_tweets_left;
	unsigned int lastop_recvcount;
	RBFS_TYPE type;
	bool read_again;
	bool started;
};

#endif

