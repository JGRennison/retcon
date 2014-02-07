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
#include "tpanel.h"
#include "tpanel-data.h"
#include "dispscr.h"
#include "twit.h"
#include "taccount.h"
#include "utf8.h"
#include "res.h"
#include "version.h"
#include "log.h"
#include "log-impl.h"
#include "alldata.h"
#include "mainui.h"
#include "twitcurlext.h"
#include "userui.h"
#include "db.h"
#include "util.h"
#include "raii.h"
#include <wx/choicdlg.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <forward_list>
#include <algorithm>
#include <deque>

#ifndef TPANEL_COPIOUS_LOGGING
#define TPANEL_COPIOUS_LOGGING 0
#endif

std::forward_list<tpanelparentwin_nt*> tpanelparentwinlist;

std::shared_ptr<tpanelglobal> tpanelglobal::Get() {
	if(tpg_glob.expired()) {
		std::shared_ptr<tpanelglobal> tmp=std::make_shared<tpanelglobal>();
		tpg_glob=tmp;
		return tmp;
	}
	else return tpg_glob.lock();
}

std::weak_ptr<tpanelglobal> tpanelglobal::tpg_glob;

std::function<void(mainframe *)> MkStdTpanelAction(unsigned int dbindex, flagwrapper<TPF> flags) {
	return [dbindex, flags](mainframe *parent) {
		std::shared_ptr<taccount> acc;
		if(dbindex) {
			if(!GetAccByDBIndex(dbindex, acc)) return;
		}

		auto tp = tpanel::MkTPanel("", "", flags, &acc);
		tp->MkTPanelWin(parent, true);
	};
}

static void PerAccTPanelMenu(wxMenu *menu, tpanelmenudata &map, int &nextid, flagwrapper<TPF> flagbase, unsigned int dbindex) {
	map[nextid] = MkStdTpanelAction(dbindex, flagbase | TPF::AUTO_TW);
	menu->Append(nextid++, wxT("&Tweets"));
	map[nextid] = MkStdTpanelAction(dbindex, flagbase | TPF::AUTO_MN);
	menu->Append(nextid++, wxT("&Mentions"));
	map[nextid] = MkStdTpanelAction(dbindex, flagbase | TPF::AUTO_DM);
	menu->Append(nextid++, wxT("&DMs"));
	map[nextid] = MkStdTpanelAction(dbindex, flagbase | TPF::AUTO_TW | TPF::AUTO_MN);
	menu->Append(nextid++, wxT("T&weets and Mentions"));
	map[nextid] = MkStdTpanelAction(dbindex, flagbase | TPF::AUTO_MN | TPF::AUTO_DM);
	menu->Append(nextid++, wxT("M&entions and DMs"));
	map[nextid] = MkStdTpanelAction(dbindex, flagbase | TPF::AUTO_TW | TPF::AUTO_MN | TPF::AUTO_DM);
	menu->Append(nextid++, wxT("Tweets, Mentions &and DMs"));
}

void MakeTPanelMenu(wxMenu *menuP, tpanelmenudata &map) {
	wxMenuItemList items=menuP->GetMenuItems();		//make a copy to avoid memory issues if Destroy modifies the list
	for(auto it=items.begin(); it!=items.end(); ++it) {
		menuP->Destroy(*it);
	}
	map.clear();

	int nextid=tpanelmenustartid;
	PerAccTPanelMenu(menuP, map, nextid, TPF::AUTO_ALLACCS | TPF::DELETEONWINCLOSE, 0);
	menuP->AppendSeparator();

	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		wxMenu *submenu = new wxMenu;
		menuP->AppendSubMenu(submenu, (*it)->dispname);
		PerAccTPanelMenu(submenu, map, nextid, TPF::DELETEONWINCLOSE, (*it)->dbindex);
	}
	menuP->AppendSeparator();

	map[nextid] = MkStdTpanelAction(0, TPF::DELETEONWINCLOSE | TPF::AUTO_NOACC | TPF::AUTO_HIGHLIGHTED);
	menuP->Append(nextid++, wxT("All Highlighted"));
	map[nextid] = MkStdTpanelAction(0, TPF::DELETEONWINCLOSE | TPF::AUTO_NOACC | TPF::AUTO_UNREAD);
	menuP->Append(nextid++, wxT("All Unread"));
	menuP->AppendSeparator();

	std::vector<std::shared_ptr<tpanel> > manual_tps;
	for(auto &it : ad.tpanels) {
		if(it.second->flags & TPF::MANUAL) {
			manual_tps.push_back(it.second);
		}
	}
	if(!manual_tps.empty()) {
		for(auto &it : manual_tps) {
			std::weak_ptr<tpanel> tpwp = it;
			map[nextid] = [tpwp](mainframe *parent) {
				std::shared_ptr<tpanel> tp = tpwp.lock();
				if(tp) {
					tp->MkTPanelWin(parent, true);
				}
			};
			menuP->Append(nextid++, wxstrstd(it->dispname));
		}
		menuP->AppendSeparator();
	}

	map[nextid] = [](mainframe *parent) {
		TPanelMenuActionCustom(parent, TPF::DELETEONWINCLOSE);
	};
	menuP->Append(nextid++, wxT("Custom Combination"));
	menuP->AppendSeparator();

	map[nextid] = [](mainframe *parent) {
		std::string default_name;
		for(size_t i = 0; ; i++) { //stop when no such tpanel exists with the given name
			default_name = string_format("Unnamed Panel %u", i);
			if(!ad.tpanels[tpanel::ManualName(default_name)]) break;
		}
		wxString str = ::wxGetTextFromUser(wxT("Enter name of new panel"), wxT("Input Name"), wxstrstd(default_name));
		str.Trim(true).Trim(false);
		if(!str.IsEmpty()) {
			std::string dispname = stdstrwx(str);
			auto tp = tpanel::MkTPanel(tpanel::ManualName(dispname), dispname, TPF::SAVETODB | TPF::MANUAL);
			tp->MkTPanelWin(parent, true);
		}
	};
	menuP->Append(nextid++, wxT("Create New Empty Panel"));

	if(!manual_tps.empty()) {
		wxMenu *submenu = new wxMenu;
		menuP->AppendSubMenu(submenu, wxT("Delete Panel"));
		for(auto &it : manual_tps) {
			std::weak_ptr<tpanel> tpwp = it;
			map[nextid] = [tpwp](mainframe *parent) {
				std::shared_ptr<tpanel> tp = tpwp.lock();
				if(tp) {
					wxString msg = wxT("Are you sure that you want to delete panel: ") + wxstrstd(tp->dispname);
					int res = ::wxMessageBox(msg, wxT("Delete Panel?"), wxICON_EXCLAMATION | wxYES_NO);
					if(res != wxYES) return;

					//Use temporary vector as tpanel list may be modified as a result of sending close message
					//Having the list modified from under us whilst iterating would be bad news
					std::vector<tpanelparentwin *> windows;
					for(auto &win : tp->twin) {
						tpanelparentwin *tpw = dynamic_cast<tpanelparentwin *>(win);
						if(tpw) windows.push_back(tpw);
					}
					for(auto &win : windows) {
						wxCommandEvent evt;
						win->tabclosehandler(evt);
					}
					ad.tpanels.erase(tp->name);
				}
			};
			submenu->Append(nextid++, wxstrstd(it->dispname));
		}
	}
}

void TPanelMenuActionCustom(mainframe *parent, flagwrapper<TPF> flags) {
	wxArrayInt selections;
	wxArrayString choices;
	std::deque<tpanel_auto> tmpltpautos;

	choices.Alloc((3 * (1 + alist.size())) + 2);

	auto add_item = [&](flagwrapper<TPF> tpf, const std::shared_ptr<taccount> &acc, const wxString &str) {
		choices.Add(str);
		tmpltpautos.emplace_back();
		tmpltpautos.back().autoflags = tpf;
		tmpltpautos.back().acc = acc;
	};

	auto add_acc_items = [&](flagwrapper<TPF> tpf, const std::shared_ptr<taccount> &acc, const wxString &prefix) {
		add_item(tpf | TPF::AUTO_TW, acc, prefix + wxT(" - Tweets"));
		add_item(tpf | TPF::AUTO_MN, acc, prefix + wxT(" - Mentions"));
		add_item(tpf | TPF::AUTO_DM, acc, prefix + wxT(" - DMs"));
	};

	add_acc_items(TPF::AUTO_ALLACCS, std::shared_ptr<taccount>(), wxT("All Accounts"));
	for(auto &it : alist) {
		add_acc_items(0, it, it->dispname);
	}

	add_item(TPF::AUTO_NOACC | TPF::AUTO_HIGHLIGHTED, std::shared_ptr<taccount>(), wxT("All Highlighted"));
	add_item(TPF::AUTO_NOACC | TPF::AUTO_UNREAD, std::shared_ptr<taccount>(), wxT("All Unread"));

	::wxGetMultipleChoices(selections, wxT(""), wxT("Select Accounts and Feed Types"), choices, parent, -1, -1, false);

	std::vector<tpanel_auto> tpautos;

	for(size_t i = 0; i < selections.GetCount(); i++) {
		int index = selections.Item(i);
		tpanel_auto &tpa = tmpltpautos[index];
		bool ok = true;

		flagwrapper<TPF> check_tpf = tpa.autoflags & (TPF::AUTO_TW | TPF::AUTO_MN | TPF::AUTO_DM);
		if(check_tpf && tpa.acc) {
			for(tpanel_auto &it : tpautos) {
				if(it.autoflags & TPF::AUTO_ALLACCS && it.autoflags & check_tpf) {
					// don't set the bit for the account, if the corresponding all accounts bit is already set
					ok = false;
					break;
				}
			}
		}

		if(ok) tpautos.emplace_back(tpa);
	}

	if(tpautos.size()) {
		auto tp = tpanel::MkTPanel("", "", flags, tpautos);
		tp->MkTPanelWin(parent, true);
	}
}

void TPanelMenuAction(tpanelmenudata &map, int curid, mainframe *parent) {
	auto &f = map[curid];
	if(f) f(parent);
}

void CheckClearNoUpdateFlag_All() {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		(*it)->CheckClearNoUpdateFlag();
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
		GenericPopupWrapper(tppw, &menu);
	}
}

void tpanelnotebook::Split(size_t page, int direction) {
	wxAuiNotebook::Split(page, direction);
	PostSplitSizeCorrect();
}

void tpanelnotebook::PostSplitSizeCorrect() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelnotebook::PostSplitSizeCorrect(): START"));
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
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANEL, wxT("TCL: PostSplitSizeCorrect1 %d %d %d %d"), all_panes.Item(i).dock_direction, all_panes.Item(i).dock_layer, all_panes.Item(i).dock_row, all_panes.Item(i).dock_pos);
			#endif
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
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANEL, wxT("TCL: PostSplitSizeCorrect2 %d %d %d %d"), all_panes.Item(i).dock_direction, all_panes.Item(i).dock_layer, all_panes.Item(i).dock_row, all_panes.Item(i).dock_pos);
			#endif
		}
	}

	DoSizing();
	owner->Refresh();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelnotebook::PostSplitSizeCorrect(): END"));
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
		twld.tpautos = tppw->tp->tpautos;
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
	EVT_MENU(TPPWID_TOPBTN, panelparentwin_base::pagetopevthandler)
END_EVENT_TABLE()

panelparentwin_base::panelparentwin_base(wxWindow *parent, bool fitnow, wxString thisname_)
: wxPanel(parent, wxID_ANY, wxPoint(-1000, -1000)), displayoffset(0), parent_win(parent), tppw_flags(0), thisname(thisname_) {

	tpg=tpanelglobal::Get();

	if(gc.showdeletedtweetsbydefault) {
		tppw_flags |= TPPWF::SHOWDELETED;
	}

	wxBoxSizer* outersizer = new wxBoxSizer(wxVERTICAL);
	headersizer = new wxBoxSizer(wxHORIZONTAL);
	scrollwin = new tpanelscrollwin(this);
	clabel=new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000));
	outersizer->Add(headersizer, 0, wxALL | wxEXPAND, 0);
	headersizer->Add(clabel, 0, wxALL, 2);
	headersizer->AddStretchSpacer();
	auto addbtn = [&](wxWindowID id, wxString name, std::string type, wxButton *& btnref) {
		btnref = new wxButton(this, id, name, wxPoint(-1000, -1000), wxDefaultSize, wxBU_EXACTFIT);
		if(!type.empty()) {
			btnref->Show(false);
			showhidemap.insert(std::make_pair(type, btnref));
		}
		headersizer->Add(btnref, 0, wxALL, 2);
	};
	addbtn(TPPWID_MOREBTN, wxT("More \x25BC"), "more", MoreBtn);
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
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::PopTop() %s START"), GetThisName().c_str());
	#endif
	RemoveIndexIntl(0);
	currentdisp.pop_front();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::PopTop() %s END"), GetThisName().c_str());
	#endif
}

void panelparentwin_base::PopBottom() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::PopBottom() %s START"), GetThisName().c_str());
	#endif
	RemoveIndexIntl(currentdisp.size() - 1);
	currentdisp.pop_back();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::PopBottom() %s END"), GetThisName().c_str());
	#endif
}

void panelparentwin_base::RemoveIndex(size_t offset) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::RemoveIndex(%u) %s START"), offset, GetThisName().c_str());
	#endif
	RemoveIndexIntl(offset);
	currentdisp.erase(std::next(currentdisp.begin(), offset));
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: panelparentwin_base::RemoveIndex(%u) %s END"), offset, GetThisName().c_str());
	#endif
}

void panelparentwin_base::RemoveIndexIntl(size_t offset) {
	wxSizer *sz=sizer->GetItem(offset)->GetSizer();
	if(sz) {
		sz->Clear(true);
		sizer->Remove(offset);
	}
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

void panelparentwin_base::SetNoUpdateFlag() {
	tppw_flags |= TPPWF::NOUPDATEONPUSH;
	if(!(tppw_flags & TPPWF::FROZEN)) {
		tppw_flags |= TPPWF::FROZEN;
		Freeze();
	}
}

void panelparentwin_base::CheckClearNoUpdateFlag() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::CheckClearNoUpdateFlag() %s START"), GetThisName().c_str());
	#endif

	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::CheckClearNoUpdateFlag() %s TPPWF::BATCHTIMERMODE"), GetThisName().c_str());
		#endif
		ResetBatchTimer();
		return;
	}

	if(tppw_flags & TPPWF::NOUPDATEONPUSH) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::CheckClearNoUpdateFlag() %s TPPWF::NOUPDATEONPUSH"), GetThisName().c_str());
		#endif
		scrollwin->Freeze();
		tppw_scrollfreeze sf;
		StartScrollFreeze(sf);
		bool rup = scrollwin->resize_update_pending;
		scrollwin->fit_inside_blocked = true;
		scrollwin->resize_update_pending = true;
		IterateCurrentDisp([](uint64_t id, dispscr_base *scr) {
			scr->CheckRefresh();
		});
		scrollwin->FitInside();
		EndScrollFreeze(sf);
		if(scrolltoid_onupdate) HandleScrollToIDOnUpdate();
		scrollwin->Thaw();
		tppw_flags &= ~TPPWF::NOUPDATEONPUSH;
		scrollwin->resize_update_pending = rup;
		scrollwin->fit_inside_blocked = false;
		if(tppw_flags & TPPWF::FROZEN) {
			tppw_flags &= ~TPPWF::FROZEN;
			Thaw();
		}
	}

	if(tppw_flags & TPPWF::CLABELUPDATEPENDING) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::CheckClearNoUpdateFlag() %s TPPWF::CLABELUPDATEPENDING"), GetThisName().c_str());
		#endif
		UpdateCLabel();
		tppw_flags &= ~TPPWF::CLABELUPDATEPENDING;
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::CheckClearNoUpdateFlag() %s END"), GetThisName().c_str());
	#endif
}

void panelparentwin_base::ResetBatchTimer() {
	tppw_flags |= TPPWF::BATCHTIMERMODE;
	batchtimer.SetOwner(this, TPPWID_TIMER_BATCHMODE);
	batchtimer.Start(BATCH_TIMER_DELAY, wxTIMER_ONE_SHOT);
}

void panelparentwin_base::UpdateBatchTimer() {
	if(!(tppw_flags & TPPWF::NOUPDATEONPUSH)) ResetBatchTimer();
}

uint64_t panelparentwin_base::GetCurrentViewTopID() const {
	int scrollstart;
	scrollwin->GetViewStart(0, &scrollstart);
	for(auto &it : currentdisp) {
		int y;
		it.second->GetPosition(0, &y);
		if(y >= 0) return it.first;
	}
	return 0;
}

void panelparentwin_base::StartScrollFreeze(tppw_scrollfreeze &s) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::StartScrollFreeze(): %s"), GetThisName().c_str());
	#endif
	int scrollstart;
	scrollwin->GetViewStart(0, &scrollstart);
	if((!scrollstart && !displayoffset && !(s.flags & tppw_scrollfreeze::SF::ALWAYSFREEZE)) || currentdisp.size() <= 2)  {
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
				LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::StartScrollFreeze(): %s, Using id: %" wxLongLongFmtSpec "d, extrapixels: %d"), GetThisName().c_str(), (*it).first, y);
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
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::EndScrollFreeze() %s"), GetThisName().c_str());
		#endif
		int y;
		s.scr->GetPosition(0, &y);
		int scrollstart;
		scrollwin->GetViewStart(0, &scrollstart);
		scrollstart+=y-s.extrapixels;
		scrollwin->Scroll(-1, std::max(0, scrollstart));
	}
}

void panelparentwin_base::SetScrollFreeze(tppw_scrollfreeze &s, dispscr_base *scr) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: panelparentwin_base::SetScrollFreeze() %s"), GetThisName().c_str());
	#endif
	s.scr = scr;
	s.extrapixels = 0;
}

mainframe *panelparentwin_base::GetMainframe() {
	return GetMainframeAncestor(this);
}

bool panelparentwin_base::IsSingleAccountWin() const {
	return alist.size() <= 1;
}

void panelparentwin_base::IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const {
	for(auto &it : currentdisp) {
		func(it.first, it.second);
	}
}

void panelparentwin_base::CLabelNeedsUpdating(flagwrapper<PUSHFLAGS> pushflags) {
	if(!(tppw_flags & TPPWF::NOUPDATEONPUSH) && !(pushflags & PUSHFLAGS::SETNOUPDATEFLAG)) UpdateCLabel();
	else tppw_flags |= TPPWF::CLABELUPDATEPENDING;
}

BEGIN_EVENT_TABLE(tpanelparentwin_nt, panelparentwin_base)
EVT_BUTTON(TPPWID_MARKALLREADBTN, tpanelparentwin_nt::markallreadevthandler)
EVT_BUTTON(TPPWID_UNHIGHLIGHTALLBTN, tpanelparentwin_nt::markremoveallhighlightshandler)
EVT_MENU(TPPWID_MARKALLREADBTN, tpanelparentwin_nt::markallreadevthandler)
EVT_MENU(TPPWID_UNHIGHLIGHTALLBTN, tpanelparentwin_nt::markremoveallhighlightshandler)
EVT_BUTTON(TPPWID_MOREBTN, tpanelparentwin_nt::morebtnhandler)
EVT_TIMER(TPPWID_TIMER_BATCHMODE, tpanelparentwin_nt::OnBatchTimerModeTimer)
END_EVENT_TABLE()

tpanelparentwin_nt::tpanelparentwin_nt(const std::shared_ptr<tpanel> &tp_, wxWindow *parent, wxString thisname_)
: panelparentwin_base(parent, false, thisname_), tp(tp_) {
	LogMsgFormat(LOGT::TPANEL, wxT("Creating tweet panel window %s"), wxstrstd(tp->name).c_str());

	tp->twin.push_front(this);
	tpanelparentwinlist.push_front(this);

	clabel->SetLabel(wxT("No Tweets"));
	ShowHideButtons("more", true);
	scrollwin->FitInside();
	FitInside();

	tppw_flags |= TPPWF::BATCHTIMERMODE;

	setupnavbuttonhandlers();
}

tpanelparentwin_nt::~tpanelparentwin_nt() {
	tp->OnTPanelWinClose(this);
	tpanelparentwinlist.remove(this);
}

void tpanelparentwin_nt::PushTweet(const std::shared_ptr<tweet> &t, flagwrapper<PUSHFLAGS> pushflags) {
	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		pushtweetbatchqueue.emplace_back(t, pushflags);
		UpdateBatchTimer();
		return;
	}

	scrollwin->Freeze();
	LogMsgFormat(LOGT::TPANEL, "tpanelparentwin_nt::PushTweet %s, id: %" wxLongLongFmtSpec "d, displayoffset: %d, pushflags: 0x%X, currentdisp: %d, tppw_flags: 0x%X", GetThisName().c_str(), t->id, displayoffset, pushflags, (int) currentdisp.size(), tppw_flags);
	tppw_scrollfreeze sf;
	if(pushflags & PUSHFLAGS::ABOVE) sf.flags = tppw_scrollfreeze::SF::ALWAYSFREEZE;
	StartScrollFreeze(sf);
	uint64_t id=t->id;
	bool recalcdisplayoffset = false;
	if(pushflags & PUSHFLAGS::NOINCDISPOFFSET && currentdisp.empty()) recalcdisplayoffset = true;
	if(displayoffset) {
		if(id>currentdisp.front().first) {
			if(!(pushflags & PUSHFLAGS::ABOVE)) {
				if(!(pushflags & PUSHFLAGS::NOINCDISPOFFSET)) displayoffset++;
				scrollwin->Thaw();
				CLabelNeedsUpdating(pushflags);
				return;
			}
			else if(pushflags & PUSHFLAGS::NOINCDISPOFFSET) recalcdisplayoffset = true;
		}
	}
	if(currentdisp.size()==gc.maxtweetsdisplayinpanel) {
		if(t->id<currentdisp.back().first) {			//off the end of the list
			if(pushflags & PUSHFLAGS::BELOW || pushflags & PUSHFLAGS::USERTL) {
				PopTop();
				displayoffset++;
			}
			else {
				scrollwin->Thaw();
				CLabelNeedsUpdating(pushflags);
				return;
			}
		}
		else PopBottom();					//too many in list, remove the last one
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweet 1, %d, %d, %d"), displayoffset, (int) currentdisp.size(), recalcdisplayoffset);
	#endif
	if(pushflags & PUSHFLAGS::SETNOUPDATEFLAG) tppw_flags |= TPPWF::NOUPDATEONPUSH;
	size_t index=0;
	auto it=currentdisp.begin();
	for(; it!=currentdisp.end(); it++, index++) {
		if(it->first<id) break;	//insert before this iterator
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweet 2, %d, %d, %d, %d"), displayoffset, currentdisp.size(), index, recalcdisplayoffset);
	#endif
	if(recalcdisplayoffset) {
		tweetidset::const_iterator stit = tp->tweetlist.find(id);
		if(stit != tp->tweetlist.end()) displayoffset = std::distance(tp->tweetlist.cbegin(), stit);
		else displayoffset = 0;
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweet 3, %d, %d, %d, %d"), displayoffset, currentdisp.size(), index, recalcdisplayoffset);
	#endif
	tweetdispscr *td = PushTweetIndex(t, index);
	currentdisp.insert(it, std::make_pair(id, td));
	if(pushflags & PUSHFLAGS::CHECKSCROLLTOID) {
		if(tppw_flags & TPPWF::NOUPDATEONPUSH) scrolltoid_onupdate = scrolltoid;
		else if(scrolltoid == id) SetScrollFreeze(sf, td);
	}

	CLabelNeedsUpdating(pushflags);

	if(!(tppw_flags & TPPWF::NOUPDATEONPUSH)) td->ForceRefresh();
	else td->gdb_flags |= tweetdispscr::GDB_F::NEEDSREFRESH;
	EndScrollFreeze(sf);
	scrollwin->Thaw();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweet %s END, %d, %d"), GetThisName().c_str(), displayoffset, currentdisp.size());
	#endif
}

tweetdispscr *tpanelparentwin_nt::PushTweetIndex(const std::shared_ptr<tweet> &t, size_t index) {
	LogMsgFormat(LOGT::TPANEL, "tpanelparentwin_nt::PushTweetIndex, %s, id: %" wxLongLongFmtSpec "d, %d", GetThisName().c_str(), t->id, index);
	wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);

	tweetdispscr *td=new tweetdispscr(t, scrollwin, this, vbox);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweetIndex 1"));
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
			t->user->ImgHalfIsReady(UPDCF::DOWNLOADIMG);
			t->user_recipient->ImgHalfIsReady(UPDCF::DOWNLOADIMG);
			td->bm = new profimg_staticbitmap(scrollwin, t->user->cached_profile_img_half, t->user->id, t->id, GetMainframe(), profimg_staticbitmap::PISBF::HALF);
			td->bm2 = new profimg_staticbitmap(scrollwin, t->user_recipient->cached_profile_img_half, t->user_recipient->id, t->id, GetMainframe(), profimg_staticbitmap::PISBF::HALF);
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
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweetIndex 2"));
	#endif

	vbox->Add(td, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);
	hbox->Add(vbox, 1, wxEXPAND, 0);

	sizer->Insert(index, hbox, 0, wxALL | wxEXPAND, 1);
	td->DisplayTweet();

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweetIndex 3"));
	#endif

	tpanel_subtweet_pending_op::CheckLoadTweetReply(t, vbox, this, td, gc.inlinereplyloadcount, t, td);

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::PushTweetIndex %s END"), GetThisName().c_str());
	#endif

	return td;
}

void tpanelparentwin_nt::RemoveTweet(uint64_t id, flagwrapper<PUSHFLAGS> pushflags) {
	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		removetweetbatchqueue.emplace_back(id, pushflags);
		UpdateBatchTimer();
		return;
	}

	auto update_label = [&]() {
		if(!(tppw_flags & TPPWF::NOUPDATEONPUSH) && !(pushflags & PUSHFLAGS::SETNOUPDATEFLAG)) UpdateCLabel();
		else tppw_flags |= TPPWF::CLABELUPDATEPENDING;
	};

	LogMsgFormat(LOGT::TPANEL, "tpanelparentwin_nt::RemoveTweet %s, id: %" wxLongLongFmtSpec "d, displayoffset: %d, pushflags: 0x%X, currentdisp: %d, tppw_flags: 0x%X",
			GetThisName().c_str(), id, displayoffset, pushflags, (int) currentdisp.size(), tppw_flags);

	if(currentdisp.empty()) {
		update_label();
		return;
	}

	if(id > currentdisp.front().first) {
		if(!(pushflags & PUSHFLAGS::NOINCDISPOFFSET)) displayoffset--;
		update_label();
		return;
	}
	if(id < currentdisp.back().first) {
		update_label();
		return;
	}

	size_t index = 0;
	auto it = currentdisp.begin();
	for(; it != currentdisp.end(); it++, index++) {
		if(it->first == id) {
			scrollwin->Freeze();
			tppw_scrollfreeze sf;
			StartScrollFreeze(sf);

			if(pushflags & PUSHFLAGS::SETNOUPDATEFLAG) tppw_flags |= TPPWF::NOUPDATEONPUSH;
			RemoveIndex(index);
			update_label();

			if(it->second != sf.scr) { // Don't reset scrolling if top of screen is item being removed
				EndScrollFreeze(sf);
			}
			scrollwin->Thaw();
			break;
		}
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LFT_TPANEL, wxT("TCL: tpanelparentwin_nt::RemoveTweet %s END, %d, %d"), GetThisName().c_str(), displayoffset, currentdisp.size());
	#endif
}

void tpanelparentwin_nt::PageUpHandler() {
	if(displayoffset) {
		SetNoUpdateFlag();
		size_t pagemove = std::min((size_t) (gc.maxtweetsdisplayinpanel + 1) / 2, displayoffset);
		uint64_t greaterthanid=currentdisp.front().first;
		LoadMore(pagemove, 0, greaterthanid, PUSHFLAGS::ABOVE | PUSHFLAGS::NOINCDISPOFFSET);
		CheckClearNoUpdateFlag();
	}
	scrollwin->page_scroll_blocked=false;
}
void tpanelparentwin_nt::PageDownHandler() {
	SetNoUpdateFlag();
	size_t curnum=currentdisp.size();
	size_t tweetnum=tp->tweetlist.size();
	if(curnum+displayoffset<tweetnum || tppw_flags & TPPWF::CANALWAYSSCROLLDOWN) {
		size_t pagemove;
		if(tppw_flags & TPPWF::CANALWAYSSCROLLDOWN) pagemove = (gc.maxtweetsdisplayinpanel + 1) / 2;
		else pagemove = std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, tweetnum-(curnum+displayoffset));
		uint64_t lessthanid=currentdisp.back().first;
		LoadMore(pagemove, lessthanid, 0, PUSHFLAGS::BELOW | PUSHFLAGS::NOINCDISPOFFSET);
	}
	scrollwin->page_scroll_blocked=false;
	CheckClearNoUpdateFlag();
}

void tpanelparentwin_nt::PageTopHandler() {
	if(displayoffset) {
		SetNoUpdateFlag();
		size_t pushcount=std::min(displayoffset, (size_t) gc.maxtweetsdisplayinpanel);
		displayoffset=0;
		LoadMore(pushcount, 0, 0, PUSHFLAGS::ABOVE | PUSHFLAGS::NOINCDISPOFFSET);
		CheckClearNoUpdateFlag();
	}
	GenericAction([](tpanelparentwin_nt *tp) {
		tp->scrollwin->Scroll(-1, 0);
	});
}

void tpanelparentwin_nt::JumpToTweetID(uint64_t id) {
	LogMsgFormat(LOGT::TPANEL, "tpanel::JumpToTweetID %s, %" wxLongLongFmtSpec "d, displayoffset: %d, display count: %d, tweets: %d", GetThisName().c_str(), id, displayoffset, (int) currentdisp.size(), (int) tp->tweetlist.size());

	bool alldone = false;

	if(id <= currentdisp.front().first && id >= currentdisp.back().first) {
		for(auto &disp : currentdisp) {
			if(disp.first == id) {
				tppw_scrollfreeze sf;
				SetScrollFreeze(sf, disp.second);
				EndScrollFreeze(sf);
				#if TPANEL_COPIOUS_LOGGING
					LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanel::JumpToTweetID setting scrollfreeze"));
				#endif
				if(GetCurrentViewTopID() == id) alldone = true;  //if this isn't true, load some more tweets below to make it true
				break;
			}
		}
	}

	if(!alldone) {
		tweetidset::const_iterator stit = tp->tweetlist.find(id);
		if(stit == tp->tweetlist.end()) return;

		SetNoUpdateFlag();
		scrollwin->Freeze();

		//this is the offset into the tweetlist set, of the target tweet
		unsigned int targ_offset = std::distance(tp->tweetlist.cbegin(), stit);

		//this is how much above and below the target tweet that we also want to load
		unsigned int offset_up_delta = std::min<unsigned int>(targ_offset, (gc.maxtweetsdisplayinpanel+1)/2);
		unsigned int offset_down_delta = std::min<unsigned int>(tp->tweetlist.size() - targ_offset, gc.maxtweetsdisplayinpanel - offset_up_delta) - 1;

		//these are the new bounds on the current display set, given the offsets above and below the target tweet, above
		uint64_t top_id = *std::prev(stit, offset_up_delta);
		uint64_t bottom_id = *std::next(stit, offset_down_delta);

		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanel::JumpToTweetID targ_offset: %d, oud: %d, odd: %d, ti: %" wxLongLongFmtSpec "d, bi: %" wxLongLongFmtSpec "d"), targ_offset, offset_up_delta, offset_down_delta, top_id, bottom_id);
		#endif

		//get rid of tweets which lie outside the new bounds
		while(!currentdisp.empty() && currentdisp.front().first > top_id) {
			PopTop();
			displayoffset++;
		}
		while(!currentdisp.empty() && currentdisp.back().first < bottom_id) PopBottom();

		if(currentdisp.empty()) displayoffset = 0;

		unsigned int loadcount = offset_up_delta + offset_down_delta + 1 - currentdisp.size();

		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanel::JumpToTweetID displayoffset: %d, loadcount: %d, display count: %d"), displayoffset, loadcount, (int) currentdisp.size());
		#endif

		scrollwin->Thaw();
		if(loadcount) {
			scrolltoid = id;

			//if the new top id is also the top of the existing range, start loading from below the bottom of the existing range
			if(!currentdisp.empty() && top_id == currentdisp.front().first) {
				#if TPANEL_COPIOUS_LOGGING
					LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanel::JumpToTweetID adjusting load bound: %" wxLongLongFmtSpec "d"), currentdisp.back().first);
				#endif
				LoadMore(loadcount, currentdisp.back().first, 0, PUSHFLAGS::ABOVE | PUSHFLAGS::CHECKSCROLLTOID | PUSHFLAGS::NOINCDISPOFFSET);
			}
			else LoadMore(loadcount, top_id + 1, 0, PUSHFLAGS::ABOVE | PUSHFLAGS::CHECKSCROLLTOID | PUSHFLAGS::NOINCDISPOFFSET);
		}
		else CheckClearNoUpdateFlag();
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanel::JumpToTweetID %s END"), GetThisName().c_str());
	#endif
}

void tpanelparentwin_nt::UpdateCLabel() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::UpdateCLabel %s START"), GetThisName().c_str());
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
	ShowHideButtons("more", true);
	headersizer->Layout();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::UpdateCLabel %s END"), GetThisName().c_str());
	#endif
}

void tpanelparentwin_nt::markallreadevthandler(wxCommandEvent &event) {
	MarkSetRead();
}

void tpanelparentwin_nt::MarkSetRead() {
	MarkSetRead(std::move(tp->cids.unreadids));
}

void tpanelparentwin_nt::MarkSetRead(tweetidset &&subset) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::MarkSetRead %s START"), GetThisName().c_str());
	#endif
	tweetidset cached_ids = std::move(subset);
	MarkClearCIDSSetHandler(
		[&](cached_id_sets &cids) -> tweetidset & {
			return cids.unreadids;
		},
		[&](const std::shared_ptr<tweet> &tw) {
			tw->UpdateMarkedAsRead(tp.get());
		},
		cached_ids
	);
	dbupdatetweetsetflagsmsg *msg = new dbupdatetweetsetflagsmsg(std::move(cached_ids), tweet_flags::GetFlagValue('r'), tweet_flags::GetFlagValue('u'));
	DBC_SendMessage(msg);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::MarkSetRead %s END"), GetThisName().c_str());
	#endif
}

void tpanelparentwin_nt::markremoveallhighlightshandler(wxCommandEvent &event) {
	MarkSetUnhighlighted();
}

void tpanelparentwin_nt::MarkSetUnhighlighted() {
	MarkSetUnhighlighted(std::move(tp->cids.highlightids));
}

void tpanelparentwin_nt::MarkSetUnhighlighted(tweetidset &&subset) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::MarkSetUnhighlighted %s START"), GetThisName().c_str());
	#endif
	tweetidset cached_ids = std::move(subset);
	MarkClearCIDSSetHandler(
		[&](cached_id_sets &cids) -> tweetidset & {
			return cids.highlightids;
		},
		[&](const std::shared_ptr<tweet> &tw) {
			tw->flags.Set('H', false);
			UpdateTweet(*tw, false);
		},
		cached_ids
	);
	dbupdatetweetsetflagsmsg *msg = new dbupdatetweetsetflagsmsg(std::move(cached_ids), 0, tweet_flags::GetFlagValue('H'));
	DBC_SendMessage(msg);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::MarkSetUnhighlighted %s END"), GetThisName().c_str());
	#endif
}

void tpanelparentwin_nt::navbuttondispatchhandler(wxCommandEvent &event) {
	btnhandlerlist[event.GetId()](event);
}

void tpanelparentwin_nt::setupnavbuttonhandlers() {
	auto addhandler = [&](int id, std::function<void(wxCommandEvent &event)> f) {
		btnhandlerlist[id] = std::move(f);
		Connect(id, wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(tpanelparentwin_nt::navbuttondispatchhandler));
		Connect(id, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(tpanelparentwin_nt::navbuttondispatchhandler));
	};

	auto cidsendjump = [&](int id, tweetidset cached_id_sets::* cid, bool newest) {
		addhandler(id, [this, cid, newest](wxCommandEvent &event) {
			if(!((tp->cids.*cid).empty())) {
				if(newest) JumpToTweetID(*((tp->cids.*cid).begin()));
				else JumpToTweetID(*((tp->cids.*cid).rbegin()));
			}
		});
	};

	auto cidsnextjump = [&](int id, tweetidset cached_id_sets::* cid, bool newer) {
		addhandler(id, [this, cid, newer](wxCommandEvent &event) {
			uint64_t current = GetCurrentViewTopID();
			tweetidset::iterator iter;
			if(newer) {
				iter = (tp->cids.*cid).lower_bound(current);
				if(iter != (tp->cids.*cid).begin()) --iter;
			}
			else iter = (tp->cids.*cid).upper_bound(current);

			if(iter != (tp->cids.*cid).end()) {
				JumpToTweetID(*iter);
			}
		});
	};

	//returns true on success
	auto getjumpval = [this](uint64_t &value, const wxString &msg, uint64_t lowerbound, uint64_t upperbound) -> bool {
		wxString str = ::wxGetTextFromUser(msg, wxT("Input Number"), wxT(""), this);
		if(!str.IsEmpty()) {
			std::string stdstr = stdstrwx(str);
			char *pos = 0;
			value = strtoull(stdstr.c_str(), &pos, 10);
			if(pos && *pos != 0) {
				::wxMessageBox(wxString::Format(wxT("'%s' does not appear to be a positive integer"), str.c_str()), wxT("Invalid Input"), wxOK | wxICON_EXCLAMATION, this);
			}
			else if(value < lowerbound || value > upperbound) {
				::wxMessageBox(wxString::Format(wxT("'%s' lies outside the range: %" wxLongLongFmtSpec "d - %" wxLongLongFmtSpec "d"), str.c_str(), lowerbound, upperbound), wxT("Invalid Input"), wxOK | wxICON_EXCLAMATION, this);
			}
			else return true;
		}
		return false;
	};

	cidsendjump(TPPWID_NEWESTUNREADBTN, &cached_id_sets::unreadids, true);
	cidsendjump(TPPWID_OLDESTUNREADBTN, &cached_id_sets::unreadids, false);
	cidsendjump(TPPWID_NEWESTHIGHLIGHTEDBTN, &cached_id_sets::highlightids, true);
	cidsendjump(TPPWID_OLDESTHIGHLIGHTEDBTN, &cached_id_sets::highlightids, false);

	cidsnextjump(TPPWID_NEXT_NEWESTUNREADBTN, &cached_id_sets::unreadids, true);
	cidsnextjump(TPPWID_NEXT_OLDESTUNREADBTN, &cached_id_sets::unreadids, false);
	cidsnextjump(TPPWID_NEXT_NEWESTHIGHLIGHTEDBTN, &cached_id_sets::highlightids, true);
	cidsnextjump(TPPWID_NEXT_OLDESTHIGHLIGHTEDBTN, &cached_id_sets::highlightids, false);

	addhandler(TPPWID_JUMPTONUM, [this, getjumpval](wxCommandEvent &event) {
		if(!tp->tweetlist.empty()) {
			uint64_t value;
			size_t maxval = tp->tweetlist.size();
			if(getjumpval(value, wxString::Format(wxT("Enter tweet number to jump to. (1 - %d)"), maxval), 1, maxval)) {
				if(value > tp->tweetlist.size()) value = tp->tweetlist.size(); // this is in case tweetlist somehow shrinks during the call
				auto iter = tp->tweetlist.begin();
				std::advance(iter, value - 1);
				JumpToTweetID(*iter);
			}
		}
	});

	addhandler(TPPWID_JUMPTOID, [this, getjumpval](wxCommandEvent &event) {
		if(!tp->tweetlist.empty()) {
			uint64_t value;
			if(getjumpval(value, wxT("Enter tweet ID to jump to."), 0, std::numeric_limits<uint64_t>::max())) {
				if(tp->tweetlist.find(value) == tp->tweetlist.end()) {
					::wxMessageBox(wxString::Format(wxT("No tweet with ID: %" wxLongLongFmtSpec "d in this panel"), value), wxT("No such tweet"), wxOK | wxICON_EXCLAMATION, this);
				}
				else JumpToTweetID(value);
			}
		}
	});

	auto hidesettogglefunc = [&](int cmdid, flagwrapper<TPPWF> tppwflag, tweetidset cached_id_sets::* setptr, const wxString &logstr) {
		addhandler(cmdid, [this, tppwflag, setptr, logstr](wxCommandEvent &event) {
			tppw_flags ^= tppwflag;
			SetNoUpdateFlag();

			//refresh any currently displayed tweets which are marked as hidden
			IterateCurrentDisp([&](uint64_t id, dispscr_base *scr) {
				if((tp->cids.*setptr).find(id) != (tp->cids.*setptr).end()) {
					#if TPANEL_COPIOUS_LOGGING
						LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::setupnavbuttonhandlers: %s: About to refresh: %" wxLongLongFmtSpec "d, items: %d"), logstr.c_str(), id, (tp->cids.*setptr).size());
					#endif
					static_cast<tweetdispscr *>(scr)->DisplayTweet(false);
				}
			});
			CheckClearNoUpdateFlag();
		});
	};
	hidesettogglefunc(TPPWID_TOGGLEHIDDEN, TPPWF::SHOWHIDDEN, &cached_id_sets::hiddenids, wxT("TPPWID_TOGGLEHIDDEN: Hidden IDs"));
	hidesettogglefunc(TPPWID_TOGGLEHIDEDELETED, TPPWF::SHOWDELETED, &cached_id_sets::deletedids, wxT("TPPWID_TOGGLEHIDEDELETED: Deleted IDs"));
}

void tpanelparentwin_nt::morebtnhandler(wxCommandEvent &event) {
	wxRect btnrect = MoreBtn->GetRect();
	wxMenu pmenu;
	pmenu.Append(TPPWID_TOPBTN, wxT("Jump To &Top"));
	pmenu.Append(TPPWID_JUMPTONUM, wxT("&Jump To Nth Tweet"));
	pmenu.Append(TPPWID_JUMPTOID, wxT("Jump To Tweet &ID"));
	if(!tp->cids.highlightids.empty()) {
		pmenu.AppendSeparator();
		pmenu.Append(TPPWID_UNHIGHLIGHTALLBTN, wxT("Unhighlight All"));
		pmenu.Append(TPPWID_NEWESTHIGHLIGHTEDBTN, wxT("&Newest Highlighted \x2191"));
		pmenu.Append(TPPWID_OLDESTHIGHLIGHTEDBTN, wxT("&Oldest Highlighted \x2193"));
		pmenu.Append(TPPWID_NEXT_NEWESTHIGHLIGHTEDBTN, wxT("Next Newest Highlighted \x21E1"));
		pmenu.Append(TPPWID_NEXT_OLDESTHIGHLIGHTEDBTN, wxT("Next Oldest Highlighted \x21E3"));
	}
	if(!tp->cids.unreadids.empty()) {
		pmenu.AppendSeparator();
		pmenu.Append(TPPWID_MARKALLREADBTN, wxT("Mark All Read"));
		pmenu.Append(TPPWID_NEWESTUNREADBTN, wxT("&Newest Unread \x2191"));
		pmenu.Append(TPPWID_OLDESTUNREADBTN, wxT("&Oldest Unread \x2193"));
		pmenu.Append(TPPWID_NEXT_NEWESTUNREADBTN, wxT("Next Newest Unread \x21E1"));
		pmenu.Append(TPPWID_NEXT_OLDESTUNREADBTN, wxT("Next Oldest Unread \x21E3"));
	}
	pmenu.AppendSeparator();
	wxMenuItem *wmith = pmenu.Append(TPPWID_TOGGLEHIDDEN, wxString::Format(wxT("Show Hidden Tweets (%d)"), tp->cids.hiddenids.size()), wxT(""), wxITEM_CHECK);
	wmith->Check(tppw_flags & TPPWF::SHOWHIDDEN);
	wxMenuItem *wmith2 = pmenu.Append(TPPWID_TOGGLEHIDEDELETED, wxString::Format(wxT("Show Deleted Tweets (%d)"), tp->cids.deletedids.size()), wxT(""), wxITEM_CHECK);
	wmith2->Check(tppw_flags & TPPWF::SHOWDELETED);

	GenericPopupWrapper(this, &pmenu, btnrect.GetLeft(), btnrect.GetBottom());
}

//this does not clear the subset
//note that the tpanel cids should be cleared/modified *before* calling this function
void tpanelparentwin_nt::MarkClearCIDSSetHandler(std::function<tweetidset &(cached_id_sets &)> idsetselector, std::function<void(const std::shared_ptr<tweet> &)> existingtweetfunc, const tweetidset &subset) {
	Freeze();
	tp->TPPWFlagMaskAllTWins(TPPWF::CLABELUPDATEPENDING|TPPWF::NOUPDATEONPUSH, 0);
	MarkTweetIDSetCIDS(subset, tp.get(), idsetselector, true, existingtweetfunc);
	CheckClearNoUpdateFlag_All();
	Thaw();
}

bool tpanelparentwin_nt::IsSingleAccountWin() const {
	return tp->IsSingleAccountTPanel();
}

void tpanelparentwin_nt::EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush) {
	Freeze();
	bool checkupdateflag=false;
	if(setnoupdateonpush) {
		checkupdateflag=!(tppw_flags&TPPWF::NOUPDATEONPUSH);
		SetNoUpdateFlag();
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
	UpdateOwnTweet(t.id, redrawimg);
}

void tpanelparentwin_nt::UpdateOwnTweet(uint64_t id, bool redrawimg) {
	EnumDisplayedTweets([&](tweetdispscr *tds) {
		if(tds->td->id == id || tds->rtid == id) {    //found matching entry

			//don't bother inserting into updatetweetbatchqueue unless we actually have a corresponding tweetdispscr
			//otherwise updatetweetbatchqueue will end up quite bloated
			if(tppw_flags & TPPWF::BATCHTIMERMODE) {
				bool &redrawimgflag = updatetweetbatchqueue[id];
				if(redrawimg) redrawimgflag = true;   // if the flag in updatetweetbatchqueue is already true, don't override it to false
				UpdateBatchTimer();
				return false;
			}

			LogMsgFormat(LOGT::TPANEL, wxT("UpdateOwnTweet: %s, Found Entry %" wxLongLongFmtSpec "d."), GetThisName().c_str(), id);
			tds->DisplayTweet(redrawimg);
		}
		return true;
	}, false);
}

void tpanelparentwin_nt::HandleScrollToIDOnUpdate() {
	for(auto &it : currentdisp) {
		if(it.first == scrolltoid_onupdate) {
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin_nt::HandleScrollToIDOnUpdate() %s"), GetThisName().c_str());
			#endif
			tppw_scrollfreeze sf;
			SetScrollFreeze(sf, it.second);
			EndScrollFreeze(sf);
			break;
		}
	}
	scrolltoid_onupdate = 0;
}

void tpanelparentwin_nt::IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const {
	for(auto &it : currentdisp) {
		func(it.first, it.second);
		tweetdispscr *tds = static_cast<tweetdispscr *>(it.second);
		for(auto &jt : tds->subtweets) {
			if(jt) {
				func(jt->td->id, jt.get());
			}
		}
	}
}

void tpanelparentwin_nt::OnBatchTimerModeTimer(wxTimerEvent& event) {
	tppw_flags &= ~TPPWF::BATCHTIMERMODE;

	raii finaliser([&]() {
		CheckClearNoUpdateFlag();
		tppw_flags |= TPPWF::BATCHTIMERMODE;
	});

	if(pushtweetbatchqueue.empty() && removetweetbatchqueue.empty() && updatetweetbatchqueue.empty() && batchedgenericactions.empty()) {
		return;
	}

	SetNoUpdateFlag();

	for(auto &it : removetweetbatchqueue) {
		RemoveTweet(it.first, it.second);
	}
	removetweetbatchqueue.clear();

	if(!pushtweetbatchqueue.empty()) {
		struct simulation_disp {
			uint64_t id;
			std::pair<std::shared_ptr<tweet>, flagwrapper<PUSHFLAGS> > *pushptr;
		};
		std::list<simulation_disp> simulation_currentdisp;
		tweetidset gotids;
		for(auto &it : currentdisp) {
			simulation_currentdisp.push_back({ it.first, 0 });
			gotids.insert(it.first);
		}

		for(auto &item : pushtweetbatchqueue) {
			uint64_t id = item.first->id;
			flagwrapper<PUSHFLAGS> pushflags = item.second;

			auto result = gotids.insert(id);
			if(!result.second) {
				//whoops, insertion didn't take place
				//hence id was already there
				//this is a duplicate push, and should be discarded
				continue;
			}

			if(simulation_currentdisp.size() == gc.maxtweetsdisplayinpanel) {
				if(id < simulation_currentdisp.back().id) {    //off the end of the list
					if(pushflags & PUSHFLAGS::BELOW || pushflags & PUSHFLAGS::USERTL) {
						simulation_currentdisp.pop_front();
					}
					else continue;
				}
				else simulation_currentdisp.pop_back();    //too many in list, remove the last one
			}

			size_t index = 0;
			auto it = simulation_currentdisp.begin();
			for(; it != simulation_currentdisp.end(); it++, index++) {
				if(it->id < id) break;	//insert before this iterator
			}

			simulation_currentdisp.insert(it, { id, &item });
		}

		size_t pushcount = 0;
		for(auto &it : simulation_currentdisp) {
			if(it.pushptr) {
				PushTweet(it.pushptr->first, it.pushptr->second);
				updatetweetbatchqueue.erase(it.pushptr->first->id);  //don't bother updating items we've just pushed
				pushcount++;
			}
		}

		LogMsgFormat(LOGT::TPANEL, wxT("tpanelparentwin_nt::OnBatchTimerModeTimer: %s, Reduced %u pushes to %u pushes."), GetThisName().c_str(), pushtweetbatchqueue.size(), pushcount);

		simulation_currentdisp.clear();
		pushtweetbatchqueue.clear();
	}

	for(auto &it : updatetweetbatchqueue) {
		UpdateOwnTweet(it.first, it.second);
	}
	updatetweetbatchqueue.clear();

	for(auto &it : batchedgenericactions) {
		it(this);
	}
	batchedgenericactions.clear();
}

void tpanelparentwin_nt::GenericAction(std::function<void(tpanelparentwin_nt *)> func) {
	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		batchedgenericactions.emplace_back(std::move(func));
		UpdateBatchTimer();
	}
	else {
		func(this);
	}
}

tweetdispscr_mouseoverwin *tpanelparentwin_nt::MakeMouseOverWin() {
	if(!mouseoverwin) mouseoverwin = new tweetdispscr_mouseoverwin(scrollwin, this);
	return mouseoverwin;
}

BEGIN_EVENT_TABLE(tpanelparentwin, tpanelparentwin_nt)
	EVT_MENU(TPPWID_DETACH, tpanelparentwin::tabdetachhandler)
	EVT_MENU(TPPWID_SPLIT, tpanelparentwin::tabsplitcmdhandler)
	EVT_MENU(TPPWID_DUP, tpanelparentwin::tabduphandler)
	EVT_MENU(TPPWID_DETACHDUP, tpanelparentwin::tabdetachedduphandler)
	EVT_MENU(TPPWID_CLOSE, tpanelparentwin::tabclosehandler)
END_EVENT_TABLE()

tpanelparentwin::tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select, wxString thisname_)
: tpanelparentwin_nt(tp_, parent, thisname_.empty() ? wxT("tpanelparentwin for ") + wxstrstd(tp_->name) : thisname_), owner(parent) {

	parent->auib->AddPage(this, wxstrstd(tp->dispname), select);
	LoadMore(gc.maxtweetsdisplayinpanel);
}

//if lessthanid is non-zero, is an exclusive upper id limit, iterate downwards
//if greaterthanid, is an exclusive lower limit, iterate upwards
//cannot set both
//if neither set: start at highest in set and iterate down
void tpanelparentwin::LoadMore(unsigned int n, uint64_t lessthanid, uint64_t greaterthanid, flagwrapper<PUSHFLAGS> pushflags) {
	dbseltweetmsg *loadmsg=0;

	LogMsgFormat(LOGT::TPANEL, "tpanelparentwin::LoadMore %s called with n: %d, lessthanid: %" wxLongLongFmtSpec "d, greaterthanid: %" wxLongLongFmtSpec "d, pushflags: 0x%X", GetThisName().c_str(), n, lessthanid, greaterthanid, pushflags);

	SetNoUpdateFlag();

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

		std::shared_ptr<tweet> tobj = ad.GetTweetById(*stit);
		if(CheckFetchPendingSingleTweet(tobj, std::shared_ptr<taccount>(), &loadmsg)) {
			PushTweet(tobj, pushflags);
		}
		else {
			MarkPending_TPanelMap(tobj, this, pushflags);
		}

		if(revdir) {
			if(stit == tp->tweetlist.cbegin()) break;
			--stit;
		}
		else ++stit;
	}
	if(loadmsg) {
		if(!DBC_AllMediaEntitiesLoaded()) loadmsg->flags |= DBSTMF::PULLMEDIA;
		loadmsg->flags |= DBSTMF::CLEARNOUPDF;
		DBC_PrepareStdTweetLoadMsg(loadmsg);
		DBC_SendMessage(loadmsg);
	}
	if(currentlogflags&LOGT::PENDTRACE) dump_tweet_pendings(LOGT::PENDTRACE, wxT(""), wxT("\t"));

	CheckClearNoUpdateFlag();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin::LoadMore %s END"), GetThisName().c_str());
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
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin::UpdateCLabel %s START"), GetThisName().c_str());
	#endif
	tpanelparentwin_nt::UpdateCLabel();
	int pageid = owner->auib->GetPageIndex(this);
	int unreadcount = tp->cids.unreadids.size();
	if(!unreadcount) {
		owner->auib->SetPageText(pageid, wxstrstd(tp->dispname));
		if((tpw_flags & TPWF::UNREADBITMAPDISP)) {
			owner->auib->SetPageBitmap(pageid, wxNullBitmap);
			tpw_flags &= ~TPWF::UNREADBITMAPDISP;
		}
	}
	else {
		owner->auib->SetPageText(pageid, wxString::Format(wxT("%d - %s"), unreadcount, wxstrstd(tp->dispname).c_str()));
		if(!(tpw_flags & TPWF::UNREADBITMAPDISP)) {
			owner->auib->SetPageBitmap(pageid, tpg->multiunreadicon);
			tpw_flags |= TPWF::UNREADBITMAPDISP;
		}
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelparentwin::UpdateCLabel %s END"), GetThisName().c_str());
	#endif
}

BEGIN_EVENT_TABLE(tpanelparentwin_user, panelparentwin_base)
END_EVENT_TABLE()

std::multimap<uint64_t, tpanelparentwin_user*> tpanelparentwin_user::pendingmap;

tpanelparentwin_user::tpanelparentwin_user(wxWindow *parent, wxString thisname_)
: panelparentwin_base(parent, thisname_) { }

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
		SetNoUpdateFlag();
		size_t pagemove=std::min((size_t) (gc.maxtweetsdisplayinpanel+1)/2, displayoffset);
		size_t curnum=currentdisp.size();
		size_t bottomdrop=std::min(curnum, (size_t) (curnum+pagemove-gc.maxtweetsdisplayinpanel));
		for(size_t i=0; i<bottomdrop; i++) PopBottom();
		auto it=userlist.begin()+displayoffset;
		for(unsigned int i=0; i<pagemove; i++) {
			it--;
			displayoffset--;
			const std::shared_ptr<userdatacontainer> &u=*it;
			if(u->IsReady(UPDCF::DOWNLOADIMG)) UpdateUser(u, displayoffset);
		}
		CheckClearNoUpdateFlag();
	}
	scrollwin->page_scroll_blocked=false;
}
void tpanelparentwin_user::PageDownHandler() {
	SetNoUpdateFlag();
	size_t curnum=currentdisp.size();
	size_t num=ItemCount();
	if(curnum+displayoffset<num || tppw_flags&TPPWF::CANALWAYSSCROLLDOWN) {
		size_t pagemove;
		if(tppw_flags&TPPWF::CANALWAYSSCROLLDOWN) pagemove=(gc.maxtweetsdisplayinpanel+1)/2;
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
		SetNoUpdateFlag();
		size_t pushcount=std::min(displayoffset, (size_t) gc.maxtweetsdisplayinpanel);
		ssize_t bottomdrop=((ssize_t) pushcount+currentdisp.size())-gc.maxtweetsdisplayinpanel;
		if(bottomdrop>0) {
			for(ssize_t i=0; i<bottomdrop; i++) PopBottom();
		}
		displayoffset=0;
		size_t i=0;
		for(auto it=userlist.begin(); it!=userlist.end() && pushcount; ++it, --pushcount, i++) {
			const std::shared_ptr<userdatacontainer> &u=*it;
			if(u->IsReady(UPDCF::DOWNLOADIMG)) UpdateUser(u, i);
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
	if(u->IsReady(UPDCF::DOWNLOADIMG|UPDCF::USEREXPIRE)) {
		wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
		userdispscr *td=new userdispscr(u, scrollwin, this, hbox);

		td->bm = new profimg_staticbitmap(scrollwin, u->cached_profile_img, u->id, 0, GetMainframe());
		hbox->Add(td->bm, 0, wxALL, 2);

		hbox->Add(td, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);

		sizer->Insert(index, hbox, 0, wxALL | wxEXPAND, 1);
		currentdisp.insert(pos, std::make_pair(u->id, td));
		td->Display();
		CLabelNeedsUpdating(0);
		if(!(tppw_flags&TPPWF::NOUPDATEONPUSH)) td->ForceRefresh();
		else td->gdb_flags |= tweetdispscr::GDB_F::NEEDSREFRESH;
		return false;
	}
	else {
		auto pit = pendingmap.equal_range(u->id);
		for(auto it = pit.first; it != pit.second; ++it) {
			if((*it).second == this) {
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
: wxScrolledWindow(parent_, wxID_ANY, wxPoint(-1000, -1000)), parent(parent_), resize_update_pending(false), page_scroll_blocked(false), fit_inside_blocked(false) {

	thisname = wxT("tpanelscrollwin for ") + parent_->GetThisName();
}

void tpanelscrollwin::resizehandler(wxSizeEvent &event) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelscrollwin::resizehandler: %s, %d, %d"), GetThisName().c_str(), event.GetSize().GetWidth(), event.GetSize().GetHeight());
	#endif
}

void tpanelscrollwin::resizemsghandler(wxCommandEvent &event) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelscrollwin::resizemsghandler %s"), GetThisName().c_str());
	#endif
	tppw_scrollfreeze sf;
	parent->StartScrollFreeze(sf);
	FitInside();
	resize_update_pending=false;
	parent->EndScrollFreeze(sf);
	Thaw();
	Update();
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
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANEL, wxT("TCL: tpanelscrollwin::OnScrollHandler %s, %d %d %d %d %d"), GetThisName().c_str(), y, sy, wy, cy, endpos);
	#endif
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

tpanelparentwin_usertweets::tpanelparentwin_usertweets(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc_, RBFS_TYPE type_, wxString thisname_)
: tpanelparentwin_nt(MkUserTweetTPanel(user_, type_), parent, thisname_.empty() ? wxString::Format(wxT("tpanelparentwin_usertweets for: %" wxLongLongFmtSpec "d"), user_->id) : thisname_),
user(user_), getacc(getacc_), havestarted(false), type(type_) {

	tppw_flags|=TPPWF::CANALWAYSSCROLLDOWN;
}

tpanelparentwin_usertweets::~tpanelparentwin_usertweets() {
	usertpanelmap.erase(std::make_pair(user->id, type));
}

std::shared_ptr<tpanel> tpanelparentwin_usertweets::MkUserTweetTPanel(const std::shared_ptr<userdatacontainer> &user, RBFS_TYPE type_) {
	std::shared_ptr<tpanel> &tp=usertpanelmap[std::make_pair(user->id, type_)];
	if(!tp) {
		tp=tpanel::MkTPanel("___UTL_" + std::to_string(user->id) + "_" + std::to_string((size_t) type_), "User Timeline: @" + user->GetUser().screen_name, TPF::DELETEONWINCLOSE|TPF::USER_TIMELINE);
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

//if lessthanid is non-zero, is an exclusive upper id limit, iterate downwards
//if greaterthanid, is an exclusive lower limit, iterate upwards
//cannot set both
//if neither set: start at highest in set and iterate down
void tpanelparentwin_usertweets::LoadMore(unsigned int n, uint64_t lessthanid, uint64_t greaterthanid, flagwrapper<PUSHFLAGS> pushflags) {
	std::shared_ptr<taccount> tac = getacc(*this);
	if(!tac) return;
	SetNoUpdateFlag();

	tweetidset::const_iterator stit;
	bool revdir = false;
	if(lessthanid) stit = tp->tweetlist.upper_bound(lessthanid);  //finds the first id *less than* lessthanid
	else if(greaterthanid) {
		stit = tp->tweetlist.lower_bound(greaterthanid);          //finds the first id *greater than or equal to* greaterthanid
		if(*stit == greaterthanid) --stit;
		revdir = true;
	}
	else stit = tp->tweetlist.cbegin();

	unsigned int numleft = n;
	uint64_t load_lessthanid = lessthanid;
	uint64_t load_greaterthanid = greaterthanid;

	while(numleft) {
		if(stit == tp->tweetlist.cend()) break;

		std::shared_ptr<tweet> t = ad.GetTweetById(*stit);
		if(tac->CheckMarkPending(t)) PushTweet(t, PUSHFLAGS::USERTL | pushflags);
		else MarkPending_TPanelMap(t, 0, PUSHFLAGS::USERTL | pushflags, &tp);

		if(revdir) {
			if((*stit) > load_greaterthanid) load_greaterthanid = *stit;
			if(stit == tp->tweetlist.cbegin()) break;
			--stit;
		}
		else {
			if((*stit) < load_lessthanid) load_lessthanid = *stit;
			++stit;
		}
		numleft--;
	}
	if(numleft) {
		uint64_t lower_id = 0;
		uint64_t upper_id = 0;
		if(load_lessthanid) {
			upper_id = load_lessthanid - 1;
		}
		else if(load_greaterthanid) {
			lower_id = load_greaterthanid;
		}
		else {
			if(tp->tweetlist.begin() != tp->tweetlist.end()) lower_id = *(tp->tweetlist.begin());
		}
		tac->SetGetTwitCurlExtHook([&](twitcurlext *tce) { tce->mp = this; });
		tac->StartRestGetTweetBackfill(lower_id /*lower limit, exclusive*/, upper_id /*upper limit, inclusive*/, numleft, type, user->id);
		tac->ClearGetTwitCurlExtHook();
	}

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

tpanelparentwin_userproplisting::tpanelparentwin_userproplisting(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent, std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc_, CS_ENUMTYPE type_, wxString thisname_)
: tpanelparentwin_user(parent, thisname_.empty() ? wxString::Format(wxT("tpanelparentwin_userproplisting for: %" wxLongLongFmtSpec "d"), user_->id) : thisname_), user(user_), getacc(getacc_), havestarted(false), type(type_) {
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
	SetNoUpdateFlag();

	bool querypendings=false;
	size_t index=userlist.size();
	for(size_t i=0; i<n && index<useridlist.size(); i++, index++) {
		std::shared_ptr<userdatacontainer> u=ad.GetUserContainerById(useridlist[index]);
		if(PushBackUser(u)) {
			u->udc_flags|=UDC::CHECK_USERLISTWIN;
			tac->pendingusers[u->id]=u;
			querypendings=true;
		}
	}
	if(querypendings) {
		tac->StartRestQueryPendings();
	}

	CheckClearNoUpdateFlag();
}

void tpanelparentwin_userproplisting::NotifyRequestFailed() {
	failed = true;
	havestarted = false;
	clabel->SetLabel(wxT("Lookup Failed"));
}

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsg(LOGT::TPANEL, wxT("TCL: RedirectMouseWheelEvent"));
	#endif
	wxWindow *wind=wxFindWindowAtPoint(wxGetMousePosition() /*event.GetPosition()*/);
	while(wind) {
		if(wind!=avoid && std::count(tpanelparentwinlist.begin(), tpanelparentwinlist.end(), wind)) {
			tpanelparentwin *tppw=(tpanelparentwin*) wind;
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANEL, wxT("TCL: RedirectMouseWheelEvent: Dispatching to %s"), wxstrstd(tppw->tp->name).c_str());
			#endif
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
			wxRichTextAttr style;
			td.GetStyle(td.reltimestart, style);
			td.SetDefaultStyle(style);
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
	if(owner || !(pisb_flags&PISBF::DONTUSEDEFAULTMF)) {
		wxMenu menu;
		int nextid=tweetactmenustartid;
		tamd.clear();
		AppendUserMenuItems(menu, tamd, nextid, ad.GetUserContainerById(userid), ad.GetTweetById(tweetid));
		GenericPopupWrapper(this, &menu);
	}
}

void profimg_staticbitmap::OnTweetActMenuCmd(wxCommandEvent &event) {
	mainframe *mf = owner;
	if(!mf && mainframelist.size() && !(pisb_flags&PISBF::DONTUSEDEFAULTMF)) mf = mainframelist.front();
	TweetActMenuAction(tamd, event.GetId(), mf);
}

tpanelglobal::tpanelglobal() : arrow_dim(0) {
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

void EnumAllDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		(*it)->EnumDisplayedTweets(func, setnoupdateonpush);
	}
}

void UpdateAllTweets(bool redrawimg, bool resethighlight) {
	EnumAllDisplayedTweets([&](tweetdispscr *tds) {
		if(resethighlight) tds->tds_flags &= ~TDSF::HIGHLIGHT;
		tds->DisplayTweet(redrawimg);
		return true;
	}, true);
}

void UpdateUsersTweet(uint64_t userid, bool redrawimg) {
	EnumAllDisplayedTweets([&](tweetdispscr *tds) {
		bool found=false;
		if((tds->td->user && tds->td->user->id==userid)
		|| (tds->td->user_recipient && tds->td->user_recipient->id==userid)) found=true;
		if(tds->td->rtsrc) {
			if((tds->td->rtsrc->user && tds->td->rtsrc->user->id==userid)
			|| (tds->td->rtsrc->user_recipient && tds->td->rtsrc->user_recipient->id==userid)) found=true;
		}
		if(found) {
			LogMsgFormat(LOGT::TPANEL, wxT("UpdateUsersTweet: Found Entry %" wxLongLongFmtSpec "d."), tds->td->id);
			tds->DisplayTweet(redrawimg);
		}
		return true;
	}, true);
}

void UpdateTweet(const tweet &t, bool redrawimg) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		(*it)->UpdateOwnTweet(t, redrawimg);
	}
}
