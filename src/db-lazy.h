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

#ifndef HGUARD_SRC_DB_LAZY
#define HGUARD_SRC_DB_LAZY

#include "univdefs.h"
#include "db.h"
#include "db-intl.h"
#include "flags.h"
#include <vector>

class db_lazy_stmt {
	// This assumes that 0 is not a valid ID
	// This is true for users and tweets
	scoped_stmt_holder stmt;
	uint64_t current_id = 0;

	public:
	db_lazy_stmt(sqlite3 *db, DBPSC_TYPE type);
	sqlite3_stmt *LookupID(sqlite3 *db, uint64_t id, bool &refreshed);
	sqlite3_stmt *GetCurrentStmt() {
		return stmt.stmt();
	}
	uint64_t GetCurrentID() const {
		return current_id;
	}
};

class db_lazy_tweet {
	db_lazy_stmt lstmt;
	sqlite3 *db;

	rapidjson::Document statjson;
	db_bind_buffer<dbb_uncompressed> statjson_buffer;
	uint64_t user;
	uint64_t user_recipient;
	uint64_t flags;
	uint64_t rtid;
	std::string text;
	std::string source;

	enum class LF {
		STATJSON             = 1<<0,
		USER                 = 1<<1,
		USERRECIP            = 1<<3,
		FLAGS                = 1<<4,
		RTID                 = 1<<5,
		TEXT                 = 1<<6,
		SOURCE               = 1<<7,
	};
	flagwrapper<LF> loaded_flags = 0;

	bool NeedsLoading(flagwrapper<LF> flag) {
		if (loaded_flags & flag) {
			return false;
		}
		loaded_flags |= flag;
		return true;
	}

	void GetStatJson();
	uint64_t GetUint64Generic(flagwrapper<LF> flag, uint64_t &value, int column);
	const std::string &GetJsonStringGeneric(flagwrapper<LF> flag, std::string &value, const char *key);

	public:
	db_lazy_tweet(sqlite3 *db_);

	void LoadTweetID(uint64_t id);

	uint64_t GetUser();
	uint64_t GetUserRecipient();
	uint64_t GetRtid();

	// common functions below

	uint64_t GetCurrentTweetID() const {
		return lstmt.GetCurrentID();
	}

	const std::string &GetText();
	const std::string &GetSource();

	tweet_flags GetFlags();
	void SetFlags(uint64_t f) {
		flags = f;
	}
};
template<> struct enum_traits<db_lazy_tweet::LF> { static constexpr bool flags = true; };

class db_lazy_tweet_compat_accessor {
	tweet &tw;

	public:
	db_lazy_tweet_compat_accessor(tweet &tw_)
			: tw(tw_) { }

	db_lazy_tweet_compat_accessor *operator->() noexcept {
		return this;
	}

	// common functions below

	uint64_t GetCurrentTweetID() const {
		return tw.id;
	}

	const std::string &GetText() {
		return tw.text;
	}

	const std::string &GetSource() {
		return tw.source;
	}

	tweet_flags GetFlags() {
		return tw.flags;
	}

	void SetFlags(uint64_t f) {
		tw.flags = tweet_flags(f);
	}
};

class db_lazy_user {
	db_lazy_stmt lstmt;
	sqlite3 *db;

	rapidjson::Document userjson;
	db_bind_buffer<dbb_uncompressed> userjson_buffer;
	std::string name;
	std::string screen_name;
	std::string description;
	std::string location;
	std::string notes;

	enum class LF {
		USERJSON             = 1<<0,
		NAME                 = 1<<1,
		SCREENNAME           = 1<<2,
		DESCRIPTION          = 1<<3,
		LOCATION             = 1<<4,
		NOTES                = 1<<5,
	};
	flagwrapper<LF> loaded_flags = 0;

	bool NeedsLoading(flagwrapper<LF> flag) {
		if (loaded_flags & flag) {
			return false;
		}
		loaded_flags |= flag;
		return true;
	}

	void GetUserJson();
	const std::string &GetJsonStringGeneric(flagwrapper<LF> flag, std::string &value, const char *key);

	public:
	db_lazy_user(sqlite3 *db_);

	// non-copyable/movable
	db_lazy_user(const db_lazy_user& other) = delete;
	db_lazy_user& operator=(const db_lazy_user&) = delete;

	void LoadUserID(uint64_t id);

	// common functions below

	bool IsValid() const {
		return true;
	}

	uint64_t GetCurrentUserID() const {
		return lstmt.GetCurrentID();
	}

	const std::string &GetName();
	const std::string &GetScreenName();
	const std::string &GetDescription();
	const std::string &GetLocation();
	const std::string &GetNotes();

	unsigned int GetRevisionNumber() {
		return 1;
	}
};
template<> struct enum_traits<db_lazy_user::LF> { static constexpr bool flags = true; };

class db_lazy_user_compat_accessor {
	userdatacontainer *u;

	public:
	db_lazy_user_compat_accessor(userdatacontainer *u_)
			: u(u_) { }

	db_lazy_user_compat_accessor *operator->() noexcept {
		return this;
	}

	// common functions below

	bool IsValid() const {
		return u;
	}

	uint64_t GetCurrentUserID() const {
		return u->id;
	}

	const std::string &GetName() {
		return u->GetUser().name;
	}

	const std::string &GetScreenName() {
		return u->GetUser().screen_name;
	}

	const std::string &GetDescription() {
		return u->GetUser().description;
	}

	const std::string &GetLocation() {
		return u->GetUser().location;
	}

	const std::string &GetNotes() {
		return u->GetUser().notes;
	}

	unsigned int GetRevisionNumber() {
		return u->GetUser().revision_number;
	}
};


#endif
