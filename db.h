#include <sqlite3.h>

typedef enum {
	DBPSC_START=0,

	DBPSC_INSTWEET=0,
	DBPSC_UPDTWEET,
	DBPSC_BEGIN,
	DBPSC_COMMIT,
	DBPSC_INSUSER,
	DBPSC_GETUSER,

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
} DBSM_TYPE;

struct dbsendmsg {
	DBSM_TYPE type;

	dbsendmsg(DBSM_TYPE type_) : type(type_) { }
};

struct dbinserttweetmsg : public dbsendmsg {
	dbinserttweetmsg() : dbsendmsg(DBSM_INSERTTWEET) { }

	std::string statjson;
	std::string dynjson;
	uint64_t id, user1, user2, timestamp;
	unsigned int flags;
};

struct dbupdatetweetmsg : public dbsendmsg {
	dbupdatetweetmsg() : dbsendmsg(DBSM_UPDATETWEET) { }

	std::string dynjson;
	uint64_t id;
	unsigned int flags;
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

	void InsertNewTweet(const std::shared_ptr<tweet> &tobj, std::string statjson);
	void UpdateTweetDyn(const std::shared_ptr<tweet> &tobj);
	void AccountSync(sqlite3 *adb);
	void SyncWriteBackAllUsers(sqlite3 *adb);
	void SyncReadInAllUsers(sqlite3 *adb);
};
