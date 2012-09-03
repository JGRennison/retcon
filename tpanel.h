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
} TAMI_TYPE;

struct tweetactmenuitem {
	std::shared_ptr<tweet> tw;
	std::shared_ptr<userdatacontainer> user;
	TAMI_TYPE type;
	unsigned int dbindex;
	unsigned int flags;
};

typedef std::map<int,tpanelmenuitem> tpanelmenudata;
typedef std::map<int,tweetactmenuitem> tweetactmenudata;

struct profimg_staticbitmap: public wxStaticBitmap {
	uint64_t userid;
	uint64_t tweetid;

	inline profimg_staticbitmap(wxWindow* parent, const wxBitmap& label, uint64_t userid_, uint64_t tweetid_)
		: wxStaticBitmap(parent, wxID_ANY, label, wxPoint(-1000, -1000)), userid(userid_), tweetid(tweetid_) { }
	void ClickHandler(wxMouseEvent &event);

	DECLARE_EVENT_TABLE()
};

struct tweetdispscr : public wxRichTextCtrl {
	std::shared_ptr<tweet> td;
	tpanelparentwin *tppw;
	tpanelscrollwin *tpsw;
	wxBoxSizer *hbox;
	profimg_staticbitmap *bm;
	profimg_staticbitmap *bm2;
	time_t updatetime;
	long reltimestart;
	long reltimeend;
	uint64_t rtid;
	static tweetactmenudata tamd;

	tweetdispscr(const std::shared_ptr<tweet> &td_, tpanelscrollwin *parent, tpanelparentwin *tppw_, wxBoxSizer *hbox_);
	~tweetdispscr();
	void DisplayTweet(bool redrawimg=false);
	void DoResize();
	void OnTweetActMenuCmd(wxCommandEvent &event);

	void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
                               int noUnitsX, int noUnitsY,
                               int xPos = 0, int yPos = 0,
                               bool noRefresh = false );	//virtual override

	void urleventhandler(wxTextUrlEvent &event);
	void mousewheelhandler(wxMouseEvent &event);

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
};

struct tpanel : std::enable_shared_from_this<tpanel> {
	std::string name;
	std::string dispname;
	tweetidset tweetlist;
	std::forward_list<tpanelparentwin*> twin;
	unsigned int flags;
	std::shared_ptr<taccount> assoc_acc;
	//tweetidset storedids;		//any tweet or DM in this list *must* be either in ad.tweetobjs, or in the database

	static std::shared_ptr<tpanel> MkTPanel(const std::string &name_, const std::string &dispname_, unsigned int flags_=0, std::shared_ptr<taccount> *acc=0);
	tpanel(const std::string &name_, const std::string &dispname_, unsigned int flags_=0, std::shared_ptr<taccount> *acc=0);		//don't use this directly
	~tpanel();

	void PushTweet(const std::shared_ptr<tweet> &t);
	tpanelparentwin *MkTPanelWin(mainframe *parent, bool select=false);
	void OnTPanelWinClose(tpanelparentwin *tppw);
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

	DECLARE_EVENT_TABLE()
};

struct tppw_scrollfreeze {
	tppw_scrollfreeze() : scr(0), extrapixels(0) { }
	tweetdispscr *scr;
	int extrapixels;
};

enum {	//window IDs
	TPPWID_DETACH = 100,
	TPPWID_DUP,
	TPPWID_DETACHDUP,
	TPPWID_CLOSE,
	TPPWID_TOPBTN,
	TPPWID_SPLIT,
};

enum {	//for pushflags
	TPPWPF_ABOVE	= 1<<0,
	TPPWPF_BELOW	= 1<<1,
};

enum {	//for tppw_flags
	TPPWF_NOUPDATEONPUSH	= 1<<0,
};

struct tpanelparentwin : public wxPanel {
	std::shared_ptr<tpanel> tp;
	std::shared_ptr<tpanelglobal> tpg;
	wxBoxSizer *sizer;
	size_t displayoffset;
	std::list<std::pair<uint64_t, tweetdispscr *> > currentdisp;
	mainframe *owner;
	tpanelscrollwin *scrollwin;
	wxStaticText *clabel;
	unsigned int tppw_flags;

	tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select=false);
	~tpanelparentwin();
	void LoadMore(unsigned int n, uint64_t lessthanid=0, unsigned int pushflags=0);
	uint64_t PushTweetOrRetLoadId(uint64_t id, unsigned int pushflags=0);
	uint64_t PushTweetOrRetLoadId(const std::shared_ptr<tweet> &tobj, unsigned int pushflags=0);
	void PushTweet(const std::shared_ptr<tweet> &t, unsigned int pushflags=0);
	tweetdispscr *PushTweetIndex(const std::shared_ptr<tweet> &t, size_t index);
	void tabdetachhandler(wxCommandEvent &event);
	void tabduphandler(wxCommandEvent &event);
	void tabdetachedduphandler(wxCommandEvent &event);
	void tabclosehandler(wxCommandEvent &event);
	void tabsplitcmdhandler(wxCommandEvent &event);
	void PageUpHandler();
	void PageDownHandler();
	void PageTopHandler();
	void pageupevthandler(wxCommandEvent &event);
	void pagedownevthandler(wxCommandEvent &event);
	void pagetopevthandler(wxCommandEvent &event);
	void UpdateCLabel();
	void CheckClearNoUpdateFlag();
	void TweetPopTop();
	void TweetPopBottom();
	void StartScrollFreeze(tppw_scrollfreeze &s);
	void EndScrollFreeze(tppw_scrollfreeze &s);

	DECLARE_EVENT_TABLE()
};

struct tpanelscrollwin : public wxScrolledWindow {
	tpanelparentwin *parent;
	bool resize_update_pending;
	bool page_scroll_blocked;

	tpanelscrollwin(tpanelparentwin *parent_);
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
	void Update();
	bool GetImage(wxImage &img, wxString &message);
	media_entity *GetMediaEntity();
	void OnSave(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid=0);
wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp=0, bool localtime=true);
wxString getreltimestr(time_t timestamp, time_t &updatetime);
void MakeTPanelMenu(wxMenu *menuP, tpanelmenudata &map);
void TPanelMenuAction(tpanelmenudata &map, int curid, mainframe *parent);

//struct tpanelwin : public wxRichTextCtrl {
//	tpanelparentwin *tppw;
//	std::shared_ptr<tpanel> tp;
//
//	tpanelwin(tpanelparentwin *tppw_);
//	~tpanelwin();
//	void PushTweet(std::shared_ptr<tweetdisp> t);
//};

struct tpaneldbloadmap_data {
	tpaneldbloadmap_data(tpanelparentwin* win_, unsigned int pushflags_=0) : win(win_), pushflags(pushflags_) { }
	tpanelparentwin* win;
	unsigned int pushflags;
};

extern std::unordered_multimap<uint64_t, tpaneldbloadmap_data> tpaneldbloadmap;
