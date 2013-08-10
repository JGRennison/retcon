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

#include "retcon.h"
#include <wx/filename.h>
#include <wx/filedlg.h>
#include <wx/dcclient.h>
#include <wx/dcscreen.h>

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
	double wratio = ((double) GetSize().GetWidth()) / ((double) img.GetWidth());
	double hratio = ((double) GetSize().GetHeight()) / ((double) img.GetHeight());
	double targratio = std::min(wratio, hratio);
	int targheight = targratio * img.GetHeight();
	int targwidth = targratio * img.GetWidth();
	bm=wxBitmap(img.Scale(targwidth, targheight, wxIMAGE_QUALITY_HIGH));
	Refresh();
}

BEGIN_EVENT_TABLE(media_display_win, wxFrame)
	EVT_MENU(MDID_SAVE,  media_display_win::OnSave)
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
	if(!(me->flags&ME_HAVE_FULL) && me->media_url.size()) {
		new mediaimgdlconn(me->media_url, media_id_, MIDC_FULLIMG | MIDC_OPPORTUNIST_THUMB | MIDC_OPPORTUNIST_REDRAW_TWEETS);
	}

	wxMenu *menuF = new wxMenu;
	savemenuitem=menuF->Append( MDID_SAVE, wxT("&Save Image"));

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuF, wxT("&File"));

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

void media_display_win::UpdateImage() {
	wxImage img;
	wxString message;
	bool imgok=GetImage(img, message);
	if(imgok) {
		savemenuitem->Enable(true);
		if(st) {
			sz->Detach(st);
			st->Destroy();
			st=0;
		}
		wxSize imgsize(img.GetWidth(), img.GetHeight());
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

		if(!sb) {
			sb=new image_panel(this, targsize);
			sb->img=img;
			sb->SetMinSize(wxSize(1, 1));
			sz->Add(sb, 1, wxEXPAND | wxALIGN_CENTRE);
		}
		sb->SetSize(targsize);
		SetSize(winsize);
		sb->UpdateBitmap();
	}
	else {
		savemenuitem->Enable(false);
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

bool media_display_win::GetImage(wxImage &img, wxString &message) {
	media_entity *me=GetMediaEntity();
	if(me) {
		if(me->flags&ME_HAVE_FULL) {
			wxMemoryInputStream memstream(me->fulldata.data(), me->fulldata.size());
			img.LoadFile(memstream, wxBITMAP_TYPE_ANY);
			return true;
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
	return false;
}

media_entity *media_display_win::GetMediaEntity() {
	auto it=ad.media_list.find(media_id);
	if(it!=ad.media_list.end()) {
		return &it->second;
	}
	else return 0;
}

void media_display_win::OnSave(wxCommandEvent &event) {
	media_entity *me=GetMediaEntity();
	if(me) {
		wxString hint;
		wxString ext;
		bool hasext;
		wxFileName::SplitPath(wxstrstd(me->media_url), 0, 0, &hint, &ext, &hasext, wxPATH_UNIX);
		if(hasext) hint+=wxT(".")+ext;
		wxString newhint;
		if(hint.EndsWith(wxT(":large"), &newhint)) hint=newhint;
		wxString filename=wxFileSelector(wxT("Save Image"), wxT(""), hint, ext, wxT("*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);
		if(filename.Len()) {
			wxFile file(filename, wxFile::write);
			file.Write(me->fulldata.data(), me->fulldata.size());
		}
	}
}
