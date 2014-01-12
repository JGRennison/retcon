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

#ifndef HGUARD_SRC_MEDIAWIN
#define HGUARD_SRC_MEDIAWIN

#include "univdefs.h"
#include "twit-common.h"
#include "flags.h"
#include <wx/panel.h>
#include <wx/gdicmn.h>
#include <wx/event.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/animate.h>
#include <wx/menu.h>
#include <wx/timer.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/window.h>
#include <wx/frame.h>
#include <wx/scrolwin.h>
#include <string>
#include <functional>
#include <vector>
#include <map>

struct media_display_win;
struct media_entity;

struct image_panel : public wxPanel {
	image_panel(wxWindow *parent, wxSize size = wxDefaultSize);
	void OnPaint(wxPaintEvent &event);
	void OnResize(wxSizeEvent &event);
	void UpdateBitmap();

	wxBitmap bm;
	wxImage img;

	DECLARE_EVENT_TABLE()
};

enum class MDZF {
	ZOOMSET         = 1<<0,
};
template<> struct enum_traits<MDZF> { static constexpr bool flags = true; };

struct media_display_win : public wxFrame {
	media_id_type media_id;
	std::string media_url;
	image_panel *sb;
	wxStaticText *st;
	wxBoxSizer *sz;
	std::function<void(bool)> setsavemenuenablestate;
	std::vector<std::function<void(wxMenuEvent &)> > menuopenhandlers;
	wxAnimation anim;
	bool is_animated = false;
	bool img_ok = false;
	unsigned int current_frame_index = 0;
	wxImage current_img;
	wxTimer animation_timer;
#if defined(__WXGTK__)
	wxAnimationCtrl anim_ctrl;
	bool using_anim_ctrl = false;
#endif
	std::map<int, std::function<void(wxCommandEvent &event)> > dynmenuhandlerlist;
	int next_dynmenu_id;
	wxMenu *zoom_menu;
	wxScrolledWindow *scrollwin = 0;
	flagwrapper<MDZF> zoomflags = 0;
	double zoomvalue = 1.0;

	media_display_win(wxWindow *parent, media_id_type media_id_);
	~media_display_win();
	void UpdateImage();
	void GetImage(wxString &message);
	media_entity *GetMediaEntity();
	void OnSave(wxCommandEvent &event);
	void SaveToDir(const wxString &dir);
	void DelayLoadNextAnimFrame();
	void OnAnimationTimer(wxTimerEvent& event);
	void dynmenudispatchhandler(wxCommandEvent &event);
	void OnMenuOpen(wxMenuEvent &event);
	void OnMenuZoomFit(wxCommandEvent &event);
	void OnMenuZoomOrig(wxCommandEvent &event);
	void OnMenuZoomSet(wxCommandEvent &event);
	void CalcSizes(wxSize imgsize, wxSize &winsize, wxSize &targimgsize);
	void ImgSizerLayout();

	DECLARE_EVENT_TABLE()
};

#endif
