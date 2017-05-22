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
#include "uiutil.h"
#include <wx/animate.h>
#include <wx/bitmap.h>
#include <wx/clipbrd.h>
#include <wx/dcclient.h>
#include <wx/dcscreen.h>
#include <wx/event.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/mstream.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textdlg.h>
#include <wx/timer.h>
#include <wx/window.h>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

#ifdef USE_LIBVLC
#include <vlc/vlc.h>
#else
#include <wx/mediactrl.h>
#include <wx/uri.h>
#endif

#ifdef __WXGTK__
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <wx/gtk/win_gtk.h>
#define GET_XID(window) GDK_WINDOW_XWINDOW(GTK_PIZZA(window->m_wxwindow)->bin_window)
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
		if (GetSize().GetWidth() == img.GetWidth() && GetSize().GetHeight() == img.GetHeight()) {
			bm = wxBitmap(img);
		}
		else {
			double wratio = ((double) GetSize().GetWidth()) / ((double) img.GetWidth());
			double hratio = ((double) GetSize().GetHeight()) / ((double) img.GetHeight());
			double targratio = std::min(wratio, hratio);
			int targheight = targratio * img.GetHeight();
			int targwidth = targratio * img.GetWidth();
			if (targheight < 1)
				targheight = 1;
			if (targwidth < 1)
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

#ifdef USE_LIBVLC

DECLARE_EVENT_TYPE(wxextVLC_MEDIAWIN_EVT, -1)
DEFINE_EVENT_TYPE(wxextVLC_MEDIAWIN_EVT)

enum {
	MCP_ID_LOAD = 1,
	MCP_ID_LOAD_STREAM,
	MCP_ID_RELOAD,
};

void VLC_Log_CB(void *data, int level, const libvlc_log_t *ctx, const char *fmt, va_list args) {
	LOGT category = LOGT::VLCWARN;
	const char *name = "???:";
	switch (level) {
		case LIBVLC_DEBUG:
			category = LOGT::VLCDEBUG;
			name = "debug:";
			break;
		case LIBVLC_NOTICE :
			name = "notice:";
			break;
		case LIBVLC_WARNING:
			name = "warning:";
			break;
		case LIBVLC_ERROR:
			name = "error:";
			break;
	}

	if (!(currentlogflags & category))
		return;

	char *str = nullptr;
	if (vasprintf(&str, fmt, args) < 0)
		return;
	TALogMsgFormat(category, "%-10s %s\n", name, str);
	free(str);
}

struct media_ctrl_panel : public wxPanel, public safe_observer_ptr_target {
	wxWindow *player_widget = nullptr;
	libvlc_media_player_t *media_player = nullptr;
	libvlc_instance_t *vlc_inst = nullptr;
	libvlc_event_manager_t *vlc_evt_man = nullptr;
	libvlc_media_t *media = nullptr;
	bool vlc_inited = false;

	media_ctrl_panel(wxWindow *parent, wxSize size = wxDefaultSize)
			: wxPanel(parent, wxID_ANY, wxDefaultPosition, size) {

		wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);
		this->SetSizer(vbox);

		player_widget = new wxWindow(this, wxID_ANY);
		player_widget->SetBackgroundColour(*wxBLACK);
		vbox->Add(player_widget, 1, wxEXPAND | wxALIGN_TOP);
	}

	~media_ctrl_panel() {
		if (media) {
			libvlc_media_release(media);
		}
		if (vlc_inited) {
			libvlc_media_player_release(media_player);
			libvlc_release(vlc_inst);
		}
	}

	void InitVLC() {
		if (vlc_inited)
			return;

		/* This is to fix the issue:
		 * Xlib not initialized for threads.
		 * This process is probably using LibVLC incorrectly.
		 * Pass "--no-xlib" to libvlc_new() to fix this.
		 */
		const char *vlc_args[] = {
			"--no-xlib",
		};

		vlc_inst = libvlc_new(1, vlc_args);
		if (!vlc_inst)
			vlc_inst = libvlc_new(0, nullptr);
		libvlc_log_set(vlc_inst, VLC_Log_CB, nullptr);
		media_player = libvlc_media_player_new(vlc_inst);
		vlc_evt_man = libvlc_media_player_event_manager(media_player);
		libvlc_event_attach(vlc_evt_man, libvlc_MediaPlayerEndReached, MediaPlayerEndReachedCallback, (void *) this);
		Show(true);
#ifdef __WXGTK__
		libvlc_media_player_set_xwindow(media_player, GET_XID(player_widget));
#else
		libvlc_media_player_set_hwnd(media_player, player_widget->GetHandle());
#endif

		vlc_inited = true;
	}

	bool Load(wxString path) {
		wxCommandEvent event(wxextVLC_MEDIAWIN_EVT, MCP_ID_LOAD);
		event.SetString(path);
		AddPendingEvent(event);
		return true;
	}

	bool LoadStreamUrl(wxString url) {
		wxCommandEvent event(wxextVLC_MEDIAWIN_EVT, MCP_ID_LOAD_STREAM);
		event.SetString(url);
		AddPendingEvent(event);
		return true;
	}

	private:
	static void MediaPlayerEndReachedCallback(const struct libvlc_event_t *event, void *data) {
		media_ctrl_panel *self = (media_ctrl_panel *) data;
		wxCommandEvent ev(wxextVLC_MEDIAWIN_EVT, MCP_ID_RELOAD);
		self->AddPendingEvent(ev);
	}

	void ReloadEvent(wxCommandEvent &event) {
		libvlc_media_player_set_media(media_player, media);
		libvlc_media_player_play(media_player);
	}

	void LoadEvent(wxCommandEvent &event) {
		InitVLC();

		if (media) {
			libvlc_media_release(media);
		}
		wxFileName filename = wxFileName::FileName(event.GetString());
		filename.MakeRelativeTo();
		media = libvlc_media_new_path(vlc_inst, filename.GetFullPath().mb_str());
		libvlc_media_player_set_media(media_player, media);
		libvlc_media_player_play(media_player);
	}

	void LoadStreamEvent(wxCommandEvent &event) {
		InitVLC();

		if (media) {
			libvlc_media_release(media);
		}
		media = libvlc_media_new_location(vlc_inst, event.GetString().mb_str());
		const char *ver = libvlc_get_version();
		if (!(ver && ver[0] && atoi(ver) > 2)) {
			/*
			 * This is because vlc 2.* seems to choke on Twitter's HLS streams
			 */
			LogMsg(LOGT::OTHERERR, "libvlc 2 has issues playing Twitter's HLS streams, try using libvlc 3");
		}
		libvlc_media_player_set_media(media_player, media);
		libvlc_media_player_play(media_player);
	}

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(media_ctrl_panel, wxPanel)
	EVT_COMMAND(MCP_ID_LOAD, wxextVLC_MEDIAWIN_EVT, media_ctrl_panel::LoadEvent)
	EVT_COMMAND(MCP_ID_LOAD_STREAM, wxextVLC_MEDIAWIN_EVT, media_ctrl_panel::LoadStreamEvent)
	EVT_COMMAND(MCP_ID_RELOAD, wxextVLC_MEDIAWIN_EVT, media_ctrl_panel::ReloadEvent)
END_EVENT_TABLE()

#else

struct media_ctrl_panel : public wxMediaCtrl, public safe_observer_ptr_target {
	media_ctrl_panel(wxWindow *parent, wxSize size = wxDefaultSize)
			: wxMediaCtrl(parent, wxID_ANY, wxT(""), wxDefaultPosition, size) { }

	void OnMediaLoaded(wxMediaEvent& evt) {
		Play();
	}

	void OnMediaFinished(wxMediaEvent& evt) {
		Play();
	}

	bool LoadStreamUrl(wxString url) {
		Load(wxURI(url.c_str()));
	}

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(media_ctrl_panel, wxMediaCtrl)
	EVT_MEDIA_LOADED(wxID_ANY, media_ctrl_panel::OnMediaLoaded)
	EVT_MEDIA_FINISHED(wxID_ANY, media_ctrl_panel::OnMediaFinished)
END_EVENT_TABLE()

#endif

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
	std::vector<std::function<void(wxMenuEvent &)> > menuopenhandlers;
	bool img_ok = false;
	unsigned int current_frame_index = 0;
	wxImage current_img;

	safe_observer_ptr<media_ctrl_panel> media_ctrl;
	bool using_media_ctrl = false;
	bool is_video = false;
	std::vector<video_entity::video_variant> video_variants;
	std::string mp4_save_url;
	std::string webm_save_url;

	dyn_menu_handler_set dyn_menu_handlers;
	wxMenu *zoom_menu = nullptr;
	wxScrolledWindow *scrollwin  = nullptr;
	flagwrapper<MDZF> zoomflags = 0;
	double zoomvalue = 1.0;

	media_display_win_pimpl(media_display_win *win_, media_id_type media_id_);
	~media_display_win_pimpl();
	void AddDynMenuItem(wxMenu *menu, const wxString &item_name, std::function<void(wxCommandEvent &event)> func);
	void AddSaveMenu(wxMenuBar *menuBar, const wxString &title, const std::string url, std::function<void(observer_ptr<media_entity>, wxString)> save_action);
	void UpdateImage();
	void GetImage(wxString &message);
	observer_ptr<media_entity> GetMediaEntity();
	void OnDynMenuHandler(wxCommandEvent &event);
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
	void LoadVideoStream(const std::string &url);

	DECLARE_EVENT_TABLE()
};

enum {
	MDID_ZOOM_FIT        = 3,
	MDID_ZOOM_ORIG       = 4,
	MDID_ZOOM_SET        = 5,
	MDID_DYN_START       = wxID_HIGHEST + 1,
	MDID_DYN_END         = MDID_DYN_START + 0x10000,
};

BEGIN_EVENT_TABLE(media_display_win_pimpl, wxEvtHandler)
	EVT_MENU_OPEN(media_display_win_pimpl::OnMenuOpen)
	EVT_MENU(MDID_ZOOM_FIT, media_display_win_pimpl::OnMenuZoomFit)
	EVT_MENU(MDID_ZOOM_ORIG, media_display_win_pimpl::OnMenuZoomOrig)
	EVT_MENU(MDID_ZOOM_SET, media_display_win_pimpl::OnMenuZoomSet)
	EVT_MENU_RANGE(MDID_DYN_START, MDID_DYN_END, media_display_win_pimpl::OnDynMenuHandler)
END_EVENT_TABLE()

media_display_win::media_display_win(wxWindow *parent, media_id_type media_id_)
	: wxFrame(parent, wxID_ANY, wxT("")) {

	pimpl.reset(new media_display_win_pimpl(this, media_id_));
}

media_display_win_pimpl::media_display_win_pimpl(media_display_win *win_, media_id_type media_id_)
	: win(win_), media_id(media_id_), dyn_menu_handlers(MDID_DYN_START) {

	win->PushEventHandler(this);

	win->Freeze();
	media_entity *me = ad.media_list[media_id_].get();
	me->win = win;

	wxMenu *copy_url_menu = new wxMenu;

	auto add_copy_url_menu_item = [&](wxMenu *parent, const std::string &url, const wxString &title) {
		AddDynMenuItem(parent, title, [url](wxCommandEvent event) {
			if (wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(wxstrstd(url)));
				wxTheClipboard->Close();
			}
		});
	};

	if (me->video && me->video->variants.size() > 0) {
		// This is a video
		// Don't request static image
		is_video = true;

		video_entity::video_variant *mp4 = nullptr;
		video_entity::video_variant *webm = nullptr;
		video_entity::video_variant *hls = nullptr;
		for (auto &vv : me->video->variants) {
			LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl: found video variant: %s (%u)", cstr(vv.content_type), vv.bitrate);
			std::string title;
			if (vv.bitrate) {
				title = string_format("%s (%dk)", vv.content_type.c_str(), vv.bitrate / 1000);
			} else {
				title = vv.content_type;
			}
			add_copy_url_menu_item(copy_url_menu, vv.url, wxstrstd(title));
			if (vv.content_type == "video/mp4") {
				if (!mp4 || vv.bitrate > mp4->bitrate) {
					mp4 = &vv;
				}
			}
			if (vv.content_type == "video/webm") {
				if (!webm || vv.bitrate > webm->bitrate) {
					webm = &vv;
				}
			}
			if (vv.content_type == "application/x-mpegURL") {
				if (!hls || vv.bitrate > hls->bitrate) {
					hls = &vv;
				}
			}
		}

		// Highest priority last
		if (hls) {
			video_variants.push_back(*hls);
		}
		if (webm) {
			video_variants.push_back(*webm);
			webm_save_url = webm->url;
		}
		if (mp4) {
			video_variants.push_back(*mp4);
			mp4_save_url = mp4->url;
		}
		LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl: using %d video variants", video_variants.size());
	} else {
		win->SetTitle(wxstrstd(me->media_url));
		me->StartFetchImageData();
	}

	wxMenuBar *menuBar = new wxMenuBar;

	if (is_video) {
		auto add_video_save_menu = [&](const std::string &url, const wxString &title) {
			AddSaveMenu(menuBar, title, url, media_entity::MakeVideoSaver(url));
		};
		if (!mp4_save_url.empty()) {
			add_video_save_menu(mp4_save_url, wxT("Save MP4"));
		}
		if (!webm_save_url.empty()) {
			add_video_save_menu(webm_save_url, wxT("Save WebM"));
		}
	} else {
		AddSaveMenu(menuBar, wxT("Save Image"), me->media_url, media_entity::MakeFullImageSaver());
		if (me->image_variants.empty()) {
			add_copy_url_menu_item(copy_url_menu, me->media_url, wxT("large"));
		} else {
			for (const auto &it : me->image_variants) {
				std::string title = string_format("%s (%d x %d)", cstr(it.name), it.size_w, it.size_h);
				add_copy_url_menu_item(copy_url_menu, it.url, wxstrstd(title));
			}
		}

		zoom_menu = new wxMenu;

		menuopenhandlers.push_back([this](wxMenuEvent &event) {
			if (event.GetMenu() != zoom_menu) {
				return;
			}

			DestroyMenuContents(zoom_menu);

			wxMenuItem *wmi1 = zoom_menu->Append(MDID_ZOOM_FIT, wxT("&Fit to Window"), wxT(""), wxITEM_CHECK);
			wmi1->Check(zoomflags == static_cast<MDZF>(0));
			wxMenuItem *wmi2 = zoom_menu->Append(MDID_ZOOM_ORIG, wxT("&Original Size"), wxT(""), wxITEM_CHECK);
			wmi2->Check(zoomflags & MDZF::ZOOMSET && zoomvalue == 1.0);
			zoom_menu->Append(MDID_ZOOM_SET, wxT("&Zoom to..."));
		});

		menuBar->Append(zoom_menu, wxT("&Zoom"));
	}

	menuBar->Append(copy_url_menu, wxT("&Copy URL"));

	win->SetMenuBar(menuBar);

	sz = new wxBoxSizer(wxVERTICAL);
	win->SetSizer(sz);
	UpdateImage();
	win->Thaw();
	win->Show();
}

media_display_win::~media_display_win() {
	observer_ptr<media_entity> me = pimpl->GetMediaEntity();
	if (me) {
		me->win = nullptr;
	}
}

media_display_win_pimpl::~media_display_win_pimpl() {
	win->PopEventHandler();
}

void media_display_win_pimpl::AddDynMenuItem(wxMenu *menu, const wxString &item_name, std::function<void(wxCommandEvent &event)> func) {
	menu->Append(dyn_menu_handlers.AddHandler(std::move(func)), item_name);
};

void media_display_win_pimpl::AddSaveMenu(wxMenuBar *menuBar, const wxString &title, const std::string url,
			std::function<void(observer_ptr<media_entity>, wxString)> save_action) {
	wxMenu *menuF = new wxMenu;
	menuBar->Append(menuF, title);

	menuopenhandlers.push_back([this, menuF, url, title, save_action](wxMenuEvent &event) {
		if (event.GetMenu() != menuF) return;

		DestroyMenuContents(menuF);

		observer_ptr<media_entity> me = this->GetMediaEntity();
		if (!me) {
			return;
		}
		me->FillSaveMenu(menuF, dyn_menu_handlers, url, title, save_action);
	});
}

void media_display_win_pimpl::OnDynMenuHandler(wxCommandEvent &event) {
	dyn_menu_handlers.Dispatch(event.GetId(), event);
}

void media_display_win_pimpl::OnMenuOpen(wxMenuEvent &event) {
	for (auto &it : menuopenhandlers) {
		it(event);
	}
}

void media_display_win::UpdateImage() {
	pimpl->UpdateImage();
}

void media_display_win_pimpl::UpdateImage() {
	if (is_video) {
		TryLoadVideo();
		return;
	}

	wxString message;
	GetImage(message);
	if (img_ok) {
		ClearAllUnlessShowingImage();

		DoSizerLayout();
	} else {
		ShowErrorMessage(message);
	}
}

void media_display_win_pimpl::ClearAllUnlessShowingImage() {
	if (!sb) {
		ClearAll();
	}
}

void media_display_win_pimpl::ClearAll() {
	if (st_sizer) {
		sz->Remove(st_sizer);
		st_sizer = nullptr;
	}
	if (st) {
		sz->Detach(st);
		st->Destroy();
		st = nullptr;
	}
	if (media_ctrl) {
		sz->Detach(media_ctrl.get());
		media_ctrl->Destroy();
		media_ctrl = nullptr;
	}
	if (sb) {
		sz->Detach(sb);
		sb->Destroy();
		sb = nullptr;
	}
	if (scrollwin) {
		sz->Detach(scrollwin);
		scrollwin->Destroy();
		scrollwin = nullptr;
	}
	sz->SetMinSize(1, 1);
}

void media_display_win_pimpl::ShowErrorMessage(const wxString &message) {
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
	if (scrwidth < 1) scrwidth = 1;
	if (scrheight < 1) scrheight = 1;

	if (zoomflags & MDZF::ZOOMSET) {
		targimgsize.SetWidth(imgsize.GetWidth() * zoomvalue);
		targimgsize.SetHeight(imgsize.GetHeight() * zoomvalue);

		winsize = win->ClientToWindowSize(targimgsize);
		if (winsize.GetWidth() > scrwidth) {
			winsize.SetWidth(scrwidth);
		}
		if (winsize.GetHeight() > scrheight) {
			winsize.SetHeight(scrheight);
		}
	} else {
		winsize = win->ClientToWindowSize(imgsize);
		if (winsize.GetWidth() > scrwidth) {
			double scale = (((double) scrwidth) / ((double) winsize.GetWidth()));
			winsize.Scale(scale, scale);
		}
		if (winsize.GetHeight() > scrheight) {
			double scale = (((double) scrheight) / ((double) winsize.GetHeight()));
			winsize.Scale(scale, scale);
		}
		targimgsize = win->WindowToClientSize(winsize);
	}
}

void media_display_win_pimpl::DoSizerLayout() {
	wxSize imgsize;
	if (is_video) {
		observer_ptr<media_entity> me = GetMediaEntity();
		if (!me || !me->video) {
			return;
		}

		unsigned int w = me->video->size_w;
		unsigned int h = me->video->size_h;

		w = std::max<unsigned int>({ w, (h * me->video->aspect_w) / me->video->aspect_h, 1 });
		h = std::max<unsigned int>({ h, (w * me->video->aspect_h) / me->video->aspect_w, 1 });

		imgsize = wxSize(w, h);
	} else {
		if (!img_ok) {
			return;
		}
		imgsize = wxSize(current_img.GetWidth(), current_img.GetHeight());
	}

	wxSize winsize, targsize;
	CalcSizes(imgsize, winsize, targsize);

	wxWindow *item = nullptr;
	int flag;
	if (is_video) {
		flag = wxSHAPED;
		if (media_ctrl) {
			item = media_ctrl.get();
			sz->Detach(item);
		}
	} else {
		flag = wxEXPAND;
		if (!sb) {
			sb = new image_panel(win, targsize);
			sb->img = current_img;
		} else {
			sz->Detach(sb);
		}
		item = sb;
	}

	if (!item) {
		return;
	}

	if (zoomflags & MDZF::ZOOMSET) {
		if (!scrollwin) {
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
	} else {
		if (scrollwin) {
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

	if (!is_video) {
		sb->UpdateBitmap();
	}

	win->Layout();
}

void media_display_win_pimpl::GetImage(wxString &message) {
	observer_ptr<media_entity> me = GetMediaEntity();
	if (me) {
		if (me->flags & MEF::HAVE_FULL) {
			wxMemoryInputStream memstream2(me->fulldata.data(), me->fulldata.size());
			current_img.LoadFile(memstream2, wxBITMAP_TYPE_ANY);
			img_ok = current_img.IsOk();
			if (!img_ok) {
				LogMsgFormat(LOGT::OTHERERR, "media_display_win_pimpl::GetImage: Image is not OK, corrupted or partial download?");
			}
			return;
		} else if (me->flags & MEF::FULL_FAILED) {
			message = wxT("Failed to Load Image");
		} else {
			message = wxT("Loading Image");
		}
	} else {
		message = wxT("No Image");
	}
	img_ok = false;
	return;
}

observer_ptr<media_entity> media_display_win_pimpl::GetMediaEntity() {
	return media_entity::GetExisting(media_id);
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
	if (!str.IsEmpty()) {
		std::string stdstr = stdstrwx(str);
		char *pos = nullptr;
		double value = strtod(stdstr.c_str(), &pos);
		if ((pos && *pos != 0) || (!std::isnormal(value)) || value <= 0) {
			::wxMessageBox(wxString::Format(wxT("'%s' does not appear to be a positive finite number"), str.c_str()), wxT("Invalid Input"), wxOK | wxICON_EXCLAMATION, win);
			return;
		}
		zoomflags |= MDZF::ZOOMSET;
		zoomvalue = value / 100;
		DoSizerLayout();
	}
}

void media_display_win_pimpl::TryLoadVideo() {
	if (video_variants.empty()) {
		return;
	}

	video_entity::video_variant &vv = video_variants.back();
	win->SetTitle(wxstrstd(vv.url));

	observer_ptr<media_entity> me = GetMediaEntity();
	if (!me) {
		NotifyVideoLoadFailure(vv.url);
		return;
	}

	if (vv.is_stream) {
		LoadVideoStream(vv.url);
		return;
	}

	auto vc = me->video_file_cache.find(vv.url);
	if (vc == me->video_file_cache.end()) {
		std::shared_ptr<taccount> acc = me->dm_media_acc.lock();
		mediaimgdlconn::NewConnWithOptAccOAuth(vv.url, media_id, MIDC::VIDEO, acc.get());
	} else if (vc->second.IsValid()) {
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

	if (video_variants.empty() || url != video_variants.back().url) {
		return;
	}

	observer_ptr<media_entity> me = GetMediaEntity();
	if (!me) {
		NotifyVideoLoadFailure(url);
		return;
	}

	auto vc = me->video_file_cache.find(url);
	if (vc == me->video_file_cache.end() || !vc->second.IsValid()) {
		NotifyVideoLoadFailure(url);
		return;
	}

	ClearAll();
	media_ctrl = new media_ctrl_panel(win);
	sz->Add(media_ctrl.get(), 1, wxSHAPED | wxALIGN_CENTRE);
	bool video_ok = media_ctrl->Load(wxstrstd(vc->second.GetFilename()));
	if (!video_ok) {
		NotifyVideoLoadFailure(url);
	} else {
		LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl::NotifyVideoLoadSuccess: appeared to load successfully");
		DoSizerLayout();
	}
}

void media_display_win::NotifyVideoLoadFailure(const std::string &url) {
	pimpl->NotifyVideoLoadFailure(url);
}

void media_display_win_pimpl::NotifyVideoLoadFailure(const std::string &url) {
	LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl::NotifyVideoLoadFailure");

	if (video_variants.empty()) {
		return;
	}

	video_variants.pop_back();
	if (video_variants.empty()) {
		win->SetTitle(wxT("Loading video failed"));
		ShowErrorMessage(wxT("Loading video failed"));
	} else {
		TryLoadVideo();
	}
}

void media_display_win_pimpl::LoadVideoStream(const std::string &url) {
	LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl::LoadVideoStream");

	ClearAll();
	media_ctrl = new media_ctrl_panel(win);
	sz->Add(media_ctrl.get(), 1, wxSHAPED | wxALIGN_CENTRE);
	bool video_ok = media_ctrl->LoadStreamUrl(wxstrstd(url));
	if (!video_ok) {
		NotifyVideoLoadFailure(url);
	} else {
		LogMsgFormat(LOGT::OTHERTRACE, "media_display_win_pimpl::LoadVideoStream: appeared to load successfully");
		DoSizerLayout();
	}
}
