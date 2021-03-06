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
	auto it = userconts.insert(std::make_pair(id, udc_ptr()));
	udc_ptr &usercont = it.first->second;
	if (it.second) {
		//new user
		usercont.reset(new userdatacontainer());
		usercont->id = id;
	}
	return usercont;
}

optional_udc_ptr alldata::GetExistingUserContainerById(uint64_t id) {
	auto it = userconts.find(id);
	if (it != userconts.end()) {
		return it->second;
	} else {
		return nullptr;
	}
}

tweet_ptr alldata::GetTweetById(uint64_t id, bool *isnew) {
	auto it = tweetobjs.insert(std::make_pair(id, tweet_ptr()));
	if (isnew) *isnew = it.second;
	tweet_ptr &t = it.first->second;
	if (it.second) {
		//new tweet
		t.reset(new tweet());
		t->id = id;
	}
	return t;
}

optional_tweet_ptr alldata::GetExistingTweetById(uint64_t id) {
	auto it = tweetobjs.find(id);
	if (it != tweetobjs.end()) {
		return it->second;
	} else {
		return nullptr;
	}
}

void alldata::UnlinkTweetById(uint64_t id) {
	tweetobjs.erase(id);
}

optional_observer_ptr<user_dm_index> alldata::GetExistingUserDMIndexById(uint64_t id) {
	auto it = user_dm_indexes.find(id);
	if (it != user_dm_indexes.end()) {
		return &(it->second);
	}
	return nullptr;
}

user_dm_index &alldata::GetUserDMIndexById(uint64_t id) {
	return user_dm_indexes[id];
}

void alldata::AddRecentMediaSavePath(wxString path) {
	recent_media_save_paths.erase(std::remove(recent_media_save_paths.begin(), recent_media_save_paths.end(), path), recent_media_save_paths.end());
	if (recent_media_save_paths.size() >= 10) {
		recent_media_save_paths.pop_back();
	}
	recent_media_save_paths.emplace(recent_media_save_paths.begin(), std::move(path));
}
