//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
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

#ifndef HGUARD_SRC_DB
#define HGUARD_SRC_DB

#include "univdefs.h"
#include "twit-common.h"
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

struct media_entity;
struct tweet;
struct userdatacontainer;
struct dbconn;

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

struct dbreplyevtstruct {
	std::deque<std::pair<wxEvtHandler *, std::unique_ptr<wxEvent> > > reply_list;
};

typedef enum {
	DBSM_QUIT=1,
	DBSM_INSERTTWEET,
	DBSM_UPDATETWEET,
	DBSM_SELTWEET,
	DBSM_INSERTUSER,
	DBSM_MSGLIST,
	DBSM_INSERTACC,
	DBSM_INSERTMEDIA,
	DBSM_UPDATEMEDIACHKSM,
	DBSM_DELACC,
	DBSM_UPDATETWEETSETFLAGS,
} DBSM_TYPE;

struct dbsendmsg {
	DBSM_TYPE type;

	dbsendmsg(DBSM_TYPE type_) : type(type_) { }
	virtual ~dbsendmsg() { }
};

struct dbsendmsg_list : public dbsendmsg {
	dbsendmsg_list() : dbsendmsg(DBSM_MSGLIST) { }

	std::queue<dbsendmsg *> msglist;
};

struct dbsendmsg_callback : public dbsendmsg {
	dbsendmsg_callback(DBSM_TYPE type_) : dbsendmsg(type_) { }
	dbsendmsg_callback(DBSM_TYPE type_, wxEvtHandler *targ_, WXTYPE cmdevtype_, int winid_ = wxID_ANY ) :
		dbsendmsg(type_), targ(targ_), cmdevtype(cmdevtype_), winid(winid_) { }

	wxEvtHandler *targ;
	WXTYPE cmdevtype;
	int winid;

	void SendReply(void *data, dbiothread *th);
};

struct dbinserttweetmsg : public dbsendmsg {
	dbinserttweetmsg() : dbsendmsg(DBSM_INSERTTWEET) { }

	std::string statjson;
	std::string dynjson;
	uint64_t id, user1, user2, rtid, timestamp;
	uint64_t flags;
	unsigned char *mediaindex;			//already packed and compressed, must be malloced
	size_t mediaindex_size;
};

struct dbupdatetweetmsg : public dbsendmsg {
	dbupdatetweetmsg() : dbsendmsg(DBSM_UPDATETWEET) { }

	std::string dynjson;
	uint64_t id;
	uint64_t flags;
};

struct dbrettweetdata {
	char *statjson;	//free when done
	char *dynjson;	//free when done
	uint64_t id, user1, user2, rtid, timestamp;
	uint64_t flags;

	dbrettweetdata() : statjson(0), dynjson(0) { }
	~dbrettweetdata() {
		if(statjson) free(statjson);
		if(dynjson) free(dynjson);
	}
	dbrettweetdata(const dbrettweetdata& that) = delete;
};

struct dbretmediadata {
	media_id_type media_id;
	char *url;	//free when done
	unsigned char full_img_sha1[20];
	unsigned char thumb_img_sha1[20];
	unsigned int flags;

	dbretmediadata() : url(0) { }
	~dbretmediadata() {
		if(url) free(url);
	}
	dbretmediadata(const dbretmediadata& that) = delete;
};

enum {
	DBSTMF_PULLMEDIA	= 1<<0,
	DBSTMF_NO_ERR		= 1<<1,
	DBSTMF_NET_FALLBACK	= 1<<2,
};

struct dbseltweetmsg : public dbsendmsg_callback {
	dbseltweetmsg() : dbsendmsg_callback(DBSM_SELTWEET), flags(0) { }

	unsigned int flags;
	std::set<uint64_t> id_set;                      //ids to select
	std::forward_list<dbrettweetdata> data;         //return data
	std::forward_list<dbretmediadata> media_data;   //return data
};

struct dbseltweetmsg_netfallback : public dbseltweetmsg {
	dbseltweetmsg_netfallback() : dbseltweetmsg(), dbindex(0) {
		flags|=DBSTMF_NET_FALLBACK;
	}

	unsigned int dbindex;				//for the use of the main thread only
};

struct dbinsertusermsg : public dbsendmsg {
	dbinsertusermsg() : dbsendmsg(DBSM_INSERTUSER) { }
	uint64_t id;
	std::string json;
	std::string cached_profile_img_url;
	time_t createtime;
	uint64_t lastupdate;
	std::string cached_profile_img_hash;
	unsigned char *mentionindex;			//already packed and compressed, must be malloced
	size_t mentionindex_size;
};

struct dbinsertaccmsg : public dbsendmsg_callback {
	dbinsertaccmsg() : dbsendmsg_callback(DBSM_INSERTACC) { }

	std::string name;            //account name
	std::string dispname;        //account name
	uint64_t userid;
	unsigned int dbindex;        //return data
};

struct dbdelaccmsg : public dbsendmsg {
	dbdelaccmsg() : dbsendmsg(DBSM_DELACC) { }

	unsigned int dbindex;
};

struct dbinsertmediamsg : public dbsendmsg {
	dbinsertmediamsg() : dbsendmsg(DBSM_INSERTMEDIA) { }
	media_id_type media_id;
	std::string url;
};

struct dbupdatemediachecksummsg : public dbsendmsg {
	dbupdatemediachecksummsg(bool isfull_) : dbsendmsg(DBSM_UPDATEMEDIACHKSM), isfull(isfull_) { }
	media_id_type media_id;
	unsigned char chksm[20];
	bool isfull;
};

struct dbupdatetweetsetflagsmsg : public dbsendmsg {
	dbupdatetweetsetflagsmsg(tweetidset &&ids_, uint64_t setmask_, uint64_t unsetmask_) : dbsendmsg(DBSM_UPDATETWEETSETFLAGS), ids(ids_), setmask(setmask_), unsetmask(unsetmask_) { }

	tweetidset ids;
	uint64_t setmask;
	uint64_t unsetmask;
};

DECLARE_EVENT_TYPE(wxextDBCONN_NOTIFY, -1)

enum {
	wxDBCONNEVT_ID_TPANELTWEETLOAD = 1,
	wxDBCONNEVT_ID_DEBUGMSG,
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
	enum {
		DBCF_INITED                = 1<<0,
		DBCF_BATCHEVTPENDING       = 1<<1,
		DBCF_REPLY_CLEARNOUPDF     = 1<<2,
		DBCF_REPLY_CHECKPENDINGS   = 1<<3,
	};

	unsigned int dbc_flags = 0;

	dbconn() { }
	~dbconn() { DeInit(); }
	bool Init(const std::string &filename);
	void DeInit();
	void SendMessage(dbsendmsg *msg);
	void SendMessageOrAddToList(dbsendmsg *msg, dbsendmsg_list *msglist);
	void SendMessageBatched(dbsendmsg *msg);

	void InsertNewTweet(const std::shared_ptr<tweet> &tobj, std::string statjson, dbsendmsg_list *msglist = 0);
	void UpdateTweetDyn(const std::shared_ptr<tweet> &tobj, dbsendmsg_list *msglist = 0);
	void InsertUser(const std::shared_ptr<userdatacontainer> &u, dbsendmsg_list *msglist = 0);
	void InsertMedia(media_entity &me, dbsendmsg_list *msglist = 0);
	void UpdateMediaChecksum(media_entity &me, bool isfull, dbsendmsg_list *msglist = 0);
	void AccountSync(sqlite3 *adb);
	void SyncWriteBackAllUsers(sqlite3 *adb);
	void SyncReadInAllUsers(sqlite3 *adb);
	void AccountIdListsSync(sqlite3 *adb);
	void SyncWriteOutRBFSs(sqlite3 *adb);
	void SyncReadInRBFSs(sqlite3 *adb);
	void SyncReadInAllMediaEntities(sqlite3 *adb);
	void OnTpanelTweetLoadFromDB(wxCommandEvent &event);
	void OnDBThreadDebugMsg(wxCommandEvent &event);
	void OnDBNewAccountInsert(wxCommandEvent &event);
	void OnSendBatchEvt(wxCommandEvent &event);
	void OnDBReplyEvt(wxCommandEvent &event);
	void SyncReadInCIDSLists(sqlite3 *adb);
	void SyncWriteBackCIDSLists(sqlite3 *adb);
	void SyncReadInWindowLayout(sqlite3 *adb);
	void SyncWriteBackWindowLayout(sqlite3 *adb);
	void SyncReadInAllTweetIDs(sqlite3 *adb);

	enum {
		HDBSF_NOPENDINGS         = 1<<0,
	};
	void HandleDBSelTweetMsg(dbseltweetmsg *msg, unsigned int flags);
	void GenericDBSelTweetMsgHandler(wxCommandEvent &event);
	void SetDBSelTweetMsgHandler(dbseltweetmsg *msg, std::function<void(dbseltweetmsg *, dbconn *)> f);

	DECLARE_EVENT_TABLE()
};

struct DBGenConfig {
	void SetDBIndexGlobal();
	void SetDBIndex(unsigned int id);
	DBGenConfig(sqlite3 *db_);

	protected:
	unsigned int dbindex;
	bool dbindex_global;
	sqlite3 *db;
	void bind_accid_name(sqlite3_stmt *stmt, const char *name);
};

struct DBWriteConfig : public DBGenConfig {
	void WriteUTF8(const char *name, const char *strval);
	void WriteWX(const char *name, const wxString &strval) { WriteUTF8(name, strval.ToUTF8()); }
	void WriteInt64(const char *name, sqlite3_int64 val);
	void Delete(const char *name);
	void DeleteAll();
	DBWriteConfig(sqlite3 *db);
	~DBWriteConfig();

	protected:
	sqlite3_stmt *stmt;
	sqlite3_stmt *delstmt;
	void exec(sqlite3_stmt *stmt);
};

struct DBReadConfig : public DBGenConfig {
	bool Read(const char *name, wxString *strval, const wxString &defval);
	bool ReadInt64(const char *name, sqlite3_int64 *strval, sqlite3_int64 defval);
	bool ReadBool(const char *name, bool *strval, bool defval);
	bool ReadUInt64(const char *name, uint64_t *strval, uint64_t defval);
	DBReadConfig(sqlite3 *db);
	~DBReadConfig();

	protected:
	sqlite3_stmt *stmt;
	bool exec(sqlite3_stmt *stmt);
};

extern dbconn dbc;

#endif
