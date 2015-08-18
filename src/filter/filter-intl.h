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
//  2015 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_FILTER_FILTER_INTL
#define HGUARD_SRC_FILTER_FILTER_INTL

#include "../univdefs.h"
#include "filter.h"
#include "../db-lazy.h"
#include "../observer_ptr.h"
#include "../map.h"
#include "../undo.h"
#include <map>

struct filter_bulk_action {
	std::map<std::string, tweetidset> panel_to_add;
	std::map<std::string, tweetidset> panel_to_remove;

	struct flag_action {
		uint64_t old_flags;
		uint64_t new_flags;
	};
	container::map<uint64_t, flag_action> flag_actions;

	void execute(optional_observer_ptr<filter_bulk_action> undo_action);
};

struct filter_undo_action : public undo::action {
	filter_bulk_action bulk_action;

	virtual void execute() override {
		bulk_action.execute(nullptr);
	}
};

struct filter_db_lazy_state {
	db_lazy_tweet dl_tweet;
	db_lazy_tweet dl_rtsrc;
	db_lazy_user dl_user1;
	db_lazy_user dl_user2;

	filter_bulk_action bulk_action;

	filter_db_lazy_state(sqlite3 *db)
			: dl_tweet(db), dl_rtsrc(db), dl_user1(db), dl_user2(db) { }
};

struct generic_tweet_access_loaded {
	tweet &tw;

	generic_tweet_access_loaded(tweet &tw_)
			: tw(tw_) { }

	bool HasRT() const {
		return (bool) tw.rtsrc;
	}

	bool HasRecipUser() const {
		return (bool) tw.user_recipient;
	}

	db_lazy_tweet_compat_accessor GetTweet() {
		return db_lazy_tweet_compat_accessor(tw);
	}

	db_lazy_tweet_compat_accessor GetRetweet() {
		return db_lazy_tweet_compat_accessor(*tw.rtsrc);
	}

	db_lazy_user_compat_accessor GetUser() {
		return db_lazy_user_compat_accessor(tw.user.get());
	}

	db_lazy_user_compat_accessor GetRTUser() {
		return db_lazy_user_compat_accessor(tw.rtsrc->user.get());
	}

	db_lazy_user_compat_accessor GetRecipUser() {
		return db_lazy_user_compat_accessor(tw.user_recipient.get());
	}
};

struct generic_tweet_access_dblazy {
	filter_db_lazy_state &state;
	uint64_t tweet_id;

	generic_tweet_access_dblazy(filter_db_lazy_state &state_, uint64_t tweet_id_)
			: state(state_), tweet_id(tweet_id_) { }

	void LoadTweet() {
		state.dl_tweet.LoadTweetID(tweet_id);
	}

	bool HasRT() {
		LoadTweet();
		return state.dl_tweet.GetRtid();
	}

	bool HasRecipUser() {
		LoadTweet();
		return state.dl_tweet.GetUserRecipient();
	}

	db_lazy_tweet *GetTweet() {
		LoadTweet();
		return &state.dl_tweet;
	}

	db_lazy_tweet *GetRetweet() {
		LoadTweet();
		state.dl_rtsrc.LoadTweetID(state.dl_tweet.GetRtid());
		return &state.dl_rtsrc;
	}

	db_lazy_user *GetUser() {
		LoadTweet();
		state.dl_user1.LoadUserID(state.dl_tweet.GetUser());
		return &state.dl_user1;
	}

	db_lazy_user *GetRTUser() {
		state.dl_user2.LoadUserID(GetRetweet()->GetUser());
		return &state.dl_user2;
	}

	db_lazy_user *GetRecipUser() {
		LoadTweet();
		state.dl_user2.LoadUserID(state.dl_tweet.GetUserRecipient());
		return &state.dl_user2;
	}
};

#endif
