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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "alldata.h"

alldata ad;

udc_ptr alldata::GetUserContainerById(uint64_t id) {
	auto it = userconts.insert(std::make_pair(id, userdatacontainer()));
	udc_ptr usercont = &(it.first->second);
	if(it.second) {
		//new user
		usercont->id = id;
		usercont->lastupdate = 0;
		usercont->udc_flags = 0;
	}
	return std::move(usercont);
}

udc_ptr alldata::GetExistingUserContainerById(uint64_t id) {
	auto it = userconts.find(id);
	if(it != userconts.end()) {
		return &(it->second);
	}
	else {
		return nullptr;
	}
}

tweet_ptr alldata::GetTweetById(uint64_t id, bool *isnew) {
	auto it = tweetobjs.insert(std::make_pair(id, tweet_ptr()));
	if(isnew) *isnew = it.second;
	tweet_ptr &t = it.first->second;
	if(it.second) {
		//new tweet
		t.reset(new tweet());
		t->id = id;
	}
	return t;
}

tweet_ptr alldata::GetExistingTweetById(uint64_t id) {
	auto it = tweetobjs.find(id);
	if(it != tweetobjs.end()) {
		return it->second;
	}
	else {
		return nullptr;
	}
}

void alldata::UnlinkTweetById(uint64_t id) {
	tweetobjs.erase(id);
}

const tweetidset alldata::empty_tweetidset;
