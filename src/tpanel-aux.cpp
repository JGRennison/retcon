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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "tpanel-aux.h"
#include "tpanel.h"
#include "tpanel-pimpl.h"
#include "tpanel-data.h"
#include "tpg.h"
#include "mainui.h"
#include "util.h"
#include "tpanel-wid.h"
#include "alldata.h"
#include "userui.h"
#include "dispscr.h"
#include "res.h"
#include "cfg.h"
#include "uiutil.h"
#include "log.h"
#include "log-util.h"
#include <wx/dcmirror.h>
#include <cstdlib>

#ifndef TPANEL_SCROLLING_COPIOUS_LOGGING
#define TPANEL_SCROLLING_COPIOUS_LOGGING 0
#endif
#ifndef TPANEL_COPIOUS_LOGGING
#define TPANEL_COPIOUS_LOGGING 0
#endif

enum {
	NOTEBOOK_ID = 42,
};

struct TabArtReverseVideoDC : public wxMirrorDC {
	wxDC &targdc;
	wxColour pivot;

	TabArtReverseVideoDC(wxDC& dc, wxColour pivot_)
		: wxMirrorDC(dc, false), targdc(dc), pivot(pivot_) { }

	wxColour PivotColour(const wxColour &in) {
		double br = in.Red();
		double bg = in.Green();
		double bb = in.Blue();
		double pr = pivot.Red();
		double pg = pivot.Green();
		double pb = pivot.Blue();

		double factor = 0.35;
		auto trans = [&](double &b, double p) {
			double delta = b - p;
			if(b > p) delta *= p / (255 - p);
			else delta *= (255 - p) / p;
			delta *= factor;
			b = p - delta;
		};
		trans(br, pr);
		trans(bg, pg);
		trans(bb, pb);

		return NormaliseColour(br, bg, bb);
	}

	virtual void SetBrush(const wxBrush& brush) override {
		wxBrush newbrush = brush;
		newbrush.SetColour(PivotColour(brush.GetColour()));
		wxMirrorDC::SetBrush(newbrush);
	}
	virtual void SetPen(const wxPen& pen) override {
		wxPen newpen = pen;
		newpen.SetColour(PivotColour(pen.GetColour()));
		wxMirrorDC::SetPen(newpen);
	}
	virtual void DoGradientFillLinear(const wxRect& rect, const wxColour& initialColour, const wxColour& destColour, wxDirection nDirection) override {
		targdc.GradientFillLinear(rect, PivotColour(initialColour), PivotColour(destColour), nDirection);
	}

#ifdef __WXGTK__
	virtual GdkWindow* GetGDKWindow() const { return targdc.GetGDKWindow(); }
#endif
};

struct customTabArt : public wxAuiDefaultTabArt {
	wxColour textcolour;
	wxColour background;
	bool reverse_video;

	customTabArt(wxColour textcolour_, wxColour background_, bool reverse_video_)
			: textcolour(textcolour_), background(background_), reverse_video(reverse_video_) {

	}

	virtual customTabArt* Clone() override {
		return new customTabArt(textcolour, background,reverse_video);
	}

	virtual void DrawTab(wxDC& dc,
                         wxWindow* wnd,
                         const wxAuiNotebookPage& pane,
                         const wxRect& in_rect,
                         int close_button_state,
                         wxRect* out_tab_rect,
                         wxRect* out_button_rect,
                         int* x_extent) override {
		dc.SetTextForeground(textcolour);
		if(reverse_video) {
			TabArtReverseVideoDC revdc(dc, background);
			wxAuiDefaultTabArt::DrawTab(revdc, wnd, pane, in_rect, close_button_state, out_tab_rect, out_button_rect, x_extent);
		}
		else {
			wxAuiDefaultTabArt::DrawTab(dc, wnd, pane, in_rect, close_button_state, out_tab_rect, out_button_rect, x_extent);
		}
	}
};

BEGIN_EVENT_TABLE(tpanelnotebook, wxAuiNotebook)
	EVT_AUINOTEBOOK_ALLOW_DND(NOTEBOOK_ID, tpanelnotebook::dragdrophandler)
	EVT_AUINOTEBOOK_DRAG_DONE(NOTEBOOK_ID, tpanelnotebook::dragdonehandler)
	EVT_AUINOTEBOOK_TAB_RIGHT_DOWN(NOTEBOOK_ID, tpanelnotebook::tabrightclickhandler)
	EVT_AUINOTEBOOK_PAGE_CLOSED(NOTEBOOK_ID, tpanelnotebook::tabclosedhandler)
	EVT_SIZE(tpanelnotebook::onsizeevt)
END_EVENT_TABLE()

tpanelnotebook::tpanelnotebook(mainframe *owner_, wxWindow *parent)
		: wxAuiNotebook(parent, NOTEBOOK_ID, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT |
			wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_TAB_EXTERNAL_MOVE | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_WINDOWLIST_BUTTON),
			owner(owner_) {

	wxColour foreground = GetForegroundColour();
	wxColour background = GetBackgroundColour();

	unsigned int forecount = foreground.Red() + foreground.Blue() + foreground.Green();
	unsigned int backcount = background.Red() + background.Blue() + background.Green();

	bool isreverse = forecount > backcount;

	SetArtProvider(new customTabArt(foreground, background, isreverse));
}

void tpanelnotebook::dragdrophandler(wxAuiNotebookEvent& event) {
	wxAuiNotebook* note = (wxAuiNotebook *) event.GetEventObject();
	if(note) {
		tpanelparentwin *tppw = static_cast<tpanelparentwin *>(note->GetPage(event.GetSelection()));
		if(tppw) tppw->pimpl()->owner = owner;
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
	if(GetPageCount() == 0 && !(mainframelist.empty() || (++mainframelist.begin()) == mainframelist.end())) {
		owner->Close();
	}
}

void tpanelnotebook::tabrightclickhandler(wxAuiNotebookEvent& event) {
	tpanelparentwin *tppw = static_cast<tpanelparentwin *>(GetPage(event.GetSelection()));
	if(tppw) {
		wxMenu menu;
		menu.SetTitle(wxstrstd(tppw->pimpl()->tp->dispname));
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
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelnotebook::PostSplitSizeCorrect(): START");
	#endif
	wxSize totalsize=GetClientSize();

	wxAuiPaneInfoArray& all_panes = m_mgr.GetAllPanes();
	size_t pane_count = all_panes.GetCount();
	size_t tabctrl_count = 0;
	std::forward_list<wxAuiPaneInfo *> tabctrlarray;
	for(size_t i = 0; i < pane_count; ++i) {
		if(all_panes.Item(i).name != wxT("dummy")) {
			tabctrl_count++;
			tabctrlarray.push_front(&(all_panes.Item(i)));
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANELTRACE, "TCL: PostSplitSizeCorrect1 %d %d %d %d", all_panes.Item(i).dock_direction, all_panes.Item(i).dock_layer,
						all_panes.Item(i).dock_row, all_panes.Item(i).dock_pos);
			#endif
		}
	}
	for(auto &it : tabctrlarray) {
		wxAuiPaneInfo &pane = *it;
		pane.BestSize(totalsize.GetWidth() / tabctrl_count, totalsize.GetHeight());
		pane.MaxSize(totalsize.GetWidth() / tabctrl_count, totalsize.GetHeight());
		pane.DockFixed();
		if(pane.dock_direction != wxAUI_DOCK_LEFT && pane.dock_direction != wxAUI_DOCK_RIGHT && pane.dock_direction != wxAUI_DOCK_CENTRE) {
			pane.Right();
			pane.dock_row = 0;
			pane.dock_pos = 1;    //trigger code below
		}
		if(pane.dock_pos > 0) {    //make a new row, bumping up any others to make room
			if(pane.dock_direction == wxAUI_DOCK_LEFT) {
				for(auto &jt : tabctrlarray) {
					if(jt->dock_direction == pane.dock_direction && jt->dock_row > pane.dock_row && jt->dock_layer == pane.dock_layer) jt->dock_row++;
				}
				pane.dock_pos = 0;
				pane.dock_row++;
			}
			else {
				for(auto &jt : tabctrlarray) {
					if(jt->dock_direction == pane.dock_direction && jt->dock_row >= pane.dock_row && jt->dock_layer == pane.dock_layer && jt->dock_pos == 0) jt->dock_row++;
				}
				pane.dock_pos = 0;
			}
		}
	}
	for(auto &it : tabctrlarray) {
		m_mgr.InsertPane(it->window, *it, wxAUI_INSERT_ROW);
	}
	m_mgr.Update();

	for(size_t i = 0; i < pane_count; ++i) {
		if(all_panes.Item(i).name != wxT("dummy")) {
			#if TPANEL_COPIOUS_LOGGING
				LogMsgFormat(LOGT::TPANELTRACE, "TCL: PostSplitSizeCorrect2 %d %d %d %d", all_panes.Item(i).dock_direction, all_panes.Item(i).dock_layer,
						all_panes.Item(i).dock_row, all_panes.Item(i).dock_pos);
			#endif
		}
	}

	DoSizing();
	owner->Refresh();
	#if TPANEL_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TCL: tpanelnotebook::PostSplitSizeCorrect(): END");
	#endif
}

void tpanelnotebook::onsizeevt(wxSizeEvent &event) {
	PostSplitSizeCorrect();
	event.Skip();
}

void tpanelnotebook::FillWindowLayout(unsigned int mainframeindex) {
	wxAuiPaneInfoArray &all_panes = m_mgr.GetAllPanes();
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
		for(size_t j = 0; j < pane_count; ++j) {
			if(all_panes.Item(j).name == wxT("dummy")) continue;
			if(all_panes.Item(j).window == tabframe) {
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
		auto tp = tppw->pimpl()->tp;
		twld.tpautos = tp->tpautos;
		twld.tpudcautos = tp->tpudcautos;
		twld.name = tp->name;
		twld.dispname = tp->dispname;
		twld.flags = tp->flags;
	}
}

BEGIN_EVENT_TABLE(profimg_staticbitmap, wxStaticBitmap)
	EVT_LEFT_DOWN(profimg_staticbitmap::ClickHandler)
	EVT_RIGHT_UP(profimg_staticbitmap::RightClickHandler)
	EVT_MENU_RANGE(tweetactmenustartid, tweetactmenuendid, profimg_staticbitmap::OnTweetActMenuCmd)
END_EVENT_TABLE()

profimg_staticbitmap::profimg_staticbitmap(wxWindow* parent, const wxBitmap& label, udc_ptr udc_, tweet_ptr t_, mainframe *owner_, flagwrapper<PISBF> flags)
		: wxStaticBitmap(parent, wxID_ANY, label, wxPoint(-1000, -1000)), udc(std::move(udc_)), t(std::move(t_)), owner(owner_), pisb_flags(flags) {
	udc->profile_img_last_used = time(nullptr);
}

profimg_staticbitmap::~profimg_staticbitmap() {
	udc->profile_img_last_used = time(nullptr);
}

void profimg_staticbitmap::ClickHandler(wxMouseEvent &event) {
	std::shared_ptr<taccount> acc_hint;
	if(t) t->GetUsableAccount(acc_hint);
	user_window::MkWin(udc->id, acc_hint);
}

void profimg_staticbitmap::RightClickHandler(wxMouseEvent &event) {
	if(owner || !(pisb_flags & PISBF::DONTUSEDEFAULTMF)) {
		wxMenu menu;
		int nextid = tweetactmenustartid;
		tamd.clear();
		AppendUserMenuItems(menu, tamd, nextid, udc, t);
		GenericPopupWrapper(this, &menu);
	}
}

void profimg_staticbitmap::OnTweetActMenuCmd(wxCommandEvent &event) {
	mainframe *mf = owner;
	if(!mf && mainframelist.size() && !(pisb_flags&PISBF::DONTUSEDEFAULTMF)) mf = mainframelist.front();
	TweetActMenuAction(tamd, event.GetId(), mf);
}

std::shared_ptr<tpanelglobal> tpanelglobal::Get() {
	if(tpg_glob.expired()) {
		std::shared_ptr<tpanelglobal> tmp = std::make_shared<tpanelglobal>();
		tpg_glob = tmp;
		return tmp;
	}
	else return tpg_glob.lock();
}

std::weak_ptr<tpanelglobal> tpanelglobal::tpg_glob;

tpanelglobal::tpanelglobal() : arrow_dim(0) {
	GetInfoIcon(&infoicon, &infoicon_img);
	GetReplyIcon(&replyicon, &replyicon_img);
	GetFavIcon(&favicon, &favicon_img);
	GetFavOnIcon(&favonicon, &favonicon_img);
	GetRetweetIcon(&retweeticon, &retweeticon_img);
	GetRetweetOnIcon(&retweetonicon, &retweetonicon_img);
	GetDMreplyIcon(&dmreplyicon, &dmreplyicon_img);
	GetLockIcon(&proticon, &proticon_img);
	GetUnlockIcon(&unlockicon, &unlockicon_img);
	GetVerifiedIcon(&verifiedicon, &verifiedicon_img);
	GetCloseIcon(&closeicon, 0);
	GetMultiUnreadIcon(&multiunreadicon, 0);
	GetPhotoIcon(&photoicon, &photoicon_img);
}

BEGIN_EVENT_TABLE(tpanel_item, wxPanel)
	EVT_MOUSEWHEEL(tpanel_item::mousewheelhandler)
END_EVENT_TABLE()

tpanel_item::tpanel_item(tpanelscrollpane *parent_)
: wxPanel(parent_, wxID_ANY, wxPoint(-1000, -1000)), parent(parent_) {

	thisname = wxT("tpanel_item for ") + parent_->GetThisName();

	hbox = new wxBoxSizer(wxHORIZONTAL);
	vbox = new wxBoxSizer(wxVERTICAL);
	hbox->Add(vbox, 1, wxALL | wxEXPAND, 1);
	SetSizer(hbox);

	wxSize clientsize = parent->GetClientSize();
	SetSize(clientsize.x, wxDefaultCoord);
}

void tpanel_item::NotifySizeChange() {
	wxSize clientsize = parent->GetClientSize();
	SetMinSize(wxSize(clientsize.x, 1));
	SetMaxSize(wxSize(clientsize.x, 100000));
	wxSize bestsize = DoGetBestSize();
	SetSize(clientsize.x, bestsize.y);

	if(!parent->resize_update_pending) {
		parent->resize_update_pending = true;
		parent->Freeze();
		wxCommandEvent event(wxextRESIZE_UPDATE_EVENT, GetId());
		parent->GetEventHandler()->AddPendingEvent(event);
	}
}

void tpanel_item::NotifyLayoutNeeded() {
	Layout();
}

void tpanel_item::mousewheelhandler(wxMouseEvent &event) {
	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsg(LOGT::TPANELTRACE, "TSCL: Item MouseWheel");
	#endif
	event.SetEventObject(GetParent());
	GetParent()->GetEventHandler()->ProcessEvent(event);
}

BEGIN_EVENT_TABLE(tpanelscrollbar, wxScrollBar)
	EVT_SCROLL_TOP(tpanelscrollbar::OnScrollHandler)
	EVT_SCROLL_BOTTOM(tpanelscrollbar::OnScrollHandler)
	EVT_SCROLL_LINEUP(tpanelscrollbar::OnScrollHandler)
	EVT_SCROLL_LINEDOWN(tpanelscrollbar::OnScrollHandler)
	EVT_SCROLL_PAGEUP(tpanelscrollbar::OnScrollHandler)
	EVT_SCROLL_PAGEDOWN(tpanelscrollbar::OnScrollHandler)
	EVT_SCROLL_THUMBRELEASE(tpanelscrollbar::OnScrollHandler)
	EVT_SCROLL_CHANGED(tpanelscrollbar::OnScrollHandler)
	EVT_SCROLL_THUMBTRACK(tpanelscrollbar::OnScrollTrack)
	EVT_MOUSEWHEEL(tpanelscrollbar::mousewheelhandler)
END_EVENT_TABLE()

tpanelscrollbar::tpanelscrollbar(panelparentwin_base *parent_)
		: wxScrollBar(parent_, wxID_ANY, wxPoint(-1000, -1000), wxDefaultSize, wxSB_VERTICAL), parent(parent_), page_scroll_blocked(false) {

	thisname = wxT("tpanelscrollbar for ") + parent_->GetThisName();
}

void tpanelscrollbar::mousewheelhandler(wxMouseEvent &event) {
	int pxdelta = -event.GetWheelRotation() * gc.mousewheelscrollspeed / event.GetWheelDelta();

	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TSCL: tpanelscrollbar::mousewheelhandler %s, %d %d %d", cstr(GetThisName()),
				GetScrollPos(wxVERTICAL), event.GetWheelRotation(), pxdelta);
	#endif

	int y = GetThumbPosition();
	SetThumbPosition(std::max(0, y + pxdelta));
	ScrollItems();

	// Use gc.mousewheelscrollspeed as the threshold, this is to try and make mousewheel scrolling smooth across pages
	OnScrollHandlerCommon(pxdelta < 0, pxdelta > 0, std::abs(gc.mousewheelscrollspeed), GetScrollPos(wxVERTICAL));
}

void tpanelscrollbar::OnScrollTrack(wxScrollEvent &event) {
	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TSCL: tpanelscrollbar::OnScrollTrack %s, %d", cstr(GetThisName()), event.GetPosition());
	#endif
	int y = event.GetPosition();
	ScrollItemsForPosition(y);
	SetThumbPosition(y);
	event.Skip();
}

void tpanelscrollbar::OnScrollHandlerCommon(bool upok, bool downok, int threshold, int current_position) {
	int y = current_position;
	int endpos = y + scroll_client_size;
	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TSCL: tpanelscrollbar::OnScrollHandlerCommon %s, %d %d %d %d", cstr(GetThisName()), y, scroll_virtual_size, scroll_client_size, endpos);
	#endif
	bool scrollup = (y <= threshold && upok);
	bool scrolldown = (endpos >= (scroll_virtual_size - threshold) && downok);
	if(scrollup && !scrolldown && !page_scroll_blocked) {
		wxCommandEvent evt(wxextTP_PAGEUP_EVENT);
		parent->GetEventHandler()->AddPendingEvent(evt);
		page_scroll_blocked = true;
	}
	if(!scrollup && scrolldown && !page_scroll_blocked) {
		wxCommandEvent evt(wxextTP_PAGEDOWN_EVENT);
		parent->GetEventHandler()->AddPendingEvent(evt);
		page_scroll_blocked = true;
	}
}

void tpanelscrollbar::OnScrollHandler(wxScrollEvent &event) {
	wxEventType type = event.GetEventType();
	bool upok = (type == wxEVT_SCROLL_TOP || type == wxEVT_SCROLL_LINEUP || type == wxEVT_SCROLL_PAGEUP || type == wxEVT_SCROLL_THUMBRELEASE || type == wxEVT_SCROLL_CHANGED);
	bool downok = (type == wxEVT_SCROLL_BOTTOM || type == wxEVT_SCROLL_LINEDOWN || type == wxEVT_SCROLL_PAGEDOWN || type == wxEVT_SCROLL_THUMBRELEASE || type == wxEVT_SCROLL_CHANGED);

	int y;
	if(type == wxEVT_SCROLL_LINEUP || type == wxEVT_SCROLL_LINEDOWN) {
		y = GetThumbPosition();
		if(type == wxEVT_SCROLL_LINEUP) y -= gc.linescrollspeed;
		else y += gc.linescrollspeed;
		SetThumbPosition(std::max(0, y));
		ScrollItems();
	}
	else {
		if(type == wxEVT_SCROLLWIN_THUMBRELEASE || type == wxEVT_SCROLL_CHANGED) {
			y = event.GetPosition();
			SetThumbPosition(y);
		}
		else {
			y = GetThumbPosition();
		}
		ScrollItemsForPosition(y);
		event.Skip();
	}

	OnScrollHandlerCommon(upok, downok, 0, y);
}

// This determines the scrollbar size and position such that, where possible
// the item at the top of the visible screen is unmoved
void tpanelscrollbar::RepositionItems() {
	tpanelscrollpane *tsp = get();
	if(!tsp) return;

	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TSCL: tpanelscrollbar::RepositionItems %s, START %d %d", cstr(GetThisName()),
				GetThumbPosition(), parent->GetCurrentDisp().size());
	#endif
	scroll_virtual_size = 0;
	int cumul_size = 0;
	bool have_scroll_offset = false;
	int scroll_offset = 0;

	for(auto &disp : parent->GetCurrentDisp()) {
		wxPoint p = disp.item->GetPosition();
		wxSize s = disp.item->GetSize();

		scroll_virtual_size += s.y;

		if(p.x == 0 && p.y + s.y > 0 && p.y <= 0) {
			// This is an item which is visible at the top of the list
			// We should use the *last* matching item, this is as earlier items may grow in size to overlap the top of the screen

			// p.y is non-positive
			// The scroll offset should be increased as p.y increases in magnitude below 0
			scroll_offset = cumul_size - p.y;
			have_scroll_offset = true;
		}
		cumul_size += s.y;
	}

	if(parent->pimpl()->displayoffset == 0 && GetThumbPosition() == 0 &&
			have_scroll_offset && !scroll_always_freeze) {
		// We were at the very top, we would normally be scrolling down as something has been inserted above
		// Scroll back to the top instead
		// Don't do this if scroll_always_freeze is true
		scroll_offset = 0;
	}
	scroll_always_freeze = false;

	if(!have_scroll_offset) {
		scroll_offset = GetThumbPosition();
	}

	scroll_client_size = tsp->GetClientSize().y;
	SetScrollbar(scroll_offset, scroll_client_size, scroll_virtual_size, 1);

	ScrollItems();
	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TSCL: tpanelscrollbar::RepositionItems %s, END %d %d %d %d %d", cstr(GetThisName()),
				GetThumbPosition(), scroll_offset, have_scroll_offset, scroll_always_freeze, cumul_size);
	#endif
}

// Calls ScrollItemsForPosition for the current position
void tpanelscrollbar::ScrollItems() {
	ScrollItemsForPosition(GetThumbPosition());
}

// Move all child windows to their correct positions
// This is separate from ScrollItems so that the current_position value can be supplied from an event
void tpanelscrollbar::ScrollItemsForPosition(int current_position) {
	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TSCL: tpanelscrollbar::ScrollItems %s, %d, %d", cstr(GetThisName()), current_position, parent->GetCurrentDisp().size());
	#endif
	int y = -current_position;

	for(auto &disp : parent->GetCurrentDisp()) {
		wxSize s = disp.item->GetSize();

		// Note that Move() cannot be used as it special-cases the value of -1, which randomly breaks scrolling >:(
		disp.item->SetSize(0, y, s.x, s.y, wxSIZE_ALLOW_MINUS_ONE);

		y += s.y;
	}
}

void tpanelscrollbar::ScrollToIndex(unsigned int index, int offset) {
	int scroll_offset = 0;

	for(auto &disp : parent->GetCurrentDisp()) {
		if(index == 0) {
			SetThumbPosition(scroll_offset - offset);
			ScrollItems();
			return;
		}
		else index--;

		wxSize s = disp.item->GetSize();
		scroll_offset += s.y;
	}
}

// Returns true if successful (id is present)
bool tpanelscrollbar::ScrollToId(uint64_t id, int offset) {
	int index = parent->IDToCurrentDispIndex(id);
	if(index >= 0) {
		ScrollToIndex(index, offset);
		return true;
	}
	else {
		return false;
	}
}

BEGIN_EVENT_TABLE(tpanelscrollpane, wxPanel)
	EVT_SIZE(tpanelscrollpane::resizehandler)
	EVT_COMMAND(wxID_ANY, wxextRESIZE_UPDATE_EVENT, tpanelscrollpane::resizemsghandler)
	EVT_MOUSEWHEEL(tpanelscrollpane::mousewheelhandler)
END_EVENT_TABLE()

tpanelscrollpane::tpanelscrollpane(panelparentwin_base *parent_)
		: wxPanel(parent_, wxID_ANY, wxPoint(-1000, -1000), wxDefaultSize, wxCLIP_CHILDREN), parent(parent_), resize_update_pending(false) {

	thisname = wxT("tpanelscrollpane for ") + parent_->GetThisName();
}

void tpanelscrollpane::resizehandler(wxSizeEvent &event) {
	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TSCL: tpanelscrollpane::resizehandler: %s, %d, %d", cstr(GetThisName()), event.GetSize().GetWidth(), event.GetSize().GetHeight());
	#endif

	for(auto &disp : parent->GetCurrentDisp()) {
		disp.item->NotifySizeChange();
	}
}

void tpanelscrollpane::resizemsghandler(wxCommandEvent &event) {
	#if TPANEL_SCROLLING_COPIOUS_LOGGING
		LogMsgFormat(LOGT::TPANELTRACE, "TSCL: tpanelscrollpane::resizemsghandler %s", cstr(GetThisName()));
	#endif
	if(tpanelscrollbar *tsb = get()) {
		tsb->RepositionItems();
	}
	resize_update_pending = false;

	Thaw();
	Update();
}

void tpanelscrollpane::mousewheelhandler(wxMouseEvent &event) {
	if(tpanelscrollbar *tsb = get()) {
		event.SetEventObject(tsb);
		tsb->GetEventHandler()->ProcessEvent(event);
	}
}

tpanelload_pending_op::tpanelload_pending_op(tpanelparentwin_nt* win_, flagwrapper<PUSHFLAGS> pushflags_, std::shared_ptr<tpanel> *pushtpanel_)
		: win(win_), pushflags(pushflags_) {
	if(pushtpanel_) pushtpanel = *pushtpanel_;
}

void tpanelload_pending_op::MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	if(tp) tp->PushTweet(t, pushflags);
	tpanelparentwin_nt *window=win.get();
	if(window) {
		if(umpt_flags&UMPTF::TPDB_NOUPDF) window->SetNoUpdateFlag();
		window->PushTweet(t, pushflags);
	}
}

std::string tpanelload_pending_op::dump() {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	tpanelparentwin_nt *window=win.get();
	return string_format("Push tweet to tpanel: %s, window: %p, pushflags: 0x%X", (tp) ? cstr(wxstrstd(tp->dispname)) : "N/A", window, pushflags);
}

void tpanel_subtweet_pending_op::CheckLoadTweetReply(tweet_ptr_p t, wxSizer *v, tpanelparentwin_nt *s,
		tweetdispscr *tds, unsigned int load_count, tweet_ptr_p top_tweet, tweetdispscr *top_tds) {
	using GUAF = tweet::GUAF;

	if(t->in_reply_to_status_id) {
		std::function<void(unsigned int)> loadmorefunc = [=](unsigned int tweet_load_count) {
			tweet_ptr subt = ad.GetTweetById(t->in_reply_to_status_id);

			if(top_tweet->IsArrivedHereAnyPerspective()) {	//save
				subt->lflags |= TLF::SHOULDSAVEINDB;
			}

			std::shared_ptr<taccount> pacc;
			t->GetUsableAccount(pacc, GUAF::NOERR) || t->GetUsableAccount(pacc, GUAF::NOERR | GUAF::USERENABLED);
			subt->AddNewPendingOp(new tpanel_subtweet_pending_op(v, s, top_tds, tweet_load_count, top_tweet));
			subt->lflags |= TLF::ISPENDING;
			CheckFetchPendingSingleTweet(subt, pacc);
			TryUnmarkPendingTweet(subt, 0);
		};

		if(load_count == 0) {
			tds->tds_flags |= TDSF::CANLOADMOREREPLIES;
			tds->loadmorereplies = [=]() {
				loadmorefunc(gc.inlinereplyloadmorecount);
			};
			return;
		}
		else loadmorefunc(load_count);
	}
}

tpanel_subtweet_pending_op::tpanel_subtweet_pending_op(wxSizer *v, tpanelparentwin_nt *s, tweetdispscr *top_tds_,
		unsigned int load_count_, tweet_ptr top_tweet_) {
	action_data = std::make_shared<tspo_action_data>();
	action_data->vbox = v;
	action_data->win = s;
	action_data->top_tds = top_tds_;
	action_data->load_count = load_count_;
	action_data->top_tweet = std::move(top_tweet_);
}

void tpanel_subtweet_pending_op::MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) {
	std::shared_ptr<tspo_action_data> data = this->action_data;

	tweetdispscr *tp_tds = data->top_tds.get();
	tpanelparentwin_nt *tp_window = data->win.get();
	if(!tp_tds || !tp_window) return;

	if(umpt_flags & UMPTF::TPDB_NOUPDF) tp_window->SetNoUpdateFlag();

	tp_window->GenericAction([data, t](tpanelparentwin_nt *window) {
		tweetdispscr *tds = data->top_tds.get();
		if(!tds) return;

		wxBoxSizer *subhbox = new wxBoxSizer(wxHORIZONTAL);
		data->vbox->Add(subhbox, 0, wxALL | wxEXPAND, 1);

		tweetdispscr *subtd = new tweetdispscr(t, tds->tpi, window, subhbox);
		subtd->tds_flags |= TDSF::SUBTWEET;

		tds->subtweets.emplace_front(subtd);
		subtd->parent_tweet.set(tds);

		if(t->rtsrc && gc.rtdisp) {
			t->rtsrc->user->ImgHalfIsReady(PENDING_REQ::PROFIMG_DOWNLOAD);
			subtd->bm = new profimg_staticbitmap(tds->tpi, t->rtsrc->user->cached_profile_img_half, t->rtsrc->user, t, window->GetMainframe(), profimg_staticbitmap::PISBF::HALF);
		}
		else {
			t->user->ImgHalfIsReady(PENDING_REQ::PROFIMG_DOWNLOAD);
			subtd->bm = new profimg_staticbitmap(tds->tpi, t->user->cached_profile_img_half, t->user, t, window->GetMainframe(), profimg_staticbitmap::PISBF::HALF);
		}
		subhbox->Add(subtd->bm, 0, wxALL, 1);
		subhbox->Add(subtd, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);

		wxFont newfont;
		wxTextAttrEx tae(subtd->GetDefaultStyleEx());
		if(tae.HasFont()) {
			newfont = tae.GetFont();
		}
		else {
			newfont = subtd->GetFont();
		}
		int newsize = 0;
		if(newfont.IsOk()) newsize = ((newfont.GetPointSize() * 3) + 2) / 4;
		if(!newsize) newsize = 7;

		newfont.SetPointSize(newsize);
		tae.SetFont(newfont);
		subtd->SetFont(newfont);
		subtd->SetDefaultStyle(tae);
		subtd->PanelInsertEvt();
		subtd->DisplayTweet();

		if(!(window->pimpl()->tppw_flags & TPPWF::NOUPDATEONPUSH)) {
			subtd->ForceRefresh();
		}
		else subtd->gdb_flags |= tweetdispscr::GDB_F::NEEDSREFRESH;

		CheckLoadTweetReply(t, data->vbox, window, subtd, data->load_count - 1, data->top_tweet, tds);
	});
}

std::string tpanel_subtweet_pending_op::dump() {
	return string_format("Push inline tweet reply to tpanel: win: %s, top tds: %s, top tweet: %s",
			action_data->win ? cstr(action_data->win->GetThisName()) : "(null)",
			action_data->top_tds ? cstr(action_data->top_tds->GetThisName()) : "(null)",
			cstr(tweet_short_log_line(action_data->top_tweet->id))
	);
}
