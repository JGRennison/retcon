#include <sqlite3.h>

typedef enum {
	DBPSC_START=0,

	DBPSC_INSTWEET=0,
	DBPSC_UPDTWEET,
	DBPSC_BEGIN,
	DBPSC_COMMIT,
	DBPSC_INSUSER,
	DBPSC_INSERTNEWACC,
	DBPSC_UPDATEACCIDLISTS,
	DBPSC_SELTWEET,

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

	dbiothread() : wxThread(wxTHREAD_JOINABLE) { }
	wxThread::ExitCode Entry();
	void MsgLoop();
};

typedef enum {
	DBSM_QUIT=1,
	DBSM_INSERTTWEET,
	DBSM_UPDATETWEET,
	DBSM_SELTWEET,
	DBSM_INSERTUSER,
	DBSM_MSGLIST,
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
	dbsendmsg_callback(DBSM_TYPE type_, wxEvtHandler *targ_, WXTYPE cmdevtype_, int winid_=wxID_ANY ) :
		dbsendmsg(type_), targ(targ_), cmdevtype(cmdevtype_), winid(winid_) { }

	wxEvtHandler *targ;
	WXTYPE cmdevtype;
	int winid;

	void SendReply(void *data);
};

struct dbinserttweetmsg : public dbsendmsg {
	dbinserttweetmsg() : dbsendmsg(DBSM_INSERTTWEET) { }

	std::string statjson;
	std::string dynjson;
	uint64_t id, user1, user2, timestamp;
	uint64_t flags;
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
	uint64_t id, user1, user2, timestamp;
	uint64_t flags;

	dbrettweetdata() : statjson(0), dynjson(0) { }
	~dbrettweetdata() {
		if(statjson) free(statjson);
		if(dynjson) free(dynjson);
	}
	dbrettweetdata(const dbrettweetdata& that) = delete;
};

struct dbseltweetmsg : public dbsendmsg_callback {
	dbseltweetmsg() : dbsendmsg_callback(DBSM_SELTWEET) { }

	std::set<uint64_t> id_set;			//ids to select
	std::forward_list<dbrettweetdata> data;		//return data
};

struct dbinsertusermsg : public dbsendmsg {
	dbinsertusermsg() : dbsendmsg(DBSM_INSERTUSER) { }
	uint64_t id;
	std::string json;
	std::string cached_profile_img_url;
	time_t createtime;
	uint64_t lastupdate;
};

DECLARE_EVENT_TYPE(wxextDBCONN_NOTIFY, -1)

enum {
	wxDBCONNEVT_ID_TPANELTWEETLOAD = 1,
	wxDBCONNEVT_ID_DEBUGMSG,
};

struct dbconn : public wxEvtHandler {
	#ifdef __WINDOWS__
	HANDLE iocp;
	#else
	int pipefd;
	#endif
	bool isinited;
	sqlite3 *syncdb;
	dbpscache cache;
	dbiothread *th;

	dbconn() : isinited(0), th(0) { }
	~dbconn() { DeInit(); }
	void Init(const std::string &filename);
	void DeInit();
	void SendMessage(dbsendmsg *msg);
	void SendMessageOrAddToList(dbsendmsg *msg, dbsendmsg_list *msglist);

	void InsertNewTweet(const std::shared_ptr<tweet> &tobj, std::string statjson, dbsendmsg_list *msglist=0);
	void UpdateTweetDyn(const std::shared_ptr<tweet> &tobj, dbsendmsg_list *msglist=0);
	void InsertUser(const std::shared_ptr<userdatacontainer> &u, dbsendmsg_list *msglist=0);
	void AccountSync(sqlite3 *adb);
	void SyncWriteBackAllUsers(sqlite3 *adb);
	void SyncReadInAllUsers(sqlite3 *adb);
	void SyncInsertNewAccount(sqlite3 *adb, taccount &acc);
	void AccountIdListsSync(sqlite3 *adb);
	void OnTpanelTweetLoadFromDB(wxCommandEvent &event);
	void OnDBThreadDebugMsg(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};
