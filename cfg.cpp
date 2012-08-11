#include "retcon.h"

genoptconf gcdefaults {
	//{ wxT("vlC5S1NCMHHg8mD1ghPRkA"), 1},
	{ wxT("qUfhKgogatGDPDeBaP1qBw"), 1},
	//{ wxT("3w4cIrHyI3IYUZW5O2ppcFXmsACDaENzFdLIKmEU84"), 1},
	{ wxT("dvJVLBwaJhmSyTxSUe7T8qz84lrydtFbxQ4snZxmYgM"), 1},
	{ wxT("1"), 1},
	{ wxT("1"), 1},
	{ wxT("300"), 1},
};

genoptglobconf gcglobdefaults {
	{ wxT("90"), 1},
	{ wxT("%Y-%m-%d %H:%M:%S"), 1},
	{ wxT("48"), 1},
	{ wxT("40"), 1},
	{ wxT("B@unb - T - t - FNC"), 1},
	{ wxT("B@unb -> B@Unb - T - t - FNC"), 1},
	{ wxT("1"), 1 },
	{ wxT("0"), 1 },
};

taccount::taccount(genoptconf *incfg) {
	if(incfg) {
		cfg.InheritFromParent(*incfg);
		CFGParamConv();
	}
	enabled=false;
	verifycreddone=false;
	verifycredinprogress=false;
	active=false;
	max_tweet_id=max_recvdm_id=max_sentdm_id=0;
}



void taccount::CFGWriteOut(DBWriteConfig &twfc) {
	twfc.SetDBIndex(dbindex);
	cfg.CFGWriteOutCurDir(twfc);
	twfc.Write("conk", conk);
	twfc.Write("cons", cons);
	twfc.WriteInt64("enabled", enabled);
	twfc.WriteInt64("max_tweet_id", max_tweet_id);
	twfc.WriteInt64("max_recvdm_id", max_recvdm_id);
	twfc.WriteInt64("max_sentdm_id", max_sentdm_id);
	twfc.Write("dispname", dispname);
}

void taccount::CFGReadIn(DBReadConfig &twfc) {
	twfc.SetDBIndex(dbindex);
	cfg.CFGReadInCurDir(twfc, gc.cfg);
	twfc.Read("conk", &conk, wxT(""));
	twfc.Read("cons", &cons, wxT(""));
	twfc.ReadBool("enabled", &enabled, false);
	twfc.ReadUInt64("max_tweet_id", &max_tweet_id, 0);
	twfc.ReadUInt64("max_recvdm_id", &max_recvdm_id, 0);
	twfc.ReadUInt64("max_sentdm_id", &max_sentdm_id, 0);
	twfc.Read("dispname", &dispname, wxT(""));
	CFGParamConv();
}
void taccount::CFGParamConv() {
	ssl=(cfg.ssl.val==wxT("1"));
	userstreams=(cfg.userstreams.val==wxT("1"));
	cfg.restinterval.val.ToULong(&restinterval);
}
void globconf::CFGWriteOut(DBWriteConfig &twfc) {
	twfc.SetDBIndexGlobal();
	cfg.CFGWriteOutCurDir(twfc);
	gcfg.CFGWriteOut(twfc);
}
void globconf::CFGReadIn(DBReadConfig &twfc) {
	twfc.SetDBIndexGlobal();
	cfg.CFGReadInCurDir(twfc, gcdefaults);
	gcfg.CFGReadIn(twfc, gcglobdefaults);
	CFGParamConv();
}
void globconf::CFGParamConv() {
	gcfg.userexpiretimemins.val.ToULong(&userexpiretime);
	userexpiretime*=60;
	gcfg.maxpanelprofimgsize.val.ToULong(&maxpanelprofimgsize);
	gcfg.maxtweetsdisplayinpanel.val.ToULong(&maxtweetsdisplayinpanel);
	cachethumbs=(gcfg.cachethumbs.val==wxT("1"));
	cachemedia=(gcfg.cachemedia.val==wxT("1"));
}

void genoptconf::CFGWriteOutCurDir(DBWriteConfig &twfc) {
	tokenk.CFGWriteOutCurDir(twfc, "tokenk");
	tokens.CFGWriteOutCurDir(twfc, "tokens");
	ssl.CFGWriteOutCurDir(twfc, "ssl");
	userstreams.CFGWriteOutCurDir(twfc, "userstreams");
	restinterval.CFGWriteOutCurDir(twfc, "restinterval");
}
void genoptconf::CFGReadInCurDir(DBReadConfig &twfc, const genoptconf &parent) {
	tokenk.CFGReadInCurDir(twfc, "tokenk", parent.tokenk.val);
	tokens.CFGReadInCurDir(twfc, "tokens", parent.tokens.val);
	ssl.CFGReadInCurDir(twfc, "ssl", parent.ssl.val);
	userstreams.CFGReadInCurDir(twfc, "userstreams", parent.userstreams.val);
	restinterval.CFGReadInCurDir(twfc, "restinterval", parent.restinterval.val);
}
void genoptconf::InheritFromParent(genoptconf &parent, bool ifunset) {
	tokenk.InheritFromParent(parent.tokenk, ifunset);
	tokens.InheritFromParent(parent.tokens, ifunset);
	ssl.InheritFromParent(parent.ssl, ifunset);
	userstreams.InheritFromParent(parent.userstreams, ifunset);
	restinterval.InheritFromParent(parent.restinterval, ifunset);
}

void genoptglobconf::CFGWriteOut(DBWriteConfig &twfc) {
	twfc.SetDBIndexGlobal();
	userexpiretimemins.CFGWriteOutCurDir(twfc, "userexpiretimemins");
	datetimeformat.CFGWriteOutCurDir(twfc, "datetimeformat");
	maxpanelprofimgsize.CFGWriteOutCurDir(twfc, "maxpanelprofimgsize");
	maxtweetsdisplayinpanel.CFGWriteOutCurDir(twfc, "maxtweetsdisplayinpanel");
	tweetdispformat.CFGWriteOutCurDir(twfc, "tweetdispformat");
	dmdispformat.CFGWriteOutCurDir(twfc, "dmdispformat");
	cachethumbs.CFGWriteOutCurDir(twfc, "cachethumbs");
	cachemedia.CFGWriteOutCurDir(twfc, "cachemedia");
}
void genoptglobconf::CFGReadIn(DBReadConfig &twfc, const genoptglobconf &parent) {
	twfc.SetDBIndexGlobal();
	userexpiretimemins.CFGReadInCurDir(twfc, "userexpiretimemins", parent.userexpiretimemins.val);
	datetimeformat.CFGReadInCurDir(twfc, "datetimeformat", parent.datetimeformat.val);
	maxpanelprofimgsize.CFGReadInCurDir(twfc, "maxpanelprofimgsize", parent.maxpanelprofimgsize.val);
	maxtweetsdisplayinpanel.CFGReadInCurDir(twfc, "maxtweetsdisplayinpanel", parent.maxtweetsdisplayinpanel.val);
	tweetdispformat.CFGReadInCurDir(twfc, "tweetdispformat", parent.tweetdispformat.val);
	dmdispformat.CFGReadInCurDir(twfc, "dmdispformat", parent.dmdispformat.val);
	cachethumbs.CFGReadInCurDir(twfc, "cachethumbs", parent.cachethumbs.val);
	cachemedia.CFGReadInCurDir(twfc, "cachemedia", parent.cachemedia.val);
}

void genopt::CFGWriteOutCurDir(DBWriteConfig &twfc, const char *name) {
	if(enable) twfc.Write(name, val);
	else twfc.Delete(name);
}
void genopt::CFGReadInCurDir(DBReadConfig &twfc, const char *name, const wxString &parent) {
	enable=twfc.Read(name, &val, parent);
	if(val.IsEmpty()) {
		val=parent;
		enable=false;
	}
}
void genopt::InheritFromParent(genopt &parent, bool ifunset) {
	if(ifunset && enable) return;
	else {
		enable=0;
		val=parent.val;
	}
}

void ReadAllCFGIn(sqlite3 *db, globconf &gc, std::list<std::shared_ptr<taccount>> &alist) {
	DBReadConfig twfc(db);
	gc.CFGReadIn(twfc);

	for(auto it=alist.begin(); it != alist.end(); it++ ) {
		(*it)->CFGReadIn(twfc);
	}
}

void WriteAllCFGOut(sqlite3 *db, globconf &gc, std::list<std::shared_ptr<taccount>> &alist) {
	DBWriteConfig twfc(db);
	twfc.DeleteAll();
	gc.CFGWriteOut(twfc);
	twfc.SetDBIndexGlobal();
	twfc.WriteInt64("LastUpdate", (sqlite3_int64) time(0));

	for(auto it=alist.begin() ; it != alist.end(); ++it ) (*it)->CFGWriteOut(twfc);
}

void AllUsersInheritFromParentIfUnset() {
	for(auto it=alist.begin() ; it != alist.end(); ++it ) (*it)->cfg.InheritFromParent(gc.cfg, true);
}
