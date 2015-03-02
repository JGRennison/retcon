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

#include "univdefs.h"
#include "alldata.h"
#include "cfg.h"
#include "log.h"
#include "mediawin.h"
#include "socket-ops.h"
#include "twit.h"
#include "util.h"
#include <wx/animate.h>
#include <wx/bitmap.h>
#include <wx/dcclient.h>
#include <wx/dcscreen.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mediactrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/mstream.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/timer.h>
#include <wx/tokenzr.h>
#include <wx/window.h>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

#if defined(__WXGTK__)
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

struct image_panel : public wxPanel {
	wxBitmap bm;
	wxImage img;

	image_panel(wxWindow *parent, wxSize size = wxDefaultSize)
			: wxPanel(parent, wxID_ANY, wxDefaultPosition, size) { }

	void OnPaint(wxPaintEvent &event) {
		wxPaintDC dc(this);
		dc.DrawBitmap(bm, (GetSize().GetWidth() - bm.GetWidth()) / 2, (GetSize().GetHeight() - bm.GetHeight()) / 2, 0);
	}

	void OnResize(wxSizeEvent &event) {
		UpdateBitmap();
	}

	void UpdateBitmap() {
		if(GetSize().GetWidth() == img.GetWidth() && GetSize().GetHeight() == img.GetHeight()) {
			bm = wxBitmap(img);
		}
		else {
			double wratio = ((double) GetSize().GetWidth()) / ((double) img.GetWidth());
			double hratio = ((double) GetSize().GetHeight()) / ((double) img.GetHeight());
			double targratio = std::min(wratio, hratio);
			int targheight = targratio * img.GetHeight();
			int targwidth = targratio * img.GetWidth();
			if(targheight < 1)
				targheight = 1;
			if(targwidth < 1)
				targwidth = 1;
			bm = wxBitmap(img.Scale(targwidth, targheight, wxIMAGE_QUALITY_HIGH));
		}
		Refresh();
	}

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(image_panel, wxPanel)
	EVT_PAINT(image_panel::OnPaint)
	EVT_SIZE(image_panel::OnResize)
END_EVENT_TABLE()

struct media_ctrl_panel : public wxMediaCtrl, public magic_ptr_base {
	media_ctrl_panel(wxWindow *parent, wxSize size = wxDefaultSize)
			: wxMediaCtrl(parent, wxID_ANY, wxT(""), wxDefaultPosition, size) { }

	void OnMediaLoaded(wxMediaEvent& evt) {
		Play();
	}

	void OnMediaFinished(wxMediaEvent& evt) {
		Play();
	}

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(media_ctrl_panel, wxMediaCtrl)
	EVT_MEDIA_LOADED(wxID_ANY, media_ctrl_panel::OnMediaLoaded)
	EVT_MEDIA_FINISHED(wxID_ANY, media_ctrl_panel::OnMediaFinished)
END_EVENT_TABLE()

enum class MDZF {
	ZOOMSET         = 1<<0,
};
template<> struct enum_traits<MDZF> { static constexpr bool flags = true; };

struct media_display_win_pimpl : public wxEvtHandler {
	media_display_win *win;
	media_id_type media_id;
	std::string media_url;
	image_panel *sb = nullptr;
	wxStaticText *st = nullptr;
	wxBoxSizer *st_sizer = nullptr;
	wxBoxSizer *sz = nullptr;
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

	magic_ptr_ts<media_ctrl_panel> media_ctrl;
	bool using_media_ctrl = false;
	bool is_video = false;
	std::vector<video_entity::video_variant> video_variants;
	std::string mp4_save_url;
	std::string webm_save_url;

	std::map<int, std::function<void(wxCommandEvent &event)> > dynmenuhandlerlist;
	int next_dynmenu_id;
	wxMenu *zoom_menu = nullptr;
	wxScrolledWindow *scrollwin  = nullptr;
	flagwrapper<MDZF> zoomflags = 0;
	double zoomvalue = 1.0;

	media_display_win_pimpl(media_display_win *win_, media_id_type media_id_);
	~media_display_win_pimpl();
	void AddSaveMenu(wxMenuBar *menuBar, const wxString &title, std::function<std::string(observer_ptr<media_entity> me)> get_url,
			std::function<void(observer_ptr<media_entity>, wxString)> save_action);
	void StartFetchImageData();
	void UpdateImage();
	void GetImage(wxString &message);
	observer_ptr<media_entity> GetMediaEntity();
	void SaveToDir(const wxString &dir, const wxString &title, const wxString &url, std::function<void(observer_ptr<media_entity>, wxString)> save_action);
	void DelayLoadNextAnimFrame();
	void OnAnimationTimer(wxTimerEvent& event);
	void dynmenudispatchhandler(wxCommandEvent &event);
	void OnMenuOpen(wxMenuEvent &event);
	void OnMenuZoomFit(wxCommandEvent &event);
	void OnMenuZoomOrig(wxCommandEvent &event);
	void OnMenuZoomSet(wxCommandEvent &event);
	void ClearAllUnlessShowingImage();
	void ClearAll();
	void ShowErrorMessage(const wxString &message);
	void CalcSizes(wxSize imgsize, wxSize &winsize, wxSize &targimgsize);
	void DoSizerLayout();
	void TryLoadVideo();
	void NotifyVideoLoadSuccess(const std::string &url);
	void NotifyVideoLoadFailure(const std::string &url);

	DECLARE_EVENT_TABLE()
};

enum {
	MDID_TIMER_EVT       = 2,
	MDID_ZOOM_FIT        = 3,
	MDID_ZOOM_ORIG       = 4,
	MDID_ZOOM_SET        = 5,
	MDID_DYN_START       = wxID_HIGHEST + 1,
};

BEGIN_EVENT_TABLE(media_display_win_pimpl, wxEvtHandler)
	EVT_TIMER(MDID_TIMER_EVT, media_display_win_pimpl::OnAnimationTimer)
	EVT_MENU_OPEN(media_display_win_pimpl::OnMenuOpen)
	EVT_MENU(MDID_ZOOM_FIT, media_display_win_pimpl::OnMenuZoomFit)
	EVT_MENU(MDID_ZOOM_ORIG, media_display_win_pimpl::OnMenuZoomOrig)
	EVT_MENU(MDID_ZOOM_SET, media_display_win_pimpl::OnMenuZoomSet)
END_EVENT_TABLE()

media_display_win::media_display_win(wxWindow *parent, media_id_type media_id_)
	: wxFrame(parent, wxID_ANY, wxT("")) {

	pimpl.reset(new media_display_win_pimpl(this, media_id_));
}

media_display_win_pimpl::media_display_win_pimpl(media_display_win *win_, media_id_type media_id_)
	: win(win_), media_id(media_id_), next_dynmenu_id(MDID_DYN_START) {

	win->PushEventHandler(this);

	win->Freeze();
	media_entity *me = ad.media_list[media_id_].get();
	me->win = win;

	if(me->video && me->video->variants.size() > 0) {
		// This is a video
		// Don't request static image
		is_video = true;

		video_entity::video_variant *mp4 = nullptr;
		video_entity::video_variant *webm = nullptr;
		for(auto &vv : me->video->variants) {
			LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl: found video variant: %s (%u)", cstr(vv.content_type), vv.bitrate);
			if(vv.content_type == "video/mp4") {
				if(!mp4 || vv.bitrate > mp4->bitrate)
					mp4 = &vv;
			}
			if(vv.content_type == "video/webm") {
				if(!webm || vv.bitrate > webm->bitrate)
					webm = &vv;
			}
		}

		// Highest priority last
		if(webm) {
			video_variants.push_back(*webm);
			webm_save_url = webm->url;
		}
		if(mp4) {
			video_variants.push_back(*mp4);
			mp4_save_url = mp4->url;
		}
		LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl: using %d video variants", video_variants.size());
	}
	else {
		win->SetTitle(wxstrstd(me->media_url));
		StartFetchImageData();
	}

	wxMenuBar *menuBar = new wxMenuBar;

	if(is_video) {
		setsavemenuenablestate = [](bool enable) { };
	}
	else {
		int menubarcount = menuBar->GetMenuCount();
		setsavemenuenablestate = [menubarcount, this, menuBar](bool enable) {
			menuBar->EnableTop(menubarcount, enable);
		};
		AddSaveMenu(menuBar, wxT("&Save Image"), [this](observer_ptr<media_entity> me) -> std::string {
			return me->media_url;
		}, [this](observer_ptr<media_entity> me, wxString filename) {
			wxFile file(filename, wxFile::write);
			file.Write(me->fulldata.data(), me->fulldata.size());
		});

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
	}

	win->SetMenuBar(menuBar);

	sz = new wxBoxSizer(wxVERTICAL);
	win->SetSizer(sz);
	UpdateImage();
	win->Thaw();
	win->Show();
}

media_display_win::~media_display_win() {
	observer_ptr<media_entity> me = pimpl->GetMediaEntity();
	if(me)
		me->win = nullptr;
}

media_display_win_pimpl::~media_display_win_pimpl() {
	win->PopEventHandler();
}

void media_display_win_pimpl::AddSaveMenu(wxMenuBar *menuBar, const wxString &title, std::function<std::string(observer_ptr<media_entity> me)> get_url,
			std::function<void(observer_ptr<media_entity>, wxString)> save_action) {
	wxMenu *menuF = new wxMenu;
	menuBar->Append(menuF, title);

	menuopenhandlers.push_back([this, menuF, get_url, title, save_action](wxMenuEvent &event) {
		if(event.GetMenu() != menuF) return;

		wxMenuItemList items = menuF->GetMenuItems();		//make a copy to avoid memory issues if Destroy modifies the list
		for(auto &it : items) {
			menuF->Destroy(it);
		}

		observer_ptr<media_entity> me = this->GetMediaEntity();
		if(!me)
			return;

		std::string url = get_url(me);
		if(url.empty())
			return;

		auto add_dyn_menu = [&](const wxString &item_name, const wxString &token) {
			menuF->Append(next_dynmenu_id, item_name);

			dynmenuhandlerlist[next_dynmenu_id] = [token, this, title, url, save_action](wxCommandEvent &e) {
				this->SaveToDir(token, title, wxstrstd(url), save_action);
			};
			Connect(next_dynmenu_id, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(media_display_win_pimpl::dynmenudispatchhandler));
			next_dynmenu_id++;
		};

		add_dyn_menu(wxT("&Save..."), wxT(""));

		wxStringTokenizer tkn(gc.gcfg.mediasave_directorylist.val, wxT("\r\n"), wxTOKEN_STRTOK);

		bool added_seperator = false;

		while(tkn.HasMoreTokens()) {
			wxString token = tkn.GetNextToken();

			if(!wxFileName::DirExists(token)) continue;

			if(!added_seperator) {
				menuF->AppendSeparator();
				added_seperator = true;
			}

			add_dyn_menu(wxT("Save to: ") + token, token);
		}
	});
}

void media_display_win_pimpl::StartFetchImageData() {
	observer_ptr<media_entity> me = GetMediaEntity();
	if(!me)
		return;

	if(me->flags & MEF::LOAD_FULL && !(me->flags & MEF::HAVE_FULL)) {
		//try to load from file
		std::string data;
		if(LoadFromFileAndCheckHash(me->cached_full_filename(), me->full_img_sha1, data)) {
			me->flags |= MEF::HAVE_FULL;
			me->fulldata = std::move(data);
		}
	}
	if(!(me->flags & MEF::FULL_NET_INPROGRESS) && !(me->flags & MEF::HAVE_FULL) && me->media_url.size()) {
		flagwrapper<MIDC> flags = MIDC::FULLIMG | MIDC::OPPORTUNIST_THUMB | MIDC::OPPORTUNIST_REDRAW_TWEETS;
		std::shared_ptr<taccount> acc = me->dm_media_acc.lock();
		mediaimgdlconn::NewConnWithOptAccOAuth(me->media_url, media_id, flags, acc.get());
	}
}

void media_display_win_pimpl::dynmenudispatchhandler(wxCommandEvent &event) {
	dynmenuhandlerlist[event.GetId()](event);
}

void media_display_win_pimpl::OnMenuOpen(wxMenuEvent &event) {
	for(auto &it : menuopenhandlers) {
		it(event);
	}
}

void media_display_win::UpdateImage() {
	pimpl->UpdateImage();
}

void media_display_win_pimpl::UpdateImage() {
	if(is_video) {
		TryLoadVideo();
		return;
	}

	wxString message;
	GetImage(message);
	if(img_ok) {
		setsavemenuenablestate(true);
		ClearAllUnlessShowingImage();

		#if defined(__WXGTK__)
		if(using_anim_ctrl) {
			wxSize imgsize(current_img.GetWidth(), current_img.GetHeight());
			wxSize origwinsize = win->ClientToWindowSize(imgsize);
			sz->Add(&anim_ctrl, 1, wxEXPAND | wxALIGN_CENTRE);
			anim_ctrl.Play();
			win->SetSize(origwinsize);
			win->SetMinSize(origwinsize);	//don't allow resizing the window, as animation controls don't scale
			win->SetMaxSize(origwinsize);
			return;
		}
		#endif

		DoSizerLayout();
		if(is_animated)
			DelayLoadNextAnimFrame();
	}
	else {
		ShowErrorMessage(message);
	}
}

void media_display_win_pimpl::ClearAllUnlessShowingImage() {
	if(!sb)
		ClearAll();
}

void media_display_win_pimpl::ClearAll() {
	if(st_sizer) {
		sz->Remove(st_sizer);
		st_sizer = nullptr;
	}
	if(st) {
		sz->Detach(st);
		st->Destroy();
		st = nullptr;
	}
	if(media_ctrl) {
		sz->Detach(media_ctrl.get());
		media_ctrl->Destroy();
		media_ctrl = nullptr;
	}
	if(sb) {
		sz->Detach(sb);
		sb->Destroy();
		sb = nullptr;
	}
	if(scrollwin) {
		sz->Detach(scrollwin);
		scrollwin->Destroy();
		scrollwin = nullptr;
	}
	sz->SetMinSize(1, 1);
}

void media_display_win_pimpl::ShowErrorMessage(const wxString &message) {
	setsavemenuenablestate(false);

	ClearAll();
	st = new wxStaticText(win, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE);
	st_sizer = new wxBoxSizer(wxVERTICAL);
	wxSizer *hsizer = new wxBoxSizer(wxHORIZONTAL);

	sz->Add(st_sizer, 1, wxALIGN_CENTRE | wxEXPAND);

	st_sizer->AddStretchSpacer();
	st_sizer->Add(hsizer, 0, wxALIGN_CENTRE | wxEXPAND);
	st_sizer->AddStretchSpacer();

	hsizer->AddStretchSpacer();
	hsizer->Add(st, 0, wxALIGN_CENTRE | wxEXPAND);
	hsizer->AddStretchSpacer();

	sz->SetMinSize(200, 200);
	sz->Fit(win);
	win->Layout();
}

void media_display_win_pimpl::CalcSizes(wxSize imgsize, wxSize &winsize, wxSize &targimgsize) {
	int scrwidth, scrheight;
	wxClientDisplayRect(0, 0, &scrwidth, &scrheight);
	scrwidth -= gc.mediawinscreensizewidthreduction;
	scrheight -= gc.mediawinscreensizeheightreduction;
	if(scrwidth < 1) scrwidth = 1;
	if(scrheight < 1) scrheight = 1;

	if(zoomflags & MDZF::ZOOMSET) {
		targimgsize.SetWidth(imgsize.GetWidth() * zoomvalue);
		targimgsize.SetHeight(imgsize.GetHeight() * zoomvalue);

		winsize = win->ClientToWindowSize(targimgsize);
		if(winsize.GetWidth() > scrwidth) winsize.SetWidth(scrwidth);
		if(winsize.GetHeight() > scrheight) winsize.SetHeight(scrheight);
	}
	else {
		winsize = win->ClientToWindowSize(imgsize);
		if(winsize.GetWidth() > scrwidth) {
			double scale = (((double) scrwidth) / ((double) winsize.GetWidth()));
			winsize.Scale(scale, scale);
		}
		if(winsize.GetHeight() > scrheight) {
			double scale = (((double) scrheight) / ((double) winsize.GetHeight()));
			winsize.Scale(scale, scale);
		}
		targimgsize = win->WindowToClientSize(winsize);
	}
}

void media_display_win_pimpl::DoSizerLayout() {
	wxSize imgsize;
	if(is_video) {
		observer_ptr<media_entity> me = GetMediaEntity();
		if(!me || !me->video)
			return;

		unsigned int w = me->video->size_w;
		unsigned int h = me->video->size_h;

		w = std::max<unsigned int>({ w, (h * me->video->aspect_w) / me->video->aspect_h, 1 });
		h = std::max<unsigned int>({ h, (w * me->video->aspect_h) / me->video->aspect_w, 1 });

		imgsize = wxSize(w, h);
	}
	else {
		if(!img_ok)
			return;
		imgsize = wxSize(current_img.GetWidth(), current_img.GetHeight());
	}

	wxSize winsize, targsize;
	CalcSizes(imgsize, winsize, targsize);

	wxWindow *item = nullptr;
	int flag;
	if(is_video) {
		flag = wxSHAPED;
		if(media_ctrl) {
			item = media_ctrl.get();
			sz->Detach(item);
		}
	}
	else {
		flag = wxEXPAND;
		if(!sb) {
			sb = new image_panel(win, targsize);
			sb->img = current_img;
		}
		else {
			sz->Detach(sb);
		}
		item = sb;
	}

	if(!item)
		return;

	if(zoomflags & MDZF::ZOOMSET) {
		if(!scrollwin) {
			scrollwin = new wxScrolledWindow(win);
			scrollwin->SetScrollRate(1, 1);
			sz->Add(scrollwin, 1, flag | wxALIGN_CENTRE | wxALIGN_CENTRE_VERTICAL);
			item->Reparent(scrollwin);
		}
		scrollwin->SetVirtualSize(targsize);
		item->SetPosition(wxPoint(0, 0));
		item->SetSize(targsize);
		wxSize winsizeinc(winsize);
		winsizeinc.IncBy(50);
		win->SetSize(winsizeinc);    //This is to encourage scrollbars to disappear
		win->Layout();
		win->SetSize(winsize);
	}
	else {
		if(scrollwin) {
			item->Reparent(win);
			sz->Detach(scrollwin);
			scrollwin->Destroy();
			scrollwin = nullptr;
		}
		item->SetSize(targsize);
		win->SetSize(winsize);
		item->SetMinSize(wxSize(1, 1));
		sz->Add(item, 1, flag | wxALIGN_CENTRE | wxALIGN_CENTRE_VERTICAL);
	}

	if(!is_video) {
		sb->UpdateBitmap();
	}

	win->Layout();
}

void media_display_win_pimpl::GetImage(wxString &message) {
	observer_ptr<media_entity> me = GetMediaEntity();
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
						LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl::GetImage found animation: %d frames", anim.GetFrameCount());
						is_animated = true;
						current_img = anim.GetFrame(0);
						current_frame_index = 0;
						animation_timer.SetOwner(this, MDID_TIMER_EVT);
					}
					#if defined(__WXGTK__)
					else {
						if(!using_anim_ctrl && !gdk_pixbuf_animation_is_static_image(anim.GetPixbuf())) {
							using_anim_ctrl = true;
							anim_ctrl.Create(win, wxID_ANY, anim);
						}
					}
					#endif
				}
			}
			else {
				LogMsgFormat(LOGT::OTHERERR, "media_display_win_pimpl::GetImage: Image is not OK, corrupted or partial download?");
			}
			return;
		}
		else if(me->flags & MEF::FULL_FAILED) {
			message = wxT("Failed to Load Image");
		}
		else {
			message = wxT("Loading Image");
		}
	}
	else {
		message = wxT("No Image");
	}
	img_ok = false;
	return;
}

void media_display_win_pimpl::DelayLoadNextAnimFrame() {
	int delay = anim.GetDelay(current_frame_index);
	if(delay >= 0) animation_timer.Start(delay, wxTIMER_ONE_SHOT);
}

void media_display_win_pimpl::OnAnimationTimer(wxTimerEvent& event) {
	if(!sb) return;
	current_frame_index++;
	if(current_frame_index >= anim.GetFrameCount()) current_frame_index = 0;
	current_img = anim.GetFrame(current_frame_index);
	sb->img = current_img;
	sb->UpdateBitmap();
	sb->Refresh();
	DelayLoadNextAnimFrame();
}

observer_ptr<media_entity> media_display_win_pimpl::GetMediaEntity() {
	return media_entity::GetExisting(media_id);
}

void media_display_win_pimpl::SaveToDir(const wxString &dir, const wxString &title, const wxString &url,
		std::function<void(observer_ptr<media_entity>, wxString)> save_action) {
	observer_ptr<media_entity> me = GetMediaEntity();
	if(me) {
		wxString hint;
		wxString ext;
		bool hasext;
		wxFileName::SplitPath(url, 0, 0, &hint, &ext, &hasext, wxPATH_UNIX);
		if(hasext) hint += wxT(".") + ext;
		wxString newhint;
		if(hint.EndsWith(wxT(":large"), &newhint)) hint = newhint;
		wxString filename = wxFileSelector(title, dir, hint, ext, wxT("*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, win);
		if(filename.Len()) {
			save_action(me, filename);
		}
	}
}

void media_display_win_pimpl::OnMenuZoomFit(wxCommandEvent &event) {
	zoomflags &= ~MDZF::ZOOMSET;
	DoSizerLayout();
}

void media_display_win_pimpl::OnMenuZoomOrig(wxCommandEvent &event) {
	zoomflags |= MDZF::ZOOMSET;
	zoomvalue = 1.0;
	DoSizerLayout();
}

void media_display_win_pimpl::OnMenuZoomSet(wxCommandEvent &event) {
	wxString str = ::wxGetTextFromUser(wxT("Enter zoom value in percent (%)"), wxT("Input Number"), wxT(""), win);
	if(!str.IsEmpty()) {
		std::string stdstr = stdstrwx(str);
		char *pos = nullptr;
		double value = strtod(stdstr.c_str(), &pos);
		if((pos && *pos != 0) || (!std::isnormal(value)) || value <= 0) {
			::wxMessageBox(wxString::Format(wxT("'%s' does not appear to be a positive finite number"), str.c_str()), wxT("Invalid Input"), wxOK | wxICON_EXCLAMATION, win);
			return;
		}
		zoomflags |= MDZF::ZOOMSET;
		zoomvalue = value / 100;
		DoSizerLayout();
	}
}

void media_display_win_pimpl::TryLoadVideo() {
	if(video_variants.empty())
		return;

	video_entity::video_variant &vv = video_variants.back();

	observer_ptr<media_entity> me = GetMediaEntity();
	if(!me) {
		NotifyVideoLoadFailure(vv.url);
		return;
	}

	auto vc = me->video_file_cache.find(vv.url);
	if(vc == me->video_file_cache.end()) {
		std::shared_ptr<taccount> acc = me->dm_media_acc.lock();
		mediaimgdlconn::NewConnWithOptAccOAuth(vv.url, media_id, MIDC::VIDEO, acc.get());
	}
	else if(vc->second.IsValid()) {
		// Already loaded
		NotifyVideoLoadSuccess(vv.url);
		return;
	}
	// Otherwise loading already in progress

	ShowErrorMessage(wxT("Loading video: ") + wxstrstd(vv.content_type));
}

void media_display_win::NotifyVideoLoadSuccess(const std::string &url) {
	pimpl->NotifyVideoLoadSuccess(url);
}

void media_display_win_pimpl::NotifyVideoLoadSuccess(const std::string &url) {
	LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl::NotifyVideoLoadSuccess");

	observer_ptr<media_entity> me = GetMediaEntity();
	if(!me) {
		NotifyVideoLoadFailure(url);
		return;
	}

	auto vc = me->video_file_cache.find(url);
	if(vc == me->video_file_cache.end() || !vc->second.IsValid()) {
		NotifyVideoLoadFailure(url);
		return;
	}

	ClearAll();
	media_ctrl = new media_ctrl_panel(win);
	sz->Add(media_ctrl.get(), 1, wxSHAPED | wxALIGN_CENTRE);
	bool video_ok = media_ctrl->Load(wxstrstd(vc->second.GetFilename()));
	if(!video_ok)
		NotifyVideoLoadFailure(url);
	else {
		LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl::NotifyVideoLoadSuccess: appeared to load successfully");
		DoSizerLayout();
	}
}

void media_display_win::NotifyVideoLoadFailure(const std::string &url) {
	pimpl->NotifyVideoLoadFailure(url);
}

void media_display_win_pimpl::NotifyVideoLoadFailure(const std::string &url) {
	LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl::NotifyVideoLoadFailure");

	if(video_variants.empty())
		return;

	video_variants.pop_back();
	if(video_variants.empty()) {
		ShowErrorMessage(wxT("Loading video failed"));
	}
	else {
		TryLoadVideo();
	}
}
