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

struct tpanelreltimeupdater : public wxTimer {
	void Notify();
};

struct tpanelglobal {
	wxBitmap arrow;
	int arrow_dim;
	tpanelreltimeupdater minutetimer;
	wxBitmap infoicon;
	wxImage infoicon_img;
	wxBitmap replyicon;
	wxImage replyicon_img;
	wxBitmap favicon;
	wxImage favicon_img;
	wxBitmap favonicon;
	wxImage favonicon_img;
	wxBitmap retweeticon;
	wxImage retweeticon_img;
	wxBitmap retweetonicon;
	wxImage retweetonicon_img;
	wxBitmap dmreplyicon;
	wxImage dmreplyicon_img;
	wxBitmap proticon;
	wxImage proticon_img;
	wxBitmap verifiedicon;
	wxImage verifiedicon_img;
	wxBitmap closeicon;
	wxBitmap multiunreadicon;

	static std::shared_ptr<tpanelglobal> Get();
	static std::weak_ptr<tpanelglobal> tpg_glob;

	tpanelglobal();	//use Get() instead
};

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

struct profimg_staticbitmap: public wxStaticBitmap {
	uint64_t userid;
	uint64_t tweetid;
	mainframe *owner;
	bool ishalf;

	inline profimg_staticbitmap(wxWindow* parent, const wxBitmap& label, uint64_t userid_, uint64_t tweetid_, mainframe *owner_=0, bool half = false)
		: wxStaticBitmap(parent, wxID_ANY, label, wxPoint(-1000, -1000)), userid(userid_), tweetid(tweetid_), owner(owner_), ishalf(half) { }
	void ClickHandler(wxMouseEvent &event);
	void RightClickHandler(wxMouseEvent &event);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

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

enum {
	TPF_DELETEONWINCLOSE		= 1<<0,
	TPF_ISAUTO			= 1<<1,
	TPF_SAVETODB			= 1<<2,
	TPF_AUTO_DM			= 1<<3,
	TPF_AUTO_TW			= 1<<4,
	TPF_AUTO_ACC			= 1<<5,
	TPF_AUTO_ALLACCS		= 1<<6,
	TPF_AUTO_MN			= 1<<7,
	TPF_USER_TIMELINE		= 1<<8,
};

struct tpanel : std::enable_shared_from_this<tpanel> {
	std::string name;
	std::string dispname;
	tweetidset tweetlist;
	std::forward_list<tpanelparentwin_nt*> twin;
	unsigned int flags;
	uint64_t upperid;
	uint64_t lowerid;
	std::shared_ptr<taccount> assoc_acc;
	cached_id_sets cids;

	static std::shared_ptr<tpanel> MkTPanel(const std::string &name_, const std::string &dispname_, unsigned int flags_=0, std::shared_ptr<taccount> *acc=0);
	tpanel(const std::string &name_, const std::string &dispname_, unsigned int flags_=0, std::shared_ptr<taccount> *acc=0);		//don't use this directly
	~tpanel();

	void PushTweet(const std::shared_ptr<tweet> &t, unsigned int pushflags=0);
	bool RegisterTweet(const std::shared_ptr<tweet> &t);
	tpanelparentwin *MkTPanelWin(mainframe *parent, bool select=false);
	void OnTPanelWinClose(tpanelparentwin_nt *tppw);
	bool IsSingleAccountTPanel() const;
	void TPPWFlagMaskAllTWins(unsigned int set, unsigned int clear) const;
};

struct tpanelnotebook : public wxAuiNotebook {
	mainframe *owner;

	tpanelnotebook(mainframe *owner_, wxWindow *parent);
	void dragdrophandler(wxAuiNotebookEvent& event);
	void dragdonehandler(wxAuiNotebookEvent& event);
	void tabrightclickhandler(wxAuiNotebookEvent& event);
	void tabclosedhandler(wxAuiNotebookEvent& event);
	void onsizeevt(wxSizeEvent &event);
	void tabnumcheck();
	virtual void Split(size_t page, int direction);
	void PostSplitSizeCorrect();
	void FillWindowLayout(unsigned int mainframeindex);

	DECLARE_EVENT_TABLE()
};

struct tppw_scrollfreeze {
	tppw_scrollfreeze() : scr(0), extrapixels(0) { }
	dispscr_base *scr;
	int extrapixels;
};

enum {	//window IDs
	TPPWID_DETACH = 100,
	TPPWID_DUP,
	TPPWID_DETACHDUP,
	TPPWID_CLOSE,
	TPPWID_TOPBTN,
	TPPWID_SPLIT,
	TPPWID_MARKALLREADBTN,
	TPPWID_UNHIGHLIGHTALLBTN,
};

enum {	//for pushflags
	TPPWPF_ABOVE	= 1<<0,
	TPPWPF_BELOW	= 1<<1,
	TPPWPF_USERTL	= 1<<2,
	TPPWPF_SETNOUPDATEFLAG	= 1<<3,
};

enum {	//for tppw_flags
	TPPWF_NOUPDATEONPUSH	= 1<<0,
	TPPWF_CANALWAYSSCROLLDOWN	= 1<<1,
	TPPWF_CLABELUPDATEPENDING	= 1<<2,
};

struct panelparentwin_base : public wxPanel, public magic_ptr_base {
	std::shared_ptr<tpanelglobal> tpg;
	wxBoxSizer *sizer;
	size_t displayoffset;
	wxWindow *parent_win;
	tpanelscrollwin *scrollwin;
	wxStaticText *clabel;
	unsigned int tppw_flags;
	wxButton *MarkReadBtn;
	wxButton *UnHighlightBtn;
	wxBoxSizer* headersizer;

	std::list<std::pair<uint64_t, dispscr_base *> > currentdisp;

	panelparentwin_base(wxWindow *parent, bool fitnow=true);
	virtual ~panelparentwin_base() { }
	virtual mainframe *GetMainframe();
	virtual void PageUpHandler() { };
	virtual void PageDownHandler() { };
	virtual void PageTopHandler() { };
	void pageupevthandler(wxCommandEvent &event);
	void pagedownevthandler(wxCommandEvent &event);
	void pagetopevthandler(wxCommandEvent &event);
	virtual void UpdateCLabel() { }
	void CheckClearNoUpdateFlag();
	void PopTop();
	void PopBottom();
	void StartScrollFreeze(tppw_scrollfreeze &s);
	void EndScrollFreeze(tppw_scrollfreeze &s);
	virtual bool IsSingleAccountWin() const;

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin_nt : public panelparentwin_base {
	std::shared_ptr<tpanel> tp;

	tpanelparentwin_nt(const std::shared_ptr<tpanel> &tp_, wxWindow *parent);
	virtual ~tpanelparentwin_nt();
	void PushTweet(const std::shared_ptr<tweet> &t, unsigned int pushflags=0);
	tweetdispscr *PushTweetIndex(const std::shared_ptr<tweet> &t, size_t index);
	virtual void LoadMore(unsigned int n, uint64_t lessthanid=0, unsigned int pushflags=0) { }
	virtual void UpdateCLabel();
	virtual void PageUpHandler();
	virtual void PageDownHandler();
	virtual void PageTopHandler();
	void markallreadevthandler(wxCommandEvent &event);
	void markremoveallhighlightshandler(wxCommandEvent &event);
	void MarkClearCIDSSetHandler(std::function<tweetidset &(cached_id_sets &)> idsetselector, std::function<void(const std::shared_ptr<tweet> &)> existingtweetfunc);
	virtual bool IsSingleAccountWin() const { return tp->IsSingleAccountTPanel(); }
	void EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush);
	void UpdateOwnTweet(const tweet &t, bool redrawimg);

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin : public tpanelparentwin_nt {
	mainframe *owner;
	unsigned int tpw_flags = 0;
	enum {
		TPWF_UNREADBITMAPDISP	= 1<<0,
	};

	tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select=false);
	virtual void LoadMore(unsigned int n, uint64_t lessthanid=0, unsigned int pushflags=0);
	virtual mainframe *GetMainframe() { return owner; }
	uint64_t PushTweetOrRetLoadId(uint64_t id, unsigned int pushflags=0);
	uint64_t PushTweetOrRetLoadId(const std::shared_ptr<tweet> &tobj, unsigned int pushflags=0);
	void tabdetachhandler(wxCommandEvent &event);
	void tabduphandler(wxCommandEvent &event);
	void tabdetachedduphandler(wxCommandEvent &event);
	void tabclosehandler(wxCommandEvent &event);
	void tabsplitcmdhandler(wxCommandEvent &event);
	virtual void UpdateCLabel();

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin_usertweets : public tpanelparentwin_nt {
	std::shared_ptr<userdatacontainer> user;
	std::weak_ptr<taccount> acc;
	static std::map<std::pair<uint64_t, RBFS_TYPE>, std::shared_ptr<tpanel> > usertpanelmap;	//use map rather than unordered_map due to the hassle associated with specialising std::hash
	bool havestarted;
	RBFS_TYPE type;

	tpanelparentwin_usertweets(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::weak_ptr<taccount> &acc_, RBFS_TYPE type_=RBFS_USER_TIMELINE);
	~tpanelparentwin_usertweets();
	virtual void LoadMore(unsigned int n, uint64_t lessthanid=0, unsigned int pushflags=0);
	virtual void UpdateCLabel();
	static std::shared_ptr<tpanel> MkUserTweetTPanel(const std::shared_ptr<userdatacontainer> &user, RBFS_TYPE type_=RBFS_USER_TIMELINE);
	static std::shared_ptr<tpanel> GetUserTweetTPanel(uint64_t userid, RBFS_TYPE type_=RBFS_USER_TIMELINE);
	virtual bool IsSingleAccountWin() const { return true; }

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin_user : public panelparentwin_base {
	std::deque< std::shared_ptr<userdatacontainer> > userlist;

	static std::multimap<uint64_t, tpanelparentwin_user*> pendingmap;

	tpanelparentwin_user(wxWindow *parent);
	~tpanelparentwin_user();
	bool PushBackUser(const std::shared_ptr<userdatacontainer> &u);
	bool UpdateUser(const std::shared_ptr<userdatacontainer> &u, size_t offset);
	virtual void LoadMoreToBack(unsigned int n) { }
	virtual void PageUpHandler();
	virtual void PageDownHandler();
	virtual void PageTopHandler();
	virtual size_t ItemCount() { return userlist.size(); }

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin_userproplisting : public tpanelparentwin_user {
	std::deque<uint64_t> useridlist;
	std::shared_ptr<userdatacontainer> user;
	std::weak_ptr<taccount> acc;
	bool havestarted;
	CS_ENUMTYPE type;

	tpanelparentwin_userproplisting(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::weak_ptr<taccount> &acc_, CS_ENUMTYPE type_);
	~tpanelparentwin_userproplisting();
	virtual void LoadMoreToBack(unsigned int n);
	virtual void UpdateCLabel();
	virtual void Init();
	virtual size_t ItemCount() { return useridlist.size(); }

	DECLARE_EVENT_TABLE()
};

struct tpanelscrollwin : public wxScrolledWindow {
	panelparentwin_base *parent;
	bool resize_update_pending;
	bool page_scroll_blocked;

	tpanelscrollwin(panelparentwin_base *parent_);
	void OnScrollHandler(wxScrollWinEvent &event);
	void resizehandler(wxSizeEvent &event);
	void resizemsghandler(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

struct image_panel : public wxPanel {
	image_panel(media_display_win *parent, wxSize size=wxDefaultSize);
	void OnPaint(wxPaintEvent &event);
	void OnResize(wxSizeEvent &event);
	void UpdateBitmap();

	wxBitmap bm;
	wxImage img;

	DECLARE_EVENT_TABLE()
};

enum {
	MDID_SAVE=1,
};

struct media_display_win : public wxFrame {
	media_id_type media_id;
	std::string media_url;
	image_panel *sb;
	wxStaticText *st;
	wxBoxSizer *sz;
	wxMenuItem *savemenuitem;

	media_display_win(wxWindow *parent, media_id_type media_id_);
	~media_display_win();
	void UpdateImage();
	bool GetImage(wxImage &img, wxString &message);
	media_entity *GetMediaEntity();
	void OnSave(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

struct twin_layout_desc {
	unsigned int mainframeindex;
	unsigned int splitindex;
	unsigned int tabindex;
	std::shared_ptr<taccount> acc;
	std::string name;
	std::string dispname;
	unsigned int flags;
};

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid=0);
wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp=0, bool localtime=true);
wxString getreltimestr(time_t timestamp, time_t &updatetime);
void MakeTPanelMenu(wxMenu *menuP, tpanelmenudata &map);
void TPanelMenuAction(tpanelmenudata &map, int curid, mainframe *parent);
void CheckClearNoUpdateFlag_All();
void SaveWindowLayout();
void RestoreWindowLayout();
