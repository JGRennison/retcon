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

#ifndef HGUARD_SRC_ALLDATA
#define HGUARD_SRC_ALLDATA

#include "univdefs.h"
#include "twit.h"
#include "tpanel-common.h"
#include "filter/filter.h"
#include <unordered_map>
#include <string>
#include <memory>
#include <map>

struct tpanel;

struct alldata {
	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > userconts;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetobjs;
	std::map<std::string,std::shared_ptr<tpanel> > tpanels;
	std::unordered_map<media_id_type,media_entity> media_list;
	std::unordered_map<std::string,media_id_type> img_media_map;
	unsigned int next_media_id;
	cached_id_sets cids;
	std::vector<twin_layout_desc> twinlayout;
	std::vector<mf_layout_desc> mflayout;
	bool twinlayout_final = false;
	filter_set incoming_filter;

	std::shared_ptr<userdatacontainer> &GetUserContainerById(uint64_t id);
	std::shared_ptr<userdatacontainer> *GetExistingUserContainerById(uint64_t id);
	std::shared_ptr<tweet> &GetTweetById(uint64_t id, bool *isnew = 0);
	std::shared_ptr<tweet> *GetExistingTweetById(uint64_t id);
	void UnlinkTweetById(uint64_t id);

	alldata() : next_media_id(1) { }
};

extern alldata ad;

#endif
