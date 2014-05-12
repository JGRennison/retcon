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

#ifndef HGUARD_SRC_CFG
#define HGUARD_SRC_CFG

#include "univdefs.h"
#include <wx/string.h>
#include <memory>
#include <list>
#include <functional>

struct DBWriteConfig;
struct DBReadConfig;
struct taccount;
struct sqlite3;

struct genopt {
	wxString val;
	bool enable;
	void CFGWriteOutCurDir(DBWriteConfig &twfc, const char *name) const;
	void CFGReadInCurDir(DBReadConfig &twfc, const char *name, const wxString &parent);
	void InheritFromParent(genopt &parent, bool ifunset=false);
};

struct genoptconf {
	genopt tokenk;
	genopt tokens;
	genopt ssl;
	genopt userstreams;
	genopt restinterval;
	void CFGWriteOutCurDir(DBWriteConfig &twfc) const;
	void CFGReadInCurDir(DBReadConfig &twfc, const genoptconf &parent);
	void InheritFromParent(genoptconf &parent, bool ifunset = false);

	static void IterateConfs(std::function<void(const std::string &, genopt genoptconf::*)> f);
	void UnShareStrings();
};

#define CFGTEMPL_EXPAND \
	CFGTEMPL(userexpiretimemins) \
	CFGTEMPL(datetimeformat) \
	CFGTEMPL_BOOL(datetimeisutc) \
	CFGTEMPL_UL(maxpanelprofimgsize) \
	CFGTEMPL_UL(maxtweetsdisplayinpanel) \
	CFGTEMPL(tweetdispformat) \
	CFGTEMPL(dmdispformat) \
	CFGTEMPL(rtdispformat) \
	CFGTEMPL(userdispformat) \
	CFGTEMPL_BOOL(cachethumbs) \
	CFGTEMPL_BOOL(cachemedia) \
	CFGTEMPL_BOOL(rtdisp) \
	CFGTEMPL_BOOL(assumementionistweet) \
	CFGTEMPL(mouseover_tweetdispformat) \
	CFGTEMPL(mouseover_dmdispformat) \
	CFGTEMPL(mouseover_rtdispformat) \
	CFGTEMPL(mouseover_userdispformat) \
	CFGTEMPL(highlight_colourdelta) \
	CFGTEMPL(mediasave_directorylist) \
	CFGTEMPL(incoming_filter) \
	CFGTEMPL(alltweet_filter) \
	CFGTEMPL_UL(imgthumbunhidetime) \
	CFGTEMPL_BOOL(setproxy) \
	CFGTEMPL(proxyurl) \
	CFGTEMPL_BOOL(proxyhttptunnel) \
	CFGTEMPL(noproxylist) \
	CFGTEMPL(netiface) \
	CFGTEMPL_UL(inlinereplyloadcount) \
	CFGTEMPL_UL(inlinereplyloadmorecount) \
	CFGTEMPL_BOOL(showdeletedtweetsbydefault) \
	CFGTEMPL_BOOL(markowntweetsasread) \
	CFGTEMPL_BOOL(markdeletedtweetsasread) \
	CFGTEMPL_UL(mediawinscreensizewidthreduction) \
	CFGTEMPL_UL(mediawinscreensizeheightreduction) \
	CFGTEMPL_BOOL(autoloadthumb_thumb) \
	CFGTEMPL_BOOL(autoloadthumb_full) \
	CFGTEMPL_BOOL(disploadthumb_thumb) \
	CFGTEMPL_BOOL(disploadthumb_full) \
	CFGTEMPL_BOOL(dispthumbs) \
	CFGTEMPL_BOOL(hideallthumbs) \
	CFGTEMPL_BOOL(loadhiddenthumbs) \
	CFGTEMPL_UL(threadpoollimit) \
	CFGTEMPL_UL(mediacachesavedays) \
	CFGTEMPL_BOOL(showunhighlightallbtn) \
	CFGTEMPL_UL(asyncstatewritebackintervalmins) \

struct genoptglobconf {
#define CFGTEMPL(x) genopt x;
#define CFGTEMPL_UL(x) CFGTEMPL(x)
#define CFGTEMPL_BOOL(x) CFGTEMPL(x)
	CFGTEMPL_EXPAND
#undef CFGTEMPL
#undef CFGTEMPL_UL
#undef CFGTEMPL_BOOL

	void CFGWriteOut(DBWriteConfig &twfc) const;
	void CFGReadIn(DBReadConfig &twfc, const genoptglobconf &parent);

	static void IterateConfs(std::function<void(const std::string &, genopt genoptglobconf::*)> f);
	void UnShareStrings();
};

struct globconf_base {
	genoptconf cfg;
	genoptglobconf gcfg;

	void CFGWriteOut(DBWriteConfig &twfc);
	void UnShareStrings();
};

struct globconf : public globconf_base {

#define CFGTEMPL(x)
#define CFGTEMPL_UL(x) unsigned long x;
#define CFGTEMPL_BOOL(x) bool x;
	CFGTEMPL_EXPAND
#undef CFGTEMPL
#undef CFGTEMPL_UL
#undef CFGTEMPL_BOOL

	unsigned long userexpiretime;
	std::string proxyurl;
	std::string noproxylist;
	std::string netiface;

	void CFGReadIn(DBReadConfig &twfc);
	void CFGParamConv();

	//Set by cmdline
	bool readonlymode = false;
};

void ReadAllCFGIn(sqlite3 *db, globconf &gc, std::list<std::shared_ptr<taccount>> &lalist);
void WriteAllCFGOut(sqlite3 *db, const globconf &gc, const std::list<std::shared_ptr<taccount>> &lalist);
std::function<void(DBWriteConfig &)> WriteAllCFGOutClosure(const globconf &lgc, const std::list<std::shared_ptr<taccount>> &lalist, bool unshare_strings);
void AllUsersInheritFromParentIfUnset();
void InitCFGDefaults();

wxString cfg_strftime(time_t timestamp);

extern globconf gc;
extern genoptconf gcdefaults;
extern genoptglobconf gcglobdefaults;

#endif
