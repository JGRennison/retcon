struct tweetpostwin;

enum
{
    ID_Quit = wxID_EXIT,
    ID_About = wxID_ABOUT,
    ID_Settings = 1,
    ID_Accounts,
    ID_Viewlog,
    ID_UserLookup,
};

class mainframe: public wxFrame
{
public:
	tpanelnotebook *auib;
	tweetpostwin *tpw;
	tpanelmenudata tpm;
	wxMenu *tpmenu;
	wxAuiManager *auim;

	mainframe(const wxString& title, const wxPoint& pos, const wxSize& size);
	~mainframe();
	void OnQuit(wxCommandEvent &event);
	void OnAbout(wxCommandEvent &event);
	void OnSettings(wxCommandEvent &event);
	void OnAccounts(wxCommandEvent &event);
	void OnViewlog(wxCommandEvent &event);
	void OnClose(wxCloseEvent &event);
	void OnMouseWheel(wxMouseEvent &event);
	void OnMenuOpen(wxMenuEvent &event);
	void OnTPanelMenuCmd(wxCommandEvent &event);
	void OnLookupUser(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

enum {
	TPWID_TEXTCTRL = 1,
	TPWIN_SENDBTN,
};

struct tweetpostwin : public wxPanel {
	wxTextCtrl *textctrl;
	wxWindow *parentwin;
	mainframe *mparentwin;
	acc_choice *accc;
	std::shared_ptr<taccount> curacc;
	wxButton *sendbtn;
	wxAuiManager *pauim;
	wxStaticText *infost;
	bool isgoodacc;

	tweetpostwin(wxWindow *parent, mainframe *mparent, wxAuiManager *parentauim=0);
	~tweetpostwin();
	void OnSendBtn(wxCommandEvent &event);
	void OnTCFocus(wxFocusEvent &event);
	void OnTCUnFocus(wxFocusEvent &event);
	void OnTCChange(wxCommandEvent &event);
	void DoShowHide(bool show);
	void UpdateAccount();
	void CheckEnableSendBtn();

	DECLARE_EVENT_TABLE()
};

mainframe *GetMainframeAncestor(wxWindow *in, bool passtoplevels=false);
void FreezeAll();
void ThawAll();
void AccountUpdateAllMainframes();