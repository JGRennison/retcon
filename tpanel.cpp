#include "retcon.h"
#include "utf8.h"
#include <wx/filename.h>
#include <wx/filedlg.h>

void tpanel::PushTweet(std::shared_ptr<tweet> t) {
	wxLogWarning(wxT("Pushing tweet id %" wxLongLongFmtSpec "d to panel %s"), t->id, wxstrstd(name).c_str());
	if(tweetlist.count(t->id)) {
		//already have this tweet
		return;
	}
	else {
		tweetlist[t->id]=t;
		for(auto i=twin.begin(); i!=twin.end(); i++) {
			(*i)->PushTweet(t);
		}
	}
}

tpanel::tpanel(std::string name_) {
	twin.clear();
	name=name_;
}

BEGIN_EVENT_TABLE(tpanelnotebook, wxAuiNotebook)
	EVT_AUINOTEBOOK_ALLOW_DND(wxID_ANY, tpanelnotebook::dragdrophandler)
	EVT_AUINOTEBOOK_DRAG_DONE(wxID_ANY, tpanelnotebook::dragdonehandler)
	EVT_AUINOTEBOOK_TAB_RIGHT_DOWN(wxID_ANY, tpanelnotebook::tabrightclickhandler)
	EVT_AUINOTEBOOK_PAGE_CLOSED(wxID_ANY, tpanelnotebook::tabclosedhandler)
END_EVENT_TABLE()

tpanelnotebook::tpanelnotebook(mainframe *owner_, wxWindow *parent) :
wxAuiNotebook(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxAUI_NB_TOP | wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_SCROLL_BUTTONS | wxAUI_NB_TAB_EXTERNAL_MOVE | wxAUI_NB_CLOSE_ON_ALL_TABS | wxAUI_NB_WINDOWLIST_BUTTON),
owner(owner_)
{

}

void tpanelnotebook::dragdrophandler(wxAuiNotebookEvent& event) {
	wxAuiNotebook* note= (wxAuiNotebook *) event.GetEventObject();
	if(note) {
		tpanelparentwin *tppw = (tpanelparentwin *) note->GetPage(event.GetSelection());
		if(tppw) tppw->owner=owner;
	}
	event.Allow();
}
void tpanelnotebook::dragdonehandler(wxAuiNotebookEvent& event) {
	tabnumcheck();
}
void tpanelnotebook::tabclosedhandler(wxAuiNotebookEvent& event) {
	tabnumcheck();
}
void tpanelnotebook::tabnumcheck() {
	if(GetPageCount()==0 && !(mainframelist.empty() || (++mainframelist.begin())==mainframelist.end())) {
		owner->Close();
	}
}

void tpanelnotebook::tabrightclickhandler(wxAuiNotebookEvent& event) {
	tpanelparentwin *tppw = (tpanelparentwin *) GetPage(event.GetSelection());
	if(tppw) {
		wxMenu menu;
		menu.Append(TPPWID_DETACH, wxT("Detach"));
		menu.Append(TPPWID_DUP, wxT("Duplicate"));
		menu.Append(TPPWID_DETACHDUP, wxT("Detached Duplicate"));
		menu.Append(TPPWID_CLOSE, wxT("Close"));
		tppw->PopupMenu(&menu);
	}
}

DECLARE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT, -1)

DEFINE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT)

BEGIN_EVENT_TABLE(tpanelparentwin, wxScrolledWindow)
	EVT_SIZE(tpanelparentwin::resizehandler)
	EVT_COMMAND(wxID_ANY, wxextRESIZE_UPDATE_EVENT, tpanelparentwin::resizemsghandler)
	EVT_MENU(TPPWID_DETACH, tpanelparentwin::tabdetachhandler)
	EVT_MENU(TPPWID_DUP, tpanelparentwin::tabduphandler)
	EVT_MENU(TPPWID_DETACHDUP, tpanelparentwin::tabdetachedduphandler)
	EVT_MENU(TPPWID_CLOSE, tpanelparentwin::tabclosehandler)
END_EVENT_TABLE()

tpanelparentwin *tpanel::MkTPanelWin(mainframe *parent) {
	return new tpanelparentwin(shared_from_this(), parent);
}

tpanelparentwin::tpanelparentwin(std::shared_ptr<tpanel> tp_, mainframe *parent)
: wxScrolledWindow(parent), tp(tp_), resize_update_pending(false), owner(parent) {
	wxLogWarning(wxT("Creating tweet panel window %s"), wxstrstd(tp->name).c_str());

	tp->twin.push_front(this);
	tpanelparentwinlist.push_front(this);

	//tpw = new tpanelwin(this);
	//wxBoxSizer *vbox = new wxBoxSizer(wxHORIZONTAL);
	//vbox->Add(tpw, 1, wxALIGN_TOP | wxEXPAND, 0);
	//SetSizer(vbox);

	//wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer = new wxBoxSizer(wxVERTICAL);
	//hsizer->Add(sizer, 1, wxEXPAND);
	SetSizer(sizer);
        FitInside();
        SetScrollRate(5, 5);


#ifdef USEAUIM
	parent->auim->AddPane(this, wxAuiPaneInfo().Resizable().Top().Caption(wxstrstd(tp->name)).Movable().GripperTop().Dockable(false).TopDockable().MinSize(50,50));
	parent->auim->Update();
#else
	parent->auib->AddPage(this, wxstrstd(tp->name));
#endif
	FillTweet();
}

tpanelparentwin::~tpanelparentwin() {
	tp->twin.remove(this);
	tpanelparentwinlist.remove(this);
}

void tpanelparentwin::FillTweet() {
	size_t index=0;
	Freeze();
	for(auto it=tp->tweetlist.rbegin(); it!=tp->tweetlist.rend(); it++, index++) {
		currentdisp.push_back(std::make_pair(it->second->id, PushTweet(it->second, index)));
	}
	Thaw();
}

void tpanelparentwin::PushTweet(std::shared_ptr<tweet> t) {
	uint64_t id=t->id;
	size_t index=0;
	auto it=currentdisp.begin();
	for(; it!=currentdisp.end(); it++, index++) {
		if(it->first<id) break;	//insert before this iterator
	}
	tweetdispscr *td = PushTweet(t, index);
	currentdisp.insert(it, std::make_pair(id, td));
}

tweetdispscr *tpanelparentwin::PushTweet(std::shared_ptr<tweet> t, size_t index) {
	Freeze();
	//if(tpw) tpw->PushTweet(t);
	wxBoxSizer *hbox = new wxBoxSizer(wxHORIZONTAL);
	tweetdispscr *td=new tweetdispscr(t, this, hbox);
	td->bm = new wxStaticBitmap(this, wxID_ANY, t->user->cached_profile_img, wxPoint(-1000, -1000));
	//wxBitmapButton *bm=new wxBitmapButton(this, wxID_ANY, *t->t->user->cached_profile_img);

	hbox->Add(td->bm, 0, wxALL, 2);
	hbox->Add(td, 1, wxALL | wxEXPAND, 2);

	sizer->Insert(index, hbox, 0, wxALL | wxEXPAND, 2);
	//sizer->Add(td, 0, wxALL | wxEXPAND, 2);
	//sizer->SetItemMinSize(td, 50, 50);
	FitInside();
	td->LayoutContent();
	Thaw();
	//td->DoResize();
	return td;
}

void tpanelparentwin::resizehandler(wxSizeEvent &event) {
	wxLogWarning(wxT("tpanelparentwin::resizehandler"));
	//FitInside();
	//Refresh();
	//Update();
}

void tpanelparentwin::resizemsghandler(wxCommandEvent &event) {
	//wxLogWarning(wxT("tpanelparentwin::resizemsghandler"));
	FitInside();
	Refresh();
	Update();
	resize_update_pending=false;
}

void tpanelparentwin::tabdetachhandler(wxCommandEvent &event) {
	mainframe *top = new mainframe( wxT("Retcon"), wxDefaultPosition, wxDefaultSize );
	int index=owner->auib->GetPageIndex(this);
	wxString text=owner->auib->GetPageText(index);
	owner->auib->RemovePage(index);
	owner=top;
	top->auib->AddPage(this, text, true);
	top->Show(true);
}
void tpanelparentwin::tabduphandler(wxCommandEvent &event) {
	tp->MkTPanelWin(owner);
}
void tpanelparentwin::tabdetachedduphandler(wxCommandEvent &event) {
	mainframe *top = new mainframe( wxT("Retcon"), wxDefaultPosition, wxDefaultSize );
	tp->MkTPanelWin(top);
	top->Show(true);
}
void tpanelparentwin::tabclosehandler(wxCommandEvent &event) {
	owner->auib->RemovePage(owner->auib->GetPageIndex(this));
	owner->auib->tabnumcheck();
	Close();
}


//tpanelwin::tpanelwin(tpanelparentwin *tppw_)
//: wxRichTextCtrl(tppw_, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxRE_READONLY) {
//	tp=tppw_->tp;
//	GetCaret()->Hide();
//}
//
//void tpanelwin::PushTweet(std::shared_ptr<tweetdisp> t) {
//	std::shared_ptr<tweet> tw=t->t;
//	std::shared_ptr<userdatacontainer> udc=tw->user;
//	WriteImage(*udc->cached_profile_img);
//	WriteText(wxstrstd(udc->user->screen_name));
//	LineBreak();
//	WriteText(wxstrstd(tw->text));
//	LineBreak();
//	LineBreak();
//}
//
//tpanelwin::~tpanelwin() {
//	if(tppw) {
//		tppw->tpw=0;
//		tppw=0;
//	}
//}

BEGIN_EVENT_TABLE(tweetdispscr, wxRichTextCtrl)
	EVT_TEXT_URL(wxID_ANY, tweetdispscr::urleventhandler)
	EVT_MOUSEWHEEL(tweetdispscr::mousewheelhandler)
END_EVENT_TABLE()

tweetdispscr::tweetdispscr(std::shared_ptr<tweet> td_, tpanelparentwin *tppw_, wxBoxSizer *hbox_)
: wxRichTextCtrl(tppw_, wxID_ANY, wxEmptyString, wxPoint(-1000, -1000), wxDefaultSize, wxRE_READONLY),
td(td_), tppw(tppw_), hbox(hbox_) {
	GetCaret()->Hide();
	DisplayTweet();
}

tweetdispscr::~tweetdispscr() {
	//tppw->currentdisp.remove_if([this](const std::pair<uint64_t, tweetdispscr *> &p){ return p.second==this; });
}

//use -1 for end to run until end of string
static void DoWriteSubstr(tweetdispscr &td, const std::string &str, int start, int end, int &track_byte, int &track_index, bool trim) {
	while(str[track_byte]) {
		if(track_index==start) break;
		register int charsize=utf8firsttonumbytes(str[track_byte]);
		track_byte+=charsize;
		track_index++;
	}
	int start_offset=track_byte;

	while(str[track_byte]) {
		if(track_index==end) break;
		if(str[track_byte]=='&') {
			char rep=0;
			if(str[track_byte+1]=='l' && str[track_byte+2]=='t' && str[track_byte+3]==';') {
				rep='<';
			}
			else if(str[track_byte+1]=='g' && str[track_byte+2]=='t' && str[track_byte+3]==';') {
				rep='>';
			}
			if(rep) {
				td.WriteText(wxString::FromUTF8(&str[start_offset], track_byte-start_offset));
				track_index+=4;
				track_byte+=4;
				td.WriteText(wxString((wxChar) rep));
				start_offset=track_byte;
				continue;
			}
		}
		register int charsize=utf8firsttonumbytes(str[track_byte]);
		track_byte+=charsize;
		track_index++;
	}
	int end_offset=track_byte;
	wxString wstr=wxString::FromUTF8(&str[start_offset], end_offset-start_offset);
	if(trim) wstr.Trim();
	if(wstr.Len()) td.WriteText(wstr);
}

void tweetdispscr::DisplayTweet() {
	std::forward_list<media_entity*> me_list;
	auto last_me=me_list.before_begin();

	tweet &tw=*td;
	userdatacontainer &udc=*tw.user;
	Clear();
	BeginBold();
	WriteText(wxT("@") + wxstrstd(udc.GetUser().screen_name));
	EndBold();
	wxString timestr=rc_wx_strftime(gc.gcfg.datetimeformat.val, localtime(&tw.createtime), tw.createtime);
	WriteText(wxT(" - ") + timestr);
	WriteText(wxT(" - ") + wxstrstd(tw.flags.GetString()));
	Newline();

	unsigned int nextoffset=0;
	unsigned int entnum=0;
	int track_byte=0;
	int track_index=0;
	for(auto it=tw.entlist.begin(); it!=tw.entlist.end(); it++, entnum++) {
		entity &et=*it;
		DoWriteSubstr(*this, tw.text, nextoffset, et.start, track_byte, track_index, false);
		BeginUnderline();
		BeginURL(wxString::Format(wxT("%d"), entnum));
		WriteText(wxstrstd(et.text));
		nextoffset=et.end;
		EndURL();
		EndUnderline();
		if((et.type==ENT_MEDIA || et.type==ENT_URL_IMG) && et.media_id) {
			media_entity &me=ad.media_list[et.media_id];
			last_me=me_list.insert_after(last_me, &me);
		}
	}
	DoWriteSubstr(*this, tw.text, nextoffset, -1, track_byte, track_index, true);
	if(!me_list.empty()) {
		Newline();
		BeginAlignment(wxTEXT_ALIGNMENT_CENTRE);
		for(auto it=me_list.begin(); it!=me_list.end(); ++it) {
			BeginURL(wxString::Format(wxT("M%" wxLongLongFmtSpec "d"), (*it)->media_id));
			if((*it)->flags&ME_HAVE_THUMB) {
				AddImage((*it)->thumbimg);
			}
			else {
				BeginUnderline();
				WriteText(wxT("[Image]"));
				EndUnderline();
			}
			EndURL();
		}
		EndAlignment();
	}
}

void tweetdispscr::DoResize() {
	//int height;
	//int width;
	//GetVirtualSize(&width, &height);
	//hbox->SetItemMinSize(this, 10, height+10);
	//GetScrollRange(wxVERTICAL)*
}

void tweetdispscr::SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
		       int noUnitsX, int noUnitsY,
		       int xPos, int yPos,
		       bool noRefresh ) {
	//wxLogWarning(wxT("tweetdispscr::SetScrollbars, tweet id %" wxLongLongFmtSpec "d"), td->id);
	wxRichTextCtrl::SetScrollbars(0, 0, 0, 0, 0, 0, noRefresh);
	int newheight=(pixelsPerUnitY*noUnitsY)+4;
	hbox->SetItemMinSize(this, 10, newheight);
	//hbox->SetMinSize(10, newheight+4);
	//SetSize(wxDefaultCoord, wxDefaultCoord, wxDefaultCoord, newheight, wxSIZE_USE_EXISTING);
	tppw->FitInside();
	if(!tppw->resize_update_pending) {
		tppw->resize_update_pending=true;
		wxCommandEvent event(wxextRESIZE_UPDATE_EVENT, GetId());
		tppw->AddPendingEvent(event);
	}
}

void tweetdispscr::urleventhandler(wxTextUrlEvent &event) {
	tweet &tw=*td;
	long start=event.GetURLStart();
	wxRichTextAttr textattr;
	GetStyle(start, textattr);
	wxString url=textattr.GetURL();
	wxLogWarning(wxT("URL clicked, id: %s"), url.c_str());
	if(url[0]=='M') {
		uint64_t media_id=0;
		//url.Mid(1).ToULongLong(&media_id);	//not implemented on some systems

		//poor man's strtoull
		for(unsigned int i=1; i<url.Len(); i++) {
			if(url[i]>='0' && url[i]<='9') {
				media_id*=10;
				media_id+=url[i]-'0';
			}
			else break;
		}

		wxLogWarning(wxT("Media image clicked, str: %s, id: %" wxLongLongFmtSpec "d"), url.Mid(1).c_str(), media_id);
		if(ad.media_list[media_id].win) {
			ad.media_list[media_id].win->Raise();
		}
		else new media_display_win(this, media_id);
	}
	else {
		unsigned long counter;
		url.ToULong(&counter);
		auto it=tw.entlist.begin();
		while(it!=tw.entlist.end()) {
			if(!counter) {
				//got entity
				entity &et= *it;
				switch(et.type) {
					case ENT_HASHTAG:
						break;
					case ENT_URL:
					case ENT_URL_IMG:
					case ENT_MEDIA:
						::wxLaunchDefaultBrowser(wxstrstd(et.fullurl));
						break;
					case ENT_MENTION:
						break;
				}
				return;
			}
			else {
				counter--;
				it++;
			}
		}
	}
}

void tweetdispscr::mousewheelhandler(wxMouseEvent &event) {
	//wxLogWarning(wxT("MouseWheel"));
	event.SetEventObject(GetParent());
	GetParent()->GetEventHandler()->ProcessEvent(event);
}

BEGIN_EVENT_TABLE(image_panel, wxPanel)
	EVT_PAINT(image_panel::OnPaint)
	EVT_SIZE(image_panel::OnResize)
END_EVENT_TABLE()

image_panel::image_panel(media_display_win *parent, wxSize size) : wxPanel(parent, wxID_ANY, wxDefaultPosition, size) {

}

void image_panel::OnPaint(wxPaintEvent &event) {
	wxPaintDC dc(this);
	dc.DrawBitmap(bm, 0, 0, 0);
}

void image_panel::OnResize(wxSizeEvent &event) {
	UpdateBitmap();
}

void image_panel::UpdateBitmap() {
	//if(imgok) {
		bm=wxBitmap(img.Scale(GetSize().GetWidth(), GetSize().GetHeight(), wxIMAGE_QUALITY_HIGH));
	//}
	/*else {
		bm.Create(GetSize().GetWidth(),GetSize().GetHeight());
		wxMemoryDC mdc(bm);
		mdc.SetBackground(*wxBLACK_BRUSH);
		mdc.SetTextForeground(*wxWHITE);
		mdc.SetTextBackground(*wxBLACK);
		mdc.Clear();
		wxSize size=mdc.GetTextExtent(message);
		mdc.DrawText(message, (GetSize().GetWidth()/2)-(size.GetWidth()/2), (GetSize().GetHeight()/2)-(size.GetHeight()/2));
	}*/
	Refresh();
}

BEGIN_EVENT_TABLE(media_display_win, wxFrame)
	EVT_MENU(MDID_SAVE,  media_display_win::OnSave)
END_EVENT_TABLE()

media_display_win::media_display_win(wxWindow *parent, uint64_t media_id_)
	: wxFrame(parent, wxID_ANY, wxstrstd(ad.media_list[media_id_].media_url)), media_id(media_id_), sb(0), st(0), sz(0) {
	Freeze();
	ad.media_list[media_id_].win=this;

	wxMenu *menuF = new wxMenu;
	savemenuitem=menuF->Append( MDID_SAVE, wxT("&Save Image"));

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuF, wxT("&File"));

	SetMenuBar( menuBar );

	sz=new wxBoxSizer(wxVERTICAL);
	SetSizer(sz);
	Update();
	if(!sb) {
		new mediaimgdlconn(ad.media_list[media_id_].media_url, media_id_, MIDC_FULLIMG | MIDC_OPPORTUNIST_THUMB | MIDC_OPPORTUNIST_REDRAW_TWEETS);
	}
	Thaw();
	Show();
}

media_display_win::~media_display_win() {
	media_entity *me=GetMediaEntity();
	if(me) me->win=0;
}

void media_display_win::Update() {
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
		wxSize size(img.GetWidth(), img.GetHeight());
		if(!sb) {
			sb=new image_panel(this, size);
			sb->img=img;
			sz->Add(sb, 1, wxSHAPED | wxALIGN_CENTRE);
		}
		else sb->SetSize(size);
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
	}
	sz->Fit(this);
}

bool media_display_win::GetImage(wxImage &img, wxString &message) {
	media_entity *me=GetMediaEntity();
	if(me) {
		if(me->flags&ME_HAVE_FULL) {
			wxMemoryInputStream memstream(me->fulldata.data(), me->fulldata.size());
			img.LoadFile(memstream, wxBITMAP_TYPE_ANY);
			me->fullsize.Set(img.GetWidth(),img.GetHeight());
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

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid) {
	//wxLogWarning(wxT("MouseWheel"));
	wxWindow *wind=wxFindWindowAtPoint(wxGetMousePosition() /*event.GetPosition()*/);
	while(wind) {
		if(wind!=avoid && std::count(tpanelparentwinlist.begin(), tpanelparentwinlist.end(), wind)) {
			tpanelparentwin *tppw=(tpanelparentwin*) wind;
			//wxLogWarning(wxT("Dispatching to %s"), wxstrstd(tppw->tp->name).c_str());
			event.SetEventObject(tppw);
			tppw->GetEventHandler()->ProcessEvent(event);
			return true;
		}
		wind=wind->GetParent();
	}
	return false;
}

wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp) {
	#ifdef __WINDOWS__	//%z is broken in MSVCRT, use a replacement
				//also add %F, %R, %T, %s
				//this is adapted from npipe var.cpp
	wxString newfmt;
	newfmt.Alloc(format.length());
	wxString &real_format=newfmt;
	const wxChar *ch=format.c_str();
	const wxChar *cur=ch;
	while(*ch) {
		if(ch[0]=='%') {
			wxString insert;
			if(ch[1]=='z') {
				int hh;
				int mm;
				if(true /*localtime*/) {
					TIME_ZONE_INFORMATION info;
					DWORD res = GetTimeZoneInformation(&info);
					int bias = - info.Bias;
					if(res==TIME_ZONE_ID_DAYLIGHT) bias-=info.DaylightBias;
					hh = bias / 60;
					if(bias<0) bias=-bias;
					mm = bias % 60;
				}
				else {
					hh=mm=0;
				}
				insert.Printf(wxT("%+03d%02d"), hh, mm);
			}
			else if(ch[1]=='F') {
				insert=wxT("%Y-%m-%d");
			}
			else if(ch[1]=='R') {
				insert=wxT("%H:%M");
			}
			else if(ch[1]=='T') {
				insert=wxT("%H:%M:%S");
			}
			else if(ch[1]=='s') {
				insert.Printf(wxT("%" wxLongLongFmtSpec "d"), (long long int) timestamp);
			}
			else if(ch[1]) {
				ch++;
			}
			if(insert.length()) {
				real_format.Append(wxString(cur, ch-cur));
				real_format.Append(insert);
				cur=ch+2;
			}
		}
		ch++;
	}
	real_format.Append(cur);
	#else
	const wxString &real_format=format;
	#endif

	char timestr[256];
	strftime(timestr, sizeof(timestr), real_format.ToUTF8(), tm);
	return wxstrstd(timestr);
}
