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
#include <wx/dcmirror.h>

enum {
	NOTEBOOK_ID=42,
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

tpanelnotebook::tpanelnotebook(mainframe *owner_, wxWindow *parent) :
wxAuiNotebook(parent, NOTEBOOK_ID, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_TAB_EXTERNAL_MOVE | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_WINDOWLIST_BUTTON),
owner(owner_)
{
	wxColour foreground = GetForegroundColour();
	wxColour background = GetBackgroundColour();

	unsigned int forecount = foreground.Red() + foreground.Blue() + foreground.Green();
	unsigned int backcount = background.Red() + background.Blue() + background.Green();

	bool isreverse = forecount > backcount;

	SetArtProvider(new customTabArt(foreground, background, isreverse));
}

void tpanelnotebook::dragdrophandler(wxAuiNotebookEvent& event) {
	wxAuiNotebook* note= (wxAuiNotebook *) event.GetEventObject();
	if(note) {
		tpanelparentwin *tppw = (tpanelparentwin *) note->GetPage(event.GetSelection());
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
	if(GetPageCount()==0 && !(mainframelist.empty() || (++mainframelist.begin())==mainframelist.end())) {
		owner->Close();
	}
}

void tpanelnotebook::tabrightclickhandler(wxAuiNotebookEvent& event) {
	tpanelparentwin *tppw = (tpanelparentwin *) GetPage(event.GetSelection());
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
		twld.name =tp->name;
		twld.dispname = tp->dispname;
		twld.flags = tp->flags;
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

std::shared_ptr<tpanelglobal> tpanelglobal::Get() {
	if(tpg_glob.expired()) {
		std::shared_ptr<tpanelglobal> tmp=std::make_shared<tpanelglobal>();
		tpg_glob=tmp;
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
	GetVerifiedIcon(&verifiedicon, &verifiedicon_img);
	GetCloseIcon(&closeicon, 0);
	GetMultiUnreadIcon(&multiunreadicon, 0);
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

wxString tpanelload_pending_op::dump() {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	tpanelparentwin_nt *window=win.get();
	return wxString::Format(wxT("Push tweet to tpanel: %s, window: %p, pushflags: 0x%X"), (tp)?wxstrstd(tp->dispname).c_str():wxT("N/A"), window, pushflags);
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
			if(CheckFetchPendingSingleTweet(subt, pacc)) UnmarkPendingTweet(subt, 0);
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

		tweetdispscr *subtd = new tweetdispscr(t, window->pimpl()->scrollwin, window, subhbox);
		subtd->tds_flags |= TDSF::SUBTWEET;

		tds->subtweets.emplace_front(subtd);
		subtd->parent_tweet.set(tds);

		if(t->rtsrc && gc.rtdisp) {
			t->rtsrc->user->ImgHalfIsReady(UPDCF::DOWNLOADIMG);
			subtd->bm = new profimg_staticbitmap(window->pimpl()->scrollwin, t->rtsrc->user->cached_profile_img_half, t->rtsrc->user->id, t->id, window->GetMainframe(), profimg_staticbitmap::PISBF::HALF);
		}
		else {
		t->user->ImgHalfIsReady(UPDCF::DOWNLOADIMG);
		subtd->bm = new profimg_staticbitmap(window->pimpl()->scrollwin, t->user->cached_profile_img_half, t->user->id, t->id, window->GetMainframe(), profimg_staticbitmap::PISBF::HALF);
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

wxString tpanel_subtweet_pending_op::dump() {
	return wxString::Format(wxT("Push inline tweet reply to tpanel: %p, %p, %p"), action_data->vbox, action_data->win.get(), action_data->top_tds.get());
}
