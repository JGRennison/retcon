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

#ifndef HGUARD_SRC_SOCKET
#define HGUARD_SRC_SOCKET

#include "univdefs.h"
#include "flags.h"
#include "util.h"
#include <wx/timer.h>
#include <wx/defs.h>
#include <wx/version.h>
#include <curl/curl.h>
#include <memory>
#include <map>
#include <forward_list>
#include <set>
#include <tuple>
#include <deque>
#include <utility>
#include <vector>

struct socketmanager;
struct userdatacontainer;
struct twitcurlext;

extern socketmanager sm;

#if !(defined(RCS_GTKSOCKMODE) || defined(RCS_WSAASYNCSELMODE) || defined(RCS_POLLTHREADMODE))
	#if defined(__WXGTK__)
		#define RCS_GTKSOCKMODE
	#elif defined(__WINDOWS__)
		#define RCS_WSAASYNCSELMODE
	#else
		#define RCS_POLLTHREADMODE
	#endif
#endif

#ifdef RCS_GTKSOCKMODE
	#include <glib.h>
#endif

#include <tuple>

//arrange in order of increasing severity
typedef enum {
	MCC_RETRY = 0,
	MCC_FAILED,
} MCC_HTTPERRTYPE;

enum {
	MCCT_RETRY = wxID_HIGHEST + 1,
};

struct mcurlconn : public wxEvtHandler {
	enum class MCF {
		NOTIMEOUT             = 1<<0,
		IN_RETRY_QUEUE        = 1<<1,
		RETRY_NOW_ON_SUCCESS  = 1<<2,
	};

	unsigned int errorcount = 0;
	flagwrapper<MCF> mcflags;
	std::string url;
	unsigned int id;

	static unsigned int lastid;

	// Functions taking a std::unique_ptr<mcurlconn>&& are entitled but not required to claim the unique_ptr being referenced.
	// This is to ensure that an mcurlconn only ever has one owner/won't leak, and that it's lifetime can be extended
	// and owner chnaged as necessary, eg. for connection retries, async DNS etc.
	// Generally if not claimed the mcurlconn will be destructed shortly after.

	void NotifyDone(CURL *easy, long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner);
	void HandleError(CURL *easy, long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner);
	void KillConn();
	std::unique_ptr<mcurlconn> RemoveConn();
	mcurlconn() {
		id = ++lastid;
	}
	virtual ~mcurlconn() { }

	virtual void NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) = 0;
	virtual void DoRetry(std::unique_ptr<mcurlconn> &&this_owner) = 0;
	virtual void HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) = 0;
	virtual void AddToRetryQueueNotify() { }
	virtual void RemoveFromRetryQueueNotify() { }
	virtual MCC_HTTPERRTYPE CheckHTTPErrType(long httpcode);
	virtual CURL *GenGetCurlHandle() = 0;
	virtual std::string GetConnTypeName() { return ""; }

	DECLARE_EVENT_TABLE()

	private:
	std::unique_ptr<mcurlconn> RemoveConnCommon(const char *logprefix);
};
template<> struct enum_traits<mcurlconn::MCF> { static constexpr bool flags = true; };

struct sockettimeout : public wxTimer {
	socketmanager &sm;
	sockettimeout(socketmanager &sm_) : sm(sm_) {};
	void Notify();
};

#ifdef RCS_POLLTHREADMODE

typedef enum {
	SPM_FDCHANGE = 1,
	SPM_ENABLE,
	SPM_QUIT,
} SPM_TYPE;

struct socketpollmessage {
	SPM_TYPE type;
	int fd;
	int events;
};

struct socketpollthread : public wxThread {
	int pipefd;

	socketpollthread() : wxThread(wxTHREAD_DETACHED) { }
	wxThread::ExitCode Entry();
};

DECLARE_EVENT_TYPE(wxextSOCK_NOTIFY, -1)

struct wxextSocketNotifyEvent : public wxEvent {
	wxextSocketNotifyEvent(int id = 0);
	wxextSocketNotifyEvent(const wxextSocketNotifyEvent &src);
	wxEvent *Clone() const;

	int fd;
	int curlbitmask;
	bool reenable;
};

typedef void (wxEvtHandler::*wxextSocketNotifyEventFunction)(wxextSocketNotifyEvent&);

#define EVT_EXTSOCKETNOTIFY(id, fn) \
    DECLARE_EVENT_TABLE_ENTRY( wxextSOCK_NOTIFY, id, -1, \
    (wxObjectEventFunction) (wxEventFunction) \
    wxStaticCastEvent( wxextSocketNotifyEventFunction, & fn ), (wxObject *) NULL ),

#endif

DECLARE_EVENT_TYPE(wxextDNS_RESOLUTION_EVENT, -1)

struct adns;

struct socketmanager : public wxEvtHandler {
	socketmanager();
	~socketmanager();
	bool AddConn(CURL* ch, std::unique_ptr<mcurlconn> cs);
	bool AddConn(std::unique_ptr<twitcurlext> cs);
	std::unique_ptr<mcurlconn> RemoveConn(CURL *ch);
	void RegisterSockInterest(CURL *e, curl_socket_t s, int what);
	void NotifySockEvent(curl_socket_t sockfd, int ev_bitmask);
#ifdef RCS_POLLTHREADMODE
	void NotifySockEventCmd(wxextSocketNotifyEvent &event);
#endif
	void InitMultiIOHandler();
	void DeInitMultiIOHandler();
	void InitMultiIOHandlerCommon();
	void DeInitMultiIOHandlerCommon();
	void RetryConn(std::unique_ptr<mcurlconn> cs);
	void RetryConnNow();
	void RetryConnLater();
	void RetryNotify(wxTimerEvent& event);
	std::unique_ptr<mcurlconn> UnregisterRetryConn(mcurlconn &cs);

	bool MultiIOHandlerInited = false;
	CURLM *curlmulti = nullptr;
	std::unique_ptr<sockettimeout> st;
	int curnumsocks = 0;
#ifdef RCS_WSAASYNCSELMODE
	HWND wind;
#endif
#ifdef RCS_POLLTHREADMODE
	int pipefd = -1;
#endif
#ifdef RCS_GTKSOCKMODE
	GSource *gs;
	unsigned int source_id;
	std::map<curl_socket_t,GPollFD> sockpollmap;
#endif

	struct conninfo {
		CURL* ch;
		std::unique_ptr<mcurlconn> cs;
	};
	std::vector<conninfo> connlist;
	std::deque<std::unique_ptr<mcurlconn>> retry_conns;
	std::unique_ptr<wxTimer> retry;

	std::unique_ptr<adns> asyncdns;
	void DNSResolutionEvent(wxCommandEvent &event);

	template<typename F> static void IterateConns(F func) {
		for(auto &it : sm.connlist) {
			if(it.cs) {
				if(func(*(it.cs))) return;
			}
		};
		for(auto &it : sm.retry_conns) {
			if(it) {
				if(func(*it)) return;
			}
		};
	}

	DECLARE_EVENT_TABLE()
};

struct adns_thread : public wxThread {
	std::string url;
	std::string hostname;
	bool success = false;
	CURLcode result = CURLE_OK;
	socketmanager *sm = nullptr;
	CURL *eh = nullptr;
	double lookuptime = 0.0;

	adns_thread(std::string url_, std::string hostname_, socketmanager *sm_, CURLSH *sharehndl);
	wxThread::ExitCode Entry();
};

struct adns {
	adns(socketmanager *sm_);
	~adns();
	inline CURLSH *GetHndl() const { return sharehndl; }
	bool CheckAsync(CURL *ch, std::unique_ptr<mcurlconn> &&cs);
	void DNSResolutionEvent(wxCommandEvent &event);

	void Lock(CURL *handle, curl_lock_data data, curl_lock_access access);
	void Unlock(CURL *handle, curl_lock_data data);

	private:
	void NewShareHndl();
	void RemoveShareHndl();
	std::set<std::string> cached_names;

	struct dns_pending_conn {
		std::string hostname;
		CURL* ch;
		std::unique_ptr<mcurlconn> cs;
	};
	std::forward_list<dns_pending_conn> dns_pending_conns;
	std::forward_list<std::pair<std::string, adns_thread> > dns_threads;

	CURLSH *sharehndl = nullptr;
	wxMutex mutex;
	socketmanager *sm;
};

void SetCurlHandleVerboseState(CURL *easy, bool verbose);

#endif
