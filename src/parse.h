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

#ifndef HGUARD_SRC_PARSE
#define HGUARD_SRC_PARSE

#include "univdefs.h"
#include "rapidjson-inc.h"
#include "json-common.h"
#include "twit-common.h"
#include "flags.h"
#include "rbfs.h"
#include <wx/version.h>
#include <wx/defs.h>
#include <wx/string.h>
#include <string>
#include <memory>
#include <vector>

struct dbsendmsg_list;
struct db_handle_msg_pending_guard;
struct taccount;
struct twitcurlext;
struct tweet;
struct userdata;
struct tpanelparentwin_userproplisting;
struct TwitterErrorMsg;

struct genjsonparser {
	enum class USERPARSERESULT {
		PROFIMG_UPDATED        = 1<<0,
		OTHER_UPDATED          = 1<<1,
	};
	enum class USERPARSEFLAGS {
		IS_SSL                = 1<<0,
		IS_DB_LOAD            = 1<<1,
	};

	static void ParseTweetStatics(const rapidjson::Value& val, tweet_ptr_p tobj,
			Handler *jw = 0, bool isnew = false, optional_observer_ptr<dbsendmsg_list> dbmsglist = nullptr, bool parse_entities = true);
	static void DoEntitiesParse(const rapidjson::Value& val, optional_observer_ptr<const rapidjson::Value> val_ex, tweet_ptr_p t,
			bool isnew = false, optional_observer_ptr<dbsendmsg_list> dbmsglist = nullptr);
	static flagwrapper<USERPARSERESULT> ParseUserContents(const rapidjson::Value& val, userdata &userobj, flagwrapper<USERPARSEFLAGS> flags);
	static void ParseTweetDyn(const rapidjson::Value& val, tweet_ptr_p tobj);
};
template<> struct enum_traits<genjsonparser::USERPARSERESULT> { static constexpr bool flags = true; };
template<> struct enum_traits<genjsonparser::USERPARSEFLAGS> { static constexpr bool flags = true; };

enum class JDTP {
	ISDM               = 1<<0,
	ISRTSRC            = 1<<1,
	FAV                = 1<<2,
	UNFAV              = 1<<3,
	DEL                = 1<<4,
	USERTIMELINE       = 1<<5,
	CHECKPENDINGONLY   = 1<<6,
	POSTDBLOAD         = 1<<7,
	ALWAYSREPARSE      = 1<<8,
	ISQUOTE            = 1<<9,
	ARRIVED            = 1<<10,
	TIMELINERECV       = 1<<11,

	SAVE_MASK          = ALWAYSREPARSE,
};
template<> struct enum_traits<JDTP> { static constexpr bool flags = true; };

struct jsonparser : public genjsonparser {
	std::shared_ptr<taccount> tac;

	//This will not be saved for deferred parses
	//This is saved for use of ProcessStreamResponse
	optional_observer_ptr<twitcurlext> twit;

	struct parse_data {
		std::string source_str;
		std::vector<char> json;
		rapidjson::Document doc;
		uint64_t rbfs_userid = 0;
		RBFS_TYPE rbfs_type = RBFS_NULL;
		flagwrapper<JDTP> base_sflags = 0;
		std::unique_ptr<db_handle_msg_pending_guard> db_pending_guard;
	};
	std::shared_ptr<parse_data> data;
	std::unique_ptr<dbsendmsg_list> dbmsglist;

	enum class OODPEF {
		HAVE_REAL_TIME       = 1<<0,
	};

	struct out_of_date_data : public intrusive_ptr_target_base {
		time_t time_estimate = 0;
		flagwrapper<OODPEF> flags = 0;

		void CheckEventToplevelJson(const rapidjson::Value& val);
	};

	jsonparser(std::shared_ptr<taccount> a, optional_observer_ptr<twitcurlext> tw = nullptr);
	~jsonparser();
	bool ParseString(std::string str);
	void SetData(std::shared_ptr<parse_data> data_) {
		data = std::move(data_);
	}

	// Methods below must only be used once data has been set using ParseString or SetData

	udc_ptr DoUserParse(const rapidjson::Value& val, flagwrapper<UMPTF> umpt_flags = 0, optional_cref_intrusive_ptr<out_of_date_data> out_of_date_state = nullptr);
	void DoEventParse(const rapidjson::Value& val);
	void DoFriendLookupParse(const rapidjson::Value& val);
	bool DoStreamTweetPreFilter(const rapidjson::Value& val);
	tweet_ptr DoTweetParse(const rapidjson::Value& val, flagwrapper<JDTP> sflags, optional_cref_intrusive_ptr<out_of_date_data> out_of_date_state = nullptr);
	void RestTweetUpdateParams(const tweet &t, optional_observer_ptr<restbackfillstate> rbfs);
	void RestTweetPreParseUpdateParams(optional_observer_ptr<restbackfillstate> rbfs);

	void ProcessTimelineResponse(flagwrapper<JDTP> sflags, optional_observer_ptr<restbackfillstate> rbfs);
	void ProcessUserTimelineResponse(optional_observer_ptr<restbackfillstate> rbfs);
	void ProcessStreamResponse(bool out_of_date_parse = false);
	void ProcessSingleTweetResponse(flagwrapper<JDTP> sflags);
	void ProcessAccVerifyResponse();
	void ProcessUserListResponse();
	void ProcessFriendLookupResponse();
	void ProcessUserLookupWinResponse();
	void ProcessGenericFriendActionResponse();
	void ProcessGenericUserFollowListResponse(observer_ptr<tpanelparentwin_userproplisting> win);
	void ProcessOwnFollowerListingResponse();
	std::string ProcessUploadMediaResponse();
	void ProcessTwitterErrorJson(std::vector<TwitterErrorMsg> &msgs);
};
template<> struct enum_traits<jsonparser::OODPEF> { static constexpr bool flags = true; };

#endif
