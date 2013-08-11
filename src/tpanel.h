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

DECLARE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEUP_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT, -1)

struct tweetdispscr_mouseoverwin;

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

enum {
	PISBF_HALF		= 1<<0,
	PISBF_DONTUSEDEFAULTMF	= 1<<1,
};

struct profimg_staticbitmap: public wxStaticBitmap {
	uint64_t userid;
	uint64_t tweetid;
	mainframe *owner;
	unsigned int pisb_flags;

	inline profimg_staticbitmap(wxWindow* parent, const wxBitmap& label, uint64_t userid_, uint64_t tweetid_, mainframe *owner_=0, unsigned int flags = 0)
		: wxStaticBitmap(parent, wxID_ANY, label, wxPoint(-1000, -1000)), userid(userid_), tweetid(tweetid_), owner(owner_), pisb_flags(flags) { }
	void ClickHandler(wxMouseEvent &event);
	void RightClickHandler(wxMouseEvent &event);
	void OnTweetActMenuCmd(wxCommandEvent &event);

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
	TPPWID_NEWESTUNREADBTN,
	TPPWID_OLDESTUNREADBTN,
	TPPWID_UNHIGHLIGHTALLBTN,
};

enum {	//for pushflags
	TPPWPF_ABOVE	= 1<<0,
	TPPWPF_BELOW	= 1<<1,
	TPPWPF_USERTL	= 1<<2,
	TPPWPF_SETNOUPDATEFLAG	= 1<<3,
	TPPWPF_NOINCDISPOFFSET	= 1<<4,
	TPPWPF_CHECKSCROLLTOID	= 1<<5,
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
	wxButton *NewestUnreadBtn;
	wxButton *OldestUnreadBtn;
	wxButton *UnHighlightBtn;
	wxBoxSizer* headersizer;
	uint64_t scrolltoid = 0;
	uint64_t scrolltoid_onupdate = 0;
	std::multimap<std::string, wxButton *> showhidemap;

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
	virtual void HandleScrollToIDOnUpdate() { }
	void PopTop();
	void PopBottom();
	void StartScrollFreeze(tppw_scrollfreeze &s);
	void EndScrollFreeze(tppw_scrollfreeze &s);
	void SetScrollFreeze(tppw_scrollfreeze &s, dispscr_base *scr);
	virtual bool IsSingleAccountWin() const;
	void ShowHideButtons(std::string type, bool show);
	virtual void NotifyRequestFailed() { }

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin_nt : public panelparentwin_base {
	std::shared_ptr<tpanel> tp;
	tweetdispscr_mouseoverwin *mouseoverwin = 0;

	tpanelparentwin_nt(const std::shared_ptr<tpanel> &tp_, wxWindow *parent);
	virtual ~tpanelparentwin_nt();
	void PushTweet(const std::shared_ptr<tweet> &t, unsigned int pushflags=0);
	tweetdispscr *PushTweetIndex(const std::shared_ptr<tweet> &t, size_t index);
	virtual void LoadMore(unsigned int n, uint64_t lessthanid=0, uint64_t greaterthanid=0, unsigned int pushflags=0) { }
	virtual void UpdateCLabel();
	virtual void PageUpHandler();
	virtual void PageDownHandler();
	virtual void PageTopHandler();
	virtual void JumpToTweetID(uint64_t id);
	virtual void HandleScrollToIDOnUpdate();
	void markallreadevthandler(wxCommandEvent &event);
	void markremoveallhighlightshandler(wxCommandEvent &event);
	void movetonewestunreadhandler(wxCommandEvent &event);
	void movetooldestunreadhandler(wxCommandEvent &event);
	void MarkClearCIDSSetHandler(std::function<tweetidset &(cached_id_sets &)> idsetselector, std::function<void(const std::shared_ptr<tweet> &)> existingtweetfunc);
	virtual bool IsSingleAccountWin() const { return tp->IsSingleAccountTPanel(); }
	void EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush);
	void UpdateOwnTweet(const tweet &t, bool redrawimg);
	tweetdispscr_mouseoverwin *MakeMouseOverWin();

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin : public tpanelparentwin_nt {
	mainframe *owner;
	unsigned int tpw_flags = 0;
	enum {
		TPWF_UNREADBITMAPDISP	= 1<<0,
	};

	tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select=false);
	virtual void LoadMore(unsigned int n, uint64_t lessthanid=0, uint64_t greaterthanid=0, unsigned int pushflags=0);
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
	std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc;
	static std::map<std::pair<uint64_t, RBFS_TYPE>, std::shared_ptr<tpanel> > usertpanelmap;	//use map rather than unordered_map due to the hassle associated with specialising std::hash
	bool havestarted;
	bool failed = false;
	RBFS_TYPE type;

	tpanelparentwin_usertweets(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc, RBFS_TYPE type_=RBFS_USER_TIMELINE);
	~tpanelparentwin_usertweets();
	virtual void LoadMore(unsigned int n, uint64_t lessthanid=0, unsigned int pushflags=0);
	virtual void UpdateCLabel();
	static std::shared_ptr<tpanel> MkUserTweetTPanel(const std::shared_ptr<userdatacontainer> &user, RBFS_TYPE type_=RBFS_USER_TIMELINE);
	static std::shared_ptr<tpanel> GetUserTweetTPanel(uint64_t userid, RBFS_TYPE type_=RBFS_USER_TIMELINE);
	virtual bool IsSingleAccountWin() const { return true; }
	virtual void NotifyRequestFailed();

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
	std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc;
	bool havestarted;
	bool failed = false;
	CS_ENUMTYPE type;

	tpanelparentwin_userproplisting(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc, CS_ENUMTYPE type_);
	~tpanelparentwin_userproplisting();
	virtual void LoadMoreToBack(unsigned int n);
	virtual void UpdateCLabel();
	virtual void Init();
	virtual size_t ItemCount() { return useridlist.size(); }
	virtual void NotifyRequestFailed();

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

struct twin_layout_desc {
	unsigned int mainframeindex;
	unsigned int splitindex;
	unsigned int tabindex;
	std::shared_ptr<taccount> acc;
	std::string name;
	std::string dispname;
	unsigned int flags;
};

struct mf_layout_desc {
	unsigned int mainframeindex;
	wxPoint pos;
	wxSize size;
	bool maximised;
};

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid=0);
void MakeTPanelMenu(wxMenu *menuP, tpanelmenudata &map);
void TPanelMenuAction(tpanelmenudata &map, int curid, mainframe *parent);
void AppendToTAMIMenuMap(tweetactmenudata &map, int &nextid, TAMI_TYPE type, std::shared_ptr<tweet> tw, unsigned int dbindex=0, std::shared_ptr<userdatacontainer> user=std::shared_ptr<userdatacontainer>(), unsigned int flags=0, wxString extra = wxT(""));
void CheckClearNoUpdateFlag_All();
