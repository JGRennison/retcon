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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_DISPSCR
#define HGUARD_SRC_DISPSCR

#include "univdefs.h"
#include "safe_observer_ptr.h"
#include "uiutil.h"
#include "flags.h"
#include <wx/colour.h>
#include <wx/string.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/window.h>
#include <wx/timer.h>
#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <memory>
#include <forward_list>
#include <vector>
#include <functional>

struct panelparentwin_base;
struct tpanelparentwin_nt;
struct tpanelparentwin_user;
struct dispscr_base;
struct tpanelscrollwin;
struct profimg_staticbitmap;
struct tweet;
struct userdatacontainer;
struct tweetdispscr;
struct media_entity_raii_updater;
struct tpanel_item;
struct rounded_box_panel;

DECLARE_EVENT_TYPE(wxextGDB_Popup_Evt, -1)

enum class TDSF {    //for tweetdispscr.tds_flags
	SUBTWEET              = 1<<0,
	HIGHLIGHT             = 1<<1,
	HIDDEN                = 1<<2,
	IMGTHUMBHIDEOVERRIDE  = 1<<3,
	CANLOADMOREREPLIES    = 1<<4,
	INSERTEDPANELIDREFS   = 1<<5,
	DELETED               = 1<<6,
	TEXTHIDEOVERRIDE      = 1<<7,
};
template<> struct enum_traits<TDSF> { static constexpr bool flags = true; };

struct generic_disp_base : public commonRichTextCtrl, public safe_observer_ptr_target {
	panelparentwin_base *tppw;
	wxColour default_background_colour;
	wxColour default_foreground_colour;
	wxString thisname;

	enum class GDB_FF {
		FORCEFROZEN      = 1<<0,
		FORCEUNFROZEN    = 1<<1,
	};
	flagwrapper<GDB_FF> freezeflags = 0;

	enum class GDB_F {
		NEEDSREFRESH      = 1<<0,
		ACTIONBATCHMODE   = 1<<1,
	};
	flagwrapper<GDB_F> gdb_flags = 0;
	std::shared_ptr<wxMenu> menuptr;
	std::vector<std::function<void()> > action_batch;
	dyn_menu_handler_set dyn_menu_handlers;

	generic_disp_base(wxWindow *parent, panelparentwin_base *tppw_, long extraflags = 0, wxString thisname_ = wxT(""));
	void mousewheelhandler(wxMouseEvent &event);
	void urleventhandler(wxTextUrlEvent &event);
	virtual void urlhandler(wxString url) { }
	inline wxString GetThisName() const { return thisname; }
	virtual bool IsFrozen() const override;
	void ForceRefresh();
	void CheckRefresh();
	virtual tweet_ptr GetTweet() const { return tweet_ptr(); }
	virtual tweetdispscr *GetTDS() { return nullptr; }
	virtual flagwrapper<TDSF> GetTDSFlags() const { return 0; }
	void popupmenuhandler(wxCommandEvent &event);

	//These functions are primarily intended for delaying the execution of popup commands until the popup has been destroyed
	//This is because the command may otherwise cause this to be destroyed before the popup is, with unpleasant results...
	void StartActionBatch();
	void StopActionBatch();
	void DoAction(std::function<void()> &&f);
	void OnDynMenuCmd(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};
template<> struct enum_traits<generic_disp_base::GDB_FF> { static constexpr bool flags = true; };
template<> struct enum_traits<generic_disp_base::GDB_F> { static constexpr bool flags = true; };

inline void generic_disp_base::CheckRefresh() {
	if (gdb_flags & GDB_F::NEEDSREFRESH) {
		gdb_flags &= ~GDB_F::NEEDSREFRESH;
		ForceRefresh();
	}
}

struct dispscr_mouseoverwin : generic_disp_base, public safe_paired_observer_ptr<dispscr_base, dispscr_mouseoverwin>,
		public generic_popup_wrapper_hook {
	bool mouse_is_entered_self = false;
	bool mouse_is_entered_parent = false;
	int popup_count = 0;
	wxTimer mouseevttimer;

	dispscr_mouseoverwin(wxWindow *parent, panelparentwin_base *tppw_, wxString thisname_ = wxT(""));
	void Init();
	virtual void OnPairedPtrChange(dispscr_base *targ, dispscr_base *prevtarg, bool targdestructing) override;
	void Position(const wxSize &targ_size);
	void targsizehandler(wxSizeEvent &event);
	virtual bool RefreshContent() { return false; }
	virtual void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY, int noUnitsX, int noUnitsY,
			int xPos = 0, int yPos = 0, bool noRefresh = false ) override;
	void MouseSetParentMouseEntered(bool mouse_entered);
	virtual void BeforePopup() override;
	virtual void AfterPopup() override;

	private:
	void mouseenterhandler(wxMouseEvent &event);
	void mouseleavehandler(wxMouseEvent &event);
	void MouseRefCountChange();
	void OnMouseEventTimer(wxTimerEvent& event);
	bool IsDestroyable() const;

	public:
	DECLARE_EVENT_TABLE()
};

struct dispscr_base : public generic_disp_base, public safe_paired_observer_ptr<dispscr_mouseoverwin, dispscr_base>,
		public safe_observer_ptr_contained<dispscr_base>, public generic_popup_wrapper_hook {
	tpanel_item *tpi;
	wxBoxSizer *hbox;
	bool mouse_is_entered = false;
	int popup_count = 0;
	bool last_mouse_entered_state = false;

	static safe_observer_ptr_container<dispscr_base> mouse_is_entered_set;

	dispscr_base(wxWindow *parent, tpanel_item *item, panelparentwin_base *tppw_, wxBoxSizer *hbox_, wxString thisname_ = wxT(""));
	virtual void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY, int noUnitsX, int noUnitsY,
			int xPos = 0, int yPos = 0, bool noRefresh = false ) override;
	void mouseenterhandler(wxMouseEvent &event);
	void mouseleavehandler(wxMouseEvent &event);
	virtual dispscr_mouseoverwin *MakeMouseOverWin() { return 0; }
	virtual void PanelInsertEvt() { }
	virtual void PanelRemoveEvt() { }
	virtual void BeforePopup() override;
	virtual void AfterPopup() override;
	void MouseStateChange(bool check_others = true);
	void CheckMouseState();

	DECLARE_EVENT_TABLE()
};

struct tweetdispscr_mouseoverwin : public dispscr_mouseoverwin {
	tweet_ptr td;
	flagwrapper<TDSF> tds_flags = 0;
	safe_observer_ptr<tweetdispscr> current_tds;

	tweetdispscr_mouseoverwin(tweetdispscr *parent, panelparentwin_base *tppw_, wxString thisname_ = wxT(""));
	virtual bool RefreshContent() override;
	virtual void urlhandler(wxString url) override;
	void rightclickhandler(wxMouseEvent &event);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	virtual tweet_ptr GetTweet() const override { return td; }
	virtual tweetdispscr *GetTDS() override { return current_tds.get(); }
	virtual flagwrapper<TDSF> GetTDSFlags() const override { return tds_flags; }

	DECLARE_EVENT_TABLE()
};

enum {
	TDS_WID_UNHIDEIMGOVERRIDETIMER  = 1,
};

struct tweetdispscr : public dispscr_base {
	tweet_ptr td;
	profimg_staticbitmap *bm;
	profimg_staticbitmap *bm2;
	time_t updatetime;
	long reltimestart;
	long reltimeend;
	uint64_t rtid;
	flagwrapper<TDSF> tds_flags = 0;
	std::vector<safe_observer_ptr<tweetdispscr> > subtweets;
	safe_observer_ptr<tweetdispscr> parent_tweet;
	std::unique_ptr<wxTimer> imghideoverridetimer;
	std::function<void()> loadmorereplies;
	std::vector<media_entity_raii_updater> media_entity_updaters;
	safe_observer_ptr_container<rounded_box_panel> child_rounded_box_panels;
	observer_ptr<rounded_box_panel> parent_rounded_box_panel;
	wxBoxSizer *vbox = nullptr;
	unsigned int recursion_depth = 0;

	tweetdispscr(tweet_ptr_p td_, wxWindow *parent, tpanel_item *item, tpanelparentwin_nt *tppw_, wxBoxSizer *hbox_, wxString thisname_ = wxT(""));
	~tweetdispscr();
	void SetParent(observer_ptr<tweetdispscr> parent);
	bool CheckHiddenState();
	void DisplayTweet(bool redrawimg = false);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	void urlhandler(wxString url);
	void urleventhandler(wxTextUrlEvent &event);
	void rightclickhandler(wxMouseEvent &event);
	virtual tweetdispscr_mouseoverwin *MakeMouseOverWin() override;
	void unhideimageoverridetimeouthandler(wxTimerEvent &event);
	void unhideimageoverridetimeoutexec();
	void unhideimageoverridestarttimeout();
	void unhidetext();
	void rehidetext();

	virtual tweet_ptr GetTweet() const override { return td; }
	virtual tweetdispscr *GetTDS() override { return this; }
	virtual flagwrapper<TDSF> GetTDSFlags() const override { return tds_flags; }

	virtual void PanelInsertEvt() override;
	virtual void PanelRemoveEvt() override;

	DECLARE_EVENT_TABLE()
};

struct userdispscr : public dispscr_base {
	udc_ptr u;
	profimg_staticbitmap *bm;

	userdispscr(udc_ptr_p u_, tpanel_item *parent, tpanelparentwin_user *tppw_, wxBoxSizer *hbox_, wxString thisname_ = wxT(""));
	~userdispscr();
	void Display(bool redrawimg=false);

	void urleventhandler(wxTextUrlEvent &event);

	DECLARE_EVENT_TABLE()
};

void AppendUserMenuItems(wxMenu &menu, tweetactmenudata &map, int &nextid, udc_ptr user, tweet_ptr tw, panelparentwin_base *tppw);
void TweetReplaceStringSeq(std::function<void(const char *, size_t)> func, const std::string &str, int start, int end, int &track_byte, int &track_index);
void WriteToRichTextCtrlWithEmojis(commonRichTextCtrl &td, const std::string &str);

#endif
