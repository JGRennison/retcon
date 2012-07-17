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

struct tpanel : std::enable_shared_from_this<tpanel> {
	std::string name;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetlist;
	std::forward_list<tpanelparentwin*> twin;

	tpanel(std::string name_);
	void PushTweet(std::shared_ptr<tweet> t);
	tpanelparentwin *MkTPanelWin(mainframe *parent);
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

	tpanelparentwin(std::shared_ptr<tpanel> tp_, mainframe *parent);
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

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid=0);

//struct tpanelwin : public wxRichTextCtrl {
//	tpanelparentwin *tppw;
//	std::shared_ptr<tpanel> tp;
//
//	tpanelwin(tpanelparentwin *tppw_);
//	~tpanelwin();
//	void PushTweet(std::shared_ptr<tweetdisp> t);
//};
