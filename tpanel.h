extern std::unordered_multimap<uint64_t, tpanel*> tpaneldbloadmap;

enum { tpanelmenustartid=wxID_HIGHEST+8001 };
enum { tpanelmenuendid=wxID_HIGHEST+12000 };

struct tpanelmenuitem {
	unsigned int dbindex;
	unsigned int flags;
};

typedef std::map<int,tpanelmenuitem> tpanelmenudata;

struct tweetdispscr : public wxRichTextCtrl {
	std::shared_ptr<tweet> td;
	tpanelparentwin *tppw;
	wxBoxSizer *hbox;
	wxStaticBitmap *bm;

	tweetdispscr(std::shared_ptr<tweet> td_, tpanelparentwin *tppw_, wxBoxSizer *hbox_);
	~tweetdispscr();
	void DisplayTweet();
	void DoResize();

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
};

struct tpanel : std::enable_shared_from_this<tpanel> {
	std::string name;
	std::string dispname;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetlist;
	std::forward_list<tpanelparentwin*> twin;
	unsigned int flags;
	std::shared_ptr<taccount> assoc_acc;
	tweetidset storedids;		//any tweet or DM in this list *must* be either in ad.tweetobjs, or in the database

	static std::shared_ptr<tpanel> MkTPanel(const std::string &name_, const std::string &dispname_, unsigned int flags_=0, std::shared_ptr<taccount> *acc=0);
	tpanel(const std::string &name_, const std::string &dispname_, unsigned int flags_=0, std::shared_ptr<taccount> *acc=0);		//don't use this directly
	~tpanel();

	void PushTweet(std::shared_ptr<tweet> t);
	uint64_t PushTweetOrRetLoadId(uint64_t id);
	tpanelparentwin *MkTPanelWin(mainframe *parent, bool select=false);
	void OnTPanelWinClose(tpanelparentwin *tppw);
	void LoadMore(unsigned int n, uint64_t lessthanid=0);
};

struct tpanelnotebook : public wxAuiNotebook {
	mainframe *owner;

	tpanelnotebook(mainframe *owner_, wxWindow *parent);
	void dragdrophandler(wxAuiNotebookEvent& event);
	void dragdonehandler(wxAuiNotebookEvent& event);
	void tabrightclickhandler(wxAuiNotebookEvent& event);
	void tabclosedhandler(wxAuiNotebookEvent& event);
	void tabnumcheck();

	DECLARE_EVENT_TABLE()
};

enum {
	TPPWID_DETACH = 100,
	TPPWID_DUP,
	TPPWID_DETACHDUP,
	TPPWID_CLOSE,
};

struct tpanelparentwin : public wxScrolledWindow {
	//tpanelwin *tpw;
	std::shared_ptr<tpanel> tp;
	wxBoxSizer *sizer;
	std::list<std::pair<uint64_t, tweetdispscr *> > currentdisp;
	bool resize_update_pending;
	mainframe *owner;

	tpanelparentwin(std::shared_ptr<tpanel> tp_, mainframe *parent, bool select=false);
	~tpanelparentwin();
	void PushTweet(std::shared_ptr<tweet> t);
	tweetdispscr *PushTweet(std::shared_ptr<tweet> t, size_t index);
	void FillTweet();
	void resizehandler(wxSizeEvent &event);
	void resizemsghandler(wxCommandEvent &event);
	void tabdetachhandler(wxCommandEvent &event);
	void tabduphandler(wxCommandEvent &event);
	void tabdetachedduphandler(wxCommandEvent &event);
	void tabclosehandler(wxCommandEvent &event);

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
	uint64_t media_id;
	std::string media_url;
	image_panel *sb;
	wxStaticText *st;
	wxBoxSizer *sz;
	wxMenuItem *savemenuitem;

	media_display_win(wxWindow *parent, uint64_t media_id_);
	~media_display_win();
	void Update();
	bool GetImage(wxImage &img, wxString &message);
	media_entity *GetMediaEntity();
	void OnSave(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid=0);
wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp=0);
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
