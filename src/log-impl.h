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

#ifndef HGUARD_SRC_LOG_IMPL
#define HGUARD_SRC_LOG_IMPL

#include "univdefs.h"
#include "log.h"
#include "flags.h"
#include <wx/frame.h>
#include <wx/event.h>
#include <queue>

struct tpanelparentwin_nt;
struct taccount;
struct tweet;

enum class LOGIMPLF : unsigned int {
	FFLUSH                = 1<<0,
};
template<> struct enum_traits<LOGIMPLF> { static constexpr bool flags = true; };
extern flagwrapper<LOGIMPLF> logimpl_flags;

struct log_object {
	LOGT lo_flags;
	virtual void log_str(LOGT logflags, const wxString &str) = 0;
	log_object(LOGT flagmask);
	virtual ~log_object();
};

struct log_window : public log_object, public wxFrame {
	std::queue<wxString> pending;
	bool isshown;
	wxTextCtrl *txtct;

	void log_str(LOGT logflags, const wxString &str);
	log_window(wxWindow *parent, LOGT flagmask, bool show = true);
	~log_window();
	void LWShow(bool shown=true);
	void OnFrameClose(wxCloseEvent &event);
	void OnSave(wxCommandEvent &event);
	void OnClear(wxCommandEvent &event);
	void OnClose(wxCommandEvent &event);
	void OnDumpPending(wxCommandEvent &event);
	void OnDumpTPanelWins(wxCommandEvent &event);
	void OnDumpConnInfo(wxCommandEvent &event);
	void OnFlushState(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

struct log_file : public log_object {
	FILE *fp;
	bool closefpondel;
	log_file(LOGT flagmask, const char *filename);
	log_file(LOGT flagmask, FILE *fp_, bool closefpondel_ = false);
	~log_file();
	void log_str(LOGT logflags, const wxString &str);
};

struct Redirector_wxLog : public wxLog {
	wxLogLevel last_loglevel;
	virtual void DoLog(wxLogLevel level, const wxChar *msg, time_t timestamp) override;
	virtual void DoLogString(const wxChar *msg, time_t timestamp) override;
};

LOGT StrToLogFlags(const wxString &str);

extern log_window *globallogwindow;

void InitWxLogger();
void DeInitWxLogger();

#endif
