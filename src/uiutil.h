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
//  2013 - j.g.rennison@gmail.com
//==========================================================================

enum { tpanelmenustartid=wxID_HIGHEST+8001 };
enum { tpanelmenuendid=wxID_HIGHEST+12000 };
enum { tweetactmenustartid=wxID_HIGHEST+12001 };
enum { tweetactmenuendid=wxID_HIGHEST+16000 };

struct tpanelmenuitem {
	unsigned int dbindex;
	unsigned int flags;
};

typedef enum {
	TAMI_RETWEET=1,
	TAMI_FAV,
	TAMI_UNFAV,
	TAMI_REPLY,
	TAMI_BROWSER,
	TAMI_COPYTEXT,
	TAMI_COPYID,
	TAMI_COPYLINK,
	TAMI_DELETE,
	TAMI_COPYEXTRA,
	TAMI_BROWSEREXTRA,
	TAMI_MEDIAWIN,
	TAMI_USERWINDOW,
	TAMI_DM,
	TAMI_NULL,
	TAMI_TOGGLEHIGHLIGHT,
	TAMI_MARKREAD,
	TAMI_MARKUNREAD,
	TAMI_MARKNOREADSTATE,
} TAMI_TYPE;

struct tweetactmenuitem {
	std::shared_ptr<tweet> tw;
	std::shared_ptr<userdatacontainer> user;
	TAMI_TYPE type;
	unsigned int dbindex;
	unsigned int flags;
	wxString extra;
};

typedef std::map<int,tpanelmenuitem> tpanelmenudata;
typedef std::map<int,tweetactmenuitem> tweetactmenudata;

extern tweetactmenudata tamd;

void AppendToTAMIMenuMap(tweetactmenudata &map, int &nextid, TAMI_TYPE type, std::shared_ptr<tweet> tw, unsigned int dbindex, std::shared_ptr<userdatacontainer> user, unsigned int flags, wxString extra);
void MakeRetweetMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw);
void MakeFavMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw);
void MakeCopyMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw);
void MakeMarkMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw);
void TweetActMenuAction(tweetactmenudata &map, int curid, mainframe *mainwin=0);
uint64_t ParseUrlID(wxString url);
media_id_type ParseMediaID(wxString url);
void SaveWindowLayout();
void RestoreWindowLayout();
wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp=0, bool localtime=true);
wxString getreltimestr(time_t timestamp, time_t &updatetime);

typedef enum {
	CO_SET,
	CO_ADD,
	CO_SUB,
	CO_AND,
	CO_OR,
	CO_RSUB,
} COLOUR_OP;
wxColour ColourOp(const wxColour &in, const wxColour &delta, COLOUR_OP co);
wxColour ColourOp(const wxColour &in, const wxString &co_str);
