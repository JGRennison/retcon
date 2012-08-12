#include <sqlite3.h>

struct genopt {
	wxString val;
	bool enable;
	void CFGWriteOutCurDir(DBWriteConfig &twfc, const char *name);
	void CFGReadInCurDir(DBReadConfig &twfc, const char *name, const wxString &parent);
	void InheritFromParent(genopt &parent, bool ifunset=false);
};

struct genoptconf {
	genopt tokenk;
	genopt tokens;
	genopt ssl;
	genopt userstreams;
	genopt restinterval;
	void CFGWriteOutCurDir(DBWriteConfig &twfc);
	void CFGReadInCurDir(DBReadConfig &twfc, const genoptconf &parent);
	void InheritFromParent(genoptconf &parent, bool ifunset=false);
};

struct genoptglobconf {
	genopt userexpiretimemins;
	genopt datetimeformat;
	genopt maxpanelprofimgsize;
	genopt maxtweetsdisplayinpanel;
	genopt tweetdispformat;
	genopt dmdispformat;
	genopt rtdispformat;
	genopt cachethumbs;
	genopt cachemedia;
	genopt persistentmediacache;
	genopt rtdisp;
	void CFGWriteOut(DBWriteConfig &twfc);
	void CFGReadIn(DBReadConfig &twfc, const genoptglobconf &parent);
};

struct globconf {
	genoptconf cfg;
	genoptglobconf gcfg;

	unsigned long userexpiretime;
	unsigned long maxpanelprofimgsize;
	unsigned long maxtweetsdisplayinpanel;
	bool cachethumbs;
	bool cachemedia;
	bool persistentmediacache;
	bool rtdisp;

	void CFGWriteOut(DBWriteConfig &twfc);
	void CFGReadIn(DBReadConfig &twfc);
	void CFGParamConv();
};

void ReadAllCFGIn(sqlite3 *db, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);
void WriteAllCFGOut(sqlite3 *db, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);
void AllUsersInheritFromParentIfUnset();

extern globconf gc;
extern genoptconf gcdefaults;
extern genoptglobconf gcglobdefaults;
