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

#include "univdefs.h"
#include "mediawin.h"
#include "alldata.h"
#include "twit.h"
#include "util.h"
#include "log.h"
#include "socket.h"
#include "cfg.h"
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/dcclient.h>
#include <wx/dcscreen.h>
#include <wx/tokenzr.h>
#include <wx/filename.h>
#include <wx/mstream.h>
#include <wx/file.h>

#if defined(__WXGTK__)
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

BEGIN_EVENT_TABLE(image_panel, wxPanel)
	EVT_PAINT(image_panel::OnPaint)
	EVT_SIZE(image_panel::OnResize)
END_EVENT_TABLE()

image_panel::image_panel(media_display_win *parent, wxSize size) : wxPanel(parent, wxID_ANY, wxDefaultPosition, size) {

}

void image_panel::OnPaint(wxPaintEvent &event) {
	wxPaintDC dc(this);
	dc.DrawBitmap(bm, (GetSize().GetWidth() - bm.GetWidth())/2, (GetSize().GetHeight() - bm.GetHeight())/2, 0);
}

void image_panel::OnResize(wxSizeEvent &event) {
	UpdateBitmap();
}

void image_panel::UpdateBitmap() {
	if(GetSize().GetWidth() == img.GetWidth() && GetSize().GetHeight() == img.GetHeight()) {
		bm=wxBitmap(img);
	}
	else {
		double wratio = ((double) GetSize().GetWidth()) / ((double) img.GetWidth());
		double hratio = ((double) GetSize().GetHeight()) / ((double) img.GetHeight());
		double targratio = std::min(wratio, hratio);
		int targheight = targratio * img.GetHeight();
		int targwidth = targratio * img.GetWidth();
		bm=wxBitmap(img.Scale(targwidth, targheight, wxIMAGE_QUALITY_HIGH));
	}
	Refresh();
}

BEGIN_EVENT_TABLE(media_display_win, wxFrame)
	EVT_MENU(MDID_SAVE,  media_display_win::OnSave)
	EVT_TIMER(MDID_TIMER_EVT, media_display_win::OnAnimationTimer)
	EVT_MENU_OPEN(media_display_win::OnMenuOpen)
END_EVENT_TABLE()

media_display_win::media_display_win(wxWindow *parent, media_id_type media_id_)
	: wxFrame(parent, wxID_ANY, wxstrstd(ad.media_list[media_id_].media_url)), media_id(media_id_), sb(0), st(0), sz(0) {
	Freeze();
	media_entity *me=&ad.media_list[media_id_];
	me->win=this;

	if(me->flags&ME_LOAD_FULL && !(me->flags&ME_HAVE_FULL)) {
		//try to load from file
		char *data=0;
		size_t size;
		if(LoadFromFileAndCheckHash(me->cached_full_filename(), me->full_img_sha1, data, size)) {
			me->flags|=ME_HAVE_FULL;
			me->fulldata.assign(data, size);	//redundant copy, but oh well
		}
		if(data) free(data);
	}
	if(!(me->flags&ME_FULL_NET_INPROGRESS) && !(me->flags&ME_HAVE_FULL) && me->media_url.size()) {
		new mediaimgdlconn(me->media_url, media_id_, MIDC_FULLIMG | MIDC_OPPORTUNIST_THUMB | MIDC_OPPORTUNIST_REDRAW_TWEETS);
	}

	wxMenu *menuF = new wxMenu;
	menuF->Append(MDID_SAVE, wxT("&Save..."));

	menuopenhandlers.push_back([this, menuF](wxMenuEvent &event) {
		if(event.GetMenu() != menuF) return;

		wxMenuItemList items = menuF->GetMenuItems();		//make a copy to avoid memory issues if Destroy modifies the list
		for(auto &it : items) {
			menuF->Destroy(it);
		}

		menuF->Append(MDID_SAVE, wxT("&Save..."));

		wxStringTokenizer tkn(gc.gcfg.mediasave_directorylist.val, wxT("\r\n"), wxTOKEN_STRTOK);

		bool added_seperator = false;

		while(tkn.HasMoreTokens()) {
			wxString token = tkn.GetNextToken();

			if(!wxFileName::DirExists(token)) continue;

			if(!added_seperator) {
				menuF->AppendSeparator();
				added_seperator = true;
			}

			menuF->Append(next_dynmenu_id, wxT("Save to: ") + token);

			dynmenuhandlerlist[next_dynmenu_id] = [token, this](wxCommandEvent &event) {
				this->SaveToDir(token);
			};
			Connect(next_dynmenu_id, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(media_display_win::dynmenudispatchhandler));
			next_dynmenu_id++;
		}
	});

	wxMenuBar *menuBar = new wxMenuBar;
	int menubarcount = menuBar->GetMenuCount();
	setsavemenuenablestate = [menubarcount, this, menuBar](bool enable) {
		menuBar->EnableTop(menubarcount, enable);
	};

	menuBar->Append(menuF, wxT("&Save Image"));

	SetMenuBar( menuBar );

	sz=new wxBoxSizer(wxVERTICAL);
	SetSizer(sz);
	UpdateImage();
	Thaw();
	Show();
}

media_display_win::~media_display_win() {
	media_entity *me=GetMediaEntity();
	if(me) me->win=0;
}

void media_display_win::dynmenudispatchhandler(wxCommandEvent &event) {
	dynmenuhandlerlist[event.GetId()](event);
}

void media_display_win::OnMenuOpen(wxMenuEvent &event) {
	for(auto &it : menuopenhandlers) {
		it(event);
	}
}

void media_display_win::UpdateImage() {
	wxString message;
	GetImage(message);
	if(img_ok) {
		setsavemenuenablestate(true);
		if(st) {
			sz->Detach(st);
			st->Destroy();
			st=0;
		}
		wxSize imgsize(current_img.GetWidth(), current_img.GetHeight());
		wxSize origwinsize = ClientToWindowSize(imgsize);
		wxSize winsize = origwinsize;
		int scrwidth, scrheight;
		wxClientDisplayRect(0, 0, &scrwidth, &scrheight);
		if(winsize.GetWidth() > scrwidth) {
			double scale = (((double) scrwidth) / ((double) winsize.GetWidth()));
			winsize.Scale(scale, scale);
		}
		if(winsize.GetHeight() > scrheight) {
			double scale = (((double) scrheight) / ((double) winsize.GetHeight()));
			winsize.Scale(scale, scale);
		}
		wxSize targsize = WindowToClientSize(winsize);
		//LogMsgFormat(LFT_OTHERTRACE, wxT("Media Display Window: targsize: %d, %d, imgsize: %d, %d, origwinsize: %d, %d, winsize: %d, %d, scr: %d, %d"), targsize.GetWidth(), targsize.GetHeight(), img.GetWidth(), img.GetHeight(), origwinsize.GetWidth(), origwinsize.GetHeight(), winsize.GetWidth(), winsize.GetHeight(), scrwidth, scrheight);

		#if defined(__WXGTK__)
		if(using_anim_ctrl) {
			sz->Add(&anim_ctrl, 1, wxEXPAND | wxALIGN_CENTRE);
			anim_ctrl.Play();
			SetSize(winsize);
			SetMinSize(winsize);	//don't allow resizing the window, as animation controls don't scale
			SetMaxSize(winsize);
			return;
		}
		#endif

		if(!sb) {
			sb=new image_panel(this, targsize);
			sb->img = current_img;
			sb->SetMinSize(wxSize(1, 1));
			sz->Add(sb, 1, wxEXPAND | wxALIGN_CENTRE);
		}
		sb->SetSize(targsize);
		SetSize(winsize);
		sb->UpdateBitmap();
		if(is_animated) DelayLoadNextAnimFrame();
	}
	else {
		setsavemenuenablestate(false);
		if(sb) {
			sz->Detach(sb);
			sb->Destroy();
			sb=0;
		}
		if(!st) {
			st=new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
			sz->Add(st, 0, wxALIGN_CENTRE);
			sz->SetMinSize(200, 200);
		}
		else st->SetLabel(message);
		sz->Fit(this);
	}
}

void media_display_win::GetImage(wxString &message) {
	media_entity *me=GetMediaEntity();
	if(me) {
		if(me->flags&ME_HAVE_FULL) {
			is_animated = false;

			wxMemoryInputStream memstream2(me->fulldata.data(), me->fulldata.size());
			current_img.LoadFile(memstream2, wxBITMAP_TYPE_ANY);
			img_ok = current_img.IsOk();

			if(img_ok) {
				wxMemoryInputStream memstream(me->fulldata.data(), me->fulldata.size());
				if(anim.Load(memstream, wxANIMATION_TYPE_ANY)) {
					if(anim.GetFrameCount() > 1) {
						LogMsgFormat(LFT_OTHERTRACE, wxT("media_display_win::GetImage found animation: %d frames"), anim.GetFrameCount());
						is_animated = true;
						current_img = anim.GetFrame(0);
						current_frame_index = 0;
						animation_timer.SetOwner(this, MDID_TIMER_EVT);
					}
					#if defined(__WXGTK__)
					else {
						if(!using_anim_ctrl && !gdk_pixbuf_animation_is_static_image(anim.GetPixbuf())) {
							using_anim_ctrl = true;
							anim_ctrl.Create(this, wxID_ANY, anim);
						}
					}
					#endif
				}
			}
			else {
				LogMsgFormat(LFT_OTHERERR, wxT("media_display_win::GetImage: Image is not OK, corrupted or partial download?"));
			}
			return;
		}
		else if(me->flags&ME_FULL_FAILED) {
			message=wxT("Failed to Load Image");
		}
		else {
			message=wxT("Loading Image");
		}
	}
	else {
		message=wxT("No Image");
	}
	img_ok = false;
	return;
}

void media_display_win::DelayLoadNextAnimFrame() {
	int delay = anim.GetDelay(current_frame_index);
	if(delay >= 0) animation_timer.Start(delay, wxTIMER_ONE_SHOT);
}

void media_display_win::OnAnimationTimer(wxTimerEvent& event) {
	if(!sb) return;
	current_frame_index++;
	if(current_frame_index >= anim.GetFrameCount()) current_frame_index = 0;
	current_img = anim.GetFrame(current_frame_index);
	sb->img = current_img;
	sb->UpdateBitmap();
	sb->Refresh();
	DelayLoadNextAnimFrame();
}

media_entity *media_display_win::GetMediaEntity() {
	auto it=ad.media_list.find(media_id);
	if(it!=ad.media_list.end()) {
		return &it->second;
	}
	else return 0;
}

void media_display_win::OnSave(wxCommandEvent &event) {
	SaveToDir(wxT(""));
}

void media_display_win::SaveToDir(const wxString &dir) {
	media_entity *me=GetMediaEntity();
	if(me) {
		wxString hint;
		wxString ext;
		bool hasext;
		wxFileName::SplitPath(wxstrstd(me->media_url), 0, 0, &hint, &ext, &hasext, wxPATH_UNIX);
		if(hasext) hint+=wxT(".")+ext;
		wxString newhint;
		if(hint.EndsWith(wxT(":large"), &newhint)) hint=newhint;
		wxString filename=wxFileSelector(wxT("Save Image"), dir, hint, ext, wxT("*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);
		if(filename.Len()) {
			wxFile file(filename, wxFile::write);
			file.Write(me->fulldata.data(), me->fulldata.size());
		}
	}
}
