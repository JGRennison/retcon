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
#include "observer_ptr.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <map>
#include <functional>
#include "map.h"
#include "hash_map.h"

struct tweet;
struct media_entity;
struct user_dm_index;

struct alldata {
	container::hash_map<uint64_t, udc_ptr> userconts;
	container::hash_map<uint64_t, tweet_ptr> tweetobjs;
	container::hash_map<std::string, std::shared_ptr<tpanel> > tpanels;
	container::hash_map<media_id_type, std::unique_ptr<media_entity> > media_list;
	container::hash_map<std::string, observer_ptr<media_entity> > img_media_map;
	container::hash_map<uint64_t, tweet_ptr> noacc_pending_tweetobjs;
	container::hash_map<uint64_t, udc_ptr> noacc_pending_userconts;
	std::unordered_map<uint64_t, user_dm_index> user_dm_indexes;
	tweetidset unloaded_db_tweet_ids;
	tweetidset loaded_db_tweet_ids;
	useridset unloaded_db_user_ids;
	unsigned int next_media_id = 1;
	cached_id_sets cids;
	std::vector<twin_layout_desc> twinlayout;
	std::vector<mf_layout_desc> mflayout;
	bool twinlayout_final = false;
	filter_set incoming_filter;
	filter_set alltweet_filter;
	std::multimap<uint64_t, std::function<void(udc_ptr_p)>> user_load_pending_funcs;
	safe_observer_ptr_container<handlenew_pending_op> handlenew_pending_ops;
	std::vector<wxString> recent_media_save_paths;

	udc_ptr GetUserContainerById(uint64_t id);
	optional_udc_ptr GetExistingUserContainerById(uint64_t id);
	tweet_ptr GetTweetById(uint64_t id, bool *isnew = nullptr);
	optional_tweet_ptr GetExistingTweetById(uint64_t id);
	void UnlinkTweetById(uint64_t id);
	optional_observer_ptr<user_dm_index> GetExistingUserDMIndexById(uint64_t id);
	user_dm_index &GetUserDMIndexById(uint64_t id);
	void AddRecentMediaSavePath(wxString path);
};

extern alldata ad;

#endif
