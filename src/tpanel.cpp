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
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/dcclient.h>
#include <wx/dcscreen.h>
#include <wx/clipbrd.h>

std::shared_ptr<tpanelglobal> tpanelglobal::Get() {
	if(tpg_glob.expired()) {
		std::shared_ptr<tpanelglobal> tmp=std::make_shared<tpanelglobal>();
		tpg_glob=tmp;
		return tmp;
	}
	else return tpg_glob.lock();
}

std::weak_ptr<tpanelglobal> tpanelglobal::tpg_glob;
tweetactmenudata tamd;

static media_id_type ParseMediaID(wxString url) {
	unsigned int i=1;
	media_id_type media_id;
	for(; i<url.Len(); i++) {
		if(url[i]>='0' && url[i]<='9') {
			media_id.m_id*=10;
			media_id.m_id+=url[i]-'0';
		}
		else break;
	}
	if(url[i]!='_') return media_id;
	for(i++; i<url.Len(); i++) {
		if(url[i]>='0' && url[i]<='9') {
			media_id.t_id*=10;
			media_id.t_id+=url[i]-'0';
		}
		else break;
	}
	return media_id;
}

static uint64_t ParseUrlID(wxString url) {
	uint64_t id=0;
	for(unsigned int i=1; i<url.Len(); i++) {
		if(url[i]>='0' && url[i]<='9') {
			id*=10;
			id+=url[i]-'0';
		}
		else break;
	}
	return id;
}

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
	if(flags&TPF_AUTO_TW && flags&TPF_AUTO_DM) type=wxT("Tweets & DMs");
	else if(flags&TPF_AUTO_TW) type=wxT("Tweets");
	else if(flags&TPF_AUTO_DM) type=wxT("DMs");
	else if(flags&TPF_AUTO_MN) type=wxT("Mentions");

	std::string paneldispname=std::string(wxString::Format(wxT("[%s - %s]"), name.c_str(), type.c_str()).ToUTF8());
	std::string panelname=std::string(wxString::Format(wxT("___ATL_%s_%s"), accname.c_str(), type.c_str()).ToUTF8());

	auto tp=tpanel::MkTPanel(panelname, paneldispname, flags, &acc);
	tp->MkTPanelWin(parent, true);
}

static void AppendToTAMIMenuMap(tweetactmenudata &map, int &nextid, TAMI_TYPE type, std::shared_ptr<tweet> tw, unsigned int dbindex=0, std::shared_ptr<userdatacontainer> user=std::shared_ptr<userdatacontainer>(), unsigned int flags=0, wxString extra = wxT("")) {
	map[nextid]={tw, user, type, dbindex, flags, extra};
	nextid++;
}

void MakeRetweetMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		wxMenuItem *menuitem=menuP->Append(nextid, (*it)->dispname);
		menuitem->Enable((*it)->enabled);
		AppendToTAMIMenuMap(map, nextid, TAMI_RETWEET, tw, (*it)->dbindex);
	}
}

void MakeFavMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		tweet_perspective *tp=tw->GetTweetTP(*it);
		bool known=(tp!=0);
		bool faved=false;
		if(tp && tp->IsFavourited()) faved=true;

		wxMenu *submenu = new wxMenu;
		menuP->AppendSubMenu(submenu, (known?(faved?wxT("\x2713 "):wxT("\x2715 ")):wxT("? ")) + (*it)->dispname);
		submenu->SetTitle(known?(faved?wxT("Favourited"):wxT("Not Favourited")):wxT("Unknown"));

		wxMenuItem *menuitem=submenu->Append(nextid, wxT("Favourite"));
		menuitem->Enable((*it)->enabled && (!known || !faved));
		AppendToTAMIMenuMap(map, nextid, TAMI_FAV, tw, (*it)->dbindex);

		menuitem=submenu->Append(nextid, wxT("Remove Favourite"));
		menuitem->Enable((*it)->enabled && (!known || faved));
		AppendToTAMIMenuMap(map, nextid, TAMI_UNFAV, tw, (*it)->dbindex);
	}
}

void MakeCopyMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, const std::shared_ptr<tweet> &tw) {
	menuP->Append(nextid, wxT("Copy Text"));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYTEXT, tw);
	menuP->Append(nextid, wxT("Copy Link to Tweet"));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYLINK, tw);
	menuP->Append(nextid, wxString::Format(wxT("Copy ID (%" wxLongLongFmtSpec "d)"), tw->id));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYID, tw);
}

void TweetActMenuAction(tweetactmenudata &map, int curid, mainframe *mainwin=0) {
	unsigned int dbindex=map[curid].dbindex;
	std::shared_ptr<taccount> *acc=0;
	if(dbindex) {
		for(auto it=alist.begin(); it!=alist.end(); ++it) {
			if((*it)->dbindex==dbindex) {
				acc=&(*it);
				break;
			}
		}
	}

	CS_ENUMTYPE type=CS_NULL;
	switch(map[curid].type) {
		case TAMI_REPLY: if(mainwin) mainwin->tpw->SetReplyTarget(map[curid].tw); break;
		case TAMI_DM: if(mainwin) mainwin->tpw->SetDMTarget(map[curid].user); break;
		case TAMI_RETWEET: type=CS_RT; break;
		case TAMI_FAV: type=CS_FAV; break;
		case TAMI_UNFAV: type=CS_UNFAV; break;
		case TAMI_DELETE: {
			if(map[curid].tw->flags.Get('D')) type=CS_DELETEDM;
			else type=CS_DELETETWEET;
			break;
		}
		case TAMI_COPYLINK: {
			std::string url=map[curid].tw->GetPermalink();
			if(url.size()) {
				if(wxTheClipboard->Open()) {
					wxTheClipboard->SetData(new wxTextDataObject(wxstrstd(url)));
					wxTheClipboard->Close();
				}
			}
			break;
		}
		case TAMI_BROWSER: {
			std::string url=map[curid].tw->GetPermalink();
			if(url.size()) {
				::wxLaunchDefaultBrowser(wxstrstd(url));
			}
			break;
		}
		case TAMI_COPYTEXT: {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(wxstrstd(map[curid].tw->text)));
				wxTheClipboard->Close();
			}
			break;
		}
		case TAMI_COPYID: {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(wxString::Format(wxT("%") wxLongLongFmtSpec wxT("d"), map[curid].tw->id)));
				wxTheClipboard->Close();
			}
			break;
		}
		case TAMI_COPYEXTRA: {
			if(wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(map[curid].extra));
				wxTheClipboard->Close();
			}
			break;
		}
		case TAMI_BROWSEREXTRA: {
			::wxLaunchDefaultBrowser(map[curid].extra);
			break;
		}
		case TAMI_MEDIAWIN: {
			media_id_type media_id=ParseMediaID(map[curid].extra);
			if(ad.media_list[media_id].win) {
				ad.media_list[media_id].win->Raise();
			}
			else new media_display_win(mainwin, media_id);
			break;
		}
		case TAMI_USERWINDOW: {
			std::shared_ptr<taccount> acc_hint;
			if(map[curid].tw) map[curid].tw->GetUsableAccount(acc_hint);
			user_window::MkWin(map[curid].user->id, acc_hint);
			break;
		}
	}
	if(type!=CS_NULL && acc && *acc) {
		twitcurlext *twit=(*acc)->cp.GetConn();
		twit->TwInit(*acc);
		twit->connmode=type;
		twit->extra_id=map[curid].tw->id;
		twit->QueueAsyncExec();
	}
}

void CheckClearNoUpdateFlag_All() {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		(*it)->CheckClearNoUpdateFlag();
	}
}

void tpanel::PushTweet(const std::shared_ptr<tweet> &t, unsigned int pushflags) {
	LogMsgFormat(LFT_TPANEL, wxT("Pushing tweet id %" wxLongLongFmtSpec "d to panel %s"), t->id, wxstrstd(name).c_str());
	if(RegisterTweet(t)) {
		for(auto i=twin.begin(); i!=twin.end(); i++) {
			(*i)->PushTweet(t, pushflags);
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
}

void tpanelnotebook::onsizeevt(wxSizeEvent &event) {
	PostSplitSizeCorrect();
	event.Skip();
}

DECLARE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEUP_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT, -1)

DEFINE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT)
DEFINE_EVENT_TYPE(wxextTP_PAGEUP_EVENT)
DEFINE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT)

BEGIN_EVENT_TABLE(panelparentwin_base, wxPanel)
	EVT_COMMAND(wxID_ANY, wxextTP_PAGEUP_EVENT, tpanelparentwin_nt::pageupevthandler)
	EVT_COMMAND(wxID_ANY, wxextTP_PAGEDOWN_EVENT, tpanelparentwin_nt::pagedownevthandler)
	EVT_BUTTON(TPPWID_TOPBTN, tpanelparentwin_nt::pagetopevthandler)
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
	wxBoxSizer* headersizer = new wxBoxSizer(wxHORIZONTAL);
	scrollwin = new tpanelscrollwin(this);
	clabel=new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000));
	outersizer->Add(headersizer, 0, wxALL | wxEXPAND, 0);
	headersizer->Add(clabel, 0, wxALL, 2);
	headersizer->AddStretchSpacer();
	headersizer->Add(new wxButton(this, TPPWID_TOPBTN, wxT("Top \x2191"), wxPoint(-1000, -1000), wxDefaultSize, wxBU_EXACTFIT), 0, wxALL, 2);
	outersizer->Add(scrollwin, 1, wxALL | wxEXPAND, 2);
	outersizer->Add(new wxStaticText(this, wxID_ANY, wxT("Bar"), wxPoint(-1000, -1000)), 0, wxALL, 2);

	sizer = new wxBoxSizer(wxVERTICAL);
	scrollwin->SetSizer(sizer);
	SetSizer(outersizer);
	scrollwin->SetScrollRate(1, 1);
	if(fitnow) {
		scrollwin->FitInside();
		FitInside();
	}
}

void panelparentwin_base::PopTop() {
	//currentdisp.front().second->Destroy();
	size_t offset=0;
	wxSizer *sz=sizer->GetItem(offset)->GetSizer();
	if(sz) {
		sz->Clear(true);
		sizer->Remove(offset);
	}
	currentdisp.pop_front();
}

void panelparentwin_base::PopBottom() {
	//currentdisp.back().second->Destroy();
	size_t offset=currentdisp.size()-1;
	wxSizer *sz=sizer->GetItem(offset)->GetSizer();
	if(sz) {
		sz->Clear(true);
		sizer->Remove(offset);
	}
	currentdisp.pop_back();
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
	if(tppw_flags&TPPWF_NOUPDATEONPUSH) {
		scrollwin->FitInside();
		UpdateCLabel();
		tppw_flags&=~TPPWF_NOUPDATEONPUSH;
	}
}

void panelparentwin_base::StartScrollFreeze(tppw_scrollfreeze &s) {
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

mainframe *panelparentwin_base::GetMainframe() {
	return GetMainframeAncestor(this);
}

BEGIN_EVENT_TABLE(tpanelparentwin_nt, panelparentwin_base)
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
	LogMsgFormat(LFT_TPANEL, "tpanelparentwin_nt::PushTweet, id: %" wxLongLongFmtSpec "d, %d, %X, %d", t->id, displayoffset, pushflags, currentdisp.size());
	tppw_scrollfreeze sf;
	StartScrollFreeze(sf);
	uint64_t id=t->id;
	if(displayoffset) {
		if(id>currentdisp.front().first) {
			if(!(pushflags&TPPWPF_ABOVE)) {
				displayoffset++;
				scrollwin->Thaw();
				return;
			}
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
	if(pushflags&TPPWPF_SETNOUPDATEFLAG) tppw_flags|=TPPWF_NOUPDATEONPUSH;
	size_t index=0;
	auto it=currentdisp.begin();
	for(; it!=currentdisp.end(); it++, index++) {
		if(it->first<id) break;	//insert before this iterator
	}
	tweetdispscr *td = PushTweetIndex(t, index);
	currentdisp.insert(it, std::make_pair(id, td));
	if(!(tppw_flags&TPPWF_NOUPDATEONPUSH)) UpdateCLabel();
	EndScrollFreeze(sf);
	scrollwin->Thaw();
}

tweetdispscr *tpanelparentwin_nt::PushTweetIndex(const std::shared_ptr<tweet> &t, size_t index) {
	LogMsgFormat(LFT_TPANEL, "tpanelparentwin_nt::PushTweetIndex, id: %" wxLongLongFmtSpec "d, %d", t->id, index);
	wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);

	tweetdispscr *td=new tweetdispscr(t, scrollwin, this, vbox);

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
			td->bm = new profimg_staticbitmap(scrollwin, t->user->cached_profile_img_half, t->user->id, t->id, GetMainframe());
			td->bm2 = new profimg_staticbitmap(scrollwin, t->user_recipient->cached_profile_img_half, t->user_recipient->id, t->id, GetMainframe());
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

	vbox->Add(td, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);
	hbox->Add(vbox, 1, wxEXPAND, 0);

	sizer->Insert(index, hbox, 0, wxALL | wxEXPAND, 1);
	td->DisplayTweet();

	if(t->in_reply_to_status_id) {
		std::shared_ptr<tweet> subt=ad.GetTweetById(t->in_reply_to_status_id);
		std::shared_ptr<taccount> pacc;
		t->GetUsableAccount(pacc, GUAF_NOERR) || t->GetUsableAccount(pacc, GUAF_NOERR|GUAF_USERENABLED);
		subt->pending_ops.emplace_front(new tpanel_subtweet_pending_op(vbox, this, td));
		if(CheckFetchPendingSingleTweet(subt, pacc)) UnmarkPendingTweet(subt, 0);
	}

	return td;
}

void tpanelparentwin_nt::PageUpHandler() {
	if(displayoffset) {
		tppw_flags|=TPPWF_NOUPDATEONPUSH;
		size_t pagemove=std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, displayoffset);
		auto it=tp->tweetlist.lower_bound(currentdisp.front().first);
		for(unsigned int i=0; i<pagemove; i++) {
			it--;
			displayoffset--;
			const std::shared_ptr<tweet> &t=ad.GetTweetById(*it);
			if(t->IsReady()) PushTweet(t, TPPWPF_ABOVE);
			//otherwise tweet is already on pending list
		}
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
		LoadMore(pagemove, lessthanid, TPPWPF_BELOW);
	}
	scrollwin->page_scroll_blocked=false;
	CheckClearNoUpdateFlag();
}

void tpanelparentwin_nt::PageTopHandler() {
	if(displayoffset) {
		tppw_flags|=TPPWF_NOUPDATEONPUSH;
		size_t pushcount=std::min(displayoffset, (size_t) gc.maxtweetsdisplayinpanel);
		for(auto it=tp->tweetlist.begin(); it!=tp->tweetlist.end() && pushcount; ++it, --pushcount) {
			const std::shared_ptr<tweet> &t=ad.GetTweetById(*it);
			if(t->IsReady()) PushTweet(t, TPPWPF_ABOVE);
		}
		displayoffset=0;
		CheckClearNoUpdateFlag();
	}
	scrollwin->Scroll(-1, 0);
}

void tpanelparentwin_nt::UpdateCLabel() {
	size_t curnum=currentdisp.size();
	if(curnum) clabel->SetLabel(wxString::Format(wxT("%d - %d of %d"), displayoffset+1, displayoffset+curnum, tp->tweetlist.size()));
	else clabel->SetLabel(wxT("No Tweets"));
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
	LoadMore(gc.maxtweetsdisplayinpanel);
	parent->auib->AddPage(this, wxstrstd(tp->dispname), select);
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

//if lessthanid is non-zero, is an exclusive upper id limit
void tpanelparentwin::LoadMore(unsigned int n, uint64_t lessthanid, unsigned int pushflags) {
	dbseltweetmsg *loadmsg=0;

	Freeze();
	tppw_flags|=TPPWF_NOUPDATEONPUSH;

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
	if(currentlogflags&LFT_PENDTRACE) dump_tweet_pendings(LFT_PENDTRACE, wxT(""), wxT("\t"));

	Thaw();
	CheckClearNoUpdateFlag();
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

		td->bm = new profimg_staticbitmap(scrollwin, u->cached_profile_img, u->id, 0);
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

tpanelparentwin_usertweets::tpanelparentwin_usertweets(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::weak_ptr<taccount> &acc_, RBFS_TYPE type_)
	: tpanelparentwin_nt(MkUserTweetTPanel(user_, type_), parent), user(user_), acc(acc_), havestarted(false), type(type_) {
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
	std::shared_ptr<taccount> tac=acc.lock();
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
		tac->StartRestGetTweetBackfill(lower_id /*lower limit, exclusive*/, upper_id /*upper limit, inclusive*/, numleft, type, user->id);
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

BEGIN_EVENT_TABLE(tpanelparentwin_userproplisting, tpanelparentwin_user)
END_EVENT_TABLE()

tpanelparentwin_userproplisting::tpanelparentwin_userproplisting(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::weak_ptr<taccount> &acc_, CS_ENUMTYPE type_)
	: tpanelparentwin_user(parent), user(user_), acc(acc_), havestarted(false), type(type_) {

}

tpanelparentwin_userproplisting::~tpanelparentwin_userproplisting() {
}

void tpanelparentwin_userproplisting::Init() {
	std::shared_ptr<taccount> tac=acc.lock();
	if(tac) {
		twitcurlext *twit=tac->cp.GetConn();
		twit->TwInit(tac);
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
	std::shared_ptr<taccount> tac=acc.lock();
	if(!tac) return;
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

BEGIN_EVENT_TABLE(dispscr_base, wxRichTextCtrl)
	EVT_MOUSEWHEEL(dispscr_base::mousewheelhandler)
END_EVENT_TABLE()

dispscr_base::dispscr_base(tpanelscrollwin *parent, panelparentwin_base *tppw_, wxBoxSizer *hbox_)
: wxRichTextCtrl(parent, wxID_ANY, wxEmptyString, wxPoint(-1000, -1000), wxDefaultSize, wxRE_READONLY | wxRE_MULTILINE | wxBORDER_NONE), tppw(tppw_), tpsw(parent), hbox(hbox_) {
	GetCaret()->Hide();
}

void dispscr_base::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
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
		tpsw->GetEventHandler()->AddPendingEvent(event);
	}
}

BEGIN_EVENT_TABLE(tweetdispscr, dispscr_base)
	EVT_TEXT_URL(wxID_ANY, tweetdispscr::urleventhandler)
	EVT_MENU_RANGE(tweetactmenustartid, tweetactmenuendid, tweetdispscr::OnTweetActMenuCmd)
	EVT_RIGHT_UP(tweetdispscr::rightclickhandler)
END_EVENT_TABLE()

tweetdispscr::tweetdispscr(const std::shared_ptr<tweet> &td_, tpanelscrollwin *parent, tpanelparentwin_nt *tppw_, wxBoxSizer *hbox_)
: dispscr_base(parent, tppw_, hbox_), td(td_), bm(0), bm2(0) {
	if(td_->rtsrc) rtid=td_->rtsrc->id;
	else rtid=0;
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
			int delta=0;
			if(str[track_byte+1]=='l' && str[track_byte+2]=='t' && str[track_byte+3]==';') {
				rep='<';
				delta=4;
			}
			else if(str[track_byte+1]=='g' && str[track_byte+2]=='t' && str[track_byte+3]==';') {
				rep='>';
				delta=4;
			}
			else if(str[track_byte+1]=='q' && str[track_byte+2]=='u' && str[track_byte+3]=='o' && str[track_byte+4]=='t' && str[track_byte+5]==';') {
				rep='\'';
				delta=6;
			}
			else if(str[track_byte+1]=='#' && str[track_byte+2]=='3' && str[track_byte+3]=='9' && str[track_byte+4]==';') {
				rep='"';
				delta=5;
			}
			else if(str[track_byte+1]=='a' && str[track_byte+2]=='m' && str[track_byte+3]=='p' && str[track_byte+4]==';') {
				rep='&';
				delta=5;
			}
			if(rep) {
				td.WriteText(wxString::FromUTF8(&str[start_offset], track_byte-start_offset));
				track_index+=delta;
				track_byte+=delta;
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

inline void GenFlush(dispscr_base *obj, wxString &str) {
	if(str.size()) {
		obj->WriteText(str);
		str.clear();
	}
}

void GenUserFmt(dispscr_base *obj, userdatacontainer *u, size_t &i, const wxString &format, wxString &str) {
	i++;
	if(i>=format.size()) return;
	switch((wxChar) format[i]) {
		case 'n':
			str+=wxstrstd(u->GetUser().screen_name);
			break;
		case 'N':
			str+=wxstrstd(u->GetUser().name);
			break;
		case 'i':
			str+=wxString::Format("%" wxLongLongFmtSpec "d", u->id);
			break;
		case 'Z': {
			GenFlush(obj, str);
			obj->BeginURL(wxString::Format("U%" wxLongLongFmtSpec "d", u->id), obj->GetDefaultStyleEx().GetCharacterStyleName());
			break;
		}
		case 'p':
			if(u->GetUser().u_flags&UF_ISPROTECTED) {
				GenFlush(obj, str);
				obj->WriteImage(obj->tppw->tpg->proticon_img);
				obj->SetInsertionPointEnd();
			}
			break;
		case 'v':
			if(u->GetUser().u_flags&UF_ISVERIFIED) {
				GenFlush(obj, str);
				obj->WriteImage(obj->tppw->tpg->verifiedicon_img);
				obj->SetInsertionPointEnd();
			}
			break;
		case 'd': {
			GenFlush(obj, str);
			long curpos=obj->GetInsertionPoint();
			obj->BeginURL(wxString::Format(wxT("Xd%" wxLongLongFmtSpec "d"), u->id));
			obj->WriteImage(obj->tppw->tpg->dmreplyicon_img);
			obj->EndURL();
			obj->SetInsertionPointEnd();
			wxTextAttrEx attr(obj->GetDefaultStyleEx());
			attr.SetURL(wxString::Format(wxT("Xd%" wxLongLongFmtSpec "d"), u->id));
			obj->SetStyleEx(curpos, obj->GetInsertionPoint(), attr, wxRICHTEXT_SETSTYLE_OPTIMIZE);
			break;
		}
		case 'w': {
			if(u->GetUser().userurl.size()) {
				GenFlush(obj, str);
				obj->BeginURL(wxT("W") + wxstrstd(u->GetUser().userurl));
				obj->WriteText(wxstrstd(u->GetUser().userurl));
				obj->EndURL();
			}
			break;
		}
		case 'D': {
			str+=wxstrstd(u->GetUser().description);
		}
		case 'l': {
			str+=wxstrstd(u->GetUser().location);
		}
		default:
			break;
	}
}

void GenFmtCodeProc(dispscr_base *obj, size_t &i, const wxString &format, wxString &str) {
	switch((wxChar) format[i]) {
		case 'B': GenFlush(obj, str); obj->BeginBold(); break;
		case 'b': GenFlush(obj, str); obj->EndBold(); break;
		case 'L': GenFlush(obj, str); obj->BeginUnderline(); break;
		case 'l': GenFlush(obj, str); obj->EndUnderline(); break;
		case 'I': GenFlush(obj, str); obj->BeginItalic(); break;
		case 'i': GenFlush(obj, str); obj->EndItalic(); break;
		case 'z': GenFlush(obj, str); obj->EndURL(); break;
		case 'N': GenFlush(obj, str); obj->Newline(); break;
		case 'n': {
			GenFlush(obj, str);
			long y;
			obj->PositionToXY(obj->GetInsertionPoint(), 0, &y);
			if(obj->GetLineLength(y)) obj->Newline();
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

void tweetdispscr::DisplayTweet(bool redrawimg) {
	updatetime=0;
	std::forward_list<media_entity*> me_list;
	auto last_me=me_list.before_begin();

	tweet &tw=*td;
	userdatacontainer *udc=tw.user.get();
	userdatacontainer *udc_recip=tw.user_recipient.get();

	if(redrawimg) {
		if(bm && bm2 && udc_recip) {
			udc->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
			udc_recip->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
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
		GenFlush(this, str);
	};
	auto userfmt=[&](userdatacontainer *u, size_t &i) {
		GenUserFmt(this, u, i, format, str);
	};

	for(size_t i=0; i<format.size(); i++) {
		switch((wxChar) format[i]) {
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
			case 'F':
				str+=wxstrstd(tw.flags.GetString());
				break;
			case 't':
				flush();
				reltimestart=GetInsertionPoint();
				WriteText(getreltimestr(tw.createtime, updatetime));
				reltimeend=GetInsertionPoint();
				if(!tppw->tpg->minutetimer.IsRunning()) tppw->tpg->minutetimer.Start(60000, wxTIMER_CONTINUOUS);
				break;
			case 'T':
				str+=rc_wx_strftime(gc.gcfg.datetimeformat.val, localtime(&tw.createtime), tw.createtime, true);
				break;

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
			case 'X': {
				i++;
				if(i>=format.size()) break;
				flush();
				long curpos=GetInsertionPoint();
				wxString url=wxString::Format(wxT("X%c"), (wxChar) format[i]);
				BeginURL(url);
				bool imginserted=false;
				switch((wxChar) format[i]) {
					case 'i': WriteImage(tppw->tpg->infoicon_img); imginserted=true; break;
					case 'f': {
						if(tw.IsFavouritable()) {
							wxImage *icon=&tppw->tpg->favicon_img;
							for(auto it=tw.tp_list.begin(); it!=tw.tp_list.end(); ++it) {
								if(it->IsFavourited()) {
									icon=&tppw->tpg->favonicon_img;
									break;
								}
							}
							WriteImage(*icon);
							imginserted=true;
						}
						break;
					}
					case 'r': WriteImage(tppw->tpg->replyicon_img); imginserted=true; break;
					case 'd': {
						EndURL();
						std::shared_ptr<userdatacontainer> targ=tw.user_recipient;
						if(!targ || targ->udc_flags&UDC_THIS_IS_ACC_USER_HINT) targ=targ=tw.user;
						url=wxString::Format(wxT("Xd%" wxLongLongFmtSpec "d"), targ->id);
						BeginURL(url);
						WriteImage(tppw->tpg->dmreplyicon_img); imginserted=true; break;
					}
					case 't': {
						if(tw.IsRetweetable()) {
							wxImage *icon=&tppw->tpg->retweeticon_img;
							for(auto it=tw.tp_list.begin(); it!=tw.tp_list.end(); ++it) {
								if(it->IsRetweeted()) {
									icon=&tppw->tpg->retweetonicon_img;
									break;
								}
							}
							WriteImage(*icon);
							imginserted=true;
						}
						break;
					}
					default: break;
				}
				EndURL();
				if(imginserted) {
					SetInsertionPointEnd();
					wxTextAttrEx attr(GetDefaultStyleEx());
					attr.SetURL(url);
					SetStyleEx(curpos, GetInsertionPoint(), attr, wxRICHTEXT_SETSTYLE_OPTIMIZE);
				}
				break;
			}
			default: {
				GenFmtCodeProc(this, i, format, str);
				break;
			}
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
	if(!(tppw->tppw_flags&TPPWF_NOUPDATEONPUSH)) {
		tpsw->FitInside();
	}
}

void tweetdispscr::urleventhandler(wxTextUrlEvent &event) {
	long start=event.GetURLStart();
	wxRichTextAttr textattr;
	GetStyle(start, textattr);
	wxString url=textattr.GetURL();
	LogMsgFormat(LFT_TPANEL, wxT("URL clicked, id: %s"), url.c_str());
	urlhandler(url);
}

void tweetdispscr::urlhandler(wxString url) {
	if(url[0]=='M') {
		media_id_type media_id=ParseMediaID(url);

		LogMsgFormat(LFT_TPANEL, wxT("Media image clicked, str: %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d"), url.Mid(1).c_str(), media_id.m_id, media_id.t_id);
		if(ad.media_list[media_id].win) {
			ad.media_list[media_id].win->Raise();
		}
		else new media_display_win(this, media_id);
	}
	else if(url[0]=='U') {
		uint64_t userid=ParseUrlID(url);
		if(userid) {
			std::shared_ptr<taccount> acc_hint;
			td->GetUsableAccount(acc_hint);
			user_window::MkWin(userid, acc_hint);
		}
	}
	else if(url[0]=='W') {
		::wxLaunchDefaultBrowser(url.Mid(1));
	}
	else if(url[0]=='X') {
		switch((wxChar) url[1]) {
			case 'd': { //send dm
				uint64_t userid;
				ownstrtonum(userid, (wxChar*) &url[2], -1);
				mainframe *mf=tppw->GetMainframe();
				if(mf) mf->tpw->SetDMTarget(ad.GetUserContainerById(userid));
				break;
			}
			case 'r': {//reply
				mainframe *mf=tppw->GetMainframe();
				if(mf) mf->tpw->SetReplyTarget(td);
				break;
			}
			case 'f': {//fav
				tamd.clear();
				int nextid=tweetactmenustartid;
				wxMenu menu;
				menu.SetTitle(wxT("Favourite:"));
				MakeFavMenu(&menu, tamd, nextid, td);
				PopupMenu(&menu);
				break;
			}
			case 't': {//retweet
				tamd.clear();
				int nextid=tweetactmenustartid;
				wxMenu menu;
				menu.SetTitle(wxT("Retweet:"));
				MakeRetweetMenu(&menu, tamd, nextid, td);
				PopupMenu(&menu);
				break;
			}
			case 'i': {//info
				tamd.clear();
				int nextid=tweetactmenustartid;
				wxMenu menu, rtsubmenu, favsubmenu, copysubmenu;

				menu.Append(nextid, wxT("Reply"));
				AppendToTAMIMenuMap(tamd, nextid, TAMI_REPLY, td);

				std::shared_ptr<userdatacontainer> targ=td->user_recipient;
				if(!targ || targ->udc_flags&UDC_THIS_IS_ACC_USER_HINT) targ=targ=td->user;
				menu.Append(nextid, wxT("Send DM"));
				AppendToTAMIMenuMap(tamd, nextid, TAMI_DM, td, 0, targ);

				menu.Append(nextid, wxT("Open in Browser"));
				AppendToTAMIMenuMap(tamd, nextid, TAMI_BROWSER, td);

				if(td->IsRetweetable()) menu.AppendSubMenu(&rtsubmenu, wxT("Retweet"));
				if(td->IsFavouritable()) menu.AppendSubMenu(&favsubmenu, wxT("Favourite"));
				menu.AppendSubMenu(&copysubmenu, wxT("Copy"));

				bool deletable=false;
				bool deletable2=false;
				unsigned int delcount=0;
				unsigned int deldbindex=0;
				unsigned int deldbindex2=0;
				std::shared_ptr<taccount> cacc;
				std::shared_ptr<taccount> cacc2;
				if(td->flags.Get('D')) {
					cacc=td->user_recipient->GetAccountOfUser();
					if(cacc) delcount++;
					if(cacc && cacc->enabled) {
						deletable=true;
						deldbindex=cacc->dbindex;
					}
				}
				cacc2=td->user->GetAccountOfUser();
				if(cacc2) delcount++;
				if(cacc2 && cacc2->enabled) {
					deletable2=true;
					deldbindex2=cacc2->dbindex;
				}
				if(delcount>1) {	//user has DMd another of their own accounts :/
					wxMenuItem *delmenuitem=menu.Append(nextid, wxT("Delete: ") + cacc->dispname);
					delmenuitem->Enable(deletable);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex);
					delmenuitem=menu.Append(nextid, wxT("Delete: ") + cacc2->dispname);
					delmenuitem->Enable(deletable2);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex2);
				}
				else {
					wxMenuItem *delmenuitem=menu.Append(nextid, wxT("Delete"));
					delmenuitem->Enable(deletable|deletable2);
					AppendToTAMIMenuMap(tamd, nextid, TAMI_DELETE, td, deldbindex2?deldbindex2:deldbindex);
				}

				MakeRetweetMenu(&rtsubmenu, tamd, nextid, td);
				MakeFavMenu(&favsubmenu, tamd, nextid, td);
				MakeCopyMenu(&copysubmenu, tamd, nextid, td);
				PopupMenu(&menu);
				break;
			}
		}
	}
	else {
		unsigned long counter;
		url.ToULong(&counter);
		auto it=td->entlist.begin();
		while(it!=td->entlist.end()) {
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
					case ENT_MENTION: {
						std::shared_ptr<taccount> acc_hint;
						td->GetUsableAccount(acc_hint);
						user_window::MkWin(et.user->id, acc_hint);
						break;
						}
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

static void AppendUserMenuItems(wxMenu &menu, tweetactmenudata &map, int &nextid, std::shared_ptr<userdatacontainer> user, std::shared_ptr<tweet> tw) {
	menu.Append(nextid, wxT("Open User Window"));
	AppendToTAMIMenuMap(map, nextid, TAMI_USERWINDOW, tw, 0, user);

	menu.Append(nextid, wxT("Open Twitter Profile in Browser"));
	AppendToTAMIMenuMap(map, nextid, TAMI_BROWSEREXTRA, tw, 0, user, 0, wxstrstd(user->GetPermalink(tw->flags.Get('s'))));

	menu.Append(nextid, wxT("Send DM"));
	AppendToTAMIMenuMap(map, nextid, TAMI_DM, tw, 0, user);

	wxString username=wxstrstd(user->GetUser().screen_name);
	menu.Append(nextid, wxString::Format(wxT("Copy User Name (%s)"), username.c_str()));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYEXTRA, tw, 0, user, 0, username);

	wxString useridstr=wxString::Format(wxT("%" wxLongLongFmtSpec "d"), user->id);
	menu.Append(nextid, wxString::Format(wxT("Copy User ID (%s)"), useridstr.c_str()));
	AppendToTAMIMenuMap(map, nextid, TAMI_COPYEXTRA, tw, 0, user, 0, useridstr);
}

void tweetdispscr::rightclickhandler(wxMouseEvent &event) {
	wxPoint mousepoint=event.GetPosition();
	long textpos=0;
	HitTest(mousepoint, &textpos);
	wxRichTextAttr style;
	GetStyle(textpos, style);
	if(style.HasURL()) {
		wxString url=style.GetURL();
		int nextid=tweetactmenustartid;
		wxMenu menu;
		if(url[0]=='M') {
			media_id_type media_id=ParseMediaID(url);
			menu.Append(nextid, wxT("Open Media in Window"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_MEDIAWIN, td, 0, std::shared_ptr<userdatacontainer>(), 0, url);
			menu.Append(nextid, wxT("Copy Media URL"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_COPYEXTRA, td, 0, std::shared_ptr<userdatacontainer>(), 0, wxstrstd(ad.media_list[media_id].media_url));
			PopupMenu(&menu);
		}
		else if(url[0]=='U') {
			uint64_t userid=ParseUrlID(url);
			if(userid) {
				std::shared_ptr<userdatacontainer> user=ad.GetUserContainerById(userid);
				AppendUserMenuItems(menu, tamd, nextid, user, td);
				PopupMenu(&menu);
			}
		}
		else if(url[0]=='W') {
			menu.Append(nextid, wxT("Open URL in Browser"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_BROWSEREXTRA, td, 0, std::shared_ptr<userdatacontainer>(), 0, url.Mid(1));
			menu.Append(nextid, wxT("Copy URL"));
			AppendToTAMIMenuMap(tamd, nextid, TAMI_COPYEXTRA, td, 0, std::shared_ptr<userdatacontainer>(), 0, url.Mid(1));
			PopupMenu(&menu);
		}
		else if(url[0]=='X') {
			return;
		}
		else {
			unsigned long counter;
			url.ToULong(&counter);
			auto it=td->entlist.begin();
			while(it!=td->entlist.end()) {
				if(!counter) {
					//got entity
					entity &et= *it;
					switch(et.type) {
						case ENT_HASHTAG:
						break;
						case ENT_URL:
						case ENT_URL_IMG:
						case ENT_MEDIA:
							menu.Append(nextid, wxT("Open URL in Browser"));
							AppendToTAMIMenuMap(tamd, nextid, TAMI_BROWSEREXTRA, td, 0, std::shared_ptr<userdatacontainer>(), 0, wxstrstd(et.fullurl));
							menu.Append(nextid, wxT("Copy URL"));
							AppendToTAMIMenuMap(tamd, nextid, TAMI_COPYEXTRA, td, 0, std::shared_ptr<userdatacontainer>(), 0, wxstrstd(et.fullurl));
							PopupMenu(&menu);
						break;
						case ENT_MENTION: {
							AppendUserMenuItems(menu, tamd, nextid, et.user, td);
							PopupMenu(&menu);
							break;
						}
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
	else {
		event.Skip();
	}
}

void dispscr_base::mousewheelhandler(wxMouseEvent &event) {
	//LogMsg(LFT_TPANEL, wxT("MouseWheel"));
	event.SetEventObject(GetParent());
	GetParent()->GetEventHandler()->ProcessEvent(event);
}

void tweetdispscr::OnTweetActMenuCmd(wxCommandEvent &event) {
	TweetActMenuAction(tamd, event.GetId(), tppw->GetMainframe());
}

BEGIN_EVENT_TABLE(userdispscr, dispscr_base)
	EVT_TEXT_URL(wxID_ANY, userdispscr::urleventhandler)
END_EVENT_TABLE()

userdispscr::userdispscr(const std::shared_ptr<userdatacontainer> &u_, tpanelscrollwin *parent, tpanelparentwin_user *tppw_, wxBoxSizer *hbox_)
: dispscr_base(parent, tppw_, hbox_), u(u_), bm(0) { }

userdispscr::~userdispscr() { }

void userdispscr::Display(bool redrawimg) {
	if(redrawimg) {
		if(bm) {
			bm->SetBitmap(u->cached_profile_img);
		}
	}

	Clear();
	wxString format=gc.gcfg.userdispformat.val;
	wxString str=wxT("");

	for(size_t i=0; i<format.size(); i++) {
		switch((wxChar) format[i]) {
			case 'u':
				GenUserFmt(this, u.get(), i, format, str);
				break;
			default: {
				GenFmtCodeProc(this, i, format, str);
				break;
			}
		}
	}
	GenFlush(this, str);

	LayoutContent();
	if(!(tppw->tppw_flags&TPPWF_NOUPDATEONPUSH)) {
		tpsw->FitInside();
	}
}

void userdispscr::urleventhandler(wxTextUrlEvent &event) {
	long start=event.GetURLStart();
	wxRichTextAttr textattr;
	GetStyle(start, textattr);
	wxString url=textattr.GetURL();
	LogMsgFormat(LFT_TPANEL, wxT("URL clicked, id: %s"), url.c_str());
	if(url[0]=='U') {
		uint64_t userid=0;
		for(unsigned int i=1; i<url.Len(); i++) {
			if(url[i]>='0' && url[i]<='9') {
				userid*=10;
				userid+=url[i]-'0';
			}
			else break;
		}
		if(userid) {
			std::shared_ptr<taccount> acc_hint;
			u->GetUsableAccount(acc_hint);
			user_window::MkWin(userid, acc_hint);
		}
	}
	else if(url[0]=='W') {
		::wxLaunchDefaultBrowser(url.Mid(1));
	}
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
		char *data=0;
		size_t size;
		if(LoadFromFileAndCheckHash(me->cached_full_filename(), me->full_img_sha1, data, size)) {
			me->flags|=ME_HAVE_FULL;
			me->fulldata.assign(data, size);	//redundant copy, but oh well
		}
		if(data) free(data);
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
			tweetdispscr &td=(tweetdispscr &) *((*jt).second);
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
	if(owner) {
		wxMenu menu;
		int nextid=tweetactmenustartid;
		tamd.clear();
		AppendUserMenuItems(menu, tamd, nextid, ad.GetUserContainerById(userid), ad.GetTweetById(tweetid));
		PopupMenu(&menu);
	}
}

void profimg_staticbitmap::OnTweetActMenuCmd(wxCommandEvent &event) {
	TweetActMenuAction(tamd, event.GetId(), owner);
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
}
