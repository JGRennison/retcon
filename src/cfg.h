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

#ifndef HGUARD_SRC_CFG
#define HGUARD_SRC_CFG

#include "univdefs.h"
#include <wx/string.h>
#include <memory>
#include <list>

struct DBWriteConfig;
struct DBReadConfig;
struct taccount;
struct sqlite3;

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
	void InheritFromParent(genoptconf &parent, bool ifunset = false);

	static void IterateConfs(std::function<void(const std::string &, genopt genoptconf::*)> f);
};

struct genoptglobconf {
	genopt userexpiretimemins;
	genopt datetimeformat;
	genopt maxpanelprofimgsize;
	genopt maxtweetsdisplayinpanel;
	genopt tweetdispformat;
	genopt dmdispformat;
	genopt rtdispformat;
	genopt userdispformat;
	genopt cachethumbs;
	genopt cachemedia;
	genopt persistentmediacache;
	genopt rtdisp;
	genopt assumementionistweet;
	genopt mouseover_tweetdispformat;
	genopt mouseover_dmdispformat;
	genopt mouseover_rtdispformat;
	genopt mouseover_userdispformat;
	genopt highlight_colourdelta;
	genopt mediasave_directorylist;
	genopt incoming_filter;
	genopt imgthumbunhidetime;
	genopt setproxy;
	genopt proxyurl;
	genopt proxyhttptunnel;
	genopt noproxylist;
	genopt netiface;
	genopt inlinereplyloadcount;
	genopt showdeletedtweetsbydefault;
	genopt markowntweetsasread;
	void CFGWriteOut(DBWriteConfig &twfc);
	void CFGReadIn(DBReadConfig &twfc, const genoptglobconf &parent);

	static void IterateConfs(std::function<void(const std::string &, genopt genoptglobconf::*)> f);
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
	bool assumementionistweet;
	unsigned long imgthumbunhidetime;
	bool setproxy;
	std::string proxyurl;
	bool proxyhttptunnel;
	std::string noproxylist;
	std::string netiface;
	unsigned long inlinereplyloadcount;
	bool showdeletedtweetsbydefault;
	bool markowntweetsasread;

	void CFGWriteOut(DBWriteConfig &twfc);
	void CFGReadIn(DBReadConfig &twfc);
	void CFGParamConv();
};

void ReadAllCFGIn(sqlite3 *db, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);
void WriteAllCFGOut(sqlite3 *db, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);
void AllUsersInheritFromParentIfUnset();
void InitCFGDefaults();

extern globconf gc;
extern genoptconf gcdefaults;
extern genoptglobconf gcglobdefaults;

#endif
