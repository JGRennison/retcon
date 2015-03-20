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
#include "tpanel.h"
#include "tpanel-pimpl.h"
#include "tpanel-data.h"
#include "tpg.h"
#include "tpanel-aux.h"
#include "dispscr.h"
#include "twit.h"
#include "taccount.h"
#include "utf8.h"
#include "res.h"
#include "version.h"
#include "log.h"
#include "log-util.h"
#include "alldata.h"
#include "mainui.h"
#include "twitcurlext.h"
#include "userui.h"
#include "db.h"
#include "util.h"
#include "raii.h"
#include "bind_wxevt.h"
#include "filter/filter-dlg.h"
#include "tpanel-wid.h"
#include "map.h"
#include <wx/choicdlg.h>
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <forward_list>
#include <algorithm>
#include <deque>

#ifndef TPANEL_COPIOUS_LOGGING
#define TPANEL_COPIOUS_LOGGING 0
#endif

std::forward_list<tpanelparentwin_nt*> tpanelparentwinlist;

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

static void DeleteTPanel(std::shared_ptr<tpanel> tp, optional_observer_ptr<undo::item> undo_item) {
	if(undo_item) {
		const tweetidset &ids = tp->tweetlist;
		const std::string &panel_name = tp->name;
		const std::string &panel_dispname = tp->dispname;
		const flagwrapper<TPF> &flags = tp->flags;
		undo_item->AppendAction(std::unique_ptr<undo::generic_action>(new undo::generic_action(
			[ids, panel_name, panel_dispname, flags]() mutable {
				std::shared_ptr<tpanel> newtp = tpanel::MkTPanel(std::move(panel_name), std::move(panel_dispname), flags);
				if(newtp->tweetlist.empty()) {
					// this is an empty panel, just move the id set in
					newtp->tweetlist = std::move(ids);
				}
				else {
					newtp->tweetlist.insert(ids.begin(), ids.end());
				}
				newtp->ReinitialiseState();

				if(newtp->twin.empty()) {
					// try to create a panel window in the current mainframe, if no panel windows exist already
					optional_observer_ptr<mainframe> mf = mainframe::GetLastMenuOpenedMainframe();
					if(mf) {
						newtp->MkTPanelWin(mf.get(), true);
					}
				}
			}
		)));
	}

	//Use temporary vector as tpanel list may be modified as a result of sending close message
	//Having the list modified from under us whilst iterating would be bad news
	std::vector<tpanelparentwin *> windows;
	for(auto &win : tp->twin) {
		tpanelparentwin *tpw = dynamic_cast<tpanelparentwin *>(win);
		if(tpw) windows.push_back(tpw);
	}
	for(auto &win : windows) {
		wxCommandEvent evt;
		win->pimpl()->tabclosehandler(evt);
	}
	ad.tpanels.erase(tp->name);
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
	DestroyMenuContents(menuP);
	map.clear();

	int nextid = tpanelmenustartid;
	PerAccTPanelMenu(menuP, map, nextid, TPF::AUTO_ALLACCS | TPF::DELETEONWINCLOSE, 0);
	menuP->AppendSeparator();

	for(auto &it : alist) {
		wxMenu *submenu = new wxMenu;
		menuP->AppendSubMenu(submenu, it->dispname);
		PerAccTPanelMenu(submenu, map, nextid, TPF::DELETEONWINCLOSE, it->dbindex);
	}
	menuP->AppendSeparator();

	auto dmsetmap = GetDMConversationMap();
	if(!dmsetmap.empty()) {
		wxMenu *submenu = new wxMenu;
		menuP->AppendSubMenu(submenu, wxT("DM Conversations"));
		for(auto &it : dmsetmap) {
			dm_conversation_map_item &item = it.second;
			udc_ptr &u = item.u;
			map[nextid] = [u](mainframe *parent) {
				auto tp = tpanel::MkTPanel("", "", TPF::DELETEONWINCLOSE, { }, { { TPFU::DMSET, u } });
				tp->MkTPanelWin(parent, true);
			};
			submenu->Append(nextid++, wxString::Format(wxT("@%s (%u)"), wxstrstd(u->GetUser().screen_name).c_str(), item.index->ids.size()));
		};
		menuP->AppendSeparator();
	}

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
		for(size_t i = 0; ; i++) {    //stop when no such tpanel exists with the given name
			default_name = string_format("Unnamed Panel %u", i);
			if(ad.tpanels.find(tpanel::ManualName(default_name)) == ad.tpanels.end()) break;
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

					DeleteTPanel(tp, tp->MakeUndoItem("delete panel"));
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
	std::deque<tpanel_auto_udc> tmpltpudcautos;

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

	auto add_udc_item = [&](flagwrapper<TPFU> tpfu, udc_ptr_p u, const wxString &str) {
		choices.Add(str);
		tmpltpudcautos.emplace_back();
		tmpltpudcautos.back().autoflags = tpfu;
		tmpltpudcautos.back().u = u;
	};

	auto dmsetmap = GetDMConversationMap();
	for(auto &it : dmsetmap) {
		dm_conversation_map_item &item = it.second;
		udc_ptr &u = item.u;
		add_udc_item(TPFU::DMSET, u, wxString::Format(wxT("DM Conversation: @%s (%u)"), wxstrstd(u->GetUser().screen_name).c_str(), item.index->ids.size()));
	}

	::wxGetMultipleChoices(selections, wxT(""), wxT("Select Accounts and Feed Types"), choices, parent, -1, -1, false);

	if(selections.GetCount() == 0) {
		return;
	}

	std::vector<tpanel_auto> tpautos;
	std::vector<tpanel_auto_udc> tpudcautos;

	for(size_t i = 0; i < selections.GetCount(); i++) {
		int index = selections.Item(i);
		if(index >= (int) tmpltpautos.size()) {
			tpudcautos.emplace_back(tmpltpudcautos[index - tmpltpautos.size()]);
		}
		else {
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
	}

	if(tpautos.size() || tmpltpudcautos.size()) {
		auto tp = tpanel::MkTPanel("", "", flags, std::move(tpautos), std::move(tpudcautos));
		tp->MkTPanelWin(parent, true);
	}
}

void TPanelMenuAction(tpanelmenudata &map, int curid, mainframe *parent) {
	auto &f = map[curid];
	if(f) f(parent);
}

void CheckClearNoUpdateFlag_All() {
	for(auto &it : tpanelparentwinlist) {
		it->CheckClearNoUpdateFlag();
	}
}

void SetNoUpdateFlag_All() {
	for(auto &it : tpanelparentwinlist) {
		it->SetNoUpdateFlag();
	}
}

DEFINE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT)
DEFINE_EVENT_TYPE(wxextTP_PAGEUP_EVENT)
DEFINE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT)

BEGIN_EVENT_TABLE(panelparentwin_base_impl, bindwxevt)
	EVT_COMMAND(wxID_ANY, wxextTP_PAGEUP_EVENT, panelparentwin_base_impl::pageupevthandler)
	EVT_COMMAND(wxID_ANY, wxextTP_PAGEDOWN_EVENT, panelparentwin_base_impl::pagedownevthandler)
	EVT_BUTTON(TPPWID_TOPBTN, panelparentwin_base_impl::pagetopevthandler)
	EVT_MENU(TPPWID_TOPBTN, panelparentwin_base_impl::pagetopevthandler)
END_EVENT_TABLE()

const panelparentwin_base_impl *panelparentwin_base::pimpl() const {
	return pimpl_ptr.get();
}

panelparentwin_base::panelparentwin_base(wxWindow *parent, bool fitnow, wxString thisname_, panelparentwin_base_impl *privimpl )
: wxPanel(parent, wxID_ANY, wxPoint(-1000, -1000)) {
	evtbinder.reset(new bindwxevt_win(this));

	if(!privimpl) privimpl = new panelparentwin_base_impl(this);
	pimpl_ptr.reset(privimpl);
	PushEventHandler(pimpl());
	pimpl()->thisname = thisname_;
	pimpl()->parent_win = parent;
	pimpl()->tpg = tpanelglobal::Get();

	if(gc.showdeletedtweetsbydefault) {
		pimpl()->tppw_flags |= TPPWF::SHOWDELETED;
	}

	wxBoxSizer* outersizer = new wxBoxSizer(wxVERTICAL);
	pimpl()->headersizer = new wxBoxSizer(wxHORIZONTAL);
	pimpl()->clabel = new wxStaticText(this, wxID_ANY, wxT(""), wxPoint(-1000, -1000));
	outersizer->Add(pimpl()->headersizer, 0, wxALL | wxEXPAND, 0);
	pimpl()->headersizer->Add(pimpl()->clabel, 0, wxALL, 2);
	pimpl()->headersizer->AddStretchSpacer();
	auto addbtn = [&](wxWindowID id, wxString name, std::string type, wxButton *& btnref) {
		btnref = new wxButton(this, id, name, wxPoint(-1000, -1000), wxDefaultSize, wxBU_EXACTFIT);
		if(!type.empty()) {
			btnref->Show(false);
			pimpl()->showhidemap.insert(std::make_pair(type, btnref));
		}
		pimpl()->headersizer->Add(btnref, 0, wxALL, 2);
	};
	addbtn(TPPWID_MOREBTN, wxT("More \x25BC"), "more", pimpl()->MoreBtn);
	addbtn(TPPWID_MARKALLREADBTN, wxT("Mark All Read"), "unread", pimpl()->MarkReadBtn);
	addbtn(TPPWID_NEWESTUNREADBTN, wxT("Newest Unread \x2191"), "unread", pimpl()->NewestUnreadBtn);
	addbtn(TPPWID_OLDESTUNREADBTN, wxT("Oldest Unread \x2193"), "unread", pimpl()->OldestUnreadBtn);
	addbtn(TPPWID_UNHIGHLIGHTALLBTN, wxT("Unhighlight All"), "unhighlightall", pimpl()->UnHighlightBtn);
	pimpl()->headersizer->Add(new wxButton(this, TPPWID_TOPBTN, wxT("Top \x2191"), wxPoint(-1000, -1000), wxDefaultSize, wxBU_EXACTFIT), 0, wxALL, 2);

	pimpl()->scrollbar = new tpanelscrollbar(this);
	pimpl()->scrollpane = new tpanelscrollpane(this);
	pimpl()->scrollbar->set(pimpl()->scrollpane);
	wxBoxSizer* lowerhsizer = new wxBoxSizer(wxHORIZONTAL);
	lowerhsizer->Add(pimpl()->scrollpane, 1, wxEXPAND, 0);
	lowerhsizer->Add(pimpl()->scrollbar, 0, wxEXPAND, 0);
	outersizer->Add(lowerhsizer, 1, wxALL | wxEXPAND, 2);

	SetSizer(outersizer);
	if(fitnow) {
		FitInside();
	}
}

panelparentwin_base::~panelparentwin_base() {
	RemoveEventHandler(pimpl());
}

wxString panelparentwin_base::GetThisName() const {
	return pimpl()->GetThisName();
}

wxString panelparentwin_base_impl::GetThisName() const {
	return thisname;
}

void panelparentwin_base_impl::ShowHideButtons(std::string type, bool show) {
	auto iterpair = showhidemap.equal_range(type);
	for(auto it = iterpair.first; it != iterpair.second; ++it) it->second->Show(show);
}

tpanel_disp_item *panelparentwin_base_impl::CreateItemAtPosition(tpanel_disp_item_list::iterator iter, uint64_t id) {
	tpanel_item *item = new tpanel_item(scrollpane);

	auto newit = currentdisp.insert(iter, { id, nullptr, item });
	return &(*newit);
}

void panelparentwin_base_impl::PopTop() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::PopTop() %s START", cstr(GetThisName()));
	#endif
	RemoveIndexIntl(0);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::PopTop() %s END", cstr(GetThisName()));
	#endif
}

void panelparentwin_base_impl::PopBottom() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::PopBottom() %s START", cstr(GetThisName()));
	#endif
	RemoveIndexIntl(currentdisp.size() - 1);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::PopBottom() %s END", cstr(GetThisName()));
	#endif
}

void panelparentwin_base_impl::RemoveIndex(size_t offset) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::RemoveIndex(%u) %s START", offset, cstr(GetThisName()));
	#endif
	RemoveIndexIntl(offset);
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::RemoveIndex(%u) %s END", offset, cstr(GetThisName()));
	#endif
}

void panelparentwin_base_impl::RemoveIndexIntl(size_t offset) {
	auto toremove = std::next(currentdisp.begin(), offset);
	toremove->disp->PanelRemoveEvt();
	toremove->item->Destroy();
	currentdisp.erase(toremove);
}

void panelparentwin_base_impl::pageupevthandler(wxCommandEvent &event) {
	PageUpHandler();
}
void panelparentwin_base_impl::pagedownevthandler(wxCommandEvent &event) {
	PageDownHandler();
}
void panelparentwin_base_impl::pagetopevthandler(wxCommandEvent &event) {
	PageTopHandler();
}

void panelparentwin_base::UpdateCLabel() {
	pimpl()->UpdateCLabel();
}

void panelparentwin_base::UpdateCLabelLater() {
	pimpl()->UpdateCLabelLater();
}

void panelparentwin_base_impl::UpdateCLabelLater() {
	SetClabelUpdatePendingFlag();
	UpdateBatchTimer();
}

void panelparentwin_base::SetNoUpdateFlag() {
	pimpl()->SetNoUpdateFlag();
}

void panelparentwin_base_impl::SetNoUpdateFlag() {
	tppw_flags |= TPPWF::NOUPDATEONPUSH;
	if(!(tppw_flags & TPPWF::FROZEN)) {
		tppw_flags |= TPPWF::FROZEN;
		base()->Freeze();
	}
}

void panelparentwin_base::SetClabelUpdatePendingFlag() {
	pimpl()->SetClabelUpdatePendingFlag();
}

void panelparentwin_base_impl::SetClabelUpdatePendingFlag() {
	tppw_flags |= TPPWF::CLABELUPDATEPENDING;
}

void panelparentwin_base::CheckClearNoUpdateFlag() {
	pimpl()->CheckClearNoUpdateFlag();
}

void panelparentwin_base_impl::CheckClearNoUpdateFlag() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::CheckClearNoUpdateFlag() %s START", cstr(GetThisName()));
	#endif

	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::CheckClearNoUpdateFlag() %s TPPWF::BATCHTIMERMODE", cstr(GetThisName()));
		#endif
		ResetBatchTimer();
		return;
	}

	if(tppw_flags & TPPWF::NOUPDATEONPUSH) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::CheckClearNoUpdateFlag() %s TPPWF::NOUPDATEONPUSH", cstr(GetThisName()));
		#endif
		scrollpane->Freeze();
		bool rup = scrollpane->resize_update_pending;
		scrollpane->resize_update_pending = true;
		IterateCurrentDisp([](uint64_t id, dispscr_base *scr) {
			scr->CheckRefresh();
		});
		if(scrolltoid_onupdate) HandleScrollToIDOnUpdate();
		scrollbar->RepositionItems();
		scrollpane->Thaw();
		tppw_flags &= ~TPPWF::NOUPDATEONPUSH;
		scrollpane->resize_update_pending = rup;
		if(tppw_flags & TPPWF::FROZEN) {
			tppw_flags &= ~TPPWF::FROZEN;
			base()->Thaw();
		}
	}

	if(tppw_flags & TPPWF::CLABELUPDATEPENDING) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::CheckClearNoUpdateFlag() %s TPPWF::CLABELUPDATEPENDING", cstr(GetThisName()));
		#endif
		UpdateCLabel();
		tppw_flags &= ~TPPWF::CLABELUPDATEPENDING;
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: panelparentwin_base_impl::CheckClearNoUpdateFlag() %s END", cstr(GetThisName()));
	#endif
}

void panelparentwin_base_impl::ResetBatchTimer() {
	tppw_flags |= TPPWF::BATCHTIMERMODE;
	batchtimer.SetOwner(this, TPPWID_TIMER_BATCHMODE);
	batchtimer.Start(BATCH_TIMER_DELAY, wxTIMER_ONE_SHOT);
}

void panelparentwin_base_impl::UpdateBatchTimer() {
	if(!(tppw_flags & TPPWF::NOUPDATEONPUSH)) ResetBatchTimer();
}

uint64_t panelparentwin_base::GetCurrentViewTopID() const {
	return pimpl()->GetCurrentViewTopID();
}

uint64_t panelparentwin_base_impl::GetCurrentViewTopID() const {
	for(auto &it : currentdisp) {
		int y;
		it.item->GetPosition(0, &y);
		if(y >= 0) return it.id;
	}
	return 0;
}

mainframe *panelparentwin_base::GetMainframe() {
	return pimpl()->GetMainframe();
}

mainframe *panelparentwin_base_impl::GetMainframe() {
	return GetMainframeAncestor(base());
}

bool panelparentwin_base::IsSingleAccountWin() const {
	return alist.size() <= 1;
}

void panelparentwin_base::IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const {
	pimpl()->IterateCurrentDisp(std::move(func));
}

void panelparentwin_base_impl::IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const {
	for(auto &it : currentdisp) {
		func(it.id, it.disp);
	}
}

const tpanel_disp_item_list &panelparentwin_base::GetCurrentDisp() const {
	return pimpl()->currentdisp;
}

void panelparentwin_base_impl::CLabelNeedsUpdating(flagwrapper<PUSHFLAGS> pushflags) {
	if(!(tppw_flags & TPPWF::NOUPDATEONPUSH) && !(pushflags & PUSHFLAGS::SETNOUPDATEFLAG)) UpdateCLabel();
	else tppw_flags |= TPPWF::CLABELUPDATEPENDING;
}

flagwrapper<TPPWF> panelparentwin_base::GetTPPWFlags() const {
	return pimpl()->tppw_flags;
}

int panelparentwin_base::IDToCurrentDispIndex(uint64_t id) const {
	return pimpl()->IDToCurrentDispIndex(id);
}

// NB: this returns -1 if not found
int panelparentwin_base_impl::IDToCurrentDispIndex(uint64_t id) const {
	auto it = std::find_if(currentdisp.begin(), currentdisp.end(), [&](const tpanel_disp_item &disp) {
		return disp.id == id;
	});

	if(it != currentdisp.end()) {
		return std::distance(currentdisp.begin(), it);
	}
	else {
		return -1;
	}
}


BEGIN_EVENT_TABLE(tpanelparentwin_nt_impl, panelparentwin_base_impl)
EVT_BUTTON(TPPWID_MARKALLREADBTN, tpanelparentwin_nt_impl::markallreadevthandler)
EVT_BUTTON(TPPWID_UNHIGHLIGHTALLBTN, tpanelparentwin_nt_impl::markremoveallhighlightshandler)
EVT_MENU(TPPWID_MARKALLREADBTN, tpanelparentwin_nt_impl::markallreadevthandler)
EVT_MENU(TPPWID_UNHIGHLIGHTALLBTN, tpanelparentwin_nt_impl::markremoveallhighlightshandler)
EVT_BUTTON(TPPWID_MOREBTN, tpanelparentwin_nt_impl::morebtnhandler)
EVT_TIMER(TPPWID_TIMER_BATCHMODE, tpanelparentwin_nt_impl::OnBatchTimerModeTimer)
END_EVENT_TABLE()

container::map<uint64_t, unsigned int> tpanelparentwin_nt_impl::all_tweetid_count_map;

const tpanelparentwin_nt_impl *tpanelparentwin_nt::pimpl() const {
	return static_cast<const tpanelparentwin_nt_impl *>(pimpl_ptr.get());
}

tpanelparentwin_nt::tpanelparentwin_nt(const std::shared_ptr<tpanel> &tp_, wxWindow *parent, wxString thisname_, tpanelparentwin_nt_impl *privimpl)
		: panelparentwin_base(parent, false, thisname_, privimpl ? privimpl : new tpanelparentwin_nt_impl(this)) {
	pimpl()->tp = tp_;
	LogMsgFormat(LOGT::TPANELTRACE, "Creating tweet panel window %s", cstr(pimpl()->tp->name));

	pimpl()->tp->twin.push_front(this);
	tpanelparentwinlist.push_front(this);

	pimpl()->clabel->SetLabel(wxT("No Tweets"));
	pimpl()->ShowHideButtons("more", true);
	FitInside();

	pimpl()->tppw_flags |= TPPWF::BATCHTIMERMODE;

	pimpl()->setupnavbuttonhandlers();
}

tpanelparentwin_nt::~tpanelparentwin_nt() {
	pimpl()->tp->OnTPanelWinClose(this);
	tpanelparentwinlist.remove(this);
}

void tpanelparentwin_nt::PushTweet(tweet_ptr_p t, flagwrapper<PUSHFLAGS> pushflags) {
	pimpl()->PushTweet(t, pushflags);
}

//This should be kept in sync with OnBatchTimerModeTimer
void tpanelparentwin_nt_impl::PushTweet(tweet_ptr_p t, flagwrapper<PUSHFLAGS> pushflags) {
	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		pushtweetbatchqueue.emplace_back(t, pushflags);
		UpdateBatchTimer();
		return;
	}

	scrollpane->Freeze();
	auto finaliser = scope_guard([&]() {
		scrollpane->Thaw();
		CLabelNeedsUpdating(pushflags);
	});

	LogMsgFormat(LOGT::TPANELTRACE, "tpanelparentwin_nt_impl::PushTweet %s, id: %" llFmtSpec "d, displayoffset: %d, pushflags: 0x%X, currentdisp: %d, tppw_flags: 0x%X",
			cstr(GetThisName()), t->id, displayoffset, pushflags, (int) currentdisp.size(), tppw_flags);
	if(pushflags & PUSHFLAGS::ABOVE) scrollbar->scroll_always_freeze = true;
	uint64_t id = t->id;
	bool recalcdisplayoffset = false;
	if(pushflags & PUSHFLAGS::NOINCDISPOFFSET && currentdisp.empty()) recalcdisplayoffset = true;
	if(displayoffset > 0) {
		if(id > currentdisp.front().id) {
			if(!(pushflags & PUSHFLAGS::ABOVE)) {
				if(!(pushflags & PUSHFLAGS::NOINCDISPOFFSET)) displayoffset++;
				return;
			}
			else if(pushflags & PUSHFLAGS::NOINCDISPOFFSET) recalcdisplayoffset = true;
		}
	}
	else if(displayoffset < 0) {
		if(pushflags & PUSHFLAGS::NOINCDISPOFFSET) recalcdisplayoffset = true;
		else return; // displayoffset not currently valid, do not insert
	}
	if(currentdisp.size() == gc.maxtweetsdisplayinpanel) {
		if(t->id < currentdisp.back().id) {    //off the bottom of the list
			if(pushflags & PUSHFLAGS::BELOW) {
				PopTop();
				displayoffset++;
			}
			else {
				return;
			}
		}
		else if(pushflags & PUSHFLAGS::BELOW) {
			PopTop();
			displayoffset++;
		}
		else PopBottom();    //too many in list, remove the last one
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::PushTweet 1, %d, %d, %d", displayoffset, currentdisp.size(), recalcdisplayoffset);
	#endif
	if(pushflags & PUSHFLAGS::SETNOUPDATEFLAG) tppw_flags |= TPPWF::NOUPDATEONPUSH;
	auto it = currentdisp.begin();
	for(; it != currentdisp.end(); ++it) {
		if(it->id < id) break;	//insert before this iterator
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::PushTweet 2, %d, %d, %d", displayoffset, currentdisp.size(), recalcdisplayoffset);
	#endif
	tpanel_disp_item *tpdi = CreateItemAtPosition(it, t->id);
	tweetdispscr *td = CreateTweetInItem(t, *tpdi);

	if(recalcdisplayoffset)
		RecalculateDisplayOffset();

	if(!(tppw_flags & TPPWF::NOUPDATEONPUSH)) td->ForceRefresh();
	else td->gdb_flags |= tweetdispscr::GDB_F::NEEDSREFRESH;

	if(pushflags & PUSHFLAGS::CHECKSCROLLTOID) {
		if(tppw_flags & TPPWF::NOUPDATEONPUSH) scrolltoid_onupdate = scrolltoid;
		else scrollbar->ScrollToId(id, 0);
	}

	scrollbar->RepositionItems();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::PushTweet %s END, %d, %d", cstr(GetThisName()), displayoffset, currentdisp.size());
	#endif
}

tweetdispscr *tpanelparentwin_nt_impl::CreateTweetInItem(tweet_ptr_p t, tpanel_disp_item &tpdi) {
	LogMsgFormat(LOGT::TPANELTRACE, "tpanelparentwin_nt_impl::CreateTweetInItem, %s, id: %" llFmtSpec "d", cstr(GetThisName()), t->id);

	tpanel_item *item = tpdi.item;

	tweetdispscr *td = new tweetdispscr(t, item, base(), item->hbox);
	item->vbox->Add(td, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);
	tpdi.disp = td;

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::CreateTweetInItem 1");
	#endif

	if(t->flags.Get('T')) {
		if(t->rtsrc && gc.rtdisp) {
			td->bm = new profimg_staticbitmap(item, t->rtsrc->user->cached_profile_img, t->rtsrc->user, t, GetMainframe());
		}
		else {
			td->bm = new profimg_staticbitmap(item, t->user->cached_profile_img, t->user, t, GetMainframe());
		}
		item->hbox->Prepend(td->bm, 0, wxALL, 2);
	}
	else if(t->flags.Get('D') && t->user_recipient) {
			t->user->ImgHalfIsReady(PENDING_REQ::PROFIMG_DOWNLOAD);
			t->user_recipient->ImgHalfIsReady(PENDING_REQ::PROFIMG_DOWNLOAD);
			td->bm = new profimg_staticbitmap(item, t->user->cached_profile_img_half, t->user, t, GetMainframe(), profimg_staticbitmap::PISBF::HALF);
			td->bm2 = new profimg_staticbitmap(item, t->user_recipient->cached_profile_img_half, t->user_recipient, t, GetMainframe(), profimg_staticbitmap::PISBF::HALF);
			int dim = gc.maxpanelprofimgsize / 2;
			if(tpg->arrow_dim != dim) {
				tpg->arrow = GetArrowIconDim(dim);
				tpg->arrow_dim = dim;
			}
			wxStaticBitmap *arrow = new wxStaticBitmap(item, wxID_ANY, tpg->arrow, wxPoint(-1000, -1000));
			wxGridSizer *gs = new wxGridSizer(2, 2, 0, 0);
			gs->Add(td->bm, 0, 0, 0);
			gs->AddStretchSpacer();
			gs->Add(arrow, 0, wxALIGN_CENTRE, 0);
			gs->Add(td->bm2, 0, 0, 0);
			item->hbox->Prepend(gs, 0, wxALL, 2);
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::CreateTweetInItem 2");
	#endif

	td->PanelInsertEvt();
	td->DisplayTweet();

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::CreateTweetInItem 3");
	#endif

	tpanel_subtweet_pending_op::CheckLoadTweetReply(t, item->vbox, base(), td, gc.inlinereplyloadcount, t, td);

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::CreateTweetInItem %s END", cstr(GetThisName()));
	#endif

	return td;
}

void tpanelparentwin_nt::RemoveTweet(uint64_t id, flagwrapper<PUSHFLAGS> pushflags) {
	pimpl()->RemoveTweet(id, pushflags);
}

void tpanelparentwin_nt_impl::RemoveTweet(uint64_t id, flagwrapper<PUSHFLAGS> pushflags) {
	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		removetweetbatchqueue.emplace_back(id, pushflags);
		UpdateBatchTimer();
		return;
	}

	LogMsgFormat(LOGT::TPANELTRACE, "tpanelparentwin_nt_impl::RemoveTweet %s, id: %" llFmtSpec "d, displayoffset: %d, pushflags: 0x%X, currentdisp: %d, tppw_flags: 0x%X",
			cstr(GetThisName()), id, displayoffset, pushflags, (int) currentdisp.size(), tppw_flags);

	if(currentdisp.empty()) {
		CLabelNeedsUpdating(pushflags);
		return;
	}

	if(id > currentdisp.front().id) {
		if(!(pushflags & PUSHFLAGS::NOINCDISPOFFSET)) displayoffset--;
		CLabelNeedsUpdating(pushflags);
		return;
	}
	if(id < currentdisp.back().id) {
		CLabelNeedsUpdating(pushflags);
		return;
	}

	size_t index = 0;
	auto it = currentdisp.begin();
	for(; it != currentdisp.end(); it++, index++) {
		if(it->id == id) {
			scrollpane->Freeze();

			if(pushflags & PUSHFLAGS::SETNOUPDATEFLAG) tppw_flags |= TPPWF::NOUPDATEONPUSH;
			RemoveIndex(index);
			CLabelNeedsUpdating(pushflags);

			scrollbar->RepositionItems();
			scrollpane->Thaw();
			break;
		}
	}
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::RemoveTweet %s END, %d, %d", cstr(GetThisName()), displayoffset, currentdisp.size());
	#endif
}

void tpanelparentwin_nt_impl::PageUpHandler() {
	if(displayoffset > 0) {
		SetNoUpdateFlag();
		size_t pagemove = std::min((size_t) (gc.maxtweetsdisplayinpanel + 1) / 2, (size_t) displayoffset);
		uint64_t greaterthanid = currentdisp.front().id;
		LoadMore(pagemove, 0, greaterthanid, PUSHFLAGS::ABOVE | PUSHFLAGS::NOINCDISPOFFSET);
		CheckClearNoUpdateFlag();
	}
	scrollbar->page_scroll_blocked = false;
}
void tpanelparentwin_nt_impl::PageDownHandler() {
	SetNoUpdateFlag();
	size_t curnum = currentdisp.size();
	size_t tweetnum = tp->tweetlist.size();
	if(displayoffset >= 0 && (curnum + displayoffset < tweetnum || tppw_flags & TPPWF::CANALWAYSSCROLLDOWN)) {
		size_t pagemove;
		if(tppw_flags & TPPWF::CANALWAYSSCROLLDOWN) pagemove = (gc.maxtweetsdisplayinpanel + 1) / 2;
		else pagemove = std::min((size_t) (gc.maxtweetsdisplayinpanel + 1) / 2, (size_t) tweetnum - (curnum + displayoffset));
		uint64_t lessthanid = currentdisp.back().id;
		LoadMore(pagemove, lessthanid, 0, PUSHFLAGS::BELOW | PUSHFLAGS::NOINCDISPOFFSET);
	}
	scrollbar->page_scroll_blocked = false;
	CheckClearNoUpdateFlag();
}

void tpanelparentwin_nt_impl::PageTopHandler() {
	if(displayoffset > 0) {
		SetNoUpdateFlag();
		size_t pushcount = std::min((size_t) displayoffset, (size_t) gc.maxtweetsdisplayinpanel);
		displayoffset = 0;
		LoadMore(pushcount, 0, 0, PUSHFLAGS::ABOVE | PUSHFLAGS::NOINCDISPOFFSET);
		CheckClearNoUpdateFlag();
	}
	base()->GenericAction([](tpanelparentwin_nt *tp) {
		tp->pimpl()->scrollbar->ScrollToIndex(0, 0);
	});
}

void tpanelparentwin_nt::JumpToTweetID(uint64_t id) {
	pimpl()->JumpToTweetID(id);
}

void tpanelparentwin_nt_impl::JumpToTweetID(uint64_t id) {
	LogMsgFormat(LOGT::TPANELINFO, "tpanelparentwin_nt_impl::JumpToTweetID %s, %" llFmtSpec "d, displayoffset: %d, display count: %d, tweets: %d",
			cstr(GetThisName()), id, displayoffset, (int) currentdisp.size(), (int) tp->tweetlist.size());

	bool alldone = false;

	if(id <= currentdisp.front().id && id >= currentdisp.back().id) {
		bool ok = scrollbar->ScrollToId(id, 0);
		if(ok) {
			if(GetCurrentViewTopID() == id) alldone = true;  //if this isn't true, load some more tweets below to make it true
		}
	}

	if(!alldone) {
		tweetidset::const_iterator stit = tp->tweetlist.find(id);
		if(stit == tp->tweetlist.end()) return;

		SetNoUpdateFlag();
		scrollpane->Freeze();

		//this is the offset into the tweetlist set, of the target tweet
		unsigned int targ_offset = std::distance(tp->tweetlist.cbegin(), stit);

		//this is how much above and below the target tweet that we also want to load
		unsigned int offset_up_delta = std::min<unsigned int>(targ_offset, (gc.maxtweetsdisplayinpanel+1)/2);
		unsigned int offset_down_delta = std::min<unsigned int>(tp->tweetlist.size() - targ_offset, gc.maxtweetsdisplayinpanel - offset_up_delta) - 1;

		//these are the new bounds on the current display set, given the offsets above and below the target tweet, above
		uint64_t top_id = *std::prev(stit, offset_up_delta);
		uint64_t bottom_id = *std::next(stit, offset_down_delta);

		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::JumpToTweetID targ_offset: %d, oud: %d, odd: %d, ti: %" llFmtSpec "d, bi: %" llFmtSpec "d",
					targ_offset, offset_up_delta, offset_down_delta, top_id, bottom_id);
		#endif

		//get rid of tweets which lie outside the new bounds
		while(!currentdisp.empty() && currentdisp.front().id > top_id) {
			PopTop();
			displayoffset++;
		}
		while(!currentdisp.empty() && currentdisp.back().id < bottom_id) PopBottom();

		if(currentdisp.empty()) {
			// Mark displayoffset as invalid
			// Using this instead of displayoffset = 0 prevents any new tweets which arrive from sniping us, as they would otherwise
			// assume that they can be inserted as there are no other tweets
			displayoffset = -1;
		}

		unsigned int loadcount = offset_up_delta + offset_down_delta + 1 - currentdisp.size();

		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::JumpToTweetID displayoffset: %d, loadcount: %d, display count: %d",
					displayoffset, loadcount, (int) currentdisp.size());
		#endif

		scrollpane->Thaw();
		if(loadcount) {
			scrolltoid = id;

			//if the new top id is also the top of the existing range, start loading from below the bottom of the existing range
			if(!currentdisp.empty() && top_id == currentdisp.front().id) {
				#if TPANEL_COPIOUS_LOGGING
					LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::JumpToTweetID adjusting load bound: %" llFmtSpec "u", currentdisp.back().id);
				#endif
				LoadMore(loadcount, currentdisp.back().id, 0, PUSHFLAGS::ABOVE | PUSHFLAGS::CHECKSCROLLTOID | PUSHFLAGS::NOINCDISPOFFSET);
			}
			else LoadMore(loadcount, top_id + 1, 0, PUSHFLAGS::ABOVE | PUSHFLAGS::CHECKSCROLLTOID | PUSHFLAGS::NOINCDISPOFFSET);
		}
		else CheckClearNoUpdateFlag();
	}

	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::JumpToTweetID %s END", cstr(GetThisName()));
	#endif
}

void tpanelparentwin_nt_impl::UpdateCLabel() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::UpdateCLabel %s START", cstr(GetThisName()));
	#endif
	size_t curnum = currentdisp.size();
	if(curnum) {
		wxString msg = wxString::Format(wxT("%d - %d of %d"), displayoffset + 1, displayoffset + curnum, tp->tweetlist.size());
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
	ShowHideButtons("unhighlightall", gc.showunhighlightallbtn && !tp->cids.highlightids.empty());
	ShowHideButtons("more", true);
	headersizer->Layout();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::UpdateCLabel %s END", cstr(GetThisName()));
	#endif
}

void tpanelparentwin_nt_impl::markallreadevthandler(wxCommandEvent &event) {
	tp->MarkSetRead(tp->MakeUndoItem("mark all read"));
}

void tpanelparentwin_nt::MarkSetRead(tweetidset &&subset, optional_observer_ptr<undo::item> undo_item) {
	pimpl()->tp->MarkSetReadOrUnread(std::move(subset), undo_item, true);
}

void tpanelparentwin_nt_impl::markremoveallhighlightshandler(wxCommandEvent &event) {
	tp->MarkSetUnhighlighted(tp->MakeUndoItem("unhighlight all"));
}

void tpanelparentwin_nt::MarkSetUnhighlighted(tweetidset &&subset, optional_observer_ptr<undo::item> undo_item) {
	pimpl()->tp->MarkSetHighlightState(std::move(subset), undo_item, true);
}

void tpanelparentwin_nt_impl::setupnavbuttonhandlers() {
	auto addhandler = [&](int id, std::function<void(wxCommandEvent &event)> f) {
		auto handler = base()->evtbinder->MakeSharedEvtHandlerSC<wxCommandEvent>(std::move(f));
		base()->evtbinder->BindEvtHandler(wxEVT_COMMAND_BUTTON_CLICKED, id, handler);
		base()->evtbinder->BindEvtHandler(wxEVT_COMMAND_MENU_SELECTED, id, handler);
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
		wxString str = ::wxGetTextFromUser(msg, wxT("Input Number"), wxT(""), base());
		if(!str.IsEmpty()) {
			std::string stdstr = stdstrwx(str);
			char *pos = 0;
			value = strtoull(stdstr.c_str(), &pos, 10);
			if(pos && *pos != 0) {
				::wxMessageBox(wxString::Format(wxT("'%s' does not appear to be a positive integer"), str.c_str()), wxT("Invalid Input"), wxOK | wxICON_EXCLAMATION, base());
			}
			else if(value < lowerbound || value > upperbound) {
				::wxMessageBox(wxString::Format(wxT("'%s' lies outside the range: %" wxLongLongFmtSpec "d - %" wxLongLongFmtSpec "d"), str.c_str(), lowerbound, upperbound),
						wxT("Invalid Input"), wxOK | wxICON_EXCLAMATION, base());
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
			uint64_t value = 0;
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
					::wxMessageBox(wxString::Format(wxT("No tweet with ID: %" wxLongLongFmtSpec "d in this panel"), value),
							wxT("No such tweet"), wxOK | wxICON_EXCLAMATION, base());
				}
				else JumpToTweetID(value);
			}
		}
	});

	auto hidesettogglefunc = [&](int cmdid, flagwrapper<TPPWF> tppwflag, tweetidset cached_id_sets::* setptr, const std::string &logstr) {
		addhandler(cmdid, [this, tppwflag, setptr, logstr](wxCommandEvent &event) {
			tppw_flags ^= tppwflag;
			SetNoUpdateFlag();

			//refresh any currently displayed tweets which are marked as hidden
			IterateCurrentDisp([&](uint64_t id, dispscr_base *scr) {
				if((tp->cids.*setptr).find(id) != (tp->cids.*setptr).end()) {
					#if TPANEL_COPIOUS_LOGGING
						LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::setupnavbuttonhandlers: %s: About to refresh: %" llFmtSpec "d, items: %d",
								cstr(logstr), id, (tp->cids.*setptr).size());
					#endif
					static_cast<tweetdispscr *>(scr)->DisplayTweet(false);
				}
			});
			CheckClearNoUpdateFlag();
		});
	};
	hidesettogglefunc(TPPWID_TOGGLEHIDDEN, TPPWF::SHOWHIDDEN, &cached_id_sets::hiddenids, "TPPWID_TOGGLEHIDDEN: Hidden IDs");
	hidesettogglefunc(TPPWID_TOGGLEHIDEDELETED, TPPWF::SHOWDELETED, &cached_id_sets::deletedids, "TPPWID_TOGGLEHIDEDELETED: Deleted IDs");
	addhandler(TPPWID_FILTERDLGBTN, [this](wxCommandEvent &event) {
		filter_dlg *fdg = new filter_dlg(base(), wxID_ANY, [this]() -> const tweetidset * {
			return &(tp->tweetlist);
		}, tp->dispname);
		fdg->ShowModal();
		fdg->Destroy();
	});
}

void tpanelparentwin_nt_impl::morebtnhandler(wxCommandEvent &event) {
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
	if(!tp->tweetlist.empty()) {
		pmenu.AppendSeparator();
		pmenu.Append(TPPWID_FILTERDLGBTN, wxT("Apply Filter"));
	}
	pmenu.AppendSeparator();
	wxMenuItem *wmith = pmenu.Append(TPPWID_TOGGLEHIDDEN, wxString::Format(wxT("Show Hidden Tweets (%d)"), tp->cids.hiddenids.size()), wxT(""), wxITEM_CHECK);
	wmith->Check(tppw_flags & TPPWF::SHOWHIDDEN);
	wxMenuItem *wmith2 = pmenu.Append(TPPWID_TOGGLEHIDEDELETED, wxString::Format(wxT("Show Deleted Tweets (%d)"), tp->cids.deletedids.size()), wxT(""), wxITEM_CHECK);
	wmith2->Check(tppw_flags & TPPWF::SHOWDELETED);

	GenericPopupWrapper(base(), &pmenu, btnrect.GetLeft(), btnrect.GetBottom());
}

bool tpanelparentwin_nt::IsSingleAccountWin() const {
	return pimpl()->tp->IsSingleAccountTPanel();
}

void tpanelparentwin_nt::EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush) {
	pimpl()->EnumDisplayedTweets(std::move(func), setnoupdateonpush);
}

void tpanelparentwin_nt_impl::EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush) {
	base()->Freeze();
	bool checkupdateflag = false;
	if(setnoupdateonpush) {
		checkupdateflag = !(tppw_flags&TPPWF::NOUPDATEONPUSH);
		SetNoUpdateFlag();
	}
	for(auto &jt : currentdisp) {
		tweetdispscr *tds = static_cast<tweetdispscr *>(jt.disp);
		bool continueflag = func(tds);
		for(auto &kt : tds->subtweets) {
			if(kt.get()) {
				func(kt.get());
			}
		}
		if(!continueflag) break;
	}
	base()->Thaw();
	if(checkupdateflag) CheckClearNoUpdateFlag();
}

void tpanelparentwin_nt::UpdateOwnTweet(const tweet &t, bool redrawimg) {
	pimpl()->UpdateOwnTweet(t, redrawimg);
}

void tpanelparentwin_nt_impl::UpdateOwnTweet(const tweet &t, bool redrawimg) {
	UpdateOwnTweet(t.id, redrawimg);
}

void tpanelparentwin_nt::UpdateOwnTweet(uint64_t id, bool redrawimg) {
	pimpl()->UpdateOwnTweet(id, redrawimg);
}

void tpanelparentwin_nt_impl::UpdateOwnTweet(uint64_t id, bool redrawimg) {
	//Escape hatch: don't bother iterating over displayed tweets if no entry in tweetid_count_map,
	//ie. no tweet is displayed which is/is a retweet that ID
	if(tweetid_count_map.find(id) == tweetid_count_map.end()) return;

	//If we get this far then we can insert into updatetweetbatchqueue without unnecessarily bloating it,
	//as we actually have a corresponding tweetdispscr already
	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		bool &redrawimgflag = updatetweetbatchqueue[id];
		if(redrawimg) redrawimgflag = true;   // if the flag in updatetweetbatchqueue is already true, don't override it to false
		UpdateBatchTimer();
		return;
	}

	EnumDisplayedTweets([&](tweetdispscr *tds) {
		if(tds->td->id == id || tds->rtid == id) {    //found matching entry
			LogMsgFormat(LOGT::TPANELTRACE, "UpdateOwnTweet: %s, Found Entry %" llFmtSpec "d.", cstr(GetThisName()), id);
			tds->DisplayTweet(redrawimg);
		}
		return true;
	}, false);
}

void tpanelparentwin_nt_impl::HandleScrollToIDOnUpdate() {
	auto it = std::find_if(currentdisp.begin(), currentdisp.end(), [&](const tpanel_disp_item &disp) {
		return disp.id == scrolltoid_onupdate;
	});

	if(it != currentdisp.end()) {
		#if TPANEL_COPIOUS_LOGGING
			LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_nt_impl::HandleScrollToIDOnUpdate() %s", cstr(GetThisName()));
		#endif

		scrollbar->ScrollToIndex(std::distance(currentdisp.begin(), it), 0);
	}

	scrolltoid_onupdate = 0;
}

void tpanelparentwin_nt_impl::IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const {
	for(auto &it : currentdisp) {
		func(it.id, it.disp);
		tweetdispscr *tds = static_cast<tweetdispscr *>(it.disp);
		for(auto &jt : tds->subtweets) {
			if(jt) {
				func(jt->td->id, jt.get());
			}
		}
	}
}

//This should be kept in sync with PushTweet
void tpanelparentwin_nt_impl::OnBatchTimerModeTimer(wxTimerEvent& event) {
	tppw_flags &= ~TPPWF::BATCHTIMERMODE;

	auto finaliser = scope_guard([&]() {
		CheckClearNoUpdateFlag();
		tppw_flags |= TPPWF::BATCHTIMERMODE;
	});

	if(pushtweetbatchqueue.empty() && removetweetbatchqueue.empty() && updatetweetbatchqueue.empty() && batchedgenericactions.empty()) {
		return;
	}

	SetNoUpdateFlag();

	size_t pre_remove_dispsize = currentdisp.size();

	for(auto &it : removetweetbatchqueue) {
		RemoveTweet(it.first, it.second);
	}
	removetweetbatchqueue.clear();

	size_t post_remove_dispsize = currentdisp.size();

	if(!pushtweetbatchqueue.empty()) {
		struct simulation_disp {
			uint64_t id;
			std::pair<tweet_ptr, flagwrapper<PUSHFLAGS> > *pushptr;
		};
		std::list<simulation_disp> simulation_currentdisp;
		tweetidset gotids;
		for(auto &it : currentdisp) {
			simulation_currentdisp.push_back({ it.id, 0 });
			gotids.insert(it.id);
		}

		bool clabelneedsupdating = false;

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

			//If we've got this far, then we really are pushing something to the tpanel
			//The CLabel should be updated, regardless of whether anything actually gets pushed to the display
			clabelneedsupdating = true;

			if(simulation_currentdisp.size() == gc.maxtweetsdisplayinpanel) {
				if(id < simulation_currentdisp.back().id) {    //off the end of the list
					if(pushflags & PUSHFLAGS::BELOW) {
						simulation_currentdisp.pop_front();
					}
					else continue;
				}
				else if(pushflags & PUSHFLAGS::BELOW) {
					//too many in list and pushing to bottom, remove the top one
					simulation_currentdisp.pop_front();
				}
				else {
					//too many in list, remove the bottom one
					simulation_currentdisp.pop_back();
				}
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

		if(pushtweetbatchqueue.size() > pushcount) {
			// Some tweets are not being pushed, and hence may
			// not be included in any displayoffset adjustment,
			// so recalculate it here
			RecalculateDisplayOffset();
		}

		LogMsgFormat(LOGT::TPANELTRACE, "tpanelparentwin_nt_impl::OnBatchTimerModeTimer: %s, Reduced %u pushes to %u pushes.",
				cstr(GetThisName()), pushtweetbatchqueue.size(), pushcount);

		simulation_currentdisp.clear();
		pushtweetbatchqueue.clear();

		if(clabelneedsupdating) CLabelNeedsUpdating(0);
	}

	for(auto &it : updatetweetbatchqueue) {
		UpdateOwnTweet(it.first, it.second);
	}
	updatetweetbatchqueue.clear();

	for(auto &it : batchedgenericactions) {
		it(base());
	}
	batchedgenericactions.clear();

	//Items have been removed, load items from the bottom to re-fill the panel
	if(post_remove_dispsize != pre_remove_dispsize) {
		size_t dispsize_now = currentdisp.size();

		if(dispsize_now < pre_remove_dispsize) {
			size_t delta = pre_remove_dispsize - dispsize_now;

			uint64_t lessthan = 0;
			if(dispsize_now) lessthan = currentdisp.back().id;

			LoadMore(delta, lessthan, 0, 0);
		}
	}
}

void tpanelparentwin_nt::GenericAction(std::function<void(tpanelparentwin_nt *)> func) {
	pimpl()->GenericAction(std::move(func));
}

void tpanelparentwin_nt_impl::GenericAction(std::function<void(tpanelparentwin_nt *)> func) {
	if(tppw_flags & TPPWF::BATCHTIMERMODE) {
		batchedgenericactions.emplace_back(std::move(func));
		UpdateBatchTimer();
	}
	else {
		func(base());
	}
}

std::shared_ptr<tpanel> tpanelparentwin_nt::GetTP() {
	return pimpl()->tp;
}

tweetdispscr_mouseoverwin *tpanelparentwin_nt::MakeMouseOverWin() {
	return pimpl()->MakeMouseOverWin();
}

tweetdispscr_mouseoverwin *tpanelparentwin_nt_impl::MakeMouseOverWin() {
	if(!mouseoverwin) mouseoverwin.set(new tweetdispscr_mouseoverwin(scrollpane, base()));
	return mouseoverwin.get();
}

void tpanelparentwin_nt::IncTweetIDRefCounts(uint64_t tid, uint64_t rtid) {
	pimpl()->tweetid_count_map[tid]++;
	pimpl()->all_tweetid_count_map[tid]++;
	if(rtid) {
		pimpl()->tweetid_count_map[rtid]++;
		pimpl()->all_tweetid_count_map[rtid]++;
	}
}

void tpanelparentwin_nt::DecTweetIDRefCounts(uint64_t tid, uint64_t rtid) {
	auto dec_tcm = [&](container::map<uint64_t, unsigned int> &tcm, uint64_t id) {
		auto it = tcm.find(id);
		if(it != tcm.end()) {
			it->second--;

			//Remove IDs with value 0 from map
			if(it->second == 0) {
				tcm.erase(it);
			}
		}
	};
	dec_tcm(pimpl()->tweetid_count_map, tid);
	dec_tcm(pimpl()->all_tweetid_count_map, tid);
	if(rtid) {
		dec_tcm(pimpl()->tweetid_count_map, rtid);
		dec_tcm(pimpl()->all_tweetid_count_map, rtid);
	}
}

void tpanelparentwin_nt::UpdateAllCLabels() {
	for(auto &it : tpanelparentwinlist) {
		it->UpdateCLabel();
	}
}

void tpanelparentwin_nt_impl::RecalculateDisplayOffset() {
	displayoffset = 0;

	if(currentdisp.empty())
		return;

	tweetidset::const_iterator stit = tp->tweetlist.find(currentdisp.front().id);
	if(stit != tp->tweetlist.end())
		displayoffset = std::distance(tp->tweetlist.cbegin(), stit);
}

void tpanelparentwin_nt::TPReinitialiseState() {
	pimpl()->TPReinitialiseState();
}

void tpanelparentwin_nt_impl::TPReinitialiseState() {
	SetNoUpdateFlag();
	SetClabelUpdatePendingFlag();
	while(!currentdisp.empty()) {
		PopTop();
	}
	displayoffset = 0;
	LoadMore(gc.maxtweetsdisplayinpanel);
	CheckClearNoUpdateFlag();
}

BEGIN_EVENT_TABLE(tpanelparentwin_impl, tpanelparentwin_nt_impl)
	EVT_MENU(TPPWID_DETACH, tpanelparentwin_impl::tabdetachhandler)
	EVT_MENU(TPPWID_SPLIT, tpanelparentwin_impl::tabsplitcmdhandler)
	EVT_MENU(TPPWID_DUP, tpanelparentwin_impl::tabduphandler)
	EVT_MENU(TPPWID_DETACHDUP, tpanelparentwin_impl::tabdetachedduphandler)
	EVT_MENU(TPPWID_CLOSE, tpanelparentwin_impl::tabclosehandler)
END_EVENT_TABLE()

const tpanelparentwin_impl *tpanelparentwin::pimpl() const {
	return static_cast<const tpanelparentwin_impl *>(pimpl_ptr.get());
}

tpanelparentwin::tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select, wxString thisname_, tpanelparentwin_impl *privimpl)
: tpanelparentwin_nt(tp_, parent, thisname_.empty() ? wxT("tpanelparentwin for ") + wxstrstd(tp_->name) : thisname_, privimpl ? privimpl : new tpanelparentwin_impl(this)) {
	pimpl()->owner = parent;
	parent->auib->AddPage(this, wxstrstd(pimpl()->tp->dispname), select);
	pimpl()->LoadMore(gc.maxtweetsdisplayinpanel);
}

//if lessthanid is non-zero, is an exclusive upper id limit, iterate downwards
//if greaterthanid, is an exclusive lower limit, iterate upwards
//cannot set both
//if neither set: start at highest in set and iterate down
void tpanelparentwin_impl::LoadMore(unsigned int n, uint64_t lessthanid, uint64_t greaterthanid, flagwrapper<PUSHFLAGS> pushflags) {
	std::unique_ptr<dbseltweetmsg> loadmsg;

	LogMsgFormat(LOGT::TPANELTRACE, "tpanelparentwin_impl::LoadMore %s called with n: %d, lessthanid: %" llFmtSpec "d, greaterthanid: %" llFmtSpec "d, pushflags: 0x%X",
			cstr(GetThisName()), n, lessthanid, greaterthanid, pushflags);

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

	for(unsigned int i = 0; i < n; i++) {
		if(stit == tp->tweetlist.cend()) break;

		tweet_ptr tobj = ad.GetTweetById(*stit);
		if(CheckFetchPendingSingleTweet(tobj, std::shared_ptr<taccount>(), &loadmsg, PENDING_REQ::GUI_DEFAULT, PENDING_RESULT::GUI_DEFAULT)) {
			PushTweet(tobj, pushflags);
		}
		else {
			MarkPending_TPanelMap(tobj, base(), pushflags);
		}

		if(revdir) {
			if(stit == tp->tweetlist.cbegin()) break;
			--stit;
		}
		else ++stit;
	}
	if(loadmsg) {
		loadmsg->flags |= DBSTMF::CLEARNOUPDF;
		DBC_PrepareStdTweetLoadMsg(*loadmsg);
		DBC_SendMessage(std::move(loadmsg));
	}
	if(currentlogflags & LOGT::PENDTRACE) dump_tweet_pendings(LOGT::PENDTRACE, "", "\t");

	CheckClearNoUpdateFlag();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin::LoadMore %s END", cstr(GetThisName()));
	#endif
}

void tpanelparentwin_impl::tabdetachhandler(wxCommandEvent &event) {
	mainframe *top = new mainframe( appversionname, wxDefaultPosition, wxDefaultSize );
	int index = owner->auib->GetPageIndex(base());
	wxString text = owner->auib->GetPageText(index);
	owner->auib->RemovePage(index);
	owner = top;
	top->auib->AddPage(base(), text, true);
	top->Show(true);
}
void tpanelparentwin_impl::tabduphandler(wxCommandEvent &event) {
	tp->MkTPanelWin(owner);
}
void tpanelparentwin_impl::tabdetachedduphandler(wxCommandEvent &event) {
	mainframe *top = new mainframe( appversionname, wxDefaultPosition, wxDefaultSize );
	tp->MkTPanelWin(top);
	top->Show(true);
}
void tpanelparentwin_impl::tabclosehandler(wxCommandEvent &event) {
	owner->auib->RemovePage(owner->auib->GetPageIndex(base()));
	owner->auib->tabnumcheck();
	base()->Close();
}
void tpanelparentwin_impl::tabsplitcmdhandler(wxCommandEvent &event) {
	size_t pagecount = owner->auib->GetPageCount();
	wxPoint curpos = base()->GetPosition();
	unsigned int tally = 0;
	for(size_t i = 0; i < pagecount; ++i) {
		if(owner->auib->GetPage(i)->GetPosition() == curpos) tally++;
	}
	if(tally < 2) return;
	owner->auib->Split(owner->auib->GetPageIndex(base()), wxRIGHT);
}

void tpanelparentwin_impl::UpdateCLabel() {
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_impl::UpdateCLabel %s START", cstr(GetThisName()));
	#endif
	tpanelparentwin_nt_impl::UpdateCLabel();
	int pageid = owner->auib->GetPageIndex(base());
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
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelparentwin_impl::UpdateCLabel %s END", cstr(GetThisName()));
	#endif
}

std::multimap<uint64_t, tpanelparentwin_user*> tpanelparentwin_user_impl::pendingmap;

tpanelparentwin_user_impl *tpanelparentwin_user::pimpl() {
	return static_cast<tpanelparentwin_user_impl *>(pimpl_ptr.get());
}

tpanelparentwin_user::tpanelparentwin_user(wxWindow *parent, wxString thisname_, tpanelparentwin_user_impl *privimpl )
		: panelparentwin_base(parent, true, thisname_, privimpl ? privimpl : new tpanelparentwin_user_impl(this)) { }

tpanelparentwin_user::~tpanelparentwin_user() {
	for(auto it = pimpl()->pendingmap.begin(); it != pimpl()->pendingmap.end(); ) {
		if((*it).second == this) {
			auto todel = it;
			it++;
			pimpl()->pendingmap.erase(todel);
		}
		else it++;
	}
}

void tpanelparentwin_user_impl::PageUpHandler() {
	if(displayoffset > 0) {
		SetNoUpdateFlag();
		size_t pagemove = std::min((size_t) (gc.maxtweetsdisplayinpanel + 1) / 2, (size_t) displayoffset);
		size_t curnum = currentdisp.size();
		size_t bottomdrop = std::min(curnum, (size_t) (curnum + pagemove - gc.maxtweetsdisplayinpanel));
		for(size_t i = 0; i < bottomdrop; i++) PopBottom();
		auto it = userlist.begin() + displayoffset;
		for(unsigned int i = 0; i < pagemove; i++) {
			--it;
			UpdateUser(*it);
		}
		displayoffset -= pagemove;
		CheckClearNoUpdateFlag();
	}
	scrollbar->page_scroll_blocked = false;
}
void tpanelparentwin_user_impl::PageDownHandler() {
	SetNoUpdateFlag();
	size_t curnum = currentdisp.size();
	size_t num = ItemCount();
	if(displayoffset >= 0 && (curnum + displayoffset < num || tppw_flags & TPPWF::CANALWAYSSCROLLDOWN)) {
		size_t pagemove;
		if(tppw_flags & TPPWF::CANALWAYSSCROLLDOWN) pagemove = (gc.maxtweetsdisplayinpanel + 1) / 2;
		else pagemove = std::min((size_t) (gc.maxtweetsdisplayinpanel + 1) / 2, (size_t) (num - (curnum + displayoffset)));
		size_t topdrop = std::min(curnum, (size_t) (curnum + pagemove - gc.maxtweetsdisplayinpanel));
		for(size_t i = 0; i < topdrop; i++) PopTop();
		displayoffset += topdrop;
		base()->LoadMoreToBack(pagemove);
	}
	scrollbar->page_scroll_blocked = false;
	CheckClearNoUpdateFlag();
}

void tpanelparentwin_user_impl::PageTopHandler() {
	if(displayoffset > 0) {
		SetNoUpdateFlag();
		size_t pushcount = std::min((size_t) displayoffset, (size_t) gc.maxtweetsdisplayinpanel);
		ssize_t bottomdrop = ((ssize_t) pushcount + currentdisp.size()) - gc.maxtweetsdisplayinpanel;
		if(bottomdrop > 0) {
			for(ssize_t i = 0; i < bottomdrop; i++) PopBottom();
		}
		displayoffset = 0;
		for(auto &u : userlist) {
			if(pushcount) --pushcount;
			else break;

			UpdateUser(u);
		}
		CheckClearNoUpdateFlag();
	}
	scrollbar->ScrollToIndex(0, 0);
}

bool tpanelparentwin_user::PushBackUser(udc_ptr_p u) {
	return pimpl()->PushBackUser(u);
}

bool tpanelparentwin_user_impl::PushBackUser(udc_ptr_p u) {
	if(std::find(userlist.begin(), userlist.end(), u) == userlist.end()) {
		userlist.push_back(u);
	}

	return UpdateUser(u);
}

// u must already be in userlist
// returns true if marked pending
bool tpanelparentwin_user_impl::UpdateUser(udc_ptr_p u) {
	// scan currentdisp first
	for(auto &it : currentdisp) {
		if(it.id == u->id) {
			static_cast<userdispscr *>(it.disp)->Display();
			return false;
		}
	}

	if(u->IsReady(PENDING_REQ::PROFIMG_DOWNLOAD | PENDING_REQ::USEREXPIRE)) {
		// currentdisp is a subsample of userlist
		// Neither are inherently ordered
		// They are ordered with respect to each other
		// u is not in currentdisp at this point

		auto cd = currentdisp.begin();
		for(auto &ul : userlist) {
			if(u == ul || cd == currentdisp.end()) {
				break;
			}
			if(cd->id == ul->id) {
				++cd;
			}
		}

		scrollpane->Freeze();
		tpanel_disp_item *tpdi = CreateItemAtPosition(cd, u->id);
		tpanel_item *item = tpdi->item;

		userdispscr *td = new userdispscr(u, item, base(), item->hbox);
		tpdi->disp = td;

		td->bm = new profimg_staticbitmap(item, u->cached_profile_img, u, nullptr, GetMainframe());
		item->hbox->Prepend(td->bm, 0, wxALL, 2);

		item->vbox->Add(td, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);

		td->PanelInsertEvt();
		td->Display();
		CLabelNeedsUpdating(0);
		if(!(tppw_flags & TPPWF::NOUPDATEONPUSH)) td->ForceRefresh();
		else td->gdb_flags |= tweetdispscr::GDB_F::NEEDSREFRESH;
		scrollbar->RepositionItems();
		scrollpane->Thaw();
		return false;
	}
	else {
		u->udc_flags |= UDC::CHECK_USERLISTWIN;
		if(!CheckIfUserAlreadyInDBAndLoad(u) && u->NeedsUpdating(PENDING_REQ::USEREXPIRE)) {
			std::shared_ptr<taccount> acc;
			u->GetUsableAccount(acc, true);
			if(acc) {
				acc->MarkUserPending(u);
			}
		}
		auto pit = pendingmap.equal_range(u->id);
		for(auto it = pit.first; it != pit.second; ++it) {
			if((*it).second == base()) {
				return true;
			}
		}
		pendingmap.insert(std::make_pair(u->id, base()));
		return true;
	}
}

void tpanelparentwin_user::LoadMoreToBack(unsigned int n) {
	pimpl()->LoadMoreToBack(n);
}

void tpanelparentwin_user::CheckPendingUser(udc_ptr_p u) {
	auto pit = tpanelparentwin_user_impl::pendingmap.equal_range(u->id);
	for(auto it = pit.first; it != pit.second; ++it) {
		it->second->PushBackUser(u);
	}
}

std::map<std::pair<uint64_t, RBFS_TYPE>, std::shared_ptr<tpanel> > tpanelparentwin_usertweets_impl::usertpanelmap;

tpanelparentwin_usertweets_impl *tpanelparentwin_usertweets::pimpl() {
	return static_cast<tpanelparentwin_usertweets_impl *>(pimpl_ptr.get());
}

tpanelparentwin_usertweets::tpanelparentwin_usertweets(udc_ptr &user_, wxWindow *parent,
		std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc_,
		RBFS_TYPE type_, wxString thisname_, tpanelparentwin_usertweets_impl *privimpl)
: tpanelparentwin_nt(MkUserTweetTPanel(user_, type_), parent,
		thisname_.empty() ? wxString::Format(wxT("tpanelparentwin_usertweets for: %" wxLongLongFmtSpec "d"), user_->id) : thisname_,
		privimpl ? privimpl : new tpanelparentwin_usertweets_impl(this)) {

	pimpl()->user = user_;
	pimpl()->getacc = getacc_;
	pimpl()->type = type_;
	pimpl()->tppw_flags |= TPPWF::CANALWAYSSCROLLDOWN;
}

tpanelparentwin_usertweets::~tpanelparentwin_usertweets() {
	pimpl()->usertpanelmap.erase(std::make_pair(pimpl()->user->id, pimpl()->type));
}

std::shared_ptr<tpanel> tpanelparentwin_usertweets::MkUserTweetTPanel(udc_ptr_p user, RBFS_TYPE type_) {
	std::shared_ptr<tpanel> &tp = tpanelparentwin_usertweets_impl::usertpanelmap[std::make_pair(user->id, type_)];
	if(!tp) {
		tp = tpanel::MkTPanel("___UTL_" + std::to_string(user->id) + "_" + std::to_string((size_t) type_), "User Timeline: @" + user->GetUser().screen_name, TPF::DELETEONWINCLOSE|TPF::USER_TIMELINE);
	}
	return tp;
}

std::shared_ptr<tpanel> tpanelparentwin_usertweets::GetUserTweetTPanel(uint64_t userid, RBFS_TYPE type_) {
	auto it = tpanelparentwin_usertweets_impl::usertpanelmap.find(std::make_pair(userid, type_));
	if(it != tpanelparentwin_usertweets_impl::usertpanelmap.end()) {
		return it->second;
	}
	else return std::shared_ptr<tpanel>();
}

//if lessthanid is non-zero, is an exclusive upper id limit, iterate downwards
//if greaterthanid, is an exclusive lower limit, iterate upwards
//cannot set both
//if neither set: start at highest in set and iterate down
void tpanelparentwin_usertweets_impl::LoadMore(unsigned int n, uint64_t lessthanid, uint64_t greaterthanid, flagwrapper<PUSHFLAGS> pushflags) {
	std::shared_ptr<taccount> tac = getacc(*base());
	if(!tac) return;
	SetNoUpdateFlag();
	SetClabelUpdatePendingFlag();

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

		tweet_ptr t = ad.GetTweetById(*stit);
		if(CheckMarkTweetPending(t, tac.get()))
			PushTweet(t, PUSHFLAGS::USERTL | pushflags);
		else
			MarkPending_TPanelMap(t, 0, PUSHFLAGS::USERTL | pushflags, &tp);

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
		tac->SetNewTwitCurlExtHook([&](observer_ptr<twitcurlext> tce) { tce->mp = base(); });
		tac->StartRestGetTweetBackfill(lower_id /*lower limit, exclusive*/, upper_id /*upper limit, inclusive*/, numleft, type, user->id);
		tac->ClearNewTwitCurlExtHook();
	}

	CheckClearNoUpdateFlag();
}

void tpanelparentwin_usertweets_impl::UpdateCLabel() {
	if(failed) {
		clabel->SetLabel(wxT("Lookup Failed"));
		return;
	}
	if(inprogress) {
		clabel->SetLabel(wxT("Loading..."));
		return;
	}

	size_t curnum = currentdisp.size();
	size_t varmax = 0;
	wxString emptymsg = wxT("No Tweets");
	switch(type) {
		case RBFS_USER_TIMELINE: varmax = user->GetUser().statuses_count; break;
		case RBFS_USER_FAVS: varmax = user->GetUser().favourites_count; emptymsg = wxT("No Favourites"); break;
		default: break;
	}
	size_t curtotal = std::max(tp->tweetlist.size(), varmax);
	if(curnum) clabel->SetLabel(wxString::Format(wxT("%d - %d of %d"), displayoffset + 1, displayoffset+curnum, curtotal));
	else clabel->SetLabel(emptymsg);
}

void tpanelparentwin_usertweets::NotifyRequestFailed() {
	pimpl()->failed = true;
	pimpl()->havestarted = false;
	pimpl()->UpdateCLabel();
}

void tpanelparentwin_usertweets::NotifyRequestSuccess() {
	pimpl()->inprogress = false;
	pimpl()->failed = false;
	SetClabelUpdatePendingFlag();
}

//! There is no harm in calling this more than once
void tpanelparentwin_usertweets::InitLoading() {
	if(!pimpl()->havestarted) {
		pimpl()->havestarted = true;
		pimpl()->inprogress = true;
		pimpl()->LoadMore(gc.maxtweetsdisplayinpanel);
	}
}

tpanelparentwin_userproplisting_impl *tpanelparentwin_userproplisting::pimpl() {
	return static_cast<tpanelparentwin_userproplisting_impl *>(pimpl_ptr.get());
}

tpanelparentwin_userproplisting::tpanelparentwin_userproplisting(udc_ptr_p user_, wxWindow *parent,
		std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc_, tpanelparentwin_userproplisting::TYPE type_, wxString thisname_,
		tpanelparentwin_userproplisting_impl *privimpl)
		: tpanelparentwin_user(parent, thisname_.empty() ? wxString::Format(wxT("tpanelparentwin_userproplisting for: %" wxLongLongFmtSpec "d"), user_->id) : thisname_,
		privimpl ? privimpl : new tpanelparentwin_userproplisting_impl(this)) {
	pimpl()->user = user_;
	pimpl()->getacc = getacc_;
	pimpl()->havestarted = false;
	pimpl()->type = type_;
}

tpanelparentwin_userproplisting::~tpanelparentwin_userproplisting() { }

void tpanelparentwin_userproplisting_impl::Init() {
	std::shared_ptr<taccount> tac = getacc(*base());
	if(tac) {
		twitcurlext_simple::CONNTYPE conntype = twitcurlext_simple::CONNTYPE::NONE;
		switch(type) {
			case tpanelparentwin_userproplisting::TYPE::USERFOLLOWING:
				conntype = twitcurlext_simple::CONNTYPE::USERFOLLOWING;
				break;
			case tpanelparentwin_userproplisting::TYPE::USERFOLLOWERS:
				conntype = twitcurlext_simple::CONNTYPE::USERFOLLOWERS;
				break;
			case tpanelparentwin_userproplisting::TYPE::OWNINCOMINGFOLLOWLISTING:
				conntype = twitcurlext_simple::CONNTYPE::OWNINCOMINGFOLLOWLISTING;
				break;
			case tpanelparentwin_userproplisting::TYPE::OWNOUTGOINGFOLLOWLISTING:
				conntype = twitcurlext_simple::CONNTYPE::OWNOUTGOINGFOLLOWLISTING;
				break;
			case tpanelparentwin_userproplisting::TYPE::NONE:
				assert(false);
				break;
		}
		std::unique_ptr<twitcurlext_simple> twit = twitcurlext_simple::make_new(tac, conntype);
		twit->extra_id = user->id;
		twit->mp = base();
		twitcurlext::QueueAsyncExec(std::move(twit));
		inprogress = true;
	}
	UpdateCLabel();
}

void tpanelparentwin_userproplisting_impl::UpdateCLabel() {
	if(failed) {
		clabel->SetLabel(wxT("Lookup Failed"));
		return;
	}
	if(inprogress) {
		clabel->SetLabel(wxT("Loading..."));
		return;
	}

	size_t curnum = currentdisp.size();
	size_t varmax = 0;
	wxString emptymsg;

	using TYPE = tpanelparentwin_userproplisting::TYPE;
	switch(type) {
		case TYPE::USERFOLLOWING:
			varmax = user->GetUser().friends_count;
			emptymsg = wxT("No Friends");
			break;
		case TYPE::USERFOLLOWERS:
			varmax = user->GetUser().followers_count;
			emptymsg = wxT("No Followers");
			break;
		case TYPE::OWNINCOMINGFOLLOWLISTING:
			emptymsg = wxT("No incoming Follower requests");
			break;
		case TYPE::OWNOUTGOINGFOLLOWLISTING:
			emptymsg = wxT("No outgoing Friend requests");
			break;
		default:
			break;
	}
	size_t curtotal = std::max(useridlist.size(), varmax);
	if(curnum) clabel->SetLabel(wxString::Format(wxT("%d - %d of %d"), displayoffset + 1, displayoffset + curnum, curtotal));
	else clabel->SetLabel(emptymsg);
}

void tpanelparentwin_userproplisting_impl::LoadMoreToBack(unsigned int n) {
	std::shared_ptr<taccount> tac = getacc(*base());
	if(!tac) return;
	failed = false;
	inprogress = false;
	SetNoUpdateFlag();
	SetClabelUpdatePendingFlag();

	bool querypendings = false;
	size_t index = userlist.size();
	for(size_t i = 0; i < n && index < useridlist.size(); i++, index++) {
		udc_ptr u = ad.GetUserContainerById(useridlist[index]);
		if(PushBackUser(u)) {
			u->udc_flags |= UDC::CHECK_USERLISTWIN;
			if(!CheckIfUserAlreadyInDBAndLoad(u)) {
				tac->MarkUserPending(u);
				querypendings = true;
			}
		}
	}
	if(querypendings) {
		tac->StartRestQueryPendings();
	}

	CheckClearNoUpdateFlag();
}

void tpanelparentwin_userproplisting::NotifyRequestFailed() {
	pimpl()->failed = true;
	pimpl()->havestarted = false;
	UpdateCLabel();
}

void tpanelparentwin_userproplisting::PushUserIDToBack(uint64_t id) {
	pimpl()->useridlist.push_back(id);
}

//! There is no harm in calling this more than once
void tpanelparentwin_userproplisting::InitLoading() {
	if(!pimpl()->havestarted) {
		pimpl()->havestarted = true;
		pimpl()->Init();
	}
}

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid) {
	#if TPANEL_COPIOUS_LOGGING
		LogMsg(LOGT::TPANELTRACE, "TCL: RedirectMouseWheelEvent");
	#endif
	wxWindow *wind = wxFindWindowAtPoint(wxGetMousePosition() /*event.GetPosition()*/);
	while(wind) {
		if(wind != avoid && std::count(tpanelparentwinlist.begin(), tpanelparentwinlist.end(), wind)) {
			tpanelparentwin *tppw = static_cast<tpanelparentwin*>(wind);
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANELTRACE, "TCL: RedirectMouseWheelEvent: Dispatching to %s", cstr(tppw->GetThisName()));
			#endif
			event.SetEventObject(tppw->pimpl()->scrollbar);
			tppw->pimpl()->scrollbar->GetEventHandler()->ProcessEvent(event);
			return true;
		}
		wind = wind->GetParent();
	}
	return false;
}

void tpanelreltimeupdater::Notify() {
	time_t nowtime = time(nullptr);

	auto updatetimes = [&](tweetdispscr &td) {
		if(!td.updatetime) return;
		else if(nowtime >= td.updatetime) {
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

	for(auto & it : tpanelparentwinlist) {
		for(auto & jt: it->pimpl()->currentdisp) {
			tweetdispscr &td = static_cast<tweetdispscr &>(*(jt.disp));
			updatetimes(td);
			for(auto &kt : td.subtweets) {
				tweetdispscr *subt = kt.get();
				if(subt) updatetimes(*subt);
			}
		}
	}
}

void EnumAllDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush) {
	for(auto &it : tpanelparentwinlist) {
		it->EnumDisplayedTweets(func, setnoupdateonpush);
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
		bool found = false;
		if((tds->td->user && tds->td->user->id == userid)
			|| (tds->td->user_recipient && tds->td->user_recipient->id == userid)) found = true;
		if(tds->td->rtsrc) {
			if((tds->td->rtsrc->user && tds->td->rtsrc->user->id == userid)
				|| (tds->td->rtsrc->user_recipient && tds->td->rtsrc->user_recipient->id == userid)) found = true;
		}
		if(found) {
			LogMsgFormat(LOGT::TPANELTRACE, "UpdateUsersTweet: Found Entry %" llFmtSpec "d.", tds->td->id);
			tds->DisplayTweet(redrawimg);
		}
		return true;
	}, true);
}

void UpdateTweet(const tweet &t, bool redrawimg) {
	//Escape hatch: don't bother iterating over tpanelparentwinlist if no entry in all_tweetid_count_map,
	//ie. no tweet is displayed in any panel which is/is a retweet of that ID
	if(tpanelparentwin_nt_impl::all_tweetid_count_map.find(t.id) == tpanelparentwin_nt_impl::all_tweetid_count_map.end()) return;

	for(auto &it : tpanelparentwinlist) {
		it->UpdateOwnTweet(t, redrawimg);
	}
}
