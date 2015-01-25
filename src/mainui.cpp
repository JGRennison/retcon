//  retcon
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "mainui.h"
#include "aboutwin.h"
#include "tpanel.h"
#include "tpanel-aux.h"
#include "tpg.h"
#include "log-impl.h"
#include "twit.h"
#include "magic_ptr.h"
#include "dispscr.h"
#include "optui.h"
#include "alldata.h"
#include "userui.h"
#include "taccount.h"
#include "util.h"
#include "twitcurlext.h"
#include "retcon.h"
#include "version.h"
#include <wx/msgdlg.h>
#include <wx/app.h>
#include <wx/filedlg.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#if HANDLE_PRIMARY_CLIPBOARD
#include <wx/clipbrd.h>
#endif
#include <forward_list>
#include <algorithm>

#define MAX_IMAGE_ATTACHMENTS_PER_TWEET 4

std::vector<mainframe*> mainframelist;

BEGIN_EVENT_TABLE(mainframe, wxFrame)
	EVT_MENU(ID_Close,  mainframe::OnCloseWindow)
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
	EVT_MENU_RANGE(lookupprofilestartid, lookupprofileendid, mainframe::OnOwnProfileMenuCmd)
	EVT_MOVE(mainframe::OnMove)
	EVT_SIZE(mainframe::OnSize)
END_EVENT_TABLE()

mainframe::mainframe(const wxString& title, const wxPoint& pos, const wxSize& size, bool maximise)
		: wxFrame(nullptr, -1, DecorateTitle(title), pos, size), origtitle(title) {
	nominal_pos = pos;
	nominal_size = size;
	if(maximise) Maximize(true);

	mainframelist.push_back(this);

	wxMenu *menuH = new wxMenu;
	menuH->Append(ID_About, wxT("&About"));
	wxMenu *menuF = new wxMenu;
	menuF->Append(ID_Viewlog, wxT("View &Log"));
	menuF->Append(ID_Close, wxT("&Close Window"));
	menuF->Append(ID_Quit, wxT("E&xit"));
	wxMenu *menuO = new wxMenu;
	menuO->Append(ID_Settings, wxT("&Settings"));
	menuO->Append(ID_Accounts, wxT("&Accounts"));

	tpmenu = new wxMenu;
	lookupmenu = new wxMenu;

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuF, wxT("&File"));
	menuBar->Append(tpmenu, wxT("&Panels"));
	menuBar->Append(lookupmenu, wxT("&Lookup"));
	menuBar->Append(menuO, wxT("&Options"));
	menuBar->Append(menuH, wxT("&Help"));


	auim = new wxAuiManager(this);
	auim->SetDockSizeConstraint(1.0, 1.0);

	auib = new tpanelnotebook(this, this);
	auim->AddPane(auib, wxAuiPaneInfo().CentrePane().Resizable());

	tpw = new tweetpostwin(this, this, auim);
	auim->AddPane(tpw, wxAuiPaneInfo().Bottom().Dockable(false).BottomDockable().TopDockable().DockFixed().CloseButton(false).CaptionVisible(false));

	auim->Update();

	SetMenuBar(menuBar);

	LogMsgFormat(LOGT::OTHERTRACE, "Creating new mainframe: %p, %d mainframes", this, mainframelist.size());

	return;
}
void mainframe::OnCloseWindow(wxCommandEvent &event) {
	Close(true);
}
void mainframe::OnQuit(wxCommandEvent &event) {
	LogMsgFormat(LOGT::OTHERTRACE, "mainframe::OnQuit: %p, %d mainframes", this, mainframelist.size());
	SaveWindowLayout();
	ad.twinlayout_final = true;
	std::vector<mainframe *> tempmf = mainframelist;
	for(auto &mf : tempmf) {
		mf->Close(true);
	}
}
void mainframe::OnAbout(wxCommandEvent &event) {
	OpenAboutWindow();
}
void mainframe::OnSettings(wxCommandEvent &event) {
	settings_window *sw = new settings_window(this, -1, wxT("Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	sw->ShowModal();
	sw->Destroy();
}
void mainframe::OnAccounts(wxCommandEvent &event) {
	acc_window *acc = new acc_window(this, -1, wxT("Accounts"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
	acc->ShowModal();
	acc->Destroy();
}
void mainframe::OnViewlog(wxCommandEvent &event) {
	if(globallogwindow) globallogwindow->LWShow(true);
}
void mainframe::OnClose(wxCloseEvent &event) {
	LogMsgFormat(LOGT::OTHERTRACE, "mainframe::OnClose: %p, %d mainframes", this, mainframelist.size());
	SaveWindowLayout();
	mainframelist.erase(std::remove(mainframelist.begin(), mainframelist.end(), this), mainframelist.end());
	if(mainframelist.empty()) {
		if(globallogwindow) globallogwindow->Destroy();
		user_window::CloseAll();
	}
	Destroy();
}

mainframe::~mainframe() {
	//OK to try this twice, must definitely happen at least once though
	mainframelist.erase(std::remove(mainframelist.begin(), mainframelist.end(), this), mainframelist.end());

	if(tpw) {
		tpw->mparentwin = nullptr;
		tpw->AUIMNoLongerValid();
	}

	auim->UnInit();
	delete auim;

	LogMsgFormat(LOGT::OTHERTRACE, "Deleting mainframe: %p, %d mainframes, top win: %p, popup recursion: %d", this, mainframelist.size(), wxGetApp().GetTopWindow(), wxGetApp().popuprecursion);

	if(mainframelist.empty()) {
		if(globallogwindow) globallogwindow->Destroy();
		user_window::CloseAll();
		wxExit(); // Be a bit more aggressive in terminating program, no point hanging around at this point
	}
}

void mainframe::OnMouseWheel(wxMouseEvent &event) {
	RedirectMouseWheelEvent(event);
}

void mainframe::OnMenuOpen(wxMenuEvent &event) {
	if(event.GetMenu() == tpmenu) {
		MakeTPanelMenu(tpmenu, tpm);
	}
	else if(event.GetMenu() == lookupmenu) {
		wxMenuItemList items = lookupmenu->GetMenuItems();		//make a copy to avoid memory issues if Destroy modifies the list
		for(auto &it : items) {
			lookupmenu->Destroy(it);
		}
		proflookupidmap.clear();

		int nextid = lookupprofilestartid;
		lookupmenu->Append(ID_UserLookup, wxT("&Lookup User"));
		wxMenu *profmenu = new wxMenu;
		for(auto &it : alist) {
			profmenu->Append(nextid, it->dispname);
			proflookupidmap[nextid] = it->dbindex;
			nextid++;
		}
		lookupmenu->AppendSubMenu(profmenu, wxT("&Own Profile"));
	}
}

void mainframe::OnOwnProfileMenuCmd(wxCommandEvent &event) {
	std::shared_ptr<taccount> acc;
	if(GetAccByDBIndex(proflookupidmap[event.GetId()], acc)) {
		if(acc->usercont) user_window::MkWin(acc->usercont->id, acc);
	}
}

void mainframe::OnTPanelMenuCmd(wxCommandEvent &event) {
	TPanelMenuAction(tpm, event.GetId(), this);
}

void mainframe::OnLookupUser(wxCommandEvent &event) {
	int type;
	wxString value;
	std::shared_ptr<taccount> acctouse;
	user_lookup_dlg uld(this, &type, &value, acctouse);
	int res = uld.ShowModal();
	if(res == wxID_OK && acctouse && type >= 0 && type <= 1) {
		std::string search_string = std::string(value.ToUTF8());
		std::unique_ptr<twitcurlext_userlookupwin> twit = twitcurlext_userlookupwin::make_new(acctouse,
				type == 1 ? twitcurlext_userlookupwin::LOOKUPMODE::ID : twitcurlext_userlookupwin::LOOKUPMODE::SCREENNAME, search_string);

		if(type == 1) {
			uint64_t id = 0;
			if(ownstrtonum(id, search_string.data(), search_string.size())) {
				udc_ptr u = ad.GetExistingUserContainerById(id);
				if(u && !u->GetUser().screen_name.empty()) {
					user_window::MkWin(u->id, acctouse);
				}
			}
		}

		twitcurlext::QueueAsyncExec(std::move(twit));
	}
}

void mainframe::OnSize(wxSizeEvent &event) {
	if(!IsMaximized()) {
		nominal_size = event.GetSize();
		nominal_pos = GetPosition();
	}
}

void mainframe::OnMove(wxMoveEvent &event) {
	if(!IsMaximized()) nominal_pos = event.GetPosition();
}

wxString mainframe::DecorateTitle(wxString basetitle) {
#ifdef __WXDEBUG__
	if(basetitle == appversionname) basetitle = wxT("Retcon ") + appbuildversion;
	basetitle += wxT("  (debug build)");
#endif
	if(gc.readonlymode) basetitle += wxT("  -*- READ-ONLY MODE -*-");
	if(gc.allaccsdisabled) basetitle += wxT("  =#= All Accounts Disabled =#=");
	return std::move(basetitle);
}

void mainframe::ResetTitle() {
	SetTitle(DecorateTitle(origtitle));
}

void mainframe::ResetAllTitles() {
	for(auto &it : mainframelist) {
		it->ResetTitle();
	}
}

void AccountUpdateAllMainframes() {
	for(auto it=mainframelist.begin(); it!=mainframelist.end(); ++it) {
		if((*it)->tpw) (*it)->tpw->UpdateAccount();
	}
}

void FreezeAll() {
	for(auto &it : mainframelist) it->Freeze();
}
void ThawAll() {
	for(auto &it : mainframelist) it->Thaw();
}

mainframe *GetMainframeAncestor(wxWindow *in, bool passtoplevels) {
	while(in) {
		if(std::count(mainframelist.begin(), mainframelist.end(), in)) return static_cast<mainframe *>(in);
		if((passtoplevels == false) && in->IsTopLevel()) return nullptr;
		in = in->GetParent();
	}
	return nullptr;
}

DECLARE_EVENT_TYPE(wxextTPRESIZE_UPDATE_EVENT, -1)
DEFINE_EVENT_TYPE(wxextTPRESIZE_UPDATE_EVENT)

BEGIN_EVENT_TABLE(tweetposttextbox, wxRichTextCtrl)
	EVT_TEXT(wxID_ANY, tweetposttextbox::OnTCUpdate)
#if HANDLE_PRIMARY_CLIPBOARD
	EVT_LEFT_UP(tweetposttextbox::OnLeftUp)
	EVT_MIDDLE_DOWN(tweetposttextbox::OnMiddleClick)
#endif
END_EVENT_TABLE()

tweetposttextbox::tweetposttextbox(tweetpostwin *parent_, const wxString &deftext, wxWindowID id)
	: wxRichTextCtrl(parent_, id, deftext, wxPoint(-1000, -1000), wxDefaultSize, wxRE_MULTILINE | wxWANTS_CHARS), parent(parent_), lastheight(0) {
}

tweetposttextbox::~tweetposttextbox() {
	if(parent) {
		parent->textctrl = nullptr;
	}
}

void tweetposttextbox::OnTCChar(wxRichTextEvent &event) { }

void tweetposttextbox::OnTCUpdate(wxCommandEvent &event) {
	if(parent) parent->OnTCChange();
}

void tweetposttextbox::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
		int noUnitsX, int noUnitsY,
		int xPos, int yPos,
		bool noRefresh ) {
	wxRichTextCtrl::SetScrollbars(0, 0, 0, 0, 0, 0, noRefresh);
	int newheight = (pixelsPerUnitY * noUnitsY) + 4;
	int curheight;
	GetSize(0, &curheight);
	if(parent && !parent->resize_update_pending && lastheight!=newheight && curheight!=newheight) {
		parent->vbox->SetItemMinSize(this, 10, newheight);
		parent->resize_update_pending = true;
		lastheight = newheight;
		wxCommandEvent event(wxextTPRESIZE_UPDATE_EVENT, GetId());
		parent->GetEventHandler()->AddPendingEvent(event);
	}
}

void tweetposttextbox::SetCursorToEnd() {
	SetCaretPosition(GetLastPosition());
	SetInsertionPoint(GetLastPosition());
	SetFocus();
	if(parent && parent->mparentwin) parent->mparentwin->Raise();
}

#if HANDLE_PRIMARY_CLIPBOARD
// This is effectively a backport of http://trac.wxwidgets.org/changeset/70011

void tweetposttextbox::OnLeftUp(wxMouseEvent& event) {
	wxTheClipboard->UsePrimarySelection(true);
	Copy();
	wxTheClipboard->UsePrimarySelection(false);

	// Propagate
	event.Skip(true);
}

void tweetposttextbox::OnMiddleClick(wxMouseEvent& event) {
	wxTheClipboard->UsePrimarySelection(true);
	Paste();
	wxTheClipboard->UsePrimarySelection(false);
}

#endif

BEGIN_EVENT_TABLE(tweetpostwin, wxPanel)
	EVT_BUTTON(TPWIN_SENDBTN, tweetpostwin::OnSendBtn)
	EVT_BUTTON(TPWID_CLOSEREPDESC, tweetpostwin::OnCloseReplyDescBtn)
	EVT_BUTTON(TPWID_CLEARTEXT, tweetpostwin::OnClearTextBtn)
	EVT_BUTTON(TPWID_ADDNAMES, tweetpostwin::OnAddNamesBtn)
	EVT_BUTTON(TPWID_TOGGLEREPDESCLOCK, tweetpostwin::OnToggleReplyDescLockBtn)
	EVT_BUTTON(TPWID_ADDIMG, tweetpostwin::OnAddImgBtn)
	EVT_BUTTON(TPWID_DELIMG, tweetpostwin::OnDelImgBtn)
	EVT_COMMAND(wxID_ANY, wxextTPRESIZE_UPDATE_EVENT, tweetpostwin::resizemsghandler)
END_EVENT_TABLE()

void tpw_acc_callback(void *userdata, acc_choice *src, bool isgoodacc) {
	tweetpostwin *win = (tweetpostwin *) userdata;
	win->isgoodacc=isgoodacc;
	win->CheckEnableSendBtn();
}

tweetpostwin::tweetpostwin(wxWindow *parent, mainframe *mparent, wxAuiManager *parentauim)
		: wxPanel(parent, wxID_ANY, wxPoint(-1000, -1000)), parentwin(parent), mparentwin(mparent) {

	tpg = tpanelglobal::Get();
	vbox = new wxBoxSizer(wxVERTICAL);
	infost = new wxStaticText(this, wxID_ANY, wxT("0/140"), wxPoint(-1000, -1000), wxDefaultSize);
	replydesc = new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000), wxDefaultSize, wxST_NO_AUTORESIZE);
	replydeslockbtn = new wxBitmapButton(this, TPWID_TOGGLEREPDESCLOCK, GetReplyDescLockBtnBitmap(), wxPoint(-1000, -1000));
	replydesclosebtn = new wxBitmapButton(this, TPWID_CLOSEREPDESC, tpg->closeicon, wxPoint(-1000, -1000));
	addnamesbtn = new wxButton(this, TPWID_ADDNAMES, wxT("Add names \x2193"), wxPoint(-1000, -1000), wxDefaultSize, wxBU_EXACTFIT);
	addnamesbtn->Show(false);
	wxBoxSizer *replydescbox = new wxBoxSizer(wxHORIZONTAL);
	replydescbox->Add(replydesc, 1, wxEXPAND | wxALL, 1);
	replydescbox->Add(addnamesbtn, 0, wxLEFT | wxRIGHT | wxALIGN_CENTRE, 1);
	replydescbox->Add(replydeslockbtn, 0, wxALL | wxALIGN_CENTRE, 1);
	replydescbox->Add(replydesclosebtn, 0, wxALL | wxALIGN_CENTRE, 1);
	textctrl = new tweetposttextbox(this, wxT(""), TPWID_TEXTCTRL);
	cleartextbtn = new wxBitmapButton(this, TPWID_CLEARTEXT, tpg->closeicon, wxPoint(-1000, -1000));
	cleartextbtn->Show(false);
	wxBoxSizer *tweetpostbox = new wxBoxSizer(wxHORIZONTAL);
	tweetpostbox->Add(textctrl, 1, wxEXPAND | wxALL, 1);
	tweetpostbox->Add(cleartextbtn, 0, wxALL | wxALIGN_CENTRE, 1);
	vbox->Add(replydescbox, 0, wxEXPAND | wxALL, 2);
	vbox->Add(tweetpostbox, 0, wxEXPAND | wxALL, 2);
	replydesc->Show(false);
	replydescbox->Show(false);

	hbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(hbox, 0, wxEXPAND | wxALL, 2);

	addimagebtn = new wxBitmapButton(this, TPWID_ADDIMG, tpg->photoicon, wxPoint(-1000, -1000));
	delimagebtn = new wxBitmapButton(this, TPWID_DELIMG, tpg->closeicon, wxPoint(-1000, -1000));
	imagestattxt = new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000), wxDefaultSize);

	sendbtn = new wxButton(this, TPWIN_SENDBTN, wxT("Send"), wxPoint(-1000, -1000));
	accc = new acc_choice(this, curacc, 0, wxID_ANY, &tpw_acc_callback, this);
	hbox->Add(accc, 0, wxALL, 2);
	hbox->Add(addimagebtn, 0, wxALL, 2);
	hbox->Add(imagestattxt, 0, wxALL, 2);
	hbox->Add(delimagebtn, 0, wxALL, 2);
	hbox->AddStretchSpacer();
	hbox->Add(infost, 0, wxALL, 2);
	hbox->Add(sendbtn, 0, wxALL, 2);

	auto set_focus_handler = [&](wxWindow *win) {
		win->Connect(wxEVT_SET_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCFocus), 0, this);
		win->Connect(wxEVT_KILL_FOCUS, wxFocusEventHandler(tweetpostwin::OnTCUnFocus), 0, this);
	};

	set_focus_handler(textctrl);
	set_focus_handler(accc);
	set_focus_handler(sendbtn);
	set_focus_handler(addimagebtn);
	set_focus_handler(delimagebtn);

	SetSizer(vbox);
	DoShowHide(false);

	Fit();
	pauim = parentauim;
	resize_update_pending = false;
}

tweetpostwin::~tweetpostwin() {
	if(mparentwin) {
		mparentwin->tpw = nullptr;
	}
	if(textctrl) {
		textctrl->parent = nullptr;
	}
}

bool tweetpostwin::okToSend() {
	return isgoodacc && !currently_posting && !(textctrl->IsEmpty() && image_upload_filenames.empty()) && current_length <= 140;
}

void tweetpostwin::OnSendBtn(wxCommandEvent &event) {
	std::string curtext = stdstrwx(textctrl->GetValue());
	if(okToSend()) {
		if(tweet_reply_targ && !IsUserMentioned(curtext, tweet_reply_targ->user)) {
			int res = ::wxMessageBox(wxString::Format(wxT("User: @%s is not mentioned in this tweet. Reply anyway?"),
					wxstrstd(tweet_reply_targ->user->GetUser().screen_name).c_str()), wxT("Confirm"), wxYES_NO | wxICON_QUESTION, this);
			if(res != wxYES) return;
		}
		currently_posting = true;
		OnTCChange();

		std::unique_ptr<twitcurlext_postcontent> twit;
		if(dm_targ) {
			twit = twitcurlext_postcontent::make_new(curacc, twitcurlext_postcontent::CONNTYPE::SENDDM);
			twit->dmtarg_id = dm_targ->id;
		}
		else {
			twit = twitcurlext_postcontent::make_new(curacc, twitcurlext_postcontent::CONNTYPE::POSTTWEET);
			if(tweet_reply_targ)
				twit->replyto_id = tweet_reply_targ->id;
			twit->SetImageUploads(image_upload_filenames);
		}
		twit->text = curtext;
		twit->ownermainframe = mparentwin;

		twitcurlext::QueueAsyncExec(std::move(twit));
	}
}

void tweetpostwin::DoShowHide(bool show) {
	isshown = show;
	accc->Show(show);
	sendbtn->Show(show);
	infost->Show(show);
	ShowHideImageUploadBtns(!show);
	if(pauim) {
		wxAuiPaneInfo pi = pauim->GetPane(this);
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
		textctrl->SetCursorToEnd();
		Fit();
	}
}

void tweetpostwin::OnTCFocus(wxFocusEvent &event) {
	tc_has_focus++;
	DoCheckFocusDisplay();
	event.Skip();
}

void tweetpostwin::OnTCUnFocus(wxFocusEvent &event) {
	tc_has_focus--;
	wxWindow *checkwin = event.GetWindow();
	while(true) {
		if(!checkwin) {
			DoCheckFocusDisplay();
			break;
		}
		else if(checkwin == this) break;
		checkwin = checkwin->GetParent();
	}
	event.Skip();
}

void tweetpostwin::DoCheckFocusDisplay(bool force) {
	bool shouldshow = false;
	if(!textctrl->IsEmpty()) shouldshow = true;
	if(!image_upload_filenames.empty()) shouldshow = true;
	if(dm_targ || tweet_reply_targ) shouldshow = true;
	if(tc_has_focus > 0) shouldshow = true;
	if(isshown != shouldshow || force) DoShowHide(shouldshow);
}

void tweetpostwin::OnTCChange() {
	current_length = TwitterCharCount(std::string(textctrl->GetValue().ToUTF8()), image_upload_filenames.empty() ? 0 : 1);
	if(current_length > 140) {
		if(!length_oob) {
			infost_colout = infost->GetForegroundColour();
			infost->SetOwnForegroundColour(*wxRED);
			length_oob = true;
		}
	}
	else {
		if(length_oob) {
			infost->SetOwnForegroundColour(infost_colout);
			length_oob = false;
		}
	}
	infost->SetLabel(wxString::Format(wxT("%s%d/140"), currently_posting ? wxT("Posting - ") : wxT(""), current_length));
	CheckEnableSendBtn();
	textctrl->Enable(!currently_posting);
	cleartextbtn->Show(!textctrl->IsEmpty());
	CheckAddNamesBtn();
	ShowHideImageUploadBtns(!isshown, !currently_posting);
	vbox->Layout();
}

void tweetpostwin::UpdateAccount() {
	accc->fill_acc();
}

void tweetpostwin::CheckEnableSendBtn() {
	sendbtn->Enable(okToSend());
}

void tweetpostwin::resizemsghandler(wxCommandEvent &event) {
	DoShowHide(isshown);
	resize_update_pending = false;
}

void tweetpostwin::NotifyPostResult(bool success) {
	if(success) {
		textctrl->Clear();
		ClearImageUploadFilenames();
		if(!replydesc_locked) {
			tweet_reply_targ.reset();
			dm_targ.reset();
		}
		UpdateReplyDesc();
	}
	currently_posting = false;
	OnTCChange();
}

void tweetpostwin::UpdateReplyDesc() {
	iumc.reset();
	if(tweet_reply_targ) {
		replydesc->SetLabel(wxT("Reply to: @") + wxstrstd(tweet_reply_targ->user->GetUser().screen_name) + wxT(": ") + wxstrstd(TweetReplaceAllStringSeqs(tweet_reply_targ->text)));
		replydesc->Show(true);
		replydesclosebtn->Show(true);
		replydeslockbtn->Show(true);
		sendbtn->SetLabel(wxT("Reply"));
	}
	else if(dm_targ) {
		replydesc->SetLabel(wxT("Direct Message: @") + wxstrstd(dm_targ->GetUser().screen_name));
		replydesc->Show(true);
		replydesclosebtn->Show(true);
		replydeslockbtn->Show(true);
		sendbtn->SetLabel(wxT("Send DM"));
		ClearImageUploadFilenames(); // Images can't be uploaded with DMs at present
	}
	else {
		if(replydesc_locked) {
			replydesc_locked = false;
		}
		replydesc->Show(false);
		replydesclosebtn->Show(false);
		replydeslockbtn->Show(false);
		sendbtn->SetLabel(wxT("Send"));
	}
	replydeslockbtn->SetBitmapLabel(GetReplyDescLockBtnBitmap());
	OnTCChange();
	DoCheckFocusDisplay(true);
}

void tweetpostwin::AddImageUploadFilename(std::string filename) {
	image_upload_filenames.push_back(std::move(filename));
	imagestattxt->SetLabel(wxString::Format(wxT("Upload %zu images"), image_upload_filenames.size()));
	OnTCChange();
	DoCheckFocusDisplay();
}

void tweetpostwin::ClearImageUploadFilenames() {
	image_upload_filenames.clear();
	imagestattxt->SetLabel(wxT(""));
	OnTCChange();
	DoCheckFocusDisplay();
}

void tweetpostwin::ShowHideImageUploadBtns(bool alwayshide, bool enabled) {
	bool show_add;
	bool show_existing = !image_upload_filenames.empty();

	if(dm_targ || alwayshide) {
		show_add = false;
		show_existing = false;
	}
	else if(image_upload_filenames.size() < MAX_IMAGE_ATTACHMENTS_PER_TWEET) {
		show_add = true;
	}
	else {
		show_add = false;
	}

	addimagebtn->Show(show_add);
	delimagebtn->Show(show_existing);
	imagestattxt->Show(show_existing);
	addimagebtn->Enable(enabled);
	delimagebtn->Enable(enabled);
}

void tweetpostwin::OnAddImgBtn(wxCommandEvent &event) {
	wxString file = wxFileSelector(wxT("Upload image with tweet"), wxStandardPaths::Get().GetDocumentsDir(), wxT(""), wxT(""),
			wxT("Image Files (*.jpg *.jpeg *.png *.gif)|*.jpg;*.jpeg;*.png;*.gif"), wxFD_OPEN | wxFD_FILE_MUST_EXIST, this);
	if(!file.IsEmpty()) {
		AddImageUploadFilename(stdstrwx(file));
	}
}

void tweetpostwin::OnDelImgBtn(wxCommandEvent &event) {
	ClearImageUploadFilenames();
}

void tweetpostwin::OnCloseReplyDescBtn(wxCommandEvent &event) {
	tweet_reply_targ.reset();
	dm_targ.reset();
	UpdateReplyDesc();
}

void tweetpostwin::OnClearTextBtn(wxCommandEvent &event) {
	textctrl->Clear();
}

void tweetpostwin::OnToggleReplyDescLockBtn(wxCommandEvent &event) {
	replydesc_locked = !replydesc_locked;
	replydeslockbtn->SetBitmapLabel(GetReplyDescLockBtnBitmap());
}

wxBitmap &tweetpostwin::GetReplyDescLockBtnBitmap() {
	return replydesc_locked ? tpg->proticon : tpg->unlockicon;
}

void CheckUserMentioned(bool &changed, udc_ptr_p user, tweetposttextbox *textctrl, std::unique_ptr<is_user_mentioned_cache> *cache = 0) {
	if(user && !IsUserMentioned(stdstrwx(textctrl->GetValue()), user, cache)) {
		textctrl->WriteText(wxT("@") + wxstrstd(user->GetUser().screen_name) + wxT(" "));
		changed=true;
	}
}

template <typename F> void IterateUserNames(tweet_ptr_p targ, F func) {
	tweet_ptr checktweet = targ;
	func(targ->user);
	if(targ->rtsrc) {
		checktweet = targ->rtsrc;
		func(targ->rtsrc->user);
	}
	for(auto &it : checktweet->entlist) {
		if(it.type == ENT_MENTION) {
			if(! (it.user->udc_flags & UDC::THIS_IS_ACC_USER_HINT)) {
				func(it.user);
			}
		}
	}
}

void tweetpostwin::CheckAddNamesBtn() {
	bool missing_name = false;
	if(tweet_reply_targ) {
		std::string txt = stdstrwx(textctrl->GetValue());
		IterateUserNames(tweet_reply_targ, [&](udc_ptr u) {
			if(u && !IsUserMentioned(txt, u, &iumc)) {
				missing_name = true;
			}
		});
	}
	addnamesbtn->Show(missing_name);
}

void tweetpostwin::OnAddNamesBtn(wxCommandEvent &event) {
	if(tweet_reply_targ) {
		textctrl->SetInsertionPoint(0);
		bool changed = false;
		IterateUserNames(tweet_reply_targ, [&](udc_ptr u) {
			CheckUserMentioned(changed, u, textctrl, &iumc);
		});
		if(changed) OnTCChange();
		textctrl->SetCursorToEnd();
	}
}

void tweetpostwin::SetReplyTarget(tweet_ptr_p targ) {
	if(tweet_reply_targ != targ) replydesc_locked = false;
	textctrl->SetInsertionPoint(0);
	bool changed = false;
	if(targ) {
		IterateUserNames(targ, [&](udc_ptr u) {
			CheckUserMentioned(changed, u, textctrl, &iumc);
		});

		unsigned int best_score = 0;
		const taccount *best = 0;
		targ->IterateTP([&](const tweet_perspective &tp) {
			if(tp.IsArrivedHere()) {
				unsigned int score = 1;
				if(tp.acc->enabled) score += 2;
				if(tp.acc.get() == accc->curacc.get()) score += 1;
				if(score > best_score) {
					best = tp.acc.get();
					best_score = score;
				}
			}
		});
		if(best && accc->curacc.get() != best) {
			accc->TrySetSel(best);
		}
	}
	if(changed) OnTCChange();
	tweet_reply_targ = targ;
	dm_targ.reset();
	UpdateReplyDesc();
	textctrl->SetCursorToEnd();
}

void tweetpostwin::SetDMTarget(udc_ptr_p targ) {
	if(dm_targ != targ) replydesc_locked = false;
	tweet_reply_targ.reset();
	dm_targ = targ;
	UpdateReplyDesc();
	textctrl->SetCursorToEnd();
}

void tweetpostwin::AUIMNoLongerValid() {
	pauim = nullptr;
}
