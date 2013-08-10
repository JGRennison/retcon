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

struct dispscr_base : public wxRichTextCtrl, public magic_ptr_base {
	panelparentwin_base *tppw;
	tpanelscrollwin *tpsw;
	wxBoxSizer *hbox;

	dispscr_base(tpanelscrollwin *parent, panelparentwin_base *tppw_, wxBoxSizer *hbox_);
	void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
                               int noUnitsX, int noUnitsY,
                               int xPos = 0, int yPos = 0,
                               bool noRefresh = false );	//virtual override
	void mousewheelhandler(wxMouseEvent &event);

	DECLARE_EVENT_TABLE()
};

enum {	//for tweetdispscr.tds_flags
	TDSF_SUBTWEET	= 1<<0,
	TDSF_HIGHLIGHT	= 1<<1,
};

struct tweetdispscr : public dispscr_base {
	std::shared_ptr<tweet> td;
	profimg_staticbitmap *bm;
	profimg_staticbitmap *bm2;
	time_t updatetime;
	long reltimestart;
	long reltimeend;
	uint64_t rtid;
	unsigned int tds_flags = 0;
	std::forward_list<magic_ptr_ts<tweetdispscr> > subtweets;
	wxColour default_background_colour;
	wxColour default_foreground_colour;

	tweetdispscr(const std::shared_ptr<tweet> &td_, tpanelscrollwin *parent, tpanelparentwin_nt *tppw_, wxBoxSizer *hbox_);
	~tweetdispscr();
	void DisplayTweet(bool redrawimg=false);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	void urlhandler(wxString url);
	void urleventhandler(wxTextUrlEvent &event);
	void rightclickhandler(wxMouseEvent &event);

	DECLARE_EVENT_TABLE()

};

struct userdispscr : public dispscr_base {
	std::shared_ptr<userdatacontainer> u;
	profimg_staticbitmap *bm;

	userdispscr(const std::shared_ptr<userdatacontainer> &u_, tpanelscrollwin *parent, tpanelparentwin_user *tppw_, wxBoxSizer *hbox_);
	~userdispscr();
	void Display(bool redrawimg=false);

	void urleventhandler(wxTextUrlEvent &event);

	DECLARE_EVENT_TABLE()
};

void AppendUserMenuItems(wxMenu &menu, tweetactmenudata &map, int &nextid, std::shared_ptr<userdatacontainer> user, std::shared_ptr<tweet> tw);