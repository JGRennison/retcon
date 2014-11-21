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

	private:
	std::map<intptr_t, std::function<void(dbseltweetmsg &, dbconn *)> > generic_sel_funcs;

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
	void SendAccDBUpdate(std::unique_ptr<dbinsertaccmsg> insmsg);

	void InsertNewTweet(tweet_ptr_p tobj, std::string statjson, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
	void UpdateTweetDyn(tweet_ptr_p tobj, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
	void InsertUser(udc_ptr_p u, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
	void InsertMedia(media_entity &me, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
	void UpdateMedia(media_entity &me, DBUMMT update_type, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
	void AccountSync(sqlite3 *adb);
	void SyncWriteBackAllUsers(sqlite3 *adb);
	void AsyncWriteBackAllUsers(dbfunctionmsg &msg);
	void SyncReadInAllUsers(sqlite3 *adb);
	void SyncWriteBackAccountIdLists(sqlite3 *adb);
	void AsyncWriteBackAccountIdLists(dbfunctionmsg &msg);
	void SyncWriteOutRBFSs(sqlite3 *adb);
	void AsyncWriteOutRBFSs(dbfunctionmsg &msg);
	void SyncReadInRBFSs(sqlite3 *adb);
	void SyncReadInAllMediaEntities(sqlite3 *adb);
	void OnStdTweetLoadFromDB(wxCommandEvent &event);
	void PrepareStdTweetLoadMsg(dbseltweetmsg &insmsg);
	void OnDBNewAccountInsert(wxCommandEvent &event);
	void OnSendBatchEvt(wxCommandEvent &event);
	void OnDBReplyEvt(wxCommandEvent &event);
	void SyncReadInCIDSLists(sqlite3 *adb);
	void SyncWriteBackCIDSLists(sqlite3 *adb);
	void AsyncWriteBackCIDSLists(dbfunctionmsg &msg);
	void SyncReadInWindowLayout(sqlite3 *adb);
	void SyncWriteBackWindowLayout(sqlite3 *adb);
	void SyncReadInAllTweetIDs(sqlite3 *adb);
	void SyncReadInTpanels(sqlite3 *adb);
	void SyncWriteBackTpanels(sqlite3 *adb);
	void AsyncWriteBackTpanels(dbfunctionmsg &msg);
	void SyncDoUpdates(sqlite3 *adb);
	void SyncWriteDBVersion(sqlite3 *adb);
	void SyncPurgeMediaEntities(sqlite3 *adb);
	void SyncPurgeProfileImages(sqlite3 *adb);
	void CheckPurgeTweets();
	void SyncReadInUserRelationships(sqlite3 *adb);
	void SyncWriteBackUserRelationships(sqlite3 *adb);

	void HandleDBSelTweetMsg(dbseltweetmsg &msg, flagwrapper<HDBSF> flags);
	void GenericDBSelTweetMsgHandler(wxCommandEvent &event);
	void SetDBSelTweetMsgHandler(dbseltweetmsg &msg, std::function<void(dbseltweetmsg &, dbconn *)> f);

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
	LogMsgFormat(LOGT::DBERR, "%s got error: %d (%s)", cstr(errspec), res, cstr(sqlite3_errmsg(adb)));
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


struct esctabledef {
	unsigned char id;
	const char *text;
};

struct esctable {
	unsigned char tag;
	const esctabledef *start;
	size_t count;
};

unsigned char *DoCompress(const void *in, size_t insize, size_t &sz, unsigned char tag = 'Z', bool *iscompressed = nullptr, const esctable *et = nullptr);
char *DoDecompress(const unsigned char *in, size_t insize, size_t &outsize);
char *column_get_compressed(sqlite3_stmt* stmt, int num, size_t &outsize);
char *column_get_compressed_and_parse(sqlite3_stmt* stmt, int num, rapidjson::Document &dc);

inline unsigned char *DoCompress(const std::string &in, size_t &sz, unsigned char tag = 'Z', bool *iscompressed = nullptr, const esctable *et = nullptr) {
	return DoCompress(in.data(), in.size(), sz, tag, iscompressed, et);
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

inline void bind_compressed(sqlite3_stmt* stmt, int num, const char *in, size_t insize, unsigned char tag = 'Z', const esctable *et = nullptr) {
	size_t comsize;
	unsigned char *com = DoCompress(in, insize, comsize, tag, nullptr, et);
	sqlite3_bind_blob(stmt, num, com, comsize, &free);
}

inline void bind_compressed(sqlite3_stmt* stmt, int num, const unsigned char *in, size_t insize, unsigned char tag = 'Z', const esctable *et = nullptr) {
	bind_compressed(stmt, num, reinterpret_cast<const char *>(in), insize, tag, et);
}

inline void bind_compressed(sqlite3_stmt* stmt, int num, const std::string &in, unsigned char tag = 'Z', const esctable *et = nullptr) {
	bind_compressed(stmt, num, in.data(), in.size(), tag, et);
}

#endif
