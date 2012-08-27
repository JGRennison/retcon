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
	auim->SetDockSizeConstraint(1.0, 1.0);

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

DECLARE_EVENT_TYPE(wxextTPRESIZE_UPDATE_EVENT, -1)
DEFINE_EVENT_TYPE(wxextTPRESIZE_UPDATE_EVENT)

BEGIN_EVENT_TABLE(tweetposttextbox, wxRichTextCtrl)
	//EVT_RICHTEXT_CHARACTER(wxID_ANY, tweetposttextbox::OnTCChar)
	EVT_TEXT(wxID_ANY, tweetposttextbox::OnTCUpdate)
END_EVENT_TABLE()

tweetposttextbox::tweetposttextbox(tweetpostwin *parent_, const wxString &deftext, wxWindowID id)
	: wxRichTextCtrl(parent_, id, deftext, wxPoint(-1000, -1000), wxDefaultSize, wxRE_MULTILINE | wxWANTS_CHARS), parent(parent_), lastheight(0) {
}

tweetposttextbox::~tweetposttextbox() {
	if(parent) {
		parent->textctrl=0;
	}
}

void tweetposttextbox::OnTCChar(wxRichTextEvent &event) {

}

void tweetposttextbox::OnTCUpdate(wxCommandEvent &event) {
	if(parent) parent->OnTCChange();
}

void tweetposttextbox::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
		       int noUnitsX, int noUnitsY,
		       int xPos, int yPos,
		       bool noRefresh ) {
	wxRichTextCtrl::SetScrollbars(0, 0, 0, 0, 0, 0, noRefresh);
	int newheight=(pixelsPerUnitY*noUnitsY)+4;
	int curheight;
	GetSize(0, &curheight);
	if(parent && !parent->resize_update_pending && lastheight!=newheight && curheight!=newheight) {
		parent->vbox->SetItemMinSize(this, 10, newheight);
		parent->resize_update_pending=true;
		lastheight=newheight;
		wxCommandEvent event(wxextTPRESIZE_UPDATE_EVENT, GetId());
		parent->GetEventHandler()->AddPendingEvent(event);
	}
}

void tweetposttextbox::SetCursorToEnd() {
	SetCaretPosition(GetLastPosition());
	SetFocus();
	if(parent && parent->mparentwin) parent->mparentwin->Raise();
}

BEGIN_EVENT_TABLE(tweetpostwin, wxPanel)
	EVT_BUTTON(TPWIN_SENDBTN, tweetpostwin::OnSendBtn)
	EVT_BUTTON(TPWID_CLOSEREPDESC, tweetpostwin::OnCloseReplyDescBtn)
	EVT_COMMAND(wxID_ANY, wxextTPRESIZE_UPDATE_EVENT, tweetpostwin::resizemsghandler)
END_EVENT_TABLE()

void tpw_acc_callback(void *userdata, acc_choice *src, bool isgoodacc) {
	tweetpostwin *win= (tweetpostwin *) userdata;
	win->isgoodacc=isgoodacc;
	win->CheckEnableSendBtn();
}

tweetpostwin::tweetpostwin(wxWindow *parent, mainframe *mparent, wxAuiManager *parentauim)
	: wxPanel(parent, wxID_ANY, wxPoint(-1000, -1000)), parentwin(parent), mparentwin(mparent),
	pauim(0), isshown(false), resize_update_pending(true), currently_posting(false), tc_has_focus(false),
	current_length(0), length_oob(false) {

	vbox = new wxBoxSizer(wxVERTICAL);
	infost=new wxStaticText(this, wxID_ANY, wxT("0/140"), wxPoint(-1000, -1000));
	statusst=new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000));
	replydesc=new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000), wxDefaultSize, wxST_NO_AUTORESIZE);
	replydesclosebtn=new wxBitmapButton(this, TPWID_CLOSEREPDESC, tpanelglobal::Get()->infoicon, wxPoint(-1000, -1000));
	wxBoxSizer *replydescbox= new wxBoxSizer(wxHORIZONTAL);
	replydescbox->Add(replydesc, 1, wxEXPAND | wxALL, 1);
	replydescbox->Add(replydesclosebtn, 0, wxALL, 1);
	textctrl=new tweetposttextbox(this, wxT(""), TPWID_TEXTCTRL);
	vbox->Add(replydescbox, 0, wxEXPAND | wxALL, 2);
	vbox->Add(textctrl, 0, wxEXPAND | wxALL, 2);
	replydesc->Show(false);
	replydescbox->Show(false);

	wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(hbox, 0, wxEXPAND | wxALL, 2);

	sendbtn=new wxButton(this, TPWIN_SENDBTN, wxT("Send"), wxPoint(-1000, -1000));
	accc=new acc_choice(this, curacc, 0, wxID_ANY, &tpw_acc_callback, this);
	hbox->Add(accc, 0, wxALL, 2);
	hbox->AddStretchSpacer();
	hbox->Add(statusst, 0, wxALL, 2);
	hbox->Add(infost, 0, wxALL, 2);
	hbox->Add(sendbtn, 0, wxALL, 2);

	textctrl->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCFocus), 0, this);
	textctrl->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCUnFocus), 0, this);
	accc->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCFocus), 0, this);
	accc->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCUnFocus), 0, this);
	sendbtn->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCFocus), 0, this);
	sendbtn->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCUnFocus), 0, this);

	SetSizer(vbox);
	DoShowHide(false);

	Fit();
	pauim=parentauim;
	resize_update_pending=false;
}

tweetpostwin::~tweetpostwin() {
	if(mparentwin) {
		mparentwin->tpw=0;
	}
	if(textctrl) {
		textctrl->parent=0;
	}
}

void tweetpostwin::OnSendBtn(wxCommandEvent &event) {
	if(isgoodacc && curacc && !currently_posting && !textctrl->IsEmpty() && current_length<=140) {
		currently_posting=true;
		OnTCChange();
		twitcurlext *twit=curacc->cp.GetConn();
		twit->TwInit(curacc);
		twit->extra1=textctrl->GetValue().ToUTF8();
		if(dm_targ) {
			twit->connmode=CS_SENDDM;
			twit->extra_id=dm_targ->id;
		}
		else {
			twit->connmode=CS_POSTTWEET;
			twit->extra_id=(tweet_reply_targ)?tweet_reply_targ->id:0;
		}
		twit->ownermainframe=mparentwin;
		twit->QueueAsyncExec();
	}
}

void tweetpostwin::DoShowHide(bool show) {
	isshown=show;
	accc->Show(show);
	sendbtn->Show(show);
	infost->Show(show);
	statusst->Show(show);
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
	else {textctrl->SetCursorToEnd();
		Fit();
	}
}

void tweetpostwin::OnTCFocus(wxFocusEvent &event) {
	tc_has_focus=true;
	DoCheckFocusDisplay();
	event.Skip();
}

void tweetpostwin::OnTCUnFocus(wxFocusEvent &event) {
	wxWindow *checkwin=event.GetWindow();
	while(true) {
		if(!checkwin) {
			tc_has_focus=false;
			DoCheckFocusDisplay();
			break;
		}
		else if(checkwin==this) break;
		checkwin=checkwin->GetParent();
	}
	event.Skip();
}

void tweetpostwin::DoCheckFocusDisplay(bool force) {
	bool shouldshow=false;
	if(!textctrl->IsEmpty()) shouldshow=true;
	if(dm_targ || tweet_reply_targ) shouldshow=true;
	if(tc_has_focus) shouldshow=true;
	if(isshown != shouldshow || force) DoShowHide(shouldshow);
}

void tweetpostwin::OnTCChange() {
	current_length=TwitterCharCount(std::string(textctrl->GetValue().ToUTF8()));
	if(current_length>140) {
		if(!length_oob) {
			infost_colout=infost->GetForegroundColour();
			infost->SetOwnForegroundColour(*wxRED);
			length_oob=true;
		}
	}
	else {
		if(length_oob) {
			infost->SetOwnForegroundColour(infost_colout);
			length_oob=false;
		}
	}
	infost->SetLabel(wxString::Format(wxT("%d/140"), current_length));
	statusst->SetLabel(currently_posting?wxT("Posting"):wxT(""));
	CheckEnableSendBtn();
	textctrl->Enable(!currently_posting);
}

void tweetpostwin::UpdateAccount() {
	accc->fill_acc();
}

void tweetpostwin::CheckEnableSendBtn() {
	sendbtn->Enable(isgoodacc && !currently_posting && !(textctrl->IsEmpty()) && current_length<=140);
}

void tweetpostwin::resizemsghandler(wxCommandEvent &event) {
	DoShowHide(isshown);
	resize_update_pending=false;
}

void tweetpostwin::NotifyPostResult(bool success) {
	if(success) textctrl->Clear();
	currently_posting=false;
	OnTCChange();
}

void tweetpostwin::UpdateReplyDesc() {
	if(tweet_reply_targ) {
		replydesc->SetLabel(wxT("Reply to: @") + wxstrstd(tweet_reply_targ->user->GetUser().screen_name) + wxT(": ") + wxstrstd(tweet_reply_targ->text));
		replydesc->Show(true);
		replydesclosebtn->Show(true);
		sendbtn->SetLabel(wxT("Reply"));
	}
	else if(dm_targ) {
		replydesc->SetLabel(wxT("Direct Message: @") + wxstrstd(dm_targ->GetUser().screen_name));
		replydesc->Show(true);
		replydesclosebtn->Show(true);
		sendbtn->SetLabel(wxT("Send DM"));
	}
	else {
		replydesc->Show(false);
		replydesclosebtn->Show(false);
		sendbtn->SetLabel(wxT("Send"));
	}
	DoCheckFocusDisplay(true);
}

void tweetpostwin::OnCloseReplyDescBtn(wxCommandEvent &event) {
	tweet_reply_targ.reset();
	dm_targ.reset();
	UpdateReplyDesc();
}

void tweetpostwin::SetReplyTarget(const std::shared_ptr<tweet> &targ) {
	wxString curtext=textctrl->GetValue();
	if(targ && true) {	//regex goes here
		textctrl->SetInsertionPoint(0);
		textctrl->WriteText(wxT("@") + wxstrstd(targ->user->GetUser().screen_name) + wxT(" "));
		OnTCChange();
	}
	tweet_reply_targ=targ;
	dm_targ.reset();
	UpdateReplyDesc();
	textctrl->SetCursorToEnd();
}
void tweetpostwin::SetDMTarget(const std::shared_ptr<userdatacontainer> &targ) {
	tweet_reply_targ.reset();
	dm_targ=targ;
	UpdateReplyDesc();
	textctrl->SetCursorToEnd();
}
