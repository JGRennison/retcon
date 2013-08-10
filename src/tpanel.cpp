//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
//
//  NOTE: This software is licensed under the GPL. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  Jonathan Rennison (or anybody else) is in no way responsible, or liable
//  for this program or its use in relation to users, 3rd parties or to any
//  persons in any way whatsoever.
//
//  You  should have  received a  copy of  the GNU  General Public
//  License along  with this program; if  not, write to  the Free Software
//  Foundation, Inc.,  59 Temple Place,  Suite 330, Boston,  MA 02111-1307
//  USA
//
//  2012 - j.g.rennison@gmail.com
//==========================================================================

#include "retcon.h"
#include "utf8.h"
#include "res.h"
#include "version.h"

#ifndef TPANEL_COPIOUS_LOGGING
#define TPANEL_COPIOUS_LOGGING 0
#endif

std::shared_ptr<tpanelglobal> tpanelglobal::Get() {
	if(tpg_glob.expired()) {
		std::shared_ptr<tpanelglobal> tmp=std::make_shared<tpanelglobal>();
		tpg_glob=tmp;
		return tmp;
	}
	else return tpg_glob.lock();
}

std::weak_ptr<tpanelglobal> tpanelglobal::tpg_glob;

static void PerAccTPanelMenu(wxMenu *menu, tpanelmenudata &map, int &nextid, unsigned int flagbase, unsigned int dbindex) {
	map[nextid]={dbindex, flagbase|TPF_AUTO_TW};
	menu->Append(nextid++, wxT("&Tweets"));
	map[nextid]={dbindex, flagbase|TPF_AUTO_MN};
	menu->Append(nextid++, wxT("&Mentions"));
	map[nextid]={dbindex, flagbase|TPF_AUTO_DM};
	menu->Append(nextid++, wxT("&DMs"));
	map[nextid]={dbindex, flagbase|TPF_AUTO_TW|TPF_AUTO_MN};
	menu->Append(nextid++, wxT("T&weets and Mentions"));
	map[nextid]={dbindex, flagbase|TPF_AUTO_MN|TPF_AUTO_DM};
	menu->Append(nextid++, wxT("M&entions and DMs"));
	map[nextid]={dbindex, flagbase|TPF_AUTO_TW|TPF_AUTO_MN|TPF_AUTO_DM};
	menu->Append(nextid++, wxT("Tweets, Mentions &and DMs"));
}

void MakeTPanelMenu(wxMenu *menuP, tpanelmenudata &map) {
	wxMenuItemList items=menuP->GetMenuItems();		//make a copy to avoid memory issues if Destroy modifies the list
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
	std::shared_ptr<taccount> acc;
	wxString name;
	wxString accname;
	wxString type;
	if(dbindex) {
		if(GetAccByDBIndex(dbindex, acc)) {
			name=acc->dispname;
			accname=acc->name;
		}
		else return;
	}
	else {
		name=wxT("All Accounts");
		accname=wxT("*");
	}
	if(flags&TPF_AUTO_TW && flags&TPF_AUTO_MN && flags&TPF_AUTO_DM) type=wxT("All");
	else if(flags&TPF_AUTO_TW && flags&TPF_AUTO_MN) type=wxT("Tweets & Mentions");
	else if(flags&TPF_AUTO_MN && flags&TPF_AUTO_DM) type=wxT("Mentions & DMs");
	else if(flags&TPF_AUTO_TW) type=wxT("Tweets");
	else if(flags&TPF_AUTO_DM) type=wxT("DMs");
	else if(flags&TPF_AUTO_MN) type=wxT("Mentions");

	std::string paneldispname=std::string(wxString::Format(wxT("[%s - %s]"), name.c_str(), type.c_str()).ToUTF8());
	std::string panelname=std::string(wxString::Format(wxT("___ATL_%s_%s"), accname.c_str(), type.c_str()).ToUTF8());

	auto tp=tpanel::MkTPanel(panelname, paneldispname, flags, &acc);
	tp->MkTPanelWin(parent, true);
}

void CheckClearNoUpdateFlag_All() {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		(*it)->CheckClearNoUpdateFlag();
	}
}

void tpanel::PushTweet(const std::shared_ptr<tweet> &t, unsigned int pushflags) {
	LogMsgFormat(LFT_TPANEL, wxT("Pushing tweet id %" wxLongLongFmtSpec "d to panel %s"), t->id, wxstrstd(name).c_str());
	if(RegisterTweet(t)) {
		for(auto &i : twin) {
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LFT_TPANEL, wxT("TCL: Pushing tweet id %" wxLongLongFmtSpec "d to tpanel window"), t->id);
			#endif
			i->PushTweet(t, pushflags);
		}
	}
	else {	//already have this in tpanel, update it
		for(auto &i : twin) {
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LFT_TPANEL, wxT("TCL: Updating tpanel window tweet: id %" wxLongLongFmtSpec "d"), t->id);
			#endif
			i->UpdateOwnTweet(*(t.get()), false);
		}
	}
}

//returns true if new tweet
bool tpanel::RegisterTweet(const std::shared_ptr<tweet> &t) {
	if(tweetlist.count(t->id)) {
		//already have this tweet
		return false;
	}
	else {
		if(t->id>upperid) upperid=t->id;
		if(t->id<lowerid || lowerid==0) lowerid=t->id;
		tweetlist.insert(t->id);
		if(t->flags.Get('u')) {
			cids.unreadids.insert(t->id);
		}
		if(t->flags.Get('H')) {
			cids.highlightids.insert(t->id);
		}
		return true;
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
			ad.cids.foreach(this->cids, [&](tweetidset &adtis, tweetidset &thistis) {
				std::set_intersection(tweetlist.begin(), tweetlist.end(), adtis.begin(), adtis.end(), std::inserter(thistis, thistis.end()), tweetlist.key_comp());
			});
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

void tpanel::OnTPanelWinClose(tpanelparentwin_nt *tppw) {
	twin.remove(tppw);
	if(twin.empty() && flags&TPF_DELETEONWINCLOSE) {
		ad.tpanels.erase(name);
	}
}

tpanelparentwin *tpanel::MkTPanelWin(mainframe *parent, bool select) {
	return new tpanelparentwin(shared_from_this(), parent, select);
}

bool tpanel::IsSingleAccountTPanel() const {
	if(alist.size() <= 1) return true;
	if(flags & TPF_AUTO_ACC || flags & TPF_USER_TIMELINE) return true;
	return false;
}

void tpanel::TPPWFlagMaskAllTWins(unsigned int set, unsigned int clear) const {
	for(auto &jt : twin) {
		jt->tppw_flags |= set;
		jt->tppw_flags &= ~clear;
	}
}

enum {
	NOTEBOOK_ID=42,
};

BEGIN_EVENT_TABLE(tpanelnotebook, wxAuiNotebook)
	EVT_AUINOTEBOOK_ALLOW_DND(NOTEBOOK_ID, tpanelnotebook::dragdrophandler)
	EVT_AUINOTEBOOK_DRAG_DONE(NOTEBOOK_ID, tpanelnotebook::dragdonehandler)
	EVT_AUINOTEBOOK_TAB_RIGHT_DOWN(NOTEBOOK_ID, tpanelnotebook::tabrightclickhandler)
	EVT_AUINOTEBOOK_PAGE_CLOSED(NOTEBOOK_ID, tpanelnotebook::tabclosedhandler)
	EVT_SIZE(tpanelnotebook::onsizeevt)
END_EVENT_TABLE()

tpanelnotebook::tpanelnotebook(mainframe *owner_, wxWindow *parent) :
wxAuiNotebook(parent, NOTEBOOK_ID, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_TAB_EXTERNAL_MOVE | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_WINDOWLIST_BUTTON),
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
	PostSplitSizeCorrect();
	tabnumcheck();
}
void tpanelnotebook::tabclosedhandler(wxAuiNotebookEvent& event) {
	PostSplitSizeCorrect();
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
		menu.SetTitle(wxstrstd(tppw->tp->dispname));
		menu.Append(TPPWID_SPLIT, wxT("Split"));
		menu.Append(TPPWID_DETACH, wxT("Detach"));
		menu.Append(TPPWID_DUP, wxT("Duplicate"));
		menu.Append(TPPWID_DETACHDUP, wxT("Detached Duplicate"));
		menu.Append(TPPWID_CLOSE, wxT("Close"));
		tppw->PopupMenu(&menu);
	}
}

void tpanelnotebook::Split(size_t page, int direction) {
	//owner->Freeze();
	wxAuiNotebook::Split(page, direction);
	PostSplitSizeCorrect();
	//owner->Thaw();
}

void tpanelnotebook::PostSplitSizeCorrect() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelnotebook::PostSplitSizeCorrect(): START"));
	#endif
	wxSize totalsize=GetClientSize();

	wxAuiPaneInfoArray& all_panes = m_mgr.GetAllPanes();
	size_t pane_count = all_panes.GetCount();
	size_t tabctrl_count=0;
	std::forward_list<wxAuiPaneInfo *> tabctrlarray;
	for(size_t i = 0; i < pane_count; ++i) {
		if(all_panes.Item(i).name != wxT("dummy")) {
			tabctrl_count++;
			tabctrlarray.push_front(&(all_panes.Item(i)));
			LogMsgFormat(LFT_TPANEL, wxT("PostSplitSizeCorrect:: %d %d %d %d"), all_panes.Item(i).dock_direction, all_panes.Item(i).dock_layer, all_panes.Item(i).dock_row, all_panes.Item(i).dock_pos);
		}
	}
	for(auto it=tabctrlarray.begin(); it!=tabctrlarray.end(); ++it) {
		wxAuiPaneInfo &pane=**(it);
		pane.BestSize(totalsize.GetWidth()/tabctrl_count, totalsize.GetHeight());
		pane.MaxSize(totalsize.GetWidth()/tabctrl_count, totalsize.GetHeight());
		pane.DockFixed();
		if(pane.dock_direction!=wxAUI_DOCK_LEFT && pane.dock_direction!=wxAUI_DOCK_RIGHT && pane.dock_direction!=wxAUI_DOCK_CENTRE) {
			pane.Right();
			pane.dock_row=0;
			pane.dock_pos=1;	//trigger code below
		}
		if(pane.dock_pos>0) {	//make a new row, bumping up any others to make room
			if(pane.dock_direction==wxAUI_DOCK_LEFT) {
				for(auto jt=tabctrlarray.begin(); jt!=tabctrlarray.end(); ++jt) {
					if((*jt)->dock_direction==pane.dock_direction && (*jt)->dock_row>pane.dock_row && (*jt)->dock_layer==pane.dock_layer) (*jt)->dock_row++;
				}
				pane.dock_pos=0;
				pane.dock_row++;
			}
			else {
				for(auto jt=tabctrlarray.begin(); jt!=tabctrlarray.end(); ++jt) {
					if((*jt)->dock_direction==pane.dock_direction && (*jt)->dock_row>=pane.dock_row && (*jt)->dock_layer==pane.dock_layer && (*jt)->dock_pos==0) (*jt)->dock_row++;
				}
				pane.dock_pos=0;
			}
		}
	}
	for(auto it=tabctrlarray.begin(); it!=tabctrlarray.end(); ++it) m_mgr.InsertPane((*it)->window, (**it), wxAUI_INSERT_ROW);
	m_mgr.Update();

	for(size_t i = 0; i < pane_count; ++i) {
		if(all_panes.Item(i).name != wxT("dummy")) {
			LogMsgFormat(LFT_TPANEL, wxT("PostSplitSizeCorrect:: %d %d %d %d"), all_panes.Item(i).dock_direction, all_panes.Item(i).dock_layer, all_panes.Item(i).dock_row, all_panes.Item(i).dock_pos);
		}
	}

	DoSizing();
	owner->Refresh();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelnotebook::PostSplitSizeCorrect(): END"));
	#endif
}

void tpanelnotebook::onsizeevt(wxSizeEvent &event) {
	PostSplitSizeCorrect();
	event.Skip();
}

void tpanelnotebook::FillWindowLayout(unsigned int mainframeindex) {
	wxAuiPaneInfoArray& all_panes = m_mgr.GetAllPanes();
	size_t pane_count = all_panes.GetCount();

	size_t pagecount = GetPageCount();
	for(size_t i = 0; i < pagecount; ++i) {
		tpanelparentwin_nt *tppw = dynamic_cast<tpanelparentwin_nt*>(GetPage(i));
		if(!tppw) continue;

		wxAuiTabCtrl* tc;
		int tabindex;
		if(!FindTab(tppw, &tc, &tabindex)) continue;

		wxWindow *tabframe = GetTabFrameFromTabCtrl(tc);
		if(!tabframe) continue;

		unsigned int splitindex = 0;
		bool found = false;
		for(size_t i = 0; i < pane_count; ++i) {
			if(all_panes.Item(i).name == wxT("dummy")) continue;
			if(all_panes.Item(i).window == tabframe) {
				found = true;
				break;
			}
			else splitindex++;
		}
		if(!found) continue;

		ad.twinlayout.emplace_back();
		twin_layout_desc &twld = ad.twinlayout.back();
		twld.mainframeindex = mainframeindex;
		twld.splitindex = splitindex;
		twld.tabindex = tabindex;
		twld.acc = tppw->tp->assoc_acc;
		twld.name = tppw->tp->name;
		twld.dispname = tppw->tp->dispname;
		twld.flags = tppw->tp->flags;
	}
}

DEFINE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT)
DEFINE_EVENT_TYPE(wxextTP_PAGEUP_EVENT)
DEFINE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT)

BEGIN_EVENT_TABLE(panelparentwin_base, wxPanel)
	EVT_COMMAND(wxID_ANY, wxextTP_PAGEUP_EVENT, panelparentwin_base::pageupevthandler)
	EVT_COMMAND(wxID_ANY, wxextTP_PAGEDOWN_EVENT, panelparentwin_base::pagedownevthandler)
	EVT_BUTTON(TPPWID_TOPBTN, panelparentwin_base::pagetopevthandler)
END_EVENT_TABLE()

panelparentwin_base::panelparentwin_base(wxWindow *parent, bool fitnow)
: wxPanel(parent, wxID_ANY, wxPoint(-1000, -1000)), displayoffset(0), parent_win(parent), tppw_flags(0) {

	tpg=tpanelglobal::Get();

	// wxVisualAttributes va=wxRichTextCtrl::GetClassDefaultAttributes();
	// wxColour col=va.colBg;
	// if(!col.IsOk()) col=*wxWHITE;
	// SetOwnBackgroundColour(col);
	// SetBackgroundStyle(wxBG_STYLE_COLOUR);

	//tpw = new tpanelwin(this);
	//wxBoxSizer *vbox = new wxBoxSizer(wxHORIZONTAL);
	//vbox->Add(tpw, 1, wxALIGN_TOP | wxEXPAND, 0);
	//SetSizer(vbox);

	wxBoxSizer* outersizer = new wxBoxSizer(wxVERTICAL);
	headersizer = new wxBoxSizer(wxHORIZONTAL);
	scrollwin = new tpanelscrollwin(this);
	clabel=new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000));
	outersizer->Add(headersizer, 0, wxALL | wxEXPAND, 0);
	headersizer->Add(clabel, 0, wxALL, 2);
	headersizer->AddStretchSpacer();
	auto addbtn = [&](wxWindowID id, wxString name, std::string type, wxButton *& btnref) {
		btnref=new wxButton(this, id, name, wxPoint(-1000, -1000), wxDefaultSize, wxBU_EXACTFIT);
		btnref->Show(false);
		headersizer->Add(btnref, 0, wxALL, 2);
		showhidemap.insert(std::make_pair(type, btnref));
	};
	addbtn(TPPWID_MARKALLREADBTN, wxT("Mark All Read"), "unread", MarkReadBtn);
	addbtn(TPPWID_NEWESTUNREADBTN, wxT("Newest Unread \x2191"), "unread", NewestUnreadBtn);
	addbtn(TPPWID_OLDESTUNREADBTN, wxT("Oldest Unread \x2193"), "unread", OldestUnreadBtn);
	addbtn(TPPWID_UNHIGHLIGHTALLBTN, wxT("Unhighlight All"), "highlight", UnHighlightBtn);
	headersizer->Add(new wxButton(this, TPPWID_TOPBTN, wxT("Top \x2191"), wxPoint(-1000, -1000), wxDefaultSize, wxBU_EXACTFIT), 0, wxALL, 2);
	outersizer->Add(scrollwin, 1, wxALL | wxEXPAND, 2);
	outersizer->Add(new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000)), 0, wxALL, 2);

	sizer = new wxBoxSizer(wxVERTICAL);
	scrollwin->SetSizer(sizer);
	SetSizer(outersizer);
	scrollwin->SetScrollRate(1, 1);
	if(fitnow) {
		scrollwin->FitInside();
		FitInside();
	}
}

void panelparentwin_base::ShowHideButtons(std::string type, bool show) {
	auto iterpair = showhidemap.equal_range(type);
	for(auto it = iterpair.first; it != iterpair.second; ++it) it->second->Show(show);
}

void panelparentwin_base::PopTop() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::PopTop(): START"));
	#endif
	//currentdisp.front().second->Destroy();
	size_t offset=0;
	wxSizer *sz=sizer->GetItem(offset)->GetSizer();
	if(sz) {
		sz->Clear(true);
		sizer->Remove(offset);
	}
	currentdisp.pop_front();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::PopTop(): END"));
	#endif
}

void panelparentwin_base::PopBottom() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::PopBottom(): START"));
	#endif
	//currentdisp.back().second->Destroy();
	size_t offset=currentdisp.size()-1;
	wxSizer *sz=sizer->GetItem(offset)->GetSizer();
	if(sz) {
		sz->Clear(true);
		sizer->Remove(offset);
	}
	currentdisp.pop_back();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::PopBottom(): END"));
	#endif
}

void panelparentwin_base::pageupevthandler(wxCommandEvent &event) {
	PageUpHandler();
}
void panelparentwin_base::pagedownevthandler(wxCommandEvent &event) {
	PageDownHandler();
}
void panelparentwin_base::pagetopevthandler(wxCommandEvent &event) {
	PageTopHandler();
}

void panelparentwin_base::CheckClearNoUpdateFlag() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::CheckClearNoUpdateFlag(): START"));
	#endif
	if(tppw_flags&TPPWF_NOUPDATEONPUSH) {
		scrollwin->Freeze();
		scrollwin->FitInside();
		if(scrolltoid_onupdate) HandleScrollToIDOnUpdate();
		UpdateCLabel();
		scrollwin->Thaw();
		tppw_flags&=~(TPPWF_NOUPDATEONPUSH|TPPWF_CLABELUPDATEPENDING);
	}
	else if(tppw_flags&TPPWF_CLABELUPDATEPENDING) {
		UpdateCLabel();
		tppw_flags&=~TPPWF_CLABELUPDATEPENDING;
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::CheckClearNoUpdateFlag(): END"));
	#endif
}

void panelparentwin_base::StartScrollFreeze(tppw_scrollfreeze &s) {
	int scrollstart;
	scrollwin->GetViewStart(0, &scrollstart);
	if((!scrollstart && !displayoffset) || currentdisp.size() <= 2)  {
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
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::StartScrollFreeze(): Using id: %" wxLongLongFmtSpec "d, extrapixels: %d"), (*it).first, y);
			#endif
			return;
		}
	}
	s.scr=0;
	s.extrapixels=0;
	return;
}

void panelparentwin_base::EndScrollFreeze(tppw_scrollfreeze &s) {
	if(s.scr) {
		int y;
		s.scr->GetPosition(0, &y);
		int scrollstart;
		scrollwin->GetViewStart(0, &scrollstart);
		scrollstart+=y-s.extrapixels;
		scrollwin->Scroll(-1, std::max(0, scrollstart));
	}
}

void panelparentwin_base::SetScrollFreeze(tppw_scrollfreeze &s, dispscr_base *scr) {
	s.scr = scr;
	s.extrapixels = 0;
}

mainframe *panelparentwin_base::GetMainframe() {
	return GetMainframeAncestor(this);
}

bool panelparentwin_base::IsSingleAccountWin() const {
	return alist.size() <= 1;
}

BEGIN_EVENT_TABLE(tpanelparentwin_nt, panelparentwin_base)
EVT_BUTTON(TPPWID_MARKALLREADBTN, tpanelparentwin_nt::markallreadevthandler)
EVT_BUTTON(TPPWID_UNHIGHLIGHTALLBTN, tpanelparentwin_nt::markremoveallhighlightshandler)
EVT_BUTTON(TPPWID_NEWESTUNREADBTN, tpanelparentwin_nt::movetonewestunreadhandler)
EVT_BUTTON(TPPWID_OLDESTUNREADBTN, tpanelparentwin_nt::movetooldestunreadhandler)
END_EVENT_TABLE()

tpanelparentwin_nt::tpanelparentwin_nt(const std::shared_ptr<tpanel> &tp_, wxWindow *parent)
: panelparentwin_base(parent, false), tp(tp_) {
	LogMsgFormat(LFT_TPANEL, wxT("Creating tweet panel window %s"), wxstrstd(tp->name).c_str());

	tp->twin.push_front(this);
	tpanelparentwinlist.push_front(this);

	clabel->SetLabel(wxT("No Tweets"));
	scrollwin->FitInside();
	FitInside();
}

tpanelparentwin_nt::~tpanelparentwin_nt() {
	tp->OnTPanelWinClose(this);
	tpanelparentwinlist.remove(this);
}

void tpanelparentwin_nt::PushTweet(const std::shared_ptr<tweet> &t, unsigned int pushflags) {
	scrollwin->Freeze();
	LogMsgFormat(LFT_TPANEL, "tpanelparentwin_nt::PushTweet, id: %" wxLongLongFmtSpec "d, %d, 0x%X, %d", t->id, displayoffset, pushflags, (int) currentdisp.size());
	tppw_scrollfreeze sf;
	StartScrollFreeze(sf);
	uint64_t id=t->id;
	bool recalcdisplayoffset = false;
	if(displayoffset) {
		if(id>currentdisp.front().first) {
			if(!(pushflags&TPPWPF_ABOVE)) {
				if(!(pushflags&TPPWPF_NOINCDISPOFFSET)) displayoffset++;
				scrollwin->Thaw();
				return;
			}
			else if(pushflags&TPPWPF_NOINCDISPOFFSET) recalcdisplayoffset = true;
		}
	}
	if(currentdisp.size()==gc.maxtweetsdisplayinpanel) {
		if(t->id<currentdisp.back().first) {			//off the end of the list
			if(pushflags&TPPWPF_BELOW || pushflags&TPPWPF_USERTL) {
				PopTop();
				displayoffset++;
			}
			else {
				scrollwin->Thaw();
				return;
			}
		}
		else PopBottom();					//too many in list, remove the last one
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweet 1, %d, %d, %d"), displayoffset, (int) currentdisp.size(), recalcdisplayoffset);
	#endif
	if(pushflags&TPPWPF_SETNOUPDATEFLAG) tppw_flags|=TPPWF_NOUPDATEONPUSH;
	size_t index=0;
	auto it=currentdisp.begin();
	for(; it!=currentdisp.end(); it++, index++) {
		if(it->first<id) break;	//insert before this iterator
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweet 2, %d, %d, %d, %d"), displayoffset, currentdisp.size(), index, recalcdisplayoffset);
	#endif
	if(recalcdisplayoffset || currentdisp.empty()) {
		tweetidset::const_iterator stit = tp->tweetlist.find(id);
		if(stit != tp->tweetlist.end()) displayoffset = std::distance(tp->tweetlist.cbegin(), stit);
		else displayoffset = 0;
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweet 3, %d, %d, %d, %d"), displayoffset, currentdisp.size(), index, recalcdisplayoffset);
	#endif
	tweetdispscr *td = PushTweetIndex(t, index);
	currentdisp.insert(it, std::make_pair(id, td));
	if(pushflags&TPPWPF_CHECKSCROLLTOID && scrolltoid == id) {
		if(tppw_flags&TPPWF_NOUPDATEONPUSH) scrolltoid_onupdate = scrolltoid;
		else SetScrollFreeze(sf, td);
		scrolltoid = 0;
	}
	if(!(tppw_flags&TPPWF_NOUPDATEONPUSH)) UpdateCLabel();
	EndScrollFreeze(sf);
	scrollwin->Thaw();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweet END, %d, %d"), displayoffset, currentdisp.size());
	#endif
}

tweetdispscr *tpanelparentwin_nt::PushTweetIndex(const std::shared_ptr<tweet> &t, size_t index) {
	LogMsgFormat(LFT_TPANEL, "tpanelparentwin_nt::PushTweetIndex, id: %" wxLongLongFmtSpec "d, %d", t->id, index);
	wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);

	tweetdispscr *td=new tweetdispscr(t, scrollwin, this, vbox);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweetIndex 1"));
	#endif

	if(t->flags.Get('T')) {
		if(t->rtsrc && gc.rtdisp) {
			td->bm = new profimg_staticbitmap(scrollwin, t->rtsrc->user->cached_profile_img, t->rtsrc->user->id, t->id, GetMainframe());
		}
		else {
			td->bm = new profimg_staticbitmap(scrollwin, t->user->cached_profile_img, t->user->id, t->id, GetMainframe());
		}
		hbox->Add(td->bm, 0, wxALL, 2);
	}
	else if(t->flags.Get('D') && t->user_recipient) {
			t->user->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
			t->user_recipient->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
			td->bm = new profimg_staticbitmap(scrollwin, t->user->cached_profile_img_half, t->user->id, t->id, GetMainframe(), PISBF_HALF);
			td->bm2 = new profimg_staticbitmap(scrollwin, t->user_recipient->cached_profile_img_half, t->user_recipient->id, t->id, GetMainframe(), PISBF_HALF);
			int dim=gc.maxpanelprofimgsize/2;
			if(tpg->arrow_dim!=dim) {
				tpg->arrow=GetArrowIconDim(dim);
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

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweetIndex 2"));
	#endif

	vbox->Add(td, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);
	hbox->Add(vbox, 1, wxEXPAND, 0);

	sizer->Insert(index, hbox, 0, wxALL | wxEXPAND, 1);
	td->DisplayTweet();

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweetIndex 3"));
	#endif

	if(t->in_reply_to_status_id) {
		std::shared_ptr<tweet> subt=ad.GetTweetById(t->in_reply_to_status_id);

		if(t->IsArrivedHereAnyPerspective()) {	//save
			subt->lflags |= TLF_SHOULDSAVEINDB;
		}

		std::shared_ptr<taccount> pacc;
		t->GetUsableAccount(pacc, GUAF_NOERR) || t->GetUsableAccount(pacc, GUAF_NOERR|GUAF_USERENABLED);
		subt->pending_ops.emplace_front(new tpanel_subtweet_pending_op(vbox, this, td));
		if(CheckFetchPendingSingleTweet(subt, pacc)) UnmarkPendingTweet(subt, 0);
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweetIndex END"));
	#endif

	return td;
}

void tpanelparentwin_nt::PageUpHandler() {
	if(displayoffset) {
		tppw_flags|=TPPWF_NOUPDATEONPUSH;
		size_t pagemove=std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, displayoffset);
		LoadMore(pagemove, 0, 0, TPPWPF_ABOVE | TPPWPF_NOINCDISPOFFSET);
		CheckClearNoUpdateFlag();
	}
	scrollwin->page_scroll_blocked=false;
}
void tpanelparentwin_nt::PageDownHandler() {
	tppw_flags|=TPPWF_NOUPDATEONPUSH;
	size_t curnum=currentdisp.size();
	size_t tweetnum=tp->tweetlist.size();
	if(curnum+displayoffset<tweetnum || tppw_flags&TPPWF_CANALWAYSSCROLLDOWN) {
		size_t pagemove;
		if(tppw_flags&TPPWF_CANALWAYSSCROLLDOWN) pagemove=(gc.maxtweetsdisplayinpanel+1)/2;
		else pagemove=std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, tweetnum-(curnum+displayoffset));
		uint64_t lessthanid=currentdisp.back().first;
		LoadMore(pagemove, lessthanid, 0, TPPWPF_BELOW | TPPWPF_NOINCDISPOFFSET);
	}
	scrollwin->page_scroll_blocked=false;
	CheckClearNoUpdateFlag();
}

void tpanelparentwin_nt::PageTopHandler() {
	if(displayoffset) {
		tppw_flags|=TPPWF_NOUPDATEONPUSH;
		size_t pushcount=std::min(displayoffset, (size_t) gc.maxtweetsdisplayinpanel);
		displayoffset=0;
		LoadMore(pushcount, 0, 0, TPPWPF_ABOVE | TPPWPF_NOINCDISPOFFSET);
		CheckClearNoUpdateFlag();
	}
	scrollwin->Scroll(-1, 0);
}

void tpanelparentwin_nt::JumpToTweetID(uint64_t id) {
	LogMsgFormat(LFT_TPANEL, "tpanel::JumpToTweetID %" wxLongLongFmtSpec "d, displayoffset: %d, display count: %d, tweets: %d", id, displayoffset, (int) currentdisp.size(), (int) tp->tweetlist.size());

	tweetidset::const_iterator stit = tp->tweetlist.find(id);
	if(stit == tp->tweetlist.end()) return;

	tppw_flags|=TPPWF_NOUPDATEONPUSH;
	scrollwin->Freeze();

	unsigned int targ_offset = std::distance(tp->tweetlist.cbegin(), stit);

	unsigned int offset_up_delta = std::min<unsigned int>(targ_offset, (gc.maxtweetsdisplayinpanel+1)/2);
	unsigned int offset_down_delta = std::min<unsigned int>(tp->tweetlist.size() - targ_offset, gc.maxtweetsdisplayinpanel - offset_up_delta) - 1;

	uint64_t top_id = *std::prev(stit, offset_up_delta);
	uint64_t bottom_id = *std::next(stit, offset_down_delta);

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanel::JumpToTweetID targ_offset: %d, oud: %d, odd: %d, ti: %" wxLongLongFmtSpec "d, bi: %" wxLongLongFmtSpec "d"), targ_offset, offset_up_delta, offset_down_delta, top_id, bottom_id);
	#endif

	while(!currentdisp.empty() && currentdisp.front().first > top_id) {
		PopTop();
		displayoffset++;
	}
	while(!currentdisp.empty() && currentdisp.back().first < bottom_id) PopBottom();

	if(currentdisp.empty()) displayoffset = 0;

	unsigned int loadcount = offset_up_delta + offset_down_delta + 1 - currentdisp.size();

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanel::JumpToTweetID displayoffset: %d, loadcount: %d, display count: %d"), displayoffset, loadcount, (int) currentdisp.size());
	#endif

	for(auto &disp : currentdisp) {
		if(disp.first == id) {
			tppw_scrollfreeze sf;
			SetScrollFreeze(sf, disp.second);
			EndScrollFreeze(sf);
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanel::JumpToTweetID setting scrollfreeze"));
			#endif
			break;
		}
	}

	if(loadcount) {
		scrolltoid = id;
		if(!currentdisp.empty() && currentdisp.back().first < top_id) {
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanel::JumpToTweetID adjusting load bound: %" wxLongLongFmtSpec "d"), currentdisp.back().first);
			#endif
			LoadMore(loadcount, currentdisp.back().first, 0, TPPWPF_ABOVE | TPPWPF_CHECKSCROLLTOID | TPPWPF_NOINCDISPOFFSET);
		}
		else LoadMore(loadcount, top_id + 1, 0, TPPWPF_ABOVE | TPPWPF_CHECKSCROLLTOID | TPPWPF_NOINCDISPOFFSET);
	}
	else CheckClearNoUpdateFlag();
	scrollwin->Thaw();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanel::JumpToTweetID END"));
	#endif
}

void tpanelparentwin_nt::UpdateCLabel() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::UpdateCLabel START"));
	#endif
	size_t curnum=currentdisp.size();
	if(curnum) {
		wxString msg=wxString::Format(wxT("%d - %d of %d"), displayoffset+1, displayoffset+curnum, tp->tweetlist.size());
		if(tp->cids.unreadids.size()) {
			msg.append(wxString::Format(wxT(", %d unread"), tp->cids.unreadids.size()));
		}
		if(tp->cids.highlightids.size()) {
			msg.append(wxString::Format(wxT(", %d highlighted"), tp->cids.highlightids.size()));
		}
		clabel->SetLabel(msg);
	}
	else clabel->SetLabel(wxT("No Tweets"));
	ShowHideButtons("unread", !tp->cids.unreadids.empty());
	ShowHideButtons("highlight", !tp->cids.highlightids.empty());
	headersizer->Layout();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::UpdateCLabel END"));
	#endif
}

void tpanelparentwin_nt::markallreadevthandler(wxCommandEvent &event) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::markallreadevthandler START"));
	#endif
	tweetidset cached_ids=tp->cids.unreadids;
	dbupdatetweetsetflagsmsg *msg=new dbupdatetweetsetflagsmsg(std::move(cached_ids), tweet_flags::GetFlagValue('r'), tweet_flags::GetFlagValue('u'));
	dbc.SendMessage(msg);
	MarkClearCIDSSetHandler(
		[&](cached_id_sets &cids) -> tweetidset & {
			return cids.unreadids;
		},
		[&](const std::shared_ptr<tweet> &tw) {
			tw->UpdateMarkedAsRead(tp.get());
		}
	);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::markallreadevthandler END"));
	#endif
}

void tpanelparentwin_nt::markremoveallhighlightshandler(wxCommandEvent &event) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::markremoveallhighlightshandler START"));
	#endif
	tweetidset cached_ids=tp->cids.highlightids;
	dbupdatetweetsetflagsmsg *msg=new dbupdatetweetsetflagsmsg(std::move(cached_ids), 0, tweet_flags::GetFlagValue('H'));
	dbc.SendMessage(msg);
	MarkClearCIDSSetHandler(
		[&](cached_id_sets &cids) -> tweetidset & {
			return cids.highlightids;
		},
		[&](const std::shared_ptr<tweet> &tw) {
			tw->flags.Set('H', false);
			UpdateTweet(*tw, false);
		}
	);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::markremoveallhighlightshandler END"));
	#endif
}

void tpanelparentwin_nt::movetonewestunreadhandler(wxCommandEvent &event) {
	if(!tp->cids.unreadids.empty()) JumpToTweetID(*(tp->cids.unreadids.begin()));
}

void tpanelparentwin_nt::movetooldestunreadhandler(wxCommandEvent &event) {
	if(!tp->cids.unreadids.empty()) JumpToTweetID(*(tp->cids.unreadids.rbegin()));
}

void tpanelparentwin_nt::MarkClearCIDSSetHandler(std::function<tweetidset &(cached_id_sets &)> idsetselector, std::function<void(const std::shared_ptr<tweet> &)> existingtweetfunc) {
	Freeze();
	tp->TPPWFlagMaskAllTWins(TPPWF_CLABELUPDATEPENDING|TPPWF_NOUPDATEONPUSH, 0);
	tweetidset &set = idsetselector(tp->cids);
	MarkTweetIDSetCIDS(set, tp.get(), idsetselector, true, existingtweetfunc);
	set.clear();
	CheckClearNoUpdateFlag_All();
	Thaw();
}

void tpanelparentwin_nt::EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush) {
	Freeze();
	bool checkupdateflag=false;
	if(setnoupdateonpush) {
		checkupdateflag=!(tppw_flags&TPPWF_NOUPDATEONPUSH);
		tppw_flags|=TPPWF_NOUPDATEONPUSH;
	}
	for(auto jt=currentdisp.begin(); jt!=currentdisp.end(); ++jt) {
		tweetdispscr *tds=(tweetdispscr *) jt->second;
		bool continueflag=func(tds);
		for(auto kt=tds->subtweets.begin(); kt!=tds->subtweets.end(); ++kt) {
			if(kt->get()) {
				func(kt->get());
			}
		}
		if(!continueflag) break;
	}
	Thaw();
	if(checkupdateflag) CheckClearNoUpdateFlag();
}

void tpanelparentwin_nt::UpdateOwnTweet(const tweet &t, bool redrawimg) {
	EnumDisplayedTweets([&](tweetdispscr *tds) {
		if(tds->td->id==t.id || tds->rtid==t.id) {	//found matching entry
			LogMsgFormat(LFT_TPANEL, wxT("UpdateOwnTweet: Found Entry %" wxLongLongFmtSpec "d."), t.id);
			tds->DisplayTweet(redrawimg);
			return false;
		}
		else return true;
	}, true);
}

void tpanelparentwin_nt::HandleScrollToIDOnUpdate() {
	for(auto &it : currentdisp) {
		if(it.first == scrolltoid_onupdate) {
			tppw_scrollfreeze sf;
			SetScrollFreeze(sf, it.second);
			EndScrollFreeze(sf);
			break;
		}

	}
	scrolltoid_onupdate = 0;
}

BEGIN_EVENT_TABLE(tpanelparentwin, tpanelparentwin_nt)
	EVT_MENU(TPPWID_DETACH, tpanelparentwin::tabdetachhandler)
	EVT_MENU(TPPWID_SPLIT, tpanelparentwin::tabsplitcmdhandler)
	EVT_MENU(TPPWID_DUP, tpanelparentwin::tabduphandler)
	EVT_MENU(TPPWID_DETACHDUP, tpanelparentwin::tabdetachedduphandler)
	EVT_MENU(TPPWID_CLOSE, tpanelparentwin::tabclosehandler)
END_EVENT_TABLE()

tpanelparentwin::tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select)
	: tpanelparentwin_nt(tp_, parent), owner(parent) {
	parent->auib->AddPage(this, wxstrstd(tp->dispname), select);
	LoadMore(gc.maxtweetsdisplayinpanel);
}

uint64_t tpanelparentwin::PushTweetOrRetLoadId(uint64_t id, unsigned int pushflags) {
	bool isnew;
	std::shared_ptr<tweet> tobj=ad.GetTweetById(id, &isnew);
	if(!isnew) {
		return PushTweetOrRetLoadId(tobj, pushflags);
	}
	else {
		tobj->lflags|=TLF_BEINGLOADEDFROMDB;
		MarkPending_TPanelMap(tobj, this, pushflags);
		return id;
	}
}

uint64_t tpanelparentwin::PushTweetOrRetLoadId(const std::shared_ptr<tweet> &tobj, unsigned int pushflags) {
	bool insertpending=false;
	if(tobj->lflags&TLF_BEINGLOADEDFROMDB) {
		insertpending=true;
	}
	else {
		if(CheckMarkPending_GetAcc(tobj, true)) PushTweet(tobj, pushflags);
		else insertpending=true;
	}
	if(insertpending) {
		MarkPending_TPanelMap(tobj, this, pushflags);
	}
	return 0;
}

//if lessthanid is non-zero, is an exclusive upper id limit, iterate downwards
//if greaterthanid, is an exclusive lower limit, iterate upwards
//cannot set both
//if neither set: start at highest in set and iterate down
void tpanelparentwin::LoadMore(unsigned int n, uint64_t lessthanid, uint64_t greaterthanid, unsigned int pushflags) {
	dbseltweetmsg *loadmsg=0;

	LogMsgFormat(LFT_TPANEL, "tpanelparentwin::LoadMore called with n: %d, lessthanid: %" wxLongLongFmtSpec "d, greaterthanid: %" wxLongLongFmtSpec "d, pushflags: 0x%X", n, lessthanid, greaterthanid, pushflags);

	Freeze();
	tppw_flags|=TPPWF_NOUPDATEONPUSH;

	tweetidset::const_iterator stit;
	bool revdir = false;
	if(lessthanid) stit=tp->tweetlist.upper_bound(lessthanid);	//finds the first id *less than* lessthanid
	else if(greaterthanid) {
		stit=tp->tweetlist.lower_bound(greaterthanid);		//finds the first id *greater than or equal to* greaterthanid
		if(*stit == greaterthanid) --stit;
		revdir = true;
	}
	else stit=tp->tweetlist.cbegin();

	for(unsigned int i=0; i<n; i++) {
		if(stit==tp->tweetlist.cend()) break;
		uint64_t loadid=PushTweetOrRetLoadId(*stit, pushflags);
		if(loadid) {
			LogMsgFormat(LFT_TPANEL, "tpanel::LoadMore loading from db id: %" wxLongLongFmtSpec "d", loadid);
			if(!loadmsg) loadmsg=new dbseltweetmsg;
			loadmsg->id_set.insert(loadid);
		}
		if(revdir) {
			if(stit == tp->tweetlist.cbegin()) break;
			--stit;
		}
		else ++stit;
	}
	if(loadmsg) {
		loadmsg->targ=&dbc;
		loadmsg->cmdevtype=wxextDBCONN_NOTIFY;
		loadmsg->winid=wxDBCONNEVT_ID_TPANELTWEETLOAD;
		if(!gc.persistentmediacache) loadmsg->flags|=DBSTMF_PULLMEDIA;
		dbc.SendMessage(loadmsg);
	}
	if(currentlogflags&LFT_PENDTRACE) dump_tweet_pendings(LFT_PENDTRACE, wxT(""), wxT("\t"));

	Thaw();
	CheckClearNoUpdateFlag();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin::LoadMore END"));
	#endif
}

void tpanelparentwin::tabdetachhandler(wxCommandEvent &event) {
	mainframe *top = new mainframe( appversionname, wxDefaultPosition, wxDefaultSize );
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
	mainframe *top = new mainframe( appversionname, wxDefaultPosition, wxDefaultSize );
	tp->MkTPanelWin(top);
	top->Show(true);
}
void tpanelparentwin::tabclosehandler(wxCommandEvent &event) {
	owner->auib->RemovePage(owner->auib->GetPageIndex(this));
	owner->auib->tabnumcheck();
	Close();
}
void tpanelparentwin::tabsplitcmdhandler(wxCommandEvent &event) {
	size_t pagecount=owner->auib->GetPageCount();
	wxPoint curpos=GetPosition();
	unsigned int tally=0;
	for(size_t i=0; i<pagecount; ++i) {
		if(owner->auib->GetPage(i)->GetPosition()==curpos) tally++;
	}
	if(tally<2) return;
	owner->auib->Split(owner->auib->GetPageIndex(this), wxRIGHT);
}

void tpanelparentwin::UpdateCLabel() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin::UpdateCLabel START"));
	#endif
	tpanelparentwin_nt::UpdateCLabel();
	int pageid = owner->auib->GetPageIndex(this);
	int unreadcount = tp->cids.unreadids.size();
	if(!unreadcount) {
		owner->auib->SetPageText(pageid, wxstrstd(tp->dispname));
		if((tpw_flags & TPWF_UNREADBITMAPDISP)) {
			owner->auib->SetPageBitmap(pageid, wxNullBitmap);
			tpw_flags &= ~TPWF_UNREADBITMAPDISP;
		}
	}
	else {
		owner->auib->SetPageText(pageid, wxString::Format(wxT("%d - %s"), unreadcount, wxstrstd(tp->dispname).c_str()));
		if(!(tpw_flags & TPWF_UNREADBITMAPDISP)) {
			owner->auib->SetPageBitmap(pageid, tpg->multiunreadicon);
			tpw_flags |= TPWF_UNREADBITMAPDISP;
		}
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin::UpdateCLabel END"));
	#endif
}

BEGIN_EVENT_TABLE(tpanelparentwin_user, panelparentwin_base)
END_EVENT_TABLE()

std::multimap<uint64_t, tpanelparentwin_user*> tpanelparentwin_user::pendingmap;

tpanelparentwin_user::tpanelparentwin_user(wxWindow *parent)
	: panelparentwin_base(parent) { }

tpanelparentwin_user::~tpanelparentwin_user() {
	for(auto it=pendingmap.begin(); it!=pendingmap.end(); ) {
		if((*it).second==this) {
			auto todel=it;
			it++;
			pendingmap.erase(todel);
		}
		else it++;
	}
}

void tpanelparentwin_user::PageUpHandler() {
	if(displayoffset) {
		tppw_flags|=TPPWF_NOUPDATEONPUSH;
		size_t pagemove=std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, displayoffset);
		size_t curnum=currentdisp.size();
		size_t bottomdrop=std::min(curnum, (size_t) (curnum+pagemove-gc.maxtweetsdisplayinpanel));
		for(size_t i=0; i<bottomdrop; i++) PopBottom();
		auto it=userlist.begin()+displayoffset;
		for(unsigned int i=0; i<pagemove; i++) {
			it--;
			displayoffset--;
			const std::shared_ptr<userdatacontainer> &u=*it;
			if(u->IsReady(UPDCF_DOWNLOADIMG)) UpdateUser(u, displayoffset);
		}
		CheckClearNoUpdateFlag();
	}
	scrollwin->page_scroll_blocked=false;
}
void tpanelparentwin_user::PageDownHandler() {
	tppw_flags|=TPPWF_NOUPDATEONPUSH;
	size_t curnum=currentdisp.size();
	size_t num=ItemCount();
	if(curnum+displayoffset<num || tppw_flags&TPPWF_CANALWAYSSCROLLDOWN) {
		size_t pagemove;
		if(tppw_flags&TPPWF_CANALWAYSSCROLLDOWN) pagemove=(gc.maxtweetsdisplayinpanel+1)/2;
		else pagemove=std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, (size_t) (num-(curnum+displayoffset)));
		size_t topdrop=std::min(curnum, (size_t) (curnum+pagemove-gc.maxtweetsdisplayinpanel));
		for(size_t i=0; i<topdrop; i++) PopTop();
		displayoffset+=topdrop;
		LoadMoreToBack(pagemove);
	}
	scrollwin->page_scroll_blocked=false;
	CheckClearNoUpdateFlag();
}

void tpanelparentwin_user::PageTopHandler() {
	if(displayoffset) {
		tppw_flags|=TPPWF_NOUPDATEONPUSH;
		size_t pushcount=std::min(displayoffset, (size_t) gc.maxtweetsdisplayinpanel);
		ssize_t bottomdrop=((ssize_t) pushcount+currentdisp.size())-gc.maxtweetsdisplayinpanel;
		if(bottomdrop>0) {
			for(ssize_t i=0; i<bottomdrop; i++) PopBottom();
		}
		displayoffset=0;
		size_t i=0;
		for(auto it=userlist.begin(); it!=userlist.end() && pushcount; ++it, --pushcount, i++) {
			const std::shared_ptr<userdatacontainer> &u=*it;
			if(u->IsReady(UPDCF_DOWNLOADIMG)) UpdateUser(u, i);
		}
		CheckClearNoUpdateFlag();
	}
	scrollwin->Scroll(-1, 0);
}

bool tpanelparentwin_user::PushBackUser(const std::shared_ptr<userdatacontainer> &u) {
	bool havealready=false;
	size_t offset;
	for(auto it=userlist.begin(); it!=userlist.end(); ++it) {
		if((*it).get()==u.get()) {
			havealready=true;
			offset=std::distance(userlist.begin(), it);
			break;
		}
	}
	if(!havealready) {
		userlist.push_back(u);
		offset=userlist.size()-1;
	}
	return UpdateUser(u, offset);
}

//returns true if marked pending
bool tpanelparentwin_user::UpdateUser(const std::shared_ptr<userdatacontainer> &u, size_t offset) {
	size_t index=0;
	auto jt=userlist.begin();
	size_t i=0;
	for(auto it=currentdisp.begin(); it!=currentdisp.end(); ++it, i++) {
		for(;jt!=userlist.end(); ++jt) {
			if(it->first==(*jt)->id) {
				if(it->first==u->id) {
					((userdispscr *) it->second)->Display();
					return false;
				}
				else if(offset> (size_t) std::distance(userlist.begin(), jt)) {
					index=i+1;
				}
				break;
			}
		}
	}
	auto pos=currentdisp.begin();
	std::advance(pos, index);
	if(u->IsReady(UPDCF_DOWNLOADIMG|UPDCF_USEREXPIRE)) {
		wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
		userdispscr *td=new userdispscr(u, scrollwin, this, hbox);

		td->bm = new profimg_staticbitmap(scrollwin, u->cached_profile_img, u->id, 0, GetMainframe());
		hbox->Add(td->bm, 0, wxALL, 2);

		hbox->Add(td, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);

		sizer->Insert(index, hbox, 0, wxALL | wxEXPAND, 1);
		currentdisp.insert(pos, std::make_pair(u->id, td));
		td->Display();
		if(!(tppw_flags&TPPWF_NOUPDATEONPUSH)) UpdateCLabel();
		return false;
	}
	else {
		auto pit=pendingmap.equal_range(u->id);
		for(auto it=pit.first; it!=pit.second; ) {
			if((*it).second==this) {
				return true;
			}
		}
		pendingmap.insert(std::make_pair(u->id, this));
		return true;
	}
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

tpanelscrollwin::tpanelscrollwin(panelparentwin_base *parent_)
	: wxScrolledWindow(parent_, wxID_ANY, wxPoint(-1000, -1000)), parent(parent_), resize_update_pending(false), page_scroll_blocked(false) {

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
		parent->GetEventHandler()->AddPendingEvent(evt);
		page_scroll_blocked=true;
	}
	if(!scrollup && scrolldown && !page_scroll_blocked) {
		wxCommandEvent evt(wxextTP_PAGEDOWN_EVENT);
		parent->GetEventHandler()->AddPendingEvent(evt);
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

BEGIN_EVENT_TABLE(tpanelparentwin_usertweets, tpanelparentwin_nt)
END_EVENT_TABLE()

std::map<std::pair<uint64_t, RBFS_TYPE>, std::shared_ptr<tpanel> > tpanelparentwin_usertweets::usertpanelmap;

tpanelparentwin_usertweets::tpanelparentwin_usertweets(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc_, RBFS_TYPE type_)
	: tpanelparentwin_nt(MkUserTweetTPanel(user_, type_), parent), user(user_), getacc(getacc_), havestarted(false), type(type_) {
	tppw_flags|=TPPWF_CANALWAYSSCROLLDOWN;
}

tpanelparentwin_usertweets::~tpanelparentwin_usertweets() {
	usertpanelmap.erase(std::make_pair(user->id, type));
}

std::shared_ptr<tpanel> tpanelparentwin_usertweets::MkUserTweetTPanel(const std::shared_ptr<userdatacontainer> &user, RBFS_TYPE type_) {
	std::shared_ptr<tpanel> &tp=usertpanelmap[std::make_pair(user->id, type_)];
	if(!tp) {
		tp=tpanel::MkTPanel("___UTL_" + std::to_string(user->id) + "_" + std::to_string((size_t) type_), "User Timeline: @" + user->GetUser().screen_name, TPF_DELETEONWINCLOSE|TPF_USER_TIMELINE);
	}
	return tp;
}

std::shared_ptr<tpanel> tpanelparentwin_usertweets::GetUserTweetTPanel(uint64_t userid, RBFS_TYPE type_) {
	auto it=usertpanelmap.find(std::make_pair(userid, type_));
	if(it!=usertpanelmap.end()) {
		return it->second;
	}
	else return std::shared_ptr<tpanel>();
}

void tpanelparentwin_usertweets::LoadMore(unsigned int n, uint64_t lessthanid, unsigned int pushflags) {
	std::shared_ptr<taccount> tac = getacc(*this);
	if(!tac) return;
	Freeze();
	tppw_flags|=TPPWF_NOUPDATEONPUSH;

	tweetidset::const_iterator stit;
	if(lessthanid) stit=tp->tweetlist.upper_bound(lessthanid);	//finds the first id *less than* lessthanid
	else stit=tp->tweetlist.cbegin();

	unsigned int numleft=n;
	uint64_t lower_bound=lessthanid;
	while(numleft) {
		if(stit==tp->tweetlist.cend()) break;

		std::shared_ptr<tweet> t=ad.GetTweetById(*stit);
		if(tac->CheckMarkPending(t)) PushTweet(t, TPPWPF_USERTL);
		else MarkPending_TPanelMap(t, 0, TPPWPF_USERTL, &tp);

		if((*stit)<lower_bound) lower_bound=*stit;
		++stit;
		numleft--;
	}
	if(numleft) {
		uint64_t lower_id=0;
		uint64_t upper_id=0;
		if(lessthanid) {
			upper_id=lower_bound-1;
		}
		else {
			if(tp->tweetlist.begin()!=tp->tweetlist.end()) lower_id=*(tp->tweetlist.begin());
		}
		tac->SetGetTwitCurlExtHook([&](twitcurlext *tce) { tce->mp = this; });
		tac->StartRestGetTweetBackfill(lower_id /*lower limit, exclusive*/, upper_id /*upper limit, inclusive*/, numleft, type, user->id);
		tac->ClearGetTwitCurlExtHook();
	}

	Thaw();
	CheckClearNoUpdateFlag();
}

void tpanelparentwin_usertweets::UpdateCLabel() {
	size_t curnum=currentdisp.size();
	size_t varmax=0;
	wxString emptymsg=wxT("No Tweets");
	switch(type) {
		case RBFS_USER_TIMELINE: varmax=user->GetUser().statuses_count; break;
		case RBFS_USER_FAVS: varmax=user->GetUser().favourites_count; emptymsg=wxT("No Favourites"); break;
		default: break;
	}
	size_t curtotal=std::max(tp->tweetlist.size(), varmax);
	if(curnum) clabel->SetLabel(wxString::Format(wxT("%d - %d of %d"), displayoffset+1, displayoffset+curnum, curtotal));
	else clabel->SetLabel(emptymsg);
}

void tpanelparentwin_usertweets::NotifyRequestFailed() {
	failed = true;
	havestarted = false;
	clabel->SetLabel(wxT("Lookup Failed"));
}

BEGIN_EVENT_TABLE(tpanelparentwin_userproplisting, tpanelparentwin_user)
END_EVENT_TABLE()

tpanelparentwin_userproplisting::tpanelparentwin_userproplisting(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc_, CS_ENUMTYPE type_)
	: tpanelparentwin_user(parent), user(user_), getacc(getacc_), havestarted(false), type(type_) {

}

tpanelparentwin_userproplisting::~tpanelparentwin_userproplisting() {
}

void tpanelparentwin_userproplisting::Init() {
	std::shared_ptr<taccount> tac = getacc(*this);
	if(tac) {
		twitcurlext *twit=tac->GetTwitCurlExt();
		twit->connmode=type;
		twit->extra_id=user->id;
		twit->mp=this;
		twit->QueueAsyncExec();
	}
}

void tpanelparentwin_userproplisting::UpdateCLabel() {
	size_t curnum=currentdisp.size();
	size_t varmax=0;
	wxString emptymsg;
	switch(type) {
		case CS_USERFOLLOWING: varmax=user->GetUser().friends_count; emptymsg=wxT("No Friends"); break;
		case CS_USERFOLLOWERS: varmax=user->GetUser().followers_count; emptymsg=wxT("No Followers"); break;
		default: break;
	}
	size_t curtotal=std::max(useridlist.size(), varmax);
	if(curnum) clabel->SetLabel(wxString::Format(wxT("%d - %d of %d"), displayoffset+1, displayoffset+curnum, curtotal));
	else clabel->SetLabel(emptymsg);
}

void tpanelparentwin_userproplisting::LoadMoreToBack(unsigned int n) {
	std::shared_ptr<taccount> tac = getacc(*this);
	if(!tac) return;
	failed = false;
	Freeze();
	tppw_flags|=TPPWF_NOUPDATEONPUSH;

	bool querypendings=false;
	size_t index=userlist.size();
	for(size_t i=0; i<n && index<useridlist.size(); i++, index++) {
		std::shared_ptr<userdatacontainer> u=ad.GetUserContainerById(useridlist[index]);
		if(PushBackUser(u)) {
			u->udc_flags|=UDC_CHECK_USERLISTWIN;
			tac->pendingusers[u->id]=u;
			querypendings=true;
		}
	}
	if(querypendings) {
		tac->StartRestQueryPendings();
	}

	Thaw();
	CheckClearNoUpdateFlag();
}

void tpanelparentwin_userproplisting::NotifyRequestFailed() {
	failed = true;
	havestarted = false;
	clabel->SetLabel(wxT("Lookup Failed"));
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

void tpanelreltimeupdater::Notify() {
	time_t nowtime=time(0);

	auto updatetimes = [&](tweetdispscr &td) {
		if(!td.updatetime) return;
		else if(nowtime>=td.updatetime) {
			td.Delete(wxRichTextRange(td.reltimestart, td.reltimeend));
			td.SetInsertionPoint(td.reltimestart);
			td.WriteText(getreltimestr(td.td->createtime, td.updatetime));
			td.reltimeend=td.GetInsertionPoint();
		}
		else return;
	};

	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		for(auto jt=(*it)->currentdisp.begin(); jt!=(*it)->currentdisp.end(); ++jt) {
			tweetdispscr &td=(tweetdispscr &) *((*jt).second);
			updatetimes(td);
			for(auto &kt : td.subtweets) {
				tweetdispscr *subt = kt.get();
				if(subt) updatetimes(*subt);
			}
		}
	}
}

BEGIN_EVENT_TABLE(profimg_staticbitmap, wxStaticBitmap)
	EVT_LEFT_DOWN(profimg_staticbitmap::ClickHandler)
	EVT_RIGHT_UP(profimg_staticbitmap::RightClickHandler)
	EVT_MENU_RANGE(tweetactmenustartid, tweetactmenuendid, profimg_staticbitmap::OnTweetActMenuCmd)
END_EVENT_TABLE()

void profimg_staticbitmap::ClickHandler(wxMouseEvent &event) {
	std::shared_ptr<taccount> acc_hint;
	ad.GetTweetById(tweetid)->GetUsableAccount(acc_hint);
	user_window::MkWin(userid, acc_hint);
}

void profimg_staticbitmap::RightClickHandler(wxMouseEvent &event) {
	if(owner || !(pisb_flags&PISBF_DONTUSEDEFAULTMF)) {
		wxMenu menu;
		int nextid=tweetactmenustartid;
		tamd.clear();
		AppendUserMenuItems(menu, tamd, nextid, ad.GetUserContainerById(userid), ad.GetTweetById(tweetid));
		PopupMenu(&menu);
	}
}

void profimg_staticbitmap::OnTweetActMenuCmd(wxCommandEvent &event) {
	mainframe *mf = owner;
	if(!mf && mainframelist.size() && !(pisb_flags&PISBF_DONTUSEDEFAULTMF)) mf = mainframelist.front();
	TweetActMenuAction(tamd, event.GetId(), mf);
}

tpanelglobal::tpanelglobal() : arrow_dim(0) {
	// int targheight=0;
	// wxVisualAttributes va=wxRichTextCtrl::GetClassDefaultAttributes();
	// if(va.font.IsOk()) {
		// wxSize res=wxScreenDC().GetPPI();
		// targheight=2+((((double) va.font.GetPointSize())/72.0) * ((double) res.GetHeight()));
	// }
	// targheight=std::max(targheight,16);
	GetInfoIcon(&infoicon, &infoicon_img);
	GetReplyIcon(&replyicon, &replyicon_img);
	GetFavIcon(&favicon, &favicon_img);
	GetFavOnIcon(&favonicon, &favonicon_img);
	GetRetweetIcon(&retweeticon, &retweeticon_img);
	GetRetweetOnIcon(&retweetonicon, &retweetonicon_img);
	GetDMreplyIcon(&dmreplyicon, &dmreplyicon_img);
	GetLockIcon(&proticon, &proticon_img);
	GetVerifiedIcon(&verifiedicon, &verifiedicon_img);
	GetCloseIcon(&closeicon, 0);
	GetMultiUnreadIcon(&multiunreadicon, 0);
}
