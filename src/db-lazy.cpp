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

#include "univdefs.h"
#include "db.h"
#include "db-intl.h"
#include "db-lazy.h"
#include "json-util.h"

db_lazy_stmt::db_lazy_stmt(sqlite3 *db, DBPSC_TYPE type) {
	stmt = DBInitialiseSql(db, dbpscache::GetQueryString(type));
}

sqlite3_stmt *db_lazy_stmt::LookupID(sqlite3 *db, uint64_t id, bool &refreshed) {
	sqlite3_stmt *s = stmt.stmt();
	if (id != current_id) {
		sqlite3_reset(s);
		sqlite3_bind_int64(s, 1, (sqlite3_int64) id);
		int res = sqlite3_step(s);
		if (res != SQLITE_ROW) {
			DBDoErr("db_lazy_stmt::LookupID", db, s, res);
		}
		current_id = id;
		refreshed = true;
	} else {
		refreshed = false;
	}
	return s;
}

db_lazy_tweet::db_lazy_tweet(sqlite3 *db_)
		: lstmt(db_, DBPSC_SELTWEET), db(db_) { }

void db_lazy_tweet::LoadTweetID(uint64_t id) {
	bool refreshed;
	lstmt.LookupID(db, id, refreshed);
	if (refreshed) {
		loaded_flags = 0;
	}
}

void db_lazy_tweet::GetStatJson() {
	if (NeedsLoading(LF::STATJSON)) {
		statjson_buffer = column_get_compressed_and_parse(lstmt.GetCurrentStmt(), 0, statjson);
	}
}

uint64_t db_lazy_tweet::GetUint64Generic(flagwrapper<LF> flag, uint64_t &value, int column) {
	if (NeedsLoading(flag)) {
		value = (uint64_t) sqlite3_column_int64(lstmt.GetCurrentStmt(), column);
	}

	return value;
}

const std::string &db_lazy_tweet::GetJsonStringGeneric(flagwrapper<LF> flag, std::string &value, const char *key) {
	if (NeedsLoading(flag)) {
		GetStatJson();
		if (statjson.IsObject()) parse_util::CheckTransJsonValueDef(value, statjson, key, "");
	}
	return value;
}

uint64_t db_lazy_tweet::GetUser() {
	return GetUint64Generic(LF::USER, user, 2);
}

uint64_t db_lazy_tweet::GetUserRecipient() {
	return GetUint64Generic(LF::USERRECIP, user_recipient, 3);
}

tweet_flags db_lazy_tweet::GetFlags() {
	return tweet_flags(GetUint64Generic(LF::FLAGS, flags, 4));
}

uint64_t db_lazy_tweet::GetRtid() {
	return GetUint64Generic(LF::RTID, rtid, 6);
}

const std::string &db_lazy_tweet::GetText() {
	return GetJsonStringGeneric(LF::TEXT, text, "text");
}

const std::string &db_lazy_tweet::GetSource() {
	return GetJsonStringGeneric(LF::SOURCE, source, "source");
}

db_lazy_user::db_lazy_user(sqlite3 *db_)
		: lstmt(db_, DBPSC_SELUSER), db(db_) { }

void db_lazy_user::LoadUserID(uint64_t id) {
	bool refreshed;
	lstmt.LookupID(db, id, refreshed);
	if (refreshed) {
		loaded_flags = 0;
	}
}

void db_lazy_user::GetUserJson() {
	if (NeedsLoading(LF::USERJSON)) {
		userjson_buffer = column_get_compressed_and_parse(lstmt.GetCurrentStmt(), 0, userjson);
	}
}

const std::string &db_lazy_user::GetJsonStringGeneric(flagwrapper<LF> flag, std::string &value, const char *key) {
	if (NeedsLoading(flag)) {
		GetUserJson();
		if (userjson.IsObject()) parse_util::CheckTransJsonValueDef(value, userjson, key, "");
	}
	return value;
}

const std::string &db_lazy_user::GetName() {
	return GetJsonStringGeneric(LF::NAME, name, "name");
}

const std::string &db_lazy_user::GetScreenName() {
	return GetJsonStringGeneric(LF::SCREENNAME, screen_name, "screen_name");
}

const std::string &db_lazy_user::GetDescription() {
	return GetJsonStringGeneric(LF::DESCRIPTION, description, "description");
}

const std::string &db_lazy_user::GetLocation() {
	return GetJsonStringGeneric(LF::LOCATION, location, "location");
}

const std::string &db_lazy_user::GetNotes() {
	return GetJsonStringGeneric(LF::NOTES, notes, "retcon_notes");
}

