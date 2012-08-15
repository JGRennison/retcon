#include "retcon.h"

std::unordered_map<uint64_t, user_window*> userwinmap;

BEGIN_EVENT_TABLE(user_window, wxDialog)
	EVT_CLOSE(user_window::OnClose)
	EVT_CHOICE(wxID_FILE1, user_window::OnSelChange)
END_EVENT_TABLE()

static void insert_uw_row(wxWindow *parent, wxSizer *sz, const wxString &label, wxStaticText *&targ) {
	wxStaticText *name=new wxStaticText(parent, wxID_ANY, label);
	wxStaticText *data=new wxStaticText(parent, wxID_ANY, wxT(""));
	sz->Add(name, 0, wxALL, 2);
	sz->Add(data, 0, wxALL, 2);
	targ=data;
}

user_window::user_window(uint64_t userid_, const std::shared_ptr<taccount> &acc_hint_)
		: wxDialog(0, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxDIALOG_NO_PARENT), userid(userid_), acc_hint(acc_hint_) {
	userwinmap[userid_]=this;
	u=ad.GetUserContainerById(userid_);
	u->udc_flags|=UDC_WINDOWOPEN;
	u->ImgIsReady(UPDCF_DOWNLOADIMG);
	CheckAccHint();

	std::shared_ptr<taccount> acc=acc_hint.lock();
	if(acc && acc->enabled && u->NeedsUpdating(0)) {
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
	name=new wxStaticText(this, wxID_ANY, wxT(""));
	screen_name=new wxStaticText(this, wxID_ANY, wxT(""));
	infobox->Add(name, 0, wxALL, 2);
	infobox->Add(screen_name, 0, wxALL, 2);

	wxStaticBoxSizer *sb=new wxStaticBoxSizer(wxVERTICAL, this, wxT("Account"));
	vbox->Add(sb, 0, wxALL, 2);
	accchoice=new wxChoice(this, wxID_FILE1);
	sb->Add(accchoice, 0, wxALL, 2);
	fill_accchoice();
	wxFlexGridSizer *follow_grid=new wxFlexGridSizer(0, 2, 2, 2);
	sb->Add(follow_grid, 0, wxALL, 2);
	insert_uw_row(this, follow_grid, wxT("Following:"), ifollow);
	insert_uw_row(this, follow_grid, wxT("Followed By:"), followsme);

	wxNotebook *nb=new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCLIP_CHILDREN | wxNB_TOP | wxNB_NOPAGETHEME);

	wxPanel *infopanel=new wxPanel(nb, wxID_ANY);
	vbox->Add(nb, 0, wxALL | wxEXPAND, 4);
	wxFlexGridSizer *if_grid=new wxFlexGridSizer(0, 2, 2, 2);
	infopanel->SetSizer(if_grid);
	insert_uw_row(infopanel, if_grid, wxT("Name:"), name2);
	insert_uw_row(infopanel, if_grid, wxT("Screen Name:"), screen_name2);
	insert_uw_row(infopanel, if_grid, wxT("Description:"), desc);
	insert_uw_row(infopanel, if_grid, wxT("Protected:"), isprotected);
	insert_uw_row(infopanel, if_grid, wxT("Verified Account:"), isverified);
	insert_uw_row(infopanel, if_grid, wxT("Tweets:"), tweets);
	insert_uw_row(infopanel, if_grid, wxT("Followers:"), followers);
	insert_uw_row(infopanel, if_grid, wxT("Following:"), follows);

	if_grid->Add(new wxStaticText(infopanel, wxID_ANY, wxT("Web URL:")), 0, wxALL, 2);
	url=new wxHyperlinkCtrl(infopanel, wxID_ANY, wxT(""), wxT(""), wxDefaultPosition, wxDefaultSize, wxNO_BORDER|wxHL_CONTEXTMENU|wxHL_ALIGN_LEFT);
	if_grid->Add(url, 0, wxALL, 2);

	insert_uw_row(infopanel, if_grid, wxT("Creation Time:"), createtime);
	insert_uw_row(infopanel, if_grid, wxT("Last Updated:"), lastupdate);
	insert_uw_row(infopanel, if_grid, wxT("Account ID:"), id_str);

	nb->AddPage(infopanel, wxT("Info"), true);

	SetSizer(vbox);

	Refresh(false);
	Thaw();

	if(!uwt.IsRunning()) uwt.Start(90000, false);

	Show();
}

void user_window::OnSelChange(wxCommandEvent &event) {
	int selection=accchoice->GetSelection();
	acc_hint.reset();
	if(selection!=wxNOT_FOUND) {
		taccount *acc=(taccount *) accchoice->GetClientData(selection);
		for(auto it=alist.begin(); it!=alist.end(); it++ ) {
			if((*it).get()==acc) {
				acc_hint=*it;
				break;
			}
		}
	}
	RefreshFollow();
}

void user_window::fill_accchoice() {
	std::shared_ptr<taccount> acc=acc_hint.lock();
	for(auto it=alist.begin(); it != alist.end(); it++ ) {
		wxString accname=(*it)->dispname;
		if(!(*it)->enabled) accname+=wxT(" [disabled]");
		accchoice->Append(accname, (*it).get());
		if((*it).get()==acc.get()) {
			accchoice->SetSelection(accchoice->GetCount()-1);
		}
	}
}

static void set_uw_time_val(wxStaticText *st, const time_t &input) {
	if(input) {
		time_t updatetime;	//not used
		wxString val=wxString::Format(wxT("%s (%s)"), getreltimestr(input, updatetime).c_str(), rc_wx_strftime(gc.gcfg.datetimeformat.val, localtime(&input), input, true).c_str());
		st->SetLabel(val);
	}
	else st->SetLabel(wxT(""));
}

void user_window::RefreshFollow() {
	std::shared_ptr<taccount> acc=acc_hint.lock();
	bool needupdate=false;

	auto fill_follow_field=[&](wxStaticText *st, bool ifollow) {
		bool known=false;
		wxString value;
		if(acc) {
			auto it=acc->user_relations.find(userid);
			if(it!=acc->user_relations.end()) {
				user_relationship &ur=it->second;
				if(ur.ur_flags&(ifollow?URF_IFOLLOW_KNOWN:URF_FOLLOWSME_KNOWN)) {
					known=true;
					if(ur.ur_flags&(ifollow?URF_IFOLLOW_TRUE:URF_FOLLOWSME_TRUE)) value=wxT("Yes");
					else if(ur.ur_flags&(ifollow?URF_IFOLLOW_PENDING:URF_FOLLOWSME_PENDING)) value=wxT("Pending");
					else value=wxT("No");

					time_t updtime=ifollow?ur.ifollow_updtime:ur.followsme_updtime;
					if(updtime && (time(0)-updtime)>180) {
						time_t updatetime;	//not used
						value=wxString::Format(wxT("%s as of %s (%s)"), value.c_str(), getreltimestr(updtime, updatetime).c_str(), rc_wx_strftime(gc.gcfg.datetimeformat.val, localtime(&updtime), updtime, true).c_str());
					}
					st->SetLabel(value);
				}
			}
			if(!known) {
				if(acc->ta_flags&TAF_STREAM_UP && ifollow) st->SetLabel(wxT("No or Pending"));
				else st->SetLabel(wxT("Unknown"));
				needupdate=true;
			}
		}
		else {
			st->SetLabel(wxT("No Account"));
			needupdate=true;
		}

	};

	fill_follow_field(ifollow, true);
	fill_follow_field(followsme, false);

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
	desc->SetLabel(wxstrstd(u->GetUser().description));
	desc->Wrap(150);
	isprotected->SetLabel((u->GetUser().u_flags&UF_ISPROTECTED)?wxT("Yes"):wxT("No"));
	isverified->SetLabel((u->GetUser().u_flags&UF_ISVERIFIED)?wxT("Yes"):wxT("No"));
	tweets->SetLabel(wxString::Format(wxT("%d"), u->GetUser().statuses_count));
	followers->SetLabel(wxString::Format(wxT("%d"), u->GetUser().followers_count));
	follows->SetLabel(wxString::Format(wxT("%d"), u->GetUser().friends_count));
	url->SetLabel(wxstrstd(u->GetUser().userurl));
	url->SetURL(wxstrstd(u->GetUser().userurl));
	set_uw_time_val(createtime, u->GetUser().createtime);
	set_uw_time_val(lastupdate, (time_t) u->lastupdate);
	id_str->SetLabel(wxString::Format(wxT("%" wxLongLongFmtSpec "d"), u->id));
	if(refreshimg) usericon->SetBitmap(u->cached_profile_img);

	RefreshFollow();
}


user_window::~user_window() {
	userwinmap.erase(userid);
	u->udc_flags&=~UDC_WINDOWOPEN;
	if(userwinmap.empty()) uwt.Stop();
}

void user_window::CheckAccHint() {
	// if(auto tac=acc_hint.lock()) {
		// if(!tac->enabled) acc_hint.reset();
	// }

	if(acc_hint.expired()) {
		for(auto it=alist.begin(); it!=alist.end(); ++it) {	//look for users who we follow, or who follow us
			taccount &acc=**it;
			//if(acc.enabled) {
				auto rel=acc.user_relations.find(userid);
				if(rel!=acc.user_relations.end()) {
					if(rel->second.ur_flags&(URF_FOLLOWSME_TRUE|URF_IFOLLOW_TRUE)) {
						acc_hint=*it;
						break;
					}
				}
			//}
		}
	}
	if(acc_hint.expired()) {					//otherwise find the first enabled account
		for(auto it=alist.begin(); it!=alist.end(); ++it) {
			if((*it)->enabled) {
				acc_hint=*it;
				break;
			}
		}
	}
	if(acc_hint.expired()) {					//otherwise find the first account
		if(!alist.empty()) acc_hint=alist.front();
	}
}

void user_window::OnClose(wxCloseEvent &event) {
	Destroy();
}

user_window *user_window::GetWin(uint64_t userid_) {
	auto it=userwinmap.find(userid_);
	if(it!=userwinmap.end()) return it->second;
	else return 0;
}

user_window *user_window::MkWin(uint64_t userid_, const std::shared_ptr<taccount> &acc_hint_) {
	user_window *cur=GetWin(userid_);
	if(cur) {
		cur->Show();
		cur->Raise();
		return cur;
	}
	else return new user_window(userid_, acc_hint_);
}

void user_window::RefreshAllAcc() {
	for(auto it=userwinmap.begin(); it!=userwinmap.end(); ++it) {
		it->second->CheckAccHint();
		it->second->fill_accchoice();
		it->second->RefreshFollow();
	}
}

void user_window::RefreshAllFollow() {
	for(auto it=userwinmap.begin(); it!=userwinmap.end(); ++it) {
		it->second->Refresh();
	}
}

void user_window::RefreshAll() {
	for(auto it=userwinmap.begin(); it!=userwinmap.end(); ++it) {
		it->second->Refresh();
	}
}

void user_window::CloseAll() {
	for(auto it=userwinmap.begin(); it!=userwinmap.end(); ++it) {
		it->second->Destroy();
	}
}

void user_window::CheckRefresh(uint64_t userid_, bool refreshimg) {
	user_window *cur=GetWin(userid_);
	if(cur) {
		cur->Refresh(refreshimg);
	}
}

void user_window_timer::Notify() {
	user_window::RefreshAll();
}

user_window_timer user_window::uwt;
