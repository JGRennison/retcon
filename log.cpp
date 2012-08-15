#include "retcon.h"
#include <wx/tokenzr.h>
#include <wx/filedlg.h>
#ifdef __WINDOWS__
#include "windows.h"
#else
#include <sys/time.h>
#endif

log_window *globallogwindow;

logflagtype currentlogflags=0;
std::forward_list<log_object*> logfunclist;

static void dump_pending_acc(logflagtype logflags, const wxString &indent, const wxString &indentstep, taccount *acc);
static void dump_pending_tpaneldbloadmap(logflagtype logflags, const wxString &indent);

const wxChar *logflagsstrings[]={
	wxT("curlverb"),
	wxT("parsetrace"),
	wxT("parseerr"),
	wxT("socktrace"),
	wxT("sockerr"),
	wxT("tpanel"),
	wxT("twitact"),
	wxT("dbtrace"),
	wxT("dberr"),
	wxT("ztrace"),
	wxT("zerr"),
	wxT("othertrace"),
	wxT("othererr"),
	wxT("userreq"),
	wxT("pendingtrace"),
};

void Update_currentlogflags() {
	logflagtype old_currentlogflags=currentlogflags;
	logflagtype t_currentlogflags=0;
	for(auto it=logfunclist.begin(); it!=logfunclist.end(); ++it) t_currentlogflags|=(*it)->lo_flags;
	currentlogflags=t_currentlogflags;
	if((old_currentlogflags ^ currentlogflags) & LFT_CURLVERB) {
		for(auto it=sm.connlist.begin(); it!=sm.connlist.end(); ++it) {
			SetCurlHandleVerboseState(*it, currentlogflags&LFT_CURLVERB);
		}
	}
}

void LogMsgRaw(logflagtype logflags, const wxString &str) {
	for(auto it=logfunclist.begin(); it!=logfunclist.end(); ++it) {
		log_object *lo=*it;
		if(logflags&lo->lo_flags) lo->log_str(logflags, str);
	}
}

wxString LogMsgFlagString(logflagtype logflags) {
	wxString out;
	logflagtype bitint=logflags&lfd_stringmask;
	while(bitint) {
		int offset=__builtin_ctzl(bitint);
		bitint&=~((logflagtype) 1<<offset);
		if(out.Len()) out+=wxT(",");
		out+=logflagsstrings[offset];
	}
	return out;
}

void LogMsgProcess(logflagtype logflags, const wxString &str) {
	time_t now=time(0);
	unsigned int ms;
	#ifdef __WINDOWS__
	SYSTEMTIME time;
	GetSystemTime(&time);
	ms=time.wMilliseconds;
	#else
	timeval time;
	gettimeofday(&time, NULL);
	ms=(time.tv_usec / 1000);
	#endif
	wxString time_str=rc_wx_strftime(wxString::Format(wxT("%%F %%T.%03d %%z"), ms), localtime(&now), now, true);
	wxString flag_str=LogMsgFlagString(logflags)+wxT(":");
	wxString in=str;
	in.Trim();
	LogMsgRaw(logflags, wxString::Format(wxT("%s %-20s %s\n"), time_str.c_str(), flag_str.c_str(), in.c_str()).c_str());
}

log_object::log_object(logflagtype flagmask) : lo_flags(flagmask) {
	logfunclist.remove(this);
	logfunclist.push_front(this);
	Update_currentlogflags();
}

log_object::~log_object() {
	logfunclist.remove(this);
	Update_currentlogflags();
}

void log_window::log_str(logflagtype logflags, const wxString &str) {
	if(isshown) txtct->AppendText(str);
	else pending.push(str);
}

struct ChkBoxLFFlagValidator : public wxValidator {
	logflagtype flagbit;
	logflagtype *targ;

	ChkBoxLFFlagValidator(logflagtype flagbit_, logflagtype *targ_)
		: wxValidator(), flagbit(flagbit_), targ(targ_) { }
	virtual wxObject* Clone() const { return new ChkBoxLFFlagValidator(flagbit, targ); }
	virtual bool TransferFromWindow() {
		wxCheckBox *chk=(wxCheckBox*) GetWindow();
		wxCheckBoxState res=chk->Get3StateValue();
		if(res==wxCHK_CHECKED) (*targ)|=flagbit;
		else if(res==wxCHK_UNCHECKED) (*targ)&=~flagbit;
		chk->GetParent()->TransferDataToWindow();
		Update_currentlogflags();
		return true;
	}
	virtual bool TransferToWindow() {
		wxCheckBox *chk=(wxCheckBox*) GetWindow();
		logflagtype res=(*targ)&flagbit;
		if(res==flagbit) chk->Set3StateValue(wxCHK_CHECKED);
		else if(!res) chk->Set3StateValue(wxCHK_UNCHECKED);
		else chk->Set3StateValue(wxCHK_UNDETERMINED);
		return true;
	}
	virtual bool Validate(wxWindow* parent) {
		return true;
	}
	void checkboxchange(wxCommandEvent &event) {
		TransferFromWindow();
	}

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(ChkBoxLFFlagValidator, wxValidator)
	EVT_CHECKBOX(wxID_ANY, ChkBoxLFFlagValidator::checkboxchange)
END_EVENT_TABLE()

BEGIN_EVENT_TABLE(log_window, wxFrame)
	EVT_CLOSE(log_window::OnFrameClose)
	EVT_MENU(wxID_SAVE, log_window::OnSave)
	EVT_MENU(wxID_CLEAR, log_window::OnClear)
	EVT_MENU(wxID_CLOSE, log_window::OnClose)
	EVT_MENU(wxID_FILE1, log_window::OnDumpPending)
END_EVENT_TABLE()

static void log_window_AddChkBox(log_window *parent, logflagtype flags, const wxString &str, wxSizer *sz) {
	ChkBoxLFFlagValidator cbv(flags, &parent->lo_flags);
	wxCheckBox *chk=new wxCheckBox(parent, wxID_ANY, str, wxDefaultPosition, wxDefaultSize, wxCHK_3STATE, cbv);
	sz->Add(chk, 0, 0, 0);
}

log_window::log_window(wxWindow *parent, logflagtype flagmask, bool show)
: log_object(flagmask), wxFrame(parent, wxID_ANY, wxT("Log Window")) {
	globallogwindow=this;
	txtct=new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_NOHIDESEL | wxHSCROLL);
	wxBoxSizer *bs=new wxBoxSizer(wxVERTICAL);
	bs->Add(txtct, 1, wxALL | wxEXPAND, 3);
	wxGridSizer *hs=new wxGridSizer(5, 1, 4);
	log_window_AddChkBox(this, lfd_allmask, wxT("ALL"), hs);
	log_window_AddChkBox(this, lfd_err, wxT("ERRORS"), hs);
	for(size_t i=hs->GetChildren().GetCount(); (i%hs->GetCols())!=0; i++) hs->AddStretchSpacer();
	for(unsigned int i=0; i<(sizeof(logflagtype)*8); i++) {
		if((((logflagtype) 1)<<i)&lfd_stringmask) {
			log_window_AddChkBox(this, ((logflagtype) 1)<<i, logflagsstrings[i], hs);
		}
	}
	bs->Add(hs, 0, wxALL, 2);
	SetSizer(bs);

	wxMenu *menuF = new wxMenu;
	menuF->Append( wxID_SAVE, wxT("&Save Log"));
	menuF->Append( wxID_CLEAR, wxT("Clear Log"));
	menuF->Append( wxID_CLOSE, wxT("&Close"));
	wxMenu *menuD = new wxMenu;
	menuD->Append( wxID_FILE1, wxT("Dump &Pendings"));

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuF, wxT("&File"));
	menuBar->Append(menuD, wxT("&Debug"));

	SetMenuBar(menuBar);

	InitDialog();
	if(show) LWShow();
}

log_window::~log_window() {
	globallogwindow=0;
}

void log_window::LWShow(bool shown) {
	isshown=shown;
	if(shown) {
		while(!pending.empty()) {
			txtct->AppendText(pending.front());
			pending.pop();
		}
	}
	Show(shown);
}

void log_window::OnFrameClose(wxCloseEvent &event) {
	LWShow(false);
	event.Veto();
}

void log_window::OnSave(wxCommandEvent &event) {
	time_t now=time(0);
	wxString hint=wxT("retcon-log-")+rc_wx_strftime(wxT("%Y%m%dT%H%M%SZ"), gmtime(&now), now, false);
	wxString filename=wxFileSelector(wxT("Save Log"), wxT(""), hint, wxT("log"), wxT("*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);
	if(filename.Len()) {
		txtct->SaveFile(filename);
	}
}

void log_window::OnClear(wxCommandEvent &event) {
	txtct->Clear();
}

void log_window::OnClose(wxCommandEvent &event) {
	LWShow(false);
}

void log_window::OnDumpPending(wxCommandEvent &event) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		dump_pending_acc(LFT_USERREQ, wxT(""), wxT("\t"), (*it).get());
		dump_pending_tpaneldbloadmap(LFT_USERREQ, wxT(""));
	}
}


log_file::log_file(logflagtype flagmask, const char *filename) : log_object(flagmask), closefpondel(0) {
	fp=fopen(filename, "a");
}
log_file::log_file(logflagtype flagmask, FILE *fp_, bool closefpondel_) : log_object(flagmask), fp(fp_), closefpondel(closefpondel_) { }

log_file::~log_file() {
	if(closefpondel) fclose(fp);
}

void log_file::log_str(logflagtype logflags, const wxString &str) {
	fprintf(fp, "%s", (const char *) str.ToUTF8());
	fflush(fp);
}

logflagtype StrToLogFlags(const wxString &str) {
	logflagtype out=0;
	wxStringTokenizer tkz(str, wxT(",\t\r\n "), wxTOKEN_STRTOK);
	while (tkz.HasMoreTokens()) {
		wxString token = tkz.GetNextToken();
		if(token==wxT("all")) out|=lfd_allmask;
		else if(token==wxT("error")) out|=lfd_err;
		else if(token==wxT("err")) out|=lfd_err;
		else {
			for(unsigned int i=0; i<sizeof(logflagsstrings)/sizeof(const wxChar *); i++) {
				if(token==logflagsstrings[i]) {
					out|=((logflagtype) 1<<i);
					break;
				}
			}
		}
	}
	return out;
}

static void dump_pending_tweet(logflagtype logflags, const wxString &indent, const wxString &indentstep, tweet *t) {
	LogMsgFormat(logflags, wxT("%sTweet: %" wxLongLongFmtSpec "d (%.15s...) lflags: %X"), indent.c_str(), t->id, wxstrstd(t->text).c_str(), t->lflags);
}

static void dump_pending_user(logflagtype logflags, const wxString &indent, const wxString &indentstep, userdatacontainer *u) {
	LogMsgFormat(logflags, wxT("%sUser: %" wxLongLongFmtSpec "d (%s)"), indent.c_str(), u->id, wxstrstd(u->GetUser().screen_name).c_str());
	for(auto it=u->pendingtweets.begin(); it!=u->pendingtweets.end(); ++it) {
		dump_pending_tweet(logflags, indent+indentstep, indentstep, (*it).get());
	}
}

static void dump_pending_acc(logflagtype logflags, const wxString &indent, const wxString &indentstep, taccount *acc) {
	LogMsgFormat(logflags, wxT("%sAccount: %s (%s)"), indent.c_str(), acc->name.c_str(), acc->dispname.c_str());
	for(auto it=acc->pendingusers.begin(); it!=acc->pendingusers.end(); ++it) {
		dump_pending_user(logflags, indent+indentstep, indentstep, (*it).second.get());
	}
}

static void dump_pending_tpaneldbloadmap(logflagtype logflags, const wxString &indent) {
	for(auto it=tpaneldbloadmap.begin(); it!=tpaneldbloadmap.end(); ++it) {
		LogMsgFormat(logflags, wxT("%sLoad Map: %" wxLongLongFmtSpec "d (%.15s...) --> %s (%s) pushflags: %X"), indent.c_str(), it->first, wxstrstd(ad.tweetobjs[it->first]->text).c_str(), wxstrstd(it->second.win->tp->name).c_str(), wxstrstd(it->second.win->tp->dispname).c_str(), it->second.pushflags);
	}
}
