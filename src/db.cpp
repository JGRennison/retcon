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

static const esctable *dynjsontable = &allesctables[0];

static const char *startup_sql=
"PRAGMA locking_mode = EXCLUSIVE;"
"CREATE TABLE IF NOT EXISTS tweets(id INTEGER PRIMARY KEY NOT NULL, statjson BLOB, dynjson BLOB, userid INTEGER, userrecipid INTEGER, flags INTEGER, timestamp INTEGER, rtid INTEGER);"
"CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY NOT NULL, json BLOB, cachedprofimgurl BLOB, createtimestamp INTEGER, lastupdatetimestamp INTEGER, cachedprofileimgchecksum BLOB, mentionindex BLOB);"
"CREATE TABLE IF NOT EXISTS acc(id INTEGER PRIMARY KEY NOT NULL, name TEXT, dispname TEXT, json BLOB, tweetids BLOB, dmids BLOB, userid INTEGER);"
"CREATE TABLE IF NOT EXISTS settings(accid BLOB, name TEXT, value BLOB, PRIMARY KEY (accid, name));"
"CREATE TABLE IF NOT EXISTS rbfspending(accid INTEGER, type INTEGER, startid INTEGER, endid INTEGER, maxleft INTEGER);"
"CREATE TABLE IF NOT EXISTS mediacache(mid INTEGER, tid INTEGER, url BLOB, fullchecksum BLOB, thumbchecksum BLOB, flags INTEGER, lastusedtimestamp INTEGER, PRIMARY KEY (mid, tid));"
"CREATE TABLE IF NOT EXISTS tpanelwins(mainframeindex INTEGER, splitindex INTEGER, tabindex INTEGER, name TEXT, dispname TEXT, flags INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanelwinautos(tpw INTEGER, accid INTEGER, autoflags INTEGER);"
"CREATE TABLE IF NOT EXISTS mainframewins(mainframeindex INTEGER, x INTEGER, y INTEGER, w INTEGER, h INTEGER, maximised INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanels(name TEXT, dispname TEXT, flags INTEGER, ids BLOB);"
"CREATE TABLE IF NOT EXISTS staticsettings(name TEXT PRIMARY KEY NOT NULL, value BLOB);"
"INSERT OR REPLACE INTO settings(accid, name, value) VALUES ('G', 'dirtyflag', strftime('%s','now'));";

static const char *std_sql_stmts[DBPSC_NUM_STATEMENTS]={
	"INSERT OR REPLACE INTO tweets(id, statjson, dynjson, userid, userrecipid, flags, timestamp, rtid) VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
	"UPDATE tweets SET dynjson = ?, flags = ? WHERE id == ?;",
	"BEGIN;",
	"COMMIT;",
	"INSERT OR REPLACE INTO users(id, json, cachedprofimgurl, createtimestamp, lastupdatetimestamp, cachedprofileimgchecksum, mentionindex) VALUES (?, ?, ?, ?, ?, ?, ?);",
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
};

static const char *update_sql[] = {
	"ALTER TABLE mediacache ADD COLUMN lastusedtimestamp INTEGER;"
	"UPDATE OR IGNORE mediacache SET lastusedtimestamp = strftime('%s','now');"
	"UPDATE OR IGNORE settings SET accid = 'G' WHERE (hex(accid) == '4700');"  //This is because previous versions of retcon accidentally inserted an embedded null when writing out the config
	"UPDATE OR IGNORE tweets SET medialist = NULL;"
	,
};
static const unsigned int db_version = 1;

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

static unsigned char *DoCompress(const void *in, size_t insize, size_t &sz, unsigned char tag = 'Z', bool *iscompressed = 0, const esctable *et = 0) {
	unsigned char *data = 0;
	if(et) {
		for(unsigned int i = 0; i < et->count; i++) {
			if(strlen(et->start[i].text) == insize && memcmp(et->start[i].text, in, insize) == 0) {
				data = (unsigned char *) malloc(2);
				data[0] = et->tag;
				data[1] = et->start[i].id;
				sz = 2;
				if(iscompressed) *iscompressed = true;
				break;
			}
		}
	}
	if(!data) {
		const unsigned char *dict;
		size_t dict_size;
		bool compress = TagToDict(tag, dict, dict_size);
		if(compress && insize >= 100) {
			z_stream strm;
			strm.zalloc = Z_NULL;
			strm.zfree = Z_NULL;
			strm.opaque = Z_NULL;
			deflateInit(&strm, 5);

			if(dict) deflateSetDictionary(&strm, dict, dict_size);
			size_t maxsize = deflateBound(&strm, insize);
			data = (unsigned char *) malloc(maxsize + HEADERSIZE);
			data[0] = tag;
			data[1] = (insize >> 24) & 0xFF;
			data[2] = (insize >> 16) & 0xFF;
			data[3] = (insize >> 8) & 0xFF;
			data[4] = (insize >> 0) & 0xFF;
			strm.avail_in = insize;
			strm.next_in = (unsigned char *) in;
			strm.avail_out = maxsize;
			strm.next_out = data + HEADERSIZE;
			int res = deflate(&strm, Z_FINISH);
			#if DB_COPIOUS_LOGGING
				DBLogMsgFormat(LOGT::ZLIBTRACE, wxT("deflate: %d, %d, %d"), res, strm.avail_in, strm.avail_out);
			#endif
			if(res != Z_STREAM_END) { DBLogMsgFormat(LOGT::ZLIBERR, wxT("DoCompress: deflate: error: res: %d (%s)"), res, wxstrstd(strm.msg).c_str()); }
			sz = HEADERSIZE + maxsize - strm.avail_out;
			deflateEnd(&strm);
			if(iscompressed) *iscompressed = true;
		}
		else {
			data=(unsigned char *) malloc(insize + 1);
			data[0] = 'T';
			if(in) memcpy(data + 1, in, insize);
			sz = insize + 1;
			if(iscompressed) *iscompressed = false;
		}
	}

#if DB_COPIOUS_LOGGING
		static size_t cumin = 0;
		static size_t cumout = 0;
		cumin += insize;
		cumout += sz;
		DBLogMsgFormat(LOGT::ZLIBTRACE, wxT("compress: %d -> %d, cum: %f"), insize, sz, (double) cumout / (double) cumin);
	#endif

	return data;
}

static unsigned char *DoCompress(const std::string &in, size_t &sz, unsigned char tag = 'Z', bool *iscompressed = 0, const esctable *et = 0) {
	return DoCompress(in.data(), in.size(), sz, tag, iscompressed, et);
}

static void bind_compressed(sqlite3_stmt* stmt, int num, const char *in, size_t insize, unsigned char tag = 'Z', const esctable *et = nullptr) {
	size_t comsize;
	unsigned char *com = DoCompress(in, insize, comsize, tag, nullptr, et);
	sqlite3_bind_blob(stmt, num, com, comsize, &free);
}

static void bind_compressed(sqlite3_stmt* stmt, int num, const unsigned char *in, size_t insize, unsigned char tag = 'Z', const esctable *et = nullptr) {
	bind_compressed(stmt, num, reinterpret_cast<const char *>(in), insize, tag, et);
}

static void bind_compressed(sqlite3_stmt* stmt, int num, const std::string &in, unsigned char tag = 'Z', const esctable *et = nullptr) {
	bind_compressed(stmt, num, in.data(), in.size(), tag, et);
}

static char *DoDecompress(const unsigned char *in, size_t insize, size_t &outsize) {
	if(!insize) {
		DBLogMsg(LOGT::ZLIBTRACE, wxT("DoDecompress: insize == 0"));
		outsize = 0;
		return 0;
	}
	if(insize == 2) {
		for(unsigned int i = 0; i < sizeof(allesctables) / sizeof(esctable); i++) {
			if(in[0] == allesctables[i].tag) {
				for(unsigned int j = 0; j < allesctables[i].count; j++) {
					if(in[1] == allesctables[i].start[j].id) {
						outsize = strlen(allesctables[i].start[j].text);
						char *data = (char *) malloc(outsize+1);
						memcpy(data, allesctables[i].start[j].text, outsize);
						data[outsize] = 0;
						return data;
					}
				}
				DBLogMsg(LOGT::ZLIBERR, wxT("DoDecompress: Bad escape table identifier"));
				outsize = 0;
				return 0;
			}
		}
	}
	const unsigned char *dict;
	size_t dict_size;
	switch(in[0]) {
		case 'T': {
			outsize = insize - 1;
			char *data = (char *) malloc(outsize + 1);
			memcpy(data, in + 1, outsize);
			data[outsize] = 0;
			return data;
		}
		default: {
			bool compress = TagToDict(in[0], dict, dict_size);
			if(compress) break;
			else {
				DBLogMsg(LOGT::ZLIBERR, wxT("DoDecompress: Bad tag"));
				outsize = 0;
				return 0;
			}
		}
	}
	z_stream strm;
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.opaque = Z_NULL;
	strm.next_in = (unsigned char*) in + HEADERSIZE;
	strm.avail_in = insize - HEADERSIZE;
	inflateInit(&strm);
	outsize = 0;
	for(unsigned int i = 1; i < 5; i++) {
		outsize <<= 8;
		outsize += in[i];
	}
	#if DB_COPIOUS_LOGGING
		DBLogMsgFormat(LOGT::ZLIBTRACE, wxT("DoDecompress: insize %d, outsize %d"), insize, outsize);
	#endif
	unsigned char *data = (unsigned char *) malloc(outsize+1);
	strm.next_out = data;
	strm.avail_out = outsize;
	while(true) {
		int res = inflate(&strm, Z_FINISH);
		#if DB_COPIOUS_LOGGING
			DBLogMsgFormat(LOGT::ZLIBTRACE, wxT("inflate: %d, %d, %d"), res, strm.avail_in, strm.avail_out);
		#endif
		if(res == Z_NEED_DICT) {
			if(dict) inflateSetDictionary(&strm, dict, dict_size);
			else {
				outsize=0;
				inflateEnd(&strm);
				free(data);
				DBLogMsgFormat(LOGT::ZLIBTRACE, wxT("DoDecompress: Wants dictionary: %ux"), strm.adler);
				return 0;
			}
		}
		else if(res == Z_OK) continue;
		else if(res == Z_STREAM_END) break;
		else {
			DBLogMsgFormat(LOGT::ZLIBERR, wxT("DoDecompress: inflate: error: res: %d (%s)"), res, wxstrstd(strm.msg).c_str());
			outsize = 0;
			inflateEnd(&strm);
			free(data);
			return 0;
		}
	}

	inflateEnd(&strm);
	data[outsize] = 0;

	#if DB_COPIOUS_LOGGING
		DBLogMsgFormat(LOGT::ZLIBTRACE,wxT("decompress: %d -> %d, text: %s"), insize, outsize, wxstrstd((const char*) data).c_str());
	#endif
	return (char *) data;
}

//the result should be freed when done if non-zero
static char *column_get_compressed(sqlite3_stmt* stmt, int num, size_t &outsize) {
	const unsigned char *data = (const unsigned char *) sqlite3_column_blob(stmt, num);
	int size = sqlite3_column_bytes(stmt, num);
	return DoDecompress(data, size, outsize);
}

//the result should be freed when done if non-zero
static char *column_get_compressed_and_parse(sqlite3_stmt* stmt, int num, rapidjson::Document &dc) {
	size_t str_size;
	char *str = column_get_compressed(stmt, num, str_size);
	if(str) {
		if(dc.ParseInsitu<0>(str).HasParseError()) {
			DisplayParseErrorMsg(dc, wxT("column_get_compressed_and_parse"), str);
			dc.SetNull();
		}
	}
	else dc.SetNull();
	return str;
}

inline void writebeuint64(unsigned char* data, uint64_t id) {
	data[0] = (id >> 56) & 0xFF;
	data[1] = (id >> 48) & 0xFF;
	data[2] = (id >> 40) & 0xFF;
	data[3] = (id >> 32) & 0xFF;
	data[4] = (id >> 24) & 0xFF;
	data[5] = (id >> 16) & 0xFF;
	data[6] = (id >> 8) & 0xFF;
	data[7] = (id >> 0) & 0xFF;
}

template <typename C> unsigned char *settoblob(const C &set, size_t &size) {
	size = set.size() * 8;
	if(!size) return 0;
	unsigned char *data = (unsigned char *) malloc(size);
	unsigned char *curdata = data;
	for(auto &it : set) {
		writebeuint64(curdata, it);
		curdata += 8;
	}
	return data;
}

template <typename C> unsigned char *settocompressedblob(const C &set, size_t &size) {
	size_t insize;
	unsigned char *data = settoblob(set, insize);
	unsigned char *comdata = DoCompress(data, insize, size, 'Z');
	free(data);
	return comdata;
}

template <typename C> void setfromcompressedblob(C func, sqlite3_stmt *stmt, int columnid) {
	size_t blarraysize;
	unsigned char *blarray = (unsigned char*) column_get_compressed(stmt, columnid, blarraysize);
	blarraysize &= ~7;
	for(unsigned int i = 0; i < blarraysize; i += 8) {    //stored in big endian format
		uint64_t id = 0;
		for(unsigned int j = 0; j < 8; j++) id <<= 8, id |= blarray[i + j];
		func(id);
	}
	free(blarray);
}

//! This calls itself for retweet sources, *unless* the retweet source ID is in idset
//! This expects to be called in *ascending* ID order
static void ProcessMessage_SelTweet(sqlite3 *db, sqlite3_stmt *stmt, dbseltweetmsg &m, std::deque<dbrettweetdata> &recv_data, uint64_t id,
		const container::set<uint64_t> &idset, bool front_insert = false) {
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64) id);
	int res = sqlite3_step(stmt);
	uint64_t rtid = 0;
	if(res == SQLITE_ROW) {
		#if DB_COPIOUS_LOGGING
			DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::SELTWEET got id:%" wxLongLongFmtSpec "d"), (sqlite3_int64) id);
		#endif

		// emplacing at the *back* in the normal case is to ensure that the resulting deque is (mostly) in *ascending* order of ID
		// This ensures that tweets come before any retweets which use them as a source
		// *front* emplacing is used to ensure that any missing retweet sources come before any tweets which use them
		if(front_insert) recv_data.emplace_front();
		else recv_data.emplace_back();
		dbrettweetdata &rd = front_insert ? recv_data.front() : recv_data.back();

		size_t outsize;
		rd.id = (id);
		rd.statjson = column_get_compressed(stmt, 0, outsize);
		rd.dynjson = column_get_compressed(stmt, 1, outsize);
		rd.user1 = (uint64_t) sqlite3_column_int64(stmt, 2);
		rd.user2 = (uint64_t) sqlite3_column_int64(stmt, 3);
		rd.flags = (uint64_t) sqlite3_column_int64(stmt, 4);
		rd.timestamp = (uint64_t) sqlite3_column_int64(stmt, 5);
		rd.rtid = rtid = (uint64_t) sqlite3_column_int64(stmt, 6);
	}
	else { DBLogMsgFormat((m.flags & DBSTMF::NO_ERR) ? LOGT::DBTRACE : LOGT::DBERR,
			wxT("DBSM::SELTWEET got error: %d (%s) for id: %" wxLongLongFmtSpec "d, net fallback flag: %d"),
			res, wxstrstd(sqlite3_errmsg(db)).c_str(), (sqlite3_int64) id, (m.flags & DBSTMF::NET_FALLBACK) ? 1 : 0); }
	sqlite3_reset(stmt);

	if(rtid && idset.find(rtid) == idset.end()) {
		// This is a retweet, if we're not already loading the retweet source, load it here
		// Note that this is front emplaced in *front* of the retweet which needs it
		ProcessMessage_SelTweet(db, stmt, m, recv_data, rtid, idset, true);
	}
}

//Note that the contents of themsg may be stolen if the lifetime of the message needs to be extended
//This is generally the case for messages which also act as replies
static void ProcessMessage(sqlite3 *db, std::unique_ptr<dbsendmsg> &themsg, bool &ok, dbpscache &cache, dbiothread *th) {
	dbsendmsg *msg = themsg.get();
	switch(msg->type) {
		case DBSM::QUIT:
			ok = false;
			DBLogMsg(LOGT::DBTRACE, wxT("DBSM::QUIT"));
			break;
		case DBSM::INSERTTWEET: {
			if(gc.readonlymode) break;
			dbinserttweetmsg *m = static_cast<dbinserttweetmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSTWEET);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->id);
			bind_compressed(stmt, 2, m->statjson, 'J');
			bind_compressed(stmt, 3, m->dynjson, 'J', dynjsontable);
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->user1);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) m->user2);
			sqlite3_bind_int64(stmt, 6, (sqlite3_int64) m->flags);
			sqlite3_bind_int64(stmt, 7, (sqlite3_int64) m->timestamp);
			sqlite3_bind_int64(stmt, 8, (sqlite3_int64) m->rtid);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::INSERTTWEET got error: %d (%s) for id:%" wxLongLongFmtSpec "d"),
					res, wxstrstd(sqlite3_errmsg(db)).c_str(), m->id); }
			else { DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::INSERTTWEET inserted row id:%" wxLongLongFmtSpec "d"), (sqlite3_int64) m->id); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::UPDATETWEET: {
			if(gc.readonlymode) break;
			dbupdatetweetmsg *m = static_cast<dbupdatetweetmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_UPDTWEET);
			bind_compressed(stmt, 1, m->dynjson, 'J', dynjsontable);
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->flags);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) m->id);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::UPDATETWEET got error: %d (%s) for id:%" wxLongLongFmtSpec "d"),
					res, wxstrstd(sqlite3_errmsg(db)).c_str(), m->id); }
			else { DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::UPDATETWEET updated id:%" wxLongLongFmtSpec "d"), (sqlite3_int64) m->id); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::SELTWEET: {
			dbseltweetmsg *m = static_cast<dbseltweetmsg*>(msg);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_SELTWEET);
			std::deque<dbrettweetdata> recv_data;

			//This is *ascending* ID order
			for(auto it = m->id_set.cbegin(); it != m->id_set.cend(); ++it) {
				ProcessMessage_SelTweet(db, stmt, *m, recv_data, *it, m->id_set);
			}
			if(!recv_data.empty() || m->flags & DBSTMF::NET_FALLBACK) {
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
			sqlite3_bind_blob(stmt, 7, m->mentionindex, m->mentionindex_size, &free);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::INSERTUSER got error: %d (%s) for id: %" wxLongLongFmtSpec "d"),
					res, wxstrstd(sqlite3_errmsg(db)).c_str(), m->id); }
			else { DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::INSERTUSER inserted id: %" wxLongLongFmtSpec "d"), (sqlite3_int64) m->id); }
			sqlite3_reset(stmt);
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
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::INSERTACC got error: %d (%s) for account name: %s"),
					res, wxstrstd(sqlite3_errmsg(db)).c_str(), wxstrstd(m->dispname).c_str()); }
			else { DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::INSERTACC inserted account dbindex: %d, name: %s"), m->dbindex, wxstrstd(m->dispname).c_str()); }
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
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::DELACC got error: %d (%s) for account dbindex: %d"),
					res, wxstrstd(sqlite3_errmsg(db)).c_str(), m->dbindex); }
			else { DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::DELACC deleted account dbindex: %d"), m->dbindex); }
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
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::INSERTMEDIA got error: %d (%s) for id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d"),
					res, wxstrstd(sqlite3_errmsg(db)).c_str(), (sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id); }
			else { DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::INSERTMEDIA inserted media id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d"),
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
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::UPDATEMEDIAMSG got error: %d (%s) for id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d (%d)"),
					res, wxstrstd(sqlite3_errmsg(db)).c_str(), (sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id, m->update_type); }
			else { DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::UPDATEMEDIAMSG updated media id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d (%d)"),
					(sqlite3_int64) m->media_id.m_id, (sqlite3_int64) m->media_id.t_id, m->update_type); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::UPDATETWEETSETFLAGS: {
			if(gc.readonlymode) break;
			dbupdatetweetsetflagsmsg *m = static_cast<dbupdatetweetsetflagsmsg*>(msg);
			cache.BeginTransaction(db);
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_UPDATETWEETFLAGSMASKED);
			for(auto it = m->ids.begin(); it != m->ids.end(); ++it) {
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->setmask);
				sqlite3_bind_int64(stmt, 2, (sqlite3_int64) (~m->unsetmask));
				sqlite3_bind_int64(stmt, 3, (sqlite3_int64) *it);
				int res = sqlite3_step(stmt);
				if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::UPDATETWEETSETFLAGS got error: %d (%s) for id: %" wxLongLongFmtSpec "d"),
						res, wxstrstd(sqlite3_errmsg(db)).c_str(), *it); }
				sqlite3_reset(stmt);
			}
			cache.EndTransaction(db);
			break;
		}
		case DBSM::MSGLIST: {
			cache.BeginTransaction(db);
			dbsendmsg_list *m = static_cast<dbsendmsg_list*>(msg);
			DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::MSGLIST: queue size: %d"), m->msglist.size());
			for(auto &onemsg : m->msglist) {
				ProcessMessage(db, onemsg, ok, cache, th);
			}
			cache.EndTransaction(db);
			break;
		}
		case DBSM::FUNCTION: {
			cache.BeginTransaction(db);
			dbfunctionmsg *m = static_cast<dbfunctionmsg*>(msg);
			DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::FUNCTION: queue size: %d"), m->funclist.size());
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
		ProcessMessage(db, msgcont, ok, cache, this);
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
		DBLogMsgFormat(LOGT::DBERR, wxT("dbconn::GenericDBSelTweetMsgHandler could not find handler for %p."), msg.get());
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
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::HandleDBSelTweetMsg start"));

	if(msg.flags & DBSTMF::CLEARNOUPDF) dbc_flags |= DBCF::REPLY_CLEARNOUPDF;

	if(msg.flags & DBSTMF::NET_FALLBACK) {
		dbseltweetmsg_netfallback *fmsg = dynamic_cast<dbseltweetmsg_netfallback *>(&msg);
		std::shared_ptr<taccount> acc;
		if(fmsg) {
			GetAccByDBIndex(fmsg->dbindex, acc);
		}
		container::set<uint64_t> missing_id_set = msg.id_set;
		for(auto &it : msg.data) {
			missing_id_set.erase(it.id);
		}
		for(auto &it : missing_id_set) {
			tweet_ptr t = ad.GetTweetById(it);

			if(!t->text.size() && !(t->lflags & TLF::BEINGLOADEDOVERNET)) {	//tweet still not loaded at all

				t->lflags &= ~TLF::BEINGLOADEDFROMDB;

				std::shared_ptr<taccount> curacc = acc;
				bool result = CheckLoadSingleTweet(t, curacc);
				if(result) {
					LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::HandleDBSelTweetMsg falling back to network for tweet: id: %" wxLongLongFmtSpec "d, account: %s."),
							t->id, curacc->dispname.c_str());
				}
				else {
					LogMsgFormat(LOGT::DBERR, wxT("dbconn::HandleDBSelTweetMsg could not fall back to network for tweet: id:%" wxLongLongFmtSpec "d, no usable account."), t->id);
				}
			}
		}
	}

	for(dbrettweetdata &dt : msg.data) {
		#if DB_COPIOUS_LOGGING
			DBLogMsgFormat(LOGT::DBTRACE, wxT("dbconn::HandleDBSelTweetMsg got tweet: id:%" wxLongLongFmtSpec "d, statjson: %s, dynjson: %s"), dt.id, wxstrstd(dt.statjson).c_str(), wxstrstd(dt.dynjson).c_str());
		#endif
		ad.unloaded_db_tweet_ids.erase(dt.id);
		tweet_ptr t = ad.GetTweetById(dt.id);
		t->lflags |= TLF::SAVED_IN_DB;
		t->lflags |= TLF::LOADED_FROM_DB;

		rapidjson::Document dc;
		if(dt.statjson && !dc.ParseInsitu<0>(dt.statjson).HasParseError() && dc.IsObject()) {
			genjsonparser::ParseTweetStatics(dc, t, 0);
		}
		else {
			DBLogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, wxT("dbconn::HandleDBSelTweetMsg static JSON parse error: malformed or missing, tweet id: %" wxLongLongFmtSpec "d"), dt.id);
		}

		if(dt.dynjson && !dc.ParseInsitu<0>(dt.dynjson).HasParseError() && dc.IsObject()) {
			genjsonparser::ParseTweetDyn(dc, t);
		}
		else {
			DBLogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, wxT("dbconn::HandleDBSelTweetMsg dyn JSON parse error: malformed or missing, tweet id: s%" wxLongLongFmtSpec "d"), dt.id);
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
				GenericMarkPending(t, res, wxT("dbconn::HandleDBSelTweetMsg"));
				dbc.dbc_flags |= DBCF::REPLY_CHECKPENDINGS;
			}
		}
	}
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::HandleDBSelTweetMsg end"));
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
		LogMsgFormat(LOGT::DBERR, wxT("dbconn::SendMessage(): Could not communicate with DB thread"));
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
				LogMsgFormat(LOGT::DBERR, wxT("dbconn::SendMessage(): Could not communicate with DB thread: %d, %s"), err, wxstrstd(strerror(err)).c_str());
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

	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::Init(): About to initialise database connection"));

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

		LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::Init(): table_count: %d"), table_count);

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
			SyncDoUpdates(syncdb);
		}
	}

	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::Init(): About to read in state from database"));

	AccountSync(syncdb);
	ReadAllCFGIn(syncdb, gc, alist);
	SyncReadInAllUsers(syncdb);
	SyncReadInRBFSs(syncdb);
	SyncReadInAllMediaEntities(syncdb);
	SyncReadInCIDSLists(syncdb);
	SyncReadInTpanels(syncdb);
	SyncReadInWindowLayout(syncdb);
	SyncReadInAllTweetIDs(syncdb);

	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::Init(): State read in from database complete, about to create database thread"));

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
	LogMsgFormat(LOGT::DBTRACE | LOGT::THREADTRACE, wxT("dbconn::Init(): Created database thread: %d"), th->GetId());

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

	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, wxT("dbconn::DeInit: About to terminate database thread and write back state"));

	SendMessage(std::unique_ptr<dbsendmsg>(new dbsendmsg(DBSM::QUIT)));

	#ifdef __WINDOWS__
	CloseHandle(iocp);
	#else
	close(pipefd);
	#endif
	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, wxT("dbconn::DeInit(): Waiting for database thread to terminate"));
	th->Wait();
	syncdb = th->db;
	delete th;

	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, wxT("dbconn::DeInit(): Database thread terminated"));

	if(!gc.readonlymode) {
		cache.BeginTransaction(syncdb);
		WriteAllCFGOut(syncdb, gc, alist);
		SyncWriteBackAllUsers(syncdb);
		SyncWriteBackAccountIdLists(syncdb);
		SyncWriteOutRBFSs(syncdb);
		SyncWriteBackCIDSLists(syncdb);
		SyncWriteBackWindowLayout(syncdb);
		SyncWriteBackTpanels(syncdb);
	}
	SyncPurgeMediaEntities(syncdb); //this does a dry-run in read-only mode
	if(!gc.readonlymode) {
		cache.EndTransaction(syncdb);
	}

	sqlite3_close(syncdb);

	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, wxT("dbconn::DeInit(): State write back to database complete, database connection closed."));
}

void dbconn::AsyncWriteBackState() {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::AsyncWriteBackState start"));
	std::unique_ptr<dbfunctionmsg> msg(new dbfunctionmsg);
	auto cfg_closure = WriteAllCFGOutClosure(gc, alist, true);
	msg->funclist.emplace_back([cfg_closure](sqlite3 *db, bool &ok, dbpscache &cache) {
		DBLogMsg(LOGT::DBTRACE, wxT("dbconn::AsyncWriteBackState: CFG write start"));
		DBWriteConfig twfc(db);
		cfg_closure(twfc);
		DBLogMsg(LOGT::DBTRACE, wxT("dbconn::AsyncWriteBackState: CFG write end"));
	});
	AsyncWriteBackAllUsers(*msg);
	AsyncWriteBackAccountIdLists(*msg);
	AsyncWriteOutRBFSs(*msg);
	AsyncWriteBackCIDSLists(*msg);
	AsyncWriteBackTpanels(*msg);

	SendMessage(std::move(msg));
	LogMsg(LOGT::DBTRACE, wxT("dbconn::AsyncWriteBackState end, message sent to DB thread"));
}

void dbconn::SyncDoUpdates(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::DoUpdates start"));

	unsigned int current_db_version = 0;

	sqlite3_stmt *getstmt = cache.GetStmt(adb, DBPSC_SELSTATICSETTING);
	sqlite3_bind_text(getstmt, 1, "dbversion", -1, SQLITE_STATIC);
	DBRowExec(adb, getstmt, [&](sqlite3_stmt *stmt) {
		current_db_version = (unsigned int) sqlite3_column_int64(stmt, 0);
	}, "dbconn::DoUpdates (get DB version)");

	if(current_db_version < db_version) {
		LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::DoUpdates updating from %u to %u"), current_db_version, db_version);
		for(unsigned int i = current_db_version; i < db_version; i++) {
			int res = sqlite3_exec(adb, update_sql[i], 0, 0, 0);
			if(res != SQLITE_OK) {
				LogMsgFormat(LOGT::DBERR, wxT("dbconn::DoUpdates %u got error: %d (%s)"), i, res, wxstrstd(sqlite3_errmsg(adb)).c_str());
			}
		}
		SyncWriteDBVersion(adb);
	}
	else if(current_db_version > db_version) {
		LogMsgFormat(LOGT::DBERR, wxT("dbconn::DoUpdates current DB version %u > %u"), current_db_version, db_version);
	}

	LogMsg(LOGT::DBTRACE, wxT("dbconn::DoUpdates end"));
}

void dbconn::SyncWriteDBVersion(sqlite3 *adb) {
	sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSSTATICSETTING);
	sqlite3_bind_text(stmt, 1, "dbversion", -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, db_version);
	DBExec(adb, stmt, "dbconn::SyncWriteDBVersion");
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
	msg->mentionindex = settocompressedblob(u->mention_index, msg->mentionindex_size);
	u->lastupdate_wrotetodb = u->lastupdate;
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
	LogMsg(LOGT::DBTRACE, wxT("dbconn::AccountSync start"));

	unsigned int total = 0;
	DBRowExecNoError(adb, "SELECT id, name, tweetids, dmids, userid, dispname FROM acc;", [&](sqlite3_stmt *getstmt) {
		unsigned int id = (unsigned int) sqlite3_column_int(getstmt, 0);
		wxString name = wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 1));

		std::shared_ptr<taccount> ta(new(taccount));
		ta->name = name;
		ta->dbindex = id;
		alist.push_back(ta);

		setfromcompressedblob([&](uint64_t &tid) { ta->tweet_ids.insert(tid); }, getstmt, 2);
		setfromcompressedblob([&](uint64_t &tid) { ta->dm_ids.insert(tid); }, getstmt, 3);
		total += ta->tweet_ids.size();
		total += ta->dm_ids.size();

		uint64_t userid = (uint64_t) sqlite3_column_int64(getstmt, 4);
		ta->usercont = ad.GetUserContainerById(userid);
		ta->dispname = wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 5));

		LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::AccountSync: Found account: dbindex: %d, name: %s, tweet IDs: %u, DM IDs: %u"),
				id, name.c_str(), ta->tweet_ids.size(), ta->dm_ids.size());
	});
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::AccountSync end, total: %u IDs"), total);
}

void dbconn::SyncReadInAllTweetIDs(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllTweetIDs start"));
	DBRowExec(adb, "SELECT id FROM tweets;", [&](sqlite3_stmt *getstmt) {
		uint64_t id = (uint64_t) sqlite3_column_int64(getstmt, 0);
		ad.unloaded_db_tweet_ids.insert(id);
	}, "dbconn::SyncReadInAllTweetIDs");
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllTweetIDs end, read %u"), ad.unloaded_db_tweet_ids.size());
}

void dbconn::SyncReadInCIDSLists(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInCIDSLists start"));
	const char getcidslist[] = "SELECT value FROM settings WHERE name == ?;";
	sqlite3_stmt *getstmt = 0;
	sqlite3_prepare_v2(adb, getcidslist, sizeof(getcidslist), &getstmt, 0);

	unsigned int total = 0;
	auto doonelist = [&](std::string name, tweetidset &tlist) {
		sqlite3_bind_text(getstmt, 1, name.c_str(), name.size(), SQLITE_STATIC);
		DBRowExecStmt(adb, getstmt, [&](sqlite3_stmt *stmt) {
			setfromcompressedblob([&](uint64_t &id) { tlist.insert(id); }, getstmt, 0);
			total += tlist.size();
		}, "dbconn::SyncReadInCIDSLists");
		sqlite3_reset(getstmt);
	};

	cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
		doonelist(name, ad.cids.*ptr);
	});

	sqlite3_finalize(getstmt);
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInCIDSLists end, total: %u IDs"), total);
}

namespace {
	template <typename T> struct WriteBackOutputter {
		std::shared_ptr<T> data;

		WriteBackOutputter(std::shared_ptr<T> d) : data(d) { }
		template <typename F> void operator()(F func) {
			for(auto& item : *data) {
				func(item);
			}
		}
	};

	template <typename T> WriteBackOutputter<T> MakeWriteBackOutputter(std::shared_ptr<T> d) {
		return WriteBackOutputter<T>(std::move(d));
	}

	//Where T looks like WriteBackCIDSLists et al.
	template <typename T> void DoGenericSyncWriteBack(sqlite3 *adb, dbpscache &cache, T obj, wxString funcname) {
		obj.dbexec(adb, cache, funcname, false, obj);
	};

	//Where T looks like WriteBackCIDSLists et al.
	template <typename T> void DoGenericAsyncWriteBack(dbfunctionmsg &msg, T obj, wxString funcname) {
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
			unsigned char *index;
			size_t index_size;
			size_t list_size;
		};

		//Where F is a functor of the form void(const itemdata &)
		//F must free() itemdata::index
		template <typename F> void operator()(F func) const {
			cached_id_sets::IterateLists([&](const char *name, const tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
				const tweetidset &tlist = ad.cids.*ptr;
				size_t index_size;
				unsigned char *index = settocompressedblob(tlist, index_size);
				func(itemdata { name, index, index_size, tlist.size() });
			});
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, wxString funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s start"), funcname.c_str());
			cache.BeginTransaction(adb);
			sqlite3_stmt *setstmt = cache.GetStmt(adb, DBPSC_INSSETTING);

			unsigned int total = 0;
			getfunc([&](const itemdata &data) {
				sqlite3_bind_text(setstmt, 1, globstr.c_str(), globstr.size(), SQLITE_STATIC);
				sqlite3_bind_text(setstmt, 2, data.name, -1, SQLITE_STATIC);
				sqlite3_bind_blob(setstmt, 3, data.index, data.index_size, &free);
				int res = sqlite3_step(setstmt);
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, wxT("%s got error: %d (%s), for set: %s"),
						funcname.c_str(), res, wxstrstd(sqlite3_errmsg(adb)).c_str(), wxstrstd(data.name).c_str());
				}
				sqlite3_reset(setstmt);
				total += data.list_size;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s end, total: %u IDs"), funcname.c_str(), total);
		}
	};
};

void dbconn::SyncWriteBackCIDSLists(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackCIDSLists(), wxT("dbconn::SyncWriteBackCIDSLists"));
}

void dbconn::AsyncWriteBackCIDSLists(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackCIDSLists(), wxT("dbconn::AsyncWriteBackCIDSLists"));
}

namespace {
	struct WriteBackAccountIdLists {
		struct itemdata {
			std::string dispname;
			unsigned int dbindex;

			unsigned char *tweet_blob;
			size_t tweet_blob_size;
			size_t tweet_count;

			unsigned char *dm_blob;
			size_t dm_blob_size;
			size_t dm_count;
		};

		//Where F is a functor of the form void(const itemdata &)
		//F must free() itemdata::index
		template <typename F> void operator()(F func) const {
			for(auto &it : alist) {
				itemdata data;

				data.tweet_count = it->tweet_ids.size();
				data.tweet_blob = settocompressedblob(it->tweet_ids, data.tweet_blob_size);

				data.dm_count = it->dm_ids.size();
				data.dm_blob = settocompressedblob(it->dm_ids, data.dm_blob_size);

				data.dispname = stdstrwx(it->dispname);
				data.dbindex = it->dbindex;

				func(std::move(data));
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, wxString funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s start"), funcname.c_str());
			cache.BeginTransaction(adb);
			sqlite3_stmt *setstmt = cache.GetStmt(adb, DBPSC_UPDATEACCIDLISTS);

			unsigned int total = 0;
			getfunc([&](const itemdata &data) {
				sqlite3_bind_blob(setstmt, 1, data.tweet_blob, data.tweet_blob_size, &free);
				sqlite3_bind_blob(setstmt, 2, data.dm_blob, data.dm_blob_size, &free);
				sqlite3_bind_text(setstmt, 3, data.dispname.c_str(), data.dispname.size(), SQLITE_TRANSIENT);
				sqlite3_bind_int(setstmt, 4, data.dbindex);

				total += data.tweet_count + data.dm_count;

				int res = sqlite3_step(setstmt);
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, wxT("%s got error: %d (%s) for user dbindex: %d, name: %s"),
							funcname.c_str(), res, wxstrstd(sqlite3_errmsg(adb)).c_str(), data.dbindex, wxstrstd(data.dispname).c_str());
				}
				else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s inserted account: dbindex: %d, name: %s, tweet IDs: %u, DM IDs: %u"),
							funcname.c_str(), data.dbindex, wxstrstd(data.dispname).c_str(), data.tweet_count, data.dm_count);
				}
				sqlite3_reset(setstmt);
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s end, total: %u IDs"), funcname.c_str(), total);
		}
	};
};

void dbconn::SyncWriteBackAccountIdLists(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackAccountIdLists(), wxT("dbconn::SyncWriteBackAccountIdLists"));
}

void dbconn::AsyncWriteBackAccountIdLists(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackAccountIdLists(), wxT("dbconn::AsyncWriteBackAccountIdLists"));
}

namespace {
	struct WriteBackAllUsers {
		size_t allusercount;

		WriteBackAllUsers(size_t a) : allusercount(a) { }

		struct itemdata {
			uint64_t id;
			unsigned int dbindex;

			unsigned char *json_blob;
			size_t json_blob_size;

			unsigned char *profimg_blob;
			size_t profimg_blob_size;

			time_t createtime;
			uint64_t lastupdate;
			shb_iptr cached_profile_img_sha1;

			unsigned char *mention_blob;
			size_t mention_blob_size;
		};

		//Where F is a functor of the form void(const itemdata &)
		//F must free() itemdata blobs
		template <typename F> void operator()(F func) const {
			for(auto &it : ad.userconts) {
				userdatacontainer *u = &(it.second);
				if(u->lastupdate == u->lastupdate_wrotetodb) continue;    //this user is already in the database and does not need updating
				if(u->user.screen_name.empty()) continue;                 //don't bother saving empty user stubs

				u->lastupdate_wrotetodb = u->lastupdate;

				itemdata data;

				data.id = it.first;
				data.json_blob = DoCompress(u->mkjson(), data.json_blob_size, 'J');
				data.profimg_blob = DoCompress(u->cached_profile_img_url, data.profimg_blob_size, 'P');

				data.createtime = u->user.createtime;
				data.lastupdate = u->lastupdate;
				data.cached_profile_img_sha1 = u->cached_profile_img_sha1;

				data.mention_blob = settocompressedblob(u->mention_index, data.mention_blob_size);

				func(std::move(data));
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, wxString funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s start"), funcname.c_str());

			DBWriteConfig dbwc(adb);
			dbwc.SetDBIndexDB();
			dbwc.WriteInt64("UserCountHint", allusercount);

			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSUSER);
			size_t user_count = 0;
			getfunc([&](const itemdata &data) {
				user_count++;
				sqlite3_bind_int64(stmt, 1, (sqlite3_int64) data.id);
				sqlite3_bind_blob(stmt, 2, data.json_blob, data.json_blob_size, &free);
				sqlite3_bind_blob(stmt, 3, data.profimg_blob, data.profimg_blob_size, &free);
				sqlite3_bind_int64(stmt, 4, (sqlite3_int64) data.createtime);
				sqlite3_bind_int64(stmt, 5, (sqlite3_int64) data.lastupdate);
				if(data.cached_profile_img_sha1) {
					sqlite3_bind_blob(stmt, 6, data.cached_profile_img_sha1->hash_sha1, sizeof(data.cached_profile_img_sha1->hash_sha1), SQLITE_TRANSIENT);
				}
				else {
					sqlite3_bind_null(stmt, 6);
				}
				sqlite3_bind_blob(stmt, 7, data.mention_blob, data.mention_blob_size, &free);

				int res = sqlite3_step(stmt);
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, wxT("%s got error: %d (%s) for user id: %" wxLongLongFmtSpec "u"),
							funcname.c_str(),res, wxstrstd(sqlite3_errmsg(adb)).c_str(), data.id);
				}
				else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s inserted user id: %" wxLongLongFmtSpec "u"), funcname.c_str(), data.id);
				}
				sqlite3_reset(stmt);
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s end, wrote back %u of %u users"), funcname.c_str(), user_count, allusercount);
		}
	};
};

void dbconn::SyncWriteBackAllUsers(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackAllUsers(ad.userconts.size()), wxT("dbconn::SyncWriteBackAllUsers"));
}

void dbconn::AsyncWriteBackAllUsers(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackAllUsers(ad.userconts.size()), wxT("dbconn::AsyncWriteBackAllUsers"));
}

void dbconn::SyncReadInAllUsers(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllUsers start"));
	const char sql[] = "SELECT id, json, cachedprofimgurl, createtimestamp, lastupdatetimestamp, cachedprofileimgchecksum, mentionindex FROM users;";
	sqlite3_stmt *stmt = 0;
	sqlite3_prepare_v2(adb, sql, sizeof(sql), &stmt, 0);

	uint64_t usercounthint = 0;
	DBReadConfig dbrc(adb);
	dbrc.SetDBIndexDB();
	dbrc.ReadUInt64("UserCountHint", &usercounthint, 0);

	if(usercounthint) {
		LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllUsers: got user count hint: %" wxLongLongFmtSpec "u."), usercounthint);
		ad.userconts.reserve(usercounthint);
	}

	size_t user_read_count = 0;
	do {
		int res = sqlite3_step(stmt);
		if(res == SQLITE_ROW) {
			user_read_count++;
			uint64_t id = (uint64_t) sqlite3_column_int64(stmt, 0);
			udc_ptr ref = ad.GetUserContainerById(id);
			userdatacontainer &u = *ref.get();
			rapidjson::Document dc;
			char *json = column_get_compressed_and_parse(stmt, 1, dc);
			if(dc.IsObject()) genjsonparser::ParseUserContents(dc, u.user);
			size_t profimg_size;
			char *profimg = column_get_compressed(stmt, 2, profimg_size);
			u.cached_profile_img_url.assign(profimg, profimg_size);
			if(u.user.profile_img_url.empty()) u.user.profile_img_url.assign(profimg, profimg_size);
			u.user.createtime = (time_t) sqlite3_column_int64(stmt, 3);
			u.lastupdate = (uint64_t) sqlite3_column_int64(stmt, 4);
			u.lastupdate_wrotetodb = u.lastupdate;
			const char *hash = (const char*) sqlite3_column_blob(stmt, 5);
			int hashsize = sqlite3_column_bytes(stmt, 5);
			if(hashsize == sizeof(sha1_hash_block::hash_sha1)) {
				std::shared_ptr<sha1_hash_block> hashptr = std::make_shared<sha1_hash_block>();
				memcpy(hashptr->hash_sha1, hash, sizeof(sha1_hash_block::hash_sha1));
				u.cached_profile_img_sha1 = std::move(hashptr);
			}
			else {
				u.cached_profile_img_sha1.reset();
				if(profimg_size) LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInAllUsers user id: %" wxLongLongFmtSpec "d, has invalid profile image hash length: %d"), (sqlite3_int64) id, hashsize);
			}
			if(json) free(json);
			if(profimg) free(profimg);
			setfromcompressedblob([&](uint64_t &tid) { u.mention_index.push_back(tid); }, stmt, 6);
			#if DB_COPIOUS_LOGGING
				LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllUsers retrieved user id: %" wxLongLongFmtSpec "d"), (sqlite3_int64) id);
			#endif
		}
		else if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInAllUsers got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		else break;
	} while(true);
	sqlite3_finalize(stmt);
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllUsers end, read in %u users (%u total)"), user_read_count, ad.userconts.size());
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
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, wxString funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s start"), funcname.c_str());

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
					SLogMsgFormat(LOGT::DBERR, TSLogging, wxT("%s got error: %d (%s)"), funcname.c_str(), res, wxstrstd(sqlite3_errmsg(adb)).c_str());
				}
				else {
					SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s inserted pending RBFS"), funcname.c_str());
				}
				sqlite3_reset(stmt);
				write_count++;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s end, wrote %u"), funcname.c_str(), write_count);
		}
	};
};

void dbconn::SyncWriteOutRBFSs(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackRBFSs(), wxT("dbconn::SyncWriteOutRBFSs"));
}

void dbconn::AsyncWriteOutRBFSs(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackRBFSs(), wxT("dbconn::AsyncWriteOutRBFSs"));
}

void dbconn::SyncReadInRBFSs(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInRBFSs start"));

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
		if(found) { LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInRBFSs retrieved RBFS")); }
		else { LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInRBFSs retrieved RBFS with no associated account or bad type, ignoring")); }
	}, "dbconn::SyncReadInRBFSs");
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInRBFSs end, read in %u"), read_count);
}

void dbconn::SyncReadInAllMediaEntities(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllMediaEntities start"));

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
		size_t outsize;
		char *url = column_get_compressed(stmt, 2, outsize);
		if(url) {
			me.media_url.assign(url, outsize);
			ad.img_media_map[me.media_url] = meptr;
		}
		free(url);
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
			LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllMediaEntities retrieved media entity %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d"), id.m_id, id.t_id);
		#endif
	}, "dbconn::SyncReadInAllMediaEntities");

	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllMediaEntities end, read in %u, cached: thumb: %u, full: %u"), read_count, thumb_count, full_count);
}

void dbconn::SyncReadInWindowLayout(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInWindowLayout start"));
	const char mfsql[] = "SELECT mainframeindex, x, y, w, h, maximised FROM mainframewins ORDER BY mainframeindex ASC;";
	sqlite3_stmt *mfstmt = 0;
	sqlite3_prepare_v2(adb, mfsql, sizeof(mfsql), &mfstmt, 0);

	do {
		int res = sqlite3_step(mfstmt);
		if(res == SQLITE_ROW) {
			ad.mflayout.emplace_back();
			mf_layout_desc &mfld = ad.mflayout.back();
			mfld.mainframeindex = (unsigned int) sqlite3_column_int(mfstmt, 0);
			mfld.pos.x = sqlite3_column_int(mfstmt, 1);
			mfld.pos.y = sqlite3_column_int(mfstmt, 2);
			mfld.size.SetWidth(sqlite3_column_int(mfstmt, 3));
			mfld.size.SetHeight(sqlite3_column_int(mfstmt, 4));
			mfld.maximised = (bool) sqlite3_column_int(mfstmt, 5);
		}
		else if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInWindowLayout (mainframewins) got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		else break;
	} while(true);
	sqlite3_finalize(mfstmt);


	const char sql[] = "SELECT mainframeindex, splitindex, tabindex, name, dispname, flags, rowid FROM tpanelwins ORDER BY mainframeindex ASC, splitindex ASC, tabindex ASC;";
	sqlite3_stmt *stmt = 0;
	int res = sqlite3_prepare_v2(adb, sql, sizeof(sql), &stmt, 0);

	const char sql2[] = "SELECT accid, autoflags FROM tpanelwinautos WHERE tpw == ?;";
	sqlite3_stmt *stmt2=0;
	int res2 = sqlite3_prepare_v2(adb, sql2, sizeof(sql2), &stmt2, 0);

	if(res != SQLITE_OK || res2 != SQLITE_OK) {
		LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInWindowLayout sqlite3_prepare_v2 failed"));
		return;
	}

	do {
		int res3 = sqlite3_step(stmt);
		if(res3 == SQLITE_ROW) {
			ad.twinlayout.emplace_back();
			twin_layout_desc &twld = ad.twinlayout.back();
			twld.mainframeindex = (unsigned int) sqlite3_column_int(stmt, 0);
			twld.splitindex = (unsigned int) sqlite3_column_int(stmt, 1);
			twld.tabindex = (unsigned int) sqlite3_column_int(stmt, 2);
			twld.name = (const char *) sqlite3_column_text(stmt, 3);
			twld.dispname = (const char *) sqlite3_column_text(stmt, 4);
			twld.flags = static_cast<TPF>(sqlite3_column_int(stmt, 5));
			sqlite3_int64 rowid = sqlite3_column_int(stmt, 6);

			sqlite3_bind_int(stmt2, 1, rowid);
			do {
				int res4 = sqlite3_step(stmt2);
				if(res4 == SQLITE_ROW) {
					std::shared_ptr<taccount> acc;
					int accid = (int) sqlite3_column_int(stmt2, 0);
					if(accid > 0) {
						for(auto &it : alist) {
							if(it->dbindex == (unsigned int) accid) {
								acc = it;
								break;
							}
						}
						if(!acc) continue;
					}
					else {
						acc.reset();
					}
					twld.tpautos.emplace_back();
					twld.tpautos.back().acc = acc;
					twld.tpautos.back().autoflags = static_cast<TPF>(sqlite3_column_int(stmt2, 1));
				}
				else if(res4 != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInWindowLayout (tpanelwinautos) got error: %d (%s)"),
						res4, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
				else break;
			}
			while(true);
			sqlite3_reset(stmt2);
		}
		else if(res3 != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInWindowLayout (tpanelwins) got error: %d (%s)"),
				res3, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		else break;
	} while(true);
	sqlite3_finalize(stmt);
	sqlite3_finalize(stmt2);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInWindowLayout end"));
}

void dbconn::SyncWriteBackWindowLayout(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackWindowLayout start"));
	cache.BeginTransaction(adb);
	sqlite3_exec(adb, "DELETE FROM mainframewins", 0, 0, 0);
	const char mfsql[] = "INSERT INTO mainframewins (mainframeindex, x, y, w, h, maximised) VALUES (?, ?, ?, ?, ?, ?);";
	sqlite3_stmt *mfstmt = 0;
	sqlite3_prepare_v2(adb, mfsql, sizeof(mfsql), &mfstmt, 0);

	for(auto &mfld : ad.mflayout) {
		sqlite3_bind_int(mfstmt, 1, mfld.mainframeindex);
		sqlite3_bind_int(mfstmt, 2, mfld.pos.x);
		sqlite3_bind_int(mfstmt, 3, mfld.pos.y);
		sqlite3_bind_int(mfstmt, 4, mfld.size.GetWidth());
		sqlite3_bind_int(mfstmt, 5, mfld.size.GetHeight());
		sqlite3_bind_int(mfstmt, 6, mfld.maximised);

		int res = sqlite3_step(mfstmt);
		if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncWriteOutWindowLayout (mainframewins) got error: %d (%s)"),
				res, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		sqlite3_reset(mfstmt);
	}
	sqlite3_finalize(mfstmt);


	sqlite3_exec(adb, "DELETE FROM tpanelwins", 0, 0, 0);
	sqlite3_exec(adb, "DELETE FROM tpanelwinautos", 0, 0, 0);
	const char sql[] = "INSERT INTO tpanelwins (mainframeindex, splitindex, tabindex, name, dispname, flags) VALUES (?, ?, ?, ?, ?, ?);";
	sqlite3_stmt *stmt = 0;
	sqlite3_prepare_v2(adb, sql, sizeof(sql), &stmt, 0);
	const char sql2[] = "INSERT INTO tpanelwinautos (tpw, accid, autoflags) VALUES (?, ?, ?);";
	sqlite3_stmt *stmt2 = 0;
	sqlite3_prepare_v2(adb, sql2, sizeof(sql2), &stmt2, 0);

	for(auto &twld : ad.twinlayout) {
		sqlite3_bind_int(stmt, 1, twld.mainframeindex);
		sqlite3_bind_int(stmt, 2, twld.splitindex);
		sqlite3_bind_int(stmt, 3, twld.tabindex);
		sqlite3_bind_text(stmt, 4, twld.name.c_str(), twld.name.size(), SQLITE_STATIC);
		sqlite3_bind_text(stmt, 5, twld.dispname.c_str(), twld.dispname.size(), SQLITE_STATIC);
		sqlite3_bind_int(stmt, 6, flag_unwrap<TPF>(twld.flags));

		int res = sqlite3_step(stmt);
		if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncWriteOutWindowLayout (tpanelwins) got error: %d (%s)"),
				res, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		sqlite3_reset(stmt);
		sqlite3_int64 rowid = sqlite3_last_insert_rowid(adb);

		for(auto &it : twld.tpautos) {
			sqlite3_bind_int(stmt2, 1, rowid);
			if(it.acc) sqlite3_bind_int(stmt2, 2, it.acc->dbindex);
			else sqlite3_bind_int(stmt2, 2, -1);
			sqlite3_bind_int(stmt2, 3, flag_unwrap<TPF>(it.autoflags));
			int res2 = sqlite3_step(stmt2);
			if(res2 != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncWriteOutWindowLayout (tpanelwinautos) got error: %d (%s)"),
					res2, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
			sqlite3_reset(stmt2);
		}
	}
	sqlite3_finalize(stmt);
	sqlite3_finalize(stmt2);
	cache.EndTransaction(adb);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackWindowLayout end"));
}

void dbconn::SyncReadInTpanels(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInTpanels start"));
	const char sql[] = "SELECT name, dispname, flags, ids FROM tpanels;";
	sqlite3_stmt *stmt = 0;
	int res = sqlite3_prepare_v2(adb, sql, sizeof(sql), &stmt, 0);
	if(res != SQLITE_OK) {
		LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInTpanels sqlite3_prepare_v2 failed"));
		return;
	}

	unsigned int read_count = 0;
	unsigned int id_count = 0;
	do {
		int res2 = sqlite3_step(stmt);
		if(res2 == SQLITE_ROW) {
			std::string name = (const char *) sqlite3_column_text(stmt, 0);
			std::string dispname = (const char *) sqlite3_column_text(stmt, 1);
			flagwrapper<TPF> flags = static_cast<TPF>(sqlite3_column_int(stmt, 2));
			std::shared_ptr<tpanel> tp = tpanel::MkTPanel(name, dispname, flags);
			setfromcompressedblob([&](uint64_t &id) { tp->tweetlist.insert(id); }, stmt, 3);
			id_count += tp->tweetlist.size();
			tp->RecalculateCIDS();
			read_count++;
		}
		else if(res2 != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInTpanels got error: %d (%s)"), res2, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		else break;
	} while(true);
	sqlite3_finalize(stmt);
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInTpanels end, read in %u, IDs: %u"), read_count, id_count);
}

namespace {
	struct WriteBackTpanels {
		struct itemdata {
			std::string name;
			std::string dispname;
			flagwrapper<TPF> flags;

			unsigned char *tweetlist_blob;
			size_t tweetlist_blob_size;
			size_t tweetlist_count;
		};

		//Where F is a functor of the form void(const itemdata &)
		//F must free() itemdata::tweetlist_blob
		template <typename F> void operator()(F func) const {
			for(auto &it : ad.tpanels) {
				tpanel &tp = *(it.second);
				if(tp.flags & TPF::SAVETODB) {
					itemdata data;
					data.name = tp.name;
					data.dispname = tp.dispname;
					data.flags = tp.flags;
					data.tweetlist_blob = settocompressedblob(tp.tweetlist, data.tweetlist_blob_size);
					data.tweetlist_count = tp.tweetlist.size();
					func(std::move(data));
				}
			}
		};

		//Where F is a functor with an operator() as above
		template <typename F> void dbexec(sqlite3 *adb, dbpscache &cache, wxString funcname, bool TSLogging, F getfunc) const {
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s start"), funcname.c_str());

			cache.BeginTransaction(adb);
			sqlite3_exec(adb, "DELETE FROM tpanels", 0, 0, 0);
			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSTPANEL);

			unsigned int write_count = 0;
			unsigned int id_count = 0;
			getfunc([&](const itemdata &data) {
				sqlite3_bind_text(stmt, 1, data.name.c_str(), data.name.size(), SQLITE_TRANSIENT);
				sqlite3_bind_text(stmt, 2, data.dispname.c_str(), data.dispname.size(), SQLITE_TRANSIENT);
				sqlite3_bind_int(stmt, 3, flag_unwrap<TPF>(data.flags));
				sqlite3_bind_blob(stmt, 4, data.tweetlist_blob, data.tweetlist_blob_size, &free);
				int res = sqlite3_step(stmt);
				if(res != SQLITE_DONE) {
					SLogMsgFormat(LOGT::DBERR, TSLogging, wxT("%s got error: %d (%s)"), funcname.c_str(), res, wxstrstd(sqlite3_errmsg(adb)).c_str());
				}
				sqlite3_reset(stmt);
				write_count++;
				id_count += data.tweetlist_count;
			});

			cache.EndTransaction(adb);
			SLogMsgFormat(LOGT::DBTRACE, TSLogging, wxT("%s end, wrote %u, IDs: %u"), funcname.c_str(), write_count, id_count);
		}
	};
};

void dbconn::SyncWriteBackTpanels(sqlite3 *adb) {
	DoGenericSyncWriteBack(adb, cache, WriteBackTpanels(), wxT("dbconn::SyncWriteBackTpanels"));
}

void dbconn::AsyncWriteBackTpanels(dbfunctionmsg &msg) {
	DoGenericAsyncWriteBack(msg, WriteBackTpanels(), wxT("dbconn::AsyncWriteBackTpanels"));
}

void dbconn::SyncPurgeMediaEntities(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncPurgeMediaEntities start"));

	time_t last_purge = 0;
	sqlite3_stmt *getstmt = cache.GetStmt(adb, DBPSC_SELSTATICSETTING);
	sqlite3_bind_text(getstmt, 1, "lastmediacachepurge", -1, SQLITE_STATIC);
	DBRowExec(adb, getstmt, [&](sqlite3_stmt *stmt) {
		last_purge = (time_t) sqlite3_column_int64(stmt, 0);
	}, "dbconn::SyncPurgeMediaEntities (get last purged)");

	time_t now = time(nullptr);
	time_t delta = now - last_purge;

	const unsigned int day = 60 * 60 * 24;

	if(delta < day) {
		LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncPurgeMediaEntities end, last purged %" wxLongLongFmtSpec "ds ago, not checking"), (int64_t) delta);
	}
	else {
		unsigned int thumb_count = 0;
		unsigned int full_count = 0;

		std::deque<media_id_type> purge_list;

		DBBindRowExec(adb,
				"SELECT mid, tid, thumbchecksum, fullchecksum FROM mediacache WHERE ((flags == 0 OR flags IS NULL) AND lastusedtimestamp < ?);",
				[&](sqlite3_stmt *stmt) {
					sqlite3_bind_int64(stmt, 1, now - (day * gc.mediacachesavedays));
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
			cache.BeginTransaction(adb);

			DBRangeBindExec(adb, "DELETE FROM mediacache WHERE (mid == ? AND tid == ?);",
					purge_list.begin(), purge_list.end(),
					[&](sqlite3_stmt *stmt, media_id_type mid) {
						sqlite3_bind_int64(stmt, 1, mid.m_id);
						sqlite3_bind_int64(stmt, 2, mid.t_id);
					}, "dbconn::SyncPurgeMediaEntities (purge row)");

			sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSSTATICSETTING);
			sqlite3_bind_text(stmt, 1, "lastmediacachepurge", -1, SQLITE_STATIC);
			sqlite3_bind_int64(stmt, 2, now);
			DBExec(adb, stmt, "dbconn::SyncPurgeMediaEntities (write last purged)");

			cache.EndTransaction(adb);
		}

		LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncPurgeMediaEntities end, last purged %" wxLongLongFmtSpec "ds ago, purged %u, (thumb: %u, full: %u)"),
				(int64_t) delta, purge_list.size(), thumb_count, full_count);
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
	sqlite3_stmt *delall;
	sqlite3_prepare_v2(db, "DELETE FROM settings;", -1, &delall, 0);
	exec(delall);
	sqlite3_finalize(delall);
}
void DBWriteConfig::exec(sqlite3_stmt *wstmt) {
	int res = sqlite3_step(wstmt);
	if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBWriteConfig got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(db)).c_str()); }
	sqlite3_reset(wstmt);
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
		DBLogMsgFormat(LOGT::DBERR, wxT("DBReadConfig got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(db)).c_str());
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
