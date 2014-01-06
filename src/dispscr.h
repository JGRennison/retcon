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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_DISPSCR
#define HGUARD_SRC_DISPSCR

#include "univdefs.h"
#include "magic_ptr.h"
#include "uiutil.h"
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

struct panelparentwin_base;
struct tpanelparentwin_nt;
struct tpanelparentwin_user;
struct dispscr_base;
struct tpanelscrollwin;
struct profimg_staticbitmap;
struct tweet;
struct userdatacontainer;
struct tweetdispscr;

DECLARE_EVENT_TYPE(wxextGDB_Popup_Evt, -1)

struct generic_disp_base : public wxRichTextCtrl, public magic_ptr_base {
	panelparentwin_base *tppw;
	wxColour default_background_colour;
	wxColour default_foreground_colour;
	wxString thisname;

	enum {
		GDB_FF_FORCEFROZEN      = 1<<0,
		GDB_FF_FORCEUNFROZEN    = 1<<1,
	};
	unsigned int freezeflags = 0;

	enum {
		GDB_F_NEEDSREFRESH      = 1<<0,
	};
	unsigned int gdb_flags = 0;
	std::shared_ptr<wxMenu> menuptr;

	generic_disp_base(wxWindow *parent, panelparentwin_base *tppw_, long extraflags = 0, wxString thisname_ = wxT(""));
	void mousewheelhandler(wxMouseEvent &event);
	void urleventhandler(wxTextUrlEvent &event);
	virtual void urlhandler(wxString url) { }
	inline wxString GetThisName() const { return thisname; }
	virtual bool IsFrozen() const override;
	void ForceRefresh();
	void CheckRefresh() {
		if(gdb_flags & GDB_F_NEEDSREFRESH) {
			gdb_flags &= ~GDB_F_NEEDSREFRESH;
			ForceRefresh();
		}
	}
	virtual std::shared_ptr<tweet> GetTweet() const { return std::shared_ptr<tweet>(); }
	virtual tweetdispscr *GetTDS() { return nullptr; }
	virtual unsigned int GetTDSFlags() const { return 0; }
	void popupmenuhandler(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

struct dispscr_mouseoverwin : generic_disp_base, public magic_paired_ptr_ts<dispscr_base, dispscr_mouseoverwin> {
	unsigned int mouse_refcount = 0;
	wxTimer mouseevttimer;

	dispscr_mouseoverwin(wxWindow *parent, panelparentwin_base *tppw_, wxString thisname_ = wxT(""));
	virtual void OnMagicPairedPtrChange(dispscr_base *targ, dispscr_base *prevtarg, bool targdestructing) override;
	void Position(const wxSize &targ_size, const wxPoint &targ_position);
	void targmovehandler(wxMoveEvent &event);
	void targsizehandler(wxSizeEvent &event);
	virtual bool RefreshContent() { return false; }
	virtual void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
                               int noUnitsX, int noUnitsY,
                               int xPos = 0, int yPos = 0,
                               bool noRefresh = false ) override;
	void mouseenterhandler(wxMouseEvent &event);
	void mouseleavehandler(wxMouseEvent &event);
	void MouseEnterLeaveEvent(bool enter);
	void OnMouseEventTimer(wxTimerEvent& event);

	DECLARE_EVENT_TABLE()
};

struct dispscr_base : public generic_disp_base, public magic_paired_ptr_ts<dispscr_mouseoverwin, dispscr_base> {
	tpanelscrollwin *tpsw;
	wxBoxSizer *hbox;

	dispscr_base(tpanelscrollwin *parent, panelparentwin_base *tppw_, wxBoxSizer *hbox_, wxString thisname_ = wxT(""));
	virtual void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
                               int noUnitsX, int noUnitsY,
                               int xPos = 0, int yPos = 0,
                               bool noRefresh = false ) override;
	void mouseenterhandler(wxMouseEvent &event);
	void mouseleavehandler(wxMouseEvent &event);
	virtual dispscr_mouseoverwin *MakeMouseOverWin() { return 0; }

	DECLARE_EVENT_TABLE()
};

enum {	//for tweetdispscr.tds_flags
	TDSF_SUBTWEET              = 1<<0,
	TDSF_HIGHLIGHT             = 1<<1,
	TDSF_HIDDEN                = 1<<2,
	TDSF_IMGTHUMBHIDEOVERRIDE  = 1<<3,
	TDSF_CANLOADMOREREPLIES    = 1<<4,
};

struct tweetdispscr_mouseoverwin : public dispscr_mouseoverwin {
	std::shared_ptr<tweet> td;
	unsigned int tds_flags = 0;
	magic_ptr_ts<tweetdispscr> current_tds;

	tweetdispscr_mouseoverwin(wxWindow *parent, panelparentwin_base *tppw_, wxString thisname_ = wxT(""));
	virtual bool RefreshContent() override;
	virtual void urlhandler(wxString url) override;
	void rightclickhandler(wxMouseEvent &event);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	virtual std::shared_ptr<tweet> GetTweet() const override { return td; }
	virtual tweetdispscr *GetTDS() override { return current_tds.get(); }
	virtual unsigned int GetTDSFlags() const override { return tds_flags; }

	DECLARE_EVENT_TABLE()
};

enum {
	TDS_WID_UNHIDEIMGOVERRIDETIMER  = 1,
};

struct tweetdispscr : public dispscr_base {
	std::shared_ptr<tweet> td;
	profimg_staticbitmap *bm;
	profimg_staticbitmap *bm2;
	time_t updatetime;
	long reltimestart;
	long reltimeend;
	uint64_t rtid;
	unsigned int tds_flags = 0;
	std::forward_list<magic_ptr_ts<tweetdispscr> > subtweets;
	magic_ptr_ts<tweetdispscr> parent_tweet;
	std::unique_ptr<wxTimer> imghideoverridetimer;
	std::function<void()> loadmorereplies;

	tweetdispscr(const std::shared_ptr<tweet> &td_, tpanelscrollwin *parent, tpanelparentwin_nt *tppw_, wxBoxSizer *hbox_, wxString thisname_ = wxT(""));
	~tweetdispscr();
	void DisplayTweet(bool redrawimg = false);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	void urlhandler(wxString url);
	void urleventhandler(wxTextUrlEvent &event);
	void rightclickhandler(wxMouseEvent &event);
	virtual tweetdispscr_mouseoverwin *MakeMouseOverWin() override;
	void unhideimageoverridetimeouthandler(wxTimerEvent &event);
	void unhideimageoverridetimeoutexec();
	void unhideimageoverridestarttimeout();

	virtual std::shared_ptr<tweet> GetTweet() const override { return td; }
	virtual tweetdispscr *GetTDS() override { return this; }
	virtual unsigned int GetTDSFlags() const override { return tds_flags; }

	DECLARE_EVENT_TABLE()
};

struct userdispscr : public dispscr_base {
	std::shared_ptr<userdatacontainer> u;
	profimg_staticbitmap *bm;

	userdispscr(const std::shared_ptr<userdatacontainer> &u_, tpanelscrollwin *parent, tpanelparentwin_user *tppw_, wxBoxSizer *hbox_, wxString thisname_ = wxT(""));
	~userdispscr();
	void Display(bool redrawimg=false);

	void urleventhandler(wxTextUrlEvent &event);

	DECLARE_EVENT_TABLE()
};

void AppendUserMenuItems(wxMenu &menu, tweetactmenudata &map, int &nextid, std::shared_ptr<userdatacontainer> user, std::shared_ptr<tweet> tw);
void TweetReplaceStringSeq(std::function<void(const char *, size_t)> func, const std::string &str, int start, int end, int &track_byte, int &track_index);
std::string TweetReplaceAllStringSeqs(const std::string &str);

#endif
