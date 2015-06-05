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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "log.h"
#include "log-impl.h"
#include "log-util.h"
#include "socket.h"
#include "twit.h"
#include "taccount.h"
#include "util.h"
#include "tpanel.h"
#include "tpanel-data.h"
#include "alldata.h"
#include "twitcurlext.h"
#include "db.h"
#include "utf8.h"
#include <wx/tokenzr.h>
#include <wx/filedlg.h>
#ifdef __WINDOWS__
#include "windows.h"
#else
#include <sys/time.h>
#include <malloc.h>
#endif
#include <memory>
#include <vector>
#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/textctrl.h>
#include <wx/event.h>
#include <wx/thread.h>
#if defined(__WXGTK__)
#include <glib.h>
#endif

log_window *globallogwindow = nullptr;
std::unique_ptr<Redirector_wxLog> globalwxlogredirector;

LOGT currentlogflags = LOGT::ZERO;
flagwrapper<LOGIMPLF> logimpl_flags = 0;
std::vector<log_object*> logfunclist;

static void dump_non_acc_user_pendings(LOGT logflags, const std::string &indent, const std::string &indentstep);

const std::string logflagsstrings[] = {
	"curlverb",
	"parsetrace",
	"parseerr",
	"socktrace",
	"sockerr",
	"netaction",
	"dbtrace",
	"dbinfo",
	"dberr",
	"ztrace",
	"zerr",
	"othertrace",
	"othererr",
	"userreq",
	"pendingtrace",
	"wxlog",
	"wxverbose",
	"filtererr",
	"filtertrace",
	"threadtrace",
	"fileiotrace",
	"fileioerr",
	"notifyevt",
	"tpaneltrace",
	"tpanelinfo",
	"glib",
};

void Update_currentlogflags() {
	LOGT old_currentlogflags = currentlogflags;
	LOGT t_currentlogflags = LOGT::ZERO;
	for(auto &it : logfunclist) t_currentlogflags |= it->lo_flags;
	currentlogflags = t_currentlogflags;
	if((old_currentlogflags ^ currentlogflags) & LOGT::CURLVERB) {
		for(auto &it : sm.connlist) {
			SetCurlHandleVerboseState(it.ch, currentlogflags & LOGT::CURLVERB);
		}
	}
	if(currentlogflags & LOGT::WXVERBOSE) wxLog::SetLogLevel(wxLOG_Info);
	else if(currentlogflags & LOGT::WXLOG) wxLog::SetLogLevel(wxLOG_Message);
	else wxLog::SetLogLevel(wxLOG_FatalError);
}

void LogMsgRaw(LOGT logflags, const std::string &str) {
	for(auto &lo : logfunclist) {
		if(logflags & lo->lo_flags) lo->log_str(logflags, str);
	}
}

std::string LogMsgFlagString(LOGT logflags) {
	std::string out;
	auto bitint = flag_unwrap<LOGT>(logflags & LOGT::GROUP_STR);
	while(bitint) {
		int offset = __builtin_ctzl(bitint);
		bitint &= ~(static_cast<decltype(bitint)>(1) << offset);
		if(out.size()) out += ",";
		out += logflagsstrings[offset];
	}
	return out;
}

void LogMsgProcess(LOGT logflags, const std::string &str) {
	time_t now = time(nullptr);
	unsigned int ms;
	#ifdef __WINDOWS__
	SYSTEMTIME time;
	GetSystemTime(&time);
	ms = time.wMilliseconds;
	#else
	timeval time;
	gettimeofday(&time, NULL);
	ms = (time.tv_usec / 1000);
	#endif
	std::string time_str = rc_strftime(string_format("%%F %%T.%03d %%z", ms), localtime(&now), now, true);
	std::string flag_str = LogMsgFlagString(logflags) + ":";
	std::string in = str;
	in.erase(std::find_if(in.rbegin(), in.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), in.end());

	#ifndef __WINDOWS__
	if(logimpl_flags & LOGIMPLF::LOGMEMUSAGE) {
		struct mallinfo mi;
		mi = mallinfo();
		in = string_format("Used: %10d, Alloc: %10d, %s", mi.uordblks, mi.arena, cstr(in));
	}
	#endif

	LogMsgRaw(logflags, string_format("%s %-20s %s\n", cstr(time_str), cstr(flag_str), cstr(in)));
}

log_object::log_object(LOGT flagmask) : lo_flags(flagmask) {
	logfunclist.push_back(this);
	Update_currentlogflags();
}

log_object::~log_object() {
	container_unordered_remove(logfunclist, this);
	Update_currentlogflags();
}

void log_window::log_str(LOGT logflags, const std::string &str) {
	if(isshown) txtct->AppendText(wxstrstd(str));
	else pending.push(str);
}

struct ChkBoxLFFlagValidator : public wxValidator {
	LOGT flagbit;
	LOGT *targ;

	ChkBoxLFFlagValidator(LOGT flagbit_, LOGT *targ_)
		: wxValidator(), flagbit(flagbit_), targ(targ_) { }
	virtual wxObject* Clone() const { return new ChkBoxLFFlagValidator(flagbit, targ); }
	virtual bool TransferFromWindow() {
		wxCheckBox *chk = (wxCheckBox*) GetWindow();
		wxCheckBoxState res = chk->Get3StateValue();
		if(res == wxCHK_CHECKED) (*targ) |= flagbit;
		else if(res == wxCHK_UNCHECKED) (*targ) &= ~flagbit;
		chk->GetParent()->TransferDataToWindow();
		Update_currentlogflags();
		return true;
	}
	virtual bool TransferToWindow() {
		wxCheckBox *chk = (wxCheckBox*) GetWindow();
		LOGT res = (*targ) &flagbit;
		if(res == flagbit) chk->Set3StateValue(wxCHK_CHECKED);
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

enum {
	LOGWIN_ID_DUMP_PENDING = wxID_HIGHEST + 1,
	LOGWIN_ID_DUMP_CONN,
	LOGWIN_ID_DUMP_STATS,
	LOGWIN_ID_FLUSH_STATE,
	LOGWIN_ID_FLUSH_LOGFILES,
	LOGWIN_ID_RETRY_FIRST_CONN_NOW,
	LOGWIN_ID_RETRY_STREAMS_NOW,
};

BEGIN_EVENT_TABLE(log_window, wxFrame)
	EVT_CLOSE(log_window::OnFrameClose)
	EVT_MENU(wxID_SAVE, log_window::OnSave)
	EVT_MENU(wxID_CLEAR, log_window::OnClear)
	EVT_MENU(wxID_CLOSE, log_window::OnClose)
	EVT_MENU(LOGWIN_ID_DUMP_PENDING, log_window::OnDumpPending)
	EVT_MENU(LOGWIN_ID_DUMP_CONN, log_window::OnDumpConnInfo)
	EVT_MENU(LOGWIN_ID_DUMP_STATS, log_window::OnDumpStats)
	EVT_MENU(LOGWIN_ID_FLUSH_STATE, log_window::OnFlushState)
	EVT_MENU(LOGWIN_ID_FLUSH_LOGFILES, log_window::OnFlushLogOutputs)
	EVT_MENU(LOGWIN_ID_RETRY_FIRST_CONN_NOW, log_window::OnRetryFirstPendingConnNow)
	EVT_MENU(LOGWIN_ID_RETRY_STREAMS_NOW, log_window::OnRestartStreamConnsNow)
	EVT_MENU_OPEN(log_window::OnMenuOpen)
END_EVENT_TABLE()

static void log_window_AddChkBox(log_window *parent, LOGT flags, const wxString &str, wxSizer *sz) {
	ChkBoxLFFlagValidator cbv(flags, &parent->lo_flags);
	wxCheckBox *chk = new wxCheckBox(parent, wxID_ANY, str, wxDefaultPosition, wxDefaultSize, wxCHK_3STATE, cbv);
	sz->Add(chk, 0, 0, 0);
}

log_window::log_window(wxWindow *parent, LOGT flagmask, bool show)
: log_object(flagmask), wxFrame(parent, wxID_ANY, wxT("Log Window")) {
	globallogwindow = this;
	txtct = new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_NOHIDESEL | wxHSCROLL);
	wxBoxSizer *bs = new wxBoxSizer(wxVERTICAL);
	bs->Add(txtct, 1, wxALL | wxEXPAND, 3);
	wxGridSizer *hs = new wxGridSizer(5, 1, 4);
	log_window_AddChkBox(this, LOGT::GROUP_ALL, wxT("ALL"), hs);
	log_window_AddChkBox(this, LOGT::GROUP_ERR, wxT("ERRORS"), hs);
	log_window_AddChkBox(this, LOGT::GROUP_LOGWINDEF, wxT("DEFAULTS"), hs);
	for(size_t i = hs->GetChildren().GetCount(); (i % hs->GetCols()) != 0; i++) hs->AddStretchSpacer();
	for(unsigned int i = 0; i < (sizeof(LOGT) * 8); i++) {
		if(flag_wrap<LOGT>(1 << i) & LOGT::GROUP_STR) {
			log_window_AddChkBox(this, flag_wrap<LOGT>(1 << i), wxstrstd(logflagsstrings[i]), hs);
		}
	}
	bs->Add(hs, 0, wxALL, 2);
	SetSizer(bs);

	wxMenu *menuF = new wxMenu;
	menuF->Append(wxID_SAVE, wxT("&Save Log"));
	menuF->Append(wxID_CLEAR, wxT("Clear Log"));
	menuF->Append(wxID_CLOSE, wxT("&Close"));
	debug_menu = new wxMenu;

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuF, wxT("&File"));
	menuBar->Append(debug_menu, wxT("&Debug"));

	SetMenuBar(menuBar);

	InitDialog();
	LWShow(show);
}

void log_window::OnMenuOpen(wxMenuEvent &event) {
	if(event.GetMenu() == debug_menu) {
		DestroyMenuContents(debug_menu);

		debug_menu->Append(LOGWIN_ID_DUMP_PENDING, wxT("Dump &Pendings"));
		debug_menu->Append(LOGWIN_ID_DUMP_CONN, wxT("Dump &Socket Data"));
		debug_menu->Append(LOGWIN_ID_DUMP_STATS, wxT("Dump S&tats"));
		debug_menu->Append(LOGWIN_ID_FLUSH_STATE, wxT("&Flush State"));

		unsigned int flushable_log_outputs = 0;
		for(auto &it : logfunclist) {
			if(it->IsFlushable())
				flushable_log_outputs++;
		}
		if(flushable_log_outputs)
			debug_menu->Append(LOGWIN_ID_FLUSH_LOGFILES, wxString::Format(wxT("Flush %u Log Outputs"), flushable_log_outputs));

		if(sm.PendingRetryCount())
			debug_menu->Append(LOGWIN_ID_RETRY_FIRST_CONN_NOW,
					wxString::Format(wxT("Retry first of %u pending connection attempts"), sm.PendingRetryCount()));

		unsigned int accs_restartable_stream_conn = 0;
		for(auto &it : alist) {
			if(it->CanRestartStreamingConn())
				accs_restartable_stream_conn++;
		}
		if(accs_restartable_stream_conn)
			debug_menu->Append(LOGWIN_ID_RETRY_STREAMS_NOW,
					wxString::Format(wxT("Retry %u failed streaming connection"), accs_restartable_stream_conn));
	}
}

log_window::~log_window() {
	container_unordered_remove(logfunclist, this);
	LogMsgFormat(LOGT::OTHERTRACE, "log_window::~log_window");
	globallogwindow = nullptr;
}

void log_window::LWShow(bool shown) {
	isshown = shown;
	if(shown) {
		while(!pending.empty()) {
			txtct->AppendText(wxstrstd(pending.front()));
			pending.pop();
		}
	}
	Show(shown);
}

void log_window::OnFrameClose(wxCloseEvent &event) {
	if(event.CanVeto()) {
		LWShow(false);
		event.Veto();
	}
	else {
		globallogwindow = nullptr;
		Destroy();
	}
}

void log_window::OnSave(wxCommandEvent &event) {
	time_t now = time(nullptr);
	wxString hint = wxT("retcon-log-")+rc_wx_strftime(wxT("%Y%m%dT%H%M%SZ"), gmtime(&now), now, false);
	wxString filename = wxFileSelector(wxT("Save Log"), wxT(""), hint, wxT("log"), wxT("*.*"), wxFD_SAVE | wxFD_OVERWRITE_PROMPT, this);
	if(filename.Len()) {
		txtct->SaveFile(filename);
	}
}

void log_window::OnClear(wxCommandEvent &event) {
	txtct->Clear();
}

void log_window::OnClose(wxCommandEvent &event) {
	container_unordered_remove(logfunclist, this);
	LogMsgFormat(LOGT::OTHERTRACE, "log_window::OnClose");
	LWShow(false);
}

void log_window::OnDumpPending(wxCommandEvent &event) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		dump_pending_acc(LOGT::USERREQ, "", "\t", (*it).get());
	}
	dump_non_acc_user_pendings(LOGT::USERREQ, "", "\t");
	dump_tweet_pendings(LOGT::USERREQ, "", "\t");
}

void log_window::OnDumpConnInfo(wxCommandEvent &event) {
	dump_pending_active_conn(LOGT::USERREQ, "", "\t");
	dump_pending_retry_conn(LOGT::USERREQ, "", "\t");
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		LogMsgFormat(LOGT::USERREQ, "Account: %s (%s)", cstr((*it)->name), cstr((*it)->dispname));
		dump_pending_acc_failed_conns(LOGT::USERREQ, "\t", "\t", (*it).get());
	}
}

void log_window::OnDumpStats(wxCommandEvent &event) {
	dump_id_stats(LOGT::USERREQ, "", "\t");
}

void log_window::OnFlushState(wxCommandEvent &event) {
	DBC_AsyncWriteBackState();
}

void log_window::OnFlushLogOutputs(wxCommandEvent &event) {
	for(auto &it : logfunclist) {
		it->Flush();
	}
}

void log_window::OnRetryFirstPendingConnNow(wxCommandEvent &event) {
	sm.CancelRetryTimer();
	sm.RetryConnNow();
}

void log_window::OnRestartStreamConnsNow(wxCommandEvent &event) {
	for(auto &it : alist) {
		if(it->CanRestartStreamingConn())
			it->TryRestartStreamingConnNow();
	}
}

log_file::log_file(LOGT flagmask, const char *filename) : log_object(flagmask), closefpondel(0) {
	fp=fopen(filename, "a");
}
log_file::log_file(LOGT flagmask, FILE *fp_, bool closefpondel_) : log_object(flagmask), fp(fp_), closefpondel(closefpondel_) { }

log_file::~log_file() {
	if(closefpondel) fclose(fp);
}

void log_file::log_str(LOGT logflags, const std::string &str) {
	fputs(cstr(str), fp);
	if(logimpl_flags & LOGIMPLF::FFLUSH) fflush(fp);
}

void log_file::Flush() {
	fflush(fp);
}

LOGT StrToLogFlags(const std::string &str) {
	LOGT out = LOGT::ZERO;
	wxString wstr = wxstrstd(str);
	wxStringTokenizer tkz(wstr, wxT(",\t\r\n "), wxTOKEN_STRTOK);
	while (tkz.HasMoreTokens()) {
		wxString token = tkz.GetNextToken();
		if(token == wxT("all")) out |= LOGT::GROUP_ALL;
		else if(token == wxT("error")) out |= LOGT::GROUP_ERR;
		else if(token == wxT("err")) out |= LOGT::GROUP_ERR;
		else if(token == wxT("def")) out |= LOGT::GROUP_LOGWINDEF;
		else if(token == wxT("default")) out |= LOGT::GROUP_LOGWINDEF;
		else {
			std::string stdtoken = stdstrwx(token);
			for(unsigned int i = 0; i < sizeof(logflagsstrings) / sizeof(logflagsstrings[0]); i++) {
				if(stdtoken == logflagsstrings[i]) {
					out |= flag_wrap<LOGT>(1 << i);
					break;
				}
			}
		}
	}
	return out;
}

std::string truncate_tweet_text(const std::string &input) {
	std::string short_text = input;
	size_t newsize = get_utf8_truncate_offset(short_text.data(), 20, short_text.size());
	if(newsize < short_text.size()) {
		short_text.resize(newsize);
		short_text += "...";
	}
	return std::move(short_text);
}

std::string tweet_log_line(const tweet *t) {
	std::string sname = "???";
	if(t->user && !t->user->GetUser().screen_name.empty()) sname = t->user->GetUser().screen_name;

	std::string short_text = truncate_tweet_text(t->text);

	std::string output = string_format("Tweet: %" llFmtSpec "d @%s (%s) tflags: %s, lflags: 0x%X, pending (default): %d, TPs: ",
			t->id, cstr(sname), cstr(short_text), cstr(t->flags.GetString()), t->lflags, (int) t->IsPendingConst().IsReady());
	bool needcomma = false;
	t->IterateTP([&](const tweet_perspective &tp) {
		std::string thistp = tp.GetFlagStringWithName();
		if(thistp.empty()) return;
		if(needcomma) output += ", ";
		else needcomma = true;
		output += thistp;
	});
	return std::move(output);
}

std::string tweet_long_log_line(const tweet *t) {
	std::string output = tweet_log_line(t) + "\n\t";
	if(t->user_recipient) {
		std::string sname = "???";
		if(!t->user_recipient->GetUser().screen_name.empty())
			sname = t->user_recipient->GetUser().screen_name;
		output += "recipient: " + sname + ", ";
	}
	std::string createtime(ctime(&(t->createtime)));
	trim(createtime);
	output += string_format("reply to: %" llFmtSpec "u, retweets: %u, favs: %u, source: %s, create time: %s, ",
			t->in_reply_to_status_id, t->retweet_count, t->favourite_count, cstr(t->source), cstr(createtime));
	if(!t->quoted_tweet_ids.empty()) {
		bool first = true;
		for(auto &it : t->quoted_tweet_ids) {
			if(first)
				output += string_format("Quoted tweets: %" llFmtSpec "u", it);
			else
				output += string_format(", %" llFmtSpec "u", it);
			first = false;
		}
	}
	if(t->rtsrc) {
		output += "\n\tRT source: " + tweet_long_log_line(t->rtsrc.get());
	}
	return std::move(output);
}

std::string user_screenname_log(uint64_t id) {
	udc_ptr u = ad.GetExistingUserContainerById(id);
	if(u && !u->GetUser().screen_name.empty()) {
		return u->GetUser().screen_name;
	}
	else return "????";
}

std::string user_short_log_line(uint64_t id) {
	return string_format("User: %" llFmtSpec "d @%s", id, cstr(user_screenname_log(id)));
}

std::string tweet_short_log_line(uint64_t id) {
	tweet_ptr t = ad.GetExistingTweetById(id);
	if(t && t->user) {
		return string_format("Tweet: %" llFmtSpec "d @%s (%s)", id, cstr(user_screenname_log(t->user->id)), cstr(truncate_tweet_text(t->text)));
	}
	else {
		return string_format("Tweet: %" llFmtSpec "d", id);
	}
}

static void dump_pending_user_line(LOGT logflags, const std::string &indent, userdatacontainer *u) {
	time_t now = time(nullptr);
	LogMsgFormat(logflags, "%sUser: %" llFmtSpec "d (%s) udc_flags: 0x%X, last update: %" llFmtSpec "ds ago"
			", last DB update: %" llFmtSpec "ds ago, image ready: %d, ready (nx): %d, ready (x): %d",
			cstr(indent), u->id, cstr(u->GetUser().screen_name), u->udc_flags, (uint64_t) (now-u->lastupdate),
			(uint64_t) (now-u->lastupdate_wrotetodb), u->ImgIsReady(PENDING_REQ::PROFIMG_NEED), u->IsReady(0), u->IsReady(PENDING_REQ::USEREXPIRE));
}

static void dump_pending_tweet(LOGT logflags, const std::string &indent, const std::string &indentstep, tweet *t, userdatacontainer *exclude_user) {
	LogMsgFormat(logflags, "%sTweet: %s", cstr(indent), cstr(tweet_log_line(t)));
	if(t->user && t->user.get()!=exclude_user) dump_pending_user_line(logflags, indent+indentstep, t->user.get());
	if(t->user_recipient && t->user_recipient.get()!=exclude_user) dump_pending_user_line(logflags, indent+indentstep, t->user_recipient.get());
}

static void dump_pending_user(LOGT logflags, const std::string &indent, const std::string &indentstep, userdatacontainer *u) {
	dump_pending_user_line(logflags, indent, u);
	for(auto &it : u->pendingtweets) {
		dump_pending_tweet(logflags, indent + indentstep, indentstep, it.get(), u);
	}
}

void dump_pending_acc(LOGT logflags, const std::string &indent, const std::string &indentstep, taccount *acc) {
	LogMsgFormat(logflags, "%sAccount: %s (%s)", cstr(indent), cstr(acc->name), cstr(acc->dispname));
	for(auto &it : acc->pendingusers) {
		dump_pending_user(logflags, indent + indentstep, indentstep, it.second.get());
	}
}

static void dump_tweet_line(LOGT logflags, const std::string &indent, const std::string &indentstep, const tweet *t) {
	LogMsgFormat(logflags, "%sTweet with operations pending ready state: %s", cstr(indent), cstr(tweet_log_line(t)));
	for(auto &jt : t->pending_ops) {
		LogMsgFormat(logflags, "%s%s%s", cstr(indent), cstr(indentstep), cstr(jt->dump()));
	}
}

void dump_tweet_pendings(LOGT logflags, const std::string &indent, const std::string &indentstep) {
	bool done_header = false;
	for(auto &it : ad.tweetobjs) {
		const tweet *t = it.second.get();
		if(t && !(t->pending_ops.empty())) {
			if(!done_header) {
				done_header = true;
				LogMsgFormat(logflags, "%sTweets with operations pending ready state:", cstr(indent));
			}
			dump_tweet_line(logflags, indent + indentstep, indentstep, t);
		}
	}
	if(!ad.noacc_pending_tweetobjs.empty()) {
		LogMsgFormat(logflags, "%sTweets pending usable account:", cstr(indent));
		for(auto &it : ad.noacc_pending_tweetobjs) {
			dump_tweet_line(logflags, indent + indentstep, indentstep, it.second.get());
		}
	}
	if(!ad.handlenew_pending_ops.empty()) {
		LogMsgFormat(logflags, "%sTotal handlenew_pending_op pending count: %zu", cstr(indent), ad.handlenew_pending_ops.size());
	}
}

static void dump_non_acc_user_pendings(LOGT logflags, const std::string &indent, const std::string &indentstep) {
	std::set<uint64_t> acc_pending_users;
	for(auto &it : alist) {
		for(auto &jt : it->pendingusers) {
			acc_pending_users.insert(jt.second->id);
		}
	}
	LogMsgFormat(logflags, "%sOther pending users:", cstr(indent));
	for(auto &it : ad.userconts) {
		if(!(it.second->pendingtweets.empty())) {
			if(acc_pending_users.find(it.first) == acc_pending_users.end()) {
				dump_pending_user(logflags, indent + indentstep, indentstep, it.second.get());
			}
		}
	}
	if(!ad.noacc_pending_userconts.empty()) {
		LogMsgFormat(logflags, "%sUsers pending usable account:", cstr(indent));
		for(auto &it : ad.noacc_pending_userconts) {
			dump_pending_user(logflags, indent + indentstep, indentstep, it.second.get());
		}
	}
}

void dump_pending_acc_failed_conns(LOGT logflags, const std::string &indent, const std::string &indentstep, taccount *acc) {
	dump_acc_socket_flags(logflags, indent, acc);
	LogMsgFormat(logflags, "%sRestartable Failed Connections: %d", cstr(indent), acc->failed_pending_conns.size());
	for(auto &it : acc->failed_pending_conns) {
		LogMsgFormat(logflags, "%s%sSocket: %s, ID: %d, Error Count: %d, mcflags: 0x%X",
				cstr(indent), cstr(indentstep), cstr(it->GetConnTypeName()), it->id, it->errorcount, it->mcflags);
	}
}

void dump_pending_active_conn(LOGT logflags, const std::string &indent, const std::string &indentstep) {
	LogMsgFormat(logflags, "%sActive connections: %d", cstr(indent), std::distance(sm.connlist.begin(), sm.connlist.end()));
	for(auto &it : sm.connlist) {
		if(!it.cs) continue;
		LogMsgFormat(logflags, "%s%sSocket: %s, ID: %d, Error Count: %d, mcflags: 0x%X",
				cstr(indent), cstr(indentstep), cstr(it.cs->GetConnTypeName()), it.cs->id, it.cs->errorcount, it.cs->mcflags);
	}
}

void dump_pending_retry_conn(LOGT logflags, const std::string &indent, const std::string &indentstep) {
	size_t count = 0;
	for(auto &it : sm.retry_conns) {
		if(it) count++;
	}
	LogMsgFormat(logflags, "%sConnections pending retry attempts: %d", cstr(indent), count);
	for(auto &it : sm.retry_conns) {
		if(!it) continue;
		LogMsgFormat(logflags, "%s%sSocket: %s, ID: %d, Error Count: %d, mcflags: 0x%X",
				cstr(indent), cstr(indentstep), cstr(it->GetConnTypeName()), it->id, it->errorcount, it->mcflags);
	}
}

void dump_acc_socket_flags(LOGT logflags, const std::string &indent, taccount *acc) {
	LogMsgFormat(logflags, "%sssl: %d, userstreams: %d, ta_flags: 0x%X, restinterval: %ds, enabled: %d, userenabled: %d, init: %d, active: %d, streaming_on: %d, stream_fail_count: %d, rest_on: %d",
			cstr(indent), acc->ssl, acc->userstreams, acc->ta_flags, acc->restinterval, acc->enabled, acc->userenabled, acc->init, acc->active, acc->streaming_on, acc->stream_fail_count, acc->rest_on);
}

void dump_cids_stats(const cached_id_sets &cids, LOGT logflags, const std::string &indent, const std::string &indentstep) {
	size_t total = 0;
	cids.IterateLists([&](const char *name, tweetidset cached_id_sets::* mptr, unsigned long long flag) {
		const tweetidset &set = cids.*mptr;
		LogMsgFormat(logflags, "%s%s (%s): %zu", cstr(indent), cstr(name), cstr(tweet_flags::GetValueString(flag)), set.size());
		total += set.size();
	});
	LogMsgFormat(logflags, "%sTotal: %zu", cstr(indent), total);
}

void dump_acc_id_stats(const taccount &acc, LOGT logflags, const std::string &indent, const std::string &indentstep) {
	auto line = [&](const char *name, size_t value) {
		LogMsgFormat(logflags, "%s%s: %zu", cstr(indent), cstr(name), value);
	};
	line("Tweets IDs", acc.tweet_ids.size());
	line("DM IDs", acc.dm_ids.size());
	line("Pending users", acc.pendingusers.size());
	line("User relations", acc.user_relations.size());
}

void dump_id_stats(LOGT logflags, const std::string &indent, const std::string &indentstep) {
	auto line = [&](const char *name, size_t value) {
		LogMsgFormat(logflags, "%s%s: %zu", cstr(indent), cstr(name), value);
	};
	line("Loaded users", ad.userconts.size());
	line("Loaded tweets", ad.tweetobjs.size());
	line("DB unloaded users", ad.unloaded_db_user_ids.size());
	line("DB unloaded tweets", ad.unloaded_db_tweet_ids.size());
	line("DB loaded tweets", ad.loaded_db_tweet_ids.size());
	line("No acc pending users", ad.noacc_pending_userconts.size());
	line("No acc pending tweets", ad.noacc_pending_tweetobjs.size());

	struct count_item {
		size_t count = 0;
		size_t total = 0;

		void add(size_t value) {
			if(value) {
				total += value;
				count++;
			}
		}
	};

	auto dline = [&](const char *name, const count_item &value) {
		line(string_format("%s: count", name).c_str(), value.count);
		line(string_format("%s: total", name).c_str(), value.total);
	};

	auto dline_bytes = [&](const char *name, const count_item &value) {
		line(string_format("%s: count", name).c_str(), value.count);
		line(string_format("%s: total bytes", name).c_str(), value.total);
	};

	{
		count_item mentionindex;
		count_item pendingtweets;
		for(const auto &it : ad.userconts) {
			if(it.second) {
				mentionindex.add(it.second->mention_set.size());
				pendingtweets.add(it.second->pendingtweets.size());
			}
		}
		dline("Loaded users: mention index", mentionindex);
		dline("Loaded users: pending tweet IDs", pendingtweets);
	}

	{
		count_item pending_ops;
		for(const auto &it : ad.tweetobjs) {
			if(it.second) {
				pending_ops.add(it.second->pending_ops.size());
			}
		}
		dline("Loaded tweets: pending operations", pending_ops);
	}

	line("Loaded tpanels", ad.tpanels.size());
	size_t total_tpanel_ids = 0;
	for(const auto &it : ad.tpanels) {
		total_tpanel_ids += it.second->tweetlist.size();
	}
	line("Total tpanel tweet IDs", total_tpanel_ids);

	line("Loaded media entities", ad.media_list.size());
	line("Loaded media map URLs", ad.img_media_map.size());

	{
		count_item thumb;
		count_item full;
		for(const auto &it : ad.media_list) {
			if(it.second) {
				media_entity &me = *(it.second);
				if(me.thumbimg.IsOk()) {
					thumb.add(static_cast<size_t>(me.thumbimg.GetWidth() * me.thumbimg.GetHeight() * me.thumbimg.GetDepth() / 8));
				}
				full.add(me.fulldata.size());
			}
		}
		dline_bytes("Media thumbnails", thumb);
		dline_bytes("Media full images", full);
	}

	line("DM indexes", ad.user_dm_indexes.size());
	size_t total_dm_indexes = 0;
	for(const auto &it : ad.user_dm_indexes) {
		total_dm_indexes += it.second.ids.size();
	}
	line("DM indexes: total size", total_dm_indexes);

	LogMsgFormat(logflags, "%sCIDS:", cstr(indent));
	dump_cids_stats(ad.cids, logflags, indent + indentstep, indentstep);

	for(auto &it : alist) {
		LogMsgFormat(logflags, "%sAccount: %s (%s)", cstr(indent), cstr(it->name), cstr(it->dispname));
		dump_acc_id_stats(*it, logflags, indent + indentstep, indentstep);
	}
}

void Redirector_wxLog::DoLog(wxLogLevel level, const wxChar *msg, time_t timestamp) {
	last_loglevel = level;
	wxLog::DoLog(level, msg, timestamp);
}

void Redirector_wxLog::DoLogString(const wxChar *msg, time_t timestamp) {
	TSLogMsg(last_loglevel <= wxLOG_Message ? LOGT::WXLOG : LOGT::WXVERBOSE, stdstrwx(msg));
}

void InitWxLogger() {
	if(globalwxlogredirector) return;
	globalwxlogredirector.reset(new Redirector_wxLog());
	globalwxlogredirector->SetTimestamp(0);
	globalwxlogredirector->SetRepetitionCounting(false);
	wxLog::SetActiveTarget(globalwxlogredirector.get());
}

void DeInitWxLogger() {
	wxLog::SetActiveTarget(nullptr);
	globalwxlogredirector.reset();
}

DEFINE_EVENT_TYPE(wxextLOGEVT)

enum {
	wxextLOGEVT_ID_THREADLOGMSG = 1,
};

struct logevt_handler : wxEvtHandler {
	void OnThreadLogMsg(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(logevt_handler, wxEvtHandler)
EVT_COMMAND(wxextLOGEVT_ID_THREADLOGMSG, wxextLOGEVT, logevt_handler::OnThreadLogMsg)
END_EVENT_TABLE()


//TODO: Fix string being converted twice for wxCommandEvent bundling

void logevt_handler::OnThreadLogMsg(wxCommandEvent &event) {
	LogMsg(flag_wrap<LOGT>(event.GetExtraLong()), stdstrwx(event.GetString()));
}

logevt_handler the_logevt_handler;

void ThreadSafeLogMsg(LOGT logflags, const std::string &str) {
	if(wxThread::IsMain()) {
		LogMsg(logflags, str);
		return;
	}

	ThreadAlwaysLogMsg(logflags, str);
}

void ThreadAlwaysLogMsg(LOGT logflags, const std::string &str) {
	wxCommandEvent evt(wxextLOGEVT, wxextLOGEVT_ID_THREADLOGMSG);
	evt.SetString(wxstrstd(str));	//prevent any COW semantics
	evt.SetExtraLong(flag_unwrap<LOGT>(logflags));
	the_logevt_handler.AddPendingEvent(evt);
}

#if defined(__WXGTK__)

const char *GetGlibLogLevelString(GLogLevelFlags log_level) {
	switch (log_level & G_LOG_LEVEL_MASK) {
		case G_LOG_LEVEL_ERROR:
			return "ERROR";
		case G_LOG_LEVEL_CRITICAL:
			return "CRITICAL";
		case G_LOG_LEVEL_WARNING:
			return "WARNING";
		case G_LOG_LEVEL_MESSAGE:
			return "Message";
		case G_LOG_LEVEL_INFO:
			return "INFO";
		case G_LOG_LEVEL_DEBUG:
			return "DEBUG";
		default:
			return "UNKNOWN";
	}
}

static void GlibLogHandler(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
	if(!(currentlogflags & LOGT::GLIB))
		return;

	if(log_level & G_LOG_FLAG_RECURSION)
		return;

	const char *extra = "";

	if(log_domain && strcmp(log_domain, "GLib-GObject") == 0 && message
			&& strstr(message, "has no handler with id")
			&& (log_level & G_LOG_LEVEL_MASK) == G_LOG_LEVEL_WARNING) {
		// found gobject signal spam message, only show once
		static bool found_gsignal_warning = false;
		if(found_gsignal_warning)
			return;

		found_gsignal_warning = true;
		extra = "\n\tIgnoring future warnings of this type.\n";
	}

	if(log_domain) {
		TALogMsgFormat(LOGT::GLIB, "%s-%s: %s%s", log_domain, GetGlibLogLevelString(log_level), message, extra);
	}
	else {
		TALogMsgFormat(LOGT::GLIB, "%s: %s%s", GetGlibLogLevelString(log_level), message, extra);
	}
}

void InitGlibLogger() {
	g_log_set_default_handler(GlibLogHandler, nullptr);
}

void DeInitGlibLogger() {
	g_log_set_default_handler(g_log_default_handler, nullptr);
}

#endif
