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
#include <wx/stdpaths.h>
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
	rtdispformat.CFGWriteOutCurDir(twfc, "rtdispformat");
	userdispformat.CFGWriteOutCurDir(twfc, "userdispformat");
	mouseover_tweetdispformat.CFGWriteOutCurDir(twfc, "mouseover_tweetdispformat");
	mouseover_dmdispformat.CFGWriteOutCurDir(twfc, "mouseover_dmdispformat");
	mouseover_rtdispformat.CFGWriteOutCurDir(twfc, "mouseover_rtdispformat");
	mouseover_userdispformat.CFGWriteOutCurDir(twfc, "mouseover_userdispformat");
	highlight_colourdelta.CFGWriteOutCurDir(twfc, "highlight_colourdelta");
	cachethumbs.CFGWriteOutCurDir(twfc, "cachethumbs");
	cachemedia.CFGWriteOutCurDir(twfc, "cachemedia");
	persistentmediacache.CFGWriteOutCurDir(twfc, "persistentmediacache");
	rtdisp.CFGWriteOutCurDir(twfc, "rtdisp");
	assumementionistweet.CFGWriteOutCurDir(twfc, "assumementionistweet");
	mediasave_directorylist.CFGWriteOutCurDir(twfc, "mediasave_directorylist");
	incoming_filter.CFGWriteOutCurDir(twfc, "incoming_filter");
	imgthumbunhidetime.CFGWriteOutCurDir(twfc, "imgthumbunhidetime");
}
void genoptglobconf::CFGReadIn(DBReadConfig &twfc, const genoptglobconf &parent) {
	twfc.SetDBIndexGlobal();
	userexpiretimemins.CFGReadInCurDir(twfc, "userexpiretimemins", parent.userexpiretimemins.val);
	datetimeformat.CFGReadInCurDir(twfc, "datetimeformat", parent.datetimeformat.val);
	maxpanelprofimgsize.CFGReadInCurDir(twfc, "maxpanelprofimgsize", parent.maxpanelprofimgsize.val);
	maxtweetsdisplayinpanel.CFGReadInCurDir(twfc, "maxtweetsdisplayinpanel", parent.maxtweetsdisplayinpanel.val);
	tweetdispformat.CFGReadInCurDir(twfc, "tweetdispformat", parent.tweetdispformat.val);
	dmdispformat.CFGReadInCurDir(twfc, "dmdispformat", parent.dmdispformat.val);
	rtdispformat.CFGReadInCurDir(twfc, "rtdispformat", parent.rtdispformat.val);
	userdispformat.CFGReadInCurDir(twfc, "userdispformat", parent.userdispformat.val);
	mouseover_tweetdispformat.CFGReadInCurDir(twfc, "mouseover_tweetdispformat", parent.mouseover_tweetdispformat.val);
	mouseover_dmdispformat.CFGReadInCurDir(twfc, "mouseover_dmdispformat", parent.mouseover_dmdispformat.val);
	mouseover_rtdispformat.CFGReadInCurDir(twfc, "mouseover_rtdispformat", parent.mouseover_rtdispformat.val);
	mouseover_userdispformat.CFGReadInCurDir(twfc, "mouseover_userdispformat", parent.mouseover_userdispformat.val);
	highlight_colourdelta.CFGReadInCurDir(twfc, "highlight_colourdelta", parent.highlight_colourdelta.val);
	cachethumbs.CFGReadInCurDir(twfc, "cachethumbs", parent.cachethumbs.val);
	cachemedia.CFGReadInCurDir(twfc, "cachemedia", parent.cachemedia.val);
	persistentmediacache.CFGReadInCurDir(twfc, "persistentmediacache", parent.persistentmediacache.val);
	rtdisp.CFGReadInCurDir(twfc, "rtdisp", parent.rtdisp.val);
	assumementionistweet.CFGReadInCurDir(twfc, "assumementionistweet", parent.assumementionistweet.val);
	mediasave_directorylist.CFGReadInCurDir(twfc, "mediasave_directorylist", parent.mediasave_directorylist.val);
	incoming_filter.CFGReadInCurDir(twfc, "incoming_filter", parent.incoming_filter.val);
	imgthumbunhidetime.CFGReadInCurDir(twfc, "imgthumbunhidetime", parent.imgthumbunhidetime.val);
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
