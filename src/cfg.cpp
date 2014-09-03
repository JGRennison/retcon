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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "cfg.h"
#include "taccount.h"
#include "db-cfg.h"
#include "util.h"
#include "log.h"
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>

globconf gc;

genoptconf gcdefaults {
	{ wxT("qUfhKgogatGDPDeBaP1qBw"), 1},
	{ wxT("dvJVLBwaJhmSyTxSUe7T8qz84lrydtFbxQ4snZxmYgM"), 1},
	{ wxT("1"), 1},
	{ wxT("1"), 1},
	{ wxT("300"), 1},
	{ wxT("1"), 1},
};


#define CFGDEFAULT_userexpiretimemins                       wxT("90")
#define CFGDEFAULT_datetimeformat                           wxT("%Y-%m-%d %H:%M:%S")
#define CFGDEFAULT_datetimeisutc                            wxT("0")
#define CFGDEFAULT_maxpanelprofimgsize                      wxT("48")
#define CFGDEFAULT_maxtweetsdisplayinpanel                  wxT("40")
#define CFGDEFAULT_format_default_num                       wxT("1")
#define CFGDEFAULT_tweetdispformat                          wxT("")    // these are dealt with in CFGParamConv()
#define CFGDEFAULT_dmdispformat                             wxT("")    //
#define CFGDEFAULT_rtdispformat                             wxT("")    //
#define CFGDEFAULT_userdispformat                           wxT("")    //
#define CFGDEFAULT_cachethumbs                              wxT("1")
#define CFGDEFAULT_cachemedia                               wxT("0")
#define CFGDEFAULT_rtdisp                                   wxT("1")
#define CFGDEFAULT_assumementionistweet                     wxT("0")
#define CFGDEFAULT_mouseover_tweetdispformat                wxT("XmXiXrXtXfXd")
#define CFGDEFAULT_mouseover_dmdispformat                   wxT("XiXd")
#define CFGDEFAULT_mouseover_rtdispformat                   wxT("XmXiXrXtXfXd")
#define CFGDEFAULT_mouseover_userdispformat                 wxT("")
#define CFGDEFAULT_highlight_colourdelta                    wxT("+#320000")
#define CFGDEFAULT_mediasave_directorylist                  wxT("")    //this is initialised in InitCFGDefaults()
#define CFGDEFAULT_incoming_filter                          wxT("")
#define CFGDEFAULT_alltweet_filter                          wxT("")
#define CFGDEFAULT_imgthumbunhidetime                       wxT("10")
#define CFGDEFAULT_setproxy                                 wxT("0")
#define CFGDEFAULT_proxyurl                                 wxT("")
#define CFGDEFAULT_proxyhttptunnel                          wxT("0")
#define CFGDEFAULT_noproxylist                              wxT("")
#define CFGDEFAULT_netiface                                 wxT("")
#define CFGDEFAULT_inlinereplyloadcount                     wxT("1")
#define CFGDEFAULT_inlinereplyloadmorecount                 wxT("3")
#define CFGDEFAULT_showdeletedtweetsbydefault               wxT("0")
#define CFGDEFAULT_markowntweetsasread                      wxT("1")
#define CFGDEFAULT_markdeletedtweetsasread                  wxT("1")
#define CFGDEFAULT_mediawinscreensizewidthreduction         wxT("0")
#define CFGDEFAULT_mediawinscreensizeheightreduction        wxT("0")
#define CFGDEFAULT_autoloadthumb_thumb                      wxT("0")
#define CFGDEFAULT_autoloadthumb_full                       wxT("0")
#define CFGDEFAULT_disploadthumb_thumb                      wxT("1")
#define CFGDEFAULT_disploadthumb_full                       wxT("1")
#define CFGDEFAULT_dispthumbs                               wxT("1")
#define CFGDEFAULT_hideallthumbs                            wxT("0")
#define CFGDEFAULT_loadhiddenthumbs                         wxT("0")
#define CFGDEFAULT_threadpoollimit                          wxT("8")
#define CFGDEFAULT_mediacachesavedays                       wxT("7")
#define CFGDEFAULT_profimgcachesavedays                     wxT("14")
#define CFGDEFAULT_showunhighlightallbtn                    wxT("1")
#define CFGDEFAULT_asyncstatewritebackintervalmins          wxT("30")
#define CFGDEFAULT_mousewheelscrollspeed                    wxT("50")
#define CFGDEFAULT_linescrollspeed                          wxT("20")
#define CFGDEFAULT_askuseraccsettingsonnewacc               wxT("0")

genoptglobconf gcglobdefaults {
#define CFGTEMPL(x) { CFGDEFAULT_##x, 1},
#define CFGTEMPL_UL(x) CFGTEMPL(x)
#define CFGTEMPL_L(x) CFGTEMPL(x)
#define CFGTEMPL_BOOL(x) CFGTEMPL(x)
	CFGTEMPL_EXPAND
#undef CFGTEMPL
#undef CFGTEMPL_UL
#undef CFGTEMPL_L
#undef CFGTEMPL_BOOL
};

const format_set format_set_short {
	{ wxT("uZB@unbzuvup  tQF+u(' - 'BK(+#000080)'Unread'kb)NC"), 0 },
	{ wxT("uZB@unbzuvup \x2192 UZB@UnbzUvUp  tQF+u(' - 'BK(+#000080)'Unread'kb)NC"), 0 },
	{ wxT("rZB@rnbzrvrp 'RT by' uZB@unbzuvup  tQF+u(' - 'BK(+#000080)'Unread'kb)Nc"), 0 },
	{ wxT("uZB@unbzuvup  uNNuD"), 0 },
};
const format_set format_set_medium {
	{ wxT("uZB@unbzuvup  tRfp( -Rp( 'R'Rn)fp( 'F'fn))m( - An)QF+u(' - 'BK(+#000080)'Unread'kb)NC"), 0 },
	{ wxT("uZB@unbzuvup \x2192 UZB@UnbzUvUp  tQF+u(' - 'BK(+#000080)'Unread'kb)NC"), 0 },
	{ wxT("rZB@rnbzrvrp 'RT by' uZB@unbzuvup  tRfp( -Rp( 'R'Rn)fp( 'F'fn))m( - An)QF+u(' - 'BK(+#000080)'Unread'kb)Nc"), 0 },
	{ wxT("uZB@unbzuvup  uN - ulNuD"), 0 },
};
const format_set format_set_long {
	{ wxT("uZB@unbzuvup  T - t - FRfp( -Rp( 'R'Rn)fp( 'F'fn))m( - An)Swp( - Sl)QF+u(' - 'BK(+#000080)'Unread'kb)NC"), 0 },
	{ wxT("uZB@unbzuvup \x2192 UZB@UnbzUvUp  T - t - FSwp( - Sl)QF+u(' - 'BK(+#000080)'Unread'kb)NC"), 0 },
	{ wxT("rZB@rnbzrvrp 'RT by' uZB@unbzuvup  T - t - FRfp( -Rp( 'R'Rn)fp( 'F'fn))m( - An)Swp( - Sl)QF+u(' - 'BK(+#000080)'Unread'kb)Nc"), 0 },
	{ wxT("uZB@unbzuvup  uN - ulNuDNuw"), 0 },
};

const format_set &IndexToFormatSet(unsigned long fdn) {
	const format_set *current = &format_set_short;
	if(fdn == 1) current = &format_set_medium;
	else if(fdn == 2) current = &format_set_long;

	return *current;
}

void taccount_cfg::CFGWriteOut(DBWriteConfig &twfc) const {
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

void taccount_cfg::CFGReadInBase(DBReadConfig &twfc) {
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
}

void taccount_cfg::UnShareStrings() {
	UnShare(conk);
	UnShare(cons);
	UnShare(dispname);
	cfg.UnShareStrings();
}

void taccount::CFGReadIn(DBReadConfig &twfc) {
	taccount_cfg::CFGReadInBase(twfc);
	CFGParamConv();
}

void taccount::CFGParamConv() {
	ssl = (cfg.ssl.val == wxT("1"));
	userstreams = (cfg.userstreams.val == wxT("1"));
	cfg.restinterval.val.ToULong(&restinterval);
	unsigned long srm_tmp;
	cfg.stream_reply_mode.val.ToULong(&srm_tmp);
	stream_reply_mode = static_cast<SRM>(srm_tmp);
}

void globconf_base::CFGWriteOut(DBWriteConfig &twfc) {
	twfc.SetDBIndexGlobal();
	cfg.CFGWriteOutCurDir(twfc);
	gcfg.CFGWriteOut(twfc);
}

void globconf_base::UnShareStrings() {
	cfg.UnShareStrings();
	gcfg.UnShareStrings();
}

void globconf::CFGReadIn(DBReadConfig &twfc) {
	twfc.SetDBIndexGlobal();
	cfg.CFGReadInCurDir(twfc, gcdefaults);
	gcfg.CFGReadIn(twfc, gcglobdefaults);
	CFGParamConv();
}

void globconf::CFGParamConv() {
#define CFGTEMPL(x)
#define CFGTEMPL_UL(x) gcfg.x.val.ToULong(&x);
#define CFGTEMPL_L(x) gcfg.x.val.ToLong(&x);
#define CFGTEMPL_BOOL(x) x = (gcfg.x.val == wxT("1"));
	CFGTEMPL_EXPAND
#undef CFGTEMPL
#undef CFGTEMPL_UL
#undef CFGTEMPL_L
#undef CFGTEMPL_BOOL

	gcfg.userexpiretimemins.val.ToULong(&userexpiretime);
	userexpiretime *= 60;

	proxyurl = stdstrwx(gc.gcfg.proxyurl.val);

	noproxylist = "";
	wxStringTokenizer tkn(gc.gcfg.noproxylist.val, wxT(",\r\n"), wxTOKEN_STRTOK);
	bool add_comma = false;
	while(tkn.HasMoreTokens()) {
		wxString token = tkn.GetNextToken();
		if(add_comma) noproxylist += ",";
		else add_comma = true;
		noproxylist += stdstrwx(token);
	}

	netiface = stdstrwx(gc.gcfg.netiface.val);

	const format_set &current_format_set = IndexToFormatSet(format_default_num);
	auto do_format_param = [&](genopt &targ, const genopt &def) {
		if(targ.val.IsEmpty() || !targ.enable) {
			targ = def;
		}
	};
	do_format_param(gcfg.tweetdispformat, current_format_set.tweetdispformat);
	do_format_param(gcfg.dmdispformat, current_format_set.dmdispformat);
	do_format_param(gcfg.rtdispformat, current_format_set.rtdispformat);
	do_format_param(gcfg.userdispformat, current_format_set.userdispformat);
}

void genoptconf::CFGWriteOutCurDir(DBWriteConfig &twfc) const {
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
	f("stream_reply_mode", &genoptconf::stream_reply_mode);
}

void genoptconf::UnShareStrings() {
	IterateConfs([this](const std::string &name, genopt genoptconf::* ref) {
		UnShare((this->*ref).val);
	});
}

void genoptglobconf::CFGWriteOut(DBWriteConfig &twfc) const {
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
#define CFGTEMPL(x) f(#x, &genoptglobconf::x);
#define CFGTEMPL_UL(x) CFGTEMPL(x)
#define CFGTEMPL_L(x) CFGTEMPL(x)
#define CFGTEMPL_BOOL(x) CFGTEMPL(x)
	CFGTEMPL_EXPAND
#undef CFGTEMPL
#undef CFGTEMPL_UL
#undef CFGTEMPL_L
#undef CFGTEMPL_BOOL
}

void genoptglobconf::UnShareStrings() {
	IterateConfs([this](const std::string &name, genopt genoptglobconf::* ref) {
		UnShare((this->*ref).val);
	});
}

void genopt::CFGWriteOutCurDir(DBWriteConfig &twfc, const char *name) const {
	if(enable) twfc.WriteWX(name, val);
	else twfc.Delete(name);
}
void genopt::CFGReadInCurDir(DBReadConfig &twfc, const char *name, const wxString &parent) {
	enable = twfc.Read(name, &val, parent);
	if(val.IsEmpty()) {
		val = parent;
		enable = false;
	}
}
void genopt::InheritFromParent(genopt &parent, bool ifunset) {
	if(ifunset && enable) return;
	else {
		enable = false;
		val = parent.val;
	}
}

void ReadAllCFGIn(sqlite3 *db, globconf &lgc, std::list<std::shared_ptr<taccount>> &lalist) {
	LogMsg(LOGT::DBTRACE, "dbconn::ReadAllCFGIn start");
	DBReadConfig twfc(db);
	lgc.CFGReadIn(twfc);

	for(auto &it : lalist) it->CFGReadIn(twfc);
	LogMsg(LOGT::DBTRACE, "dbconn::ReadAllCFGIn end");
}

//This returns a closure that can be executed now or later and is safe to use in a different thread
//Set unshare_strings if COW semantics need to be worked around
std::function<void(DBWriteConfig &)> WriteAllCFGOutClosure(const globconf &lgc, const std::list<std::shared_ptr<taccount>> &lalist, bool unshare_strings) {
	struct cfgdata {
		int64_t now;
		std::vector<taccount_cfg> taccs;
		globconf_base lgc;
	};
	auto data = std::make_shared<cfgdata>();
	for(auto &it : lalist) {
		data->taccs.emplace_back(*it);
		if(unshare_strings) {
			data->taccs.back().UnShareStrings();
		}
	}
	data->now = (int64_t) time(nullptr);
	data->lgc = lgc;
	if(unshare_strings) {
		data->lgc.UnShareStrings();
	}

	return [data](DBWriteConfig &twfc) {
		twfc.DeleteAll();
		data->lgc.CFGWriteOut(twfc);
		twfc.SetDBIndexGlobal();
		twfc.WriteInt64("LastUpdate", data->now);
		for(auto &it : data->taccs) {
			it.CFGWriteOut(twfc);
		}
	};
}

void WriteAllCFGOut(sqlite3 *db, const globconf &lgc, const std::list<std::shared_ptr<taccount>> &lalist) {
	LogMsg(LOGT::DBTRACE, "dbconn::WriteAllCFGOut start");
	DBWriteConfig twfc(db);
	WriteAllCFGOutClosure(lgc, lalist, false)(twfc);
	LogMsg(LOGT::DBTRACE, "dbconn::WriteAllCFGOut end");
}

void AllUsersInheritFromParentIfUnset() {
	gc.cfg.InheritFromParent(gcdefaults, true);
	for(auto &it : alist) it->cfg.InheritFromParent(gc.cfg, true);
}

void InitCFGDefaults() {
	//this is done because wxStandardPaths needs wxApp to be initialised first
	gcglobdefaults.mediasave_directorylist.val = wxStandardPaths::Get().GetDocumentsDir();
}

wxString cfg_strftime(time_t timestamp) {
	return rc_wx_strftime(gc.gcfg.datetimeformat.val, gc.datetimeisutc ? gmtime(&timestamp) : localtime(&timestamp), timestamp, !gc.datetimeisutc);
}
