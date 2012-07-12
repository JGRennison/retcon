struct tweetdispscr : public wxRichTextCtrl {
	std::shared_ptr<tweetdisp> td;
	tpanelparentwin *tppw;
	wxBoxSizer *hbox;
	wxStaticBitmap *bm;

	tweetdispscr(std::shared_ptr<tweetdisp> td_, tpanelparentwin *tppw_, wxBoxSizer *hbox_);
	~tweetdispscr();
	void DisplayTweet();
	void DoResize();

	void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
                               int noUnitsX, int noUnitsY,
                               int xPos = 0, int yPos = 0,
                               bool noRefresh = false );	//virtual override

	void urleventhandler(wxTextUrlEvent &event);

	DECLARE_EVENT_TABLE()

};

struct tweetdisp {
	std::shared_ptr<tweet> t;
	tweetdispscr *tdscr;

	tweetdisp(std::shared_ptr<tweet> t_);
	~tweetdisp();
};

struct tpanel : std::enable_shared_from_this<tpanel> {
	std::string name;
	std::map<uint64_t,std::shared_ptr<tweetdisp> > tweetlist;
	tpanelparentwin *twin;

	tpanel(std::string name_);
	void PushTweet(std::shared_ptr<tweet> t);
	tpanelparentwin *MkTPanelWin();
};

struct tpanelparentwin : public wxScrolledWindow {
	//tpanelwin *tpw;
	std::shared_ptr<tpanel> tp;
	wxBoxSizer *sizer;
	std::list<std::pair<uint64_t, tweetdispscr *> > currentdisp;
	bool resize_update_pending;

	tpanelparentwin(std::shared_ptr<tpanel> tp_);
	~tpanelparentwin();
	void PushTweet(std::shared_ptr<tweetdisp> t);
	void resizehandler(wxSizeEvent &event);
	void resizemsghandler(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

//struct tpanelwin : public wxRichTextCtrl {
//	tpanelparentwin *tppw;
//	std::shared_ptr<tpanel> tp;
//
//	tpanelwin(tpanelparentwin *tppw_);
//	~tpanelwin();
//	void PushTweet(std::shared_ptr<tweetdisp> t);
//};
