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

#ifndef HGUARD_SRC_LOG_IMPL
#define HGUARD_SRC_LOG_IMPL

#include "univdefs.h"
#include "log.h"
#include <wx/frame.h>
#include <wx/event.h>
#include <queue>

struct tpanelparentwin_nt;
struct taccount;
struct tweet;

struct log_object {
	logflagtype lo_flags;
	virtual void log_str(logflagtype logflags, const wxString &str) = 0;
	log_object(logflagtype flagmask);
	virtual ~log_object();
};

struct log_window : public log_object, public wxFrame {
	std::queue<wxString> pending;
	bool isshown;
	wxTextCtrl *txtct;

	void log_str(logflagtype logflags, const wxString &str);
	log_window(wxWindow *parent, logflagtype flagmask, bool show = true);
	~log_window();
	void LWShow(bool shown=true);
	void OnFrameClose(wxCloseEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnClear(wxCommandEvent &event);
	void OnClose(wxCommandEvent &event);
	void OnDumpPending(wxCommandEvent &event);
	void OnDumpTPanelWins(wxCommandEvent &event);
	void OnDumpConnInfo(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

struct log_file : public log_object {
	FILE *fp;
	bool closefpondel;
	log_file(logflagtype flagmask, const char *filename);
	log_file(logflagtype flagmask, FILE *fp_, bool closefpondel_ = false);
	~log_file();
	void log_str(logflagtype logflags, const wxString &str);
};

struct Redirector_wxLog : public wxLog {
	wxLogLevel last_loglevel;
	virtual void DoLog(wxLogLevel level, const wxChar *msg, time_t timestamp) override;
	virtual void DoLogString(const wxChar *msg, time_t timestamp) override;
};

logflagtype StrToLogFlags(const wxString &str);

extern log_window *globallogwindow;

wxString tweet_log_line(const tweet *t);
void dump_pending_acc(logflagtype logflags, const wxString &indent, const wxString &indentstep, taccount *acc);
void dump_tweet_pendings(logflagtype logflags, const wxString &indent, const wxString &indentstep);
void dump_tpanel_scrollwin_data(logflagtype logflags, const wxString &indent, const wxString &indentstep, tpanelparentwin_nt *tppw);
void dump_pending_acc_failed_conns(logflagtype logflags, const wxString &indent, const wxString &indentstep, taccount *acc);
void dump_pending_retry_conn(logflagtype logflags, const wxString &indent, const wxString &indentstep);
void dump_pending_active_conn(logflagtype logflags, const wxString &indent, const wxString &indentstep);
void dump_acc_socket_flags(logflagtype logflags, const wxString &indent, taccount *acc);

void InitWxLogger();
void DeInitWxLogger();

#endif
