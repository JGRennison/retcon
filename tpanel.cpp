#include "retcon.h"
#include "utf8.h"
#include "res.h"
#include <wx/filename.h>
#include <wx/filedlg.h>

std::unordered_multimap<uint64_t, tpaneldbloadmap_data> tpaneldbloadmap;
tpanelglobal *tpg;

static void PerAccTPanelMenu(wxMenu *menu, tpanelmenudata &map, int &nextid, unsigned int flagbase, unsigned int dbindex) {
	map[nextid]={dbindex, flagbase|TPF_AUTO_TW};
	menu->Append(nextid++, wxT("&Tweets"));
	map[nextid]={dbindex, flagbase|TPF_AUTO_MN};
	menu->Append(nextid++, wxT("&Mentions"));
	map[nextid]={dbindex, flagbase|TPF_AUTO_DM};
	menu->Append(nextid++, wxT("&DMs"));
	map[nextid]={dbindex, flagbase|TPF_AUTO_TW|TPF_AUTO_DM};
	menu->Append(nextid++, wxT("Tweets &and DMs"));
}

void MakeTPanelMenu(wxMenu *menuP, tpanelmenudata &map) {
	wxMenuItemList& items=menuP->GetMenuItems();
	for(auto it=items.begin(); it!=items.end(); ++it) {
		menuP->Destroy(*it);
	}
	map.clear();

	int nextid=tpanelmenustartid;
	PerAccTPanelMenu(menuP, map, nextid, TPF_ISAUTO | TPF_AUTO_ALLACCS | TPF_DELETEONWINCLOSE, 0);
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		wxMenu *submenu = new wxMenu;
		menuP->AppendSubMenu(submenu, (*it)->dispname);
		PerAccTPanelMenu(submenu, map, nextid, TPF_ISAUTO | TPF_AUTO_ACC | TPF_DELETEONWINCLOSE, (*it)->dbindex);
	}
}

void TPanelMenuAction(tpanelmenudata &map, int curid, mainframe *parent) {
	unsigned int dbindex=map[curid].dbindex;
	unsigned int flags=map[curid].flags;
	std::shared_ptr<taccount> *acc=0;
	wxString name;
	wxString accname;
	wxString type;
	if(dbindex) {
		for(auto it=alist.begin(); it!=alist.end(); ++it) {
			if((*it)->dbindex==dbindex) {
				acc=&(*it);
				name=(*it)->dispname;
				accname=(*it)->name;
				break;
			}
		}
		if(!acc) return;
	}
	else {
		name=wxT("All Accounts");
		accname=wxT("*");
	}
	if(flags&TPF_AUTO_TW && flags&TPF_AUTO_DM) type=wxT("Tweets & DMs");
	else if(flags&TPF_AUTO_TW) type=wxT("Tweets");
	else if(flags&TPF_AUTO_DM) type=wxT("DMs");
	else if(flags&TPF_AUTO_MN) type=wxT("Mentions");

	std::string paneldispname=std::string(wxString::Format(wxT("[%s - %s]"), name.c_str(), type.c_str()).ToUTF8());
	std::string panelname=std::string(wxString::Format(wxT("___%s - %s"), accname.c_str(), type.c_str()).ToUTF8());

	auto tp=tpanel::MkTPanel(panelname, paneldispname, flags, acc);
	tp->MkTPanelWin(parent, true);
}

void tpanel::PushTweet(const std::shared_ptr<tweet> &t) {
	LogMsgFormat(LFT_TPANEL, wxT("Pushing tweet id %" wxLongLongFmtSpec "d to panel %s"), t->id, wxstrstd(name).c_str());
	if(tweetlist.count(t->id)) {
		//already have this tweet
		return;
	}
	else {
		tweetlist.insert(t->id);
		for(auto i=twin.begin(); i!=twin.end(); i++) {
			(*i)->PushTweet(t);
		}
	}
}

tpanel::tpanel(const std::string &name_, const std::string &dispname_, unsigned int flags_, std::shared_ptr<taccount> *acc) : name(name_), dispname(dispname_), flags(flags_) {
	twin.clear();
	if(acc) assoc_acc=*acc;

	std::forward_list<taccount *> accs;

	if(flags&TPF_ISAUTO) {
		if(flags&TPF_AUTO_ACC) accs.push_front(assoc_acc.get());
		else if(flags&TPF_AUTO_ALLACCS) {
			for(auto it=alist.begin(); it!=alist.end(); ++it) accs.push_front((*it).get());
		}
		for(auto it=accs.begin(); it!=accs.end(); ++it) {
			if(flags&TPF_AUTO_DM) tweetlist.insert((*it)->dm_ids.begin(), (*it)->dm_ids.end());
			if(flags&TPF_AUTO_TW) tweetlist.insert((*it)->tweet_ids.begin(), (*it)->tweet_ids.end());
			if(flags&TPF_AUTO_MN) tweetlist.insert((*it)->usercont->mention_index.begin(), (*it)->usercont->mention_index.end());
		}
	}
	else return;
}

std::shared_ptr<tpanel> tpanel::MkTPanel(const std::string &name_, const std::string &dispname_, unsigned int flags_, std::shared_ptr<taccount> *acc) {
	std::shared_ptr<tpanel> &ref=ad.tpanels[name_];
	if(!ref) {
		ref=std::make_shared<tpanel>(name_, dispname_, flags_, acc);
	}
	return ref;
}

tpanel::~tpanel() {

}

void tpanel::OnTPanelWinClose(tpanelparentwin *tppw) {
	twin.remove(tppw);
	if(twin.empty() && flags&TPF_DELETEONWINCLOSE) {
		ad.tpanels.erase(name);
	}
}

BEGIN_EVENT_TABLE(tpanelnotebook, wxAuiNotebook)
	EVT_AUINOTEBOOK_ALLOW_DND(wxID_ANY, tpanelnotebook::dragdrophandler)
	EVT_AUINOTEBOOK_DRAG_DONE(wxID_ANY, tpanelnotebook::dragdonehandler)
	EVT_AUINOTEBOOK_TAB_RIGHT_DOWN(wxID_ANY, tpanelnotebook::tabrightclickhandler)
	EVT_AUINOTEBOOK_PAGE_CLOSED(wxID_ANY, tpanelnotebook::tabclosedhandler)
END_EVENT_TABLE()

tpanelnotebook::tpanelnotebook(mainframe *owner_, wxWindow *parent) :
wxAuiNotebook(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_TAB_EXTERNAL_MOVE | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_WINDOWLIST_BUTTON),
owner(owner_)
{

}

void tpanelnotebook::dragdrophandler(wxAuiNotebookEvent& event) {
	wxAuiNotebook* note= (wxAuiNotebook *) event.GetEventObject();
	if(note) {
		tpanelparentwin *tppw = (tpanelparentwin *) note->GetPage(event.GetSelection());
		if(tppw) tppw->owner=owner;
	}
	event.Allow();
}
void tpanelnotebook::dragdonehandler(wxAuiNotebookEvent& event) {
	tabnumcheck();
}
void tpanelnotebook::tabclosedhandler(wxAuiNotebookEvent& event) {
	tabnumcheck();
}
void tpanelnotebook::tabnumcheck() {
	if(GetPageCount()==0 && !(mainframelist.empty() || (++mainframelist.begin())==mainframelist.end())) {
		owner->Close();
	}
}

void tpanelnotebook::tabrightclickhandler(wxAuiNotebookEvent& event) {
	tpanelparentwin *tppw = (tpanelparentwin *) GetPage(event.GetSelection());
	if(tppw) {
		wxMenu menu;
		menu.Append(TPPWID_DETACH, wxT("Detach"));
		menu.Append(TPPWID_DUP, wxT("Duplicate"));
		menu.Append(TPPWID_DETACHDUP, wxT("Detached Duplicate"));
		menu.Append(TPPWID_CLOSE, wxT("Close"));
		tppw->PopupMenu(&menu);
	}
}

DECLARE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEUP_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT, -1)

DEFINE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT)
DEFINE_EVENT_TYPE(wxextTP_PAGEUP_EVENT)
DEFINE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT)

BEGIN_EVENT_TABLE(tpanelparentwin, wxPanel)
	EVT_MENU(TPPWID_DETACH, tpanelparentwin::tabdetachhandler)
	EVT_MENU(TPPWID_DUP, tpanelparentwin::tabduphandler)
	EVT_MENU(TPPWID_DETACHDUP, tpanelparentwin::tabdetachedduphandler)
	EVT_MENU(TPPWID_CLOSE, tpanelparentwin::tabclosehandler)
	EVT_COMMAND(wxID_ANY, wxextTP_PAGEUP_EVENT, tpanelparentwin::pageupevthandler)
	EVT_COMMAND(wxID_ANY, wxextTP_PAGEDOWN_EVENT, tpanelparentwin::pagedownevthandler)
END_EVENT_TABLE()

tpanelparentwin *tpanel::MkTPanelWin(mainframe *parent, bool select) {
	return new tpanelparentwin(shared_from_this(), parent, select);
}

tpanelparentwin::tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select)
: wxPanel(parent), tp(tp_), displayoffset(0), owner(parent) {
	LogMsgFormat(LFT_TPANEL, wxT("Creating tweet panel window %s"), wxstrstd(tp->name).c_str());

	tp->twin.push_front(this);
	tpanelparentwinlist.push_front(this);

	//tpw = new tpanelwin(this);
	//wxBoxSizer *vbox = new wxBoxSizer(wxHORIZONTAL);
	//vbox->Add(tpw, 1, wxALIGN_TOP | wxEXPAND, 0);
	//SetSizer(vbox);

	wxBoxSizer* outersizer = new wxBoxSizer(wxVERTICAL);
	scrollwin = new tpanelscrollwin(this);
	clabel=new wxStaticText(this, wxID_ANY, wxT("No Tweets"));
	outersizer->Add(clabel, 0, wxALL, 2);
	outersizer->Add(scrollwin, 1, wxALL | wxEXPAND, 2);
	outersizer->Add(new wxStaticText(this, wxID_ANY, wxT("Bar")), 0, wxALL, 2);

	sizer = new wxBoxSizer(wxVERTICAL);
	scrollwin->SetSizer(sizer);
	SetSizer(outersizer);
	scrollwin->SetScrollRate(1, 1);
        scrollwin->FitInside();
	FitInside();

	LoadMore(gc.maxtweetsdisplayinpanel);
	parent->auib->AddPage(this, wxstrstd(tp->dispname), select);
}

tpanelparentwin::~tpanelparentwin() {
	for(auto it=tpaneldbloadmap.begin(); it!=tpaneldbloadmap.end(); ) {
		if((*it).second.win==this) {
			auto todel=it;
			it++;
			tpaneldbloadmap.erase(todel);
		}
		else it++;
	}
	tp->OnTPanelWinClose(this);
	tpanelparentwinlist.remove(this);
}

void tpanelparentwin::TweetPopTop() {
	currentdisp.front().second->Destroy();
	size_t offset=0;
	wxSizer *sz=sizer->GetItem(offset)->GetSizer();
	if(sz) {
		sz->Clear(true);
		sizer->Remove(offset);
	}
	currentdisp.pop_front();
}

void tpanelparentwin::TweetPopBottom() {
	currentdisp.back().second->Destroy();
	size_t offset=currentdisp.size()-1;
	wxSizer *sz=sizer->GetItem(offset)->GetSizer();
	if(sz) {
		sz->Clear(true);
		sizer->Remove(offset);
	}
	currentdisp.pop_back();
}

void tpanelparentwin::PushTweet(const std::shared_ptr<tweet> &t, unsigned int pushflags) {
	scrollwin->Freeze();
	LogMsgFormat(LFT_TPANEL, "tpanelparentwin::PushTweet, id: %" wxLongLongFmtSpec "d, %d, %X, %d", t->id, displayoffset, pushflags, currentdisp.size());
	tppw_scrollfreeze sf;
	StartScrollFreeze(sf);
	uint64_t id=t->id;
	if(displayoffset) {
		if(id>currentdisp.front().first) {
			if(!(pushflags&TPPWPF_ABOVE)) {
				displayoffset++;
				return;
			}
		}
	}
	if(currentdisp.size()==gc.maxtweetsdisplayinpanel) {
		if(t->id<currentdisp.back().first) {			//off the end of the list
			if(pushflags&TPPWPF_BELOW) {
				TweetPopTop();
				displayoffset++;
			}
			else return;
		}
		else TweetPopBottom();					//too many in list, remove the last one
	}
	size_t index=0;
	auto it=currentdisp.begin();
	for(; it!=currentdisp.end(); it++, index++) {
		if(it->first<id) break;	//insert before this iterator
	}
	tweetdispscr *td = PushTweetIndex(t, index);
	currentdisp.insert(it, std::make_pair(id, td));
	UpdateCLabel();
	EndScrollFreeze(sf);
	scrollwin->Thaw();
}

tweetdispscr *tpanelparentwin::PushTweetIndex(const std::shared_ptr<tweet> &t, size_t index) {
	LogMsgFormat(LFT_TPANEL, "tpanelparentwin::PushTweetIndex, id: %" wxLongLongFmtSpec "d, %d", t->id, index);
	wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
	tweetdispscr *td=new tweetdispscr(t, scrollwin, this, hbox);

	if(t->flags.Get('T')) {
		if(t->rtsrc && gc.rtdisp) {
			td->bm = new wxStaticBitmap(scrollwin, wxID_ANY, t->rtsrc->user->cached_profile_img, wxPoint(-1000, -1000));
		}
		else {
			td->bm = new wxStaticBitmap(scrollwin, wxID_ANY, t->user->cached_profile_img, wxPoint(-1000, -1000));
		}
		hbox->Add(td->bm, 0, wxALL, 2);
	}
	else if(t->flags.Get('D') && t->user_recipient) {
			t->user->ImgHalfIsReady();
			t->user_recipient->ImgHalfIsReady();
			td->bm = new wxStaticBitmap(scrollwin, wxID_ANY, t->user->cached_profile_img_half, wxPoint(-1000, -1000));
			td->bm2 = new wxStaticBitmap(scrollwin, wxID_ANY, t->user_recipient->cached_profile_img_half, wxPoint(-1000, -1000));
			int dim=gc.maxpanelprofimgsize/2;
			if(tpg->arrow_dim!=dim) {
				tpg->arrow=GetArrowIcon(dim);
				tpg->arrow_dim=dim;
			}
			wxStaticBitmap *arrow = new wxStaticBitmap(scrollwin, wxID_ANY, tpg->arrow, wxPoint(-1000, -1000));
			wxGridSizer *gs=new wxGridSizer(2,2,0,0);
			gs->Add(td->bm, 0, 0, 0);
			gs->AddStretchSpacer();
			gs->Add(arrow, 0, wxALIGN_CENTRE, 0);
			gs->Add(td->bm2, 0, 0, 0);
			hbox->Add(gs, 0, wxALL, 2);
	}

	hbox->Add(td, 1, wxALL | wxEXPAND, 2);

	sizer->Insert(index, hbox, 0, wxALL | wxEXPAND, 2);
	td->DisplayTweet();
	return td;
}

uint64_t tpanelparentwin::PushTweetOrRetLoadId(uint64_t id, unsigned int pushflags) {
	std::shared_ptr<tweet> &tobj=ad.tweetobjs[id];
	if(tobj) {
		return PushTweetOrRetLoadId(tobj, pushflags);
	}
	else {
		tobj=std::make_shared<tweet>();
		tobj->id=id;
		tobj->lflags=TLF_BEINGLOADEDFROMDB|TLF_PENDINGINDBTPANELMAP;
		tpaneldbloadmap.insert(std::make_pair(id, tpaneldbloadmap_data(this, pushflags)));
		return id;
	}
}

uint64_t tpanelparentwin::PushTweetOrRetLoadId(const std::shared_ptr<tweet> &tobj, unsigned int pushflags) {
	bool insertpending=false;
	if(tobj->lflags&TLF_BEINGLOADEDFROMDB) {
		insertpending=true;
	}
	else {
		std::shared_ptr<taccount> curacc;
		if(tobj->GetUsableAccount(curacc)) {
			if(curacc->CheckMarkPending(tobj, true)) {
				PushTweet(tobj, pushflags);
			}
			else {
				insertpending=true;
			}
		}
		else PushTweet(tobj, pushflags);	//best effort, as no pendings can be resolved
	}
	if(insertpending) {
		tobj->lflags|=TLF_PENDINGINDBTPANELMAP;
		bool found=false;
		auto pit=tpaneldbloadmap.equal_range(tobj->id);
		for(auto it=pit.first; it!=pit.second; ++it) {
			if((*it).second.win==this) {
				found=true;
				break;
			}
		}
		if(!found) tpaneldbloadmap.insert(std::make_pair(tobj->id, tpaneldbloadmap_data(this, pushflags))); //tpaneldbloadmap.emplace(id, this, pushflags);
	}
	return 0;
}

//if lessthanid is non-zero, is an exclusive upper id limit
void tpanelparentwin::LoadMore(unsigned int n, uint64_t lessthanid, unsigned int pushflags) {
	dbseltweetmsg *loadmsg=0;

	tweetidset::const_iterator stit;
	if(lessthanid) stit=tp->tweetlist.upper_bound(lessthanid);	//finds the first id *less than* lessthanid
	else stit=tp->tweetlist.cbegin();

	for(unsigned int i=0; i<n; i++) {
		if(stit==tp->tweetlist.cend()) break;
		uint64_t loadid=PushTweetOrRetLoadId(*stit, pushflags);
		++stit;
		if(loadid) {
			LogMsgFormat(LFT_TPANEL, "tpanel::LoadMore loading from db id: %" wxLongLongFmtSpec "d", loadid);
			if(!loadmsg) loadmsg=new dbseltweetmsg;
			loadmsg->id_set.insert(loadid);
		}
	}
	if(loadmsg) {
		loadmsg->targ=&dbc;
		loadmsg->cmdevtype=wxextDBCONN_NOTIFY;
		loadmsg->winid=wxDBCONNEVT_ID_TPANELTWEETLOAD;
		if(!gc.persistentmediacache) loadmsg->flags|=DBSTMF_PULLMEDIA;
		dbc.SendMessage(loadmsg);
	}
}

void tpanelparentwin::pageupevthandler(wxCommandEvent &event) {
	PageUpHandler();
}
void tpanelparentwin::pagedownevthandler(wxCommandEvent &event) {
	PageDownHandler();
}

void tpanelparentwin::PageUpHandler() {
	if(displayoffset) {
		size_t pagemove=std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, displayoffset);
		auto it=tp->tweetlist.lower_bound(currentdisp.front().first);
		for(unsigned int i=0; i<pagemove; i++) {
			it--;
			displayoffset--;
			const std::shared_ptr<tweet> &t=ad.GetTweetById(*it);
			if(t->IsReady()) PushTweet(t, TPPWPF_ABOVE);
			//otherwise tweet is already on pending list
		}
	}
	scrollwin->page_scroll_blocked=false;
}
void tpanelparentwin::PageDownHandler() {
	size_t curnum=currentdisp.size();
	size_t tweetnum=tp->tweetlist.size();
	if(curnum+displayoffset<tweetnum) {
		size_t pagemove=std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, tweetnum-(curnum+displayoffset));
		uint64_t lessthanid=currentdisp.back().first;
		LoadMore(pagemove, lessthanid, TPPWPF_BELOW);
	}
	scrollwin->page_scroll_blocked=false;
}

void tpanelparentwin::UpdateCLabel() {
	size_t curnum=currentdisp.size();
	if(curnum) clabel->SetLabel(wxString::Format(wxT("%d - %d of %d"), displayoffset+1, displayoffset+curnum, tp->tweetlist.size()));
	else clabel->SetLabel(wxT("No Tweets"));
}

void tpanelparentwin::StartScrollFreeze(tppw_scrollfreeze &s) {
	int scrollstart;
	scrollwin->GetViewStart(0, &scrollstart);
	if(!scrollstart && !displayoffset) {
		s.scr=0;
		s.extrapixels=0;
		return;
	}
	auto it=currentdisp.cbegin();
	if(it!=currentdisp.cend()) ++it;
	else {
		s.scr=0;
		s.extrapixels=0;
		return;
	}
	auto endit=currentdisp.cend();
	if(endit!=currentdisp.cbegin()) --endit;
	else {
		s.scr=0;
		s.extrapixels=0;
		return;
	}
	for(;it!=endit; ++it) {
		int y;
		(*it).second->GetPosition(0, &y);
		if(y>=0) {
			s.scr=(*it).second;
			s.extrapixels=y;
			return;
		}
	}
}

void tpanelparentwin::EndScrollFreeze(tppw_scrollfreeze &s) {
	if(s.scr) {
		int y;
		s.scr->GetPosition(0, &y);
		int scrollstart;
		scrollwin->GetViewStart(0, &scrollstart);
		scrollstart+=y-s.extrapixels;
		scrollwin->Scroll(-1, std::max(0, scrollstart));
	}
}

void tpanelparentwin::tabdetachhandler(wxCommandEvent &event) {
	mainframe *top = new mainframe( wxT("Retcon"), wxDefaultPosition, wxDefaultSize );
	int index=owner->auib->GetPageIndex(this);
	wxString text=owner->auib->GetPageText(index);
	owner->auib->RemovePage(index);
	owner=top;
	top->auib->AddPage(this, text, true);
	top->Show(true);
}
void tpanelparentwin::tabduphandler(wxCommandEvent &event) {
	tp->MkTPanelWin(owner);
}
void tpanelparentwin::tabdetachedduphandler(wxCommandEvent &event) {
	mainframe *top = new mainframe( wxT("Retcon"), wxDefaultPosition, wxDefaultSize );
	tp->MkTPanelWin(top);
	top->Show(true);
}
void tpanelparentwin::tabclosehandler(wxCommandEvent &event) {
	owner->auib->RemovePage(owner->auib->GetPageIndex(this));
	owner->auib->tabnumcheck();
	Close();
}

BEGIN_EVENT_TABLE(tpanelscrollwin, wxScrolledWindow)
	EVT_SIZE(tpanelscrollwin::resizehandler)
	EVT_COMMAND(wxID_ANY, wxextRESIZE_UPDATE_EVENT, tpanelscrollwin::resizemsghandler)
	EVT_SCROLLWIN_TOP(tpanelscrollwin::OnScrollHandler)
	EVT_SCROLLWIN_BOTTOM(tpanelscrollwin::OnScrollHandler)
	EVT_SCROLLWIN_LINEUP(tpanelscrollwin::OnScrollHandler)
	EVT_SCROLLWIN_LINEDOWN(tpanelscrollwin::OnScrollHandler)
	EVT_SCROLLWIN_PAGEUP(tpanelscrollwin::OnScrollHandler)
	EVT_SCROLLWIN_PAGEDOWN(tpanelscrollwin::OnScrollHandler)
	EVT_SCROLLWIN_THUMBRELEASE(tpanelscrollwin::OnScrollHandler)
END_EVENT_TABLE()

tpanelscrollwin::tpanelscrollwin(tpanelparentwin *parent_)
	: wxScrolledWindow(parent_), parent(parent_), resize_update_pending(false), page_scroll_blocked(false) {

}

void tpanelscrollwin::resizehandler(wxSizeEvent &event) {
	//LogMsgFormat(LFT_TPANEL, wxT("tpanelscrollwin::resizehandler"));
	//FitInside();
	//Refresh();
	//Update();
}

void tpanelscrollwin::resizemsghandler(wxCommandEvent &event) {
	//wxLogWarning(wxT("tpanelscrollwin::resizemsghandler"));
	FitInside();
	Refresh();
	Update();
	resize_update_pending=false;
}

void tpanelscrollwin::OnScrollHandler(wxScrollWinEvent &event) {
	if(event.GetOrientation()!=wxVERTICAL) {
		event.Skip();
		return;
	}
	wxEventType type=event.GetEventType();
	bool upok=(type==wxEVT_SCROLLWIN_TOP || type==wxEVT_SCROLLWIN_LINEUP || type==wxEVT_SCROLLWIN_PAGEUP || type==wxEVT_SCROLLWIN_THUMBRELEASE);
	bool downok=(type==wxEVT_SCROLLWIN_BOTTOM || type==wxEVT_SCROLLWIN_LINEDOWN || type==wxEVT_SCROLLWIN_PAGEDOWN || type==wxEVT_SCROLLWIN_THUMBRELEASE);

	int y, sy, wy, cy;
	GetViewStart(0, &y);
	GetScrollPixelsPerUnit(0, &sy);
	GetVirtualSize(0, &wy);
	GetClientSize(0, &cy);
	int endpos=(y*sy)+cy;
	//LogMsgFormat(LFT_TPANEL, wxT("tpanelscrollwin::OnScrollHandler %d %d %d %d %d"), y, sy, wy, cy, endpos);
	bool scrollup=(y==0 && upok);
	bool scrolldown=(endpos>=wy && downok);
	if(scrollup && !scrolldown && !page_scroll_blocked) {
		wxCommandEvent evt(wxextTP_PAGEUP_EVENT);
		parent->AddPendingEvent(evt);
		page_scroll_blocked=true;
	}
	if(!scrollup && scrolldown && !page_scroll_blocked) {
		wxCommandEvent evt(wxextTP_PAGEDOWN_EVENT);
		parent->AddPendingEvent(evt);
		page_scroll_blocked=true;
	}

	if(type==wxEVT_SCROLLWIN_LINEUP || type==wxEVT_SCROLLWIN_LINEDOWN) {
		if(type==wxEVT_SCROLLWIN_LINEUP) y-=15;
		else y+=15;
		Scroll(-1, std::max(0, y));
		return;
	}

	event.Skip();
}

BEGIN_EVENT_TABLE(tweetdispscr, wxRichTextCtrl)
	EVT_TEXT_URL(wxID_ANY, tweetdispscr::urleventhandler)
	EVT_MOUSEWHEEL(tweetdispscr::mousewheelhandler)
END_EVENT_TABLE()

tweetdispscr::tweetdispscr(const std::shared_ptr<tweet> &td_, tpanelscrollwin *parent, tpanelparentwin *tppw_, wxBoxSizer *hbox_)
: wxRichTextCtrl(parent, wxID_ANY, wxEmptyString, wxPoint(-1000, -1000), wxDefaultSize, wxRE_READONLY),
td(td_), tppw(tppw_), tpsw(parent), hbox(hbox_), bm(0), bm2(0) {
	GetCaret()->Hide();
}

tweetdispscr::~tweetdispscr() {
	//tppw->currentdisp.remove_if([this](const std::pair<uint64_t, tweetdispscr *> &p){ return p.second==this; });
}

//use -1 for end to run until end of string
static void DoWriteSubstr(tweetdispscr &td, const std::string &str, int start, int end, int &track_byte, int &track_index, bool trim) {
	while(str[track_byte]) {
		if(track_index==start) break;
		register int charsize=utf8firsttonumbytes(str[track_byte]);
		track_byte+=charsize;
		track_index++;
	}
	int start_offset=track_byte;

	while(str[track_byte]) {
		if(track_index==end) break;
		if(str[track_byte]=='&') {
			char rep=0;
			if(str[track_byte+1]=='l' && str[track_byte+2]=='t' && str[track_byte+3]==';') {
				rep='<';
			}
			else if(str[track_byte+1]=='g' && str[track_byte+2]=='t' && str[track_byte+3]==';') {
				rep='>';
			}
			if(rep) {
				td.WriteText(wxString::FromUTF8(&str[start_offset], track_byte-start_offset));
				track_index+=4;
				track_byte+=4;
				td.WriteText(wxString((wxChar) rep));
				start_offset=track_byte;
				continue;
			}
		}
		register int charsize=utf8firsttonumbytes(str[track_byte]);
		track_byte+=charsize;
		track_index++;
	}
	int end_offset=track_byte;
	wxString wstr=wxString::FromUTF8(&str[start_offset], end_offset-start_offset);
	if(trim) wstr.Trim();
	if(wstr.Len()) td.WriteText(wstr);
}

void tweetdispscr::DisplayTweet(bool redrawimg) {
	updatetime=0;
	std::forward_list<media_entity*> me_list;
	auto last_me=me_list.before_begin();

	tweet &tw=*td;
	userdatacontainer *udc=tw.user.get();
	userdatacontainer *udc_recip=tw.user_recipient.get();

	if(redrawimg) {
		if(bm && bm2 && udc_recip) {
			udc->ImgHalfIsReady();
			udc_recip->ImgHalfIsReady();
			bm->SetBitmap(udc->cached_profile_img_half);
			bm2->SetBitmap(udc_recip->cached_profile_img_half);
		}
		else if(bm) {
			if(tw.rtsrc && gc.rtdisp) bm->SetBitmap(tw.rtsrc->user->cached_profile_img);
			else bm->SetBitmap(udc->cached_profile_img);
		}
	}

	Clear();
	wxString format=wxT("");
	wxString str=wxT("");
	if(tw.flags.Get('R') && gc.rtdisp) format=gc.gcfg.rtdispformat.val;
	else if(tw.flags.Get('T')) format=gc.gcfg.tweetdispformat.val;
	else if(tw.flags.Get('D')) format=gc.gcfg.dmdispformat.val;

	auto flush=[&]() {
		if(str.size()) {
			this->WriteText(str);
			str.clear();
		}
	};
	auto userfmt=[&](userdatacontainer *u, size_t &i) {
		i++;
		if(i>=format.size()) return;
		switch(format[i]) {
			case 'n':
				str+=wxstrstd(u->GetUser().screen_name);
				break;
			case 'N':
				str+=wxString::Format("%" wxLongLongFmtSpec "d", u->id);
				break;
			default:
				break;
		}
	};

	for(size_t i=0; i<format.size(); i++) {
		switch(format[i]) {
			case 'u':
				userfmt(udc, i);
				break;
			case 'U':
				if(udc_recip) userfmt(udc_recip, i);
				else i++;
				break;
			case 'r':
				if(tw.rtsrc && gc.rtdisp) userfmt(tw.rtsrc->user.get(), i);
				else userfmt(udc, i);
				break;
			case 'N':
				flush();
				Newline();
				break;
			case 'F':
				str+=wxstrstd(tw.flags.GetString());
				break;
			case 't':
				flush();
				reltimestart=GetInsertionPoint();
				WriteText(getreltimestr(tw.createtime, updatetime));
				reltimeend=GetInsertionPoint();
				if(!tpg->minutetimer.IsRunning()) tpg->minutetimer.Start(60000, wxTIMER_CONTINUOUS);
				break;
			case 'T':
				str+=rc_wx_strftime(gc.gcfg.datetimeformat.val, localtime(&tw.createtime), tw.createtime, true);
				break;
			case 'B': flush(); BeginBold(); break;
			case 'b': flush(); EndBold(); break;
			case 'L': flush(); BeginUnderline(); break;
			case 'l': flush(); EndUnderline(); break;
			case 'I': flush(); BeginItalic(); break;
			case 'i': flush(); EndItalic(); break;
			case 'C':
			case 'c': {
				flush();
				tweet &twgen=(format[i]=='c' && gc.rtdisp)?*(tw.rtsrc):tw;
				unsigned int nextoffset=0;
				unsigned int entnum=0;
				int track_byte=0;
				int track_index=0;
				for(auto it=twgen.entlist.begin(); it!=twgen.entlist.end(); it++, entnum++) {
					entity &et=*it;
					DoWriteSubstr(*this, twgen.text, nextoffset, et.start, track_byte, track_index, false);
					BeginUnderline();
					BeginURL(wxString::Format(wxT("%d"), entnum));
					WriteText(wxstrstd(et.text));
					nextoffset=et.end;
					EndURL();
					EndUnderline();
					if((et.type==ENT_MEDIA || et.type==ENT_URL_IMG) && et.media_id) {
						media_entity &me=ad.media_list[et.media_id];
						last_me=me_list.insert_after(last_me, &me);
					}
				}
				DoWriteSubstr(*this, twgen.text, nextoffset, -1, track_byte, track_index, true);
				break;
			}
			case '\'':
			case '"': {
				auto quotechar=format[i];
				i++;
				while(i<format.size()) {
					if(format[i]==quotechar) break;
					else str+=format[i];
					i++;
				}
				break;
			}
			default:
				str+=format[i];
				break;
		}
	}
	flush();

	if(!me_list.empty()) {
		Newline();
		BeginAlignment(wxTEXT_ALIGNMENT_CENTRE);
		for(auto it=me_list.begin(); it!=me_list.end(); ++it) {
			BeginURL(wxString::Format(wxT("M%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), (int64_t) (*it)->media_id.m_id, (int64_t) (*it)->media_id.t_id));
			if((*it)->flags&ME_HAVE_THUMB) {
				AddImage((*it)->thumbimg);
			}
			else {
				BeginUnderline();
				WriteText(wxT("[Image]"));
				EndUnderline();
			}
			EndURL();
		}
		EndAlignment();
	}
	LayoutContent();
	tpsw->FitInside();
}

void tweetdispscr::DoResize() {
	//int height;
	//int width;
	//GetVirtualSize(&width, &height);
	//hbox->SetItemMinSize(this, 10, height+10);
	//GetScrollRange(wxVERTICAL)*
}

void tweetdispscr::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
		       int noUnitsX, int noUnitsY,
		       int xPos, int yPos,
		       bool noRefresh ) {
	//LogMsgFormat(LFT_TPANEL, wxT("tweetdispscr::SetScrollbars, tweet id %" wxLongLongFmtSpec "d"), td->id);
	wxRichTextCtrl::SetScrollbars(0, 0, 0, 0, 0, 0, noRefresh);
	int newheight=(pixelsPerUnitY*noUnitsY)+4;
	hbox->SetItemMinSize(this, 10, newheight);
	//hbox->SetMinSize(10, newheight+4);
	//SetSize(wxDefaultCoord, wxDefaultCoord, wxDefaultCoord, newheight, wxSIZE_USE_EXISTING);
	tpsw->FitInside();
	if(!tpsw->resize_update_pending) {
		tpsw->resize_update_pending=true;
		wxCommandEvent event(wxextRESIZE_UPDATE_EVENT, GetId());
		tpsw->AddPendingEvent(event);
	}
}

void tweetdispscr::urleventhandler(wxTextUrlEvent &event) {
	tweet &tw=*td;
	long start=event.GetURLStart();
	wxRichTextAttr textattr;
	GetStyle(start, textattr);
	wxString url=textattr.GetURL();
	LogMsgFormat(LFT_TPANEL, wxT("URL clicked, id: %s"), url.c_str());
	if(url[0]=='M') {
		media_id_type media_id;
		//url.Mid(1).ToULongLong(&media_id);	//not implemented on some systems

		//poor man's strtoull
		unsigned int i=1;
		for(; i<url.Len(); i++) {
			if(url[i]>='0' && url[i]<='9') {
				media_id.m_id*=10;
				media_id.m_id+=url[i]-'0';
			}
			else break;
		}
		if(url[i]!='_') return;
		for(i++; i<url.Len(); i++) {
			if(url[i]>='0' && url[i]<='9') {
				media_id.t_id*=10;
				media_id.t_id+=url[i]-'0';
			}
			else break;
		}

		LogMsgFormat(LFT_TPANEL, wxT("Media image clicked, str: %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d"), url.Mid(1).c_str(), media_id.m_id, media_id.t_id);
		if(ad.media_list[media_id].win) {
			ad.media_list[media_id].win->Raise();
		}
		else new media_display_win(this, media_id);
	}
	else {
		unsigned long counter;
		url.ToULong(&counter);
		auto it=tw.entlist.begin();
		while(it!=tw.entlist.end()) {
			if(!counter) {
				//got entity
				entity &et= *it;
				switch(et.type) {
					case ENT_HASHTAG:
						break;
					case ENT_URL:
					case ENT_URL_IMG:
					case ENT_MEDIA:
						::wxLaunchDefaultBrowser(wxstrstd(et.fullurl));
						break;
					case ENT_MENTION:
						break;
				}
				return;
			}
			else {
				counter--;
				it++;
			}
		}
	}
}

void tweetdispscr::mousewheelhandler(wxMouseEvent &event) {
	//LogMsg(LFT_TPANEL, wxT("MouseWheel"));
	event.SetEventObject(GetParent());
	GetParent()->GetEventHandler()->ProcessEvent(event);
}

BEGIN_EVENT_TABLE(image_panel, wxPanel)
	EVT_PAINT(image_panel::OnPaint)
	EVT_SIZE(image_panel::OnResize)
END_EVENT_TABLE()

image_panel::image_panel(media_display_win *parent, wxSize size) : wxPanel(parent, wxID_ANY, wxDefaultPosition, size) {

}

void image_panel::OnPaint(wxPaintEvent &event) {
	wxPaintDC dc(this);
	dc.DrawBitmap(bm, 0, 0, 0);
}

void image_panel::OnResize(wxSizeEvent &event) {
	UpdateBitmap();
}

void image_panel::UpdateBitmap() {
	//if(imgok) {
		bm=wxBitmap(img.Scale(GetSize().GetWidth(), GetSize().GetHeight(), wxIMAGE_QUALITY_HIGH));
	//}
	/*else {
		bm.Create(GetSize().GetWidth(),GetSize().GetHeight());
		wxMemoryDC mdc(bm);
		mdc.SetBackground(*wxBLACK_BRUSH);
		mdc.SetTextForeground(*wxWHITE);
		mdc.SetTextBackground(*wxBLACK);
		mdc.Clear();
		wxSize size=mdc.GetTextExtent(message);
		mdc.DrawText(message, (GetSize().GetWidth()/2)-(size.GetWidth()/2), (GetSize().GetHeight()/2)-(size.GetHeight()/2));
	}*/
	Refresh();
}

BEGIN_EVENT_TABLE(media_display_win, wxFrame)
	EVT_MENU(MDID_SAVE,  media_display_win::OnSave)
END_EVENT_TABLE()

media_display_win::media_display_win(wxWindow *parent, media_id_type media_id_)
	: wxFrame(parent, wxID_ANY, wxstrstd(ad.media_list[media_id_].media_url)), media_id(media_id_), sb(0), st(0), sz(0) {
	Freeze();
	media_entity *me=&ad.media_list[media_id_];
	me->win=this;

	if(me->flags&ME_LOAD_FULL && !(me->flags&ME_HAVE_FULL)) {
		//try to load from file
		char *data;
		size_t size;
		if(LoadFromFileAndCheckHash(me->cached_full_filename(), me->full_img_sha1, data, size)) {
			me->flags|=ME_HAVE_FULL;
			me->fulldata.assign(data, size);	//redundant copy, but oh well
		}
		free(data);
	}
	if(!(me->flags&ME_HAVE_FULL) && me->media_url.size()) {
		new mediaimgdlconn(me->media_url, media_id_, MIDC_FULLIMG | MIDC_OPPORTUNIST_THUMB | MIDC_OPPORTUNIST_REDRAW_TWEETS);
	}

	wxMenu *menuF = new wxMenu;
	savemenuitem=menuF->Append( MDID_SAVE, wxT("&Save Image"));

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuF, wxT("&File"));

	SetMenuBar( menuBar );

	sz=new wxBoxSizer(wxVERTICAL);
	SetSizer(sz);
	Update();
	Thaw();
	Show();
}

media_display_win::~media_display_win() {
	media_entity *me=GetMediaEntity();
	if(me) me->win=0;
}

void media_display_win::Update() {
	wxImage img;
	wxString message;
	bool imgok=GetImage(img, message);
	if(imgok) {
		savemenuitem->Enable(true);
		if(st) {
			sz->Detach(st);
			st->Destroy();
			st=0;
		}
		wxSize size(img.GetWidth(), img.GetHeight());
		if(!sb) {
			sb=new image_panel(this, size);
			sb->img=img;
			sz->Add(sb, 1, wxSHAPED | wxALIGN_CENTRE);
		}
		else sb->SetSize(size);
		sb->UpdateBitmap();
	}
	else {
		savemenuitem->Enable(false);
		if(sb) {
			sz->Detach(sb);
			sb->Destroy();
			sb=0;
		}
		if(!st) {
			st=new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
			sz->Add(st, 0, wxALIGN_CENTRE);
			sz->SetMinSize(200, 200);
		}
		else st->SetLabel(message);
	}
	sz->Fit(this);
}

bool media_display_win::GetImage(wxImage &img, wxString &message) {
	media_entity *me=GetMediaEntity();
	if(me) {
		if(me->flags&ME_HAVE_FULL) {
			wxMemoryInputStream memstream(me->fulldata.data(), me->fulldata.size());
			img.LoadFile(memstream, wxBITMAP_TYPE_ANY);
			return true;
		}
		else if(me->flags&ME_FULL_FAILED) {
			message=wxT("Failed to Load Image");
		}
		else {
			message=wxT("Loading Image");
		}
	}
	else {
		message=wxT("No Image");
	}
	return false;
}

media_entity *media_display_win::GetMediaEntity() {
	auto it=ad.media_list.find(media_id);
	if(it!=ad.media_list.end()) {
		return &it->second;
	}
	else return 0;
}

void media_display_win::OnSave(wxCommandEvent &event) {
	media_entity *me=GetMediaEntity();
	if(me) {
		wxString hint;
		wxString ext;
		bool hasext;
		wxFileName::SplitPath(wxstrstd(me->media_url), 0, 0, &hint, &ext, &hasext, wxPATH_UNIX);
		if(hasext) hint+=wxT(".")+ext;
		wxString newhint;
		if(hint.EndsWith(wxT(":large"), &newhint)) hint=newhint;
		wxString filename=wxFileSelector(wxT("Save Image"), wxT(""), hint, ext, wxT("*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);
		if(filename.Len()) {
			wxFile file(filename, wxFile::write);
			file.Write(me->fulldata.data(), me->fulldata.size());
		}
	}
}

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid) {
	//LogMsg(LFT_TPANEL, wxT("Redirect MouseWheel"));
	wxWindow *wind=wxFindWindowAtPoint(wxGetMousePosition() /*event.GetPosition()*/);
	while(wind) {
		if(wind!=avoid && std::count(tpanelparentwinlist.begin(), tpanelparentwinlist.end(), wind)) {
			tpanelparentwin *tppw=(tpanelparentwin*) wind;
			//LogMsgFormat(LFT_TPANEL, wxT("Redirect MouseWheel: Dispatching to %s"), wxstrstd(tppw->tp->name).c_str());
			event.SetEventObject(tppw->scrollwin);
			tppw->scrollwin->GetEventHandler()->ProcessEvent(event);
			return true;
		}
		wind=wind->GetParent();
	}
	return false;
}

wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp, bool localtime) {
	#ifdef __WINDOWS__	//%z is broken in MSVCRT, use a replacement
				//also add %F, %R, %T, %s
				//this is adapted from npipe var.cpp
	wxString newfmt;
	newfmt.Alloc(format.length());
	wxString &real_format=newfmt;
	const wxChar *ch=format.c_str();
	const wxChar *cur=ch;
	while(*ch) {
		if(ch[0]=='%') {
			wxString insert;
			if(ch[1]=='z') {
				int hh;
				int mm;
				if(localtime) {
					TIME_ZONE_INFORMATION info;
					DWORD res = GetTimeZoneInformation(&info);
					int bias = - info.Bias;
					if(res==TIME_ZONE_ID_DAYLIGHT) bias-=info.DaylightBias;
					hh = bias / 60;
					if(bias<0) bias=-bias;
					mm = bias % 60;
				}
				else {
					hh=mm=0;
				}
				insert.Printf(wxT("%+03d%02d"), hh, mm);
			}
			else if(ch[1]=='F') {
				insert=wxT("%Y-%m-%d");
			}
			else if(ch[1]=='R') {
				insert=wxT("%H:%M");
			}
			else if(ch[1]=='T') {
				insert=wxT("%H:%M:%S");
			}
			else if(ch[1]=='s') {
				insert.Printf(wxT("%" wxLongLongFmtSpec "d"), (long long int) timestamp);
			}
			else if(ch[1]) {
				ch++;
			}
			if(insert.length()) {
				real_format.Append(wxString(cur, ch-cur));
				real_format.Append(insert);
				cur=ch+2;
			}
		}
		ch++;
	}
	real_format.Append(cur);
	#else
	const wxString &real_format=format;
	#endif

	char timestr[256];
	strftime(timestr, sizeof(timestr), real_format.ToUTF8(), tm);
	return wxstrstd(timestr);
}

wxString getreltimestr(time_t timestamp, time_t &updatetime) {
	time_t nowtime=time(0);
	if(timestamp>nowtime) {
		updatetime=30+timestamp-nowtime;
		return wxT("In the future");
	}
	time_t diff=nowtime-timestamp;
	if(diff<60) {
		updatetime=nowtime+60;
		return wxT("< 1 minute ago");
	}
	diff/=60;
	if(diff<120) {
		updatetime=nowtime+60;
		return wxString::Format(wxT("%d minute%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));
	}
	diff/=60;
	if(diff<48) {
		updatetime=nowtime+60*30;
		return wxString::Format(wxT("%d hour%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));
	}
	diff/=24;
	if(diff<30) {
		updatetime=nowtime+60*60*2;
		return wxString::Format(wxT("%d day%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));
	}
	diff/=30;
	if(diff<12) {
		updatetime=nowtime+60*60*24*2;
		return wxString::Format(wxT("%d month%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));
	}
	diff/=12;
	updatetime=nowtime+60*60*24*2;
	return wxString::Format(wxT("%d year%s ago"), diff, (diff!=1)?wxT("s"):wxT(""));

}

void tpanelreltimeupdater::Notify() {
	time_t nowtime=time(0);
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		for(auto jt=(*it)->currentdisp.begin(); jt!=(*it)->currentdisp.end(); ++jt) {
			tweetdispscr &td=*((*jt).second);
			if(!td.updatetime) continue;
			else if(nowtime>=td.updatetime) {
				td.Delete(wxRichTextRange(td.reltimestart, td.reltimeend));
				td.SetInsertionPoint(td.reltimestart);
				td.WriteText(getreltimestr(td.td->createtime, td.updatetime));
				td.reltimeend=td.GetInsertionPoint();
			}
			else break;
		}
	}
}
