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
	wxT("parse"),
	wxT("auth"),
	wxT("socktrace"),
	wxT("sockerr"),
	wxT("tpanel"),
	wxT("twitact"),
	wxT("dbtrace"),
	wxT("ztrace"),
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
	LogMsgRaw(logflags, wxString::Format(wxT("%s %+10s %s\n"), time_str.c_str(), flag_str.c_str(), in.c_str()).c_str());
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

log_window::log_window(wxWindow *parent, logflagtype flagmask, bool show)
: log_object(flagmask), wxFrame(parent, wxID_ANY, wxT("Log Window")) {
	globallogwindow=this;
	txtct=new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_NOHIDESEL | wxHSCROLL);
	wxBoxSizer *bs=new wxBoxSizer(wxVERTICAL);
	bs->Add(txtct, 1, wxALL | wxEXPAND, 3);
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

bool log_window::OnFrameClose(wxFrame *frame) {
	LWShow(false);
	return false;
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
