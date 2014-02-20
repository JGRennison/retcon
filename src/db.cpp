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
#ifdef __WINDOWS__
#include <windows.h>
#endif
#ifdef _GNU_SOURCE
#include <pthread.h>
#endif
#include <zlib.h>
#include <wx/msgdlg.h>

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
"CREATE TABLE IF NOT EXISTS tweets(id INTEGER PRIMARY KEY NOT NULL, statjson BLOB, dynjson BLOB, userid INTEGER, userrecipid INTEGER, flags INTEGER, timestamp INTEGER, medialist BLOB, rtid INTEGER);"
"CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY NOT NULL, json BLOB, cachedprofimgurl BLOB, createtimestamp INTEGER, lastupdatetimestamp INTEGER, cachedprofileimgchecksum BLOB, mentionindex BLOB);"
"CREATE TABLE IF NOT EXISTS acc(id INTEGER PRIMARY KEY NOT NULL, name TEXT, dispname TEXT, json BLOB, tweetids BLOB, dmids BLOB, userid INTEGER);"
"CREATE TABLE IF NOT EXISTS settings(accid BLOB, name TEXT, value BLOB, PRIMARY KEY (accid, name));"
"CREATE TABLE IF NOT EXISTS rbfspending(accid INTEGER, type INTEGER, startid INTEGER, endid INTEGER, maxleft INTEGER);"
"CREATE TABLE IF NOT EXISTS mediacache(mid INTEGER, tid INTEGER, url BLOB, fullchecksum BLOB, thumbchecksum BLOB, flags INTEGER, PRIMARY KEY (mid, tid));"
"CREATE TABLE IF NOT EXISTS tpanelwins(mainframeindex INTEGER, splitindex INTEGER, tabindex INTEGER, name TEXT, dispname TEXT, flags INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanelwinautos(tpw INTEGER, accid INTEGER, autoflags INTEGER);"
"CREATE TABLE IF NOT EXISTS mainframewins(mainframeindex INTEGER, x INTEGER, y INTEGER, w INTEGER, h INTEGER, maximised INTEGER);"
"CREATE TABLE IF NOT EXISTS tpanels(name TEXT, dispname TEXT, flags INTEGER, ids BLOB);"
"UPDATE OR IGNORE settings SET accid = 'G' WHERE (hex(accid) == '4700');"  //This is because previous versions of retcon accidentally inserted an embedded null when writing out the config
"INSERT OR REPLACE INTO settings(accid, name, value) VALUES ('G', 'dirtyflag', strftime('%s','now'));";

static const char *std_sql_stmts[DBPSC_NUM_STATEMENTS]={
	"INSERT OR REPLACE INTO tweets(id, statjson, dynjson, userid, userrecipid, flags, timestamp, medialist, rtid) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
	"UPDATE tweets SET dynjson = ?, flags = ? WHERE id == ?;",
	"BEGIN;",
	"COMMIT;",
	"INSERT OR REPLACE INTO users(id, json, cachedprofimgurl, createtimestamp, lastupdatetimestamp, cachedprofileimgchecksum, mentionindex) VALUES (?, ?, ?, ?, ?, ?, ?);",
	"INSERT INTO acc(name, dispname, userid) VALUES (?, ?, ?);",
	"UPDATE acc SET tweetids = ?, dmids = ?, dispname = ? WHERE id == ?;",
	"SELECT statjson, dynjson, userid, userrecipid, flags, timestamp, medialist, rtid FROM tweets WHERE id == ?;",
	"INSERT INTO rbfspending(accid, type, startid, endid, maxleft) VALUES (?, ?, ?, ?, ?);",
	"SELECT url, fullchecksum, thumbchecksum, flags FROM mediacache WHERE (mid == ? AND tid == ?);",
	"INSERT OR IGNORE INTO mediacache(mid, tid, url) VALUES (?, ?, ?);",
	"UPDATE OR IGNORE mediacache SET thumbchecksum = ? WHERE (mid == ? AND tid == ?);",
	"UPDATE OR IGNORE mediacache SET fullchecksum = ? WHERE (mid == ? AND tid == ?);",
	"UPDATE OR IGNORE mediacache SET flags = ? WHERE (mid == ? AND tid == ?);",
	"DELETE FROM acc WHERE id == ?;",
	"UPDATE tweets SET flags = ? | (flags & ?) WHERE id == ?;",
};

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
			deflateInit(&strm, 9);

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

	static size_t cumin = 0;
	static size_t cumout = 0;
	cumin += insize;
	cumout += sz;
	#if DB_COPIOUS_LOGGING
		DBLogMsgFormat(LOGT::ZLIBTRACE, wxT("compress: %d -> %d, cum: %f"), insize, sz, (double) cumout / (double) cumin);
	#endif

	return data;
}

static unsigned char *DoCompress(const std::string &in, size_t &sz, unsigned char tag = 'Z', bool *iscompressed = 0, const esctable *et = 0) {
	return DoCompress(in.data(), in.size(), sz, tag, iscompressed, et);
}

enum class BINDCF {
	NONTEXT		= 1<<0,
};
template<> struct enum_traits<BINDCF> { static constexpr bool flags = true; };

static void bind_compressed(sqlite3_stmt* stmt, int num, const std::string &in, unsigned char tag = 'Z', flagwrapper<BINDCF> flags = 0, const esctable *et = 0) {
	size_t comsize;
	bool iscompressed;
	unsigned char *com = DoCompress(in, comsize, tag, &iscompressed, et);
	if(iscompressed || flags & BINDCF::NONTEXT) sqlite3_bind_blob(stmt, num, com, comsize, &free);
	else sqlite3_bind_text(stmt, num, (const char *) com, comsize, &free);
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

static void ProcessMessage_SelTweet(sqlite3 *db, sqlite3_stmt *stmt, dbseltweetmsg *m, std::forward_list<dbrettweetdata> &recv_data, std::forward_list<media_id_type> &media_ids, uint64_t id) {
	sqlite3_bind_int64(stmt, 1, (sqlite3_int64) id);
	int res = sqlite3_step(stmt);
	uint64_t rtid = 0;
	if(res == SQLITE_ROW) {
		#if DB_COPIOUS_LOGGING
			DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::SELTWEET got id:%" wxLongLongFmtSpec "d"), (sqlite3_int64) id);
		#endif
		recv_data.emplace_front();
		dbrettweetdata &rd = recv_data.front();
		size_t outsize;
		rd.id = (id);
		rd.statjson = column_get_compressed(stmt, 0, outsize);
		rd.dynjson = column_get_compressed(stmt, 1, outsize);
		rd.user1 = (uint64_t) sqlite3_column_int64(stmt, 2);
		rd.user2 = (uint64_t) sqlite3_column_int64(stmt, 3);
		rd.flags = (uint64_t) sqlite3_column_int64(stmt, 4);
		rd.timestamp = (uint64_t) sqlite3_column_int64(stmt, 5);
		rd.rtid = rtid = (uint64_t) sqlite3_column_int64(stmt, 7);

		if(m->flags&DBSTMF::PULLMEDIA) {
			size_t mediaidarraysize;
			unsigned char *mediaidarray = (unsigned char*) column_get_compressed(stmt, 6, mediaidarraysize);
			mediaidarraysize &= ~15;
			for(unsigned int i = 0; i < mediaidarraysize; i += 16) {    //stored in big endian format
				media_ids.emplace_front();
				media_id_type &md = media_ids.front();
				for(unsigned int j = 0; j < 8; j++) md.m_id <<= 8, md.m_id |= mediaidarray[i + j];
				for(unsigned int j = 8; j < 15; j++) md.t_id <<= 8, md.t_id |= mediaidarray[i + j];
			}
			free(mediaidarray);
		}
	}
	else { DBLogMsgFormat((m->flags&DBSTMF::NO_ERR) ? LOGT::DBTRACE : LOGT::DBERR, wxT("DBSM::SELTWEET got error: %d (%s) for id: %" wxLongLongFmtSpec "d, net fallback flag: %d"),
			res, wxstrstd(sqlite3_errmsg(db)).c_str(), (sqlite3_int64) id, (m->flags & DBSTMF::NET_FALLBACK) ? 1 : 0); }
	sqlite3_reset(stmt);
	if(rtid) {
		ProcessMessage_SelTweet(db, stmt, m, recv_data, media_ids, rtid);
	}
}

static void ProcessMessage(sqlite3 *db, dbsendmsg *msg, bool &ok, dbpscache &cache, dbiothread *th) {
	switch(msg->type) {
		case DBSM::QUIT:
			ok = false;
			DBLogMsg(LOGT::DBTRACE, wxT("DBSM::QUIT"));
			break;
		case DBSM::INSERTTWEET: {
			if(gc.readonlymode) break;
			dbinserttweetmsg *m = (dbinserttweetmsg*) msg;
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_INSTWEET);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->id);
			bind_compressed(stmt, 2, m->statjson, 'J');
			bind_compressed(stmt, 3, m->dynjson, 'J', 0, dynjsontable);
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) m->user1);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) m->user2);
			sqlite3_bind_int64(stmt, 6, (sqlite3_int64) m->flags);
			sqlite3_bind_int64(stmt, 7, (sqlite3_int64) m->timestamp);
			sqlite3_bind_blob(stmt, 8, m->mediaindex, m->mediaindex_size, &free);
			sqlite3_bind_int64(stmt, 9, (sqlite3_int64) m->rtid);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::INSERTTWEET got error: %d (%s) for id:%" wxLongLongFmtSpec "d"),
					res, wxstrstd(sqlite3_errmsg(db)).c_str(), m->id); }
			else { DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::INSERTTWEET inserted row id:%" wxLongLongFmtSpec "d"), (sqlite3_int64) m->id); }
			sqlite3_reset(stmt);
			break;
		}
		case DBSM::UPDATETWEET: {
			if(gc.readonlymode) break;
			dbupdatetweetmsg *m = (dbupdatetweetmsg*) msg;
			sqlite3_stmt *stmt = cache.GetStmt(db, DBPSC_UPDTWEET);
			bind_compressed(stmt, 1, m->dynjson, 'J', 0, dynjsontable);
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
			dbseltweetmsg *m = (dbseltweetmsg*) msg;
			sqlite3_stmt *stmt=cache.GetStmt(db, DBPSC_SELTWEET);
			std::forward_list<dbrettweetdata> recv_data;
			std::forward_list<media_id_type> media_ids;
			for(auto it = m->id_set.cbegin(); it != m->id_set.cend(); ++it) {
				ProcessMessage_SelTweet(db, stmt, m, recv_data, media_ids, *it);
			}
			m->media_data.clear();
			if(!media_ids.empty()) {
				sqlite3_stmt *mstmt = cache.GetStmt(db, DBPSC_SELMEDIA);
				for(auto it = media_ids.cbegin(); it != media_ids.cend(); ++it) {
					sqlite3_bind_int64(mstmt, 1, (sqlite3_int64) it->m_id);
					sqlite3_bind_int64(mstmt, 2, (sqlite3_int64) it->t_id);
					int res = sqlite3_step(mstmt);
					if(res == SQLITE_ROW) {
						m->media_data.emplace_front();
						dbretmediadata &md = m->media_data.front();
						md.media_id = *it;
						md.flags = 0;
						size_t outsize;
						md.url = column_get_compressed(mstmt, 0, outsize);
						if(sqlite3_column_bytes(mstmt, 1) == sizeof(sha1_hash_block::hash_sha1)) {
							std::shared_ptr<sha1_hash_block> hashptr = std::make_shared<sha1_hash_block>();
							memcpy(hashptr->hash_sha1, sqlite3_column_blob(mstmt, 1), sizeof(sha1_hash_block::hash_sha1));
							md.full_img_sha1 = std::move(hashptr);
							md.flags |= MEF::LOAD_FULL;
						}
						if(sqlite3_column_bytes(mstmt, 2) == sizeof(sha1_hash_block::hash_sha1)) {
							std::shared_ptr<sha1_hash_block> hashptr = std::make_shared<sha1_hash_block>();
							memcpy(hashptr->hash_sha1, sqlite3_column_blob(mstmt, 2), sizeof(sha1_hash_block::hash_sha1));
							md.thumb_img_sha1 = std::move(hashptr);
							md.flags |= MEF::LOAD_THUMB;
						}
						md.flags |= static_cast<MEF>(sqlite3_column_int64(mstmt, 3));
					}
					else {
						DBLogMsgFormat(LOGT::DBERR, wxT("DBSM::SELTWEET (media load) got error: %d (%s) for id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d, net fallback flag: %d"),
								res, wxstrstd(sqlite3_errmsg(db)).c_str(), (sqlite3_int64) it->m_id, (sqlite3_int64) it->t_id, (m->flags&DBSTMF::NET_FALLBACK) ? 1 : 0);
					}
					sqlite3_reset(mstmt);
				}
			}
			if(!recv_data.empty() || m->flags&DBSTMF::NET_FALLBACK) {
				m->data = std::move(recv_data);
				m->SendReply(m, th);
				return;
			}
			break;
		}
		case DBSM::INSERTUSER: {
			if(gc.readonlymode) break;
			dbinsertusermsg *m = (dbinsertusermsg*) msg;
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
			dbinsertaccmsg *m = (dbinsertaccmsg*) msg;
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
			m->SendReply(m, th);
			return;
		}
		case DBSM::DELACC: {
			if(gc.readonlymode) break;
			dbdelaccmsg *m = (dbdelaccmsg*) msg;
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
			dbinsertmediamsg *m=(dbinsertmediamsg*) msg;
			sqlite3_stmt *stmt=cache.GetStmt(db, DBPSC_INSERTMEDIA);
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) m->media_id.m_id);
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) m->media_id.t_id);
			bind_compressed(stmt, 3, m->url, 'P');
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
			dbupdatemediamsg *m = (dbupdatemediamsg*) msg;
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
			dbupdatetweetsetflagsmsg *m = (dbupdatetweetsetflagsmsg*) msg;
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
			dbsendmsg_list *m = (dbsendmsg_list*) msg;
			DBLogMsgFormat(LOGT::DBTRACE, wxT("DBSM::MSGLIST: queue size: %d"), m->msglist.size());
			while(!m->msglist.empty()) {
				ProcessMessage(db, m->msglist.front(), ok, cache, th);
				m->msglist.pop();
			}
			cache.EndTransaction(db);
		}
		default: break;
	}
	delete msg;
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
		ProcessMessage(db, msg, ok, cache, this);
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
END_EVENT_TABLE()

void dbconn::OnStdTweetLoadFromDB(wxCommandEvent &event) {
	dbseltweetmsg *msg = (dbseltweetmsg *) event.GetClientData();
	event.SetClientData(0);
	HandleDBSelTweetMsg(msg, 0);
	delete msg;
}

void dbconn::PrepareStdTweetLoadMsg(dbseltweetmsg *loadmsg) {
	loadmsg->targ = this;
	loadmsg->cmdevtype = wxextDBCONN_NOTIFY;
	loadmsg->winid = wxDBCONNEVT_ID_STDTWEETLOAD;
}

void dbconn::GenericDBSelTweetMsgHandler(wxCommandEvent &event) {
	dbseltweetmsg *msg = (dbseltweetmsg *) event.GetClientData();
	event.SetClientData(0);

	const auto &it = generic_sel_funcs.find(reinterpret_cast<intptr_t>(msg));
	if(it != generic_sel_funcs.end()) {
		it->second(msg, this);
		generic_sel_funcs.erase(it);
	}
	else {
		DBLogMsgFormat(LOGT::DBERR, wxT("dbconn::GenericDBSelTweetMsgHandler could not find handler for %p."), msg);
	}

	delete msg;
	dbc_flags |= DBCF::REPLY_CLEARNOUPDF;
}

void dbconn::SetDBSelTweetMsgHandler(dbseltweetmsg *msg, std::function<void(dbseltweetmsg *, dbconn *)> f) {
	msg->targ = this;
	msg->cmdevtype = wxextDBCONN_NOTIFY;
	msg->winid = wxDBCONNEVT_ID_GENERICSELTWEET;
	generic_sel_funcs[reinterpret_cast<intptr_t>(msg)] = std::move(f);
}

void dbconn::HandleDBSelTweetMsg(dbseltweetmsg *msg, flagwrapper<HDBSF> flags) {
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::HandleDBSelTweetMsg start"));

	if(msg->flags & DBSTMF::CLEARNOUPDF) dbc_flags |= DBCF::REPLY_CLEARNOUPDF;

	if(msg->flags & DBSTMF::NET_FALLBACK) {
		dbseltweetmsg_netfallback *fmsg = dynamic_cast<dbseltweetmsg_netfallback *>(msg);
		std::shared_ptr<taccount> acc;
		if(fmsg) {
			GetAccByDBIndex(fmsg->dbindex, acc);
		}
		std::set<uint64_t> missing_id_set = msg->id_set;
		for(auto &it : msg->data) {
			missing_id_set.erase(it.id);
		}
		for(auto &it : missing_id_set) {
			tweet_ptr t = ad.GetTweetById(it);

			if(!t->text.size() && !(t->lflags & TLF::BEINGLOADEDOVERNET)) {	//tweet still not loaded at all

				t->lflags &= ~TLF::BEINGLOADEDFROMDB;

				std::shared_ptr<taccount> curacc = acc;
				bool result = CheckLoadSingleTweet(t, curacc);
				if(result) {
					LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::HandleDBSelTweetMsg falling back to network for tweet: id: %" wxLongLongFmtSpec "d, account: %s."), t->id, curacc->dispname.c_str());
				}
				else {
					LogMsgFormat(LOGT::DBERR, wxT("dbconn::HandleDBSelTweetMsg could not fall back to network for tweet: id:%" wxLongLongFmtSpec "d, no usable account."), t->id);
				}
			}
		}
	}

	for(dbretmediadata &dt : msg->media_data) {
		std::unique_ptr<media_entity> &me = ad.media_list[dt.media_id];
		if(!me) {
			me.reset(new media_entity);
			me->media_id = dt.media_id;
			me->media_url = std::move(dt.url);
			dt.url.clear();
			if(me->media_url.size()) {
				ad.img_media_map[me->media_url] = me->media_id;
			}
		}
		if(dt.flags & MEF::LOAD_FULL) me->full_img_sha1 = dt.full_img_sha1;
		if(dt.flags & MEF::LOAD_THUMB) me->thumb_img_sha1 = dt.thumb_img_sha1;
		me->flags |= dt.flags | MEF::IN_DB;
	}

	for(dbrettweetdata &dt : msg->data) {
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
		if(dt.user2) t->user_recipient=ad.GetUserContainerById(dt.user2);
		t->createtime = (time_t) dt.timestamp;
		new (&t->flags) tweet_flags(dt.flags);
		if(dt.rtid) {
			t->rtsrc = ad.GetTweetById(dt.rtid);
		}

		t->updcf_flags = UPDCF::DEFAULT;

		if(!(flags & HDBSF::NOPENDINGS)) {
			if(CheckMarkPending_GetAcc(t)) {
				t->lflags &= ~TLF::BEINGLOADEDFROMDB;
				UnmarkPendingTweet(t, UMPTF::TPDB_NOUPDF);
			}
			else {
				dbc.dbc_flags |= DBCF::REPLY_CHECKPENDINGS;
			}
		}
	}
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::HandleDBSelTweetMsg end"));
}

void dbconn::OnDBNewAccountInsert(wxCommandEvent &event) {
	dbinsertaccmsg *msg = (dbinsertaccmsg *) event.GetClientData();
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

void dbconn::SendMessageBatched(dbsendmsg *msg) {
	if(!batchqueue)
		batchqueue = new dbsendmsg_list;

	batchqueue->msglist.push(msg);
	if(!(dbc_flags & DBCF::BATCHEVTPENDING)) {
		dbc_flags |= DBCF::BATCHEVTPENDING;
		wxCommandEvent evt(wxextDBCONN_NOTIFY, wxDBCONNEVT_ID_SENDBATCH);
		AddPendingEvent(evt);
	}
}

void dbconn::OnSendBatchEvt(wxCommandEvent &event) {
	if(!(dbc_flags & DBCF::INITED)) return;

	dbc_flags &= ~DBCF::BATCHEVTPENDING;
	if(batchqueue) {
		SendMessage(batchqueue);
		batchqueue = 0;
	}
}

void dbconn::OnDBReplyEvt(wxCommandEvent &event) {
	dbreplyevtstruct *msg = (dbreplyevtstruct *) event.GetClientData();
	event.SetClientData(0);

	if(dbc_flags & DBCF::INITED) {
		for(auto &it : msg->reply_list) {
			it.first->ProcessEvent(*it.second);
		}
	}
	delete msg;

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

void dbconn::SendMessageOrAddToList(dbsendmsg *msg, dbsendmsg_list *msglist) {
	if(msglist) msglist->msglist.push(msg);
	else SendMessage(msg);
}

void dbconn::SendMessage(dbsendmsg *msg) {
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

void dbconn::SendAccDBUpdate(dbinsertaccmsg *insmsg) {
	insmsg->targ = this;
	insmsg->cmdevtype = wxextDBCONN_NOTIFY;
	insmsg->winid = wxDBCONNEVT_ID_INSERTNEWACC;
	dbc.SendMessage(insmsg);
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
		res = sqlite3_exec(syncdb, startup_sql, 0, 0, 0);
		if(res != SQLITE_OK) {
			wxMessageDialog(0, wxString::Format(wxT("Startup SQL failed, got error: %d (%s)\nDatabase filename: %s\nCheck that the database is not locked by another process, and that the directory is read/writable."),
				res, wxstrstd(sqlite3_errmsg(syncdb)).c_str(), wxstrstd(filename).c_str()),
				wxT("Fatal Startup Error"), wxOK | wxICON_ERROR ).ShowModal();
			sqlite3_close(syncdb);
			syncdb = 0;
			return false;
		}
	}

	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::Init(): About to read in state from database"));

	AccountSync(syncdb);
	ReadAllCFGIn(syncdb, gc, alist);
	SyncReadInAllUsers(syncdb);
	SyncReadInRBFSs(syncdb);
	if(gc.persistentmediacache) SyncReadInAllMediaEntities(syncdb);
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

	dbc_flags |= DBCF::INITED;
	return true;
}

void dbconn::DeInit() {
	if(!(dbc_flags & DBCF::INITED)) return;

	if(batchqueue) {
		SendMessage(batchqueue);
		batchqueue = 0;
	}

	dbc_flags &= ~DBCF::INITED;

	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, wxT("dbconn::DeInit: About to terminate database thread and write back state"));

	SendMessage(new dbsendmsg(DBSM::QUIT));

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
		SyncWriteBackAllUsers(syncdb);
		AccountIdListsSync(syncdb);
		SyncWriteOutRBFSs(syncdb);
		WriteAllCFGOut(syncdb, gc, alist);
		SyncWriteBackCIDSLists(syncdb);
		SyncWriteBackWindowLayout(syncdb);
		SyncWriteBackTpanels(syncdb);
	}

	sqlite3_close(syncdb);

	LogMsg(LOGT::DBTRACE | LOGT::THREADTRACE, wxT("dbconn::DeInit(): State write back to database complete, database connection closed."));
}

void dbconn::InsertNewTweet(tweet_ptr_p tobj, std::string statjson, dbsendmsg_list *msglist) {
	dbinserttweetmsg *msg = new dbinserttweetmsg();
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

	auto shouldsavemediaid = [&](const entity &ent) -> bool {
		return (ent.media_id && ad.media_list[ent.media_id]->flags & MEF::IN_DB);
	};

	unsigned int count = 0;
	for(auto &it : tobj->entlist) {
		if(shouldsavemediaid(it)) count++;
	}
	if(count) {
		unsigned char *data = (unsigned char *) malloc(count*16);
		unsigned char *curdata = data;
		for(auto &it : tobj->entlist) {
			if(shouldsavemediaid(it)) {
				writebeuint64(curdata, it.media_id.m_id);
				writebeuint64(curdata + 8, it.media_id.t_id);
				curdata += 16;
			}
		}
		msg->mediaindex = DoCompress(data, count*16, msg->mediaindex_size, 'Z');
		free(data);
	}
	else {
		msg->mediaindex = 0;
		msg->mediaindex_size = 0;
	}

	SendMessageOrAddToList(msg, msglist);
}

void dbconn::UpdateTweetDyn(tweet_ptr_p tobj, dbsendmsg_list *msglist) {
	dbupdatetweetmsg *msg = new dbupdatetweetmsg();
	msg->dynjson = tobj->mkdynjson();
	msg->id = tobj->id;
	msg->flags = tobj->flags.Save();
	SendMessageOrAddToList(msg, msglist);
}

void dbconn::InsertUser(udc_ptr_p u, dbsendmsg_list *msglist) {
	dbinsertusermsg *msg = new dbinsertusermsg();
	msg->id = u->id;
	msg->json=u->mkjson();
	msg->cached_profile_img_url = std::string(u->cached_profile_img_url.begin(), u->cached_profile_img_url.end());	//prevent any COW semantics
	msg->createtime = u->user.createtime;
	msg->lastupdate = u->lastupdate;
	msg->cached_profile_img_hash  =  u->cached_profile_img_sha1;
	msg->mentionindex = settocompressedblob(u->mention_index, msg->mentionindex_size);
	u->lastupdate_wrotetodb = u->lastupdate;
	SendMessageOrAddToList(msg, msglist);
}

void dbconn::InsertMedia(media_entity &me, dbsendmsg_list *msglist) {
	dbinsertmediamsg *msg = new dbinsertmediamsg();
	msg->media_id = me.media_id;
	msg->url = std::string(me.media_url.begin(), me.media_url.end());
	SendMessageOrAddToList(msg, msglist);
	me.flags |= MEF::IN_DB;
}

void dbconn::UpdateMedia(media_entity &me, DBUMMT update_type, dbsendmsg_list *msglist) {
	dbupdatemediamsg *msg = new dbupdatemediamsg(update_type);
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

	}
	SendMessageOrAddToList(msg, msglist);
}

//tweetids, dmids are big endian in database
void dbconn::AccountSync(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::AccountSync start"));
	const char getacc[] = "SELECT id, name, tweetids, dmids, userid, dispname FROM acc;";
	sqlite3_stmt *getstmt = 0;
	sqlite3_prepare_v2(adb, getacc, sizeof(getacc), &getstmt, 0);
	do {
		int res = sqlite3_step(getstmt);
		if(res == SQLITE_ROW) {
			unsigned int id = (unsigned int) sqlite3_column_int(getstmt, 0);
			wxString name = wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 1));
			LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::AccountSync: Found %d, %s"), id, name.c_str());

			std::shared_ptr<taccount> ta(new(taccount));
			ta->name = name;
			ta->dbindex = id;
			alist.push_back(ta);

			setfromcompressedblob([&](uint64_t &tid) { ta->tweet_ids.insert(tid); }, getstmt, 2);
			setfromcompressedblob([&](uint64_t &tid) { ta->dm_ids.insert(tid); }, getstmt, 3);
			uint64_t userid = (uint64_t) sqlite3_column_int64(getstmt, 4);
			ta->usercont = ad.GetUserContainerById(userid);
			ta->dispname = wxString::FromUTF8((const char*) sqlite3_column_text(getstmt, 5));
		}
		else break;
	} while(true);
	sqlite3_finalize(getstmt);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::AccountSync end"));
}

void dbconn::SyncReadInAllTweetIDs(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllTweetIDs start"));
	const char gettweetids[] = "SELECT id FROM tweets;";
	sqlite3_stmt *getstmt = 0;
	sqlite3_prepare_v2(adb, gettweetids, sizeof(gettweetids), &getstmt, 0);
	do {
		int res = sqlite3_step(getstmt);
		if(res == SQLITE_ROW) {
			uint64_t id = (uint64_t) sqlite3_column_int64(getstmt, 0);
			ad.unloaded_db_tweet_ids.insert(id);
		}
		else if(res != SQLITE_DONE) {
			LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInAllTweetIDs got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(adb)).c_str());
		}
		else break;
	} while(true);

	sqlite3_finalize(getstmt);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllTweetIDs end"));
}

void dbconn::SyncReadInCIDSLists(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInCIDSLists start"));
	const char getcidslist[] = "SELECT value FROM settings WHERE name == ?;";
	sqlite3_stmt *getstmt = 0;
	sqlite3_prepare_v2(adb, getcidslist, sizeof(getcidslist), &getstmt, 0);

	auto doonelist = [&](std::string name, tweetidset &tlist) {
		sqlite3_bind_text(getstmt, 1, name.c_str(), name.size(), SQLITE_STATIC);
		do {
			int res = sqlite3_step(getstmt);
			if(res == SQLITE_ROW) {
				setfromcompressedblob([&](uint64_t &id) { tlist.insert(id); }, getstmt, 0);
			}
			else if(res != SQLITE_DONE) {
				LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInCIDSLists got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(adb)).c_str());
			}
			else break;
		} while(true);
		sqlite3_reset(getstmt);
	};

	cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
		doonelist(name, ad.cids.*ptr);
	});

	sqlite3_finalize(getstmt);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInCIDSLists end"));
}

void dbconn::SyncWriteBackCIDSLists(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackCIDSLists start"));
	cache.BeginTransaction(adb);
	const char setunreadlist[] = "INSERT OR REPLACE INTO settings(accid, name, value) VALUES ('G', ?, ?);";

	sqlite3_stmt *setstmt = 0;
	sqlite3_prepare_v2(adb, setunreadlist, sizeof(setunreadlist), &setstmt, 0);

	auto doonelist = [&](std::string name, tweetidset &tlist) {
		size_t index_size;
		unsigned char *index = settocompressedblob(tlist, index_size);
		sqlite3_bind_text(setstmt, 1, name.c_str(), name.size(), SQLITE_STATIC);
		sqlite3_bind_blob(setstmt, 2, index, index_size, &free);
		int res = sqlite3_step(setstmt);
		if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncWriteBackCIDSLists got error: %d (%s), for set: %s"),
				res, wxstrstd(sqlite3_errmsg(adb)).c_str(), wxstrstd(name).c_str()); }
		sqlite3_reset(setstmt);
	};

	cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
		doonelist(name, ad.cids.*ptr);
	});

	sqlite3_finalize(setstmt);
	cache.EndTransaction(adb);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackCIDSLists end"));
}

//tweetids, dmids are big endian in database
void dbconn::AccountIdListsSync(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::AccountIdListsSync start"));
	cache.BeginTransaction(adb);
	sqlite3_stmt *setstmt = cache.GetStmt(adb, DBPSC_UPDATEACCIDLISTS);
	for(auto &it : alist) {
		size_t size;
		unsigned char *data;

		data=settocompressedblob(it->tweet_ids, size);
		sqlite3_bind_blob(setstmt, 1, data, size, &free);

		data=settocompressedblob(it->dm_ids, size);
		sqlite3_bind_blob(setstmt, 2, data, size, &free);

		sqlite3_bind_text(setstmt, 3, it->dispname.ToUTF8(), -1, SQLITE_TRANSIENT);

		sqlite3_bind_int(setstmt, 4, it->dbindex);

		int res = sqlite3_step(setstmt);
		if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::AccountIdListsSync got error: %d (%s) for user dbindex: %d, name: %s"),
				res, wxstrstd(sqlite3_errmsg(adb)).c_str(), it->dbindex, it->dispname.c_str()); }
		else { LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::AccountIdListsSync inserted user dbindex: %d, name: %s"),
				it->dbindex, it->dispname.c_str()); }
		sqlite3_reset(setstmt);
	}
	cache.EndTransaction(adb);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::AccountIdListsSync end"));
}

void dbconn::SyncWriteBackAllUsers(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackAllUsers start"));
	cache.BeginTransaction(adb);

	sqlite3_stmt *stmt = cache.GetStmt(adb, DBPSC_INSUSER);
	size_t user_count = 0;
	for(auto &it : ad.userconts) {
		userdatacontainer *u = &(it.second);
		if(u->lastupdate == u->lastupdate_wrotetodb) continue;    //this user is already in the database and does not need updating
		if(u->user.screen_name.empty()) continue;               //don't bother saving empty user stubs
		user_count++;
		sqlite3_bind_int64(stmt, 1, (sqlite3_int64) it.first);
		bind_compressed(stmt, 2, u->mkjson(), 'J');
		bind_compressed(stmt, 3, u->cached_profile_img_url, 'P');
		sqlite3_bind_int64(stmt, 4, (sqlite3_int64) u->user.createtime);
		sqlite3_bind_int64(stmt, 5, (sqlite3_int64) u->lastupdate);
		if(u->cached_profile_img_sha1) {
			sqlite3_bind_blob(stmt, 6, u->cached_profile_img_sha1->hash_sha1, sizeof(u->cached_profile_img_sha1->hash_sha1), SQLITE_TRANSIENT);
		}
		else {
			sqlite3_bind_null(stmt, 6);
		}
		size_t mentionindex_size;
		unsigned char *mentionindex = settocompressedblob(u->mention_index, mentionindex_size);
		sqlite3_bind_blob(stmt, 7, mentionindex, mentionindex_size, &free);
		int res = sqlite3_step(stmt);
		if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncWriteBackAllUsers got error: %d (%s) for user id: %" wxLongLongFmtSpec "d"),
				res, wxstrstd(sqlite3_errmsg(adb)).c_str(), it.first); }
		else { LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackAllUsers inserted user id:%" wxLongLongFmtSpec "d"), it.first); }
		sqlite3_reset(stmt);
	}
	cache.EndTransaction(adb);
	LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackAllUsers end, wrote back %u of %u users"), user_count, ad.userconts.size());
}

void dbconn::SyncReadInAllUsers(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllUsers start"));
	const char sql[] = "SELECT id, json, cachedprofimgurl, createtimestamp, lastupdatetimestamp, cachedprofileimgchecksum, mentionindex FROM users;";
	sqlite3_stmt *stmt = 0;
	sqlite3_prepare_v2(adb, sql, sizeof(sql), &stmt, 0);

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

void dbconn::SyncWriteOutRBFSs(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteOutRBFSs start"));
	cache.BeginTransaction(adb);
	sqlite3_exec(adb, "DELETE FROM rbfspending", 0, 0, 0);
	sqlite3_stmt *stmt=cache.GetStmt(adb, DBPSC_INSERTRBFSP);
	for(auto &it : alist) {
		taccount &acc = *it;
		for(restbackfillstate &rbfs : acc.pending_rbfs_list) {
			if(rbfs.start_tweet_id >= acc.GetMaxId(rbfs.type)) continue;    //rbfs would be read next time anyway
			if(!rbfs.end_tweet_id || rbfs.end_tweet_id >= acc.GetMaxId(rbfs.type)) {
				rbfs.end_tweet_id = acc.GetMaxId(rbfs.type);    //remove overlap
				if(rbfs.end_tweet_id) rbfs.end_tweet_id--;
			}
			sqlite3_bind_int64(stmt, 1, (sqlite3_int64) acc.dbindex);
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64) rbfs.type);
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64) rbfs.start_tweet_id);
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64) rbfs.end_tweet_id);
			sqlite3_bind_int64(stmt, 5, (sqlite3_int64) rbfs.max_tweets_left);
			int res = sqlite3_step(stmt);
			if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncWriteOutRBFSs got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
			else { LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncWriteOutRBFSs inserted pending RBFS")); }
			sqlite3_reset(stmt);
		}
	}
	cache.EndTransaction(adb);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteOutRBFSs end"));
}

void dbconn::SyncReadInRBFSs(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInRBFSs start"));
	const char sql[] = "SELECT accid, type, startid, endid, maxleft FROM rbfspending;";
	sqlite3_stmt *stmt = 0;
	sqlite3_prepare_v2(adb, sql, sizeof(sql), &stmt, 0);

	do {
		int res = sqlite3_step(stmt);
		if(res == SQLITE_ROW) {
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
					break;
				}
			}
			if(found) { LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInRBFSs retrieved RBFS")); }
			else { LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInRBFSs retrieved RBFS with no associated account or bad type, ignoring")); }
		}
		else if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInRBFSs got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		else break;
	} while(true);
	sqlite3_finalize(stmt);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInRBFSs end"));
}

void dbconn::SyncReadInAllMediaEntities(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllMediaEntities start"));
	const char sql[] = "SELECT mid, tid, url, fullchecksum, thumbchecksum, flags FROM mediacache;";
	sqlite3_stmt *stmt = 0;
	sqlite3_prepare_v2(adb, sql, sizeof(sql), &stmt, 0);

	do {
		int res = sqlite3_step(stmt);
		if(res == SQLITE_ROW) {
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
				ad.img_media_map[me.media_url] = me.media_id;
			}
			free(url);
			if(sqlite3_column_bytes(stmt, 3) == sizeof(sha1_hash_block::hash_sha1)) {
				std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
				memcpy(hash->hash_sha1, sqlite3_column_blob(stmt, 3), sizeof(hash->hash_sha1));
				me.full_img_sha1 = std::move(hash);
				me.flags |= MEF::LOAD_FULL;
			}
			if(sqlite3_column_bytes(stmt, 4) == sizeof(sha1_hash_block::hash_sha1)) {
				std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
				memcpy(hash->hash_sha1, sqlite3_column_blob(stmt, 4), sizeof(hash->hash_sha1));
				me.thumb_img_sha1 = std::move(hash);
				me.flags |= MEF::LOAD_THUMB;
			}
			me.flags |= static_cast<MEF>(sqlite3_column_int64(stmt, 5));

			#if DB_COPIOUS_LOGGING
				LogMsgFormat(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllMediaEntities retrieved media entity %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d"), id.m_id, id.t_id);
			#endif
		}
		else if(res != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInAllMediaEntities got error: %d (%s)"), res, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		else break;
	} while(true);
	sqlite3_finalize(stmt);

	dbc_flags |= DBCF::ALL_MEDIA_ENTITIES_LOADED;

	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInAllMediaEntities end"));
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

	do {
		int res2 = sqlite3_step(stmt);
		if(res2 == SQLITE_ROW) {
			std::string name = (const char *) sqlite3_column_text(stmt, 0);
			std::string dispname = (const char *) sqlite3_column_text(stmt, 1);
			flagwrapper<TPF> flags = static_cast<TPF>(sqlite3_column_int(stmt, 2));
			std::shared_ptr<tpanel> tp = tpanel::MkTPanel(name, dispname, flags);
			setfromcompressedblob([&](uint64_t &id) { tp->tweetlist.insert(id); }, stmt, 3);
			tp->RecalculateCIDS();
		}
		else if(res2 != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncReadInTpanels got error: %d (%s)"), res2, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
		else break;
	} while(true);
	sqlite3_finalize(stmt);
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncReadInTpanels end"));
}

void dbconn::SyncWriteBackTpanels(sqlite3 *adb) {
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackTpanels start"));
	sqlite3_exec(adb, "DELETE FROM tpanels", 0, 0, 0);
	const char sql[] = "INSERT INTO tpanels (name, dispname, flags, ids) VALUES (?, ?, ?, ?);";
	sqlite3_stmt *stmt = 0;
	int res = sqlite3_prepare_v2(adb, sql, sizeof(sql), &stmt, 0);
	if(res != SQLITE_OK) {
		LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncWriteBackTpanels sqlite3_prepare_v2 failed"));
		return;
	}

	for(auto &it : ad.tpanels) {
		tpanel &tp = *(it.second);
		if(tp.flags & TPF::SAVETODB) {
			sqlite3_bind_text(stmt, 1, tp.name.c_str(), tp.name.size(), SQLITE_STATIC);
			sqlite3_bind_text(stmt, 2, tp.dispname.c_str(), tp.dispname.size(), SQLITE_STATIC);
			sqlite3_bind_int(stmt, 3, flag_unwrap<TPF>(tp.flags));
			size_t ids_size;
			unsigned char *ids = settocompressedblob(tp.tweetlist, ids_size);
			sqlite3_bind_blob(stmt, 4, ids, ids_size, &free);

			int res2 = sqlite3_step(stmt);
			if(res2 != SQLITE_DONE) { LogMsgFormat(LOGT::DBERR, wxT("dbconn::SyncWriteBackTpanels got error: %d (%s)"), res2, wxstrstd(sqlite3_errmsg(adb)).c_str()); }
			sqlite3_reset(stmt);
		}
	}
	LogMsg(LOGT::DBTRACE, wxT("dbconn::SyncWriteBackTpanels end"));
}

void dbsendmsg_callback::SendReply(void *data, dbiothread *th) {
	wxCommandEvent *evt = new wxCommandEvent(cmdevtype, winid);
	evt->SetClientData(data);
	th->reply_list.emplace_back(targ, std::unique_ptr<wxEvent>(evt));
}

static const std::string globstr = "G";

void DBGenConfig::SetDBIndexGlobal() {
	dbindex_global=true;
}

void DBGenConfig::SetDBIndex(unsigned int id) {
	dbindex_global=false;
	dbindex=id;
}
void DBGenConfig::bind_accid_name(sqlite3_stmt *stmt, const char *name) {
	if(dbindex_global) sqlite3_bind_text(stmt, 1, globstr.c_str(), globstr.size(), SQLITE_STATIC);
	else sqlite3_bind_int(stmt, 1, dbindex);
	sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
}

DBGenConfig::DBGenConfig(sqlite3 *db_) : dbindex(0), dbindex_global(true), db(db_) { }

DBWriteConfig::DBWriteConfig(sqlite3 *db_) : DBGenConfig(db_), stmt(0) {
	sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO settings(accid, name, value) VALUES (?, ?, ?);", -1, &stmt, 0);
	sqlite3_prepare_v2(db, "DELETE FROM settings WHERE (accid IS ?) AND (name IS ?)", -1, &delstmt, 0);
	sqlite3_stmt *exst = 0;
	sqlite3_prepare_v2(db, "BEGIN;", -1, &exst, 0);
	exec(exst);
	sqlite3_finalize(exst);
}

DBWriteConfig::~DBWriteConfig() {
	sqlite3_stmt *exst = 0;
	sqlite3_prepare_v2(db, "COMMIT;", -1, &exst, 0);
	exec(exst);
	sqlite3_finalize(exst);
	sqlite3_finalize(stmt);
	sqlite3_finalize(delstmt);
}
void DBWriteConfig::WriteUTF8(const char *name, const char *strval) {
	bind_accid_name(stmt, name);
	sqlite3_bind_text(stmt, 3, strval, -1, SQLITE_TRANSIENT);
	exec(stmt);
}
void DBWriteConfig::WriteInt64(const char *name, int64_t val) {
	bind_accid_name(stmt, name);
	sqlite3_bind_int64(stmt, 3, val);
	exec(stmt);
}
void DBWriteConfig::Delete(const char *name) {
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

DBReadConfig::DBReadConfig(sqlite3 *db_) : DBGenConfig(db_), stmt(0) {
	sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE (accid IS ?) AND (name IS ?);", -1, &stmt, 0);
	sqlite3_stmt *exst = 0;
	sqlite3_prepare_v2(db, "BEGIN;", -1, &exst, 0);
	exec(exst);
	sqlite3_finalize(exst);
}

DBReadConfig::~DBReadConfig() {
	sqlite3_stmt *exst = 0;
	sqlite3_prepare_v2(db, "COMMIT;", -1, &exst, 0);
	exec(exst);
	sqlite3_finalize(exst);
	sqlite3_finalize(stmt);
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

void DBC_SendMessage(dbsendmsg *msg) {
	dbc.SendMessage(msg);
}

void DBC_SendMessageOrAddToList(dbsendmsg *msg, dbsendmsg_list *msglist) {
	dbc.SendMessageOrAddToList(msg, msglist);
}

void DBC_SendMessageBatched(dbsendmsg *msg) {
	dbc.SendMessageBatched(msg);
}

void DBC_SendAccDBUpdate(dbinsertaccmsg *insmsg) {
	dbc.SendAccDBUpdate(insmsg);
}

void DBC_InsertMedia(media_entity &me, dbsendmsg_list *msglist) {
	dbc.InsertMedia(me, msglist);
}

void DBC_UpdateMedia(media_entity &me, DBUMMT update_type, dbsendmsg_list *msglist) {
	dbc.UpdateMedia(me, update_type, msglist);
}

void DBC_InsertNewTweet(tweet_ptr_p tobj, std::string statjson, dbsendmsg_list *msglist) {
	dbc.InsertNewTweet(tobj, statjson, msglist);
}

void DBC_UpdateTweetDyn(tweet_ptr_p tobj, dbsendmsg_list *msglist) {
	dbc.UpdateTweetDyn(tobj, msglist);
}

void DBC_InsertUser(udc_ptr_p u, dbsendmsg_list *msglist) {
	dbc.InsertUser(u, msglist);
}

void DBC_HandleDBSelTweetMsg(dbseltweetmsg *msg, flagwrapper<HDBSF> flags) {
	dbc.HandleDBSelTweetMsg(msg, flags);
}

void DBC_SetDBSelTweetMsgHandler(dbseltweetmsg *msg, std::function<void(dbseltweetmsg *, dbconn *)> f) {
	dbc.SetDBSelTweetMsgHandler(msg, std::move(f));
}

bool DBC_AllMediaEntitiesLoaded() {
	return (dbc.dbc_flags & dbconn::DBCF::ALL_MEDIA_ENTITIES_LOADED);
}

void DBC_PrepareStdTweetLoadMsg(dbseltweetmsg *loadmsg) {
	dbc.PrepareStdTweetLoadMsg(loadmsg);
}
