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
	DBPSC_INSERTRBFSP,
	DBPSC_SELMEDIA,

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
	DBSM_INSERTACC,
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
	uint64_t id, user1, user2, timestamp;
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

struct dbseltweetmsg : public dbsendmsg_callback {
	dbseltweetmsg() : dbsendmsg_callback(DBSM_SELTWEET) { }

	std::set<uint64_t> id_set;			//ids to select
	std::forward_list<dbrettweetdata> data;		//return data
	std::forward_list<dbretmediadata> media_data;	//return data
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

	std::string name;				//account name
	std::string dispname;				//account name
	uint64_t userid;
	unsigned int dbindex;				//return data
};

DECLARE_EVENT_TYPE(wxextDBCONN_NOTIFY, -1)

enum {
	wxDBCONNEVT_ID_TPANELTWEETLOAD = 1,
	wxDBCONNEVT_ID_DEBUGMSG,
	wxDBCONNEVT_ID_INSERTNEWACC,
};

struct dbconn : public wxEvtHandler {
	#ifdef __WINDOWS__
	HANDLE iocp;
	#else
	int pipefd;
	#endif
	bool isinited;
	sqlite3 *syncdb;
	dbiothread *th;
	dbpscache cache;

	dbconn() : isinited(0), th(0) { }
	~dbconn() { DeInit(); }
	bool Init(const std::string &filename);
	void DeInit();
	void SendMessage(dbsendmsg *msg);
	void SendMessageOrAddToList(dbsendmsg *msg, dbsendmsg_list *msglist);

	void InsertNewTweet(const std::shared_ptr<tweet> &tobj, std::string statjson, dbsendmsg_list *msglist=0);
	void UpdateTweetDyn(const std::shared_ptr<tweet> &tobj, dbsendmsg_list *msglist=0);
	void InsertUser(const std::shared_ptr<userdatacontainer> &u, dbsendmsg_list *msglist=0);
	void AccountSync(sqlite3 *adb);
	void SyncWriteBackAllUsers(sqlite3 *adb);
	void SyncReadInAllUsers(sqlite3 *adb);
	void AccountIdListsSync(sqlite3 *adb);
	void SyncWriteOutRBFSs(sqlite3 *adb);
	void SyncReadInRBFSs(sqlite3 *adb);
	void OnTpanelTweetLoadFromDB(wxCommandEvent &event);
	void OnDBThreadDebugMsg(wxCommandEvent &event);
	void OnDBNewAccountInsert(wxCommandEvent &event);

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
	void Write(const char *name, const char *strval);
	void Write(const char *name, const wxString &strval) { Write(name, strval.ToUTF8()); }
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
