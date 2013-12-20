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

#include "univdefs.h"
#include "cfg.h"
#include "taccount.h"
#include "db.h"
#include "util.h"
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <sqlite3.h>

globconf gc;

genoptconf gcdefaults {
	{ wxT("qUfhKgogatGDPDeBaP1qBw"), 1},
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
	{ wxT("uZB@unbzuvup - T - t - Fm( - An)QF+u(' - 'BK(+#000080)'Unread'kb)NC"), 1},
	{ wxT("uZB@unbzuvup -> UZB@UnbzUvUp - T - t - Fm( - An)QF+u(' - 'BK(+#000080)'Unread'kb)NC"), 1},
	{ wxT("rZB@rnbzrvrp 'RT by' uZB@unbzuvup - T - t - Fm( - An)QF+u(' - 'BK(+#000080)'Unread'kb)Nc"), 1},
	{ wxT("uZB@unbzuvup - uN - ulNuDNuw"), 1},
	{ wxT("1"), 1 },
	{ wxT("0"), 1 },
	{ wxT("1"), 1 },
	{ wxT("1"), 1 },
	{ wxT("0"), 1 },
	{ wxT("XiXrXtXfXd"), 1 },
	{ wxT("XiXd"), 1 },
	{ wxT("XiXrXtXfXd"), 1 },
	{ wxT(""), 1 },
	{ wxT("+#320000"), 1 },
	{ wxT(""), 1 },
	{ wxT(""), 1 },		//this is initialised in InitCFGDefaults()
	{ wxT("10"), 1 },
	{ wxT("0"), 1 },
	{ wxT(""), 1 },
	{ wxT("0"), 1 },
	{ wxT(""), 1 },
	{ wxT(""), 1 },
};

void taccount::CFGWriteOut(DBWriteConfig &twfc) {
	twfc.SetDBIndex(dbindex);
	cfg.CFGWriteOutCurDir(twfc);
	twfc.WriteWX("conk", conk);
	twfc.WriteWX("cons", cons);
	twfc.WriteInt64("enabled", userenabled);
	twfc.WriteInt64("max_tweet_id", max_tweet_id);
	twfc.WriteInt64("max_recvdm_id", max_recvdm_id);
	twfc.WriteInt64("max_sentdm_id", max_sentdm_id);
	twfc.WriteInt64("max_mention_id", max_mention_id);
	twfc.WriteWX("dispname", dispname);
}

void taccount::CFGReadIn(DBReadConfig &twfc) {
	twfc.SetDBIndex(dbindex);
	cfg.CFGReadInCurDir(twfc, gc.cfg);
	twfc.Read("conk", &conk, wxT(""));
	twfc.Read("cons", &cons, wxT(""));
	twfc.ReadBool("enabled", &userenabled, false);
	twfc.ReadUInt64("max_tweet_id", &max_tweet_id, 0);
	twfc.ReadUInt64("max_recvdm_id", &max_recvdm_id, 0);
	twfc.ReadUInt64("max_sentdm_id", &max_sentdm_id, 0);
	twfc.ReadUInt64("max_mention_id", &max_mention_id, 0);
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
	persistentmediacache=(gcfg.persistentmediacache.val==wxT("1"));
	rtdisp=(gcfg.rtdisp.val==wxT("1"));
	assumementionistweet=(gcfg.assumementionistweet.val==wxT("1"));
	gcfg.imgthumbunhidetime.val.ToULong(&imgthumbunhidetime);
	setproxy=(gcfg.setproxy.val==wxT("1"));
	proxyurl=stdstrwx(gc.gcfg.proxyurl.val);
	proxyhttptunnel=(gcfg.proxyhttptunnel.val==wxT("1"));

	noproxylist = "";
	wxStringTokenizer tkn(gc.gcfg.noproxylist.val, wxT(",\r\n"), wxTOKEN_STRTOK);
	bool add_comma = false;
	while(tkn.HasMoreTokens()) {
		wxString token = tkn.GetNextToken();
		if(add_comma) noproxylist += ",";
		else add_comma = true;
		noproxylist += stdstrwx(token);
	}

	netiface=stdstrwx(gc.gcfg.netiface.val);
}

void genoptconf::CFGWriteOutCurDir(DBWriteConfig &twfc) {
	IterateConfs([&](const std::string &name, genopt genoptconf::*ptr) {
		(this->*ptr).CFGWriteOutCurDir(twfc, name.c_str());
	});
}
void genoptconf::CFGReadInCurDir(DBReadConfig &twfc, const genoptconf &parent) {
	IterateConfs([&](const std::string &name, genopt genoptconf::*ptr) {
		(this->*ptr).CFGReadInCurDir(twfc, name.c_str(), (parent.*ptr).val);
	});
}
void genoptconf::InheritFromParent(genoptconf &parent, bool ifunset) {
	IterateConfs([&](const std::string &name, genopt genoptconf::*ptr) {
		(this->*ptr).InheritFromParent(parent.*ptr, ifunset);
	});
}

void genoptconf::IterateConfs(std::function<void(const std::string &, genopt genoptconf::*)> f) {
	f("tokenk", &genoptconf::tokenk);
	f("tokens", &genoptconf::tokens);
	f("ssl", &genoptconf::ssl);
	f("userstreams", &genoptconf::userstreams);
	f("restinterval", &genoptconf::restinterval);
}

void genoptglobconf::CFGWriteOut(DBWriteConfig &twfc) {
	twfc.SetDBIndexGlobal();
	IterateConfs([&](const std::string &name, genopt genoptglobconf::*ptr) {
		(this->*ptr).CFGWriteOutCurDir(twfc, name.c_str());
	});
}
void genoptglobconf::CFGReadIn(DBReadConfig &twfc, const genoptglobconf &parent) {
	twfc.SetDBIndexGlobal();
	IterateConfs([&](const std::string &name, genopt genoptglobconf::*ptr) {
		(this->*ptr).CFGReadInCurDir(twfc, name.c_str(), (parent.*ptr).val);
	});
}

void genoptglobconf::IterateConfs(std::function<void(const std::string &, genopt genoptglobconf::*)> f) {
	f("userexpiretimemins", &genoptglobconf::userexpiretimemins);
	f("datetimeformat", &genoptglobconf::datetimeformat);
	f("maxpanelprofimgsize", &genoptglobconf::maxpanelprofimgsize);
	f("maxtweetsdisplayinpanel", &genoptglobconf::maxtweetsdisplayinpanel);
	f("tweetdispformat", &genoptglobconf::tweetdispformat);
	f("dmdispformat", &genoptglobconf::dmdispformat);
	f("rtdispformat", &genoptglobconf::rtdispformat);
	f("userdispformat", &genoptglobconf::userdispformat);
	f("mouseover_tweetdispformat", &genoptglobconf::mouseover_tweetdispformat);
	f("mouseover_dmdispformat", &genoptglobconf::mouseover_dmdispformat);
	f("mouseover_rtdispformat", &genoptglobconf::mouseover_rtdispformat);
	f("mouseover_userdispformat", &genoptglobconf::mouseover_userdispformat);
	f("highlight_colourdelta", &genoptglobconf::highlight_colourdelta);
	f("cachethumbs", &genoptglobconf::cachethumbs);
	f("cachemedia", &genoptglobconf::cachemedia);
	f("persistentmediacache", &genoptglobconf::persistentmediacache);
	f("rtdisp", &genoptglobconf::rtdisp);
	f("assumementionistweet", &genoptglobconf::assumementionistweet);
	f("mediasave_directorylist", &genoptglobconf::mediasave_directorylist);
	f("incoming_filter", &genoptglobconf::incoming_filter);
	f("imgthumbunhidetime", &genoptglobconf::imgthumbunhidetime);
	f("setproxy", &genoptglobconf::setproxy);
	f("proxyurl", &genoptglobconf::proxyurl);
	f("proxyhttptunnel", &genoptglobconf::proxyhttptunnel);
	f("noproxylist", &genoptglobconf::noproxylist);
	f("netiface", &genoptglobconf::netiface);
}

void genopt::CFGWriteOutCurDir(DBWriteConfig &twfc, const char *name) {
	if(enable) twfc.WriteWX(name, val);
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

void InitCFGDefaults() {
	//this is done because wxStandardPaths needs wxApp to be initialised first
	gcglobdefaults.mediasave_directorylist.val = wxStandardPaths::Get().GetDocumentsDir();
}
