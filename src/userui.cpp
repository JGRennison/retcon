//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
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
#include "userui.h"
#include "mainui.h"
#include "taccount.h"
#include "twitcurlext.h"
#include "twit.h"
#include "tpanel.h"
#include "userui.h"
#include "util.h"
#include "alldata.h"

std::unordered_map<uint64_t, user_window*> userwinmap;

BEGIN_EVENT_TABLE(notebook_event_prehandler, wxEvtHandler)
	EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, notebook_event_prehandler::OnPageChange)
END_EVENT_TABLE()

void notebook_event_prehandler::OnPageChange(wxNotebookEvent &event) {
	int i = event.GetSelection();
	if(i >= 0) {
		for(auto it = timeline_pane_list.begin(); it != timeline_pane_list.end(); ++it) {
			if(nb->GetPage(i) == (*it)) {
				if(!(*it)->havestarted) {
					(*it)->havestarted=true;
					(*it)->LoadMore(gc.maxtweetsdisplayinpanel);
				}
			}
		}
		for(auto it = userlist_pane_list.begin(); it != userlist_pane_list.end(); ++it) {
			if(nb->GetPage(i) == (*it)) {
				if(!(*it)->havestarted) {
					(*it)->havestarted=true;
					(*it)->Init();
				}
			}
		}
	}
	event.Skip();
}

BEGIN_EVENT_TABLE(user_window, wxDialog)
	EVT_CLOSE(user_window::OnClose)
	EVT_CHOICE(wxID_FILE1, user_window::OnSelChange)
	EVT_BUTTON(FOLLOWBTN_ID, user_window::OnFollowBtn)
	EVT_BUTTON(REFRESHBTN_ID, user_window::OnRefreshBtn)
	EVT_BUTTON(DMBTN_ID, user_window::OnDMBtn)
END_EVENT_TABLE()

static void insert_uw_row(wxWindow *parent, wxSizer *sz, const wxString &label, wxStaticText *&targ) {
	wxStaticText *name = new wxStaticText(parent, wxID_ANY, label);
	wxStaticText *data = new wxStaticText(parent, wxID_ANY, wxT(""));
	sz->Add(name, 0, wxALL, 2);
	sz->Add(data, 0, wxALL, 2);
	targ = data;
}

user_window::user_window(uint64_t userid_, const std::shared_ptr<taccount> &acc_hint_)
		: wxDialog(0, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxDIALOG_NO_PARENT), userid(userid_), acc_hint(acc_hint_) {
	userwinmap[userid_] = this;
	u = ad.GetUserContainerById(userid_);
	u->udc_flags |= UDC::WINDOWOPEN;
	u->ImgIsReady(UPDCF::DOWNLOADIMG);
	CheckAccHint();

	std::shared_ptr<taccount> acc = acc_hint.lock();
	if(acc && acc->enabled && u->NeedsUpdating(0) && !(u->udc_flags & UDC::LOOKUP_IN_PROGRESS)) {
		acc->pendingusers[userid_]=u;
		acc->StartRestQueryPendings();
	}

	Freeze();

	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer *headerbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(headerbox, 0, wxALL | wxEXPAND, 4);
	usericon = new wxStaticBitmap(this, wxID_ANY, u->cached_profile_img, wxPoint(-1000, -1000));
	headerbox->Add(usericon, 0, wxALL, 2);
	wxBoxSizer *infobox = new wxBoxSizer(wxVERTICAL);
	headerbox->Add(infobox, 0, wxALL, 2);
	name = new wxStaticText(this, wxID_ANY, wxT(""));
	screen_name = new wxStaticText(this, wxID_ANY, wxT(""));
	infobox->Add(name, 0, wxALL, 2);
	infobox->Add(screen_name, 0, wxALL, 2);

	wxStaticBoxSizer *sb = new wxStaticBoxSizer(wxHORIZONTAL, this, wxT("Account"));
	wxBoxSizer *sbvbox = new wxBoxSizer(wxVERTICAL);
	vbox->Add(sb, 0, wxALL | wxEXPAND, 2);
	sb->Add(sbvbox, 0, 0, 0);
	accchoice = new wxChoice(this, wxID_FILE1);
	sbvbox->Add(accchoice, 0, wxALL, 2);
	fill_accchoice();
	wxFlexGridSizer *follow_grid = new wxFlexGridSizer(0, 2, 2, 2);
	sbvbox->Add(follow_grid, 0, wxALL, 2);
	insert_uw_row(this, follow_grid, wxT("Following:"), ifollow);
	insert_uw_row(this, follow_grid, wxT("Followed By:"), followsme);
	sb->AddStretchSpacer();
	wxBoxSizer *accbuttonbox = new wxBoxSizer(wxVERTICAL);
	sb->Add(accbuttonbox, 0, wxALIGN_RIGHT | wxALIGN_TOP, 0);
	followbtn = new wxButton(this, FOLLOWBTN_ID, wxT(""));
	refreshbtn = new wxButton(this, REFRESHBTN_ID, wxT("Refresh"));
	dmbtn = new wxButton(this, DMBTN_ID, wxT("Send DM"));
	accbuttonbox->Add(followbtn, 0, wxEXPAND | wxALIGN_TOP, 0);
	accbuttonbox->Add(refreshbtn, 0, wxEXPAND | wxALIGN_TOP, 0);
	accbuttonbox->Add(dmbtn, 0, wxEXPAND | wxALIGN_TOP, 0);
	follow_btn_mode = FOLLOWBTNMODE::FBM_NONE;

	nb=new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN | wxNB_TOP | wxNB_NOPAGETHEME);

	wxPanel *infopanel=new wxPanel(nb, wxID_ANY);
	vbox->Add(nb, 0, wxALL | wxEXPAND, 4);
	if_grid = new wxFlexGridSizer(0, 2, 2, 2);
	infopanel->SetSizer(if_grid);
	insert_uw_row(infopanel, if_grid, wxT("Name:"), name2);
	insert_uw_row(infopanel, if_grid, wxT("Screen Name:"), screen_name2);
	insert_uw_row(infopanel, if_grid, wxT("Description:"), desc);
	insert_uw_row(infopanel, if_grid, wxT("Location:"), location);
	insert_uw_row(infopanel, if_grid, wxT("Protected:"), isprotected);
	insert_uw_row(infopanel, if_grid, wxT("Verified Account:"), isverified);
	insert_uw_row(infopanel, if_grid, wxT("Tweets:"), tweets);
	insert_uw_row(infopanel, if_grid, wxT("Followers:"), followers);
	insert_uw_row(infopanel, if_grid, wxT("Following:"), follows);
	insert_uw_row(infopanel, if_grid, wxT("Has Favourited:"), faved);

	url_label = new wxStaticText(infopanel, wxID_ANY, wxT("Web URL:"));
	if_grid->Add(url_label, 0, wxALL, 2);
	url = new wxHyperlinkCtrl(infopanel, wxID_ANY, wxT("url"), wxT("url"), wxDefaultPosition, wxDefaultSize, wxNO_BORDER|wxHL_CONTEXTMENU|wxHL_ALIGN_LEFT);
	if_grid->Add(url, 0, wxALL, 2);

	if_grid->Add(new wxStaticText(infopanel, wxID_ANY, wxT("Profile URL:")), 0, wxALL, 2);
	profileurl = new wxHyperlinkCtrl(infopanel, wxID_ANY, wxT("url"), wxT("url"), wxDefaultPosition, wxDefaultSize, wxNO_BORDER|wxHL_CONTEXTMENU|wxHL_ALIGN_LEFT);
	if_grid->Add(profileurl, 0, wxALL, 2);

	insert_uw_row(infopanel, if_grid, wxT("Creation Time:"), createtime);
	insert_uw_row(infopanel, if_grid, wxT("Last Updated:"), lastupdate);
	insert_uw_row(infopanel, if_grid, wxT("Account ID:"), id_str);

	nb->AddPage(infopanel, wxT("Info"), true);

	magic_ptr_ts<user_window> safe_win_ptr(this);
	std::function<std::shared_ptr<taccount>()> getacc = [safe_win_ptr]() -> std::shared_ptr<taccount> {
		std::shared_ptr<taccount> acc;
		user_window *uw = safe_win_ptr.get();
		if(uw) acc = uw->acc_hint.lock();
		return acc;
	};
	auto getacc_tw = [getacc](tpanelparentwin_usertweets &src) -> std::shared_ptr<taccount> { return getacc(); };
	auto getacc_prop = [getacc](tpanelparentwin_userproplisting &src) -> std::shared_ptr<taccount> { return getacc(); };

	timeline_pane = new tpanelparentwin_usertweets(u, nb, getacc_tw);
	nb->AddPage(timeline_pane, wxT("Timeline"), false);
	fav_timeline_pane = new tpanelparentwin_usertweets(u, nb, getacc_tw, RBFS_USER_FAVS);
	nb->AddPage(fav_timeline_pane, wxT("Favourites"), false);
	followers_pane = new tpanelparentwin_userproplisting(u, nb, getacc_prop, CS_USERFOLLOWERS);
	nb->AddPage(followers_pane, wxT("Followers"), false);
	friends_pane = new tpanelparentwin_userproplisting(u, nb, getacc_prop, CS_USERFOLLOWING);
	nb->AddPage(friends_pane, wxT("Following"), false);
	nb_prehndlr.timeline_pane_list.push_front(timeline_pane);
	nb_prehndlr.timeline_pane_list.push_front(fav_timeline_pane);
	nb_prehndlr.userlist_pane_list.push_front(followers_pane);
	nb_prehndlr.userlist_pane_list.push_front(friends_pane);
	nb_prehndlr.nb = nb;
	nb->PushEventHandler(&nb_prehndlr);

	SetSizer(vbox);

	Refresh(false);
	Thaw();

	if(uwt_common.expired()) {
		uwt = std::make_shared<user_window_timer>();
		uwt_common = uwt;
		uwt->Start(90000, false);
	}
	else {
		uwt = uwt_common.lock();
	}

	Show();
}

void user_window::OnSelChange(wxCommandEvent &event) {
	int selection = accchoice->GetSelection();
	acc_hint.reset();
	if(selection != wxNOT_FOUND) {
		taccount *acc = (taccount *) accchoice->GetClientData(selection);
		for(auto it = alist.begin(); it != alist.end(); it++) {
			if((*it).get() == acc) {
				acc_hint = *it;
				break;
			}
		}
		wxNotebookEvent evt(wxEVT_NULL, nb->GetId(), nb->GetSelection(), nb->GetSelection());
		nb_prehndlr.OnPageChange(evt);
	}
	RefreshFollow();
}

void user_window::fill_accchoice() {
	std::shared_ptr<taccount> acc = acc_hint.lock();
	for(auto it = alist.begin(); it != alist.end(); it++ ) {
		wxString accname = (*it)->dispname;
		if(!(*it)->enabled) accname += wxT(" [disabled]");
		accchoice->Append(accname, (*it).get());
		if((*it).get() == acc.get()) {
			accchoice->SetSelection(accchoice->GetCount() - 1);
		}
	}
}

static void set_uw_time_val(wxStaticText *st, const time_t &input) {
	if(input) {
		time_t updatetime;	//not used
		wxString val = wxString::Format(wxT("%s (%s)"), getreltimestr(input, updatetime).c_str(), rc_wx_strftime(gc.gcfg.datetimeformat.val, localtime(&input), input, true).c_str());
		st->SetLabel(val);
	}
	else st->SetLabel(wxT(""));
}

void user_window::RefreshFollow(bool forcerefresh) {
	using URF = user_relationship::URF;
	std::shared_ptr<taccount> acc = acc_hint.lock();
	bool needupdate = false;
	FOLLOWBTNMODE fbm = FOLLOWBTNMODE::FBM_NONE;

	auto fill_follow_field = [&](wxStaticText *st, bool ifollow) {
		bool known = false;
		wxString value;
		if(acc) {
			auto it = acc->user_relations.find(userid);
			if(it != acc->user_relations.end()) {
				user_relationship &ur = it->second;
				if(ur.ur_flags & (ifollow ? URF::IFOLLOW_KNOWN : URF::FOLLOWSME_KNOWN)) {
					known=true;
					if(ur.ur_flags&(ifollow ? URF::IFOLLOW_TRUE : URF::FOLLOWSME_TRUE)) {
						if(ifollow) fbm = FOLLOWBTNMODE::FBM_UNFOLLOW;
						value = wxT("Yes");
					}
					else if(ur.ur_flags&(ifollow ? URF::IFOLLOW_PENDING : URF::FOLLOWSME_PENDING)) {
						if(ifollow) fbm = FOLLOWBTNMODE::FBM_REMOVE_PENDING;
						value = wxT("Pending");
					}
					else {
						if(ifollow) fbm = FOLLOWBTNMODE::FBM_FOLLOW;
						value = wxT("No");
					}

					time_t updtime = ifollow ? ur.ifollow_updtime : ur.followsme_updtime;
					if(updtime && (time(0) - updtime) > 180) {
						time_t updatetime;	//not used
						value = wxString::Format(wxT("%s as of %s (%s)"), value.c_str(), getreltimestr(updtime, updatetime).c_str(), rc_wx_strftime(gc.gcfg.datetimeformat.val, localtime(&updtime), updtime, true).c_str());
					}
					if(updtime && forcerefresh) needupdate=true;
					st->SetLabel(value);
				}
			}
			if(!known) {
				if(acc->ta_flags & taccount::TAF::STREAM_UP && ifollow) st->SetLabel(wxT("No or Pending"));
				else st->SetLabel(wxT("Unknown"));
				fbm = FOLLOWBTNMODE::FBM_NONE;
				needupdate = true;
			}
		}
		else {
			st->SetLabel(wxT("No Account"));
			needupdate = true;
		}

	};

	fill_follow_field(ifollow, true);
	fill_follow_field(followsme, false);

	switch(fbm) {
		case FOLLOWBTNMODE::FBM_UNFOLLOW:
			followbtn->SetLabel(wxT("Unfollow"));
			break;
		case FOLLOWBTNMODE::FBM_REMOVE_PENDING:
			//followbtn->SetLabel(wxT("Cancel Follow Request"));
			//break;	//not implemented in twitter API
		case FOLLOWBTNMODE::FBM_FOLLOW:
		case FOLLOWBTNMODE::FBM_NONE:
			followbtn->SetLabel(wxT("Follow"));
			break;
	}

	followbtn->Enable(acc && acc->enabled && fbm != FOLLOWBTNMODE::FBM_NONE && fbm != FOLLOWBTNMODE::FBM_REMOVE_PENDING && !(u->udc_flags & UDC::FRIENDACT_IN_PROGRESS));
	refreshbtn->Enable(acc && acc->enabled);
	dmbtn->Enable(acc && acc->enabled);
	follow_btn_mode = fbm;

	if(needupdate && acc && acc->enabled) {
		acc->LookupFriendships(userid);
	}

	Fit();
}

void user_window::Refresh(bool refreshimg) {
	SetTitle(wxT("@") + wxstrstd(u->GetUser().screen_name) + wxT(" (") + wxstrstd(u->GetUser().name) + wxT(")"));
	name->SetLabel(wxstrstd(u->GetUser().name));
	screen_name->SetLabel(wxT("@") + wxstrstd(u->GetUser().screen_name));
	name2->SetLabel(wxstrstd(u->GetUser().name));
	screen_name2->SetLabel(wxT("@") + wxstrstd(u->GetUser().screen_name));
	desc->SetLabel(wxstrstd(u->GetUser().description).Trim());
	desc->Wrap(150);
	location->SetLabel(wxstrstd(u->GetUser().location).Trim());
	isprotected->SetLabel((u->GetUser().u_flags & userdata::userdata::UF::ISPROTECTED)?wxT("Yes"):wxT("No"));
	isverified->SetLabel((u->GetUser().u_flags & userdata::userdata::UF::ISVERIFIED)?wxT("Yes"):wxT("No"));
	tweets->SetLabel(wxString::Format(wxT("%d"), u->GetUser().statuses_count));
	followers->SetLabel(wxString::Format(wxT("%d"), u->GetUser().followers_count));
	follows->SetLabel(wxString::Format(wxT("%d"), u->GetUser().friends_count));
	faved->SetLabel(wxString::Format(wxT("%d"), u->GetUser().favourites_count));

	bool showurl = !u->GetUser().userurl.empty();
	if(showurl) {
		wxString wurl = wxstrstd(u->GetUser().userurl);
		url->SetLabel(wurl);
		url->SetURL(wurl);

	}
	else {
		url->SetLabel(wxT("<empty>"));
		url->SetURL(wxT("<empty>"));
	}
	if_grid->Show(url, showurl);
	if_grid->Show(url_label, showurl);

	wxString profurl = wxstrstd(u->GetPermalink(true));
	profileurl->SetLabel(profurl);
	profileurl->SetURL(profurl);
	set_uw_time_val(createtime, u->GetUser().createtime);
	set_uw_time_val(lastupdate, (time_t) u->lastupdate);
	id_str->SetLabel(wxString::Format(wxT("%" wxLongLongFmtSpec "d"), u->id));
	if(refreshimg) usericon->SetBitmap(u->cached_profile_img);

	RefreshFollow();
}


user_window::~user_window() {
	nb->PopEventHandler();
	userwinmap.erase(userid);
	u->udc_flags &= ~UDC::WINDOWOPEN;
}

void user_window::CheckAccHint() {
	std::shared_ptr<taccount> acc_sp = acc_hint.lock();
	u->GetUsableAccount(acc_sp);
	acc_hint = acc_sp;
}

void user_window::OnClose(wxCloseEvent &event) {
	Destroy();
}

void user_window::OnRefreshBtn(wxCommandEvent &event) {
	std::shared_ptr<taccount> acc = acc_hint.lock();
	if(acc && acc->enabled && !(u->udc_flags & UDC::LOOKUP_IN_PROGRESS)) {
		acc->pendingusers[userid] = u;
		u->udc_flags |= UDC::FORCE_REFRESH;
		acc->StartRestQueryPendings();
		RefreshFollow(true);
	}
}

void user_window::OnFollowBtn(wxCommandEvent &event) {
	std::shared_ptr<taccount> acc=acc_hint.lock();
	if(follow_btn_mode != FOLLOWBTNMODE::FBM_NONE && acc && acc->enabled && !(u->udc_flags & UDC::FRIENDACT_IN_PROGRESS)) {
		u->udc_flags |= UDC::FRIENDACT_IN_PROGRESS;
		followbtn->Enable(false);
		twitcurlext *twit = acc->GetTwitCurlExt();
		if(follow_btn_mode == FOLLOWBTNMODE::FBM_FOLLOW) twit->connmode = CS_FRIENDACTION_FOLLOW;
		else twit->connmode = CS_FRIENDACTION_UNFOLLOW;
		twit->extra_id = userid;
		twit->QueueAsyncExec();
	}
}

void user_window::OnDMBtn(wxCommandEvent &event) {
	mainframe *win = GetMainframeAncestor(this);
	if(!win) win = mainframelist.front();
	if(win) {
		win->tpw->SetDMTarget(u);
	}
}

user_window *user_window::GetWin(uint64_t userid_) {
	auto it = userwinmap.find(userid_);
	if(it != userwinmap.end()) return it->second;
	else return 0;
}

user_window *user_window::MkWin(uint64_t userid_, const std::shared_ptr<taccount> &acc_hint_) {
	user_window *cur = GetWin(userid_);
	if(cur) {
		cur->Show();
		cur->Raise();
		return cur;
	}
	else return new user_window(userid_, acc_hint_);
}

void user_window::RefreshAllAcc() {
	for(auto it = userwinmap.begin(); it != userwinmap.end(); ++it) {
		it->second->CheckAccHint();
		it->second->fill_accchoice();
		it->second->RefreshFollow();
	}
}

void user_window::RefreshAllFollow() {
	for(auto it = userwinmap.begin(); it != userwinmap.end(); ++it) {
		it->second->Refresh();
	}
}

void user_window::RefreshAll() {
	for(auto it = userwinmap.begin(); it != userwinmap.end(); ++it) {
		it->second->Refresh();
	}
}

void user_window::CloseAll() {
	for(auto it = userwinmap.begin(); it != userwinmap.end(); ++it) {
		it->second->Destroy();
	}
}

void user_window::CheckRefresh(uint64_t userid_, bool refreshimg) {
	user_window *cur = GetWin(userid_);
	if(cur) {
		cur->Refresh(refreshimg);
	}
}

void user_window_timer::Notify() {
	user_window::RefreshAll();
}

std::weak_ptr<user_window_timer> user_window::uwt_common;

BEGIN_EVENT_TABLE(user_lookup_dlg, wxDialog)
EVT_TEXT_ENTER(wxID_FILE2, user_lookup_dlg::OnTCEnter)
END_EVENT_TABLE()

user_lookup_dlg::user_lookup_dlg(wxWindow *parent, int *type, wxString *value, std::shared_ptr<taccount> &acc)
	: wxDialog(parent, wxID_ANY, wxT("Enter user name or ID to look up"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE), curacc(acc) {

	const wxString opts[2] = {wxT("User screen name"), wxT("User numeric identifier")};
	*type = 0;
	wxRadioBox *rb = new wxRadioBox(this, wxID_FILE1, wxT("Type"), wxDefaultPosition, wxDefaultSize, 2, opts, 0, wxRA_SPECIFY_COLS, wxGenericValidator(type));
	*value = wxT("");
	wxTextCtrl *tc = new wxTextCtrl(this, wxID_FILE2, wxT(""), wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER, wxGenericValidator(value));
	wxButton *okbtn = new wxButton(this, wxID_OK, wxT("OK"));
	wxButton *cancelbtn = new wxButton(this, wxID_CANCEL, wxT("Cancel"));
	acc_choice *acd = new acc_choice(this, curacc, acc_choice::ACCCF::OKBTNCTRL | acc_choice::ACCCF::NOACCITEM);

	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);
	vbox->Add(acd, 0, wxALL, 2);
	vbox->Add(rb, 0, wxALL, 2);
	vbox->Add(tc, 0, wxALL | wxEXPAND, 2);
	wxBoxSizer *hboxfooter = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(hboxfooter, 0, wxALL | wxEXPAND, 2);
	hboxfooter->AddStretchSpacer();
	hboxfooter->Add(okbtn, 0, wxALL | wxALIGN_BOTTOM | wxALIGN_RIGHT, 2);
	hboxfooter->Add(cancelbtn, 0, wxALL | wxALIGN_BOTTOM | wxALIGN_RIGHT, 2);

	tc->SetFocus();

	SetSizer(vbox);
	Fit();
}

void user_lookup_dlg::OnTCEnter(wxCommandEvent &event) {
	wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED, wxID_OK);
	ProcessEvent(evt);
}

BEGIN_EVENT_TABLE(acc_choice, wxChoice)
	EVT_CHOICE(wxID_ANY, acc_choice::OnSelChange)
END_EVENT_TABLE()

acc_choice::acc_choice(wxWindow *parent, std::shared_ptr<taccount> &acc, flagwrapper<ACCCF> flags_, int winid, acc_choice_callback callbck, void *extra)
	: wxChoice(parent, winid, wxDefaultPosition, wxDefaultSize, 0, 0), curacc(acc), flags(flags_), fnptr(callbck), fnextra(extra) {
	if(!acc.get()) {
		for(auto it = alist.begin(); it != alist.end(); ++it) {
			acc = (*it);
			if((*it)->enabled) break;
		}
	}
	fill_acc();
}

void acc_choice::fill_acc() {
	Clear();
	for(auto it = alist.begin(); it != alist.end(); ++it) {
		wxString accname = (*it)->dispname;
		wxString status = (*it)->GetStatusString(true);
		if(status.size()) accname += wxT(" [") + status + wxT("]");
		int index = Append(accname, (*it).get());
		if((*it).get() == curacc.get()) SetSelection(index);
	}
	if((GetCount() == 0) && (flags & ACCCF::NOACCITEM)) {
		int index = Append(wxT("[No Accounts]"), (void *) 0);
		SetSelection(index);
	}
	UpdateSel();
}

void acc_choice::OnSelChange(wxCommandEvent &event) {
	UpdateSel();
}

void acc_choice::UpdateSel() {
	bool havegoodacc = false;
	bool haveanyacc = false;
	int selection = GetSelection();
	if(selection != wxNOT_FOUND) {
		taccount *accptr = (taccount *) GetClientData(selection);
		for(auto it = alist.begin(); it != alist.end(); ++it) {
			if((*it).get() == accptr) {
				haveanyacc = true;
				curacc = (*it);
				if((*it)->enabled) havegoodacc = true;
				break;
			}
		}
	}
	if(!haveanyacc) {
		curacc.reset();
	}
	if(flags & ACCCF::OKBTNCTRL) {
		wxWindow *topparent = this;
		while(topparent) {
			if(topparent->IsTopLevel()) break;
			else topparent = topparent->GetParent();
		}
		wxWindow *okbtn = wxWindow::FindWindowById(wxID_OK, topparent);
		if(okbtn) {
			okbtn->Enable(havegoodacc);
		}
	}
	if(fnptr) (*fnptr)(fnextra, this, havegoodacc);
}

void acc_choice::TrySetSel(const taccount *tac) {
	for(unsigned int i = 0; i < GetCount(); i++) {
		if(GetClientData(i) == tac) {
			if((int) i != GetSelection()) {
				SetSelection(i);
				UpdateSel();
			}
		}
	}
}
