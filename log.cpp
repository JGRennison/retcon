#include "retcon.h"
#include <wx/tokenzr.h>
#ifdef __WINDOWS__
#include "windows.h"
#else
#include <sys/time.h>
#endif

log_window *globallogwindow;

logflagtype currentlogflags=0;
std::forward_list<log_object*> logfunclist;

const wxChar *logflagsstrings[]={
	wxT("curlverb"),
	wxT("parsetrace"),
	wxT("parseerr"),
	wxT("authtrace"),
	wxT("socktrace"),
	wxT("sockerr"),
	wxT("tpanel"),
	wxT("twitact"),
	wxT("dbtrace"),
	wxT("dberr"),
	wxT("ztrace"),
	wxT("zerr"),
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
	InitDialog();
	if(show) Show();
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

log_file::log_file(logflagtype flagmask, const char *filename) : log_object(flagmask), closefpondel(0) {
	fp=fopen(filename, "a");
}
log_file::log_file(logflagtype flagmask, FILE *fp_, bool closefpondel_) : log_object(flagmask), fp(fp_), closefpondel(closefpondel_) { }

log_file::~log_file() {
	if(closefpondel) fclose(fp);
}

void log_file::log_str(logflagtype logflags, const wxString &str) {
	fprintf(fp, "%s", (const char *) str.ToUTF8());
}

logflagtype StrToLogFlags(const wxString &str) {
	logflagtype out=0;
	wxStringTokenizer tkz(str, wxT(",\t\r\n "), wxTOKEN_STRTOK);
	while (tkz.HasMoreTokens()) {
		wxString token = tkz.GetNextToken();
		if(token==wxT("all")) out|=lfd_allmask;
		else if(token==wxT("error")) out|=lfd_err;
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
