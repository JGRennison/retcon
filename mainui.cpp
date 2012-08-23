#include "retcon.h"
#include "libtwitcurl/urlencode.h"

BEGIN_EVENT_TABLE(mainframe, wxFrame)
	EVT_MENU(ID_Quit,  mainframe::OnQuit)
	EVT_MENU(ID_About, mainframe::OnAbout)
	EVT_MENU(ID_Settings, mainframe::OnSettings)
	EVT_MENU(ID_Accounts, mainframe::OnAccounts)
	EVT_MENU(ID_Viewlog, mainframe::OnViewlog)
	EVT_MENU(ID_UserLookup, mainframe::OnLookupUser)
	EVT_CLOSE(mainframe::OnClose)
	EVT_MOUSEWHEEL(mainframe::OnMouseWheel)
	EVT_MENU_OPEN(mainframe::OnMenuOpen)
	EVT_MENU_RANGE(tpanelmenustartid, tpanelmenuendid, mainframe::OnTPanelMenuCmd)
END_EVENT_TABLE()

mainframe::mainframe(const wxString& title, const wxPoint& pos, const wxSize& size)
       : wxFrame(NULL, -1, title, pos, size)
{

	mainframelist.push_front(this);

	wxMenu *menuH = new wxMenu;
	menuH->Append( ID_About, wxT("&About"));
	wxMenu *menuF = new wxMenu;
	menuF->Append( ID_Viewlog, wxT("View &Log"));
	menuF->Append( ID_Quit, wxT("E&xit"));
	wxMenu *menuO = new wxMenu;
	menuO->Append( ID_Settings, wxT("&Settings"));
	menuO->Append( ID_Accounts, wxT("&Accounts"));
	tpmenu = new wxMenu;
	wxMenu *searchmenu = new wxMenu;
	searchmenu->Append( ID_UserLookup, wxT("&Lookup User"));

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuF, wxT("&File"));
	menuBar->Append(tpmenu, wxT("&Panels"));
	menuBar->Append(searchmenu, wxT("&Search"));
	menuBar->Append(menuO, wxT("&Options"));
	menuBar->Append(menuH, wxT("&Help"));


	auim=new wxAuiManager(this);

	auib = new tpanelnotebook(this, this);
	auim->AddPane(auib, wxAuiPaneInfo().CentrePane().Resizable());

	tpw=new tweetpostwin(this, this, auim);
	auim->AddPane(tpw, wxAuiPaneInfo().Bottom().Dockable(false).BottomDockable().TopDockable().Floatable().DockFixed().CloseButton(false).CaptionVisible(false).Gripper());

	auim->Update();

	SetMenuBar( menuBar );
	return;
}
void mainframe::OnQuit(wxCommandEvent &event) {
	Close(true);
}
void mainframe::OnAbout(wxCommandEvent &event) {

}
void mainframe::OnSettings(wxCommandEvent &event) {
	settings_window *sw=new settings_window(this, -1, wxT("Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	sw->ShowModal();
	sw->Destroy();
}
void mainframe::OnAccounts(wxCommandEvent &event) {
	acc_window *acc=new acc_window(this, -1, wxT("Accounts"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	//acc->Show(true);
	acc->ShowModal();
	acc->Destroy();
	//delete acc;
}
void mainframe::OnViewlog(wxCommandEvent &event) {
	if(globallogwindow) globallogwindow->LWShow(true);
}
void mainframe::OnClose(wxCloseEvent &event) {
	mainframelist.remove(this);
	if(mainframelist.empty()) {
		if(globallogwindow) globallogwindow->Destroy();
		user_window::CloseAll();
	}
	Destroy();
}

mainframe::~mainframe() {
	mainframelist.remove(this);	//OK to try this twice, must definitely happen at least once though
	if(tpw) {
		tpw->mparentwin=0;
	}
	auim->UnInit();
	delete auim;
}

void mainframe::OnMouseWheel(wxMouseEvent &event) {
	RedirectMouseWheelEvent(event);
}

void mainframe::OnMenuOpen(wxMenuEvent &event) {
	if(event.GetMenu()==tpmenu) {
		MakeTPanelMenu(tpmenu, tpm);
	}
}

void mainframe::OnTPanelMenuCmd(wxCommandEvent &event) {
	TPanelMenuAction(tpm, event.GetId(), this);
}

void mainframe::OnLookupUser(wxCommandEvent &event) {
	//wxString username=::wxGetTextFromUser(wxT("Enter user screen name (eg. @twitter) or numeric identifier (eg. 783214) to look up."), wxT("Lookup User"), wxT(""), this, wxDefaultCoord, wxDefaultCoord, false);
	int type;
	wxString value;
	std::shared_ptr<taccount> acctouse;
	user_lookup_dlg uld(this, &type, &value, acctouse);
	int res=uld.ShowModal();
	if(res==wxID_OK && acctouse && type>=0 && type<=1) {
		twitcurlext *twit=acctouse->cp.GetConn();
		twit->TwInit(acctouse);
		twit->connmode=CS_USERLOOKUPWIN;
		twit->extra1=std::string(value.ToUTF8());
		twit->genurl="api.twitter.com/1/users/show.json";
		if(type==0) twit->genurl+="?screen_name="+urlencode(twit->extra1);
		else if(type==1) twit->genurl+="?user_id="+urlencode(twit->extra1);
		twit->QueueAsyncExec();
	}
}

void AccountUpdateAllMainframes() {
	for(auto it=mainframelist.begin(); it!=mainframelist.end(); ++it) {
		if((*it)->tpw) (*it)->tpw->UpdateAccount();
	}
}

void FreezeAll() {
	for(auto it=mainframelist.begin(); it!=mainframelist.end(); ++it) (*it)->Freeze();
}
void ThawAll() {
	for(auto it=mainframelist.begin(); it!=mainframelist.end(); ++it) (*it)->Thaw();
}

mainframe *GetMainframeAncestor(wxWindow *in, bool passtoplevels) {
	while(in) {
		if(std::count(mainframelist.begin(), mainframelist.end(), in)) return (mainframe *) in;
		if((passtoplevels==false) && in->IsTopLevel()) return 0;
		in=in->GetParent();
	}
	return 0;
}

BEGIN_EVENT_TABLE(tweetpostwin, wxPanel)
	EVT_BUTTON(TPWIN_SENDBTN, tweetpostwin::OnSendBtn)
END_EVENT_TABLE()

void tpw_acc_callback(void *userdata, acc_choice *src, bool isgoodacc) {
	tweetpostwin *win= (tweetpostwin *) userdata;
	win->isgoodacc=isgoodacc;
	win->CheckEnableSendBtn();
}

tweetpostwin::tweetpostwin(wxWindow *parent, mainframe *mparent, wxAuiManager *parentauim)
	: wxPanel(parent, wxID_ANY, wxPoint(-1000, -1000)), parentwin(parent), mparentwin(mparent), pauim(0) {

	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);
	SetSizer(vbox);
	infost=new wxStaticText(this, wxID_ANY, wxT("0/140"), wxPoint(-1000, -1000));
	textctrl=new wxTextCtrl(this, TPWID_TEXTCTRL, wxT(""), wxPoint(-1000, -1000), wxDefaultSize, wxTE_MULTILINE | wxTE_NOHIDESEL | wxTE_BESTWRAP);
	vbox->Add(textctrl, 0, wxEXPAND | wxALL, 2);

	wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(hbox, 0, wxEXPAND | wxALL, 2);

	sendbtn=new wxButton(this, TPWIN_SENDBTN, wxT("Send"), wxPoint(-1000, -1000));
	accc=new acc_choice(this, curacc, 0, wxID_ANY, &tpw_acc_callback, this);
	hbox->Add(accc, 0, wxALL, 2);
	hbox->AddStretchSpacer();
	hbox->Add(infost, 0, wxALL, 2);
	hbox->Add(sendbtn, 0, wxALL, 2);

	SetSizer(vbox);
	DoShowHide(false);

	textctrl->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCFocus), 0, this);
	textctrl->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCUnFocus), 0, this);
	textctrl->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(tweetpostwin::OnTCChange), 0, this);

	Fit();
	pauim=parentauim;
}

tweetpostwin::~tweetpostwin() {
	if(mparentwin) {
		mparentwin->tpw=0;
	}
}

void tweetpostwin::OnSendBtn(wxCommandEvent &event) {

}

void tweetpostwin::OnTCFocus(wxFocusEvent &event) {
	DoShowHide(true);
}
void tweetpostwin::OnTCUnFocus(wxFocusEvent &event) {
	if(textctrl->IsEmpty()) DoShowHide(false);
}
void tweetpostwin::DoShowHide(bool show) {
	accc->Show(show);
	sendbtn->Show(show);
	if(pauim) {
		wxAuiPaneInfo pi=pauim->GetPane(this);
		pauim->DetachPane(this);
		pi.floating_size = wxDefaultSize;
		pi.best_size = wxDefaultSize;
		pi.min_size = wxDefaultSize;
		pi.max_size = wxDefaultSize;
		Fit();
		pauim->AddPane(this, pi);
		pauim->Update();
	}
	else {
		Fit();
	}
}

void tweetpostwin::OnTCChange(wxCommandEvent &event) {
	unsigned int len=TwitterCharCount(std::string(textctrl->GetValue().ToUTF8()));
	infost->SetLabel(wxString::Format(wxT("%d/140"), len));
	CheckEnableSendBtn();
}

void tweetpostwin::UpdateAccount() {
	accc->fill_acc();
}

void tweetpostwin::CheckEnableSendBtn() {
	sendbtn->Enable(isgoodacc && !(textctrl->IsEmpty()));
}
