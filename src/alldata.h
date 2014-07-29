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

#ifndef HGUARD_SRC_ALLDATA
#define HGUARD_SRC_ALLDATA

#include "univdefs.h"
#include "twit.h"
#include "media_id_type.h"
#include "tpanel-common.h"
#include "filter/filter.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <map>
#include "map.h"

struct tweet;
struct media_entity;

struct alldata {
	std::unordered_map<uint64_t, userdatacontainer> userconts;
	container::map<uint64_t, tweet_ptr> tweetobjs;
	std::map<std::string, std::shared_ptr<tpanel> > tpanels;
	std::unordered_map<media_id_type, std::unique_ptr<media_entity> > media_list;
	std::unordered_map<std::string, observer_ptr<media_entity> > img_media_map;
	container::map<uint64_t, tweet_ptr> noacc_pending_tweetobjs;
	container::map<uint64_t, udc_ptr> noacc_pending_userconts;
	tweetidset unloaded_db_tweet_ids;
	unsigned int next_media_id = 1;
	cached_id_sets cids;
	std::vector<twin_layout_desc> twinlayout;
	std::vector<mf_layout_desc> mflayout;
	bool twinlayout_final = false;
	filter_set incoming_filter;
	filter_set alltweet_filter;

	udc_ptr GetUserContainerById(uint64_t id);
	udc_ptr GetExistingUserContainerById(uint64_t id);
	tweet_ptr GetTweetById(uint64_t id, bool *isnew = nullptr);
	tweet_ptr GetExistingTweetById(uint64_t id);
	void UnlinkTweetById(uint64_t id);
};

extern alldata ad;

#endif
