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
//  2013 - j.g.rennison@gmail.com
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