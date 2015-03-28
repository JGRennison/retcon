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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_DB_INTL
#define HGUARD_SRC_DB_INTL

#include "univdefs.h"
#include "flags.h"
#include "db.h"
#include "log.h"
#include "util.h"
#include "rapidjson-inc.h"
#include "tweetidset.h"
#include <cstdlib>
#include <queue>
#include <string>
#include <set>
#include <map>
#include <forward_list>
#include <wx/string.h>
#include <wx/event.h>
#include <wx/timer.h>
#include <sqlite3.h>
#include <string.h>
#ifdef __WINDOWS__
#include <windows.h>
#endif

struct dbconn;
extern dbconn dbc;

typedef enum {
	DBPSC_START = 0,

	DBPSC_INSTWEET = 0,
	DBPSC_UPDTWEET,
	DBPSC_BEGIN,
	DBPSC_COMMIT,
	DBPSC_INSUSER,
	DBPSC_INSERTNEWACC,
	DBPSC_UPDATEACCIDLISTS,
	DBPSC_SELTWEET,
	DBPSC_INSERTRBFSP,
	DBPSC_SELMEDIA,
	DBPSC_INSERTMEDIA,
	DBPSC_UPDATEMEDIATHUMBCHKSM,
	DBPSC_UPDATEMEDIAFULLCHKSM,
	DBPSC_UPDATEMEDIAFLAGS,
	DBPSC_UPDATEMEDIALASTUSED,
	DBPSC_DELACC,
	DBPSC_UPDATETWEETFLAGSMASKED,
	DBPSC_INSSETTING,
	DBPSC_DELSETTING,
	DBPSC_SELSETTING,
	DBPSC_INSSTATICSETTING,
	DBPSC_DELSTATICSETTING,
	DBPSC_SELSTATICSETTING,
	DBPSC_INSTPANEL,
	DBPSC_INSUSERDMINDEX,
	DBPSC_SELUSER,
	DBPSC_CLEARHANDLENEWPENDINGS,
	DBPSC_INSHANDLENEWPENDINGS,

	DBPSC_NUM_STATEMENTS,
} DBPSC_TYPE;

struct dbpscache {
	sqlite3_stmt *stmts[DBPSC_NUM_STATEMENTS];

	sqlite3_stmt *GetStmt(sqlite3 *adb, DBPSC_TYPE type);
	int ExecStmt(sqlite3 *adb, DBPSC_TYPE type);
	void DeAllocAll();
	dbpscache();
	~dbpscache();
	void BeginTransaction(sqlite3 *adb);
	void EndTransaction(sqlite3 *adb);
	void CheckTransactionRefcountState();

	private:
	int transaction_refcount = 0;
	bool transaction_refcount_went_negative = false;
};

struct dbiothread : public wxThread {
	#ifdef __WINDOWS__
	HANDLE iocp;
	#else
	int pipefd;
	#endif
	std::string filename;

	sqlite3 *db;
	dbpscache cache;
	std::deque<std::pair<wxEvtHandler *, std::unique_ptr<wxEvent> > > reply_list;
	dbconn *dbc;

	dbiothread() : wxThread(wxTHREAD_JOINABLE) { }
	wxThread::ExitCode Entry();
	void MsgLoop();
};

struct dbfunctionmsg : public dbsendmsg {
	dbfunctionmsg() : dbsendmsg(DBSM::FUNCTION) { }
	std::vector<std::function<void(sqlite3 *, bool &, dbpscache &)> > funclist;
};

DECLARE_EVENT_TYPE(wxextDBCONN_NOTIFY, -1)

enum {
	wxDBCONNEVT_ID_STDTWEETLOAD = 1,
	wxDBCONNEVT_ID_INSERTNEWACC,
	wxDBCONNEVT_ID_SENDBATCH,
	wxDBCONNEVT_ID_REPLY,
	wxDBCONNEVT_ID_GENERICSELTWEET,
	wxDBCONNEVT_ID_STDUSERLOAD,
	wxDBCONNEVT_ID_GENERICSELUSER,
};

enum {
	DBCONNTIMER_ID_ASYNCSTATEWRITE = 1,
};

struct dbconn : public wxEvtHandler {
	#ifdef __WINDOWS__
	HANDLE iocp;
	#else
	int pipefd;
	#endif
	sqlite3 *syncdb;
	dbiothread *th = nullptr;
	dbpscache cache;
	std::unique_ptr<dbsendmsg_list> batchqueue;
	std::unique_ptr<wxTimer> asyncstateflush_timer;

	// This has the same function as, but is distinct from ad.unloaded_db_user_ids.
	// This is eventually consistent with ad.unloaded_db_user_ids, but not instantaneously consistent,
	// mainly because the two sets are owned by different threads. The DB thread will clear an item
	// from this before sending it to the main thread, whch will *then* clear the same item.
	useridset unloaded_user_ids;
	unsigned int sync_load_user_count = 0;

	// This contains all tweet IDs in the DB at any given time
	tweetidset all_tweet_ids;

	private:
	std::map<intptr_t, std::function<void(dbseltweetmsg &, dbconn *)> > generic_sel_funcs;
	std::map<intptr_t, std::function<void(dbselusermsg &, dbconn *)> > generic_sel_user_funcs;
	std::vector<std::function<void(dbconn *)> > post_init_callbacks;

	public:
	enum class DBCF {
		INITED                      = 1<<0,
		BATCHEVTPENDING             = 1<<1,
		REPLY_CLEARNOUPDF           = 1<<2,
		REPLY_CHECKPENDINGS         = 1<<3,
	};

	flagwrapper<DBCF> dbc_flags = 0;

	dbconn() { }
	~dbconn() { DeInit(); }

	bool Init(const std::string &filename);
	void DeInit();

	void AsyncWriteBackState();

	void SendMessage(std::unique_ptr<dbsendmsg> msg);
	void SendMessageOrAddToList(std::unique_ptr<dbsendmsg> msg, optional_observer_ptr<dbsendmsg_list> msglist);
	void SendMessageBatched(std::unique_ptr<dbsendmsg> msg);
	observer_ptr<dbsendmsg_list> GetMessageBatchQueue();

	void SendBatchedTweetFlagUpdate(uint64_t id, uint64_t setmask, uint64_t unsetmask);

	void SendAccDBUpdate(std::unique_ptr<dbinsertaccmsg> insmsg);

	void AccountSync(sqlite3 *adb);

	void InsertNewTweet(tweet_ptr_p tobj, std::string statjson, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
	void UpdateTweetDyn(tweet_ptr_p tobj, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);

	void InsertUser(udc_ptr_p u, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
	void SyncWriteBackAllUsers(sqlite3 *adb);
	void AsyncWriteBackAllUsers(dbfunctionmsg &msg);
	void SyncReadInAllUserIDs(sqlite3 *adb);
	udc_ptr SyncReadInUser(sqlite3 *syncdb, uint64_t id);
	void AsyncReadInUser(sqlite3 *adb, uint64_t id, std::deque<dbretuserdata> &out);
	void SyncPostUserLoadCompletion();

	void InsertMedia(media_entity &me, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
	void UpdateMedia(media_entity &me, DBUMMT update_type, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);

	void SyncWriteBackUserDMIndexes(sqlite3 *adb);
	void AsyncWriteBackUserDMIndexes(dbfunctionmsg &msg);
	void SyncReadInUserDMIndexes(sqlite3 *adb);

	void SyncWriteBackAccountIdLists(sqlite3 *adb);
	void AsyncWriteBackAccountIdLists(dbfunctionmsg &msg);

	void SyncWriteOutRBFSs(sqlite3 *adb);
	void AsyncWriteOutRBFSs(dbfunctionmsg &msg);
	void SyncReadInRBFSs(sqlite3 *adb);

	void SyncWriteOutHandleNewPendingOps(sqlite3 *adb);
	void AsyncWriteOutHandleNewPendingOps(dbfunctionmsg &msg);
	void SyncReadInHandleNewPendingOps(sqlite3 *adb);

	void SyncReadInAllMediaEntities(sqlite3 *adb);

	void OnDBNewAccountInsert(wxCommandEvent &event);

	void OnSendBatchEvt(wxCommandEvent &event);
	void OnDBReplyEvt(wxCommandEvent &event);

	void SyncReadInCIDSLists(sqlite3 *adb);
	void SyncWriteBackCIDSLists(sqlite3 *adb);
	void AsyncWriteBackCIDSLists(dbfunctionmsg &msg);

	void SyncReadInWindowLayout(sqlite3 *adb);
	void SyncWriteBackWindowLayout(sqlite3 *adb);

	void SyncReadInAllTweetIDs(sqlite3 *syncdb);
	void SyncWriteBackTweetIDIndexCache(sqlite3 *syncdb);

	void SyncReadInTpanels(sqlite3 *adb);
	void SyncWriteBackTpanels(sqlite3 *adb);
	void AsyncWriteBackTpanels(dbfunctionmsg &msg);

	bool SyncDoUpdates(sqlite3 *adb);
	void SyncWriteDBVersion(sqlite3 *adb);

	bool CheckIfPurgeDue(sqlite3 *db, time_t threshold, const char *settingname, const char *funcname, time_t &delta);
	void UpdateLastPurged(sqlite3 *db, const char *settingname, const char *funcname);
	void SyncPurgeMediaEntities(sqlite3 *db);
	void SyncPurgeProfileImages(sqlite3 *adb);
	void CheckPurgeTweets();
	void CheckPurgeUsers();

	void SyncReadInUserRelationships(sqlite3 *adb);
	void SyncWriteBackUserRelationships(sqlite3 *adb);

	void OnStdTweetLoadFromDB(wxCommandEvent &event);
	void PrepareStdTweetLoadMsg(dbseltweetmsg &loadmsg);
	void HandleDBSelTweetMsg(dbseltweetmsg &msg, optional_observer_ptr<db_handle_msg_pending_guard> pending_guard);
	void GenericDBSelTweetMsgHandler(wxCommandEvent &event);
	void SetDBSelTweetMsgHandler(dbseltweetmsg &msg, std::function<void(dbseltweetmsg &, dbconn *)> f);

	void OnStdUserLoadFromDB(wxCommandEvent &event);
	void PrepareStdUserLoadMsg(dbselusermsg &loadmsg);
	void GenericDBSelUserMsgHandler(wxCommandEvent &event);
	void SetDBSelUserMsgHandler(dbselusermsg &msg, std::function<void(dbselusermsg &, dbconn *)> f);
	void DBSelUserReturnDataHandler(std::deque<dbretuserdata> data, optional_observer_ptr<db_handle_msg_pending_guard> pending_guard);

	void OnAsyncStateWriteTimer(wxTimerEvent& event);
	void ResetAsyncStateWriteTimer();

	DECLARE_EVENT_TABLE()

	private:
	void SyncDoUpdates_FillUserDMIndexes(sqlite3 *adb);
};
template<> struct enum_traits<dbconn::DBCF> { static constexpr bool flags = true; };

template <typename E> void DBDoErr(E errspec, sqlite3 *adb, sqlite3_stmt *stmt, int res) {
	errspec(stmt, res);
}

template <> inline void DBDoErr<std::string>(std::string errspec, sqlite3 *adb, sqlite3_stmt *stmt, int res) {
	TSLogMsgFormat(LOGT::DBERR, "%s got error: %d (%s)", cstr(errspec), res, cstr(sqlite3_errmsg(adb)));
}

template <> inline void DBDoErr<const char *>(const char *errspec, sqlite3 *adb, sqlite3_stmt *stmt, int res) {
	DBDoErr<std::string>(errspec, adb, stmt, res);
}

template <> inline void DBDoErr<std::nullptr_t>(std::nullptr_t p, sqlite3 *adb, sqlite3_stmt *stmt, int res) {
	//do nothing
}

struct stmt_holder {
	sqlite3_stmt *m_stmt;

	sqlite3_stmt *stmt() { return m_stmt; }
};

inline stmt_holder DBInitialiseSql(sqlite3 *adb, sqlite3_stmt *stmt) {
	return stmt_holder { stmt };
}

struct stmt_deleter {
	void operator()(sqlite3_stmt* ptr) const {
		sqlite3_finalize(ptr);
	}
};

struct scoped_stmt_holder {
	std::unique_ptr<sqlite3_stmt, stmt_deleter> m_stmt;

	sqlite3_stmt *stmt() { return m_stmt.get(); }
};

inline scoped_stmt_holder DBInitialiseSql(sqlite3 *adb, std::string sql) {
	sqlite3_stmt *stmt;
	sqlite3_prepare_v2(adb, sql.c_str(), sql.size(), &stmt, nullptr);
	return scoped_stmt_holder { std::unique_ptr<sqlite3_stmt, stmt_deleter>(stmt) };
}

inline scoped_stmt_holder DBInitialiseSql(sqlite3 *adb, const char *sql) {
	return DBInitialiseSql(adb, std::string(sql));
}

template <typename F, typename E> void DBRowExecStmt(sqlite3 *adb, sqlite3_stmt *stmt, F func, E errspec) {
	do {
		int res = sqlite3_step(stmt);
		if(res == SQLITE_ROW) {
			func(stmt);
		}
		else if(res != SQLITE_DONE) {
			DBDoErr(errspec, adb, stmt, res);
			break;
		}
		else break;
	} while(true);
}

template <typename F> void DBRowExecStmtNoError(sqlite3 *adb, sqlite3_stmt *stmt, F func) {
	DBRowExecStmt(adb, stmt, func, [](sqlite3_stmt *stmt_, int res_) { });
};

template <typename B, typename F, typename E, typename S> void DBBindRowExec(sqlite3 *adb, S sql, B bindfunc, F func, E errspec) {
	auto s = DBInitialiseSql(adb, sql);
	bindfunc(s.stmt());
	DBRowExecStmt(adb, s.stmt(), func, errspec);
	sqlite3_reset(s.stmt());
};

template <typename F, typename E, typename S> void DBRowExec(sqlite3 *adb, S sql, F func, E errspec) {
	DBBindRowExec(adb, sql, [](sqlite3_stmt *stmt) { }, func, errspec);
}

template <typename B, typename F, typename S> void DBBindRowExecNoError(sqlite3 *adb, S sql, B bindfunc, F func) {
	DBBindRowExec(adb, sql, bindfunc, func, nullptr);
};

template <typename F, typename S> void DBRowExecNoError(sqlite3 *adb, S sql, F func) {
	DBRowExec(adb, sql, func, nullptr);
};



template <typename E> void DBExecStmt(sqlite3 *adb, sqlite3_stmt *stmt, E errspec) {
	int res = sqlite3_step(stmt);
	if(res != SQLITE_DONE) {
		DBDoErr(errspec, adb, stmt, res);
	}
}

template <typename S> void DBExecStmtNoError(sqlite3 *adb, S sql) {
	DBRowExecStmt(adb, sql, nullptr);
};


template <typename B, typename E, typename S> void DBBindExec(sqlite3 *adb, S sql, B bindfunc, E errspec) {
	auto s = DBInitialiseSql(adb, sql);
	bindfunc(s.stmt());
	DBExecStmt(adb, s.stmt(), errspec);
	sqlite3_reset(s.stmt());
};


template <typename E, typename S> void DBExec(sqlite3 *adb, S sql, E errspec) {
	DBBindExec(adb, sql, [](sqlite3_stmt *stmt) { }, errspec);
}

template <typename B, typename S> void DBBindExecNoError(sqlite3 *adb, S sql, B bindfunc) {
	DBBindExec(adb, sql, bindfunc, nullptr);
};


template <typename B, typename E, typename S, typename I, typename J> void DBRangeBindExec(sqlite3 *adb, S sql, I rangebegin, J rangeend, B bindfunc, E errspec) {
	auto s = DBInitialiseSql(adb, sql);
	for(auto it = rangebegin; it != rangeend; ++it) {
		bindfunc(s.stmt(), *it);
		DBExecStmt(adb, s.stmt(), errspec);
		sqlite3_reset(s.stmt());
	}
};

template <typename B, typename S, typename I, typename J> void DBRangeBindExecNoError(sqlite3 *adb, S sql, I rangebegin, J rangeend, B bindfunc) {
	DBRangeBindExec(adb, sql, rangebegin, rangeend, bindfunc, nullptr);
};

db_bind_buffer<dbb_compressed> DoCompress(const void *in, size_t insize, unsigned char tag = 'Z', bool *iscompressed = nullptr);
db_bind_buffer<dbb_uncompressed> DoDecompress(db_bind_buffer<dbb_compressed> &&in);
db_bind_buffer<dbb_uncompressed> column_get_compressed(sqlite3_stmt* stmt, int num);
db_bind_buffer<dbb_uncompressed> column_get_compressed_and_parse(sqlite3_stmt* stmt, int num, rapidjson::Document &dc);

inline db_bind_buffer<dbb_compressed> DoCompress(const std::string &in, unsigned char tag = 'Z', bool *iscompressed = nullptr) {
	return DoCompress(in.data(), in.size(), tag, iscompressed);
}

inline db_bind_buffer<dbb_compressed> DoCompress(const db_bind_buffer<dbb_uncompressed> &in, unsigned char tag = 'Z', bool *iscompressed = nullptr) {
	return DoCompress(in.data, in.data_size, tag, iscompressed);
}

inline void store_packed_uint64(unsigned char *&curdata, uint64_t diff) {
	if(diff < (static_cast<uint64_t>(1) << 30)) {
		curdata[0] = (diff >> 24) & 0x3F;
		curdata[1] = (diff >> 16) & 0xFF;
		curdata[2] = (diff >> 8) & 0xFF;
		curdata[3] = (diff) & 0xFF;
		curdata += 4;
	}
	else if(diff < (static_cast<uint64_t>(1) << 38)) {
		curdata[0] = ((diff >> 32) & 0x3F) | 0x40;
		curdata[1] = (diff >> 24) & 0xFF;
		curdata[2] = (diff >> 16) & 0xFF;
		curdata[3] = (diff >> 8) & 0xFF;
		curdata[4] = (diff) & 0xFF;
		curdata += 5;
	}
	else if(diff < (static_cast<uint64_t>(1) << 46)) {
		curdata[0] = ((diff >> 40) & 0x3F) | 0x80;
		curdata[1] = (diff >> 32) & 0xFF;
		curdata[2] = (diff >> 24) & 0xFF;
		curdata[3] = (diff >> 16) & 0xFF;
		curdata[4] = (diff >> 8) & 0xFF;
		curdata[5] = (diff) & 0xFF;
		curdata += 6;
	}
	else {
		curdata[0] = 0xC0;
		curdata[1] = (diff >> 56) & 0xFF;
		curdata[2] = (diff >> 48) & 0xFF;
		curdata[3] = (diff >> 40) & 0xFF;
		curdata[4] = (diff >> 32) & 0xFF;
		curdata[5] = (diff >> 24) & 0xFF;
		curdata[6] = (diff >> 16) & 0xFF;
		curdata[7] = (diff >> 8) & 0xFF;
		curdata[8] = (diff) & 0xFF;
		curdata += 9;
	}
}

template <typename C, typename F> db_bind_buffer<dbb_compressed> settocompressedblob_intpack_generic(const C &set, unsigned char tag, F encoder) {
	db_bind_buffer<dbb_compressed> out;
	if(set.size()) {
		out.allocate(1 + (set.size() * 9));
		unsigned char *curdata = reinterpret_cast<unsigned char *>(out.mutable_data());
		curdata[0] = tag;
		curdata++;

		for(uint64_t id : set) {
			store_packed_uint64(curdata, encoder(id));
		}
		out.data_size = curdata - reinterpret_cast<unsigned char *>(out.mutable_data());
	}

	return std::move(out);
}

// Descending diff
template <typename C> db_bind_buffer<dbb_compressed> settocompressedblob_desc(const C &set) {
	uint64_t last_in = 0;
	return settocompressedblob_intpack_generic(set, 'B', [&](uint64_t in) -> uint64_t {
		uint64_t diff = last_in - in;
		last_in = in;
		return diff;
	});
}

// Ascending diff, zigzagged
template <typename C> db_bind_buffer<dbb_compressed> settocompressedblob_zigzag(const C &set) {
	uint64_t last_in = 0;
	return settocompressedblob_intpack_generic(set, 'C', [&](uint64_t in) -> uint64_t {
		uint64_t diff = in - last_in;
		last_in = in;

		uint64_t negmask = (diff >> 63) & 1;
		// negmask = 1 if input negative, else 0
		negmask = -negmask;
		// negmask is -1 (all ones) if input negative, else 0
		return (diff << 1) ^ negmask;
	});
}

inline uint64_t fetch_packed_uint64(const unsigned char *&input) {
	unsigned char tag = input[0] & 0xC0;
	uint64_t output = 0;
	switch(tag) {
		case 0x00:
			output = static_cast<uint64_t>(input[0] & 0x3F) << 24 |
				static_cast<uint64_t>(input[1]) << 16 |
				static_cast<uint64_t>(input[2]) << 8 |
				static_cast<uint64_t>(input[3]);
				input += 4;
			break;
		case 0x40:
			output = static_cast<uint64_t>(input[0] & 0x3F) << 32 |
				static_cast<uint64_t>(input[1]) << 24 |
				static_cast<uint64_t>(input[2]) << 16 |
				static_cast<uint64_t>(input[3]) << 8 |
				static_cast<uint64_t>(input[4]);
				input += 5;
			break;
		case 0x80:
			output = static_cast<uint64_t>(input[0] & 0x3F) << 40 |
				static_cast<uint64_t>(input[1]) << 32 |
				static_cast<uint64_t>(input[2]) << 24 |
				static_cast<uint64_t>(input[3]) << 16 |
				static_cast<uint64_t>(input[4]) << 8 |
				static_cast<uint64_t>(input[5]);
				input += 6;
			break;
		case 0xC0:
			output = static_cast<uint64_t>(input[1]) << 56 |
				static_cast<uint64_t>(input[2]) << 48 |
				static_cast<uint64_t>(input[3]) << 40 |
				static_cast<uint64_t>(input[4]) << 32 |
				static_cast<uint64_t>(input[5]) << 24 |
				static_cast<uint64_t>(input[6]) << 16 |
				static_cast<uint64_t>(input[7]) << 8 |
				static_cast<uint64_t>(input[8]);
				input += 9;
			break;
	}
	return output;
}

template <typename C> void setfromcompressedblob_generic(C func, sqlite3_stmt *stmt, int columnid) {
	db_bind_buffer<dbb_compressed> src;
	src.data = static_cast<const char *>(sqlite3_column_blob(stmt, columnid));
	src.data_size = sqlite3_column_bytes(stmt, columnid);

	if(!src.data_size)
		return;

	if(src.data[0] == 'B') {
		const unsigned char *curdata = reinterpret_cast<const unsigned char *>(src.data);
		const unsigned char *enddata = curdata + src.data_size;
		curdata++;
		uint64_t last = 0;
		while(curdata < enddata) {
			last -= fetch_packed_uint64(curdata);
			func(last);
		}
	}
	else if(src.data[0] == 'C') {
		const unsigned char *curdata = reinterpret_cast<const unsigned char *>(src.data);
		const unsigned char *enddata = curdata + src.data_size;
		curdata++;
		uint64_t last = 0;
		while(curdata < enddata) {
			uint64_t zigzag = fetch_packed_uint64(curdata);
			uint64_t diff = (zigzag >> 1) ^ (-(zigzag & 1));
			last += diff;
			func(last);
		}
	}
	else {
		// legacy format
		db_bind_buffer<dbb_uncompressed> blblob = DoDecompress(std::move(src));

		unsigned char *blarray = (unsigned char*) blblob.data;
		size_t blarraysize = blblob.data_size & ~7;

		for(unsigned int i = 0; i < blarraysize; i += 8) {    //stored in big endian format
			uint64_t id = 0;
			for(unsigned int j = 0; j < 8; j++) id <<= 8, id |= blarray[i + j];
			func(id);
		}
	}
}

template <typename C> void setfromcompressedblob(C &set, sqlite3_stmt *stmt, int columnid) {
	setfromcompressedblob_generic([&](uint64_t id) {
		set.insert(set.end(), id);
	}, stmt, columnid);
}

inline void bind_compressed(sqlite3_stmt* stmt, int num, db_bind_buffer<dbb_compressed> &&buffer, unsigned char tag = 'Z') {
	buffer.align();
	sqlite3_bind_blob(stmt, num, buffer.release_membuffer(), buffer.data_size, &free);
}

inline void bind_compressed(sqlite3_stmt* stmt, int num, db_bind_buffer<dbb_uncompressed> &&buffer, unsigned char tag = 'Z') {
	bind_compressed(stmt, num, DoCompress(buffer, tag));
}

inline void bind_compressed(sqlite3_stmt* stmt, int num, const char *in, size_t insize, unsigned char tag = 'Z') {
	bind_compressed(stmt, num, DoCompress(in, insize, tag, nullptr));
}

inline void bind_compressed(sqlite3_stmt* stmt, int num, const unsigned char *in, size_t insize, unsigned char tag = 'Z') {
	bind_compressed(stmt, num, reinterpret_cast<const char *>(in), insize, tag);
}

inline void bind_compressed(sqlite3_stmt* stmt, int num, const std::string &in, unsigned char tag = 'Z') {
	bind_compressed(stmt, num, in.data(), in.size(), tag);
}

#endif
