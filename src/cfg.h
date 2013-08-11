//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
//
//  NOTE: This software is licensed under the GPL. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  Jonathan Rennison (or anybody else) is in no way responsible, or liable
//  for this program or its use in relation to users, 3rd parties or to any
//  persons in any way whatsoever.
//
//  You  should have  received a  copy of  the GNU  General Public
//  License along  with this program; if  not, write to  the Free Software
//  Foundation, Inc.,  59 Temple Place,  Suite 330, Boston,  MA 02111-1307
//  USA
//
//  2012 - j.g.rennison@gmail.com
//==========================================================================

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
	bool assumementionistweet;

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
