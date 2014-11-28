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
#include "tpanel.h"
#include "tpanel-data.h"
#include "set.h"
#include "map.h"
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
"CREATE TABLE IF NOT EXISTS tweets(id INTEGER PRIMARY KEY NOT NULL, statjson BLOB, dynjson BLOB, userid INTEGER, userrecipid INTEGER, flags INTEGER, timestamp INTEGER, rtid INTEGER);"
"CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY NOT NULL, json BLOB, cachedprofimgurl BLOB, createtimestamp INTEGER, lastupdatetimestamp INTEGER, cachedprofileimgchecksum BLOB, mentionindex BLOB, profimglastusedtimestamp INTEGER);"
"CREATE TABLE IF NOT EXISTS acc(id INTEGER PRIMARY KEY NOT NULL, name TEXT, dispname TEXT, json BLOB, tweetids BLOB, dmids BLOB, userid INTEGER);"
"CREATE TABLE IF NOT EXISTS settings(accid BLOB, name TEXT, value BLOB, PRIMARY KEY (accid, name));"
"CREATE TABLE IF NOT EXISTS rbfspending(accid INTEGER, type INTEGER, startid INTEGER, endid INTEGER, maxleft INTEGER);"
"CREATE TABLE IF NOT EXISTS mediacache(mid INTEGER, tid INTEGER, url BLOB, fullchecksum BLOB, thumbchecksum BLOB, flags INTEGER, lastusedtimestamp INTEGER, PRIMARY KEY (mid, tid));"
"CREATE TABLE IF NOT EXISTS tpanelwins(mainframeindex INTEGER, splitindex INTEGER, tabindex INTEGER, name TEXT, dispname TEXT, flags INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanelwinautos(tpw INTEGER, accid INTEGER, autoflags INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanelwinudcautos(tpw INTEGER, userid INTEGER, autoflags INTEGER);"
"CREATE TABLE IF NOT EXISTS mainframewins(mainframeindex INTEGER, x INTEGER, y INTEGER, w INTEGER, h INTEGER, maximised INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanels(name TEXT, dispname TEXT, flags INTEGER, ids BLOB);"
"CREATE TABLE IF NOT EXISTS staticsettings(name TEXT PRIMARY KEY NOT NULL, value BLOB);"
"CREATE TABLE IF NOT EXISTS userrelationships(accid INTEGER, userid INTEGER, flags INTEGER, followmetime INTEGER, ifollowtime INTEGER);"
"CREATE TABLE IF NOT EXISTS userdmsets(userid INTEGER PRIMARY KEY NOT NULL, dmindex BLOB);"
"INSERT OR REPLACE INTO settings(accid, name, value) VALUES ('G', 'dirtyflag', strftime('%s','now'));";

static const char *std_sql_stmts[DBPSC_NUM_STATEMENTS]={
	"INSERT OR REPLACE INTO tweets(id, statjson, dynjson, userid, userrecipid, flags, timestamp, rtid) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
	"UPDATE tweets SET dynjson = ?, flags = ? WHERE id == ?;",
	"BEGIN;",
	"COMMIT;",
	"INSERT OR REPLACE INTO users(id, json, cachedprofimgurl, createtimestamp, lastupdatetimestamp, cachedprofileimgchecksum, mentionindex, profimglastusedtimestamp) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
	"INSERT INTO acc(name, dispname, userid) VALUES (?, ?, ?);",
	"UPDATE acc SET tweetids = ?, dmids = ?, dispname = ? WHERE id == ?;",
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
};

static const std::string globstr = "G";
static const std::string globdbstr = "D";

#define DBLogMsgFormat TSLogMsgFormat
#define DBLogMsg TSLogMsg

static int busy_handler_callback(void *ptr, int count) {
	if(count < 7) {    //this should lead to a maximum wait of ~3.2s
		unsigned int sleeplen = 25 << count;
		wxThread *th = wxThread::This();
		if(th) th->Sleep(sleeplen);
		else wxMilliSleep(sleeplen);
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

sqlite3_stmt *dbpscache::GetStmt(sqlite3 *adb, DBPSC_TYPE type) {
	if(!stmts[type]) {
		sqlite3_prepare_v2(adb, std_sql_stmts[type], -1, &stmts[type], 0);
	}
	return stmts[type];
}

int dbpscache::ExecStmt(sqlite3 *adb, DBPSC_TYPE type) {
	sqlite3_stmt *stmt = GetStmt(adb, type);
	sqlite3_step(stmt);
	return sqlite3_reset(stmt);
}


void dbpscache::DeAllocAll() {
	for(unsigned int i = DBPSC_START; i < DBPSC_NUM_STATEMENTS; i++) {
		if(stmts[i]) {
			sqlite3_finalize(stmts[i]);
			stmts[i] = 0;
		}
	}
}

void dbpscache::BeginTransaction(sqlite3 *adb) {
	if(transaction_refcount == 0) ExecStmt(adb, DBPSC_BEGIN);
	transaction_refcount++;
}

void dbpscache::EndTransaction(sqlite3 *adb) {
	transaction_refcount--;
	if(transaction_refcount == 0) ExecStmt(adb, DBPSC_COMMIT);
	else if(transaction_refcount < 0) transaction_refcount_went_negative = true;
}

void dbpscache::CheckTransactionRefcountState() {
	if(transaction_refcount_went_negative) {
		LogMsgFormat(LOGT::DBERR, "dbpscache::CheckTransactionRefcountState transaction_refcount went negative");
	}
	if(transaction_refcount != 0) {
		LogMsgFormat(LOGT::DBERR, "dbpscache::CheckTransactionRefcountState transaction_refcount is %d", transaction_refcount);
	}
}

static bool TagToDict(unsigned char tag, const unsigned char *&dict, size_t &dict_size) {
	switch(tag) {
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
		default: return false;
	}
}

#define HEADERSIZE 5

db_bind_buffer<dbb_compressed> DoCompress(const void *in, size_t insize, unsigned char tag, bool *iscompressed) {
	db_bind_buffer<dbb_compressed> out;

	if(insize) {
		const unsigned char *dict = nullptr;
		size_t dict_size = 0;
		bool compress = TagToDict(tag, dict, dict_size);
		if(compress && insize >= 100) {
			z_stream strm;
			strm.zalloc = Z_NULL;
			strm.zfree = Z_NULL;
			strm.opaque = Z_NULL;
			deflateInit(&strm, 5);

			if(dict)
				deflateSetDictionary(&strm, dict, dict_size);
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
				DBLogMsgFormat(LOGT::ZLIBTRACE, "deflate: %d, %d, %d", res, strm.avail_in, strm.avail_out);
			#endif
			if(res != Z_STREAM_END) {
				DBLogMsgFormat(LOGT::ZLIBERR, "DoCompress: deflate: error: res: %d (%s)", res, cstr(strm.msg));
			}
			out.data_size = HEADERSIZE + maxsize - strm.avail_out;
			deflateEnd(&strm);
			if(iscompressed) *iscompressed = true;
		}
		else {
			out.allocate(insize + 1);
			unsigned char *data = reinterpret_cast<unsigned char *>(out.mutable_data());
			data[0] = 'T';
			memcpy(data + 1, in, insize);
			if(iscompressed) *iscompressed = false;
		}
	}

	#if DB_COPIOUS_LOGGING
		static size_t cumin = 0;
		static size_t cumout = 0;
		cumin += insize;
		cumout += out.data_size;
		DBLogMsgFormat(LOGT::ZLIBTRACE, "compress: %d -> %d, cum: %f", insize, out.data_size, (double) cumout / (double) cumin);
	#endif

	return std::move(out);
}

db_bind_buffer<dbb_uncompressed> DoDecompress(db_bind_buffer<dbb_compressed> &&in) {
	if(!in.data_size) {
		return { };
	}

	const unsigned char *input = reinterpret_cast<const unsigned char *>(in.data);
	if(in.data_size == 2) {
		for(unsigned int i = 0; i < sizeof(allesctables) / sizeof(esctable); i++) {
			if(input[0] == allesctables[i].tag) {
				for(unsigned int j = 0; j < allesctables[i].count; j++) {
					if(input[1] == allesctables[i].start[j].id) {
						db_bind_buffer<dbb_uncompressed> out;
						out.data = allesctables[i].start[j].text;
						out.data_size = strlen(allesctables[i].start[j].text);
						return std::move(out);
					}
				}
				DBLogMsg(LOGT::ZLIBERR, "DoDecompress: Bad escape table identifier");
				return { };
			}
		}
	}
	const unsigned char *dict;
	size_t dict_size;
	switch(input[0]) {
		case 'T': {
			db_bind_buffer<dbb_uncompressed> out;
			std::swap(out.membuffer, in.membuffer);
			out.data = in.data + 1;
			out.data_size = in.data_size - 1;
			return std::move(out);
		}
		default: {
			bool compress = TagToDict(input[0], dict, dict_size);
			if(compress) break;
			else {
				DBLogMsgFormat(LOGT::ZLIBERR, "DoDecompress: Bad tag: 0x%X", input[0]);
				return { };
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
	for(unsigned int i = 1; i < 5; i++) {
		out.data_size <<= 8;
		out.data_size += input[i];
	}
	#if DB_COPIOUS_LOGGING
		DBLogMsgFormat(LOGT::ZLIBTRACE, "DoDecompress: insize %d, outsize %d", insize, outsize);
	#endif

	out.allocate_nt(out.data_size);
	unsigned char *data = reinterpret_cast<unsigned char *>(out.mutable_data());

	strm.next_out = data;
	strm.avail_out = out.data_size;
	while(true) {
		int res = inflate(&strm, Z_FINISH);
		#if DB_COPIOUS_LOGGING
			DBLogMsgFormat(LOGT::ZLIBTRACE, "inflate: %d, %d, %d", res, strm.avail_in, strm.avail_out);
		#endif
		if(res == Z_NEED_DICT) {
			if(dict) inflateSetDictionary(&strm, dict, dict_size);
			else {
				inflateEnd(&strm);
				DBLogMsgFormat(LOGT::ZLIBERR, "DoDecompress: Wants dictionary: %ux", strm.adler);
				return { };
			}
		}
		else if(res == Z_OK) continue;
		else if(res == Z_STREAM_END) break;
		else {
			DBLogMsgFormat(LOGT::ZLIBERR, "DoDecompress: inflate: error: res: %d (%s)", res, cstr(strm.msg));
			inflateEnd(&strm);
			return { };
		}
	}

	inflateEnd(&strm);

	#if DB_COPIOUS_LOGGING
		DBLogMsgFormat(LOGT::ZLIBTRACE, "decompress: %d -> %d", in.data_size, out.data_size);
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
	if(buffer.data_size) {
		buffer.make_persistent();
		if(dc.ParseInsitu<0>(const_cast<char *>(buffer.data)).HasParseError()) {
			DisplayParseErrorMsg(dc, "column_get_compressed_and_parse", buffer.data);
			dc.SetNull();
		}
	}
	else dc.SetNull();
	return std::move(buffer);
}

//! This calls itself for retweet sources, *unless* the retweet source ID is in idset
//! This expects to be called in *ascending* ID order
static void ProcessMessage_SelTweet(sqlite3 *db, sqlite3_stmt *stmt, dbseltweetmsg &m, std::deque<dbrettweetdata> &recv_data, uint64_t id,
		const container::set<uint64_t> &idset, dbconn *dbc, bool front_insert = false) {
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64) id);
	int res = sqlite3_step(stmt);
	uint64_t rtid = 0;
	if(res == SQLITE_ROW) {
		#if DB_COPIOUS_LOGGING
			DBLogMsgFormat(LOGT::DBTRACE, "DBSM::SELTWEET got id:%" llFmtSpec "d", (sqlite3_int64) id);
		#endif

		// emplacing at the *back* in the normal case is to ensure that the resulting deque is (mostly) in *ascending* order of ID
		// This ensures that tweets come before any retweets which use them as a source
		// *front* emplacing is used to ensure that any missing retweet sources come before any tweets which use them
		if(front_insert) recv_data.emplace_front();
		else recv_data.emplace_back();
		dbrettweetdata &rd = front_insert ? recv_data.front() : recv_data.back();

		rd.id = id;
		rd.statjson = column_get_compressed(stmt, 0);
		rd.dynjson = column_get_compressed(stmt, 1);
		rd.user1 = (uint64_t) sqlite3_column_int64(stmt, 2);
		rd.user2 = (uint64_t) sqlite3_column_int64(stmt, 3);
		rd.flags = (uint64_t) sqlite3_column_int64(stmt, 4);
		rd.timestamp = (uint64_t) sqlite3_column_int64(stmt, 5);
		rd.rtid = rtid = (uint64_t) sqlite3_column_int64(stmt, 6);

		if(rd.user1) dbc->AsyncReadInUser(db, rd.user1, m.user_data);
		if(rd.user2) dbc->AsyncReadInUser(db, rd.user2, m.user_data);
	}
	else {
		DBLogMsgFormat((m.flags & DBSTMF::NO_ERR) ? LOGT::DBTRACE : LOGT::DBERR,
				"DBSM::SELTWEET got error: %d (%s) for id: %" llFmtSpec "d",
				res, cstr(sqlite3_errmsg(db)), (sqlite3_int64) id);
	}
	sqlite3_reset(stmt);

	if(rtid && idset.find(rtid) == idset.end()) {
		// This is a retweet, if we're not already loading the retweet source, load it here
		// Note that this is front emplaced in *front* of the retweet which needs it
		ProcessMessage_SelTweet(db, stmt, m, recv_data, rtid, idset, dbc, true);
	}
}

//Note that the contents of themsg may be stolen if the lifetime of the message needs to be extended
//This is generally the case for messages which also act as replies
static void ProcessMessage(sqlite3 *db, std::unique_ptr<dbsendmsg> &themsg, bool &ok, dbpscache &cache, dbiothread *th, dbconn *dbc) {
	dbsendmsg *msg = themsg.get();
	switch(msg->type) {
		case DBSM::QUIT:
			ok = false;
			DBLogMsg(LOGT::DBTRACE, "DBSM::QUIT");
			break;
		case DBSM::INSERTTWEET: {
			if(gc.readonlymode) break;
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
			if(res != SQLITE_DONE) {
				DBLogMsgFormat(LOGT::DBERR, "DBSM::INSERTTWEET got error: %d (%s) for id: %" llFmtSpec "d",
					res, cstr(sqlite3_errmsg(db)), m->id);
			}
			else {
				DBLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTTWEET inserted row id: %" llFmtSpec "d", (sqlite3_int64) m->id);
				dbc->all_tweet_ids.insert(m->id);
			}
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::UPDATETWEET: {
			if(gc.readonlymode) break;
			dbupdatetweetmsg *m = static_cast<dbupdatetweetmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_UPDTWEET);
			bind_compressed(stmt, 1, m->dynjson, 'J');
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->flags);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) m->id);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, "DBSM::UPDATETWEET got error: %d (%s) for id: %" llFmtSpec "d",
					res, cstr(sqlite3_errmsg(db)), m->id); }
			else { DBLogMsgFormat(LOGT::DBTRACE, "DBSM::UPDATETWEET updated id: %" llFmtSpec "d", (sqlite3_int64) m->id); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::SELTWEET: {
			dbseltweetmsg *m = static_cast<dbseltweetmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_SELTWEET);
			std::deque<dbrettweetdata> recv_data;

			//This is *ascending* ID order
			for(auto it = m->id_set.cbegin(); it != m->id_set.cend(); ++it) {
				ProcessMessage_SelTweet(db, stmt, *m, recv_data, *it, m->id_set, dbc);
			}
			if(!recv_data.empty()) {
				m->data = std::move(recv_data);
				m->SendReply(std::move(themsg), th);
				return;
			}
			break;
		}
		case DBSM::INSERTUSER: {
			if(gc.readonlymode) break;
			dbinsertusermsg *m = static_cast<dbinsertusermsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSUSER);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->id);
			bind_compressed(stmt, 2, m->json, 'J');
			bind_compressed(stmt, 3, m->cached_profile_img_url, 'P');
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->createtime);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) m->lastupdate);
			if(m->cached_profile_img_hash) {
				sqlite3_bind_blob(stmt, 6, m->cached_profile_img_hash->hash_sha1, sizeof(m->cached_profile_img_hash->hash_sha1), SQLITE_TRANSIENT);
			}
			else {
				sqlite3_bind_null(stmt, 6);
			}
			bind_compressed(stmt, 7, std::move(m->mentionindex));
			sqlite3_bind_int64(stmt, 8, (sqlite3_int64) m->profile_img_last_used);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, "DBSM::INSERTUSER got error: %d (%s) for id: %" llFmtSpec "d",
					res, cstr(sqlite3_errmsg(db)), m->id); }
			else { DBLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTUSER inserted id: %" llFmtSpec "d", (sqlite3_int64) m->id); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::SELUSER: {
			dbselusermsg *m = static_cast<dbselusermsg*>(msg);
			for(uint64_t id : m->id_set) {
				DBLogMsgFormat(LOGT::DBTRACE, "DBSM::SELUSER got request for user: %" llFmtSpec "u", id);

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
			DBLogMsgFormat(LOGT::DBTRACE, "DBSM::NOTIFYUSERSPURGED inserted %d ids", m->ids.size());
			break;
		}
		case DBSM::INSERTACC: {
			if(gc.readonlymode) break;
			dbinsertaccmsg *m = static_cast<dbinsertaccmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSERTNEWACC);
			sqlite3_bind_text(stmt, 1, m->name.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 2, m->dispname.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) m->userid);
			int res = sqlite3_step(stmt);
			m->dbindex = (unsigned int) sqlite3_last_insert_rowid(db);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, "DBSM::INSERTACC got error: %d (%s) for account name: %s",
					res, cstr(sqlite3_errmsg(db)), cstr(m->dispname)); }
			else { DBLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTACC inserted account dbindex: %d, name: %s", m->dbindex, cstr(m->dispname)); }
			sqlite3_reset(stmt);
			m->SendReply(std::move(themsg), th);
			return;
		}
		case DBSM::DELACC: {
			if(gc.readonlymode) break;
			dbdelaccmsg *m = static_cast<dbdelaccmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_DELACC);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->dbindex);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, "DBSM::DELACC got error: %d (%s) for account dbindex: %d",
					res, cstr(sqlite3_errmsg(db)), m->dbindex); }
			else { DBLogMsgFormat(LOGT::DBTRACE, "DBSM::DELACC deleted account dbindex: %d", m->dbindex); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::INSERTMEDIA: {
			if(gc.readonlymode) break;
			dbinsertmediamsg *m = static_cast<dbinsertmediamsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSERTMEDIA);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->media_id.m_id);
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->media_id.t_id);
			bind_compressed(stmt, 3, m->url, 'P');
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->lastused);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, "DBSM::INSERTMEDIA got error: %d (%s) for id: %" llFmtSpec "d/%" llFmtSpec "d",
					res, cstr(sqlite3_errmsg(db)), (sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id); }
			else { DBLogMsgFormat(LOGT::DBTRACE, "DBSM::INSERTMEDIA inserted media id: %" llFmtSpec "d/%" llFmtSpec "d",
					(sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::UPDATEMEDIAMSG: {
			if(gc.readonlymode) break;
			dbupdatemediamsg *m = static_cast<dbupdatemediamsg*>(msg);
			DBPSC_TYPE stmt_id = static_cast<DBPSC_TYPE>(-1); //invalid value
			switch(m->update_type) {
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
			switch(m->update_type) {
				case DBUMMT::THUMBCHECKSUM:
				case DBUMMT::FULLCHECKSUM:
					if(m->chksm) sqlite3_bind_blob(stmt, 1, m->chksm->hash_sha1, sizeof(m->chksm->hash_sha1), SQLITE_TRANSIENT);
					else sqlite3_bind_null(stmt, 1);
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
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, "DBSM::UPDATEMEDIAMSG got error: %d (%s) for id: %" llFmtSpec "d/%" llFmtSpec "d (%d)",
					res, cstr(sqlite3_errmsg(db)), (sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id, m->update_type); }
			else { DBLogMsgFormat(LOGT::DBTRACE, "DBSM::UPDATEMEDIAMSG updated media id: %" llFmtSpec "d/%" llFmtSpec "d (%d)",
					(sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id, m->update_type); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::UPDATETWEETSETFLAGS: {
			if(gc.readonlymode) break;
			dbupdatetweetsetflagsmsg *m = static_cast<dbupdatetweetsetflagsmsg *>(msg);
			cache.BeginTransaction(db);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_UPDATETWEETFLAGSMASKED);
			for(auto it = m->ids.begin(); it != m->ids.end(); ++it) {
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->setmask);
				sqlite3_bind_int64(stmt, 2, (sqlite3_int64) (~m->unsetmask));
				sqlite3_bind_int64(stmt, 3, (sqlite3_int64) *it);
				int res = sqlite3_step(stmt);
				if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, "DBSM::UPDATETWEETSETFLAGS got error: %d (%s) for id: %" llFmtSpec "d",
						res, cstr(sqlite3_errmsg(db)), *it); }
				sqlite3_reset(stmt);
			}
			cache.EndTransaction(db);
			break;
		}
		case DBSM::MSGLIST: {
			cache.BeginTransaction(db);
			dbsendmsg_list *m = static_cast<dbsendmsg_list *>(msg);
			DBLogMsgFormat(LOGT::DBTRACE, "DBSM::MSGLIST: queue size: %d", m->msglist.size());
			for(auto &onemsg : m->msglist) {
				ProcessMessage(db, onemsg, ok, cache, th, dbc);
			}
			cache.EndTransaction(db);
			break;
		}
		case DBSM::FUNCTION: {
			cache.BeginTransaction(db);
			dbfunctionmsg *m = static_cast<dbfunctionmsg *>(msg);
			DBLogMsgFormat(LOGT::DBTRACE, "DBSM::FUNCTION: queue size: %d", m->funclist.size());
			for(auto &onemsg : m->funclist) {
				onemsg(db, ok, cache);
			}
			cache.EndTransaction(db);
			break;
		}
		default: break;
	}
}

wxThread::ExitCode dbiothread::Entry() {
	MsgLoop();
	return 0;
}

void dbiothread::MsgLoop() {
	bool ok = true;
	while(ok) {
		dbsendmsg *msg;
		#ifdef __WINDOWS__
		DWORD num;
		OVERLAPPED *ovlp;
		bool res = GetQueuedCompletionStatus(iocp, &num, (PULONG_PTR) &msg, &ovlp, INFINITE);
		if(!res) {
			return;
		}
		#else
		size_t bytes_to_read = sizeof(msg);
		size_t bytes_read = 0;
		while(bytes_to_read) {
			ssize_t l_bytes_read = read(pipefd, ((char *) &msg) + bytes_read, bytes_to_read);
			if(l_bytes_read >= 0) {
				bytes_read += l_bytes_read;
				bytes_to_read -= l_bytes_read;
			}
			else {
				if(l_bytes_read == EINTR) continue;
				else {
					close(pipefd);
					return;
				}
			}
		}
		#endif
		std::unique_ptr<dbsendmsg> msgcont(msg);
		ProcessMessage(db, msgcont, ok, cache, this, dbc);
		if(!reply_list.empty()) {
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
EVT_TIMER(DBCONNTIMER_ID_ASYNCSTATEWRITE, dbconn::OnAsyncStateWriteTimer)
END_EVENT_TABLE()

void dbconn::OnStdTweetLoadFromDB(wxCommandEvent &event) {
	std::unique_ptr<dbseltweetmsg> msg(static_cast<dbseltweetmsg *>(event.GetClientData()));
	event.SetClientData(0);
	HandleDBSelTweetMsg(*msg, 0);
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
	if(it != generic_sel_funcs.end()) {
		it->second(*msg, this);
		generic_sel_funcs.erase(it);
	}
	else {
		DBLogMsgFormat(LOGT::DBERR, "dbconn::GenericDBSelTweetMsgHandler could not find handler for %p.", msg.get());
	}

	dbc_flags |= DBCF::REPLY_CLEARNOUPDF;
}

void dbconn::SetDBSelTweetMsgHandler(dbseltweetmsg &msg, std::function<void(dbseltweetmsg &, dbconn *)> f) {
	msg.targ = this;
	msg.cmdevtype = wxextDBCONN_NOTIFY;
	msg.winid = wxDBCONNEVT_ID_GENERICSELTWEET;
	generic_sel_funcs[reinterpret_cast<intptr_t>(&msg)] = std::move(f);
}

void dbconn::HandleDBSelTweetMsg(dbseltweetmsg &msg, flagwrapper<HDBSF> flags) {
	LogMsgFormat(LOGT::DBTRACE, "dbconn::HandleDBSelTweetMsg start");

	if(msg.flags & DBSTMF::CLEARNOUPDF) dbc_flags |= DBCF::REPLY_CLEARNOUPDF;

	DBSelUserReturnDataHandler(std::move(msg.user_data), 0);

	for(dbrettweetdata &dt : msg.data) {
		#if DB_COPIOUS_LOGGING
			DBLogMsgFormat(LOGT::DBTRACE, "dbconn::HandleDBSelTweetMsg got tweet: id:%" llFmtSpec "d, statjson: %s, dynjson: %s", dt.id, cstr(dt.statjson), cstr(dt.dynjson));
		#endif
		ad.unloaded_db_tweet_ids.erase(dt.id);
		tweet_ptr t = ad.GetTweetById(dt.id);
		t->lflags |= TLF::SAVED_IN_DB;
		t->lflags |= TLF::LOADED_FROM_DB;

		rapidjson::Document dc;
		if(dt.statjson.data_size && !dc.ParseInsitu<0>(dt.statjson.mutable_data()).HasParseError() && dc.IsObject()) {
			genjsonparser::ParseTweetStatics(dc, t, 0);
		}
		else {
			DBLogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, "dbconn::HandleDBSelTweetMsg static JSON parse error: malformed or missing, tweet id: %" llFmtSpec "d", dt.id);
		}

		if(dt.dynjson.data_size && !dc.ParseInsitu<0>(dt.dynjson.mutable_data()).HasParseError() && dc.IsObject()) {
			genjsonparser::ParseTweetDyn(dc, t);
		}
		else {
			DBLogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, "dbconn::HandleDBSelTweetMsg dyn JSON parse error: malformed or missing, tweet id: %" llFmtSpec "d", dt.id);
		}

		t->user = ad.GetUserContainerById(dt.user1);
		if(dt.user2) t->user_recipient = ad.GetUserContainerById(dt.user2);
		t->createtime = (time_t) dt.timestamp;

		new (&t->flags) tweet_flags(dt.flags);

		//This sets flags_at_prev_update to the new value of flags
		//This prevents subsequent flag changes being missed without needing to do an update
		t->IgnoreChangeToFlagsByMask(~static_cast<unsigned long long>(0));

		if(dt.rtid) {
			t->rtsrc = ad.GetTweetById(dt.rtid);
		}


		if(!(flags & HDBSF::NOPENDINGS)) {
			flagwrapper<PENDING_BITS> res = TryUnmarkPendingTweet(t, UMPTF::TPDB_NOUPDF);
			if(res) {
				GenericMarkPending(t, res, "dbconn::HandleDBSelTweetMsg");
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
	DBSelUserReturnDataHandler(std::move(msg->data), 0);
}

void dbconn::GenericDBSelUserMsgHandler(wxCommandEvent &event) {
	std::unique_ptr<dbselusermsg> msg(static_cast<dbselusermsg *>(event.GetClientData()));
	event.SetClientData(0);

	const auto &it = generic_sel_user_funcs.find(reinterpret_cast<intptr_t>(msg.get()));
	if(it != generic_sel_user_funcs.end()) {
		it->second(*msg, this);
		generic_sel_user_funcs.erase(it);
	}
	else {
		DBLogMsgFormat(LOGT::DBERR, "dbconn::GenericDBSelUserMsgHandler could not find handler for %p.", msg.get());
	}

	dbc_flags |= DBCF::REPLY_CLEARNOUPDF;
}

void dbconn::SetDBSelUserMsgHandler(dbselusermsg &msg, std::function<void(dbselusermsg &, dbconn *)> f) {
	msg.targ = this;
	msg.cmdevtype = wxextDBCONN_NOTIFY;
	msg.winid = wxDBCONNEVT_ID_GENERICSELUSER;
	generic_sel_user_funcs[reinterpret_cast<intptr_t>(&msg)] = std::move(f);
}

void dbconn::DBSelUserReturnDataHandler(std::deque<dbretuserdata> data, flagwrapper<HDBSF> flags) {
	for(dbretuserdata &du : data) {
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

		// Incoming mention_index likely to be larger, old one likely to be empty or nearly empty
		std::deque<uint64_t> old_mention_index = std::move(u->mention_index);
		u->mention_index = std::move(du.mention_index);
		u->mention_index.insert(u->mention_index.end(), old_mention_index.begin(), old_mention_index.end());

		if(!(flags & HDBSF::NOPENDINGS)) {
			u->CheckPendingTweets();
			dbc.dbc_flags |= DBCF::REPLY_CHECKPENDINGS;
		}
	}
}

void dbconn::OnDBNewAccountInsert(wxCommandEvent &event) {
	std::unique_ptr<dbinsertaccmsg> msg(static_cast<dbinsertaccmsg *>(event.GetClientData()));
	event.SetClientData(0);
	wxString accname = wxstrstd(msg->name);
	for(auto &it : alist) {
		if(it->name == accname) {
			it->dbindex = msg->dbindex;
			it->beinginsertedintodb = false;
			it->CalcEnabled();
			it->Exec();
		}
	}
}

void dbconn::SendMessageBatched(std::unique_ptr<dbsendmsg> msg) {
	GetMessageBatchQueue()->msglist.emplace_back(std::move(msg));
}

observer_ptr<dbsendmsg_list> dbconn::GetMessageBatchQueue() {
	if(!batchqueue)
		batchqueue.reset(new dbsendmsg_list);

	if(!(dbc_flags & DBCF::BATCHEVTPENDING)) {
		dbc_flags |= DBCF::BATCHEVTPENDING;
		wxCommandEvent evt(wxextDBCONN_NOTIFY, wxDBCONNEVT_ID_SENDBATCH);
		AddPendingEvent(evt);
	}
	return make_observer(batchqueue);
}

void dbconn::OnSendBatchEvt(wxCommandEvent &event) {
	if(!(dbc_flags & DBCF::INITED)) return;

	dbc_flags &= ~DBCF::BATCHEVTPENDING;
	if(batchqueue) {
		SendMessage(std::move(batchqueue));
	}
}

void dbconn::OnDBReplyEvt(wxCommandEvent &event) {
	dbreplyevtstruct *msg = static_cast<dbreplyevtstruct *>(event.GetClientData());
	std::unique_ptr<dbreplyevtstruct> msgcont(msg);
	event.SetClientData(0);

	if(dbc_flags & DBCF::INITED) {
		for(auto &it : msg->reply_list) {
			it.first->ProcessEvent(*it.second);
		}
	}

	if(dbc_flags & DBCF::REPLY_CLEARNOUPDF) {
		dbc_flags &= ~DBCF::REPLY_CLEARNOUPDF;
		CheckClearNoUpdateFlag_All();
	}

	if(dbc_flags & DBCF::REPLY_CHECKPENDINGS) {
		dbc_flags &= ~DBCF::REPLY_CHECKPENDINGS;
		for(auto &it : alist) {
			if(it->enabled) it->StartRestQueryPendings();
		}
	}
}

void dbconn::SendMessageOrAddToList(std::unique_ptr<dbsendmsg> msg, optional_observer_ptr<dbsendmsg_list> msglist) {
	if(msglist) msglist->msglist.emplace_back(std::move(msg));
	else SendMessage(std::move(msg));
}

void dbconn::SendMessage(std::unique_ptr<dbsendmsg> msgp) {
	dbsendmsg *msg = msgp.release();
	#ifdef __WINDOWS__
	bool result = PostQueuedCompletionStatus(iocp, 0, (ULONG_PTR) msg, 0);
	if(!result) {
		LogMsgFormat(LOGT::DBERR, "dbconn::SendMessage(): Could not communicate with DB thread");
		CloseHandle(iocp);
		iocp = INVALID_HANDLE_VALUE;
	}
	#else
	size_t offset = 0;
	while(offset < sizeof(msg)) {
		ssize_t result = write(pipefd, ((const char *) &msg) + offset, sizeof(msg) - offset);
		if(result < 0) {
			int err = errno;
			if(err == EINTR) continue;
			else {
				LogMsgFormat(LOGT::DBERR, "dbconn::SendMessage(): Could not communicate with DB thread: %d, %s", err, cstr(strerror(err)));
				close(pipefd);
				pipefd = -1;
			}
		}
		else offset += result;
	}
	#endif
}

void dbconn::SendAccDBUpdate(std::unique_ptr<dbinsertaccmsg> insmsg) {
	insmsg->targ = this;
	insmsg->cmdevtype = wxextDBCONN_NOTIFY;
	insmsg->winid = wxDBCONNEVT_ID_INSERTNEWACC;
	dbc.SendMessage(std::move(insmsg));
}

bool dbconn::Init(const std::string &filename /*UTF-8*/) {
	if(dbc_flags & DBCF::INITED) return true;

	LogMsgFormat(LOGT::DBTRACE, "dbconn::Init(): About to initialise database connection");

	sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);		//only use sqlite from one thread at any given time
	sqlite3_initialize();

	int res = sqlite3_open_v2(filename.c_str(), &syncdb, gc.readonlymode ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
	if(res != SQLITE_OK) {
		wxMessageDialog(0, wxString::Format(wxT("Database could not be opened/created, got error: %d (%s)\nDatabase filename: %s\nCheck that the database is not locked by another process, and that the directory is read/writable."),
			res, wxstrstd(sqlite3_errmsg(syncdb)).c_str(), wxstrstd(filename).c_str()),
			wxT("Fatal Startup Error"), wxOK | wxICON_ERROR ).ShowModal();
		return false;
	}

	sqlite3_busy_handler(syncdb, &busy_handler_callback, 0);

	if(!gc.readonlymode) {
		int table_count = -1;
		DBRowExec(syncdb, "SELECT COUNT(*) FROM sqlite_master WHERE type == \"table\" AND name NOT LIKE \"sqlite%\";", [&](sqlite3_stmt *getstmt) {
			table_count = sqlite3_column_int(getstmt, 0);
		}, "dbconn::Init (table count)");

		LogMsgFormat(LOGT::DBTRACE, "dbconn::Init(): table_count: %d", table_count);

		res = sqlite3_exec(syncdb, startup_sql, 0, 0, 0);
		if(res != SQLITE_OK) {
			wxMessageDialog(0, wxString::Format(wxT("Startup SQL failed, got error: %d (%s)\nDatabase filename: %s\nCheck that the database is not locked by another process, and that the directory is read/writable."),
				res, wxstrstd(sqlite3_errmsg(syncdb)).c_str(), wxstrstd(filename).c_str()),
				wxT("Fatal Startup Error"), wxOK | wxICON_ERROR ).ShowModal();
			sqlite3_close(syncdb);
			syncdb = 0;
			return false;
		}

		if(table_count <= 0) {
			// This is a new DB, no need to do update check, just write version
			SyncWriteDBVersion(syncdb);
		}
		else {
			// Check whether DB is old and needs to be updated
			if(!SyncDoUpdates(syncdb)) {
				// All bets are off, give up now
				sqlite3_close(syncdb);
				syncdb = 0;
				return false;
			}
		}
	}

	LogMsgFormat(LOGT::DBTRACE, "dbconn::Init(): About to read in state from database");

	SyncReadInAllUserIDs(syncdb);
	AccountSync(syncdb);
	ReadAllCFGIn(syncdb, gc, alist);
	SyncReadInRBFSs(syncdb);
	SyncReadInAllMediaEntities(syncdb);
	SyncReadInCIDSLists(syncdb);
	SyncReadInTpanels(syncdb);
	SyncReadInWindowLayout(syncdb);
	SyncReadInAllTweetIDs(syncdb);
	SyncReadInUserRelationships(syncdb);
	SyncReadInUserDMIndexes(syncdb);
	SyncPostUserLoadCompletion();

	LogMsgFormat(LOGT::DBTRACE, "dbconn::Init(): State read in from database complete, about to create database thread");

	th = new dbiothread();
	th->filename = filename;
	th->db = syncdb;
	th->dbc = this;
	syncdb = 0;

#ifdef __WINDOWS__
	iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 1);
	th->iocp = iocp;
	if(!iocp) {
		wxMessageDialog(0, wxT("DB IOCP creation failed."));
		sqlite3_close(syncdb);
		syncdb = 0;
		return false;
	}
#else
	int pipepair[2];
	int result = pipe(pipepair);
	if(result < 0) {
		wxMessageDialog(0, wxString::Format(wxT("DB pipe creation failed: %d, %s"), errno, wxstrstd(strerror(errno)).c_str()));
		sqlite3_close(syncdb);
		syncdb = 0;
		return false;
	}
	th->pipefd = pipepair[0];
	this->pipefd = pipepair[1];
#endif
	th->Create();
#if defined(_GNU_SOURCE)
#if __GLIBC_PREREQ(2, 12)
	pthread_setname_np(th->GetId(), "retcon-sqlite3");
#endif
#endif
	th->Run();
	LogMsgFormat(LOGT::DBTRACE | LOGT::THREADTRACE, "dbconn::Init(): Created database thread: %d", th->GetId());

	asyncstateflush_timer.reset(new wxTimer(this, DBCONNTIMER_ID_ASYNCSTATEWRITE));
	ResetAsyncStateWriteTimer();

	dbc_flags |= DBCF::INITED;
	return true;
}

void dbconn::DeInit() {
	if(!(dbc_flags & DBCF::INITED)) return;

	asyncstateflush_timer.reset();

	if(batchqueue) {
		SendMessage(std::move(batchqueue));
	}

	dbc_flags &= ~DBCF::INITED;

	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, "dbconn::DeInit: About to terminate database thread and write back state");

	SendMessage(std::unique_ptr<dbsendmsg>(new dbsendmsg(DBSM::QUIT)));

	#ifdef __WINDOWS__
	CloseHandle(iocp);
	#else
	close(pipefd);
	#endif
	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, "dbconn::DeInit(): Waiting for database thread to terminate");
	th->Wait();
	syncdb = th->db;
	delete th;

	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, "dbconn::DeInit(): Database thread terminated");

	if(!gc.readonlymode) {
		cache.BeginTransaction(syncdb);
		WriteAllCFGOut(syncdb, gc, alist);
		SyncWriteBackAllUsers(syncdb);
		SyncWriteBackAccountIdLists(syncdb);
		SyncWriteOutRBFSs(syncdb);
		SyncWriteBackCIDSLists(syncdb);
		SyncWriteBackWindowLayout(syncdb);
		SyncWriteBackTpanels(syncdb);
		SyncWriteBackUserRelationships(syncdb);
		SyncWriteBackUserDMIndexes(syncdb);
		SyncWriteBackTweetIDIndexCache(syncdb);
	}
	SyncPurgeMediaEntities(syncdb); //this does a dry-run in read-only mode
	SyncPurgeProfileImages(syncdb); //this does a dry-run in read-only mode
	if(!gc.readonlymode) {
		cache.EndTransaction(syncdb);
	}

	sqlite3_close(syncdb);

	cache.CheckTransactionRefcountState();

	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, "dbconn::DeInit(): State write back to database complete, database connection closed.");
}

void dbconn::CheckPurgeTweets() {
	unsigned int purge_count = 0;
	unsigned int refone_count = 0;

	auto it = ad.tweetobjs.begin();
	while(it != ad.tweetobjs.end()) {
		tweet_ptr &t = it->second;
		uint64_t id = it->first;

		if(t->lflags & TLF::REFCOUNT_WENT_GT1 || t->HasPendingOps()) {
			// Keep it

			// Reset the flag, if no one creates a second pointer to it before the next call to CheckPurgeTweets, it will then be purged
			if(t->GetRefcount() == 1) {
				t->lflags &= ~TLF::REFCOUNT_WENT_GT1;
				refone_count++;
			}
			++it;
		}
		else {
			// Bin it
			if(t->lflags & TLF::SAVED_IN_DB) ad.unloaded_db_tweet_ids.insert(id);
			it = ad.tweetobjs.erase(it);
			purge_count++;
		}
	}
	LogMsgFormat(LOGT::DBTRACE, "dbconn::CheckPurgeTweets purged %u tweets from memory, %zu remaining, %u might be purged next time",
			purge_count, ad.tweetobjs.size(), refone_count);
}

// This should be called immediately after AsyncWriteBackAllUsers
void dbconn::CheckPurgeUsers() {
	unsigned int refone_count = 0;
	unsigned int purge_count = 0;
	useridset db_purged_ids;

	auto it = ad.userconts.begin();
	while(it != ad.userconts.end()) {
		udc_ptr &u = it->second;
		uint64_t id = it->first;

		if(u->udc_flags & UDC::NON_PURGABLE) {
			// do nothing, user not purgable
			++it;
		}
		else if(u->udc_flags & UDC::REFCOUNT_WENT_GT1 || !u->pendingtweets.empty()) {
			// Keep it

			// Reset the flag, if no one creates a second pointer to it before the next call to CheckPurgeUsers, it will then be purged
			if(u->GetRefcount() == 1) {
				u->udc_flags &= ~UDC::REFCOUNT_WENT_GT1;
				refone_count++;
			}
			++it;
		}
		else {
			// Bin it
			if(u->udc_flags & UDC::SAVED_IN_DB) {
				db_purged_ids.insert(id);
				ad.unloaded_db_user_ids.insert(id);
			}
			it = ad.userconts.erase(it);
			purge_count++;
		}
	}
	LogMsgFormat(LOGT::DBTRACE, "dbconn::CheckPurgeUsers purged %u users from memory (%u in DB), %zu remaining, %u might be purged next time",
			purge_count, db_purged_ids.size(), ad.userconts.size(), refone_count);

	if(!db_purged_ids.empty()) {
		SendMessage(std::unique_ptr<dbnotifyuserspurgedmsg>(new dbnotifyuserspurgedmsg(std::move(db_purged_ids))));
	}
}

void dbconn::AsyncWriteBackState() {
	if(!gc.readonlymode) {
		LogMsg(LOGT::DBTRACE, "dbconn::AsyncWriteBackState start");

		if(batchqueue) {
			SendMessage(std::move(batchqueue));
		}

		std::unique_ptr<dbfunctionmsg> msg(new dbfunctionmsg);
		auto cfg_closure = WriteAllCFGOutClosure(gc, alist, true);
		msg->funclist.emplace_back([cfg_closure](sqlite3 *db, bool &ok, dbpscache &cache) {
			DBLogMsg(LOGT::DBTRACE, "dbconn::AsyncWriteBackState: CFG write start");
			DBWriteConfig twfc(db);
			cfg_closure(twfc);
			DBLogMsg(LOGT::DBTRACE, "dbconn::AsyncWriteBackState: CFG write end");
		});
		AsyncWriteBackAllUsers(*msg);
		AsyncWriteBackAccountIdLists(*msg);
		AsyncWriteOutRBFSs(*msg);
		AsyncWriteBackCIDSLists(*msg);
		AsyncWriteBackTpanels(*msg);
		AsyncWriteBackUserDMIndexes(*msg);

		SendMessage(std::move(msg));

		LogMsg(LOGT::DBTRACE, "dbconn::AsyncWriteBackState: message sent to DB thread");
	}

	CheckPurgeTweets();
	CheckPurgeUsers();
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
	msg->flags = tobj->flags.Save();
	if(tobj->rtsrc) msg->rtid = tobj->rtsrc->id;
	else msg->rtid = 0;

	SendMessageOrAddToList(std::move(msg), msglist);
}

void dbconn::UpdateTweetDyn(tweet_ptr_p tobj, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbupdatetweetmsg> msg(new dbupdatetweetmsg());
	msg->dynjson = tobj->mkdynjson();
	msg->id = tobj->id;
	msg->flags = tobj->flags.Save();
	SendMessageOrAddToList(std::move(msg), msglist);
}

void dbconn::InsertUser(udc_ptr_p u, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbinsertusermsg> msg(new dbinsertusermsg());
	msg->id = u->id;
	msg->json = u->mkjson();
	msg->cached_profile_img_url = std::string(u->cached_profile_img_url.begin(), u->cached_profile_img_url.end());	//prevent any COW semantics
	msg->createtime = u->user.createtime;
	msg->lastupdate = u->lastupdate;
	msg->cached_profile_img_hash = u->cached_profile_img_sha1;
	msg->mentionindex = settocompressedblob_desc(u->mention_index);
	u->lastupdate_wrotetodb = u->lastupdate;
	msg->profile_img_last_used = u->profile_img_last_used;
	u->profile_img_last_used_db = u->profile_img_last_used;
	u->udc_flags |= UDC::SAVED_IN_DB;
	SendMessageOrAddToList(std::move(msg), msglist);
}

void dbconn::InsertMedia(media_entity &me, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbinsertmediamsg> msg(new dbinsertmediamsg());
	msg->media_id = me.media_id;
	msg->url = std::string(me.media_url.begin(), me.media_url.end());
	msg->lastused = me.lastused;
	SendMessageOrAddToList(std::move(msg), msglist);
	me.flags |= MEF::IN_DB;
}

void dbconn::UpdateMedia(media_entity &me, DBUMMT update_type, optional_observer_ptr<dbsendmsg_list> msglist) {
	std::unique_ptr<dbupdatemediamsg> msg(new dbupdatemediamsg(update_type));
	msg->media_id = me.media_id;
	switch(update_type) {
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
	SendMessageOrAddToList(std::move(msg), msglist);
}

//tweetids, dmids are big endian in database
void dbconn::AccountSync(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::AccountSync start");

	unsigned int total = 0;
	DBRowExecNoError(adb, "SELECT id, name, tweetids, dmids, userid, dispname FROM acc;", [&](sqlite3_stmt *getstmt) {
		unsigned int id = (unsigned int) sqlite3_column_int(getstmt, 0);
		wxString name = wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 1));

		std::shared_ptr<taccount> ta(new(taccount));
		ta->name = name;
		ta->dbindex = id;
		alist.push_back(ta);

		setfromcompressedblob(ta->tweet_ids, getstmt, 2);
		setfromcompressedblob(ta->dm_ids, getstmt, 3);
		total += ta->tweet_ids.size();
		total += ta->dm_ids.size();

		uint64_t userid = (uint64_t) sqlite3_column_int64(getstmt, 4);
		ta->usercont = SyncReadInUser(adb, userid);
		ta->dispname = wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 5));

		LogMsgFormat(LOGT::DBTRACE, "dbconn::AccountSync: Found account: dbindex: %d, name: %s, tweet IDs: %u, DM IDs: %u",
				id, cstr(name), ta->tweet_ids.size(), ta->dm_ids.size());
	});
	LogMsgFormat(LOGT::DBTRACE, "dbconn::AccountSync end, total: %u IDs", total);
}

void dbconn::SyncReadInAllTweetIDs(sqlite3 *syncdb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInAllTweetIDs start");

	DBBindRowExec(syncdb, cache.GetStmt(syncdb, DBPSC_SELSTATICSETTING),
		[&](sqlite3_stmt *getstmt) {
			sqlite3_bind_text(getstmt, 1, "tweetidsetcache", -1, SQLITE_STATIC);
		},
		[&](sqlite3_stmt *getstmt) {
			setfromcompressedblob(all_tweet_ids, getstmt, 0);
		},
		"dbconn::SyncReadInAllTweetIDs (cache load)"
	);

	if(all_tweet_ids.empty()) {
		// Didn't find any cache
		LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInAllTweetIDs table scan");

		DBRowExec(syncdb, "SELECT id FROM tweets ORDER BY id DESC;", [&](sqlite3_stmt *getstmt) {
			uint64_t id = (uint64_t) sqlite3_column_int64(getstmt, 0);
			all_tweet_ids.insert(all_tweet_ids.end(), id);
		}, "dbconn::SyncReadInAllTweetIDs");
	}

	if(!gc.readonlymode) {
		DBBindExec(syncdb, cache.GetStmt(syncdb, DBPSC_DELSTATICSETTING),
			[&](sqlite3_stmt *stmt) {
				sqlite3_bind_text(stmt, 1, "tweetidsetcache", -1, SQLITE_STATIC);
			},
			"dbconn::SyncReadInAllTweetIDs (delete cache)"
		);
	}

	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInAllTweetIDs set copy");
	ad.unloaded_db_tweet_ids = all_tweet_ids;
	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInAllTweetIDs end, read %u", all_tweet_ids.size());
}

void dbconn::SyncWriteBackTweetIDIndexCache(sqlite3 *syncdb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncWriteBackTweetIDIndexCache start");

	DBBindExec(syncdb, cache.GetStmt(syncdb, DBPSC_INSSTATICSETTING),
		[&](sqlite3_stmt *setstmt) {
			sqlite3_bind_text(setstmt, 1, "tweetidsetcache", -1, SQLITE_STATIC);
			bind_compressed(setstmt, 2, settocompressedblob_desc(all_tweet_ids));
		},
		"dbconn::SyncWriteBackTweetIDIndexCache"
	);

	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncWriteBackTweetIDIndexCache end, wrote %u", all_tweet_ids.size());
}

void dbconn::SyncReadInCIDSLists(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInCIDSLists start");
	const char getcidslist[] = "SELECT value FROM settings WHERE name == ?;";
	sqlite3_stmt *getstmt = nullptr;
	sqlite3_prepare_v2(adb, getcidslist, sizeof(getcidslist), &getstmt, 0);

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

	sqlite3_finalize(getstmt);
	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInCIDSLists end, total: %u IDs", total);
}

namespace {
	template <typename T> struct WriteBackOutputter {
		std::shared_ptr<T> data;

		WriteBackOutputter(std::shared_ptr<T> d) : data(d) { }
		template <typename F> void operator()(F func) {
			for(auto&& item : *data) {
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
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s start", cstr(funcname));
			cache.BeginTransaction(adb);
			sqlite3_stmt *setstmt = cache.GetStmt(adb, DBPSC_INSSETTING);

			unsigned int total = 0;
			getfunc([&](itemdata &&data) {
				sqlite3_bind_text(setstmt, 1, globstr.c_str(), globstr.size(), SQLITE_STATIC);
				sqlite3_bind_text(setstmt, 2, data.name, -1, SQLITE_STATIC);
				bind_compressed(setstmt, 3, std::move(data.index));
				int res = sqlite3_step(setstmt);
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s), for set: %s",
						cstr(funcname), res, cstr(sqlite3_errmsg(adb)), cstr(data.name));
				}
				sqlite3_reset(setstmt);
				total += data.list_size;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s end, total: %u IDs", cstr(funcname), total);
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
		};

		//Where F is a functor of the form void(itemdata &&)
		//F must free() itemdata::index
		template <typename F> void operator()(F func) const {
			for(auto &it : alist) {
				itemdata data;

				data.tweet_count = it->tweet_ids.size();
				data.tweet_blob = settocompressedblob_desc(it->tweet_ids);

				data.dm_count = it->dm_ids.size();
				data.dm_blob = settocompressedblob_desc(it->dm_ids);

				data.dispname = stdstrwx(it->dispname);
				data.dbindex = it->dbindex;

				func(std::move(data));
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s start", cstr(funcname));
			cache.BeginTransaction(adb);
			sqlite3_stmt *setstmt = cache.GetStmt(adb, DBPSC_UPDATEACCIDLISTS);

			unsigned int total = 0;
			getfunc([&](itemdata &&data) {
				bind_compressed(setstmt, 1, std::move(data.tweet_blob), 'Z');
				bind_compressed(setstmt, 2, std::move(data.dm_blob), 'Z');
				sqlite3_bind_text(setstmt, 3, data.dispname.c_str(), data.dispname.size(), SQLITE_TRANSIENT);
				sqlite3_bind_int(setstmt, 4, data.dbindex);

				total += data.tweet_count + data.dm_count;

				int res = sqlite3_step(setstmt);
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s) for user dbindex: %d, name: %s",
							cstr(funcname), res, cstr(sqlite3_errmsg(adb)), data.dbindex, cstr(data.dispname));
				}
				else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s inserted account: dbindex: %d, name: %s, tweet IDs: %u, DM IDs: %u",
							cstr(funcname), data.dbindex, cstr(data.dispname), data.tweet_count, data.dm_count);
				}
				sqlite3_reset(setstmt);
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s end, total: %u IDs", cstr(funcname), total);
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
			for(auto &it : ad.userconts) {
				udc_ptr_p u = it.second;

				if(u->user.screen_name.empty()) {
					continue;    //don't bother saving empty user stubs
				}

				if(u->udc_flags & UDC::BEING_LOADED_FROM_DB) {
					continue;    //read of this user from DB in progress, do not try to write back
				}

				// User needs updating
				bool user_needs_updating = u->lastupdate != u->lastupdate_wrotetodb;

				// Profile image last used time needs updating (is more than 8 hours out)
				bool profimgtime_needs_updating = u->profile_img_last_used > u->profile_img_last_used_db + (8 * 60 * 60);

				if(!user_needs_updating && !profimgtime_needs_updating) {
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

				data.mention_blob = settocompressedblob_zigzag(u->mention_index);

				data.profile_img_last_used = u->profile_img_last_used;

				data.user_needs_updating = user_needs_updating;
				data.profimgtime_needs_updating = profimgtime_needs_updating;

				func(std::move(data));
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s start", cstr(funcname));
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
				if(data.cached_profile_img_sha1) {
					sqlite3_bind_blob(stmt, 6, data.cached_profile_img_sha1->hash_sha1, sizeof(data.cached_profile_img_sha1->hash_sha1), SQLITE_TRANSIENT);
				}
				else {
					sqlite3_bind_null(stmt, 6);
				}
				bind_compressed(stmt, 7, std::move(data.mention_blob));
				sqlite3_bind_int64(stmt, 8, (sqlite3_int64) data.profile_img_last_used);

				if(data.user_needs_updating) lastupdate_count++;
				if(data.profimgtime_needs_updating) profimgtime_count++;

				int res = sqlite3_step(stmt);
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s) for user id: %" llFmtSpec "u",
							cstr(funcname),res, cstr(sqlite3_errmsg(adb)), data.id);
				}
				else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s inserted user id: %" llFmtSpec "u", cstr(funcname), data.id);
				}
				sqlite3_reset(stmt);
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s end, wrote back %u users (update: %u, prof img: %u)",
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
	if(dc.IsObject()) genjsonparser::ParseUserContents(dc, ud);
	db_bind_buffer<dbb_uncompressed> profimg = column_get_compressed(stmt, 1);
	u.cached_profile_img_url.assign(profimg.data, profimg.data_size);
	if(ud.profile_img_url.empty()) {
		ud.profile_img_url.assign(profimg.data, profimg.data_size);
	}
	ud.createtime = (time_t) sqlite3_column_int64(stmt, 2);
	u.lastupdate = (uint64_t) sqlite3_column_int64(stmt, 3);
	u.lastupdate_wrotetodb = u.lastupdate;

	const char *hash = (const char*) sqlite3_column_blob(stmt, 4);
	int hashsize = sqlite3_column_bytes(stmt, 4);
	if(hashsize == sizeof(sha1_hash_block::hash_sha1)) {
		std::shared_ptr<sha1_hash_block> hashptr = std::make_shared<sha1_hash_block>();
		memcpy(hashptr->hash_sha1, hash, sizeof(sha1_hash_block::hash_sha1));
		u.cached_profile_img_sha1 = std::move(hashptr);
	}
	else {
		u.cached_profile_img_sha1.reset();
		if(hashsize)
			LogMsgFormat(LOGT::DBERR, "%s user id: %" llFmtSpec "d, has invalid profile image hash length: %d", cstr(name), (sqlite3_int64) id, hashsize);
	}

	setfromcompressedblob_generic([&](uint64_t &tid) { u.mention_index.push_back(tid); }, stmt, 5);
	u.profile_img_last_used = (uint64_t) sqlite3_column_int64(stmt, 6);
	u.profile_img_last_used_db = u.profile_img_last_used;
}

// Anything loaded here will be marked non-purgable
udc_ptr dbconn::SyncReadInUser(sqlite3 *syncdb, uint64_t id) {
	udc_ptr u = ad.GetUserContainerById(id);

	// Fetch only if in unloaded_user_ids, ie. in DB but not already loaded
	if(unloaded_user_ids.erase(id) > 0) {
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
	if(unloaded_user_ids.erase(id) > 0) {
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
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInAllUserIDs start");
	DBRowExec(adb, "SELECT id FROM users ORDER BY id DESC;", [&](sqlite3_stmt *getstmt) {
		uint64_t id = (uint64_t) sqlite3_column_int64(getstmt, 0);
		unloaded_user_ids.insert(unloaded_user_ids.end(), id);
	}, "dbconn::SyncReadInAllTweetIDs");
	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInAllUserIDs end, read %u", unloaded_user_ids.size());
}

void dbconn::SyncPostUserLoadCompletion() {
	ad.unloaded_db_user_ids = unloaded_user_ids;
	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInUser read %u", sync_load_user_count);
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
			for(auto &it : ad.user_dm_indexes) {
				user_dm_index &udi = it.second;

				if(!(udi.flags & user_dm_index::UDIF::ISDIRTY))
					continue;

				itemdata data;
				data.id = it.first;
				data.dmset_blob = settocompressedblob_desc(udi.ids);
				udi.flags &= ~user_dm_index::UDIF::ISDIRTY;

				func(std::move(data));
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s start", cstr(funcname));
			cache.BeginTransaction(adb);

			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSUSERDMINDEX);
			unsigned int count = 0;
			getfunc([&](itemdata &&data) {
				count++;
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) data.id);
				bind_compressed(stmt, 2, std::move(data.dmset_blob));

				int res = sqlite3_step(stmt);
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s) for user id: %" llFmtSpec "u",
							cstr(funcname),res, cstr(sqlite3_errmsg(adb)), data.id);
				}
				else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s inserted user id: %" llFmtSpec "u", cstr(funcname), data.id);
				}
				sqlite3_reset(stmt);
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s end, wrote back %u of %u user DM indexes",
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

void dbconn::SyncReadInUserDMIndexes(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInUserDMIndexes start");

	unsigned int read_count = 0;
	tweetidset dmindex;
	DBRowExec(adb, "SELECT userid, dmindex FROM userdmsets;", [&](sqlite3_stmt *stmt) {
		setfromcompressedblob(dmindex, stmt, 1);
		if(!dmindex.empty()) {
			uint64_t userid = (uint64_t) sqlite3_column_int64(stmt, 0);
			user_dm_index &udi = ad.GetUserDMIndexById(userid);
			udi.ids = std::move(dmindex);
			dmindex.clear();
			read_count++;
			SyncReadInUser(adb, userid);
		}
	}, "dbconn::SyncReadInUserDMIndexes");

	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInUserDMIndexes end, read in %u", read_count);
}

namespace {
	struct WriteBackRBFSs {
		struct itemdata {
			restbackfillstate rbfs;
			unsigned int dbindex;
		};

		//Where F is a functor of the form void(const itemdata &)
		template <typename F> void operator()(F func) const {
			for(auto &it : alist) {
				taccount &acc = *it;
				for(restbackfillstate &rbfs : acc.pending_rbfs_list) {
					if(rbfs.start_tweet_id >= acc.GetMaxId(rbfs.type)) continue;    //rbfs would be read next time anyway
					if(!rbfs.end_tweet_id || rbfs.end_tweet_id >= acc.GetMaxId(rbfs.type)) {
						rbfs.end_tweet_id = acc.GetMaxId(rbfs.type);    //remove overlap
						if(rbfs.end_tweet_id) rbfs.end_tweet_id--;
					}
					func(itemdata { rbfs, acc.dbindex });
				}
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, std::string funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s start", cstr(funcname));

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
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s)", cstr(funcname), res, cstr(sqlite3_errmsg(adb)));
				}
				else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s inserted pending RBFS", cstr(funcname));
				}
				sqlite3_reset(stmt);
				write_count++;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s end, wrote %u", cstr(funcname), write_count);
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
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInRBFSs start");

	unsigned int read_count = 0;
	DBRowExec(adb, "SELECT accid, type, startid, endid, maxleft FROM rbfspending;", [&](sqlite3_stmt *stmt) {
		unsigned int dbindex = (unsigned int) sqlite3_column_int64(stmt, 0);
		bool found = false;
		for(auto &it : alist) {
			if(it->dbindex == dbindex) {
				RBFS_TYPE type = (RBFS_TYPE) sqlite3_column_int64(stmt, 1);
				if((type < RBFS_MIN) || (type > RBFS_MAX)) break;

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
		if(found) { LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInRBFSs retrieved RBFS"); }
		else { LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInRBFSs retrieved RBFS with no associated account or bad type, ignoring"); }
	}, "dbconn::SyncReadInRBFSs");
	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInRBFSs end, read in %u", read_count);
}

void dbconn::SyncReadInAllMediaEntities(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInAllMediaEntities start");

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
		if(url.data_size) {
			me.media_url.assign(url.data, url.data_size);
			ad.img_media_map[me.media_url] = meptr;
		}
		if(sqlite3_column_bytes(stmt, 3) == hash_size) {
			std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
			memcpy(hash->hash_sha1, sqlite3_column_blob(stmt, 3), hash_size);
			me.full_img_sha1 = std::move(hash);
			me.flags |= MEF::LOAD_FULL;
			full_count++;
		}
		if(sqlite3_column_bytes(stmt, 4) == hash_size) {
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

	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInAllMediaEntities end, read in %u, cached: thumb: %u, full: %u", read_count, thumb_count, full_count);
}

void dbconn::SyncReadInWindowLayout(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInWindowLayout start");

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
		"SELECT mainframeindex, splitindex, tabindex, name, dispname, flags, rowid FROM tpanelwins ORDER BY mainframeindex ASC, splitindex ASC, tabindex ASC;");
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

			DBBindRowExec(adb, winautossel.stmt(),
				[&](sqlite3_stmt *stmt2) {
					sqlite3_bind_int(stmt2, 1, rowid);
				},
				[&](sqlite3_stmt *stmt2) {
					std::shared_ptr<taccount> acc;
					int accid = (int) sqlite3_column_int(stmt2, 0);
					if(accid > 0) {
						for(auto &it : alist) {
							if(it->dbindex == (unsigned int) accid) {
								acc = it;
								break;
							}
						}
						if(!acc) return;
					}
					else {
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
					if(uid > 0) {
						twld.tpudcautos.back().u = ad.GetUserContainerById(uid);
					}
					twld.tpudcautos.back().autoflags = static_cast<TPFU>(sqlite3_column_int(stmt3, 1));
				},
				"dbconn::SyncReadInWindowLayout (tpanelwinudcautos)"
			);
		},
		"dbconn::SyncReadInWindowLayout (tpanelwins)"
	);

	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInWindowLayout end");
}

void dbconn::SyncWriteBackWindowLayout(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncWriteBackWindowLayout start");
	cache.BeginTransaction(adb);

	const std::string errspec = "dbconn::SyncWriteBackWindowLayout";
	DBExec(adb, "DELETE FROM mainframewins", errspec);

	auto mfins = DBInitialiseSql(adb, "INSERT INTO mainframewins (mainframeindex, x, y, w, h, maximised) VALUES (?, ?, ?, ?, ?, ?);");

	for(auto &mfld : ad.mflayout) {
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
	auto winsins = DBInitialiseSql(adb, "INSERT INTO tpanelwins (mainframeindex, splitindex, tabindex, name, dispname, flags) VALUES (?, ?, ?, ?, ?, ?);");
	auto winautosins = DBInitialiseSql(adb, "INSERT INTO tpanelwinautos (tpw, accid, autoflags) VALUES (?, ?, ?);");
	auto winudcautosins = DBInitialiseSql(adb, "INSERT INTO tpanelwinudcautos (tpw, userid, autoflags) VALUES (?, ?, ?);");

	for(auto &twld : ad.twinlayout) {
		DBBindExec(adb, winsins.stmt(),
			[&](sqlite3_stmt *stmt) {
				sqlite3_bind_int(stmt, 1, twld.mainframeindex);
				sqlite3_bind_int(stmt, 2, twld.splitindex);
				sqlite3_bind_int(stmt, 3, twld.tabindex);
				sqlite3_bind_text(stmt, 4, twld.name.c_str(), twld.name.size(), SQLITE_STATIC);
				sqlite3_bind_text(stmt, 5, twld.dispname.c_str(), twld.dispname.size(), SQLITE_STATIC);
				sqlite3_bind_int(stmt, 6, flag_unwrap<TPF>(twld.flags));
			},
			"dbconn::SyncWriteOutWindowLayout (tpanelwins)"
		);
		sqlite3_int64 rowid = sqlite3_last_insert_rowid(adb);

		for(auto &it : twld.tpautos) {
			DBBindExec(adb, winautosins.stmt(),
				[&](sqlite3_stmt *stmt) {
					sqlite3_bind_int(stmt, 1, rowid);
					if(it.acc) sqlite3_bind_int(stmt, 2, it.acc->dbindex);
					else sqlite3_bind_int(stmt, 2, -1);
					sqlite3_bind_int(stmt, 3, flag_unwrap<TPF>(it.autoflags));
				},
				"dbconn::SyncWriteOutWindowLayout (tpanelwinautos)"
			);
		}
		for(auto &it : twld.tpudcautos) {
			DBBindExec(adb, winudcautosins.stmt(),
				[&](sqlite3_stmt *stmt) {
					sqlite3_bind_int(stmt, 1, rowid);
					if(it.u) sqlite3_bind_int64(stmt, 2, it.u->id);
					else sqlite3_bind_int64(stmt, 2, 0);
					sqlite3_bind_int(stmt, 3, flag_unwrap<TPFU>(it.autoflags));
				},
				"dbconn::SyncWriteOutWindowLayout (tpanelwinudcautos)"
			);
		}
	}
	cache.EndTransaction(adb);
	LogMsg(LOGT::DBTRACE, "dbconn::SyncWriteBackWindowLayout end");
}

void dbconn::SyncReadInTpanels(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInTpanels start");

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

	LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInTpanels end, read in %u, IDs: %u", read_count, id_count);
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
			for(auto &it : ad.tpanels) {
				tpanel &tp = *(it.second);
				if(tp.flags & TPF::SAVETODB) {
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
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s start", cstr(funcname));

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
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, "%s got error: %d (%s)", cstr(funcname), res, cstr(sqlite3_errmsg(adb)));
				}
				sqlite3_reset(stmt);
				write_count++;
				id_count += data.tweetlist_count;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, "%s end, wrote %u, IDs: %u", cstr(funcname), write_count, id_count);
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
	LogMsg(LOGT::DBTRACE, "dbconn::SyncReadInUserRelationships start");

	auto s = DBInitialiseSql(adb, "SELECT userid, flags, followmetime, ifollowtime FROM userrelationships WHERE accid == ?;");

	for(auto &it : alist) {
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
		LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncReadInUserRelationships read in %u for account: %s", read_count, cstr(it->dispname));
	}
}

void dbconn::SyncWriteBackUserRelationships(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncWriteBackUserRelationships start");

	cache.BeginTransaction(adb);
	sqlite3_exec(adb, "DELETE FROM userrelationships", 0, 0, 0);

	auto s = DBInitialiseSql(adb, "INSERT INTO userrelationships (accid, userid, flags, followmetime, ifollowtime) VALUES (?, ?, ?, ?, ?);");

	for(auto &it : alist) {
		unsigned int write_count = 0;

		for(auto &ur : it->user_relations) {
			using URF = user_relationship::URF;
			URF flags = ur.second.ur_flags;
			if(!(flags & (URF::FOLLOWSME_TRUE | URF::IFOLLOW_TRUE | URF::FOLLOWSME_PENDING | URF::IFOLLOW_PENDING))) continue;
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
		LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncWriteBackUserRelationships wrote %u for account: %s", write_count, cstr(it->dispname));
	}

	cache.EndTransaction(adb);
	LogMsg(LOGT::DBTRACE, "dbconn::SyncWriteBackUserRelationships end");
}

bool dbconn::CheckIfPurgeDue(sqlite3 *db, time_t threshold, const char *settingname, const char *funcname, time_t &delta) {
	time_t last_purge = 0;

	sqlite3_stmt *getstmt = cache.GetStmt(db, DBPSC_SELSTATICSETTING);
	sqlite3_bind_text(getstmt, 1, settingname, -1, SQLITE_STATIC);
	DBRowExec(db, getstmt, [&](sqlite3_stmt *stmt) {
		last_purge = (time_t) sqlite3_column_int64(stmt, 0);
	}, string_format("%s (get last purged)", funcname));

	delta = time(nullptr) - last_purge;

	if(delta < threshold) {
		LogMsgFormat(LOGT::DBTRACE, "%s, last purged %" llFmtSpec "ds ago, not checking", cstr(funcname), (int64_t) delta);
		return false;
	}
	else {
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
	LogMsg(LOGT::DBTRACE, "dbconn::SyncPurgeMediaEntities start");

	const char *lastpurgesetting = "lastmediacachepurge";
	const char *funcname = "dbconn::SyncPurgeMediaEntities";
	const time_t day = 60 * 60 * 24;
	time_t delta;

	if(CheckIfPurgeDue(syncdb, day, lastpurgesetting, funcname, delta)) {
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
					if(sqlite3_column_bytes(stmt, 2) > 0) {
						thumb_count++;
						if(!gc.readonlymode) wxRemoveFile(media_entity::cached_thumb_filename(mid));
					}
					if(sqlite3_column_bytes(stmt, 3) > 0) {
						full_count++;
						if(!gc.readonlymode) wxRemoveFile(media_entity::cached_full_filename(mid));
					}
					purge_list.push_back(mid);
				}, "dbconn::SyncPurgeMediaEntities (get purge list)");

		if(!gc.readonlymode) {
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

		LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncPurgeMediaEntities end, last purged %" llFmtSpec "ds ago, %spurged %u, (thumb: %u, full: %u)",
				(int64_t) delta, gc.readonlymode ? "would have " : "", (unsigned int) purge_list.size(), thumb_count, full_count);
	}
}

void dbconn::SyncPurgeProfileImages(sqlite3 *syncdb) {
	LogMsg(LOGT::DBTRACE, "dbconn::SyncPurgeProfileImages start");

	const char *lastpurgesetting = "lastprofileimagepurge";
	const char *funcname = "dbconn::SyncPurgeProfileImages";
	const time_t day = 60 * 60 * 24;
	time_t delta;

	if(CheckIfPurgeDue(syncdb, day, lastpurgesetting, funcname, delta)) {
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

		if(!gc.readonlymode && expire_list.size()) {
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

		LogMsgFormat(LOGT::DBTRACE, "dbconn::SyncPurgeProfileImages end, last purged %" llFmtSpec "ds ago, %spurged %u",
				(int64_t) delta, gc.readonlymode ? "would have " : "", (unsigned int) expire_list.size());
	}
}

void dbconn::OnAsyncStateWriteTimer(wxTimerEvent& event) {
	AsyncWriteBackState();
	ResetAsyncStateWriteTimer();
}

void dbconn::ResetAsyncStateWriteTimer() {
	if(gc.asyncstatewritebackintervalmins > 0) {
		asyncstateflush_timer->Start(gc.asyncstatewritebackintervalmins * 1000 * 60, wxTIMER_ONE_SHOT);
	}
}

//The contents of data will be released and stashed in the event sent to the main thread
//The main thread will then unstash it from the event and stick it back in a unique_ptr
void dbsendmsg_callback::SendReply(std::unique_ptr<dbsendmsg> data, dbiothread *th) {
	wxCommandEvent *evt = new wxCommandEvent(cmdevtype, winid);
	evt->SetClientData(data.release());
	th->reply_list.emplace_back(targ, std::unique_ptr<wxEvent>(evt));
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
	switch(dbindextype) {
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
	if(res == SQLITE_ROW) return true;
	else if(res == SQLITE_DONE) return false;
	else {
		DBLogMsgFormat(LOGT::DBERR, "DBReadConfig got error: %d (%s)", res, cstr(sqlite3_errmsg(db)));
		return false;
	}
}

bool DBReadConfig::Read(const char *name, wxString *strval, const wxString &defval) {
	sqlite3_stmt *stmt = dbc.cache.GetStmt(db, DBPSC_SELSETTING);
	bind_accid_name(stmt, name);
	bool ok = exec(stmt);
	if(ok) {
		const char *text = (const char *) sqlite3_column_text(stmt, 0);
		int len = sqlite3_column_bytes(stmt, 0);
		*strval = wxstrstd(text, len);
	}
	else *strval = defval;
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

bool DBReadConfig::ReadInt64(const char *name, int64_t *strval, int64_t defval) {
	sqlite3_stmt *stmt = dbc.cache.GetStmt(db, DBPSC_SELSETTING);
	bind_accid_name(stmt, name);
	bool ok = exec(stmt);
	if(ok) {
		*strval = sqlite3_column_int64(stmt, 0);
	}
	else *strval = defval;
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

void DBC_SendMessage(std::unique_ptr<dbsendmsg> msg) {
	dbc.SendMessage(std::move(msg));
}

void DBC_SendMessageOrAddToList(std::unique_ptr<dbsendmsg> msg, dbsendmsg_list *msglist) {
	dbc.SendMessageOrAddToList(std::move(msg), msglist);
}

void DBC_SendMessageBatched(std::unique_ptr<dbsendmsg> msg) {
	dbc.SendMessageBatched(std::move(msg));
}

observer_ptr<dbsendmsg_list> DBC_GetMessageBatchQueue() {
	return dbc.GetMessageBatchQueue();
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

void DBC_InsertNewTweet(tweet_ptr_p tobj, std::string statjson, optional_observer_ptr<dbsendmsg_list> msglist) {
	dbc.InsertNewTweet(tobj, statjson, msglist);
}

void DBC_UpdateTweetDyn(tweet_ptr_p tobj, optional_observer_ptr<dbsendmsg_list> msglist) {
	dbc.UpdateTweetDyn(tobj, msglist);
}

void DBC_InsertUser(udc_ptr_p u, optional_observer_ptr<dbsendmsg_list> msglist) {
	dbc.InsertUser(u, msglist);
}

void DBC_HandleDBSelTweetMsg(dbseltweetmsg &msg, flagwrapper<HDBSF> flags) {
	dbc.HandleDBSelTweetMsg(msg, flags);
}

void DBC_SetDBSelTweetMsgHandler(dbseltweetmsg &msg, std::function<void(dbseltweetmsg &, dbconn *)> f) {
	dbc.SetDBSelTweetMsgHandler(msg, std::move(f));
}

void DBC_PrepareStdTweetLoadMsg(dbseltweetmsg &loadmsg) {
	dbc.PrepareStdTweetLoadMsg(loadmsg);
}

void DBC_DBSelUserReturnDataHandler(std::deque<dbretuserdata> data, flagwrapper<HDBSF> flags) {
	dbc.DBSelUserReturnDataHandler(std::move(data), flags);
}

void DBC_SetDBSelUserMsgHandler(dbselusermsg &msg, std::function<void(dbselusermsg &, dbconn *)> f) {
	dbc.SetDBSelUserMsgHandler(msg, std::move(f));
}

void DBC_PrepareStdUserLoadMsg(dbselusermsg &loadmsg) {
	dbc.PrepareStdUserLoadMsg(loadmsg);
}
