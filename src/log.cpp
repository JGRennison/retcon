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
//  2012 - j.g.rennison@gmail.com
//==========================================================================

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

static void dump_non_acc_user_pendings(logflagtype logflags, const wxString &indent, const wxString &indentstep);

const wxChar *logflagsstrings[]={
	wxT("curlverb"),
	wxT("parsetrace"),
	wxT("parseerr"),
	wxT("socktrace"),
	wxT("sockerr"),
	wxT("tpanel"),
	wxT("netaction"),
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
			SetCurlHandleVerboseState(it->first, currentlogflags&LFT_CURLVERB);
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
	EVT_MENU(wxID_FILE2, log_window::OnDumpTPanelWins)
	EVT_MENU(wxID_FILE3, log_window::OnDumpConnInfo)
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
	log_window_AddChkBox(this, lfd_defaultwin, wxT("DEFAULTS"), hs);
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
	menuD->Append( wxID_FILE2, wxT("Dump &Tpanel Window Data"));
	menuD->Append( wxID_FILE3, wxT("Dump &Socket Data"));

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
	}
	dump_non_acc_user_pendings(LFT_USERREQ, wxT(""), wxT("\t"));
	dump_tweet_pendings(LFT_USERREQ, wxT(""), wxT("\t"));
}

void log_window::OnDumpTPanelWins(wxCommandEvent &event) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		dump_tpanel_scrollwin_data(LFT_USERREQ, wxT(""), wxT("\t"), (*it));
	}
}

void log_window::OnDumpConnInfo(wxCommandEvent &event) {
	dump_pending_active_conn(LFT_USERREQ, wxT(""), wxT("\t"));
	dump_pending_retry_conn(LFT_USERREQ, wxT(""), wxT("\t"));
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		LogMsgFormat(LFT_USERREQ, wxT("Account: %s (%s)"), (*it)->name.c_str(), (*it)->dispname.c_str());
		dump_pending_acc_failed_conns(LFT_USERREQ, wxT("\t"), wxT("\t"), (*it).get());
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
		else if(token==wxT("def")) out|=lfd_defaultwin;
		else if(token==wxT("default")) out|=lfd_defaultwin;
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

static void dump_pending_user_line(logflagtype logflags, const wxString &indent, userdatacontainer *u) {
	time_t now=time(0);
	LogMsgFormat(logflags, wxT("%sUser: %" wxLongLongFmtSpec "d (%s) udc_flags: 0x%X, last update: %" wxLongLongFmtSpec "ds ago, last DB update: %" wxLongLongFmtSpec "ds ago, image ready: %d"), indent.c_str(), u->id, wxstrstd(u->GetUser().screen_name).c_str(), u->udc_flags, (uint64_t) (now-u->lastupdate), (uint64_t) (now-u->lastupdate_wrotetodb), u->ImgIsReady(0));
}

static void dump_pending_tweet(logflagtype logflags, const wxString &indent, const wxString &indentstep, tweet *t, userdatacontainer *exclude_user) {
	LogMsgFormat(logflags, wxT("%sTweet: %" wxLongLongFmtSpec "d (%.15s...) lflags: 0x%X, updcf_flags: 0x%X"), indent.c_str(), t->id, wxstrstd(t->text).c_str(), t->lflags, t->updcf_flags);
	if(t->user && t->user.get()!=exclude_user) dump_pending_user_line(logflags, indent+indentstep, t->user.get());
	if(t->user_recipient && t->user_recipient.get()!=exclude_user) dump_pending_user_line(logflags, indent+indentstep, t->user_recipient.get());
}

static void dump_pending_user(logflagtype logflags, const wxString &indent, const wxString &indentstep, userdatacontainer *u) {
	dump_pending_user_line(logflags, indent, u);
	for(auto it=u->pendingtweets.begin(); it!=u->pendingtweets.end(); ++it) {
		dump_pending_tweet(logflags, indent+indentstep, indentstep, (*it).get(), u);
	}
}

void dump_pending_acc(logflagtype logflags, const wxString &indent, const wxString &indentstep, taccount *acc) {
	LogMsgFormat(logflags, wxT("%sAccount: %s (%s)"), indent.c_str(), acc->name.c_str(), acc->dispname.c_str());
	for(auto it=acc->pendingusers.begin(); it!=acc->pendingusers.end(); ++it) {
		dump_pending_user(logflags, indent+indentstep, indentstep, (*it).second.get());
	}
}

void dump_tweet_pendings(logflagtype logflags, const wxString &indent, const wxString &indentstep) {
	for(auto it=ad.tweetobjs.begin(); it!=ad.tweetobjs.end(); ++it) {
		const tweet *t=it->second.get();
		if(t && !(t->pending_ops.empty())) {
			LogMsgFormat(logflags, wxT("%sTweet with operations pending ready state: %" wxLongLongFmtSpec "d (%.20s...)"), indent.c_str(), t->id, wxstrstd(t->text).c_str());
			for(auto jt=t->pending_ops.begin(); jt!=t->pending_ops.end(); ++jt) {
				LogMsgFormat(logflags, wxT("%s%s%s"), indent.c_str(), indentstep.c_str(), (*jt)->dump().c_str());
			}
		}
	}
}

static void dump_non_acc_user_pendings(logflagtype logflags, const wxString &indent, const wxString &indentstep) {
	std::set<uint64_t> acc_pending_users;
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		for(auto jt=(*it)->pendingusers.begin(); jt!=(*it)->pendingusers.end(); ++jt) {
			acc_pending_users.insert(jt->second->id);
		}
	}
	LogMsgFormat(logflags, wxT("%sOther pending users:"), indent.c_str());
	for(auto it=ad.userconts.begin(); it!=ad.userconts.begin(); ++it) {
		if(!(it->second->pendingtweets.empty())) {
			if(acc_pending_users.find(it->first)==acc_pending_users.end()) {
				dump_pending_user(logflags, indent, indentstep, it->second.get());
			}
		}
	}
}

static void dump_window_pos_data(logflagtype logflags, const wxString &indent, const wxString &indentstep, wxWindow *win) {
	int x, y, px, py;
	win->GetSize(&x, &y);
	win->GetPosition(&px, &py);
	LogMsgFormat(logflags, wxT("%sWindow: %p, size: %d, %d, pos: %d, %d"), indent.c_str(), win, x, y, px, py);
}

void dump_tpanel_scrollwin_data(logflagtype logflags, const wxString &indent, const wxString &indentstep, tpanelparentwin_nt *tppw) {
	int x, y, vx, vy, vsx, vsy;
	tppw->scrollwin->GetSize(&x, &y);
	tppw->scrollwin->GetVirtualSize(&vx, &vy);
	tppw->scrollwin->GetViewStart(&vsx, &vsy);
	const wxWindowList& wl=tppw->scrollwin->GetChildren();
	LogMsgFormat(logflags, wxT("%sTpanel: %s, size: %d, %d, vsize: %d, %d, vstart: %d, %d, children: %d, numcurdisp: %d"), indent.c_str(), wxstrstd(tppw->tp->name).c_str(), x, y, vx, vy, vsx, vsy, wl.GetCount(), tppw->currentdisp.size());
	for(auto it=wl.begin(); it!=wl.end(); ++it) {
		dump_window_pos_data(logflags, indent+indentstep, indentstep, (*it));
	}
}

void dump_pending_acc_failed_conns(logflagtype logflags, const wxString &indent, const wxString &indentstep, taccount *acc) {
	dump_acc_socket_flags(logflags, indent, acc);
	LogMsgFormat(logflags, wxT("%sRestartable Failed Connections: %d"), indent.c_str(), acc->failed_pending_conns.size());
	for(auto it=acc->failed_pending_conns.begin(); it!=acc->failed_pending_conns.end(); ++it) {
		LogMsgFormat(logflags, wxT("%s%sSocket: %s, %p, Error Count: %d, mcflags: 0x"), indent.c_str(), indentstep.c_str(), (*it)->GetConnTypeName().c_str(), (*it), (*it)->errorcount, (*it)->mcflags);
	}
}

void dump_pending_active_conn(logflagtype logflags, const wxString &indent, const wxString &indentstep) {
	LogMsgFormat(logflags, wxT("%sActive connections: %d"), indent.c_str(), std::distance(sm.connlist.begin(), sm.connlist.end()));
	for(auto it=sm.connlist.begin(); it!=sm.connlist.end(); ++it) {
		LogMsgFormat(logflags, wxT("%s%sSocket: %s, %p, Error Count: %d, mcflags: 0x%X"), indent.c_str(), indentstep.c_str(), it->second->GetConnTypeName().c_str(), it->second, it->second->errorcount, it->second->mcflags);
	}
}

void dump_pending_retry_conn(logflagtype logflags, const wxString &indent, const wxString &indentstep) {
	size_t count=0;
	for(auto it=sm.retry_conns.begin(); it!=sm.retry_conns.end(); ++it) {
		if((*it)) count++;
	}
	LogMsgFormat(logflags, wxT("%sConnections pending retry attempts: %d"), indent.c_str(), count);
	for(auto it=sm.retry_conns.begin(); it!=sm.retry_conns.end(); ++it) {
		if(!(*it)) continue;
		LogMsgFormat(logflags, wxT("%s%sSocket: %s, %p, Error Count: %d, mcflags: 0x"), indent.c_str(), indentstep.c_str(), (*it)->GetConnTypeName().c_str(), (*it), (*it)->errorcount, (*it)->mcflags);
	}
}

void dump_acc_socket_flags(logflagtype logflags, const wxString &indent, taccount *acc) {
	LogMsgFormat(logflags, wxT("%sssl: %d, userstreams: %d, ta_flags: 0x%X, restinterval: %ds, enabled: %d, userenabled: %d, init: %d, active: %d, streaming_on: %d, stream_fail_count: %d, rest_on: %d"), indent.c_str(), acc->ssl, acc->userstreams, acc->ta_flags, acc->restinterval, acc->enabled, acc->userenabled, acc->init, acc->active, acc->streaming_on, acc->stream_fail_count, acc->rest_on);
}
