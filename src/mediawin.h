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

#include <wx/animate.h>

struct image_panel : public wxPanel {
	image_panel(media_display_win *parent, wxSize size=wxDefaultSize);
	void OnPaint(wxPaintEvent &event);
	void OnResize(wxSizeEvent &event);
	void UpdateBitmap();

	wxBitmap bm;
	wxImage img;

	DECLARE_EVENT_TABLE()
};

enum {
	MDID_SAVE=1,
	MDID_TIMER_EVT=2,
};

struct media_display_win : public wxFrame {
	media_id_type media_id;
	std::string media_url;
	image_panel *sb;
	wxStaticText *st;
	wxBoxSizer *sz;
	wxMenuItem *savemenuitem;
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

	media_display_win(wxWindow *parent, media_id_type media_id_);
	~media_display_win();
	void UpdateImage();
	void GetImage(wxString &message);
	media_entity *GetMediaEntity();
	void OnSave(wxCommandEvent &event);
	void DelayLoadNextAnimFrame();
	void OnAnimationTimer(wxTimerEvent& event);

	DECLARE_EVENT_TABLE()
};
