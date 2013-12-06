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

struct generic_disp_base : public wxRichTextCtrl, public magic_ptr_base {
	panelparentwin_base *tppw;
	wxColour default_background_colour;
	wxColour default_foreground_colour;
	wxString thisname;
	enum {
		GDB_FF_FORCEFROZEN	= 1<<0,
		GDB_FF_FORCEUNFROZEN	= 1<<1,
	};
	unsigned int freezeflags = 0;

	generic_disp_base(wxWindow *parent, panelparentwin_base *tppw_, long extraflags = 0, wxString thisname_=wxT(""));
	void mousewheelhandler(wxMouseEvent &event);
	void urleventhandler(wxTextUrlEvent &event);
	virtual void urlhandler(wxString url) { }
	inline wxString GetThisName() const { return thisname; }
	virtual bool IsFrozen() const override;
	void ForceRefresh();

	DECLARE_EVENT_TABLE()
};

struct dispscr_mouseoverwin : generic_disp_base, public magic_paired_ptr_ts<dispscr_base, dispscr_mouseoverwin> {
	unsigned int mouse_refcount = 0;
	wxTimer mouseevttimer;

	dispscr_mouseoverwin(wxWindow *parent, panelparentwin_base *tppw_, wxString thisname_=wxT(""));
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

	dispscr_base(tpanelscrollwin *parent, panelparentwin_base *tppw_, wxBoxSizer *hbox_, wxString thisname_=wxT(""));
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
	TDSF_SUBTWEET	= 1<<0,
	TDSF_HIGHLIGHT	= 1<<1,
};

struct tweetdispscr_mouseoverwin : public dispscr_mouseoverwin {
	std::shared_ptr<tweet> td;
	unsigned int tds_flags = 0;

	tweetdispscr_mouseoverwin(wxWindow *parent, panelparentwin_base *tppw_, wxString thisname_=wxT(""));
	virtual bool RefreshContent() override;
	virtual void urlhandler(wxString url) override;
	void rightclickhandler(wxMouseEvent &event);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
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

	tweetdispscr(const std::shared_ptr<tweet> &td_, tpanelscrollwin *parent, tpanelparentwin_nt *tppw_, wxBoxSizer *hbox_, wxString thisname_=wxT(""));
	~tweetdispscr();
	void DisplayTweet(bool redrawimg=false);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	void urlhandler(wxString url);
	void urleventhandler(wxTextUrlEvent &event);
	void rightclickhandler(wxMouseEvent &event);
	virtual tweetdispscr_mouseoverwin *MakeMouseOverWin() override;

	DECLARE_EVENT_TABLE()
};

struct userdispscr : public dispscr_base {
	std::shared_ptr<userdatacontainer> u;
	profimg_staticbitmap *bm;

	userdispscr(const std::shared_ptr<userdatacontainer> &u_, tpanelscrollwin *parent, tpanelparentwin_user *tppw_, wxBoxSizer *hbox_, wxString thisname_=wxT(""));
	~userdispscr();
	void Display(bool redrawimg=false);

	void urleventhandler(wxTextUrlEvent &event);

	DECLARE_EVENT_TABLE()
};

void AppendUserMenuItems(wxMenu &menu, tweetactmenudata &map, int &nextid, std::shared_ptr<userdatacontainer> user, std::shared_ptr<tweet> tw);

#endif
