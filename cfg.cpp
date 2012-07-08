#include "retcon.h"

genoptconf gcdefaults {
	//{ wxT("vlC5S1NCMHHg8mD1ghPRkA"), 1},
	{ wxT("qUfhKgogatGDPDeBaP1qBw"), 1},
	//{ wxT("3w4cIrHyI3IYUZW5O2ppcFXmsACDaENzFdLIKmEU84"), 1},
	{ wxT("dvJVLBwaJhmSyTxSUe7T8qz84lrydtFbxQ4snZxmYgM"), 1},
	{ wxT("1"), 1},
	{ wxT("0"), 1},	//set this to 1 later
	{ wxT(""), 1},	//fill this in later
};

genoptglobconf gcglobdefaults {
	{ wxT("90"), 1},
};

taccount::taccount(genoptconf *incfg) {
	if(incfg) {
		cfg=*incfg;
	}
	CFGParamConv();
	enabled=false;
	verifycreddone=false;
	verifycredinprogress=false;
}

void taccount::CFGWriteOut(wxConfigBase &twfc) {
	//wxString oldpath=twfc.GetPath();
	twfc.SetPath(wxT("/accounts/") + name);
	cfg.CFGWriteOutCurDir(twfc);
	twfc.Write(wxT("conk"), conk);
	twfc.Write(wxT("cons"), cons);
	twfc.Write(wxT("enabled"), enabled);
	wxString t_max_tweet_id;
	t_max_tweet_id.Printf("%" wxLongLongFmtSpec "d", max_tweet_id);
	twfc.Write(wxT("max_tweet_id"), t_max_tweet_id);
	//twfc.SetPath(oldpath);
}
void taccount::CFGReadIn(wxConfigBase &twfc) {
	//wxString oldpath=twfc.GetPath();
	twfc.SetPath(wxT("/accounts/") + name);
	cfg.CFGReadInCurDir(twfc, gc.cfg);
	twfc.Read(wxT("conk"), &conk, wxT(""));
	twfc.Read(wxT("cons"), &cons, wxT(""));
	twfc.Read(wxT("enabled"), &enabled, wxT(""));
	wxString t_max_tweet_id;
	twfc.Read(wxT("max_tweet_id"), &t_max_tweet_id, wxT("0"));
	t_max_tweet_id.ToULongLong(&max_tweet_id);
	CFGParamConv();
	//twfc.SetPath(oldpath);
}
void taccount::CFGParamConv() {
	ssl=(cfg.ssl.val==wxT("1"));
	userstreams=(cfg.userstreams.val==wxT("1"));
}
void globconf::CFGWriteOut(wxConfigBase &twfc) {
	twfc.SetPath(wxT("/"));
	cfg.CFGWriteOutCurDir(twfc);
	gcfg.CFGWriteOut(twfc);
}
void globconf::CFGReadIn(wxConfigBase &twfc) {
	twfc.SetPath(wxT("/"));
	cfg.CFGReadInCurDir(twfc, gcdefaults);
	gcfg.CFGReadIn(twfc, gcglobdefaults);
	CFGParamConv();
}
void globconf::CFGParamConv() {
	gcfg.userexpiretimemins.val.ToULong(&userexpiretime);
	userexpiretime*=60;
}

void genoptconf::CFGWriteOutCurDir(wxConfigBase &twfc) {
	tokenk.CFGWriteOutCurDir(twfc, wxT("tokenk"));
	tokens.CFGWriteOutCurDir(twfc, wxT("tokens"));
	ssl.CFGWriteOutCurDir(twfc, wxT("ssl"));
	userstreams.CFGWriteOutCurDir(twfc, wxT("userstreams"));
	restinterval.CFGWriteOutCurDir(twfc, wxT("restinterval"));
}
void genoptconf::CFGReadInCurDir(wxConfigBase &twfc, const genoptconf &parent) {
	tokenk.CFGReadInCurDir(twfc, wxT("tokenk"), parent.tokenk.val);
	tokens.CFGReadInCurDir(twfc, wxT("tokens"), parent.tokens.val);
	ssl.CFGReadInCurDir(twfc, wxT("ssl"), parent.ssl.val);
	userstreams.CFGReadInCurDir(twfc, wxT("userstreams"), parent.userstreams.val);
	restinterval.CFGReadInCurDir(twfc, wxT("restinterval"), parent.restinterval.val);
}

void genoptglobconf::CFGWriteOut(wxConfigBase &twfc) {
	userexpiretimemins.CFGWriteOutCurDir(twfc, wxT("/userexpiretimemins"));
}
void genoptglobconf::CFGReadIn(wxConfigBase &twfc, const genoptglobconf &parent) {
	userexpiretimemins.CFGReadInCurDir(twfc, wxT("/userexpiretimemins"), parent.userexpiretimemins.val);
}

void genopt::CFGWriteOutCurDir(wxConfigBase &twfc, const wxString &name) {
	twfc.Write(name, enable?val:wxT(""));
}
void genopt::CFGReadInCurDir(wxConfigBase &twfc, const wxString &name, const wxString &parent) {
	enable=twfc.Read(name, &val, parent);
	if(val.IsEmpty()) {
		val=parent;
		enable=false;
	}
}

void ReadAllCFGIn(wxConfigBase &twfc, globconf &gc, std::list<std::shared_ptr<taccount>> &alist) {
	gc.CFGReadIn(twfc);

	twfc.SetPath(wxT("/accounts/"));
	alist.clear();

	wxString str;
	long dummy;
	bool bCont=twfc.GetFirstGroup(str, dummy);
	while(bCont) {
		std::shared_ptr<taccount> ta(new(taccount));
		ta->name=str;
		alist.push_back(ta);
		bCont=twfc.GetNextGroup(str, dummy);
	}
	for(auto it=alist.begin() ; it != alist.end(); it++ ) {
		(*it)->CFGReadIn(twfc);
	}
}

void WriteAllCFGOut(wxConfigBase &twfc, globconf &gc, std::list<std::shared_ptr<taccount>> &alist) {
	gc.CFGWriteOut(twfc);
	twfc.Write(wxT("LastUpdate"), wxGetUTCTime());

	twfc.DeleteGroup(wxT("/accounts/"));
	for(auto it=alist.begin() ; it != alist.end(); it++ ) (*it)->CFGWriteOut(twfc);
}
