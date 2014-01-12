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
#include <wx/textdlg.h>
#include <wx/msgdlg.h>
#include <wx/file.h>
#include <cmath>

#if defined(__WXGTK__)
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

BEGIN_EVENT_TABLE(image_panel, wxPanel)
	EVT_PAINT(image_panel::OnPaint)
	EVT_SIZE(image_panel::OnResize)
END_EVENT_TABLE()

image_panel::image_panel(wxWindow *parent, wxSize size) : wxPanel(parent, wxID_ANY, wxDefaultPosition, size) {

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

enum {
	MDID_SAVE            = 1,
	MDID_TIMER_EVT       = 2,
	MDID_ZOOM_FIT        = 3,
	MDID_ZOOM_ORIG       = 4,
	MDID_ZOOM_SET        = 5,
	MDID_DYN_START       = wxID_HIGHEST + 1,
};

BEGIN_EVENT_TABLE(media_display_win, wxFrame)
	EVT_MENU(MDID_SAVE,  media_display_win::OnSave)
	EVT_TIMER(MDID_TIMER_EVT, media_display_win::OnAnimationTimer)
	EVT_MENU_OPEN(media_display_win::OnMenuOpen)
	EVT_MENU(MDID_ZOOM_FIT,  media_display_win::OnMenuZoomFit)
	EVT_MENU(MDID_ZOOM_ORIG,  media_display_win::OnMenuZoomOrig)
	EVT_MENU(MDID_ZOOM_SET,  media_display_win::OnMenuZoomSet)
END_EVENT_TABLE()

media_display_win::media_display_win(wxWindow *parent, media_id_type media_id_)
	: wxFrame(parent, wxID_ANY, wxstrstd(ad.media_list[media_id_].media_url)), media_id(media_id_),
		sb(0), st(0), sz(0), next_dynmenu_id(MDID_DYN_START) {
	Freeze();
	media_entity *me=&ad.media_list[media_id_];
	me->win=this;

	if(me->flags & MEF::LOAD_FULL && !(me->flags & MEF::HAVE_FULL)) {
		//try to load from file
		char *data=0;
		size_t size;
		if(LoadFromFileAndCheckHash(me->cached_full_filename(), me->full_img_sha1, data, size)) {
			me->flags |= MEF::HAVE_FULL;
			me->fulldata.assign(data, size);	//redundant copy, but oh well
		}
		if(data) free(data);
	}
	if(!(me->flags & MEF::FULL_NET_INPROGRESS) && !(me->flags & MEF::HAVE_FULL) && me->media_url.size()) {
		new mediaimgdlconn(me->media_url, media_id_, MIDC::FULLIMG | MIDC::OPPORTUNIST_THUMB | MIDC::OPPORTUNIST_REDRAW_TWEETS);
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

	zoom_menu = new wxMenu;

	menuopenhandlers.push_back([this](wxMenuEvent &event) {
		if(event.GetMenu() != zoom_menu) return;

		wxMenuItemList items = zoom_menu->GetMenuItems();		//make a copy to avoid memory issues if Destroy modifies the list
		for(auto &it : items) {
			zoom_menu->Destroy(it);
		}

#if defined(__WXGTK__)
		if(using_anim_ctrl) {
			wxMenuItem *wmi = zoom_menu->Append(MDID_ZOOM_ORIG, wxT("&Original Size"), wxT(""), wxITEM_CHECK);
			wmi->Check(true);
			wmi->Enable(false);
			return;
		}
#endif

		wxMenuItem *wmi1 = zoom_menu->Append(MDID_ZOOM_FIT, wxT("&Fit to Window"), wxT(""), wxITEM_CHECK);
		wmi1->Check(zoomflags == static_cast<MDZF>(0));
		wxMenuItem *wmi2 = zoom_menu->Append(MDID_ZOOM_ORIG, wxT("&Original Size"), wxT(""), wxITEM_CHECK);
		wmi2->Check(zoomflags & MDZF::ZOOMSET && zoomvalue == 1.0);
		zoom_menu->Append(MDID_ZOOM_SET, wxT("&Zoom to..."));
	});

	menuBar->Append(zoom_menu, wxT("&Zoom"));

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

		#if defined(__WXGTK__)
		if(using_anim_ctrl) {
			wxSize imgsize(current_img.GetWidth(), current_img.GetHeight());
			wxSize origwinsize = ClientToWindowSize(imgsize);
			sz->Add(&anim_ctrl, 1, wxEXPAND | wxALIGN_CENTRE);
			anim_ctrl.Play();
			SetSize(origwinsize);
			SetMinSize(origwinsize);	//don't allow resizing the window, as animation controls don't scale
			SetMaxSize(origwinsize);
			return;
		}
		#endif

		ImgSizerLayout();
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

void media_display_win::CalcSizes(wxSize imgsize, wxSize &winsize, wxSize &targimgsize) {
	int scrwidth, scrheight;
	wxClientDisplayRect(0, 0, &scrwidth, &scrheight);
	scrwidth -= gc.mediawinscreensizewidthreduction;
	scrheight -= gc.mediawinscreensizeheightreduction;
	if(scrwidth < 1) scrwidth = 1;
	if(scrheight < 1) scrheight = 1;

	if(zoomflags & MDZF::ZOOMSET) {
		targimgsize.SetWidth(imgsize.GetWidth() * zoomvalue);
		targimgsize.SetHeight(imgsize.GetHeight() * zoomvalue);

		winsize = ClientToWindowSize(targimgsize);
		if(winsize.GetWidth() > scrwidth) winsize.SetWidth(scrwidth);
		if(winsize.GetHeight() > scrheight) winsize.SetHeight(scrheight);
	}
	else {
		winsize = ClientToWindowSize(imgsize);
		if(winsize.GetWidth() > scrwidth) {
			double scale = (((double) scrwidth) / ((double) winsize.GetWidth()));
			winsize.Scale(scale, scale);
		}
		if(winsize.GetHeight() > scrheight) {
			double scale = (((double) scrheight) / ((double) winsize.GetHeight()));
			winsize.Scale(scale, scale);
		}
		targimgsize = WindowToClientSize(winsize);
	}
}

void media_display_win::ImgSizerLayout() {
	wxSize imgsize(current_img.GetWidth(), current_img.GetHeight());

	wxSize winsize, targsize;
	CalcSizes(imgsize, winsize, targsize);

	//LogMsgFormat(LOGT::OTHERTRACE, wxT("Media Display Window: targsize: %d, %d, imgsize: %d, %d, origwinsize: %d, %d, winsize: %d, %d, scr: %d, %d"), targsize.GetWidth(), targsize.GetHeight(), img.GetWidth(), img.GetHeight(), origwinsize.GetWidth(), origwinsize.GetHeight(), winsize.GetWidth(), winsize.GetHeight(), scrwidth, scrheight);

	if(!sb) {
		sb=new image_panel(this, targsize);
		sb->img = current_img;
	}
	else {
		sz->Detach(sb);
	}

	if(zoomflags & MDZF::ZOOMSET) {
		if(!scrollwin) {
			scrollwin = new wxScrolledWindow(this);
			scrollwin->SetScrollRate(1, 1);
			sz->Add(scrollwin, 1, wxEXPAND | wxALIGN_CENTRE);
			sb->Reparent(scrollwin);
		}
		scrollwin->SetVirtualSize(targsize);
		sb->SetPosition(wxPoint(0,0));
		sb->SetSize(targsize);
		wxSize winsizeinc(winsize);
		winsizeinc.IncBy(50);
		SetSize(winsizeinc);    //This is to encourage scrollbars to disappear
		Layout();
		SetSize(winsize);
	}
	else {
		if(scrollwin) {
			sb->Reparent(this);
			sz->Detach(scrollwin);
			scrollwin->Destroy();
			scrollwin = 0;
		}
		sb->SetSize(targsize);
		SetSize(winsize);
		sb->SetMinSize(wxSize(1, 1));
		sz->Add(sb, 1, wxEXPAND | wxALIGN_CENTRE);
	}
	sb->UpdateBitmap();
	Layout();
}

void media_display_win::GetImage(wxString &message) {
	media_entity *me=GetMediaEntity();
	if(me) {
		if(me->flags & MEF::HAVE_FULL) {
			is_animated = false;

			wxMemoryInputStream memstream2(me->fulldata.data(), me->fulldata.size());
			current_img.LoadFile(memstream2, wxBITMAP_TYPE_ANY);
			img_ok = current_img.IsOk();

			if(img_ok) {
				wxMemoryInputStream memstream(me->fulldata.data(), me->fulldata.size());
				if(anim.Load(memstream, wxANIMATION_TYPE_ANY)) {
					if(anim.GetFrameCount() > 1) {
						LogMsgFormat(LOGT::OTHERTRACE, wxT("media_display_win::GetImage found animation: %d frames"), anim.GetFrameCount());
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
				LogMsgFormat(LOGT::OTHERERR, wxT("media_display_win::GetImage: Image is not OK, corrupted or partial download?"));
			}
			return;
		}
		else if(me->flags & MEF::FULL_FAILED) {
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

void media_display_win::OnMenuZoomFit(wxCommandEvent &event) {
	zoomflags &= ~MDZF::ZOOMSET;
	ImgSizerLayout();
}

void media_display_win::OnMenuZoomOrig(wxCommandEvent &event) {
	zoomflags |= MDZF::ZOOMSET;
	zoomvalue = 1.0;
	ImgSizerLayout();
}

void media_display_win::OnMenuZoomSet(wxCommandEvent &event) {
	wxString str = ::wxGetTextFromUser(wxT("Enter zoom value in percent (%)"), wxT("Input Number"), wxT(""), this);
	if(!str.IsEmpty()) {
		std::string stdstr = stdstrwx(str);
		char *pos = 0;
		double value = strtod(stdstr.c_str(), &pos);
		if((pos && *pos != 0) || (!std::isnormal(value)) || value <= 0) {
			::wxMessageBox(wxString::Format(wxT("'%s' does not appear to be a positive finite number"), str.c_str()), wxT("Invalid Input"), wxOK | wxICON_EXCLAMATION, this);
			return;
		}
		zoomflags |= MDZF::ZOOMSET;
		zoomvalue = value / 100;
		ImgSizerLayout();
	}
}
