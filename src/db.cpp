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
#include "db.h"
#include "db-intl.h"
#include "db-cfg.h"
#include "taccount.h"
#include "log.h"
#include "twit.h"
#include "util.h"
#include "rapidjson-inc.h"
#include "twitcurlext.h"
#include "alldata.h"
#include "parse.h"
#include "json-util.h"
#include "tpanel.h"
#include "tpanel-data.h"
#include "set.h"
#include "map.h"
#include "raii.h"
#include "retcon.h"
#ifdef __WINDOWS__
#include <windows.h>
#endif
#ifdef _GNU_SOURCE
#include <pthread.h>
#endif
#include <zlib.h>
#include <wx/msgdlg.h>
#include <wx/filefn.h>

#ifndef DB_COPIOUS_LOGGING
#define DB_COPIOUS_LOGGING 0
#endif

dbconn dbc;

//don't modify these
static const unsigned char jsondictionary[] = "<a href=\"http://retweet_countsourcetextentitiesindiceshashtagsurlsdisplayexpandedjpgpnguser_mentionsmediaidhttptweetusercreatedfavoritedscreen_namein_reply_to_user_idprofileprotectedfollowdescriptionfriends"
		"typesizesthe[{\",\":\"}]";
static const unsigned char profimgdictionary[] = "http://https://si0.twimg.com/profile_images/imagesmallnormal.png.jpg.jpeg.gif";


/* This is retained only for backwards compatibility */

struct esctabledef {
	unsigned char id;
	const char *text;
};

struct esctable {
	unsigned char tag;
	const esctabledef *start;
	size_t count;
};

//never remove or change an entry in these tables
static esctabledef dynjsondefs[] = {
	{ 1, "{\"p\":[{\"f\":1,\"a\":1}]}" },
	{ 2, "{\"p\":[{\"f\":1,\"a\":2}]}" },
	{ 3, "{\"p\":[{\"f\":1,\"a\":3}]}" },
	{ 4, "{\"p\":[{\"f\":1,\"a\":4}]}" },
	{ 5, "{\"p\":[{\"f\":1,\"a\":5}]}" },
	{ 6, "{\"p\":[{\"f\":1,\"a\":6}]}" },
};

static esctable allesctables[] = {
	{'S', dynjsondefs, sizeof(dynjsondefs)/sizeof(esctabledef) },
};

/* ends */

static const char *startup_sql=
"PRAGMA locking_mode = EXCLUSIVE;"
"BEGIN EXCLUSIVE;"
"CREATE TABLE IF NOT EXISTS tweets(id INTEGER PRIMARY KEY NOT NULL, statjson BLOB, dynjson BLOB, userid INTEGER, userrecipid INTEGER, flags INTEGER, timestamp INTEGER, rtid INTEGER);"
"CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY NOT NULL, json BLOB, cachedprofimgurl BLOB, createtimestamp INTEGER, lastupdatetimestamp INTEGER, cachedprofileimgchecksum BLOB, mentionindex BLOB, profimglastusedtimestamp INTEGER);"
"CREATE TABLE IF NOT EXISTS acc(id INTEGER PRIMARY KEY NOT NULL, name TEXT, dispname TEXT, json BLOB, tweetids BLOB, dmids BLOB, blockedids BLOB, mutedids BLOB, nortids BLOB, userid INTEGER);"
"CREATE TABLE IF NOT EXISTS settings(accid BLOB, name TEXT, value BLOB, PRIMARY KEY (accid, name));"
"CREATE TABLE IF NOT EXISTS rbfspending(accid INTEGER, type INTEGER, startid INTEGER, endid INTEGER, maxleft INTEGER);"
"CREATE TABLE IF NOT EXISTS mediacache(mid INTEGER, tid INTEGER, url BLOB, fullchecksum BLOB, thumbchecksum BLOB, flags INTEGER, lastusedtimestamp INTEGER, PRIMARY KEY (mid, tid));"
"CREATE TABLE IF NOT EXISTS tpanelwins(mainframeindex INTEGER, splitindex INTEGER, tabindex INTEGER, name TEXT, dispname TEXT, flags INTEGER, intersect_flags INTEGER, tppw_flags INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanelwinautos(tpw INTEGER, accid INTEGER, autoflags INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanelwinudcautos(tpw INTEGER, userid INTEGER, autoflags INTEGER);"
"CREATE TABLE IF NOT EXISTS mainframewins(mainframeindex INTEGER, x INTEGER, y INTEGER, w INTEGER, h INTEGER, maximised INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanels(name TEXT, dispname TEXT, flags INTEGER, ids BLOB);"
"CREATE TABLE IF NOT EXISTS staticsettings(name TEXT PRIMARY KEY NOT NULL, value BLOB);"
"CREATE TABLE IF NOT EXISTS userrelationships(accid INTEGER, userid INTEGER, flags INTEGER, followmetime INTEGER, ifollowtime INTEGER);"
"CREATE TABLE IF NOT EXISTS userdmsets(userid INTEGER PRIMARY KEY NOT NULL, dmindex BLOB);"
"CREATE TABLE IF NOT EXISTS handlenewpending(accid INTEGER, arrivalflags INTEGER, tweetid INTEGER);"
"CREATE TABLE IF NOT EXISTS incrementaltweetids(id INTEGER PRIMARY KEY NOT NULL);"
"CREATE TABLE IF NOT EXISTS eventlog(id INTEGER PRIMARY KEY NOT NULL, accid INTEGER, type INTEGER, flags INTEGER, obj INTEGER, timestamp INTEGER, extrajson BLOB);"
"CREATE TABLE IF NOT EXISTS tweetxref(fromid INTEGER, toid INTEGER, PRIMARY KEY (fromid, toid));"
"CREATE INDEX IF NOT EXISTS tweetxref_index1 ON tweetxref (fromid);"
"CREATE INDEX IF NOT EXISTS tweetxref_index2 ON tweetxref (toid);"
"CREATE INDEX IF NOT EXISTS tweets_ts_index ON tweets (timestamp);"
"CREATE INDEX IF NOT EXISTS eventlog_obj_index ON eventlog (obj);"
"INSERT OR REPLACE INTO staticsettings(name, value) VALUES ('dirtyflag', strftime('%s','now'));"
"COMMIT;";

static const char *std_sql_stmts[DBPSC_NUM_STATEMENTS]={
	"INSERT OR REPLACE INTO tweets(id, statjson, dynjson, userid, userrecipid, flags, timestamp, rtid) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
	"UPDATE tweets SET dynjson = ?, flags = ? WHERE id == ?;",
	"BEGIN;",
	"COMMIT;",
	"INSERT OR REPLACE INTO users(id, json, cachedprofimgurl, createtimestamp, lastupdatetimestamp, cachedprofileimgchecksum, mentionindex, profimglastusedtimestamp) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
	"INSERT INTO acc(name, dispname, userid) VALUES (?, ?, ?);",
	"UPDATE acc SET tweetids = ?, dmids = ?, blockedids = ?, mutedids = ?, nortids = ?, dispname = ? WHERE id == ?;",
	"SELECT statjson, dynjson, userid, userrecipid, flags, timestamp, rtid FROM tweets WHERE id == ?;",
	"INSERT INTO rbfspending(accid, type, startid, endid, maxleft) VALUES (?, ?, ?, ?, ?);",
	"SELECT url, fullchecksum, thumbchecksum, flags, lastusedtimestamp FROM mediacache WHERE (mid == ? AND tid == ?);",
	"INSERT OR IGNORE INTO mediacache(mid, tid, url, lastusedtimestamp) VALUES (?, ?, ?, ?);",
	"UPDATE OR IGNORE mediacache SET thumbchecksum = ? WHERE (mid == ? AND tid == ?);",
	"UPDATE OR IGNORE mediacache SET fullchecksum = ? WHERE (mid == ? AND tid == ?);",
	"UPDATE OR IGNORE mediacache SET flags = ? WHERE (mid == ? AND tid == ?);",
	"UPDATE OR IGNORE mediacache SET lastusedtimestamp = ? WHERE (mid == ? AND tid == ?);",
	"DELETE FROM acc WHERE id == ?;",
	"UPDATE tweets SET flags = ? | (flags & ?) WHERE id == ?;",
	"INSERT OR REPLACE INTO settings(accid, name, value) VALUES (?, ?, ?);",
	"DELETE FROM settings WHERE (accid IS ?) AND (name IS ?);",
	"SELECT value FROM settings WHERE (accid IS ?) AND (name IS ?);",
	"INSERT OR REPLACE INTO staticsettings(name, value) VALUES (?, ?);",
	"DELETE FROM staticsettings WHERE (name IS ?);",
	"SELECT value FROM staticsettings WHERE (name IS ?);",
	"INSERT INTO tpanels (name, dispname, flags, ids) VALUES (?, ?, ?, ?);",
	"INSERT OR REPLACE INTO userdmsets (userid, dmindex) VALUES (?, ?);",
	"SELECT json, cachedprofimgurl, createtimestamp, lastupdatetimestamp, cachedprofileimgchecksum, mentionindex, profimglastusedtimestamp FROM users WHERE id == ?;",
	"DELETE FROM handlenewpending;",
	"INSERT INTO handlenewpending (accid, arrivalflags, tweetid) VALUES (?, ?, ?);",
	"INSERT OR IGNORE INTO incrementaltweetids(id) VALUES (?);",
	"INSERT INTO eventlog(accid, type, flags, obj, timestamp, extrajson) VALUES (?, ?, ?, ?, ?, ?);",
	"INSERT INTO tweetxref(fromid, toid) VALUES (?, ?);",
	"SELECT id FROM tweets WHERE timestamp < ? ORDER BY timestamp DESC LIMIT 1;",
	"SELECT id, accid, type, flags, timestamp, extrajson FROM eventlog WHERE obj == ?;",
	"SELECT id, accid, type, flags, timestamp, extrajson, obj FROM eventlog WHERE obj == ? OR accid == ?;",
};

static const std::string globstr = "G";
static const std::string globdbstr = "D";

static int busy_handler_callback(void *ptr, int count) {
	if (count < 7) {    //this should lead to a maximum wait of ~3.2s
		unsigned int sleeplen = 25 << count;
		wxThread *th = wxThread::This();
		if (th) {
			th->Sleep(sleeplen);
		} else {
			wxMilliSleep(sleeplen);
		}
		return 1;
	}
	else return 0;
}

dbpscache::dbpscache() {
	memset(stmts, 0, sizeof(stmts));
}

dbpscache::~dbpscache() {
	DeAllocAll();
}

const char *dbpscache::GetQueryString(DBPSC_TYPE type) {
	return std_sql_stmts[type];
}

sqlite3_stmt *dbpscache::GetStmt(sqlite3 *adb, DBPSC_TYPE type) {
	if (!stmts[type]) {
		sqlite3_prepare_v2(adb, GetQueryString(type), -1, &stmts[type], 0);
	}
	return stmts[type];
}

int dbpscache::ExecStmt(sqlite3 *adb, DBPSC_TYPE type) {
	sqlite3_stmt *stmt = GetStmt(adb, type);
	sqlite3_step(stmt);
	return sqlite3_reset(stmt);
}


void dbpscache::DeAllocAll() {
	for (unsigned int i = DBPSC_START; i < DBPSC_NUM_STATEMENTS; i++) {
		if (stmts[i]) {
			sqlite3_finalize(stmts[i]);
			stmts[i] = 0;
		}
	}
}

void dbpscache::BeginTransaction(sqlite3 *adb) {
	if (transaction_refcount == 0) {
		ExecStmt(adb, DBPSC_BEGIN);
	}
	transaction_refcount++;
}

void dbpscache::EndTransaction(sqlite3 *adb) {
	transaction_refcount--;
	if (transaction_refcount == 0) {
		ExecStmt(adb, DBPSC_COMMIT);
	} else if (transaction_refcount < 0) {
		transaction_refcount_went_negative = true;
	}
}

void dbpscache::CheckTransactionRefcountState() {
	if (transaction_refcount_went_negative) {
		TSLogMsgFormat(LOGT::DBERR, "dbpscache::CheckTransactionRefcountState transaction_refcount went negative");
	}
	if (transaction_refcount != 0) {
		TSLogMsgFormat(LOGT::DBERR, "dbpscache::CheckTransactionRefcountState transaction_refcount is %d", transaction_refcount);
	}
}

static bool TagToDict(unsigned char tag, const unsigned char *&dict, size_t &dict_size) {
	switch (tag) {
		case 'Z': {
			dict = 0;
			dict_size = 0;
			return true;
		}

		case 'J': {
			dict = jsondictionary;
			dict_size = sizeof(jsondictionary);
			return true;
		}

		case 'P': {
			dict = profimgdictionary;
			dict_size = sizeof(profimgdictionary);
			return true;
		}

		default: {
			return false;
		}
	}
}

scoped_stmt_holder DBInitialiseSql(sqlite3 *adb, std::string sql) {
	sqlite3_stmt *stmt;
	const char *leftover = nullptr;
	int result = sqlite3_prepare_v2(adb, sql.c_str(), sql.size(), &stmt, &leftover);
	if (result != SQLITE_OK) {
		TSLogMsgFormat(LOGT::DBERR, "sqlite3_prepare_v2 error: %d (%s)", result, cstr(sqlite3_errmsg(adb)));
		return scoped_stmt_holder();
	} else if (leftover && *leftover != 0) {
		TSLogMsgFormat(LOGT::DBERR, "More than one SQL statement passed to sqlite3_prepare_v2: \"%s\"", cstr(sql));
		return scoped_stmt_holder();
	} else {
		return scoped_stmt_holder { std::unique_ptr<sqlite3_stmt, stmt_deleter>(stmt) };
	}
}

#define HEADERSIZE 5

db_bind_buffer<dbb_compressed> DoCompress(const void *in, size_t insize, unsigned char tag, bool *iscompressed) {
	db_bind_buffer<dbb_compressed> out;

	if (insize) {
		const unsigned char *dict = nullptr;
		size_t dict_size = 0;
		bool compress = TagToDict(tag, dict, dict_size);
		if (compress && insize >= 100) {
			z_stream strm;
			strm.zalloc = Z_NULL;
			strm.zfree = Z_NULL;
			strm.opaque = Z_NULL;
			deflateInit(&strm, 5);

			if (dict) {
				deflateSetDictionary(&strm, dict, dict_size);
			}
			size_t maxsize = deflateBound(&strm, insize);
			out.allocate(maxsize + HEADERSIZE);
			unsigned char *data = reinterpret_cast<unsigned char *>(out.mutable_data());
			data[0] = tag;
			data[1] = (insize >> 24) & 0xFF;
			data[2] = (insize >> 16) & 0xFF;
			data[3] = (insize >> 8) & 0xFF;
			data[4] = (insize >> 0) & 0xFF;
			strm.avail_in = insize;
			strm.next_in = const_cast<unsigned char *>(static_cast<const unsigned char *>(in));
			strm.avail_out = maxsize;
			strm.next_out = data + HEADERSIZE;
			int res = deflate(&strm, Z_FINISH);
			#if DB_COPIOUS_LOGGING
				TSLogMsgFormat(LOGT::ZLIBTRACE, "deflate: %d, %d, %d", res, strm.avail_in, strm.avail_out);
			#endif
			if (res != Z_STREAM_END) {
				TSLogMsgFormat(LOGT::ZLIBERR, "DoCompress: deflate: error: res: %d (%s)", res, cstr(strm.msg));
			}
			out.data_size = HEADERSIZE + maxsize - strm.avail_out;
			deflateEnd(&strm);
			if (iscompressed) {
				*iscompressed = true;
			}
		} else {
			out.allocate(insize + 1);
			unsigned char *data = reinterpret_cast<unsigned char *>(out.mutable_data());
			data[0] = 'T';
			memcpy(data + 1, in, insize);
			if (iscompressed) {
				*iscompressed = false;
			}
		}
	}

	#if DB_COPIOUS_LOGGING
		static size_t cumin = 0;
		static size_t cumout = 0;
		cumin += insize;
		cumout += out.data_size;
		TSLogMsgFormat(LOGT::ZLIBTRACE, "compress: %d -> %d, cum: %f", insize, out.data_size, (double) cumout / (double) cumin);
	#endif

	return std::move(out);
}

db_bind_buffer<dbb_uncompressed> DoDecompress(db_bind_buffer<dbb_compressed> &&in) {
	if (!in.data_size) {
		return {};
	}

	const unsigned char *input = reinterpret_cast<const unsigned char *>(in.data);
	if (in.data_size == 2) {
		for (unsigned int i = 0; i < sizeof(allesctables) / sizeof(esctable); i++) {
			if (input[0] == allesctables[i].tag) {
				for (unsigned int j = 0; j < allesctables[i].count; j++) {
					if (input[1] == allesctables[i].start[j].id) {
						db_bind_buffer<dbb_uncompressed> out;
						out.data = allesctables[i].start[j].text;
						out.data_size = strlen(allesctables[i].start[j].text);
						return std::move(out);
					}
				}
				TSLogMsg(LOGT::ZLIBERR, "DoDecompress: Bad escape table identifier");
				return {};
			}
		}
	}
	const unsigned char *dict;
	size_t dict_size;
	switch (input[0]) {
		case 'T': {
			db_bind_buffer<dbb_uncompressed> out;
			std::swap(out.membuffer, in.membuffer);
			out.data = in.data + 1;
			out.data_size = in.data_size - 1;
			return std::move(out);
		}

		default: {
			bool compress = TagToDict(input[0], dict, dict_size);
			if (compress) {
				break;
			} else {
				TSLogMsgFormat(LOGT::ZLIBERR, "DoDecompress: Bad tag: 0x%X", input[0]);
				return {};
			}
		}
	}

	db_bind_buffer<dbb_uncompressed> out;
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.next_in = const_cast<unsigned char *>(input) + HEADERSIZE;
	strm.avail_in = in.data_size - HEADERSIZE;
	inflateInit(&strm);
	out.data_size = 0;
	for (unsigned int i = 1; i < 5; i++) {
		out.data_size <<= 8;
		out.data_size += input[i];
	}
	#if DB_COPIOUS_LOGGING
		TSLogMsgFormat(LOGT::ZLIBTRACE, "DoDecompress: insize %d, outsize %d", insize, outsize);
	#endif

	out.allocate_nt(out.data_size);
	unsigned char *data = reinterpret_cast<unsigned char *>(out.mutable_data());

	strm.next_out = data;
	strm.avail_out = out.data_size;
	while (true) {
		int res = inflate(&strm, Z_FINISH);
		#if DB_COPIOUS_LOGGING
			TSLogMsgFormat(LOGT::ZLIBTRACE, "inflate: %d, %d, %d", res, strm.avail_in, strm.avail_out);
		#endif
		if (res == Z_NEED_DICT) {
			if (dict) {
				inflateSetDictionary(&strm, dict, dict_size);
			} else {
				inflateEnd(&strm);
				TSLogMsgFormat(LOGT::ZLIBERR, "DoDecompress: Wants dictionary: %ux", strm.adler);
				return {};
			}
		} else if (res == Z_OK) {
			continue;
		} else if (res == Z_STREAM_END) {
			break;
		} else {
			TSLogMsgFormat(LOGT::ZLIBERR, "DoDecompress: inflate: error: res: %d (%s)", res, cstr(strm.msg));
			inflateEnd(&strm);
			return {};
		}
	}

	inflateEnd(&strm);

	#if DB_COPIOUS_LOGGING
		TSLogMsgFormat(LOGT::ZLIBTRACE, "decompress: %d -> %d", in.data_size, out.data_size);
	#endif
	return std::move(out);
}

db_bind_buffer<dbb_uncompressed> column_get_compressed(sqlite3_stmt* stmt, int num) {
	db_bind_buffer<dbb_compressed> src;
	src.data = static_cast<const char *>(sqlite3_column_blob(stmt, num));
	src.data_size = sqlite3_column_bytes(stmt, num);
	return DoDecompress(std::move(src));
}

db_bind_buffer<dbb_uncompressed> column_get_compressed_and_parse(sqlite3_stmt* stmt, int num, rapidjson::Document &dc) {
	db_bind_buffer<dbb_uncompressed> buffer = column_get_compressed(stmt, num);
	if (buffer.data_size) {
		buffer.make_persistent();
		if (!parse_util::ParseStringInPlace(dc, const_cast<char *>(buffer.data), "column_get_compressed_and_parse")) {
			dc.SetNull();
		}
	} else {
		dc.SetNull();
	}
	return std::move(buffer);
}

//! This calls itself for retweet sources, *unless* the retweet source ID is in idset
//! This expects to be called in *ascending* ID order
static void ProcessMessage_SelTweet(sqlite3 *db, sqlite3_stmt *stmt, dbseltweetmsg &m, std::deque<dbrettweetdata> &recv_data, uint64_t id,
		const container::set<uint64_t> &idset, dbconn *dbc, bool front_insert = false) {
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64) id);
	int res = sqlite3_step(stmt);
	uint64_t rtid = 0;
	if (res == SQLITE_ROW) {
		#if DB_COPIOUS_LOGGING
			TSLogMsgFormat(LOGT::DBTRACE, "DBSM::SELTWEET got id:%" llFmtSpec "d", (sqlite3_int64) id);
		#endif

		// emplacing at the *back* in the normal case is to ensure that the resulting deque is (mostly) in *ascending* order of ID
		// This ensures that tweets come before any retweets which use them as a source
		// *front* emplacing is used to ensure that any missing retweet sources come before any tweets which use them
		if (front_insert) {
			recv_data.emplace_front();
		} else {
			recv_data.emplace_back();
		}
		dbrettweetdata &rd = front_insert ? recv_data.front() : recv_data.back();

		rd.id = id;
		rd.statjson = column_get_compressed(stmt, 0);
		rd.dynjson = column_get_compressed(stmt, 1);
		rd.user1 = (uint64_t) sqlite3_column_int64(stmt, 2);
		rd.user2 = (uint64_t) sqlite3_column_int64(stmt, 3);
		rd.flags = (uint64_t) sqlite3_column_int64(stmt, 4);
		rd.timestamp = (uint64_t) sqlite3_column_int64(stmt, 5);
		rd.rtid = rtid = (uint64_t) sqlite3_column_int64(stmt, 6);

		if (rd.user1) {
			dbc->AsyncReadInUser(db, rd.user1, m.user_data);
		}
		if (rd.user2) {
			dbc->AsyncReadInUser(db, rd.user2, m.user_data);
		}
	} else {
		TSLogMsgFormat((m.flags & DBSTMF::NO_ERR) ? LOGT::DBTRACE : LOGT::DBERR,
				"DBSM::SELTWEET got error: %d (%s) for id: %" llFmtSpec "d",
				res, cstr(sqlite3_errmsg(db)), (sqlite3_int64) id);
	}
	sqlite3_reset(stmt);

	if (rtid && idset.find(rtid) == idset.end()) {
		// This is a retweet, if we're not already loading the retweet source, load it here
		// Note that this is front emplaced in *front* of the retweet which needs it
		ProcessMessage_SelTweet(db, stmt, m, recv_data, rtid, idset, dbc, true);
	}
}

//Note that the contents of themsg may be stolen if the lifetime of the message needs to be extended
//This is generally the case for messages which also act as replies
static void ProcessMessage(sqlite3 *db, std::unique_ptr<dbsendmsg> &themsg, bool &ok, dbpscache &cache, dbiothread *th, dbconn *dbc) {
	dbsendmsg *msg = themsg.get();
	switch (msg->type) {
		case DBSM::QUIT:
			ok = false;
			TSLogMsg(LOGT::DBINFO, "DBSM::QUIT");
			break;

		case DBSM::INSERTTWEET: {
			if (gc.readonlymode) break;
			dbinserttweetmsg *m = static_cast<dbinserttweetmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSTWEET);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->id);
			bind_compressed(stmt, 2, m->statjson, 'J');
			bind_compressed(stmt, 3, m->dynjson, 'J');
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->user1);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) m->user2);
			sqlite3_bind_int64(stmt, 6, (sqlite3_int64) m->flags);
			sqlite3_bind_int64(stmt, 7, (sqlite3_int64) m->timestamp);
			sqlite3_bind_int64(stmt, 8, (sqlite3_int64) m->rtid);
			int res = sqlite3_step(stmt);
			if (res != SQLITE_DONE) {
				TSLogMsgFormat(LOGT::DBERR, "DBSM::INSERTTWEET got error: %d (%s) for id: %" llFmtSpec "d",
					res, cstr(sqlite3_errmsg(db)), m->id);
				dbc->dbc_flags |= dbconn::DBCF::TWEET_ID_CACHE_INVALID;
			} else {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTTWEET inserted row id: %" llFmtSpec "d", (sqlite3_int64) m->id);
				if (m->rtid) {
					DBBindExec(db, cache.GetStmt(db, DBPSC_INSERTTWEETXREF), [&](sqlite3_stmt *incstmt) {
						sqlite3_bind_int64(incstmt, 1, (sqlite3_int64) m->id);
						sqlite3_bind_int64(incstmt, 2, (sqlite3_int64) m->rtid);
					}, "DBSM::INSERTTWEET (rtid xref)");
				}
				if (!m->xref_tweet_ids.empty()) {
					DBRangeBindExec(db, cache.GetStmt(db, DBPSC_INSERTTWEETXREF), m->xref_tweet_ids.begin(), m->xref_tweet_ids.end(),
							[&](sqlite3_stmt *incstmt, uint64_t xref) {
								sqlite3_bind_int64(incstmt, 1, (sqlite3_int64) m->id);
								sqlite3_bind_int64(incstmt, 2, (sqlite3_int64) xref);
							},
							"DBSM::INSERTTWEET (reply xref)");
				}
			}
			sqlite3_reset(stmt);
			DBBindExec(db, cache.GetStmt(db, DBPSC_INSINCREMENTALTWEETID), [&](sqlite3_stmt *incstmt) {
				sqlite3_bind_int64(incstmt, 1, (sqlite3_int64) m->id);
			}, "DBSM::INSERTTWEET (incrementaltweetids)");
			break;
		}

		case DBSM::UPDATETWEET: {
			if (gc.readonlymode) break;
			dbupdatetweetmsg *m = static_cast<dbupdatetweetmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_UPDTWEET);
			bind_compressed(stmt, 1, m->dynjson, 'J');
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->flags);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) m->id);
			int res = sqlite3_step(stmt);
			if (res != SQLITE_DONE) {
				TSLogMsgFormat(LOGT::DBERR, "DBSM::UPDATETWEET got error: %d (%s) for id: %" llFmtSpec "d",
						res, cstr(sqlite3_errmsg(db)), m->id);
			} else {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::UPDATETWEET updated id: %" llFmtSpec "d", (sqlite3_int64) m->id);
			}
			sqlite3_reset(stmt);
			DBBindExec(db, cache.GetStmt(db, DBPSC_INSINCREMENTALTWEETID), [&](sqlite3_stmt *incstmt) {
				sqlite3_bind_int64(incstmt, 1, (sqlite3_int64) m->id);
			}, "DBSM::UPDATETWEET (incrementaltweetids)");
			break;
		}

		case DBSM::SELTWEET: {
			dbseltweetmsg *m = static_cast<dbseltweetmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_SELTWEET);
			std::deque<dbrettweetdata> recv_data;

			//This is *ascending* ID order
			for (auto it = m->id_set.cbegin(); it != m->id_set.cend(); ++it) {
				ProcessMessage_SelTweet(db, stmt, *m, recv_data, *it, m->id_set, dbc);
			}
			if (!recv_data.empty()) {
				m->data = std::move(recv_data);
				m->SendReply(std::move(themsg), th);
				return;
			}
			break;
		}

		case DBSM::INSERTUSER: {
			if (gc.readonlymode) break;
			dbinsertusermsg *m = static_cast<dbinsertusermsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSUSER);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->id);
			bind_compressed(stmt, 2, m->json, 'J');
			bind_compressed(stmt, 3, m->cached_profile_img_url, 'P');
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->createtime);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) m->lastupdate);
			if (m->cached_profile_img_hash) {
				sqlite3_bind_blob(stmt, 6, m->cached_profile_img_hash->hash_sha1, sizeof(m->cached_profile_img_hash->hash_sha1), SQLITE_TRANSIENT);
			} else {
				sqlite3_bind_null(stmt, 6);
			}
			bind_compressed(stmt, 7, std::move(m->mentionindex));
			sqlite3_bind_int64(stmt, 8, (sqlite3_int64) m->profile_img_last_used);
			int res = sqlite3_step(stmt);
			if (res != SQLITE_DONE) {
				TSLogMsgFormat(LOGT::DBERR, "DBSM::INSERTUSER got error: %d (%s) for id: %" llFmtSpec "d",
						res, cstr(sqlite3_errmsg(db)), m->id);
			} else {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTUSER inserted id: %" llFmtSpec "d", (sqlite3_int64) m->id);
			}
			sqlite3_reset(stmt);
			break;
		}

		case DBSM::SELUSER: {
			dbselusermsg *m = static_cast<dbselusermsg*>(msg);
			for (uint64_t id : m->id_set) {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::SELUSER got request for user: %" llFmtSpec "u", id);

				dbc->AsyncReadInUser(db, id, m->data);
			}
			// Always reply, even if empty.
			// This avoids a race conditions if the main thread sends multiple requests
			// for the same user with reply handlers, before the first reply is
			// received.
			m->SendReply(std::move(themsg), th);
			return;
		}

		case DBSM::NOTIFYUSERSPURGED: {
			dbnotifyuserspurgedmsg *m = static_cast<dbnotifyuserspurgedmsg*>(msg);
			dbc->unloaded_user_ids.insert(m->ids.begin(), m->ids.end());
			TSLogMsgFormat(LOGT::DBTRACE, "DBSM::NOTIFYUSERSPURGED inserted %d ids", m->ids.size());
			break;
		}

		case DBSM::INSERTACC: {
			if (gc.readonlymode) break;
			dbinsertaccmsg *m = static_cast<dbinsertaccmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSERTNEWACC);
			sqlite3_bind_text(stmt, 1, m->name.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 2, m->dispname.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) m->userid);
			int res = sqlite3_step(stmt);
			m->dbindex = (unsigned int) sqlite3_last_insert_rowid(db);
			if (res != SQLITE_DONE) {
				TSLogMsgFormat(LOGT::DBERR, "DBSM::INSERTACC got error: %d (%s) for account name: %s",
						res, cstr(sqlite3_errmsg(db)), cstr(m->dispname));
			} else {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTACC inserted account dbindex: %d, name: %s", m->dbindex, cstr(m->dispname));
			}
			sqlite3_reset(stmt);
			m->SendReply(std::move(themsg), th);
			return;
		}

		case DBSM::DELACC: {
			if (gc.readonlymode) break;
			dbdelaccmsg *m = static_cast<dbdelaccmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_DELACC);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->dbindex);
			int res = sqlite3_step(stmt);
			if (res != SQLITE_DONE) {
				TSLogMsgFormat(LOGT::DBERR, "DBSM::DELACC got error: %d (%s) for account dbindex: %d",
						res, cstr(sqlite3_errmsg(db)), m->dbindex);
			} else {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::DELACC deleted account dbindex: %d", m->dbindex);
			}
			sqlite3_reset(stmt);
			break;
		}

		case DBSM::INSERTMEDIA: {
			if (gc.readonlymode) break;
			dbinsertmediamsg *m = static_cast<dbinsertmediamsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSERTMEDIA);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->media_id.m_id);
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->media_id.t_id);
			bind_compressed(stmt, 3, m->url, 'P');
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->lastused);
			int res = sqlite3_step(stmt);
			if (res != SQLITE_DONE) {
				TSLogMsgFormat(LOGT::DBERR, "DBSM::INSERTMEDIA got error: %d (%s) for id: %" llFmtSpec "d/%" llFmtSpec "d",
						res, cstr(sqlite3_errmsg(db)), (sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id);
			} else {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTMEDIA inserted media id: %" llFmtSpec "d/%" llFmtSpec "d",
						(sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id);
			}
			sqlite3_reset(stmt);
			break;
		}

		case DBSM::UPDATEMEDIAMSG: {
			if (gc.readonlymode) break;
			dbupdatemediamsg *m = static_cast<dbupdatemediamsg*>(msg);
			DBPSC_TYPE stmt_id = static_cast<DBPSC_TYPE>(-1); //invalid value
			switch (m->update_type) {
				case DBUMMT::THUMBCHECKSUM:
					stmt_id = DBPSC_UPDATEMEDIATHUMBCHKSM;
					break;

				case DBUMMT::FULLCHECKSUM:
					stmt_id = DBPSC_UPDATEMEDIAFULLCHKSM;
					break;

				case DBUMMT::FLAGS:
					stmt_id = DBPSC_UPDATEMEDIAFLAGS;
					break;

				case DBUMMT::LASTUSED:
					stmt_id = DBPSC_UPDATEMEDIALASTUSED;
					break;
			}
			sqlite3_stmt *stmt = cache.GetStmt(db, stmt_id);
			switch (m->update_type) {
				case DBUMMT::THUMBCHECKSUM:
				case DBUMMT::FULLCHECKSUM:
					if (m->chksm) {
						sqlite3_bind_blob(stmt, 1, m->chksm->hash_sha1, sizeof(m->chksm->hash_sha1), SQLITE_TRANSIENT);
					} else {
						sqlite3_bind_null(stmt, 1);
					}
					break;

				case DBUMMT::FLAGS:
					sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->flags.get());
					break;

				case DBUMMT::LASTUSED:
					sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->lastused);
					break;
			}

			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->media_id.m_id);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) m->media_id.t_id);
			int res = sqlite3_step(stmt);
			if (res != SQLITE_DONE) {
				TSLogMsgFormat(LOGT::DBERR, "DBSM::UPDATEMEDIAMSG got error: %d (%s) for id: %" llFmtSpec "d/%" llFmtSpec "d (%d)",
						res, cstr(sqlite3_errmsg(db)), (sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id, m->update_type);
			} else {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::UPDATEMEDIAMSG updated media id: %" llFmtSpec "d/%" llFmtSpec "d (%d)",
						(sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id, m->update_type);
			}
			sqlite3_reset(stmt);
			break;
		}

		case DBSM::UPDATETWEETSETFLAGS_GROUP: {
			if (gc.readonlymode) break;
			dbupdatetweetsetflagsmsg_group *m = static_cast<dbupdatetweetsetflagsmsg_group *>(msg);
			cache.BeginTransaction(db);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_UPDATETWEETFLAGSMASKED);
			for (auto it = m->ids.begin(); it != m->ids.end(); ++it) {
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->setmask);
				sqlite3_bind_int64(stmt, 2, (sqlite3_int64) (~m->unsetmask));
				sqlite3_bind_int64(stmt, 3, (sqlite3_int64) *it);
				int res = sqlite3_step(stmt);
				if (res != SQLITE_DONE) {
					TSLogMsgFormat(LOGT::DBERR, "DBSM::UPDATETWEETSETFLAGS_GROUP got error: %d (%s) for id: %" llFmtSpec "d",
							res, cstr(sqlite3_errmsg(db)), *it);
				}
				sqlite3_reset(stmt);
				DBBindExec(db, cache.GetStmt(db, DBPSC_INSINCREMENTALTWEETID), [&](sqlite3_stmt *incstmt) {
					sqlite3_bind_int64(incstmt, 1, (sqlite3_int64) *it);
				}, "DBSM::UPDATETWEETSETFLAGS_GROUP (incrementaltweetids)");
			}
			cache.EndTransaction(db);
			break;
		}

		case DBSM::UPDATETWEETSETFLAGS_MULTI: {
			if (gc.readonlymode) break;
			dbupdatetweetsetflagsmsg_multi *m = static_cast<dbupdatetweetsetflagsmsg_multi *>(msg);
			cache.BeginTransaction(db);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_UPDATETWEETFLAGSMASKED);
			for (auto &it : m->flag_actions) {
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) it.setmask);
				sqlite3_bind_int64(stmt, 2, (sqlite3_int64) ~(it.unsetmask));
				sqlite3_bind_int64(stmt, 3, (sqlite3_int64) it.id);
				int res = sqlite3_step(stmt);
				if (res != SQLITE_DONE) {
					TSLogMsgFormat(LOGT::DBERR, "DBSM::UPDATETWEETSETFLAGS_MULTI got error: %d (%s) for id: %" llFmtSpec "d",
							res, cstr(sqlite3_errmsg(db)), it.id);
				}
				sqlite3_reset(stmt);
				DBBindExec(db, cache.GetStmt(db, DBPSC_INSINCREMENTALTWEETID), [&](sqlite3_stmt *incstmt) {
					sqlite3_bind_int64(incstmt, 1, (sqlite3_int64) it.id);
				}, "DBSM::UPDATETWEETSETFLAGS_MULTI (incrementaltweetids)");
			}
			cache.EndTransaction(db);
			break;
		}

		case DBSM::INSERTEVENTLOGENTRY: {
			if (gc.readonlymode) break;
			dbinserteventlogentrymsg *m = static_cast<dbinserteventlogentrymsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSEVENTLOGENTRY);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->accid);
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->event_type);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) m->flags.get());
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->obj);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) m->eventtime);
			if (!m->extrajson.empty()) {
				bind_compressed(stmt, 6, m->extrajson, 'J');
			} else {
				sqlite3_bind_null(stmt, 6);
			}
			int res = sqlite3_step(stmt);
			if (res != SQLITE_DONE) {
				TSLogMsgFormat(LOGT::DBERR, "DBSM::INSERTEVENTLOGENTRY got error: %d (%s)",
					res, cstr(sqlite3_errmsg(db)));
			} else {
				TSLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTEVENTLOGENTRY inserted entry");
			}
			sqlite3_reset(stmt);
			break;
		}

		case DBSM::MSGLIST: {
			cache.BeginTransaction(db);
			dbsendmsg_list *m = static_cast<dbsendmsg_list *>(msg);
			TSLogMsgFormat(LOGT::DBTRACE, "DBSM::MSGLIST: queue size: %d START", m->msglist.size());
			for (auto &onemsg : m->msglist) {
				ProcessMessage(db, onemsg, ok, cache, th, dbc);
			}
			TSLogMsgFormat(LOGT::DBTRACE, "DBSM::MSGLIST: queue size: %d END", m->msglist.size());
			cache.EndTransaction(db);
			break;
		}

		case DBSM::FUNCTION: {
			cache.BeginTransaction(db);
			dbfunctionmsg *m = static_cast<dbfunctionmsg *>(msg);
			TSLogMsgFormat(LOGT::DBTRACE, "DBSM::FUNCTION: queue size: %d", m->funclist.size());
			for (auto &onemsg : m->funclist) {
				onemsg(db, ok, cache);
			}
			cache.EndTransaction(db);
			break;
		}

		case DBSM::FUNCTION_CALLBACK: {
			cache.BeginTransaction(db);
			dbfunctionmsg_callback *m = static_cast<dbfunctionmsg_callback *>(msg);
			m->db_func(db, ok, cache, *m);
			cache.EndTransaction(db);
			m->SendReply(std::move(themsg), th);
			return;
		}

		default:
			break;
	}
}

wxThread::ExitCode dbiothread::Entry() {
	MsgLoop();
	return 0;
}

void dbiothread::MsgLoop() {
	bool ok = true;
	while (ok) {
		dbsendmsg *msg;
		#ifdef __WINDOWS__
		DWORD num;
		OVERLAPPED *ovlp;
		bool res = GetQueuedCompletionStatus(iocp, &num, (PULONG_PTR) &msg, &ovlp, INFINITE);
		if (!res) {
			return;
		}
		#else
		size_t bytes_to_read = sizeof(msg);
		size_t bytes_read = 0;
		while (bytes_to_read) {
			ssize_t l_bytes_read = read(pipefd, ((char *) &msg) + bytes_read, bytes_to_read);
			if (l_bytes_read >= 0) {
				bytes_read += l_bytes_read;
				bytes_to_read -= l_bytes_read;
			} else {
				if (l_bytes_read == EINTR) {
					continue;
				} else {
					close(pipefd);
					return;
				}
			}
		}
		#endif
		std::unique_ptr<dbsendmsg> msgcont(msg);
		ProcessMessage(db, msgcont, ok, cache, this, dbc);
		if (!reply_list.empty()) {
			dbreplyevtstruct *rs = new dbreplyevtstruct;
			rs->reply_list = std::move(reply_list);

			wxCommandEvent evt(wxextDBCONN_NOTIFY, wxDBCONNEVT_ID_REPLY);
			evt.SetClientData(rs);
			dbc->AddPendingEvent(evt);

			reply_list.clear();
		}
	}
}

DEFINE_EVENT_TYPE(wxextDBCONN_NOTIFY)

BEGIN_EVENT_TABLE(dbconn, wxEvtHandler)
EVT_COMMAND(wxDBCONNEVT_ID_STDTWEETLOAD, wxextDBCONN_NOTIFY, dbconn::OnStdTweetLoadFromDB)
EVT_COMMAND(wxDBCONNEVT_ID_INSERTNEWACC, wxextDBCONN_NOTIFY, dbconn::OnDBNewAccountInsert)
EVT_COMMAND(wxDBCONNEVT_ID_SENDBATCH, wxextDBCONN_NOTIFY, dbconn::OnSendBatchEvt)
EVT_COMMAND(wxDBCONNEVT_ID_REPLY, wxextDBCONN_NOTIFY, dbconn::OnDBReplyEvt)
EVT_COMMAND(wxDBCONNEVT_ID_GENERICSELTWEET, wxextDBCONN_NOTIFY, dbconn::GenericDBSelTweetMsgHandler)
EVT_COMMAND(wxDBCONNEVT_ID_STDUSERLOAD, wxextDBCONN_NOTIFY, dbconn::OnStdUserLoadFromDB)
EVT_COMMAND(wxDBCONNEVT_ID_GENERICSELUSER, wxextDBCONN_NOTIFY, dbconn::GenericDBSelUserMsgHandler)
EVT_COMMAND(wxDBCONNEVT_ID_FUNCTIONCALLBACK, wxextDBCONN_NOTIFY, dbconn::OnDBSendFunctionMsgCallback)
EVT_TIMER(DBCONNTIMER_ID_ASYNCSTATEWRITE, dbconn::OnAsyncStateWriteTimer)
EVT_TIMER(DBCONNTIMER_ID_ASYNCPURGEOLDTWEETS, dbconn::OnAsyncPurgeOldTweetsTimer)
END_EVENT_TABLE()

void dbconn::OnStdTweetLoadFromDB(wxCommandEvent &event) {
	std::unique_ptr<dbseltweetmsg> msg(static_cast<dbseltweetmsg *>(event.GetClientData()));
	event.SetClientData(0);
	HandleDBSelTweetMsg(*msg, nullptr);
}

void dbconn::PrepareStdTweetLoadMsg(dbseltweetmsg &loadmsg) {
	loadmsg.targ = this;
	loadmsg.cmdevtype = wxextDBCONN_NOTIFY;
	loadmsg.winid = wxDBCONNEVT_ID_STDTWEETLOAD;
}

void dbconn::GenericDBSelTweetMsgHandler(wxCommandEvent &event) {
	std::unique_ptr<dbseltweetmsg> msg(static_cast<dbseltweetmsg *>(event.GetClientData()));
	event.SetClientData(0);

	const auto &it = generic_sel_funcs.find(reinterpret_cast<intptr_t>(msg.get()));
	if (it != generic_sel_funcs.end()) {
		it->second(*msg, this);
		generic_sel_funcs.erase(it);
	} else {
		TSLogMsgFormat(LOGT::DBERR, "dbconn::GenericDBSelTweetMsgHandler could not find handler for %p.", msg.get());
	}

	dbc_flags |= DBCF::REPLY_CLEARNOUPDF;
}

void dbconn::SetDBSelTweetMsgHandler(dbseltweetmsg &msg, std::function<void(dbseltweetmsg &, dbconn *)> f) {
	msg.targ = this;
	msg.cmdevtype = wxextDBCONN_NOTIFY;
	msg.winid = wxDBCONNEVT_ID_GENERICSELTWEET;
	generic_sel_funcs[reinterpret_cast<intptr_t>(&msg)] = std::move(f);
}

void dbconn::HandleDBSelTweetMsg(dbseltweetmsg &msg, optional_observer_ptr<db_handle_msg_pending_guard> pending_guard) {
	LogMsgFormat(LOGT::DBTRACE, "dbconn::HandleDBSelTweetMsg start");

	if (msg.flags & DBSTMF::CLEARNOUPDF) {
		dbc_flags |= DBCF::REPLY_CLEARNOUPDF;
	}

	DBSelUserReturnDataHandler(std::move(msg.user_data), pending_guard);

	for (dbrettweetdata &dt : msg.data) {
		#if DB_COPIOUS_LOGGING
			LogMsgFormat(LOGT::DBTRACE, "dbconn::HandleDBSelTweetMsg got tweet: id:%" llFmtSpec "d, statjson: %s, dynjson: %s", dt.id, cstr(dt.statjson), cstr(dt.dynjson));
		#endif
		ad.unloaded_db_tweet_ids.erase(dt.id);
		ad.loaded_db_tweet_ids.insert(dt.id);
		tweet_ptr t = ad.GetTweetById(dt.id);
		t->lflags |= TLF::SAVED_IN_DB;
		t->lflags |= TLF::LOADED_FROM_DB;

		rapidjson::Document dc;
		if (dt.statjson.data_size && !dc.ParseInsitu<0>(dt.statjson.mutable_data()).HasParseError() && dc.IsObject()) {
			genjsonparser::ParseTweetStatics(dc, t, 0);
		} else {
			LogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, "dbconn::HandleDBSelTweetMsg static JSON parse error: malformed or missing, tweet id: %" llFmtSpec "d", dt.id);
		}

		if (dt.dynjson.data_size && !dc.ParseInsitu<0>(dt.dynjson.mutable_data()).HasParseError() && dc.IsObject()) {
			genjsonparser::ParseTweetDyn(dc, t);
		} else {
			LogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, "dbconn::HandleDBSelTweetMsg dyn JSON parse error: malformed or missing, tweet id: %" llFmtSpec "d", dt.id);
		}

		t->user = ad.GetUserContainerById(dt.user1);
		if (dt.user2) {
			t->user_recipient = ad.GetUserContainerById(dt.user2);
		}
		t->createtime = (time_t) dt.timestamp;

		new (&t->flags) tweet_flags(dt.flags);

		//This sets flags_at_prev_update to the new value of flags
		//This prevents subsequent flag changes being missed without needing to do an update
		t->IgnoreChangeToFlagsByMask(~static_cast<unsigned long long>(0));
		t->SetFlagsInDBNow(t->flags);

		if (dt.rtid) {
			t->rtsrc = ad.GetTweetById(dt.rtid);
		}

		if (pending_guard) {
			pending_guard->tweets.push_back(std::move(t));
		} else {
			tweet_pending_bits_guard res = TryUnmarkPendingTweet(t, UMPTF::TPDB_NOUPDF);
			if (res) {
				dbc.dbc_flags |= DBCF::REPLY_CHECKPENDINGS;
			}
		}
	}
	LogMsgFormat(LOGT::DBTRACE, "dbconn::HandleDBSelTweetMsg end");
}

void dbconn::PrepareStdUserLoadMsg(dbselusermsg &loadmsg) {
	loadmsg.targ = this;
	loadmsg.cmdevtype = wxextDBCONN_NOTIFY;
	loadmsg.winid = wxDBCONNEVT_ID_STDUSERLOAD;
}

void dbconn::OnStdUserLoadFromDB(wxCommandEvent &event) {
	std::unique_ptr<dbselusermsg> msg(static_cast<dbselusermsg *>(event.GetClientData()));
	event.SetClientData(0);
	DBSelUserReturnDataHandler(std::move(msg->data), nullptr);
}

void dbconn::GenericDBSelUserMsgHandler(wxCommandEvent &event) {
	std::unique_ptr<dbselusermsg> msg(static_cast<dbselusermsg *>(event.GetClientData()));
	event.SetClientData(0);

	const auto &it = generic_sel_user_funcs.find(reinterpret_cast<intptr_t>(msg.get()));
	if (it != generic_sel_user_funcs.end()) {
		it->second(*msg, this);
		generic_sel_user_funcs.erase(it);
	} else {
		TSLogMsgFormat(LOGT::DBERR, "dbconn::GenericDBSelUserMsgHandler could not find handler for %p.", msg.get());
	}

	dbc_flags |= DBCF::REPLY_CLEARNOUPDF;
}

void dbconn::SetDBSelUserMsgHandler(dbselusermsg &msg, std::function<void(dbselusermsg &, dbconn *)> f) {
	msg.targ = this;
	msg.cmdevtype = wxextDBCONN_NOTIFY;
	msg.winid = wxDBCONNEVT_ID_GENERICSELUSER;
	generic_sel_user_funcs[reinterpret_cast<intptr_t>(&msg)] = std::move(f);
}

void dbconn::DBSelUserReturnDataHandler(std::deque<dbretuserdata> data, optional_observer_ptr<db_handle_msg_pending_guard> pending_guard) {
	for (dbretuserdata &du : data) {
		LogMsgFormat(LOGT::DBTRACE, "dbconn::DBSelUserReturnDataHandler got user data id: %" llFmtSpec "u", du.id);

		udc_ptr u = ad.GetUserContainerById(du.id);

		ad.unloaded_db_user_ids.erase(du.id);
		u->udc_flags &= ~UDC::BEING_LOADED_FROM_DB;
		u->udc_flags |= UDC::SAVED_IN_DB;

		unsigned int new_revision = u->user.revision_number + 1;
		u->user = std::move(du.ud);
		u->user.revision_number = new_revision;

		u->lastupdate = std::move(du.lastupdate);
		u->lastupdate_wrotetodb = std::move(du.lastupdate_wrotetodb);
		u->cached_profile_img_url = std::move(du.cached_profile_img_url);
		u->cached_profile_img_sha1 = std::move(du.cached_profile_img_sha1);
		u->profile_img_last_used = std::move(du.profile_img_last_used);
		u->profile_img_last_used_db = std::move(du.profile_img_last_used_db);

		// Incoming mention_set likely to be larger, old one likely to be empty or nearly empty
		tweetidset old_mention_set = std::move(u->mention_set);
		u->mention_set = std::move(du.mention_set);
		u->mention_set.insert(old_mention_set.begin(), old_mention_set.end());

		if (pending_guard) {
			pending_guard->users.push_back(std::move(u));
		} else {
			u->CheckPendingTweets();
			dbc.dbc_flags |= DBCF::REPLY_CHECKPENDINGS;
		}
	}
}

void dbconn::OnDBNewAccountInsert(wxCommandEvent &event) {
	std::unique_ptr<dbinsertaccmsg> msg(static_cast<dbinsertaccmsg *>(event.GetClientData()));
	event.SetClientData(0);
	wxString accname = wxstrstd(msg->name);
	for (auto &it : alist) {
		if (it->name == accname) {
			it->dbindex = msg->dbindex;
			it->sort_order = msg->dbindex;
			it->beinginsertedintodb = false;
			it->CalcEnabled();
			it->Exec();
		}
	}
}

void dbconn::OnDBSendFunctionMsgCallback(wxCommandEvent &event) {
	std::unique_ptr<dbfunctionmsg_callback> msg(static_cast<dbfunctionmsg_callback *>(event.GetClientData()));
	event.SetClientData(0);

	dbfunctionmsg_callback *msg_ptr = msg.get();
	msg_ptr->callback_func(std::move(msg));
}

void dbconn::SendMessageBatched(std::unique_ptr<dbsendmsg> msg) {
	observer_ptr<dbsendmsg_list> batch = GetMessageBatchQueue();
	batch->msglist.emplace_back(std::move(msg));

	if (batch->msglist.size() >= 8192) {
		FlushBatchQueue(); // queue is getting large, send it off now
	}
}

void dbconn::FlushBatchQueue() {
	if (batchqueue) {
		SendMessage(std::move(batchqueue));
	}
}

observer_ptr<dbsendmsg_list> dbconn::GetMessageBatchQueue() {
	if (!batchqueue) {
		batchqueue.reset(new dbsendmsg_list);
	}

	if (!(dbc_flags & DBCF::BATCHEVTPENDING)) {
		dbc_flags |= DBCF::BATCHEVTPENDING;
		wxCommandEvent evt(wxextDBCONN_NOTIFY, wxDBCONNEVT_ID_SENDBATCH);
		AddPendingEvent(evt);
	}
	return make_observer(batchqueue);
}

void dbconn::SendBatchedTweetFlagUpdate(uint64_t id, uint64_t setmask, uint64_t unsetmask) {
	observer_ptr<dbsendmsg_list> batch = GetMessageBatchQueue();

	if (!batch->msglist.empty() && batch->msglist.back()->type == DBSM::UPDATETWEETSETFLAGS_MULTI) {
		// the last item in the batch is a UPDATETWEETSETFLAGS_MULTI
		// try to use that instead of allocating a new one

		dbupdatetweetsetflagsmsg_multi *msg = static_cast<dbupdatetweetsetflagsmsg_multi *>(batch->msglist.back().get());
		if (msg->flag_actions.capacity() < 65536 || msg->flag_actions.size() < msg->flag_actions.capacity()) {
			// not too full, this can be appended to
			msg->flag_actions.push_back({ id, setmask, unsetmask });
			return;
		}
	}
	std::unique_ptr<dbupdatetweetsetflagsmsg_multi> msg(new dbupdatetweetsetflagsmsg_multi());
	msg->flag_actions.push_back({ id, setmask, unsetmask });
	SendMessageBatched(std::move(msg));
}

void dbconn::OnSendBatchEvt(wxCommandEvent &event) {
	if (!(dbc_flags & DBCF::INITED)) return;

	dbc_flags &= ~DBCF::BATCHEVTPENDING;
	FlushBatchQueue();
}

void dbconn::OnDBReplyEvt(wxCommandEvent &event) {
	dbreplyevtstruct *msg = static_cast<dbreplyevtstruct *>(event.GetClientData());
	std::unique_ptr<dbreplyevtstruct> msgcont(msg);
	event.SetClientData(0);

	if (dbc_flags & DBCF::INITED) {
		for (auto &it : msg->reply_list) {
			it.first->ProcessEvent(*it.second);
		}
	}

	if (dbc_flags & DBCF::REPLY_CLEARNOUPDF) {
		dbc_flags &= ~DBCF::REPLY_CLEARNOUPDF;
		CheckClearNoUpdateFlag_All();
	}

	if (dbc_flags & DBCF::REPLY_CHECKPENDINGS) {
		dbc_flags &= ~DBCF::REPLY_CHECKPENDINGS;
		for (auto &it : alist) {
			if (it->enabled) {
				it->StartRestQueryPendings();
			}
		}
	}
}

void dbconn::SendMessageBatchedOrAddToList(std::unique_ptr<dbsendmsg> msg, optional_observer_ptr<dbsendmsg_list> msglist) {
	if (msglist) {
		msglist->msglist.emplace_back(std::move(msg));
	} else {
		SendMessageBatched(std::move(msg));
	}
}

void dbconn::SendMessage(std::unique_ptr<dbsendmsg> msgp) {
	dbsendmsg *msg = msgp.release();
	#ifdef __WINDOWS__
	bool result = PostQueuedCompletionStatus(iocp, 0, (ULONG_PTR) msg, 0);
	if (!result) {
		LogMsgFormat(LOGT::DBERR, "dbconn::SendMessage(): Could not communicate with DB thread");
		CloseHandle(iocp);
		iocp = INVALID_HANDLE_VALUE;
	}
	#else
	size_t offset = 0;
	while (offset < sizeof(msg)) {
		ssize_t result = write(pipefd, ((const char *) &msg) + offset, sizeof(msg) - offset);
		if (result < 0) {
			int err = errno;
			if (err == EINTR) {
				continue;
			} else {
				LogMsgFormat(LOGT::DBERR, "dbconn::SendMessage(): Could not communicate with DB thread: %d, %s", err, cstr(strerror(err)));
				close(pipefd);
				pipefd = -1;
			}
		} else {
			offset += result;
		}
	}
	#endif
}

void dbconn::SendAccDBUpdate(std::unique_ptr<dbinsertaccmsg> insmsg) {
	insmsg->targ = this;
	insmsg->cmdevtype = wxextDBCONN_NOTIFY;
	insmsg->winid = wxDBCONNEVT_ID_INSERTNEWACC;
	dbc.SendMessage(std::move(insmsg));
}

void dbconn::SendFunctionMsgCallback(std::unique_ptr<dbfunctionmsg_callback> insmsg) {
	insmsg->targ = this;
	insmsg->cmdevtype = wxextDBCONN_NOTIFY;
	insmsg->winid = wxDBCONNEVT_ID_FUNCTIONCALLBACK;
	dbc.SendMessage(std::move(insmsg));
}

bool dbconn::Init(const std::string &filename /*UTF-8*/) {
	if (dbc_flags & DBCF::INITED) return true;

	LogMsgFormat(LOGT::DBINFO, "dbconn::Init(): About to initialise database connection");

	sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);		//only use sqlite from one thread at any given time
	sqlite3_initialize();

	int res = sqlite3_open_v2(filename.c_str(), &syncdb, gc.readonlymode ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
	if (res != SQLITE_OK) {
		wxMessageDialog(0, wxString::Format(wxT("Database could not be opened/created, got error: %d (%s)\nDatabase filename: %s\nCheck that the database is not locked by another process, and that the directory is read/writable."),
			res, wxstrstd(sqlite3_errmsg(syncdb)).c_str(), wxstrstd(filename).c_str()),
			wxT("Fatal Startup Error"), wxOK | wxICON_ERROR ).ShowModal();
		return false;
	}

	sqlite3_busy_handler(syncdb, &busy_handler_callback, 0);

	if (!gc.readonlymode) {
		int table_count = -1;
		DBRowExec(syncdb, "SELECT COUNT(*) FROM sqlite_master WHERE type == \"table\" AND name NOT LIKE \"sqlite%\";", [&](sqlite3_stmt *getstmt) {
			table_count = sqlite3_column_int(getstmt, 0);
		}, "dbconn::Init (table count)");

		LogMsgFormat(LOGT::DBTRACE, "dbconn::Init(): table_count: %d", table_count);

		auto db_startup_fatal = [&]() {
			wxMessageDialog(0, wxString::Format(wxT("Startup SQL failed, got error: %d (%s)\nDatabase filename: %s\nCheck that the database is not locked by another process, and that the directory is read/writable."),
					res, wxstrstd(sqlite3_errmsg(syncdb)).c_str(), wxstrstd(filename).c_str()),
					wxT("Fatal Startup Error"), wxOK | wxICON_ERROR ).ShowModal();
			sqlite3_close(syncdb);
			syncdb = 0;
		};

		res = sqlite3_exec(syncdb, startup_sql, 0, 0, 0);
		if (res != SQLITE_OK) {
			db_startup_fatal();
			return false;
		}

		if (table_count <= 0) {
			// This is a new DB, no need to do update check, just write version
			if (!SyncWriteDBVersion(syncdb)) {
				db_startup_fatal();
				return false;
			}
		} else {
			// Check whether DB is old and needs to be updated
			if (!SyncDoUpdates(syncdb)) {
				// All bets are off, give up now
				sqlite3_close(syncdb);
				syncdb = 0;
				return false;
			}
		}
	} else {
		// read-only DB, check it's the right version
		if (!SyncCheckReadOnlyDBVersion(syncdb)) {
			// Wrong version, give up now
			sqlite3_close(syncdb);
			syncdb = 0;
			return false;
		}
	}

	LogMsgFormat(LOGT::DBINFO, "dbconn::Init(): About to read in state from database");

	SyncReadInAllUserIDs(syncdb);
	SyncReadInUserDMIndexes(syncdb);
	AccountSync(syncdb);
	ReadAllCFGIn(syncdb, gc, alist);
	SortAccounts();
	SyncReadInRBFSs(syncdb);
	SyncReadInHandleNewPendingOps(syncdb);
	SyncReadInAllMediaEntities(syncdb);
	SyncReadInAllTweetIDs(syncdb);
	SyncPurgeOldTweets(syncdb);
	SyncReadInTpanels(syncdb);
	SyncReadInWindowLayout(syncdb);
	SyncReadInUserRelationships(syncdb);
	SyncPostUserLoadCompletion();

	LogMsgFormat(LOGT::DBINFO, "dbconn::Init(): State read in from database complete, about to create database thread");

	th = new dbiothread();
	th->filename = filename;
	th->db = syncdb;
	th->dbc = this;
	syncdb = 0;

#ifdef __WINDOWS__
	iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
	th->iocp = iocp;
	if (!iocp) {
		wxMessageDialog(0, wxT("DB IOCP creation failed."));
		sqlite3_close(syncdb);
		syncdb = 0;
		return false;
	}
#else
	int pipepair[2];
	int result = pipe(pipepair);
	if (result < 0) {
		wxMessageDialog(0, wxString::Format(wxT("DB pipe creation failed: %d, %s"), errno, wxstrstd(strerror(errno)).c_str()));
		sqlite3_close(syncdb);
		syncdb = 0;
		return false;
	}
	th->pipefd = pipepair[0];
	this->pipefd = pipepair[1];
#endif
	th->Create();
#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 12)
	pthread_setname_np(th->GetId(), "retcon-sqlite3");
#endif
#endif
	th->Run();
	LogMsgFormat(LOGT::DBINFO | LOGT::THREADTRACE, "dbconn::Init(): Created database thread: %d", th->GetId());

	asyncstateflush_timer.reset(new wxTimer(this, DBCONNTIMER_ID_ASYNCSTATEWRITE));
	asyncpurgeoldtweets_timer.reset(new wxTimer(this, DBCONNTIMER_ID_ASYNCPURGEOLDTWEETS));
	ResetAsyncStateWriteTimer();
	ResetPurgeOldTweetsTimer();

	dbc_flags |= DBCF::INITED;

	for (auto &it : post_init_callbacks) {
		it(this);
	}
	post_init_callbacks.clear();

	return true;
}

void dbconn::DeInit() {
	if (!(dbc_flags & DBCF::INITED)) return;

	asyncstateflush_timer.reset();
	asyncpurgeoldtweets_timer.reset();

	FlushBatchQueue();

	dbc_flags &= ~DBCF::INITED;

	LogMsg(LOGT::DBINFO | LOGT::THREADTRACE, "dbconn::DeInit: About to terminate database thread and write back state");

	SendMessage(std::unique_ptr<dbsendmsg>(new dbsendmsg(DBSM::QUIT)));

	#ifdef __WINDOWS__
	CloseHandle(iocp);
	#else
	close(pipefd);
	#endif
	LogMsg(LOGT::DBINFO | LOGT::THREADTRACE, "dbconn::DeInit(): Waiting for database thread to terminate");
	th->Wait();
	syncdb = th->db;
	delete th;

	LogMsg(LOGT::DBINFO | LOGT::THREADTRACE, "dbconn::DeInit(): Database thread terminated");

	size_t pending = 0;
	while (wxGetApp().Pending()) {
		pending++;
		wxGetApp().Dispatch();
	}

	LogMsgFormat(LOGT::DBINFO | LOGT::THREADTRACE, "dbconn::DeInit(): Flushed %u pending events", pending);

	if (generic_sel_funcs.size()) {
		LogMsgFormat(LOGT::DBERR, "dbconn::DeInit(): %zu handlers left in generic_sel_funcs", generic_sel_funcs.size());
	}
	if (generic_sel_user_funcs.size()) {
		LogMsgFormat(LOGT::DBERR, "dbconn::DeInit(): %zu handlers left in generic_sel_user_funcs", generic_sel_user_funcs.size());
	}

	MergeTweetIdSets();

	if (!gc.readonlymode) {
		cache.BeginTransaction(syncdb);
	}
	SyncPurgeUnreferencedTweets(syncdb); //this does a dry-run in read-only mode
	if (!gc.readonlymode) {
		WriteAllCFGOut(syncdb, gc, alist);
		SyncWriteBackAllUsers(syncdb);
		SyncWriteBackAccountIdLists(syncdb);
		SyncWriteOutRBFSs(syncdb);
		SyncWriteOutHandleNewPendingOps(syncdb);
		SyncWriteBackWindowLayout(syncdb);
		SyncWriteBackTpanels(syncdb);
		SyncWriteBackUserRelationships(syncdb);
		SyncWriteBackUserDMIndexes(syncdb);
		SyncWriteBackTweetIDIndexCache(syncdb);
		SyncClearDirtyFlag(syncdb);
	}
	SyncPurgeMediaEntities(syncdb); //this does a dry-run in read-only mode
	SyncPurgeProfileImages(syncdb); //this does a dry-run in read-only mode
	if (!gc.readonlymode) {
		cache.EndTransaction(syncdb);
	}

	sqlite3_close(syncdb);

	cache.CheckTransactionRefcountState();

	LogMsg(LOGT::DBINFO | LOGT::THREADTRACE, "dbconn::DeInit(): State write back to database complete, database connection closed.");
}

void dbconn::CheckPurgeTweets() {
	unsigned int purge_count = 0;
	unsigned int refone_count = 0;

	auto it = ad.tweetobjs.begin();
	while (it != ad.tweetobjs.end()) {
		tweet_ptr &t = it->second;
		uint64_t id = it->first;

		t->ClearDeadPendingOps();

		if (t->lflags & TLF::REFCOUNT_WENT_GT1 || t->HasPendingOps()) {
			// Keep it

			// Reset the flag, if no one creates a second pointer to it before the next call to CheckPurgeTweets, it will then be purged
			if (t->GetRefcount() == 1) {
				t->lflags &= ~TLF::REFCOUNT_WENT_GT1;
				refone_count++;
			}
			++it;
		} else {
			// Bin it
			if (t->lflags & TLF::SAVED_IN_DB) {
				ad.unloaded_db_tweet_ids.insert(id);
				ad.loaded_db_tweet_ids.erase(id);
			}
			it = ad.tweetobjs.erase(it);
			purge_count++;
		}
	}
	LogMsgFormat(LOGT::DBINFO, "dbconn::CheckPurgeTweets purged %u tweets from memory, %zu remaining, %u might be purged next time",
			purge_count, ad.tweetobjs.size(), refone_count);
}

// This should be called immediately after AsyncWriteBackAllUsers
void dbconn::CheckPurgeUsers() {
	unsigned int refone_count = 0;
	unsigned int purge_count = 0;
	useridset db_purged_ids;

	auto it = ad.userconts.begin();
	while (it != ad.userconts.end()) {
		udc_ptr &u = it->second;
		uint64_t id = it->first;

		if (u->udc_flags & UDC::NON_PURGABLE) {
			// do nothing, user not purgable
			++it;
		} else if (u->udc_flags & UDC::REFCOUNT_WENT_GT1 || !u->pendingtweets.empty()) {
			// Keep it

			// Reset the flag, if no one creates a second pointer to it before the next call to CheckPurgeUsers, it will then be purged
			if (u->GetRefcount() == 1) {
				u->udc_flags &= ~UDC::REFCOUNT_WENT_GT1;
				refone_count++;
			}
			++it;
		} else {
			// Bin it
			if (u->udc_flags & UDC::SAVED_IN_DB) {
				db_purged_ids.insert(id);
				ad.unloaded_db_user_ids.insert(id);
			}
			it = ad.userconts.erase(it);
			purge_count++;
		}
	}
	LogMsgFormat(LOGT::DBINFO, "dbconn::CheckPurgeUsers purged %u users from memory (%u in DB), %zu remaining, %u might be purged next time",
			purge_count, db_purged_ids.size(), ad.userconts.size(), refone_count);

	if (!db_purged_ids.empty()) {
		SendMessage(std::unique_ptr<dbnotifyuserspurgedmsg>(new dbnotifyuserspurgedmsg(std::move(db_purged_ids))));
	}
}

void dbconn::AsyncWriteBackState() {
	if (!gc.readonlymode) {
		LogMsg(LOGT::DBINFO, "dbconn::AsyncWriteBackState start");

		FlushBatchQueue();

		std::unique_ptr<dbfunctionmsg> msg(new dbfunctionmsg);
		auto cfg_closure = WriteAllCFGOutClosure(gc, alist, true);
		msg->funclist.emplace_back([cfg_closure](sqlite3 *db, bool &ok, dbpscache &cache) {
			TSLogMsg(LOGT::DBTRACE, "dbconn::AsyncWriteBackState: CFG write start");
			DBWriteConfig twfc(db);
			cfg_closure(twfc);
			TSLogMsg(LOGT::DBTRACE, "dbconn::AsyncWriteBackState: CFG write end");
		});
		AsyncWriteBackAllUsers(*msg);
		AsyncWriteBackAccountIdLists(*msg);
		AsyncWriteOutRBFSs(*msg);
		AsyncWriteOutHandleNewPendingOps(*msg);
		AsyncWriteBackCIDSLists(*msg);
		AsyncWriteBackTpanels(*msg);
		AsyncWriteBackUserDMIndexes(*msg);

		SendMessage(std::move(msg));

		LogMsg(LOGT::DBINFO, "dbconn::AsyncWriteBackState: message sent to DB thread");
	}

	CheckPurgeTweets();
	CheckPurgeUsers();
}

// This is mainly useful for DB filtering
void dbconn::AsyncWriteBackStateMinimal() {
	if (!gc.readonlymode) {
		LogMsg(LOGT::DBINFO, "dbconn::AsyncWriteBackStateMinimal start");

		FlushBatchQueue();

		std::unique_ptr<dbfunctionmsg> msg(new dbfunctionmsg);
		AsyncWriteBackAllUsers(*msg);
		SendMessage(std::move(msg));

		LogMsg(LOGT::DBINFO, "dbconn::AsyncWriteBackStateMinimal: message sent to DB thread");
	}
}

void dbconn::InsertNewTweet(tweet_ptr_p tobj, std::string statjson, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbinserttweetmsg> msg(new dbinserttweetmsg());
	msg->statjson = std::move(statjson);
	msg->statjson.push_back((char) 42);	//modify the string to prevent any possible COW semantics
	msg->statjson.resize(msg->statjson.size() - 1);
	msg->dynjson = tobj->mkdynjson();
	msg->id = tobj->id;
	msg->user1 = tobj->user->id;
	msg->user2 = tobj->user_recipient ? tobj->user_recipient->id : 0;
	msg->timestamp = tobj->createtime;
	msg->flags = tobj->flags.ToULLong();

	msg->xref_tweet_ids = tobj->quoted_tweet_ids;
	msg->xref_tweet_ids.push_back(tobj->in_reply_to_status_id);

	// remove duplicates
	std::sort(msg->xref_tweet_ids.begin(), msg->xref_tweet_ids.end());
	msg->xref_tweet_ids.erase(std::unique(msg->xref_tweet_ids.begin(), msg->xref_tweet_ids.end()), msg->xref_tweet_ids.end());

	if (tobj->rtsrc) {
		msg->rtid = tobj->rtsrc->id;
	} else {
		msg->rtid = 0;
	}
	ad.loaded_db_tweet_ids.insert(tobj->id);

	SendMessageBatchedOrAddToList(std::move(msg), msglist);
	tobj->SetFlagsInDBNow(tobj->flags);
}

void dbconn::UpdateTweetDyn(tweet_ptr_p tobj, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbupdatetweetmsg> msg(new dbupdatetweetmsg());
	msg->dynjson = tobj->mkdynjson();
	msg->id = tobj->id;
	msg->flags = tobj->flags.ToULLong();
	SendMessageBatchedOrAddToList(std::move(msg), msglist);
	tobj->SetFlagsInDBNow(tobj->flags);
}

void dbconn::InsertUser(udc_ptr_p u, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbinsertusermsg> msg(new dbinsertusermsg());
	msg->id = u->id;
	msg->json = u->mkjson();
	msg->cached_profile_img_url = std::string(u->cached_profile_img_url.begin(), u->cached_profile_img_url.end());	//prevent any COW semantics
	msg->createtime = u->user.createtime;
	msg->lastupdate = u->lastupdate;
	msg->cached_profile_img_hash = u->cached_profile_img_sha1;
	msg->mentionindex = settocompressedblob_desc(u->mention_set);
	u->lastupdate_wrotetodb = u->lastupdate;
	msg->profile_img_last_used = u->profile_img_last_used;
	u->profile_img_last_used_db = u->profile_img_last_used;
	u->udc_flags |= UDC::SAVED_IN_DB;
	SendMessageBatchedOrAddToList(std::move(msg), msglist);
}

void dbconn::InsertMedia(media_entity &me, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbinsertmediamsg> msg(new dbinsertmediamsg());
	msg->media_id = me.media_id;
	msg->url = std::string(me.media_url.begin(), me.media_url.end());
	msg->lastused = me.lastused;
	SendMessageBatchedOrAddToList(std::move(msg), msglist);
	me.flags |= MEF::IN_DB;
}

void dbconn::UpdateMedia(media_entity &me, DBUMMT update_type, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbupdatemediamsg> msg(new dbupdatemediamsg(update_type));
	msg->media_id = me.media_id;
	switch (update_type) {
		case DBUMMT::THUMBCHECKSUM:
			msg->chksm = me.thumb_img_sha1;
			break;

		case DBUMMT::FULLCHECKSUM:
			msg->chksm = me.full_img_sha1;
			break;

		case DBUMMT::FLAGS:
			msg->flags = me.flags & MEF::DB_SAVE_MASK;
			break;

		case DBUMMT::LASTUSED:
			msg->lastused = me.lastused;
			break;
	}
	SendMessageBatchedOrAddToList(std::move(msg), msglist);
}

void dbconn::InsertNewEventLogEntry(optional_observer_ptr<dbsendmsg_list> msglist, optional_observer_ptr<taccount> acc, DB_EVENTLOG_TYPE type,
		flagwrapper<DBELF> flags, uint64_t obj, time_t eventtime, std::string extrajson) {
	std::unique_ptr<dbinserteventlogentrymsg> msg(new dbinserteventlogentrymsg());
	if (acc) {
		msg->accid = acc->dbindex;
	} else {
		msg->accid = -1;
	}
	msg->event_type = type;
	msg->flags = flags;
	msg->obj = obj;
	if (eventtime) {
		msg->eventtime = eventtime;
	} else {
		msg->eventtime = time(nullptr);
	}
	msg->extrajson = std::move(extrajson);
	SendMessageBatchedOrAddToList(std::move(msg), msglist);
}

void dbconn::AccountSync(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::AccountSync start");

	unsigned int total = 0;
	DBRowExec(adb, "SELECT id, name, tweetids, dmids, userid, dispname, blockedids, mutedids, nortids FROM acc;", [&](sqlite3_stmt *getstmt) {
		unsigned int id = (unsigned int) sqlite3_column_int(getstmt, 0);
		wxString name = wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 1));

		std::shared_ptr<taccount> ta(new(taccount));
		ta->name = name;
		ta->dbindex = id;
		alist.push_back(ta);

		setfromcompressedblob(ta->tweet_ids, getstmt, 2);
		setfromcompressedblob(ta->dm_ids, getstmt, 3);
		setfromcompressedblob(ta->blocked_users, getstmt, 6);
		setfromcompressedblob(ta->muted_users, getstmt, 7);
		setfromcompressedblob(ta->no_rt_users, getstmt, 8);
		total += ta->tweet_ids.size();
		total += ta->dm_ids.size();
		total += ta->blocked_users.size();
		total += ta->muted_users.size();
		total += ta->no_rt_users.size();

		uint64_t userid = (uint64_t) sqlite3_column_int64(getstmt, 4);
		ta->usercont = SyncReadInUser(adb, userid);
		ta->dispname = wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 5));

		LogMsgFormat(LOGT::DBINFO, "dbconn::AccountSync: Found account: dbindex: %d, "
				"name: %s, tweet IDs: %u, DM IDs: %u, blocked IDs: %u, muted IDs: %u, no RT IDs: %u",
				id, cstr(name), ta->tweet_ids.size(), ta->dm_ids.size(), ta->blocked_users.size(), ta->muted_users.size(), ta->no_rt_users.size());
	}, "dbconn::AccountSync");
	LogMsgFormat(LOGT::DBINFO, "dbconn::AccountSync end, total: %u IDs", total);
}

struct tweet_scan_dynjson_parser {
	std::vector<observer_ptr<taccount>> accs;

	tweet_scan_dynjson_parser() {
		for (auto &it : alist) {
			if (it->dbindex >= accs.size()) {
				accs.resize(it->dbindex + 1);
			}
			accs[it->dbindex] = it.get();
		}
	}

	void parse(uint64_t id, sqlite3_stmt* stmt, int col_num, bool isdm) {
		using namespace parse_util;

		rapidjson::Document dc;
		db_bind_buffer<dbb_uncompressed> json = column_get_compressed_and_parse(stmt, col_num, dc);
		if (dc.IsObject()) {
			// this must stay in sync with genjsonparser::ParseTweetDyn
			const rapidjson::Value &p = dc["p"];
			if (p.IsArray()) {
				for (rapidjson::SizeType i = 0; i < p.Size(); i++) {
					unsigned int dbindex = CheckGetJsonValueDef<unsigned int>(p[i], "a", 0);
					unsigned int flags = CheckGetJsonValueDef<unsigned int>(p[i], "f", 0);
					if (tweet_perspective::IsFlagsArrivedHere(flags)) {
						if (dbindex < accs.size() && accs[dbindex]) {
							if (isdm) {
								accs[dbindex]->dm_ids.insert(id);
							} else {
								accs[dbindex]->tweet_ids.insert(id);
							}
						}
					}
				}
			}
		}
	}
};

struct tweet_scan_statjson_parser {
	std::map<uint64_t, tweetidset> user_mention_insert_map;

	void parse(uint64_t id, sqlite3_stmt* stmt, int col_num) {
		using namespace parse_util;

		rapidjson::Document dc;
		db_bind_buffer<dbb_uncompressed> json = column_get_compressed_and_parse(stmt, col_num, dc);
		if (!dc.IsObject()) {
			return;
		}

		const rapidjson::Value &ent = dc["entities"];
		if (!ent.IsObject()) {
			return;
		}

		const rapidjson::Value &user_mentions = ent["user_mentions"];
		if (!user_mentions.IsArray()) {
			return;
		}

		for (rapidjson::SizeType i = 0; i < user_mentions.Size(); i++) {
			uint64_t userid;
			if (!CheckTransJsonValueDef(userid, user_mentions[i], "id", 0)) {
				continue;
			}
			tweetidset &idset = user_mention_insert_map[userid];
			idset.insert(id);
		}
	}

	void execute(sqlite3 *syncdb, dbpscache &cache) {
		auto select = DBInitialiseSql(syncdb, "SELECT mentionindex FROM users WHERE id == ?;");
		auto update = DBInitialiseSql(syncdb, "UPDATE users SET mentionindex = ? WHERE id == ?;");

		for (auto &it : user_mention_insert_map) {
			uint64_t id = it.first;
			optional_udc_ptr u = ad.GetExistingUserContainerById(id);
			if (u && u->udc_flags & UDC::SAVED_IN_DB) {
				u->mention_set.insert(it.second.begin(), it.second.end());
				u->lastupdate_wrotetodb = 0;		//force flush of user to DB
			} else if (!gc.readonlymode) {
				// read and write back to DB
				cache.BeginTransaction(syncdb);
				DBBindRowExec(syncdb, select.stmt(),
					[&](sqlite3_stmt *getstmt) {
						sqlite3_bind_int64(getstmt, 1, (sqlite3_int64) id);
					},
					[&](sqlite3_stmt *getstmt) {
						tweetidset ids;
						setfromcompressedblob(ids, getstmt, 0);

						ids.insert(it.second.begin(), it.second.end());

						DBBindExec(syncdb, update.stmt(),
							[&](sqlite3_stmt *setstmt) {
								sqlite3_bind_int64(setstmt, 1, (sqlite3_int64) id);
								bind_compressed(setstmt, 2, settocompressedblob_desc(ids));
							},
							"tweet_scan_statjson_parser::execute (update)"
						);
					},
					"tweet_scan_statjson_parser::execute (select)"
				);
				cache.EndTransaction(syncdb);
			}
		}
	}
};

// This must be called after SyncReadInUserDMIndexes
void dbconn::SyncReadInAllTweetIDs(sqlite3 *syncdb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInAllTweetIDs start");

	DBBindRowExec(syncdb, cache.GetStmt(syncdb, DBPSC_SELSTATICSETTING),
		[&](sqlite3_stmt *getstmt) {
			sqlite3_bind_text(getstmt, 1, "tweetidsetcache", -1, SQLITE_STATIC);
		},
		[&](sqlite3_stmt *getstmt) {
			setfromcompressedblob(ad.unloaded_db_tweet_ids, getstmt, 0);
		},
		"dbconn::SyncReadInAllTweetIDs (cache load)"
	);

	tweetidset incremental_ids;

	auto add_dm_index = [&](uint64_t id, sqlite3_stmt* stmt) {
		auto exec = [&](int col_num) {
			uint64_t user_id = (uint64_t) sqlite3_column_int64(stmt, col_num);
			if (user_id) {
				ad.GetUserDMIndexById(user_id).AddDMId(id);
				SyncReadInUser(syncdb, user_id);
			}
		};
		exec(4);
		exec(5);
	};

	if (ad.unloaded_db_tweet_ids.empty() || gc.rescan_tweets_table) {
		// Didn't find any cache
		LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInAllTweetIDs table scan");

		cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
			(ad.cids.*mptr).clear();
		});

		tweet_scan_dynjson_parser tsdp;
		tweet_scan_statjson_parser tssp;

		DBRowExec(syncdb, "SELECT id, flags, dynjson, statjson, userid, userrecipid FROM tweets ORDER BY id DESC;", [&](sqlite3_stmt *getstmt) {
			uint64_t id = (uint64_t) sqlite3_column_int64(getstmt, 0);
			ad.unloaded_db_tweet_ids.insert(ad.unloaded_db_tweet_ids.end(), id);

			uint64_t flags = (uint64_t) sqlite3_column_int64(getstmt, 1);
			cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
				if (flags & flagvalue) {
					(ad.cids.*mptr).insert(id);
				}
			});

			tsdp.parse(id, getstmt, 2, flags & tweet_flags::GetFlagValue('D'));
			tssp.parse(id, getstmt, 3);
			if (flags & tweet_flags::GetFlagValue('D')) {
				add_dm_index(id, getstmt);
			}
		}, "dbconn::SyncReadInAllTweetIDs (table scan)");

		if (!gc.readonlymode) {
			cache.BeginTransaction(syncdb);
			SyncWriteBackTweetIDIndexCache(syncdb);
			SyncWriteBackAccountIdLists(syncdb);
			SyncWriteBackUserDMIndexes(syncdb);
			cache.EndTransaction(syncdb);
		}
		tssp.execute(syncdb, cache);
	} else {
		SyncReadInCIDSLists(syncdb);

		DBRowExec(syncdb, "SELECT id FROM incrementaltweetids ORDER BY id DESC;", [&](sqlite3_stmt *getstmt) {
			incremental_ids.insert(incremental_ids.end(), (uint64_t) sqlite3_column_int64(getstmt, 0));
		}, "dbconn::SyncReadInAllTweetIDs (incrementaltweetids)");
		if (!incremental_ids.empty()) {
			tweet_scan_dynjson_parser tsdp;
			tweet_scan_statjson_parser tssp;

			DBRangeBindRowExec(
				syncdb, "SELECT id, flags, dynjson, statjson, userid, userrecipid FROM tweets WHERE id == ?;", incremental_ids.begin(), incremental_ids.end(),
				[&](sqlite3_stmt *getstmt, uint64_t id) {
					sqlite3_bind_int64(getstmt, 1, (sqlite3_int64) id);
				},
				[&](sqlite3_stmt *getstmt) {
					uint64_t id = (uint64_t) sqlite3_column_int64(getstmt, 0);
					ad.unloaded_db_tweet_ids.insert(ad.unloaded_db_tweet_ids.end(), id);

					uint64_t flags = (uint64_t) sqlite3_column_int64(getstmt, 1);
					cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
						if (flags & flagvalue) {
							(ad.cids.*mptr).insert(id);
						} else {
							(ad.cids.*mptr).erase(id);
						}
					});

					tsdp.parse(id, getstmt, 2, flags & tweet_flags::GetFlagValue('D'));
					tssp.parse(id, getstmt, 3);
					if (flags & tweet_flags::GetFlagValue('D')) {
						add_dm_index(id, getstmt);
					}
				},
				"dbconn::SyncReadInAllTweetIDs (incrementaltweetids)"
			);

			tssp.execute(syncdb, cache);
		}
	}

	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInAllTweetIDs end, read: incremental %zu, total: %zu",
			incremental_ids.size(), ad.unloaded_db_tweet_ids.size());
}

void dbconn::MergeTweetIdSets() {
	LogMsgFormat(LOGT::DBINFO, "dbconn::MergeTweetIdSets start: unloaded: %zu, loaded: %zu",
			ad.unloaded_db_tweet_ids.size(), ad.loaded_db_tweet_ids.size());
	ad.unloaded_db_tweet_ids.insert(ad.loaded_db_tweet_ids.begin(), ad.loaded_db_tweet_ids.end());
	LogMsgFormat(LOGT::DBINFO, "dbconn::MergeTweetIdSets end: total: %zu", ad.unloaded_db_tweet_ids.size());
}

void dbconn::SyncWriteBackTweetIDIndexCache(sqlite3 *syncdb) {
	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncWriteBackTweetIDIndexCache start");

	if (dbc_flags & DBCF::TWEET_ID_CACHE_INVALID) {
		LogMsg(LOGT::DBINFO, "dbconn::SyncWriteBackTweetIDIndexCache not writing back as marked invalid");
		return;
	}

	DBBindExec(syncdb, cache.GetStmt(syncdb, DBPSC_INSSTATICSETTING),
		[&](sqlite3_stmt *setstmt) {
			sqlite3_bind_text(setstmt, 1, "tweetidsetcache", -1, SQLITE_STATIC);
			bind_compressed(setstmt, 2, settocompressedblob_desc(ad.unloaded_db_tweet_ids));
		},
		"dbconn::SyncWriteBackTweetIDIndexCache"
	);

	SyncWriteBackCIDSLists(syncdb);
	DBExec(syncdb, "DELETE FROM incrementaltweetids;", "dbconn::SyncWriteBackTweetIDIndexCache (incrementaltweetids)");

	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncWriteBackTweetIDIndexCache end, wrote %u", ad.unloaded_db_tweet_ids.size());
}

// This is called by SyncReadInAllTweetIDs
void dbconn::SyncReadInCIDSLists(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInCIDSLists start");
	sqlite3_stmt *getstmt = cache.GetStmt(adb, DBPSC_SELSTATICSETTING);

	unsigned int total = 0;
	auto doonelist = [&](std::string name, tweetidset &tlist) {
		sqlite3_bind_text(getstmt, 1, name.c_str(), name.size(), SQLITE_STATIC);
		DBRowExecStmt(adb, getstmt, [&](sqlite3_stmt *stmt) {
			setfromcompressedblob(tlist, getstmt, 0);
			total += tlist.size();
		}, "dbconn::SyncReadInCIDSLists");
		sqlite3_reset(getstmt);
	};

	cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
		doonelist(name, ad.cids.*ptr);
	});

	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInCIDSLists end, total: %u IDs", total);
}

namespace {
	template <typename T> struct WriteBackOutputter {
		std::shared_ptr<T> data;

		WriteBackOutputter(std::shared_ptr<T> d) : data(d) { }
		template <typename F> void operator()(F func) {
			for (auto&& item : *data) {
				func(std::move(item));
			}
		}
	};

	template <typename T> WriteBackOutputter<T> MakeWriteBackOutputter(std::shared_ptr<T> d) {
		return WriteBackOutputter<T>(std::move(d));
	}

	//Where T looks like WriteBackCIDSLists et al.
	template <typename T> void DoGenericSyncWriteBack(sqlite3 *adb, dbpscache &cache, T obj, std::string funcname) {
		obj.dbexec(adb, cache, funcname, false, obj);
	};

	//Where T looks like WriteBackCIDSLists et al.
	template <typename T> void DoGenericAsyncWriteBack(dbfunctionmsg &msg, T obj, std::string funcname) {
		auto items = std::make_shared<std::vector<typename T::itemdata> >();
		obj([&](typename T::itemdata data) {
			items->emplace_back(std::move(data));
		});
		msg.funclist.emplace_back([obj, items, funcname](sqlite3 *db, bool &ok, dbpscache &cache) {
			obj.dbexec(db, cache, funcname, true, MakeWriteBackOutputter(items));
		});
	};
};

namespace {
	struct WriteBackCIDSLists {
		struct itemdata {
			const char *name;
			db_bind_buffer_persistent<dbb_compressed> index;
			size_t list_size;
		};

		//Where F is a functor of the form void(itemdata &&)
		//F must free() itemdata::index
		template <typename F> void operator()(F func) const {
			cached_id_sets::IterateLists([&](const char *name, const tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
				const tweetidset &tlist = ad.cids.*ptr;
				func(itemdata { name, settocompressedblob_desc(tlist), tlist.size() });
			});
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s start", cstr(funcname));
			cache.BeginTransaction(adb);
			sqlite3_stmt *setstmt = cache.GetStmt(adb, DBPSC_INSSTATICSETTING);

			unsigned int total = 0;
			getfunc([&](itemdata &&data) {
				sqlite3_bind_text(setstmt, 1, data.name, -1, SQLITE_STATIC);
				bind_compressed(setstmt, 2, std::move(data.index));
				int res = sqlite3_step(setstmt);
				if (res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s), for set: %s",
						cstr(funcname), res, cstr(sqlite3_errmsg(adb)), cstr(data.name));
				}
				sqlite3_reset(setstmt);
				total += data.list_size;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s end, total: %u IDs", cstr(funcname), total);
		}
	};
};

void dbconn::SyncWriteBackCIDSLists(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackCIDSLists(), "dbconn::SyncWriteBackCIDSLists");
}

void dbconn::AsyncWriteBackCIDSLists(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackCIDSLists(), "dbconn::AsyncWriteBackCIDSLists");
}

namespace {
	struct WriteBackAccountIdLists {
		struct itemdata {
			std::string dispname;
			unsigned int dbindex;

			db_bind_buffer_persistent<dbb_compressed> tweet_blob;
			size_t tweet_count;

			db_bind_buffer_persistent<dbb_compressed> dm_blob;
			size_t dm_count;

			db_bind_buffer_persistent<dbb_compressed> blocked_blob;
			size_t blocked_count;

			db_bind_buffer_persistent<dbb_compressed> muted_blob;
			size_t muted_count;

			db_bind_buffer_persistent<dbb_compressed> no_rt_blob;
			size_t no_rt_count;
		};

		//Where F is a functor of the form void(itemdata &&)
		//F must free() itemdata::index
		template <typename F> void operator()(F func) const {
			for (auto &it : alist) {
				itemdata data;

				data.tweet_count = it->tweet_ids.size();
				data.tweet_blob = settocompressedblob_desc(it->tweet_ids);

				data.dm_count = it->dm_ids.size();
				data.dm_blob = settocompressedblob_desc(it->dm_ids);

				data.blocked_count = it->blocked_users.size();
				data.blocked_blob = settocompressedblob_desc(it->blocked_users);

				data.muted_count = it->muted_users.size();
				data.muted_blob = settocompressedblob_desc(it->muted_users);

				data.no_rt_count = it->no_rt_users.size();
				data.no_rt_blob = settocompressedblob_desc(it->no_rt_users);

				data.dispname = stdstrwx(it->dispname);
				data.dbindex = it->dbindex;

				func(std::move(data));
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s start", cstr(funcname));
			cache.BeginTransaction(adb);
			sqlite3_stmt *setstmt = cache.GetStmt(adb, DBPSC_UPDATEACCIDLISTS);

			unsigned int total = 0;
			getfunc([&](itemdata &&data) {
				bind_compressed(setstmt, 1, std::move(data.tweet_blob), 'Z');
				bind_compressed(setstmt, 2, std::move(data.dm_blob), 'Z');
				bind_compressed(setstmt, 3, std::move(data.blocked_blob), 'Z');
				bind_compressed(setstmt, 4, std::move(data.muted_blob), 'Z');
				bind_compressed(setstmt, 5, std::move(data.no_rt_blob), 'Z');
				sqlite3_bind_text(setstmt, 6, data.dispname.c_str(), data.dispname.size(), SQLITE_TRANSIENT);
				sqlite3_bind_int(setstmt, 7, data.dbindex);

				total += data.tweet_count + data.dm_count + data.blocked_count + data.muted_count + data.no_rt_count;

				int res = sqlite3_step(setstmt);
				if (res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s) for user dbindex: %d, name: %s",
							cstr(funcname), res, cstr(sqlite3_errmsg(adb)), data.dbindex, cstr(data.dispname));
				} else {
					SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s inserted account: dbindex: %d, name: %s, "
							"tweet IDs: %u, DM IDs: %u, blocked IDs: %u, muted IDs: %u, no RT IDs: %u",
							cstr(funcname), data.dbindex, cstr(data.dispname), data.tweet_count, data.dm_count, data.blocked_count,
							data.muted_count, data.no_rt_count);
				}
				sqlite3_reset(setstmt);
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s end, total: %u IDs", cstr(funcname), total);
		}
	};
};

void dbconn::SyncWriteBackAccountIdLists(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackAccountIdLists(), "dbconn::SyncWriteBackAccountIdLists");
}

void dbconn::AsyncWriteBackAccountIdLists(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackAccountIdLists(), "dbconn::AsyncWriteBackAccountIdLists");
}

namespace {
	struct WriteBackAllUsers {
		struct itemdata {
			uint64_t id;
			unsigned int dbindex;

			db_bind_buffer_persistent<dbb_compressed> json_blob;
			db_bind_buffer_persistent<dbb_compressed> profimg_blob;

			time_t createtime;
			uint64_t lastupdate;
			shb_iptr cached_profile_img_sha1;

			db_bind_buffer_persistent<dbb_compressed> mention_blob;

			uint64_t profile_img_last_used;

			bool user_needs_updating;
			bool profimgtime_needs_updating;
		};

		//Where F is a functor of the form void(itemdata &&)
		//F must free() itemdata blobs
		template <typename F> void operator()(F func) const {
			for (auto &it : ad.userconts) {
				udc_ptr_p u = it.second;

				if (u->user.screen_name.empty() && u->user.notes.empty()) {
					continue;    //don't bother saving empty user stubs
				}

				if (u->udc_flags & UDC::BEING_LOADED_FROM_DB) {
					continue;    //read of this user from DB in progress, do not try to write back
				}

				// User needs updating
				bool user_needs_updating = u->lastupdate != u->lastupdate_wrotetodb;

				// Profile image last used time needs updating (is more than 8 hours out)
				bool profimgtime_needs_updating = u->profile_img_last_used > u->profile_img_last_used_db + (8 * 60 * 60);

				if (!user_needs_updating && !profimgtime_needs_updating) {
					continue;    //this user does not need updating
				}

				u->lastupdate_wrotetodb = u->lastupdate;
				u->profile_img_last_used_db = u->profile_img_last_used;
				u->udc_flags |= UDC::SAVED_IN_DB;

				itemdata data;

				data.id = it.first;
				data.json_blob = DoCompress(u->mkjson(), 'J');
				data.profimg_blob = DoCompress(u->cached_profile_img_url, 'P');

				data.createtime = u->user.createtime;
				data.lastupdate = u->lastupdate;
				data.cached_profile_img_sha1 = u->cached_profile_img_sha1;

				data.mention_blob = settocompressedblob_desc(u->mention_set);

				data.profile_img_last_used = u->profile_img_last_used;

				data.user_needs_updating = user_needs_updating;
				data.profimgtime_needs_updating = profimgtime_needs_updating;

				func(std::move(data));
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s start", cstr(funcname));
			cache.BeginTransaction(adb);

			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSUSER);
			unsigned int user_count = 0;
			unsigned int lastupdate_count = 0;
			unsigned int profimgtime_count = 0;
			getfunc([&](itemdata &&data) {
				user_count++;
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) data.id);
				bind_compressed(stmt, 2, std::move(data.json_blob));
				bind_compressed(stmt, 3, std::move(data.profimg_blob));
				sqlite3_bind_int64(stmt, 4, (sqlite3_int64) data.createtime);
				sqlite3_bind_int64(stmt, 5, (sqlite3_int64) data.lastupdate);
				if (data.cached_profile_img_sha1) {
					sqlite3_bind_blob(stmt, 6, data.cached_profile_img_sha1->hash_sha1, sizeof(data.cached_profile_img_sha1->hash_sha1), SQLITE_TRANSIENT);
				} else {
					sqlite3_bind_null(stmt, 6);
				}
				bind_compressed(stmt, 7, std::move(data.mention_blob));
				sqlite3_bind_int64(stmt, 8, (sqlite3_int64) data.profile_img_last_used);

				if (data.user_needs_updating) lastupdate_count++;
				if (data.profimgtime_needs_updating) profimgtime_count++;

				int res = sqlite3_step(stmt);
				if (res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s) for user id: %" llFmtSpec "u",
							cstr(funcname),res, cstr(sqlite3_errmsg(adb)), data.id);
				} else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s inserted user id: %" llFmtSpec "u", cstr(funcname), data.id);
				}
				sqlite3_reset(stmt);
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s end, wrote back %u users (update: %u, prof img: %u)",
					cstr(funcname), user_count, lastupdate_count, profimgtime_count);
		}
	};
};

void dbconn::SyncWriteBackAllUsers(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackAllUsers(), "dbconn::SyncWriteBackAllUsers");
}

void dbconn::AsyncWriteBackAllUsers(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackAllUsers(), "dbconn::AsyncWriteBackAllUsers");
}

template <typename UDC> static void ReadInUserObject(sqlite3_stmt *stmt, uint64_t id, UDC &u, userdata &ud, const char *name) {
	rapidjson::Document dc;
	db_bind_buffer<dbb_uncompressed> json = column_get_compressed_and_parse(stmt, 0, dc);
	if (dc.IsObject()) {
		genjsonparser::ParseUserContents(dc, ud, genjsonparser::USERPARSEFLAGS::IS_DB_LOAD);
	}
	db_bind_buffer<dbb_uncompressed> profimg = column_get_compressed(stmt, 1);
	u.cached_profile_img_url.assign(profimg.data, profimg.data_size);
	if (ud.profile_img_url.empty()) {
		ud.profile_img_url.assign(profimg.data, profimg.data_size);
	}
	ud.createtime = (time_t) sqlite3_column_int64(stmt, 2);
	u.lastupdate = (uint64_t) sqlite3_column_int64(stmt, 3);
	u.lastupdate_wrotetodb = u.lastupdate;

	const char *hash = (const char*) sqlite3_column_blob(stmt, 4);
	int hashsize = sqlite3_column_bytes(stmt, 4);
	if (hashsize == sizeof(sha1_hash_block::hash_sha1)) {
		std::shared_ptr<sha1_hash_block> hashptr = std::make_shared<sha1_hash_block>();
		memcpy(hashptr->hash_sha1, hash, sizeof(sha1_hash_block::hash_sha1));
		u.cached_profile_img_sha1 = std::move(hashptr);
	} else {
		u.cached_profile_img_sha1.reset();
		if (hashsize) {
			TSLogMsgFormat(LOGT::DBERR, "%s user id: %" llFmtSpec "d, has invalid profile image hash length: %d", cstr(name), (sqlite3_int64) id, hashsize);
		}
	}

	setfromcompressedblob(u.mention_set, stmt, 5);
	u.profile_img_last_used = (uint64_t) sqlite3_column_int64(stmt, 6);
	u.profile_img_last_used_db = u.profile_img_last_used;
}

// Anything loaded here will be marked non-purgable
udc_ptr dbconn::SyncReadInUser(sqlite3 *syncdb, uint64_t id) {
	udc_ptr u = ad.GetUserContainerById(id);

	// Fetch only if in unloaded_user_ids, ie. in DB but not already loaded
	if (unloaded_user_ids.erase(id) > 0) {
		DBBindRowExec(syncdb, cache.GetStmt(syncdb, DBPSC_SELUSER),
			[&](sqlite3_stmt *getstmt) {
				sqlite3_bind_int64(getstmt, 1, (sqlite3_int64) id);
			},
			[&](sqlite3_stmt *getstmt) {
				ReadInUserObject(getstmt, id, *u, u->user, "dbconn::SyncReadInUser");
				u->udc_flags |= UDC::NON_PURGABLE | UDC::SAVED_IN_DB;
				sync_load_user_count++;
			},
			"dbconn::SyncReadInUser"
		);
	}
	return u;
}

void dbconn::AsyncReadInUser(sqlite3 *adb, uint64_t id, std::deque<dbretuserdata> &out) {
	// Fetch only if in unloaded_user_ids, ie. in DB but not already loaded
	if (unloaded_user_ids.erase(id) > 0) {
		DBBindRowExec(adb, cache.GetStmt(adb, DBPSC_SELUSER),
			[&](sqlite3_stmt *getstmt) {
				sqlite3_bind_int64(getstmt, 1, (sqlite3_int64) id);
			},
			[&](sqlite3_stmt *getstmt) {
				out.emplace_back();
				dbretuserdata &u = out.back();
				u.id = id;
				ReadInUserObject(getstmt, id, u, u.ud, "dbconn::AsyncReadInUser");
			},
			"dbconn::AsyncReadInUser"
		);
	}
}

// This must be called before all calls to SyncReadInUser and SyncPostUserLoadCompletion
void dbconn::SyncReadInAllUserIDs(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInAllUserIDs start");
	DBRowExec(adb, "SELECT id FROM users ORDER BY id DESC;", [&](sqlite3_stmt *getstmt) {
		uint64_t id = (uint64_t) sqlite3_column_int64(getstmt, 0);
		unloaded_user_ids.insert(unloaded_user_ids.end(), id);
	}, "dbconn::SyncReadInAllTweetIDs");
	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInAllUserIDs end, read %u", unloaded_user_ids.size());
}

void dbconn::SyncPostUserLoadCompletion() {
	ad.unloaded_db_user_ids = unloaded_user_ids;
	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInUser read %u", sync_load_user_count);
}

namespace {
	struct WriteBackUserDMIndexes {
		unsigned int alldmindexcount;

		WriteBackUserDMIndexes() : alldmindexcount(ad.user_dm_indexes.size()) { }

		struct itemdata {
			uint64_t id;

			db_bind_buffer_persistent<dbb_compressed> dmset_blob;
		};

		//Where F is a functor of the form void(itemdata &&)
		//F must free() itemdata blobs
		template <typename F> void operator()(F func) const {
			for (auto &it : ad.user_dm_indexes) {
				user_dm_index &udi = it.second;

				if (!(udi.flags & user_dm_index::UDIF::ISDIRTY)) {
					continue;
				}

				itemdata data;
				data.id = it.first;
				data.dmset_blob = settocompressedblob_desc(udi.ids);
				udi.flags &= ~user_dm_index::UDIF::ISDIRTY;

				func(std::move(data));
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s start", cstr(funcname));
			cache.BeginTransaction(adb);

			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSUSERDMINDEX);
			unsigned int count = 0;
			getfunc([&](itemdata &&data) {
				count++;
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) data.id);
				bind_compressed(stmt, 2, std::move(data.dmset_blob));

				int res = sqlite3_step(stmt);
				if (res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s) for user id: %" llFmtSpec "u",
							cstr(funcname),res, cstr(sqlite3_errmsg(adb)), data.id);
				} else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s inserted user id: %" llFmtSpec "u", cstr(funcname), data.id);
				}
				sqlite3_reset(stmt);
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s end, wrote back %u of %u user DM indexes",
					cstr(funcname), count, alldmindexcount);
		}
	};
};

void dbconn::SyncWriteBackUserDMIndexes(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackUserDMIndexes(), "dbconn::SyncWriteBackUserDMIndexes");
}

void dbconn::AsyncWriteBackUserDMIndexes(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackUserDMIndexes(), "dbconn::AsyncWriteBackUserDMIndexes");
}

// This must be called before SyncReadInAllTweetIDs
void dbconn::SyncReadInUserDMIndexes(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInUserDMIndexes start");

	unsigned int read_count = 0;
	tweetidset dmindex;
	DBRowExec(adb, "SELECT userid, dmindex FROM userdmsets;", [&](sqlite3_stmt *stmt) {
		setfromcompressedblob(dmindex, stmt, 1);
		if (!dmindex.empty()) {
			uint64_t userid = (uint64_t) sqlite3_column_int64(stmt, 0);
			user_dm_index &udi = ad.GetUserDMIndexById(userid);
			udi.ids = std::move(dmindex);
			dmindex.clear();
			read_count++;
			SyncReadInUser(adb, userid);
		}
	}, "dbconn::SyncReadInUserDMIndexes");

	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInUserDMIndexes end, read in %u", read_count);
}

namespace {
	struct WriteBackRBFSs {
		struct itemdata {
			restbackfillstate rbfs;
			unsigned int dbindex;
		};

		//Where F is a functor of the form void(const itemdata &)
		template <typename F> void operator()(F func) const {
			for (auto &it : alist) {
				taccount &acc = *it;
				for (restbackfillstate &rbfs : acc.pending_rbfs_list) {
					if (rbfs.start_tweet_id >= acc.GetMaxId(rbfs.type)) {
						continue;    //rbfs would be read next time anyway
					}
					if (!rbfs.end_tweet_id || rbfs.end_tweet_id >= acc.GetMaxId(rbfs.type)) {
						rbfs.end_tweet_id = acc.GetMaxId(rbfs.type);    //remove overlap
						if (rbfs.end_tweet_id) rbfs.end_tweet_id--;
					}
					func(itemdata { rbfs, acc.dbindex });
				}
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s start", cstr(funcname));

			cache.BeginTransaction(adb);
			sqlite3_exec(adb, "DELETE FROM rbfspending", 0, 0, 0);
			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSERTRBFSP);

			unsigned int write_count = 0;
			getfunc([&](const itemdata &data) {
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) data.dbindex);
				sqlite3_bind_int64(stmt, 2, (sqlite3_int64) data.rbfs.type);
				sqlite3_bind_int64(stmt, 3, (sqlite3_int64) data.rbfs.start_tweet_id);
				sqlite3_bind_int64(stmt, 4, (sqlite3_int64) data.rbfs.end_tweet_id);
				sqlite3_bind_int64(stmt, 5, (sqlite3_int64) data.rbfs.max_tweets_left);
				int res = sqlite3_step(stmt);
				if (res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s)", cstr(funcname), res, cstr(sqlite3_errmsg(adb)));
				} else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s inserted pending RBFS", cstr(funcname));
				}
				sqlite3_reset(stmt);
				write_count++;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s end, wrote %u", cstr(funcname), write_count);
		}
	};
};

void dbconn::SyncWriteOutRBFSs(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackRBFSs(), "dbconn::SyncWriteOutRBFSs");
}

void dbconn::AsyncWriteOutRBFSs(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackRBFSs(), "dbconn::AsyncWriteOutRBFSs");
}

void dbconn::SyncReadInRBFSs(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInRBFSs start");

	unsigned int read_count = 0;
	DBRowExec(adb, "SELECT accid, type, startid, endid, maxleft FROM rbfspending;", [&](sqlite3_stmt *stmt) {
		unsigned int dbindex = (unsigned int) sqlite3_column_int64(stmt, 0);
		bool found = false;
		for (auto &it : alist) {
			if (it->dbindex == dbindex) {
				RBFS_TYPE type = (RBFS_TYPE) sqlite3_column_int64(stmt, 1);
				if ((type < RBFS_MIN) || (type > RBFS_MAX)) break;

				it->pending_rbfs_list.emplace_front();
				restbackfillstate &rbfs = it->pending_rbfs_list.front();
				rbfs.type = type;
				rbfs.start_tweet_id = (uint64_t) sqlite3_column_int64(stmt, 2);
				rbfs.end_tweet_id = (uint64_t) sqlite3_column_int64(stmt, 3);
				rbfs.max_tweets_left = (uint64_t) sqlite3_column_int64(stmt, 4);
				rbfs.read_again = true;
				rbfs.started = false;
				found = true;
				read_count++;
				break;
			}
		}
		if (found) {
			LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInRBFSs retrieved RBFS");
		} else {
			LogMsgFormat(LOGT::DBERR, "dbconn::SyncReadInRBFSs retrieved RBFS with no associated account or bad type, ignoring");
		}
	}, "dbconn::SyncReadInRBFSs");
	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInRBFSs end, read in %u", read_count);
}

namespace {
	struct WriteBackHandleNewPendingOps {
		struct itemdata {
			unsigned int dbindex;
			flagwrapper<ARRIVAL> arr;
			uint64_t tweetid;
		};

		//Where F is a functor of the form void(const itemdata &)
		template <typename F> void operator()(F func) const {
			for (auto &it : ad.handlenew_pending_ops) {
				auto acc = it->tac.lock();
				if (acc) {
					func(itemdata { acc->dbindex, it->arr, it->tweet_id });
				}
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s start", cstr(funcname));

			cache.BeginTransaction(adb);
			DBExec(adb, cache.GetStmt(adb, DBPSC_CLEARHANDLENEWPENDINGS), "WriteBackHandleNewPendingOps");
			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSHANDLENEWPENDINGS);

			unsigned int write_count = 0;
			getfunc([&](const itemdata &data) {
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) data.dbindex);
				sqlite3_bind_int64(stmt, 2, (sqlite3_int64) data.arr);
				sqlite3_bind_int64(stmt, 3, (sqlite3_int64) data.tweetid);
				int res = sqlite3_step(stmt);
				if (res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s)", cstr(funcname), res, cstr(sqlite3_errmsg(adb)));
				} else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s inserted pending handle new", cstr(funcname));
				}
				sqlite3_reset(stmt);
				write_count++;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s end, wrote %u", cstr(funcname), write_count);
		}
	};
};

void dbconn::SyncWriteOutHandleNewPendingOps(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackHandleNewPendingOps(), "dbconn::SyncWriteOutHandleNewPendingOps");
}

void dbconn::AsyncWriteOutHandleNewPendingOps(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackHandleNewPendingOps(), "dbconn::AsyncWriteOutHandleNewPendingOps");
}

void dbconn::SyncReadInHandleNewPendingOps(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInHandleNewPendingOps start");

	unsigned int read_count = 0;
	DBRowExec(adb, "SELECT accid, arrivalflags, tweetid FROM handlenewpending;", [&](sqlite3_stmt *stmt) {
		unsigned int dbindex = (unsigned int) sqlite3_column_int64(stmt, 0);
		bool found = false;
		for (auto &it : alist) {
			if (it->dbindex == dbindex) {
				uint64_t tweetid = (uint64_t) sqlite3_column_int64(stmt, 2);
				ARRIVAL arrflags = static_cast<ARRIVAL>(sqlite3_column_int64(stmt, 1));
				post_init_callbacks.emplace_back([tweetid, arrflags, it](dbconn *dbc) {
					tweet_ptr t = ad.GetTweetById(tweetid);
					CheckFetchPendingSingleTweet(t, it);
					it->MarkPendingOrHandle(t, arrflags);
				});
				found = true;
				read_count++;
				LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInHandleNewPendingOps retrieved: %" llFmtSpec "d -> %s (0x%X)",
						tweetid, cstr(it->name), static_cast<unsigned int>(arrflags));
				break;
			}
		}
		if (!found) {
			LogMsgFormat(LOGT::DBERR, "dbconn::SyncReadInHandleNewPendingOps retrieved handlenewpending with no associated account or bad type, ignoring");
		}
	}, "dbconn::SyncReadInHandleNewPendingOps");
	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInHandleNewPendingOps end, read in %u", read_count);
}

void dbconn::SyncReadInAllMediaEntities(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInAllMediaEntities start");

	//This is to placate flaky mingw builds which get upset if this is put in the lambda
	const size_t hash_size = sizeof(sha1_hash_block::hash_sha1);

	unsigned int read_count = 0;
	unsigned int thumb_count = 0;
	unsigned int full_count = 0;
	DBRowExec(adb, "SELECT mid, tid, url, fullchecksum, thumbchecksum, flags, lastusedtimestamp FROM mediacache;", [&](sqlite3_stmt *stmt) {
		media_id_type id;
		id.m_id = (uint64_t) sqlite3_column_int64(stmt, 0);
		id.t_id = (uint64_t) sqlite3_column_int64(stmt, 1);
		media_entity *meptr = new media_entity;
		ad.media_list[id].reset(meptr);
		media_entity &me = *meptr;
		me.media_id = id;
		db_bind_buffer_persistent<dbb_uncompressed> url = column_get_compressed(stmt, 2);
		if (url.data_size) {
			me.media_url.assign(url.data, url.data_size);
			ad.img_media_map[me.media_url] = meptr;
		}
		if (sqlite3_column_bytes(stmt, 3) == hash_size) {
			std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
			memcpy(hash->hash_sha1, sqlite3_column_blob(stmt, 3), hash_size);
			me.full_img_sha1 = std::move(hash);
			me.flags |= MEF::LOAD_FULL;
			full_count++;
		}
		if (sqlite3_column_bytes(stmt, 4) == hash_size) {
			std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
			memcpy(hash->hash_sha1, sqlite3_column_blob(stmt, 4), hash_size);
			me.thumb_img_sha1 = std::move(hash);
			me.flags |= MEF::LOAD_THUMB;
			thumb_count++;
		}
		me.flags |= static_cast<MEF>(sqlite3_column_int64(stmt, 5));
		me.lastused = (uint64_t) sqlite3_column_int64(stmt, 6);
		read_count++;

		#if DB_COPIOUS_LOGGING
			LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInAllMediaEntities retrieved media entity %" llFmtSpec "d/%" llFmtSpec "d", id.m_id, id.t_id);
		#endif
	}, "dbconn::SyncReadInAllMediaEntities");

	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInAllMediaEntities end, read in %u, cached: thumb: %u, full: %u", read_count, thumb_count, full_count);
}

void dbconn::SyncReadInWindowLayout(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInWindowLayout start");

	DBRowExec(adb, "SELECT mainframeindex, x, y, w, h, maximised FROM mainframewins ORDER BY mainframeindex ASC;",
		[&](sqlite3_stmt *mfstmt) {
			ad.mflayout.emplace_back();
			mf_layout_desc &mfld = ad.mflayout.back();
			mfld.mainframeindex = (unsigned int) sqlite3_column_int(mfstmt, 0);
			mfld.pos.x = sqlite3_column_int(mfstmt, 1);
			mfld.pos.y = sqlite3_column_int(mfstmt, 2);
			mfld.size.SetWidth(sqlite3_column_int(mfstmt, 3));
			mfld.size.SetHeight(sqlite3_column_int(mfstmt, 4));
			mfld.maximised = (bool) sqlite3_column_int(mfstmt, 5);
		},
		"dbconn::SyncReadInWindowLayout (mainframewins)"
	);

	auto winssel = DBInitialiseSql(adb,
		"SELECT mainframeindex, splitindex, tabindex, name, dispname, flags, rowid, intersect_flags, tppw_flags FROM tpanelwins ORDER BY mainframeindex ASC, splitindex ASC, tabindex ASC;");
	auto winautossel = DBInitialiseSql(adb, "SELECT accid, autoflags FROM tpanelwinautos WHERE tpw == ?;");
	auto winudcautossel = DBInitialiseSql(adb, "SELECT userid, autoflags FROM tpanelwinudcautos WHERE tpw == ?;");

	DBRowExec(adb, winssel.stmt(),
		[&](sqlite3_stmt *stmt) {
			ad.twinlayout.emplace_back();
			twin_layout_desc &twld = ad.twinlayout.back();
			twld.mainframeindex = (unsigned int) sqlite3_column_int(stmt, 0);
			twld.splitindex = (unsigned int) sqlite3_column_int(stmt, 1);
			twld.tabindex = (unsigned int) sqlite3_column_int(stmt, 2);
			twld.name = (const char *) sqlite3_column_text(stmt, 3);
			twld.dispname = (const char *) sqlite3_column_text(stmt, 4);
			twld.flags = static_cast<TPF>(sqlite3_column_int(stmt, 5));
			sqlite3_int64 rowid = sqlite3_column_int(stmt, 6);
			twld.intersect_flags = static_cast<TPF_INTERSECT>(sqlite3_column_int(stmt, 7));
			twld.tppw_flags = static_cast<TPPWF>(sqlite3_column_int(stmt, 8));

			DBBindRowExec(adb, winautossel.stmt(),
				[&](sqlite3_stmt *stmt2) {
					sqlite3_bind_int(stmt2, 1, rowid);
				},
				[&](sqlite3_stmt *stmt2) {
					std::shared_ptr<taccount> acc;
					int accid = (int) sqlite3_column_int(stmt2, 0);
					if (accid > 0) {
						for (auto &it : alist) {
							if (it->dbindex == (unsigned int) accid) {
								acc = it;
								break;
							}
						}
						if (!acc) return;
					} else {
						acc.reset();
					}
					twld.tpautos.emplace_back();
					twld.tpautos.back().acc = acc;
					twld.tpautos.back().autoflags = static_cast<TPF>(sqlite3_column_int(stmt2, 1));
				},
				"dbconn::SyncReadInWindowLayout (tpanelwinautos)"
			);

			DBBindRowExec(adb, winudcautossel.stmt(),
				[&](sqlite3_stmt *stmt3) {
					sqlite3_bind_int(stmt3, 1, rowid);
				},
				[&](sqlite3_stmt *stmt3) {
					twld.tpudcautos.emplace_back();
					uint64_t uid = (uint64_t) sqlite3_column_int64(stmt3, 0);
					if (uid > 0) {
						twld.tpudcautos.back().u = ad.GetUserContainerById(uid);
					}
					twld.tpudcautos.back().autoflags = static_cast<TPFU>(sqlite3_column_int(stmt3, 1));
				},
				"dbconn::SyncReadInWindowLayout (tpanelwinudcautos)"
			);
		},
		"dbconn::SyncReadInWindowLayout (tpanelwins)"
	);

	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInWindowLayout end");
}

void dbconn::SyncWriteBackWindowLayout(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncWriteBackWindowLayout start");
	cache.BeginTransaction(adb);

	const std::string errspec = "dbconn::SyncWriteBackWindowLayout";
	DBExec(adb, "DELETE FROM mainframewins", errspec);

	auto mfins = DBInitialiseSql(adb, "INSERT INTO mainframewins (mainframeindex, x, y, w, h, maximised) VALUES (?, ?, ?, ?, ?, ?);");

	for (auto &mfld : ad.mflayout) {
		DBBindExec(adb, mfins.stmt(),
			[&](sqlite3_stmt *mfstmt) {
				sqlite3_bind_int(mfstmt, 1, mfld.mainframeindex);
				sqlite3_bind_int(mfstmt, 2, mfld.pos.x);
				sqlite3_bind_int(mfstmt, 3, mfld.pos.y);
				sqlite3_bind_int(mfstmt, 4, mfld.size.GetWidth());
				sqlite3_bind_int(mfstmt, 5, mfld.size.GetHeight());
				sqlite3_bind_int(mfstmt, 6, mfld.maximised);
			},
			"dbconn::SyncWriteOutWindowLayout (mainframewins)"
		);
	}

	DBExec(adb, "DELETE FROM tpanelwins", errspec);
	DBExec(adb, "DELETE FROM tpanelwinautos", errspec);
	DBExec(adb, "DELETE FROM tpanelwinudcautos", errspec);
	auto winsins = DBInitialiseSql(adb, "INSERT INTO tpanelwins (mainframeindex, splitindex, tabindex, name, dispname, flags, intersect_flags, tppw_flags) VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
	auto winautosins = DBInitialiseSql(adb, "INSERT INTO tpanelwinautos (tpw, accid, autoflags) VALUES (?, ?, ?);");
	auto winudcautosins = DBInitialiseSql(adb, "INSERT INTO tpanelwinudcautos (tpw, userid, autoflags) VALUES (?, ?, ?);");

	for (auto &twld : ad.twinlayout) {
		DBBindExec(adb, winsins.stmt(),
			[&](sqlite3_stmt *stmt) {
				sqlite3_bind_int(stmt, 1, twld.mainframeindex);
				sqlite3_bind_int(stmt, 2, twld.splitindex);
				sqlite3_bind_int(stmt, 3, twld.tabindex);
				sqlite3_bind_text(stmt, 4, twld.name.c_str(), twld.name.size(), SQLITE_STATIC);
				sqlite3_bind_text(stmt, 5, twld.dispname.c_str(), twld.dispname.size(), SQLITE_STATIC);
				sqlite3_bind_int(stmt, 6, flag_unwrap<TPF>(twld.flags));
				sqlite3_bind_int(stmt, 7, flag_unwrap<TPF_INTERSECT>(twld.intersect_flags));
				sqlite3_bind_int(stmt, 8, flag_unwrap<TPPWF>(twld.tppw_flags));
			},
			"dbconn::SyncWriteOutWindowLayout (tpanelwins)"
		);
		sqlite3_int64 rowid = sqlite3_last_insert_rowid(adb);

		for (auto &it : twld.tpautos) {
			DBBindExec(adb, winautosins.stmt(),
				[&](sqlite3_stmt *stmt) {
					sqlite3_bind_int(stmt, 1, rowid);
					if (it.acc) {
						sqlite3_bind_int(stmt, 2, it.acc->dbindex);
					} else {
						sqlite3_bind_int(stmt, 2, -1);
					}
					sqlite3_bind_int(stmt, 3, flag_unwrap<TPF>(it.autoflags));
				},
				"dbconn::SyncWriteOutWindowLayout (tpanelwinautos)"
			);
		}
		for (auto &it : twld.tpudcautos) {
			DBBindExec(adb, winudcautosins.stmt(),
				[&](sqlite3_stmt *stmt) {
					sqlite3_bind_int(stmt, 1, rowid);
					if (it.u) {
						sqlite3_bind_int64(stmt, 2, it.u->id);
					} else {
						sqlite3_bind_int64(stmt, 2, 0);
					}
					sqlite3_bind_int(stmt, 3, flag_unwrap<TPFU>(it.autoflags));
				},
				"dbconn::SyncWriteOutWindowLayout (tpanelwinudcautos)"
			);
		}
	}
	cache.EndTransaction(adb);
	LogMsg(LOGT::DBINFO, "dbconn::SyncWriteBackWindowLayout end");
}

void dbconn::SyncReadInTpanels(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInTpanels start");

	unsigned int read_count = 0;
	unsigned int id_count = 0;
	DBRowExec(adb, "SELECT name, dispname, flags, ids FROM tpanels;",
		[&](sqlite3_stmt *stmt) {
			std::string name = (const char *) sqlite3_column_text(stmt, 0);
			std::string dispname = (const char *) sqlite3_column_text(stmt, 1);
			flagwrapper<TPF> flags = static_cast<TPF>(sqlite3_column_int(stmt, 2));
			std::shared_ptr<tpanel> tp = tpanel::MkTPanel(name, dispname, flags);
			setfromcompressedblob(tp->tweetlist, stmt, 3);
			id_count += tp->tweetlist.size();
			tp->RecalculateCIDS();
			read_count++;
		},
		"dbconn::SyncReadInTpanels"
	);

	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInTpanels end, read in %u, IDs: %u", read_count, id_count);
}

namespace {
	struct WriteBackTpanels {
		struct itemdata {
			std::string name;
			std::string dispname;
			flagwrapper<TPF> flags;

			db_bind_buffer_persistent<dbb_compressed> tweetlist_blob;
			size_t tweetlist_count;
		};

		//Where F is a functor of the form void(itemdata &&)
		//F must free() itemdata::tweetlist_blob
		template <typename F> void operator()(F func) const {
			for (auto &it : ad.tpanels) {
				tpanel &tp = *(it.second);
				if (tp.flags & TPF::SAVETODB) {
					itemdata data;
					data.name = tp.name;
					data.dispname = tp.dispname;
					data.flags = tp.flags;
					data.tweetlist_blob = settocompressedblob_desc(tp.tweetlist);
					data.tweetlist_count = tp.tweetlist.size();
					func(std::move(data));
				}
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s start", cstr(funcname));

			cache.BeginTransaction(adb);
			sqlite3_exec(adb, "DELETE FROM tpanels", 0, 0, 0);
			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSTPANEL);

			unsigned int write_count = 0;
			unsigned int id_count = 0;
			getfunc([&](itemdata &&data) {
				sqlite3_bind_text(stmt, 1, data.name.c_str(), data.name.size(), SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 2, data.dispname.c_str(), data.dispname.size(), SQLITE_TRANSIENT);
				sqlite3_bind_int(stmt, 3, flag_unwrap<TPF>(data.flags));
				bind_compressed(stmt, 4, std::move(data.tweetlist_blob));
				int res = sqlite3_step(stmt);
				if (res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s)", cstr(funcname), res, cstr(sqlite3_errmsg(adb)));
				}
				sqlite3_reset(stmt);
				write_count++;
				id_count += data.tweetlist_count;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBINFO, TSLogging, "%s end, wrote %u, IDs: %u", cstr(funcname), write_count, id_count);
		}
	};
};

void dbconn::SyncWriteBackTpanels(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackTpanels(), "dbconn::SyncWriteBackTpanels");
}

void dbconn::AsyncWriteBackTpanels(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackTpanels(), "dbconn::AsyncWriteBackTpanels");
}

void dbconn::SyncReadInUserRelationships(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncReadInUserRelationships start");

	auto s = DBInitialiseSql(adb, "SELECT userid, flags, followmetime, ifollowtime FROM userrelationships WHERE accid == ?;");

	for (auto &it : alist) {
		unsigned int read_count = 0;

		DBBindRowExec(adb, s.stmt(),
			[&](sqlite3_stmt *stmt) {
				sqlite3_bind_int(stmt, 1, it->dbindex);
			},
			[&](sqlite3_stmt *stmt) {
				uint64_t id = (uint64_t) sqlite3_column_int64(stmt, 0);
				auto &ur = it->user_relations[id];
				ur.ur_flags = static_cast<user_relationship::URF>(sqlite3_column_int64(stmt, 1));
				ur.followsme_updtime = (time_t) sqlite3_column_int64(stmt, 2);
				ur.ifollow_updtime = (time_t) sqlite3_column_int64(stmt, 3);
				read_count++;
			},
			"dbconn::SyncReadInUserRelationships"
		);
		LogMsgFormat(LOGT::DBINFO, "dbconn::SyncReadInUserRelationships read in %u for account: %s", read_count, cstr(it->dispname));
	}
}

void dbconn::SyncWriteBackUserRelationships(sqlite3 *adb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncWriteBackUserRelationships start");

	cache.BeginTransaction(adb);
	sqlite3_exec(adb, "DELETE FROM userrelationships", 0, 0, 0);

	auto s = DBInitialiseSql(adb, "INSERT INTO userrelationships (accid, userid, flags, followmetime, ifollowtime) VALUES (?, ?, ?, ?, ?);");

	for (auto &it : alist) {
		unsigned int write_count = 0;

		for (auto &ur : it->user_relations) {
			using URF = user_relationship::URF;
			URF flags = ur.second.ur_flags;
			if (!(flags & (URF::FOLLOWSME_TRUE | URF::IFOLLOW_TRUE | URF::FOLLOWSME_PENDING | URF::IFOLLOW_PENDING))) {
				continue;
			}
			flags &= ~URF::QUERY_PENDING;

			DBBindExec(adb, s.stmt(),
				[&](sqlite3_stmt *stmt) {
					sqlite3_bind_int64(stmt, 1, it->dbindex);
					sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(ur.first));
					sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(flags));
					sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(ur.second.followsme_updtime));
					sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(ur.second.ifollow_updtime));
				},
				"dbconn::SyncWriteBackUserRelationships"
			);
			write_count++;
		}
		LogMsgFormat(LOGT::DBINFO, "dbconn::SyncWriteBackUserRelationships wrote %u for account: %s", write_count, cstr(it->dispname));
	}

	cache.EndTransaction(adb);
	LogMsg(LOGT::DBINFO, "dbconn::SyncWriteBackUserRelationships end");
}

bool dbconn::CheckIfPurgeDue(sqlite3 *db, time_t threshold, const char *settingname, const char *funcname, time_t &delta) {
	time_t last_purge = 0;

	sqlite3_stmt *getstmt = cache.GetStmt(db, DBPSC_SELSTATICSETTING);
	sqlite3_bind_text(getstmt, 1, settingname, -1, SQLITE_STATIC);
	DBRowExec(db, getstmt, [&](sqlite3_stmt *stmt) {
		last_purge = (time_t) sqlite3_column_int64(stmt, 0);
	}, string_format("%s (get last purged)", funcname));

	delta = time(nullptr) - last_purge;

	if (delta < threshold) {
		TSLogMsgFormat(LOGT::DBINFO, "%s, last purged %" llFmtSpec "ds ago, not checking", cstr(funcname), (int64_t) delta);
		return false;
	} else {
		return true;
	}
}

void dbconn::UpdateLastPurged(sqlite3 *db, const char *settingname, const char *funcname) {
	sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSSTATICSETTING);
	sqlite3_bind_text(stmt, 1, settingname, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, time(nullptr));
	DBExec(db, stmt, string_format("%s (write last purged)", funcname));
}

void dbconn::SyncPurgeMediaEntities(sqlite3 *syncdb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncPurgeMediaEntities start");

	const char *lastpurgesetting = "lastmediacachepurge";
	const char *funcname = "dbconn::SyncPurgeMediaEntities";
	const time_t day = 60 * 60 * 24;
	time_t delta;

	if (CheckIfPurgeDue(syncdb, day, lastpurgesetting, funcname, delta)) {
		unsigned int thumb_count = 0;
		unsigned int full_count = 0;

		std::deque<media_id_type> purge_list;

		DBBindRowExec(syncdb,
				"SELECT mid, tid, thumbchecksum, fullchecksum FROM mediacache WHERE ((flags == 0 OR flags IS NULL) AND lastusedtimestamp < ?);",
				[&](sqlite3_stmt *stmt) {
					sqlite3_bind_int64(stmt, 1, time(nullptr) - (day * gc.mediacachesavedays));
				},
				[&](sqlite3_stmt *stmt) {
					media_id_type mid;
					mid.m_id = (uint64_t) sqlite3_column_int64(stmt, 0);
					mid.t_id = (uint64_t) sqlite3_column_int64(stmt, 1);
					if (sqlite3_column_bytes(stmt, 2) > 0) {
						thumb_count++;
						if (!gc.readonlymode) {
							wxRemoveFile(media_entity::cached_thumb_filename(mid));
						}
					}
					if (sqlite3_column_bytes(stmt, 3) > 0) {
						full_count++;
						if (!gc.readonlymode) {
							wxRemoveFile(media_entity::cached_full_filename(mid));
						}
					}
					purge_list.push_back(mid);
				}, "dbconn::SyncPurgeMediaEntities (get purge list)");

		if (!gc.readonlymode) {
			cache.BeginTransaction(syncdb);

			DBRangeBindExec(syncdb, "DELETE FROM mediacache WHERE (mid == ? AND tid == ?);",
					purge_list.begin(), purge_list.end(),
					[&](sqlite3_stmt *stmt, media_id_type mid) {
						sqlite3_bind_int64(stmt, 1, mid.m_id);
						sqlite3_bind_int64(stmt, 2, mid.t_id);
					}, "dbconn::SyncPurgeMediaEntities (purge row)");

			UpdateLastPurged(syncdb, lastpurgesetting, funcname);

			cache.EndTransaction(syncdb);
		}

		LogMsgFormat(LOGT::DBINFO, "dbconn::SyncPurgeMediaEntities end, last purged %" llFmtSpec "ds ago, %spurged %u, (thumb: %u, full: %u)",
				(int64_t) delta, gc.readonlymode ? "would have " : "", (unsigned int) purge_list.size(), thumb_count, full_count);
	}
}

void dbconn::SyncPurgeProfileImages(sqlite3 *syncdb) {
	LogMsg(LOGT::DBINFO, "dbconn::SyncPurgeProfileImages start");

	const char *lastpurgesetting = "lastprofileimagepurge";
	const char *funcname = "dbconn::SyncPurgeProfileImages";
	const time_t day = 60 * 60 * 24;
	time_t delta;

	if (CheckIfPurgeDue(syncdb, day, lastpurgesetting, funcname, delta)) {
		std::deque<uint64_t> expire_list;

		// TODO: optimise this so it doesn't need a full table scan
		DBBindRowExec(syncdb,
				"SELECT id FROM users WHERE (cachedprofileimgchecksum IS NOT NULL AND profimglastusedtimestamp < ?);",
				[&](sqlite3_stmt *stmt) {
					sqlite3_bind_int64(stmt, 1, time(nullptr) - (day * gc.profimgcachesavedays));
				},
				[&](sqlite3_stmt *stmt) {
					// User has a profile image which can now be evicted from the disk cache
					uint64_t id = (uint64_t) sqlite3_column_int64(stmt, 0);
					expire_list.push_back(id);
				}, "dbconn::SyncPurgeProfileImages (get purge list)");

		if (!gc.readonlymode && expire_list.size()) {
			cache.BeginTransaction(syncdb);

			DBRangeBindExec(syncdb, "UPDATE users SET cachedprofimgurl = NULL, cachedprofileimgchecksum = NULL WHERE id == ?;",
					expire_list.begin(), expire_list.end(),
					[&](sqlite3_stmt *stmt, uint64_t id) {
						sqlite3_bind_int64(stmt, 1, id);

						wxString filename;
						userdatacontainer::GetImageLocalFilename(id, filename);
						wxRemoveFile(filename);
					}, "dbconn::SyncPurgeProfileImages (purge cache checksum)");

			UpdateLastPurged(syncdb, lastpurgesetting, funcname);

			cache.EndTransaction(syncdb);
		}

		LogMsgFormat(LOGT::DBINFO, "dbconn::SyncPurgeProfileImages end, last purged %" llFmtSpec "ds ago, %spurged %u",
				(int64_t) delta, gc.readonlymode ? "would have " : "", (unsigned int) expire_list.size());
	}
}

void dbconn::SyncPurgeUnreferencedTweets(sqlite3 *syncdb) {
	LogMsgFormat(LOGT::DBINFO, "dbconn::SyncPurgeUnreferencedTweets start: %zd tweets in total", ad.unloaded_db_tweet_ids.size());

	auto erase_ids_from = [&](tweetidset &set, const tweetidset &ids_to_remove) {
		for (uint64_t id : ids_to_remove) {
			set.erase(id);
		}
	};

	try {
		const char *lastpurgesetting = "lastunreferencedtweetspurge";
		const char *funcname = "dbconn::SyncPurgeUnreferencedTweets";
		const time_t day = 60 * 60 * 24;
		time_t delta;
		if (CheckIfPurgeDue(syncdb, day, lastpurgesetting, funcname, delta)) {
			std::vector<observer_ptr<tweetidset>> save_id_sets;

			save_id_sets.push_back(&(ad.cids.highlightids));
			save_id_sets.push_back(&(ad.cids.unreadids));

			for (auto &it : alist) {
				save_id_sets.push_back(&(it->dm_ids));
				save_id_sets.push_back(&(it->tweet_ids));
				save_id_sets.push_back(&(it->usercont->mention_set));
			}

			for (auto &it : ad.user_dm_indexes) {
				save_id_sets.push_back(&(it.second.ids));
			}

			for (auto &it : ad.tpanels) {
				if (it.second->flags & TPF::MANUAL) {
					save_id_sets.push_back(&(it.second->tweetlist));
				}
			}

			tweetidset delete_candidates = ad.unloaded_db_tweet_ids;
			for (auto &it : save_id_sets) {
				erase_ids_from(delete_candidates, *it);
			}

			LogMsgFormat(LOGT::DBINFO, "dbconn::SyncPurgeUnreferencedTweets: %zd candidate tweets to check", delete_candidates.size());

			tweetidset delete_ids;

			auto stmt = DBInitialiseSql(syncdb, "SELECT fromid FROM tweetxref WHERE toid = ?;");

			// iterate in descending ID order
			// newer tweets may depend on older ones, but not vice versa
			// the fromid field is the newer id, toid is the older id
			for (auto it = delete_candidates.begin(); it != delete_candidates.end(); ++it) {
				bool can_delete = true;
				DBBindRowExec(syncdb, stmt.stmt(),
						[&](sqlite3_stmt *stmt) {
							sqlite3_bind_int64(stmt, 1, *it);
						},
						[&](sqlite3_stmt *stmt) {
							if (!can_delete) return;

							uint64_t fromid = (uint64_t) sqlite3_column_int64(stmt, 0);
							if (delete_ids.find(fromid) == delete_ids.end()) {
								// xref to something which was not found in the list of ids marked for deletion
								can_delete = false;
							}
						}, db_throw_on_error("dbconn::SyncPurgeUnreferencedTweets (xref check)"));
				if (can_delete) {
					delete_ids.insert(*it);
				}
			}

			if (!gc.readonlymode) {
				DBExec(syncdb, "SAVEPOINT tweet_gc;", db_throw_on_error("dbconn::SyncPurgeUnreferencedTweets (savepoint)"));

				auto finaliser = scope_guard([&]() {
					DBExec(syncdb, "ROLLBACK TO SAVEPOINT tweet_gc;", "dbconn::SyncPurgeUnreferencedTweets (rollback)");
				});

				DBRangeBindExec(syncdb, "DELETE FROM tweets WHERE id = ?;",
						delete_ids.begin(), delete_ids.end(),
						[&](sqlite3_stmt *stmt, uint64_t id) {
							sqlite3_bind_int64(stmt, 1, id);
						},
						db_throw_on_error("dbconn::SyncPurgeUnreferencedTweets (xref check)"));

				DBRangeBindExec(syncdb, "DELETE FROM tweetxref WHERE fromid = ? OR toid = ?;",
						delete_ids.begin(), delete_ids.end(),
						[&](sqlite3_stmt *stmt, uint64_t id) {
							sqlite3_bind_int64(stmt, 1, id);
							sqlite3_bind_int64(stmt, 2, id);
						},
						db_throw_on_error("dbconn::SyncPurgeUnreferencedTweets (xref check)"));

				UpdateLastPurged(syncdb, lastpurgesetting, funcname);

				DBExec(syncdb, "RELEASE SAVEPOINT tweet_gc;", db_throw_on_error("dbconn::SyncPurgeUnreferencedTweets (unlock)"));

				// do this after DB operations
				erase_ids_from(ad.cids.hiddenids, delete_ids);
				erase_ids_from(ad.cids.deletedids, delete_ids);
				erase_ids_from(ad.unloaded_db_tweet_ids, delete_ids);

				finaliser.cancel();
			}

			LogMsgFormat(LOGT::DBINFO, "dbconn::SyncPurgeUnreferencedTweets end, last purged %" llFmtSpec "ds ago, %spurged %zd",
					(int64_t) delta, gc.readonlymode ? "would have " : "", delete_ids.size());
		}
	} catch (std::exception &e) {
		LogMsgFormat(LOGT::DBERR, "dbconn::SyncPurgeUnreferencedTweets failed, rolling back. Exception: %s", cstr(e.what()));
	}
}

void dbconn::OnAsyncStateWriteTimer(wxTimerEvent& event) {
	AsyncWriteBackState();
	ResetAsyncStateWriteTimer();
}

void dbconn::ResetAsyncStateWriteTimer() {
	if (gc.asyncstatewritebackintervalmins > 0) {
		asyncstateflush_timer->Start(gc.asyncstatewritebackintervalmins * 1000 * 60, wxTIMER_ONE_SHOT);
	}
}

struct tweet_id_timestamp_info {
	time_t now;
	container::map<time_t, uint64_t> timestamp_map;
};

static tweet_id_timestamp_info CreateTweetIdTimestampMap() {
	tweet_id_timestamp_info out;

	out.now = time(nullptr);

	for (auto &it : alist) {
		if (it->expire_tweets_days > 0) {
			out.timestamp_map[out.now - (it->expire_tweets_days * 24 * 60 * 60)] = 0;
		}
	}
	return out;
}

// returns 0 if none found
static uint64_t DBGetNewestTweetOlderThan(sqlite3 *db, dbpscache &cache, time_t timestamp) {
	uint64_t id = 0;
	DBBindRowExec(db, cache.GetStmt(db, DBPSC_SELTWEETIDBYTIMESTAMP),
			[&](sqlite3_stmt *stmt) {
				sqlite3_bind_int64(stmt, 1, timestamp);
			},
			[&](sqlite3_stmt *stmt) {
				id = (uint64_t) sqlite3_column_int64(stmt, 0);
			}, "DBGetNewestTweetOlderThan");
	return id;
}

static void FillTweetIdTimestampMap(sqlite3 *db, dbpscache &cache, tweet_id_timestamp_info &info) {
	for (auto &it : info.timestamp_map) {
		it.second = DBGetNewestTweetOlderThan(db, cache, it.first);
	}
}

static void TimelineExpireOldTweets(const tweet_id_timestamp_info &info) {
	LogMsgFormat(LOGT::DBINFO, "TimelineExpireOldTweets: start");
	for (auto &acc : alist) {
		if (acc->expire_tweets_days > 0) {
			time_t expire_time = info.now - (acc->expire_tweets_days * 24 * 60 * 60);
			uint64_t expire_id = 0;
			auto it = info.timestamp_map.find(expire_time);
			if (it != info.timestamp_map.end()) expire_id = it->second;
			if (expire_id != 0) {
				size_t old_size = acc->tweet_ids.size();
				size_t unread_skipped = 0;
				auto iter = acc->tweet_ids.lower_bound(expire_id); // finds the first id *less than or equal to* expire_id
				if (iter != acc->tweet_ids.end() && !ad.cids.unreadids.empty() && *iter < *ad.cids.unreadids.rbegin()) {
					// if the upper limit of the IDs to remove is less than the lower limit of unread IDs, there is no overlap
					acc->tweet_ids.erase(iter, acc->tweet_ids.end());
				} else {
					// check unread set
					auto delete_iter = iter;
					for (size_t count = 1; iter != acc->tweet_ids.end(); count++) {
						uint64_t id = *iter;
						++iter;
						if (ad.cids.unreadids.find(id) != ad.cids.unreadids.end()) {
							// id in unreadids, do not delete this or prior items
							delete_iter = iter;
							unread_skipped = count;
						}
					}
					acc->tweet_ids.erase(delete_iter, acc->tweet_ids.end());
				}

				if (old_size != acc->tweet_ids.size()) {
					for (auto &it : ad.tpanels) {
						tpanel &tp = *(it.second);
						if (tp.AccountTimelineMatches(acc)) {
							tp.RecalculateSets();
							tp.UpdateCLabelLater_TP();
						}
					}
				}

				LogMsgFormat(LOGT::DBINFO, "TimelineExpireOldTweets: account name: %s, orig size: %zu, removed: %zu, not removed as unread: %zu, left: %zu",
						cstr(acc->dispname), old_size, old_size - acc->tweet_ids.size(), unread_skipped, acc->tweet_ids.size());
			}
		}
	}
}

void dbconn::SyncPurgeOldTweets(sqlite3 *syncdb) {
	tweet_id_timestamp_info timestamp_map = CreateTweetIdTimestampMap();
	FillTweetIdTimestampMap(syncdb, cache, timestamp_map);
	TimelineExpireOldTweets(timestamp_map);
}

void dbconn::OnAsyncPurgeOldTweetsTimer(wxTimerEvent& event) {
	AsyncPurgeOldTweets();
	ResetPurgeOldTweetsTimer();
}

void dbconn::AsyncPurgeOldTweets() {
	LogMsgFormat(LOGT::DBTRACE, "dbconn::OnAsyncPurgeOldTweetsTimer: start");

	struct db_get_timestamp_id_msg : public dbfunctionmsg_callback {
		tweet_id_timestamp_info timestamp_map;
	};

	std::unique_ptr<db_get_timestamp_id_msg> msg(new db_get_timestamp_id_msg());
	msg->timestamp_map = CreateTweetIdTimestampMap();

	msg->db_func = [](sqlite3 *db, bool &ok, dbpscache &cache, dbfunctionmsg_callback &self_) {
		// We are now in the DB thread

		db_get_timestamp_id_msg &self = static_cast<db_get_timestamp_id_msg &>(self_);
		FillTweetIdTimestampMap(db, cache, self.timestamp_map);
	};

	msg->callback_func = [](std::unique_ptr<dbfunctionmsg_callback> self_) {
		LogMsgFormat(LOGT::DBTRACE, "dbconn::OnAsyncPurgeOldTweetsTimer: got reply from DB thread");

		db_get_timestamp_id_msg &self = static_cast<db_get_timestamp_id_msg &>(*self_);
		TimelineExpireOldTweets(self.timestamp_map);

		LogMsgFormat(LOGT::DBTRACE, "dbconn::OnAsyncPurgeOldTweetsTimer: end");
	};

	SendFunctionMsgCallback(std::move(msg));
}

void dbconn::ResetPurgeOldTweetsTimer() {
	if (gc.asyncpurgeoldtweetsintervalmins > 0) {
		asyncpurgeoldtweets_timer->Start(gc.asyncpurgeoldtweetsintervalmins * 1000 * 60, wxTIMER_ONE_SHOT);
	}
}

void dbconn::AsyncGetNewestTweetOlderThan(time_t timestamp, std::function<void(uint64_t)> completion) {
	struct msgobj : public dbfunctionmsg_callback {
		time_t timestamp;
		uint64_t tweet_id;
		std::function<void(uint64_t)> completion;
	};

	std::unique_ptr<msgobj> msg(new msgobj());
	msg->timestamp = timestamp;
	msg->completion = std::move(completion);

	msg->db_func = [](sqlite3 *db, bool &ok, dbpscache &cache, dbfunctionmsg_callback &self_) {
		// We are now in the DB thread
		msgobj &self = static_cast<msgobj &>(self_);

		self.tweet_id = DBGetNewestTweetOlderThan(db, cache, self.timestamp);
	};

	msg->callback_func = [](std::unique_ptr<dbfunctionmsg_callback> self_) {
		msgobj &self = static_cast<msgobj &>(*self_);
		self.completion(self.tweet_id);
	};

	SendFunctionMsgCallback(std::move(msg));
}

void dbconn::SyncClearDirtyFlag(sqlite3 *db) {
	sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_DELSTATICSETTING);
	sqlite3_bind_text(stmt, 1, "dirtyflag", -1, SQLITE_STATIC);
	DBExec(db, stmt, "dbconn::ClearDirtyFlag");
}

// set acc_db_index to < 0, to only query by obj_id
// set acc_db_index to >= 0, to query by obj_id OR acc db_index
void dbconn::AsyncSelEventLogByObj(uint64_t obj_id, int acc_db_index, std::function<void(std::deque<dbeventlogdata>)> completion) {
	LogMsgFormat(LOGT::DBTRACE, "dbconn::AsyncSelEventLogByObj: start");

	FlushBatchQueue();

	struct dbseleventlogmsgbyobj : public dbfunctionmsg_callback {
		uint64_t obj_id;
		int acc_db_index;
		std::deque<dbeventlogdata> data;
		std::function<void(std::deque<dbeventlogdata>)> completion;
	};

	std::unique_ptr<dbseleventlogmsgbyobj> msg(new dbseleventlogmsgbyobj());
	msg->obj_id = obj_id;
	msg->acc_db_index = acc_db_index;
	msg->completion = std::move(completion);

	msg->db_func = [](sqlite3 *db, bool &ok, dbpscache &cache, dbfunctionmsg_callback &self_) {
		// We are now in the DB thread
		dbseleventlogmsgbyobj &self = static_cast<dbseleventlogmsgbyobj &>(self_);

		DBBindRowExec(db, cache.GetStmt(db, self.acc_db_index >= 0 ? DBPSC_SELEVENTLOGBYOBJ_ACCID : DBPSC_SELEVENTLOGBYOBJ),
			[&](sqlite3_stmt *stmt) {
				sqlite3_bind_int64(stmt, 1, self.obj_id);
				if (self.acc_db_index >= 0) {
					sqlite3_bind_int(stmt, 2, self.acc_db_index);
				}
			},
			[&](sqlite3_stmt *stmt) {
				self.data.emplace_back();
				dbeventlogdata &entry = self.data.back();
				entry.id = (uint64_t) sqlite3_column_int64(stmt, 0);
				entry.accid = sqlite3_column_int(stmt, 1);
				entry.event_type = static_cast<DB_EVENTLOG_TYPE>(sqlite3_column_int(stmt, 2));
				entry.flags = static_cast<DBELF>(sqlite3_column_int(stmt, 3));
				entry.eventtime = (uint64_t) sqlite3_column_int64(stmt, 4);
				db_bind_buffer<dbb_uncompressed> extrajson = column_get_compressed(stmt, 5);
				entry.extrajson.assign(extrajson.data, extrajson.data_size);
				if (self.acc_db_index >= 0) {
					entry.obj =  (uint64_t) sqlite3_column_int64(stmt, 6);
				} else {
					entry.obj = self.obj_id;
				}
			},
			"dbconn::AsyncSelEventLogByObj"
		);
	};

	msg->callback_func = [](std::unique_ptr<dbfunctionmsg_callback> self_) {
		dbseleventlogmsgbyobj &self = static_cast<dbseleventlogmsgbyobj &>(*self_);

		LogMsgFormat(LOGT::DBTRACE, "dbconn::AsyncSelEventLogByObj: got reply from DB thread (%" llFmtSpec "d, %d, %zu)",
				self.obj_id, self.acc_db_index, self.data.size());

		self.completion(std::move(self.data));

		LogMsgFormat(LOGT::DBTRACE, "dbconn::AsyncSelEventLogByObj: end");
	};

	SendFunctionMsgCallback(std::move(msg));
}

//The contents of data will be released and stashed in the event sent to the main thread
//The main thread will then unstash it from the event and stick it back in a unique_ptr
void dbsendmsg_callback::SendReply(std::unique_ptr<dbsendmsg> data, dbiothread *th) {
	wxCommandEvent *evt = new wxCommandEvent(cmdevtype, winid);
	evt->SetClientData(data.release());
	th->reply_list.emplace_back(targ, std::unique_ptr<wxEvent>(evt));
}

db_handle_msg_pending_guard::~db_handle_msg_pending_guard() {
	if (users.empty() && tweets.empty())
		return;

	LogMsgFormat(LOGT::PENDTRACE, "db_handle_msg_pending_guard::~db_handle_msg_pending_guard: have %zu users, %zu tweets", users.size(), tweets.size());

	for (auto &it : users) {
		it->CheckPendingTweets();
	}
	for (auto &it : tweets) {
		TryUnmarkPendingTweet(it, UMPTF::TPDB_NOUPDF);
	}
	for (auto &it : alist) {
		if (it->enabled) {
			it->StartRestQueryPendings();
		}
	}
	CheckClearNoUpdateFlag_All();
}

void DBGenConfig::SetDBIndexGlobal() {
	dbindextype = DBI_TYPE::GLOBAL;
}

void DBGenConfig::SetDBIndexDB() {
	dbindextype = DBI_TYPE::DB;
}

void DBGenConfig::SetDBIndex(unsigned int id) {
	dbindextype = DBI_TYPE::ACC;
	dbindex = id;
}
void DBGenConfig::bind_accid_name(sqlite3_stmt *stmt, const char *name) {
	switch (dbindextype) {
		case DBI_TYPE::GLOBAL:
			sqlite3_bind_text(stmt, 1, globstr.c_str(), globstr.size(), SQLITE_STATIC);
			break;

		case DBI_TYPE::DB:
			sqlite3_bind_text(stmt, 1, globdbstr.c_str(), globdbstr.size(), SQLITE_STATIC);
			break;

		case DBI_TYPE::ACC:
			sqlite3_bind_int(stmt, 1, dbindex);
			break;
	}
	sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
}

DBGenConfig::DBGenConfig(sqlite3 *db_)
		: dbindextype(DBI_TYPE::GLOBAL), dbindex(0), db(db_) { }

DBWriteConfig::DBWriteConfig(sqlite3 *db_) : DBGenConfig(db_) {
	dbc.cache.BeginTransaction(db);
}

DBWriteConfig::~DBWriteConfig() {
	dbc.cache.EndTransaction(db);
}
void DBWriteConfig::WriteUTF8(const char *name, const char *strval) {
	sqlite3_stmt *stmt = dbc.cache.GetStmt(db, DBPSC_INSSETTING);
	bind_accid_name(stmt, name);
	sqlite3_bind_text(stmt, 3, strval, -1, SQLITE_TRANSIENT);
	exec(stmt);
}
void DBWriteConfig::WriteInt64(const char *name, int64_t val) {
	sqlite3_stmt *stmt = dbc.cache.GetStmt(db, DBPSC_INSSETTING);
	bind_accid_name(stmt, name);
	sqlite3_bind_int64(stmt, 3, val);
	exec(stmt);
}
void DBWriteConfig::Delete(const char *name) {
	sqlite3_stmt *delstmt = dbc.cache.GetStmt(db, DBPSC_DELSETTING);
	bind_accid_name(delstmt, name);
	exec(delstmt);
}
void DBWriteConfig::DeleteAll() {
	DBExec(db, "DELETE FROM settings;", "DBWriteConfig::DeleteAll");
}

void DBWriteConfig::exec(sqlite3_stmt *wstmt) {
	DBExec(db, wstmt, "DBWriteConfig");
}

DBReadConfig::DBReadConfig(sqlite3 *db_) : DBGenConfig(db_) {
}

DBReadConfig::~DBReadConfig() {
}

bool DBReadConfig::exec(sqlite3_stmt *rstmt) {
	int res = sqlite3_step(rstmt);
	if (res == SQLITE_ROW) {
		return true;
	} else if (res == SQLITE_DONE) {
		return false;
	} else {
		TSLogMsgFormat(LOGT::DBERR, "DBReadConfig got error: %d (%s)", res, cstr(sqlite3_errmsg(db)));
		return false;
	}
}

bool DBReadConfig::Read(const char *name, wxString *strval, const wxString &defval) {
	sqlite3_stmt *stmt = dbc.cache.GetStmt(db, DBPSC_SELSETTING);
	bind_accid_name(stmt, name);
	bool ok = exec(stmt);
	if (ok) {
		const char *text = (const char *) sqlite3_column_text(stmt, 0);
		int len = sqlite3_column_bytes(stmt, 0);
		*strval = wxstrstd(text, len);
	} else {
		*strval = defval;
	}
	sqlite3_reset(stmt);
	return ok;
}

bool DBReadConfig::ReadBool(const char *name, bool *strval, bool defval) {
	int64_t value;
	bool res = ReadInt64(name, &value, (int64_t) defval);
	*strval = (bool) value;
	return res;
}

bool DBReadConfig::ReadUInt64(const char *name, uint64_t *strval, uint64_t defval) {
	return ReadInt64(name, (int64_t *) strval, (int64_t) defval);
}

bool DBReadConfig::ReadUInt(const char *name, unsigned int *strval, unsigned int defval) {
	int64_t val = *strval;
	bool ok = ReadInt64(name, &val, (unsigned int) defval);
	*strval = (unsigned int) val;
	return ok;
}

bool DBReadConfig::ReadInt64(const char *name, int64_t *strval, int64_t defval) {
	sqlite3_stmt *stmt = dbc.cache.GetStmt(db, DBPSC_SELSETTING);
	bind_accid_name(stmt, name);
	bool ok = exec(stmt);
	if (ok) {
		*strval = sqlite3_column_int64(stmt, 0);
	} else {
		*strval = defval;
	}
	sqlite3_reset(stmt);
	return ok;
}

bool DBC_Init(const std::string &filename) {
	return dbc.Init(filename);
}

void DBC_DeInit() {
	dbc.DeInit();
}

void DBC_AsyncWriteBackState() {
	dbc.AsyncWriteBackState();
}

void DBC_AsyncWriteBackStateMinimal() {
	dbc.AsyncWriteBackStateMinimal();
}

void DBC_SendMessage(std::unique_ptr<dbsendmsg> msg) {
	dbc.SendMessage(std::move(msg));
}

void DBC_SendMessageBatchedOrAddToList(std::unique_ptr<dbsendmsg> msg, dbsendmsg_list *msglist) {
	dbc.SendMessageBatchedOrAddToList(std::move(msg), msglist);
}

void DBC_SendMessageBatched(std::unique_ptr<dbsendmsg> msg) {
	dbc.SendMessageBatched(std::move(msg));
}

observer_ptr<dbsendmsg_list> DBC_GetMessageBatchQueue() {
	return dbc.GetMessageBatchQueue();
}

void DBC_SendBatchedTweetFlagUpdate(uint64_t id, uint64_t setmask, uint64_t unsetmask) {
	dbc.SendBatchedTweetFlagUpdate(id, setmask, unsetmask);
}

void DBC_SendAccDBUpdate(std::unique_ptr<dbinsertaccmsg> insmsg) {
	dbc.SendAccDBUpdate(std::move(insmsg));
}

void DBC_InsertMedia(media_entity &me, optional_observer_ptr<dbsendmsg_list> msglist) {
	dbc.InsertMedia(me, msglist);
}

void DBC_UpdateMedia(media_entity &me, DBUMMT update_type, optional_observer_ptr<dbsendmsg_list> msglist) {
	dbc.UpdateMedia(me, update_type, msglist);
}

void DBC_InsertNewEventLogEntry(optional_observer_ptr<dbsendmsg_list> msglist, optional_observer_ptr<taccount> acc, DB_EVENTLOG_TYPE type,
		flagwrapper<DBELF> flags, uint64_t obj, time_t eventtime, std::string extrajson) {
	dbc.InsertNewEventLogEntry(msglist, acc, type, flags, obj, eventtime, extrajson);
}

void DBC_InsertNewTweet(tweet_ptr_p tobj, std::string statjson, optional_observer_ptr<dbsendmsg_list> msglist) {
	dbc.InsertNewTweet(tobj, statjson, msglist);
}

void DBC_UpdateTweetDyn(tweet_ptr_p tobj, optional_observer_ptr<dbsendmsg_list> msglist) {
	dbc.UpdateTweetDyn(tobj, msglist);
}

void DBC_InsertUser(udc_ptr_p u, optional_observer_ptr<dbsendmsg_list> msglist) {
	dbc.InsertUser(u, msglist);
}

void DBC_HandleDBSelTweetMsg(dbseltweetmsg &msg, optional_observer_ptr<db_handle_msg_pending_guard> pending_guard) {
	dbc.HandleDBSelTweetMsg(msg, pending_guard);
}

void DBC_SetDBSelTweetMsgHandler(dbseltweetmsg &msg, std::function<void(dbseltweetmsg &, dbconn *)> f) {
	dbc.SetDBSelTweetMsgHandler(msg, std::move(f));
}

void DBC_PrepareStdTweetLoadMsg(dbseltweetmsg &loadmsg) {
	dbc.PrepareStdTweetLoadMsg(loadmsg);
}

void DBC_DBSelUserReturnDataHandler(std::deque<dbretuserdata> data, optional_observer_ptr<db_handle_msg_pending_guard> pending_guard) {
	dbc.DBSelUserReturnDataHandler(std::move(data), pending_guard);
}

void DBC_SetDBSelUserMsgHandler(dbselusermsg &msg, std::function<void(dbselusermsg &, dbconn *)> f) {
	dbc.SetDBSelUserMsgHandler(msg, std::move(f));
}

void DBC_PrepareStdUserLoadMsg(dbselusermsg &loadmsg) {
	dbc.PrepareStdUserLoadMsg(loadmsg);
}

void DBC_AsyncPurgeOldTweets() {
	dbc.AsyncPurgeOldTweets();
}

void DBC_AsyncGetNewestTweetOlderThan(time_t timestamp, std::function<void(uint64_t)> completion) {
	dbc.AsyncGetNewestTweetOlderThan(timestamp, completion);
}

void DBC_AsyncSelEventLogByObj(uint64_t obj_id, int acc_db_index, std::function<void(std::deque<dbeventlogdata>)> completion) {
	dbc.AsyncSelEventLogByObj(obj_id, acc_db_index, std::move(completion));
}
