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
#include <cstdlib>
#include <queue>
#include <string>
#include <set>
#include <map>
#include <forward_list>
#include <wx/string.h>
#include <wx/event.h>
#include <sqlite3.h>
#ifdef __WINDOWS__
#include <windows.h>
#endif

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
	DBPSC_DELACC,
	DBPSC_UPDATETWEETFLAGSMASKED,

	DBPSC_NUM_STATEMENTS,
} DBPSC_TYPE;

struct dbpscache {
	sqlite3_stmt *stmts[DBPSC_NUM_STATEMENTS];

	sqlite3_stmt *GetStmt(sqlite3 *adb, DBPSC_TYPE type);
	int ExecStmt(sqlite3 *adb, DBPSC_TYPE type);
	void DeAllocAll();
	dbpscache() {
		memset(stmts, 0, sizeof(stmts));
	}
	~dbpscache() { DeAllocAll(); }
	void BeginTransaction(sqlite3 *adb);
	void EndTransaction(sqlite3 *adb);

	private:
	unsigned int transaction_refcount = 0;
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

DECLARE_EVENT_TYPE(wxextDBCONN_NOTIFY, -1)

enum {
	wxDBCONNEVT_ID_STDTWEETLOAD = 1,
	wxDBCONNEVT_ID_INSERTNEWACC,
	wxDBCONNEVT_ID_SENDBATCH,
	wxDBCONNEVT_ID_REPLY,
	wxDBCONNEVT_ID_GENERICSELTWEET,
};

struct dbconn : public wxEvtHandler {
	#ifdef __WINDOWS__
	HANDLE iocp;
	#else
	int pipefd;
	#endif
	sqlite3 *syncdb;
	dbiothread *th = 0;
	dbpscache cache;
	dbsendmsg_list *batchqueue = 0;

	private:
	std::map<intptr_t, std::function<void(dbseltweetmsg *, dbconn *)> > generic_sel_funcs;

	public:
	enum class DBCF {
		INITED                      = 1<<0,
		BATCHEVTPENDING             = 1<<1,
		REPLY_CLEARNOUPDF           = 1<<2,
		REPLY_CHECKPENDINGS         = 1<<3,
		ALL_MEDIA_ENTITIES_LOADED   = 1<<4,
	};

	flagwrapper<DBCF> dbc_flags = 0;

	dbconn() { }
	~dbconn() { DeInit(); }
	bool Init(const std::string &filename);
	void DeInit();
	void SendMessage(dbsendmsg *msg);
	void SendMessageOrAddToList(dbsendmsg *msg, dbsendmsg_list *msglist);
	void SendMessageBatched(dbsendmsg *msg);
	void SendAccDBUpdate(dbinsertaccmsg *insmsg);

	void InsertNewTweet(const std::shared_ptr<tweet> &tobj, std::string statjson, dbsendmsg_list *msglist = 0);
	void UpdateTweetDyn(const std::shared_ptr<tweet> &tobj, dbsendmsg_list *msglist = 0);
	void InsertUser(const std::shared_ptr<userdatacontainer> &u, dbsendmsg_list *msglist = 0);
	void InsertMedia(media_entity &me, dbsendmsg_list *msglist = 0);
	void UpdateMedia(media_entity &me, DBUMMT update_type, dbsendmsg_list *msglist = 0);
	void AccountSync(sqlite3 *adb);
	void SyncWriteBackAllUsers(sqlite3 *adb);
	void SyncReadInAllUsers(sqlite3 *adb);
	void AccountIdListsSync(sqlite3 *adb);
	void SyncWriteOutRBFSs(sqlite3 *adb);
	void SyncReadInRBFSs(sqlite3 *adb);
	void SyncReadInAllMediaEntities(sqlite3 *adb);
	void OnStdTweetLoadFromDB(wxCommandEvent &event);
	void PrepareStdTweetLoadMsg(dbseltweetmsg *insmsg);
	void OnDBNewAccountInsert(wxCommandEvent &event);
	void OnSendBatchEvt(wxCommandEvent &event);
	void OnDBReplyEvt(wxCommandEvent &event);
	void SyncReadInCIDSLists(sqlite3 *adb);
	void SyncWriteBackCIDSLists(sqlite3 *adb);
	void SyncReadInWindowLayout(sqlite3 *adb);
	void SyncWriteBackWindowLayout(sqlite3 *adb);
	void SyncReadInAllTweetIDs(sqlite3 *adb);
	void SyncReadInTpanels(sqlite3 *adb);
	void SyncWriteBackTpanels(sqlite3 *adb);

	void HandleDBSelTweetMsg(dbseltweetmsg *msg, flagwrapper<HDBSF> flags);
	void GenericDBSelTweetMsgHandler(wxCommandEvent &event);
	void SetDBSelTweetMsgHandler(dbseltweetmsg *msg, std::function<void(dbseltweetmsg *, dbconn *)> f);

	DECLARE_EVENT_TABLE()
};
template<> struct enum_traits<dbconn::DBCF> { static constexpr bool flags = true; };

#endif
