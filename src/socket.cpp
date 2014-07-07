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
////  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "socket.h"
#include "log.h"
#include "cfg.h"
#include "util.h"
#include <wx/event.h>
#include <algorithm>

#ifdef RCS_WSAASYNCSELMODE
	#include <windows.h>
#endif
#ifdef RCS_POLLTHREADMODE
	#include <poll.h>
#endif
#ifdef RCS_GTKSOCKMODE
	#include <glib.h>
#endif

socketmanager sm;

BEGIN_EVENT_TABLE( mcurlconn, wxEvtHandler )
END_EVENT_TABLE()

int curl_debug_func(CURL *cl, curl_infotype ci, char *txt, size_t len, void *extra) {
	if(ci == CURLINFO_TEXT) {
		LogMsgProcess(LOGT::CURLVERB, std::string(txt, len));
	}
	return 0;
}

void SetCurlHandleVerboseState(CURL *easy, bool verbose) {
	if(verbose) curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, &curl_debug_func);
	curl_easy_setopt(easy, CURLOPT_VERBOSE, verbose);
}

void mcurlconn::KillConn() {
	RemoveConnCommon("KillConn");
}

std::unique_ptr<mcurlconn> mcurlconn::RemoveConn() {
	return RemoveConnCommon("RemoveConn");
}

// This checks both the retry list and the active list for removal
std::unique_ptr<mcurlconn> mcurlconn::RemoveConnCommon(const char *logprefix) {
	std::unique_ptr<mcurlconn> conn = sm.UnregisterRetryConn(*this);
	if(conn) {
		LogMsgFormat(LOGT::SOCKTRACE, "%s (retry list): conn ID: %d", logprefix, id);
		return std::move(conn);
	}

	conn = sm.RemoveConn(GenGetCurlHandle());
	if(conn) {
		LogMsgFormat(LOGT::SOCKTRACE, "%s: conn ID: %d", logprefix, id);
		return std::move(conn);
	}

	LogMsgFormat(LOGT::SOCKERR, "%s: failed, conn ID: %d", logprefix, id);
	return nullptr;
}

void mcurlconn::NotifyDone(CURL *easy, long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	if(httpcode != 200 || res != CURLE_OK) {
		//failed
		if(res == CURLE_OK) {
			char *req_url;
			curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &req_url);
			LogMsgFormat(LOGT::SOCKERR, "Request failed: type: %s, conn ID: %d, code: %d, url: %s", cstr(GetConnTypeName()), id, httpcode, cstr(req_url));
		}
		else {
			LogMsgFormat(LOGT::SOCKERR, "Socket error: type: %s, conn ID: %d, code: %d, message: %s", cstr(GetConnTypeName()), id, res, cstr(curl_easy_strerror(res)));
		}
		HandleError(easy, httpcode, res, std::move(this_owner));    //this may re-add the connection
	}
	else {
		if(mcflags & MCF::RETRY_NOW_ON_SUCCESS) {
			mcflags &= ~MCF::RETRY_NOW_ON_SUCCESS;
			sm.RetryConnNow();
		}
		errorcount = 0;
		NotifyDoneSuccess(easy, res, std::move(this_owner));
	}
}

void mcurlconn::HandleError(CURL *easy, long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	errorcount++;
	MCC_HTTPERRTYPE err = MCC_RETRY;
	if(res == CURLE_OK) {	//http error, not socket error
		err = CheckHTTPErrType(httpcode);
	}
	if(errorcount >= 3 && err < MCC_FAILED) {
		err = MCC_FAILED;
	}
	switch(err) {
		case MCC_RETRY:
			LogMsgFormat(LOGT::SOCKERR, "Adding request to retry queue: type: %s, conn ID: %d, url: %s", cstr(GetConnTypeName()), id, cstr(url));
			sm.RetryConn(std::move(this_owner));
			break;
		case MCC_FAILED:
			LogMsgFormat(LOGT::SOCKERR, "Calling failure handler: type: %s, conn ID: %d, url: %s", cstr(GetConnTypeName()), id, cstr(url));
			HandleFailure(httpcode, res, std::move(this_owner));
			break;
	}
}

MCC_HTTPERRTYPE mcurlconn::CheckHTTPErrType(long httpcode) {
	if(httpcode >= 400 && httpcode < 420) return MCC_FAILED;
	return MCC_RETRY;
}

unsigned int mcurlconn::lastid = 0;

static void check_multi_info(socketmanager *smp) {
	int msgs_left;
	while(CURLMsg *msg = curl_multi_info_read(smp->curlmulti, &msgs_left)) {
		if(msg->msg == CURLMSG_DONE) {
			CURL *easy = msg->easy_handle;
			CURLcode res = msg->data.result;

			long httpcode;
			curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpcode);

			// This gets the mcurlconn associated with the easy handle, and removes it from the socketmanager
			// We then hold onto it here, it will be destructed when the scope ends unless NotifyDone claims it
			std::unique_ptr<mcurlconn> cs = sm.RemoveConn(easy);

			if(cs) {
				LogMsgFormat(LOGT::SOCKTRACE, "Socket Done, conn ID: %d, res: %d, http: %d", cs->id, res, httpcode);
				cs->NotifyDone(easy, httpcode, res, std::move(cs));
			}
			else {
				// Getting here is a severe bug
				mcurlconn *conn;
				curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
				LogMsgFormat(LOGT::SOCKERR, "Socket Done, yet could not be found in connection list, conn ID: %d, res: %d, http: %d", conn->id, res, httpcode);
			}
		}
	}
	if(sm.curnumsocks == 0) {
		LogMsgFormat(LOGT::SOCKTRACE, "No Sockets Left, Stopping Timer");
		smp->st->Stop();
	}
}

static int sock_cb(CURL *e, curl_socket_t s, int what, socketmanager *smp, mcurlconn *cs) {
	LogMsgFormat(LOGT::SOCKTRACE, "Socket Interest Change Callback: %d, %d, conn ID: %d", s, what, cs ? cs->id : -1);
	if(what != CURL_POLL_REMOVE) {
		if(!cs) {
			curl_easy_getinfo(e, CURLINFO_PRIVATE, &cs);
			curl_multi_assign(smp->curlmulti, s, cs);
		}
	}
	smp->RegisterSockInterest(e, s, what);
	return 0;
}

static int multi_timer_cb(CURLM *multi, long timeout_ms, socketmanager *smp) {
	LogMsgFormat(LOGT::SOCKTRACE, "Socket Timer Callback: %d ms", timeout_ms);

	if(timeout_ms>0) smp->st->Start(timeout_ms,wxTIMER_ONE_SHOT);
	else smp->st->Stop();
	if(!timeout_ms) smp->st->Notify();

	return 0;
}

socketmanager::socketmanager() { }

socketmanager::~socketmanager() {
	DeInitMultiIOHandler();
}

curl_socket_t pre_connect_func(void *clientp, curl_socket_t curlfd, curlsocktype purpose) {
	mcurlconn *cs = static_cast<mcurlconn *>(clientp);
	double lookuptime;
	curl_easy_getinfo(cs->GenGetCurlHandle(), CURLINFO_NAMELOOKUP_TIME, &lookuptime);
	LogMsgFormat(LOGT::SOCKTRACE, "DNS lookup took: %fs. Request: type: %s, conn ID: %d, url: %s", lookuptime, cstr(cs->GetConnTypeName()), cs->id, cstr(cs->url));
	return 0;
}

bool socketmanager::AddConn(CURL* ch, std::unique_ptr<mcurlconn> cs) {
	if(asyncdns) {
		// This can conditionally steal cs, if it does it returns true and we stop here
		if(asyncdns->CheckAsync(ch, std::move(cs))) return true;
	}

	SetCurlHandleVerboseState(ch, currentlogflags & LOGT::CURLVERB);
	curl_easy_setopt(ch, CURLOPT_TIMEOUT, (cs->mcflags & mcurlconn::MCF::NOTIMEOUT) ? 0 : 180);
	curl_easy_setopt(ch, CURLOPT_PRIVATE, cs.get());
	curl_easy_setopt(ch, CURLOPT_ACCEPT_ENCODING, ""); //accept all enabled encodings
	if(currentlogflags & LOGT::SOCKTRACE) {
		curl_easy_setopt(ch, CURLOPT_SOCKOPTFUNCTION, &pre_connect_func);
		curl_easy_setopt(ch, CURLOPT_SOCKOPTDATA, cs.get());
	}
	if(gc.setproxy) {
		curl_easy_setopt(ch, CURLOPT_PROXY, gc.proxyurl.c_str());
		curl_easy_setopt(ch, CURLOPT_NOPROXY, gc.noproxylist.c_str());
		curl_easy_setopt(ch, CURLOPT_HTTPPROXYTUNNEL, gc.proxyhttptunnel ? 1 : 0);
	}
	else {
		curl_easy_setopt(ch, CURLOPT_PROXY, nullptr);
		curl_easy_setopt(ch, CURLOPT_NOPROXY, nullptr);
		curl_easy_setopt(ch, CURLOPT_HTTPPROXYTUNNEL, 0);
	}
	if(!gc.netiface.empty()) {
		curl_easy_setopt(ch, CURLOPT_INTERFACE, gc.netiface.c_str());
	}
	else {
		curl_easy_setopt(ch, CURLOPT_INTERFACE, nullptr);
	}

	connlist.push_back({ ch, std::move(cs) });

	bool ret = (CURLM_OK == curl_multi_add_handle(curlmulti, ch));
	curl_multi_socket_action(curlmulti, 0, 0, &curnumsocks);
	check_multi_info(&sm);
	return ret;
}

// This removes the corresponding connection from the multi IO and the socketmanager's connection list
// This returns the existing mcurlconn that was removed
// It's OK to not do anything with the return value, it'll just get destructed as normal
std::unique_ptr<mcurlconn> socketmanager::RemoveConn(CURL *ch) {
	std::unique_ptr<mcurlconn> conn;
	curl_multi_remove_handle(curlmulti, ch);
	container_unordered_remove_if(connlist, [&](conninfo &p) {
		if(p.ch == ch) {
			if(p.cs) conn = std::move(p.cs);
			return true;
		}
		else return false;
	});
	curl_multi_socket_action(curlmulti, 0, 0, &curnumsocks);
	return std::move(conn);
}

void sockettimeout::Notify() {
	LogMsgFormat(LOGT::SOCKTRACE, "Socket Timer Event");
	curl_multi_socket_action(sm.curlmulti, CURL_SOCKET_TIMEOUT, 0, &sm.curnumsocks);
	check_multi_info(&sm);
}

void socketmanager::NotifySockEvent(curl_socket_t sockfd, int ev_bitmask) {
	LogMsgFormat(LOGT::SOCKTRACE, "Socket Notify (%d)", sockfd);
	curl_multi_socket_action(curlmulti, sockfd, ev_bitmask, &curnumsocks);
	check_multi_info(this);
}

adns::adns(socketmanager *sm_) : sm(sm_) {
	NewShareHndl();
}

adns::~adns() {
	size_t n = std::distance(dns_threads.begin(), dns_threads.end());
	if(n) {
		size_t i = 0;
		for(auto &it : dns_threads) {
			LogMsgFormat(LOGT::SOCKTRACE, "Waiting for all DNS threads to terminate: %d of %d", i, n);
			it.second.Wait();
			i++;
		}
		LogMsg(LOGT::SOCKTRACE, "All DNS threads terminated");
	}
	RemoveShareHndl();
}

void adns::Lock(CURL *handle, curl_lock_data data, curl_lock_access access) {
	mutex.Lock();
}

void adns::Unlock(CURL *handle, curl_lock_data data) {
	mutex.Unlock();
}

void adns_lock_function(CURL *handle, curl_lock_data data, curl_lock_access access, void *userptr) {
	adns *a = static_cast<adns*>(userptr);
	a->Lock(handle, data, access);
}

void adns_unlock_function(CURL *handle, curl_lock_data data, void *userptr) {
	adns *a = static_cast<adns*>(userptr);
	a->Unlock(handle, data);
}

void adns::NewShareHndl() {
	RemoveShareHndl();
	sharehndl = curl_share_init();
	curl_share_setopt(sharehndl, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
	curl_share_setopt(sharehndl, CURLSHOPT_USERDATA, this);
	curl_share_setopt(sharehndl, CURLSHOPT_LOCKFUNC, adns_lock_function);
	curl_share_setopt(sharehndl, CURLSHOPT_UNLOCKFUNC, adns_unlock_function);
}

void adns::RemoveShareHndl() {
	if(sharehndl) {
		curl_share_cleanup(sharehndl);
		sharehndl = 0;
	}
}

//return true if has been handled asynchronously
bool adns::CheckAsync(CURL *ch, std::unique_ptr<mcurlconn> &&cs) {
	long timeout = -1;
	curl_easy_setopt(ch, CURLOPT_DNS_CACHE_TIMEOUT, timeout);
	curl_easy_setopt(ch, CURLOPT_SHARE, GetHndl());

	const std::string &url = cs->url;
	if(url.empty()) return false;

	std::string::size_type proto_start = url.find("://");
	std::string::size_type name_start;
	if(proto_start == std::string::npos) name_start = 0;
	else name_start = proto_start + 3;
	std::string::size_type name_end = url.find('/', name_start);

	std::string name = url.substr(name_start, (name_end == std::string::npos) ? std::string::npos : name_end - name_start);

	if(cached_names.count(name)) return false;	// name already in cache, can go now

	dns_pending_conns.push_front({ name, ch, std::move(cs) });
	// cs is now null, don't use again

	for(auto &it : dns_threads) {
		if(it.first == name) {
			LogMsgFormat(LOGT::SOCKTRACE, "DNS lookup thread already exists: %s, %s", cstr(url), cstr(name));
			return true;
		}
	}

	LogMsgFormat(LOGT::SOCKTRACE, "Creating DNS lookup thread: %s, %s", cstr(url), cstr(name));
	dns_threads.emplace_front(std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(url, name, sm, GetHndl()));
	adns_thread &ad = dns_threads.front().second;
	ad.Create();
#if defined(_GNU_SOURCE)
#if __GLIBC_PREREQ(2, 12)
	pthread_setname_np(ad.GetId(), "retcon-adns");
#endif
#endif
	ad.Run();

	return true;
}

void adns::DNSResolutionEvent(wxCommandEvent &event) {
	adns_thread *at = static_cast<adns_thread*>(event.GetClientData());
	if(!at) return;

	at->Wait();

	if(at->success) {
		LogMsgFormat(LOGT::SOCKTRACE, "Asynchronous DNS lookup succeeded: %s, %s, time: %fs", cstr(at->hostname), cstr(at->url), at->lookuptime);
		cached_names.insert(at->hostname);
	}
	else {
		LogMsgFormat(LOGT::SOCKERR, "Asynchronous DNS lookup failed: %s, (%s), error: %s (%d), time: %fs", cstr(at->hostname), cstr(at->url), cstr(curl_easy_strerror(at->result)), at->result, at->lookuptime);
	}

	std::vector<dns_pending_conn> current_dns_pending_conns;
	dns_pending_conns.remove_if([&](dns_pending_conn &a) -> bool {
		if(a.hostname == at->hostname) {
			current_dns_pending_conns.emplace_back(std::move(a));
			return true;
		}
		else return false;
	});

	dns_threads.remove_if([&](const std::pair<std::string, adns_thread> &a) {
		return at == &(a.second);
	});

	for(auto &it : current_dns_pending_conns) {
		CURL *ch = it.ch;
		std::unique_ptr<mcurlconn> mc = std::move(it.cs);
		if(at->success) {
			LogMsgFormat(LOGT::SOCKTRACE, "Launching request as DNS lookup succeeded: type: %s, conn ID: %d, url: %s", cstr(mc->GetConnTypeName()), mc->id, cstr(mc->url));
			sm->AddConn(ch, std::move(mc));
		}
		else {
			LogMsgFormat(LOGT::SOCKERR, "Request failed due to asynchronous DNS lookup failure: type: %s, conn ID: %d, url: %s", cstr(mc->GetConnTypeName()), mc->id, cstr(mc->url));
			mc->HandleError(ch, 0, CURLE_COULDNT_RESOLVE_HOST, std::move(mc));
		}
	}
}

curl_socket_t stub_socket_func(void *clientp, curlsocktype purpose, struct curl_sockaddr *address) {
	return CURL_SOCKET_BAD;
}

adns_thread::adns_thread(std::string url_, std::string hostname_, socketmanager *sm_, CURLSH *sharehndl)
	: wxThread(wxTHREAD_JOINABLE), url(url_.c_str(), url_.length()), hostname(hostname_.c_str(), hostname_.length()), sm(sm_) {
	eh = curl_easy_init();
	long val = 1;
	curl_easy_setopt(eh, CURLOPT_URL, url_.c_str());
	curl_easy_setopt(eh, CURLOPT_SHARE, sharehndl);
	curl_easy_setopt(eh, CURLOPT_NOSIGNAL, val);
	curl_easy_setopt(eh, CURLOPT_OPENSOCKETFUNCTION, &stub_socket_func);
}

wxThread::ExitCode adns_thread::Entry() {
	result = curl_easy_perform(eh);
	curl_easy_getinfo(eh, CURLINFO_NAMELOOKUP_TIME, &lookuptime);
	curl_easy_cleanup(eh);
	if(result == CURLE_COULDNT_CONNECT) {
		success = true;
	}
	wxCommandEvent ev(wxextDNS_RESOLUTION_EVENT);
	ev.SetClientData(this);
	sm->AddPendingEvent(ev);
	return 0;
}

BEGIN_EVENT_TABLE(socketmanager, wxEvtHandler)
	EVT_TIMER(MCCT_RETRY, socketmanager::RetryNotify)
#ifdef RCS_POLLTHREADMODE
	EVT_EXTSOCKETNOTIFY(wxID_ANY, socketmanager::NotifySockEventCmd)
#endif
	EVT_COMMAND(wxID_ANY, wxextDNS_RESOLUTION_EVENT, socketmanager::DNSResolutionEvent)
END_EVENT_TABLE()

DEFINE_EVENT_TYPE(wxextDNS_RESOLUTION_EVENT)

void socketmanager::InitMultiIOHandlerCommon() {
	LogMsg(LOGT::SOCKTRACE, "socketmanager::InitMultiIOHandlerCommon");
	st.reset(new sockettimeout(*this));
	curlmulti=curl_multi_init();
	curl_multi_setopt(curlmulti, CURLMOPT_SOCKETFUNCTION, sock_cb);
	curl_multi_setopt(curlmulti, CURLMOPT_SOCKETDATA, this);
	curl_multi_setopt(curlmulti, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
	curl_multi_setopt(curlmulti, CURLMOPT_TIMERDATA, this);

	curl_version_info_data *data = curl_version_info(CURLVERSION_NOW);
	if(!(data->features & CURL_VERSION_ASYNCHDNS)) {
		LogMsg(LOGT::SOCKTRACE, "This version of libcurl does not support asynchronous DNS resolution, using a workaround.");
		asyncdns.reset(new adns(this));
	}
}

void socketmanager::DeInitMultiIOHandlerCommon() {
	LogMsg(LOGT::SOCKTRACE, "socketmanager::DeInitMultiIOHandlerCommon");
	if(asyncdns) asyncdns.reset();
	curl_multi_cleanup(curlmulti);
}

void socketmanager::RetryConn(std::unique_ptr<mcurlconn> cs) {
	if(cs->mcflags & mcurlconn::MCF::IN_RETRY_QUEUE) {
		LogMsgFormat(LOGT::SOCKERR, "socketmanager::RetryConn: Attempt to add mcurlconn to retry queue which is marked as already in queue, this is a bug: type: %s, conn ID: %d, url: %s",
				cstr(cs->GetConnTypeName()), cs->id, cstr(cs->url));
		return;
	}

	cs->mcflags |= mcurlconn::MCF::IN_RETRY_QUEUE;
	cs->AddToRetryQueueNotify();
	retry_conns.push_back(std::move(cs));
	RetryConnLater();
}

std::unique_ptr<mcurlconn> socketmanager::UnregisterRetryConn(mcurlconn &cs) {
	if(!(cs.mcflags & mcurlconn::MCF::IN_RETRY_QUEUE)) return nullptr;

	std::unique_ptr<mcurlconn> csptr;
	container_unordered_remove_if(retry_conns, [&](std::unique_ptr<mcurlconn> &it) {
		if(it.get() == &cs) {
			LogMsgFormat(LOGT::SOCKTRACE, "socketmanager::UnregisterRetryConn: Unregistered from retry queue: type: %s, conn ID: %d, url: %s", cstr(cs.GetConnTypeName()), cs.id, cstr(cs.url));
			csptr = std::move(it);
			csptr->mcflags &= ~mcurlconn::MCF::IN_RETRY_QUEUE;
			csptr->RemoveFromRetryQueueNotify();
			return true;
		}
		else return false;
	});
	return std::move(csptr);
}

void socketmanager::RetryConnNow() {
	std::unique_ptr<mcurlconn> cs;
	while(true) {
		if(retry_conns.empty()) return;
		cs = std::move(retry_conns.front());
		retry_conns.pop_front();
		if(cs) break;
	}
	cs->mcflags &= ~mcurlconn::MCF::IN_RETRY_QUEUE;
	cs->mcflags |= mcurlconn::MCF::RETRY_NOW_ON_SUCCESS;
	LogMsgFormat(LOGT::SOCKTRACE, "Dequeueing request from retry queue: type: %s, conn ID: %d, url: %s", cstr(cs->GetConnTypeName()), cs->id, cstr(cs->url));
	cs->RemoveFromRetryQueueNotify();
	cs->DoRetry(std::move(cs));
}

void socketmanager::RetryConnLater() {

	// This gets the first non-null connection without removing it
	mcurlconn *cs = nullptr;
	while(true) {
		if(retry_conns.empty()) return;
		cs = retry_conns.front().get();
		if(cs) break;
		else retry_conns.pop_front();
	}

	if(!retry) retry.reset(new wxTimer(this, MCCT_RETRY));
	if(!retry->IsRunning()) {
		uint64_t ms = 5000 * (1 << cs->errorcount);
		ms += (ms * ((uint64_t) rand())) / RAND_MAX;
		retry->Start((int) ms, wxTIMER_ONE_SHOT);
	}
}

void socketmanager::RetryNotify(wxTimerEvent& event) {
	RetryConnNow();
}

void socketmanager::DNSResolutionEvent(wxCommandEvent &event) {
	if(asyncdns) asyncdns->DNSResolutionEvent(event);
}

#ifdef RCS_WSAASYNCSELMODE

const char *tclassname="____retcon_wsaasyncselect_window";

LRESULT CALLBACK wndproc(
		HWND hwnd,	// handle of window
		UINT uMsg,	// message identifier
		WPARAM wParam,	// first message parameter
		LPARAM lParam 	// second message parameter
		) {
	if(uMsg == WM_USER) {
		int sendbitmask;
		switch(lParam) {
			case FD_READ:
				sendbitmask = CURL_CSELECT_IN;
				break;
			case FD_WRITE:
				sendbitmask = CURL_CSELECT_OUT;
				break;
			default:
				sendbitmask = 0;
				break;
		}
		sm.NotifySockEvent((curl_socket_t) wParam, sendbitmask);
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void socketmanager::InitMultiIOHandler() {
	if(MultiIOHandlerInited) return;
	InitMultiIOHandlerCommon();
	WNDCLASSA wc = {
		0, &wndproc, 0, 0,
		(HINSTANCE) GetModuleHandle(0),
		0, 0, 0, 0,
		tclassname
	};
	RegisterClassA(&wc);
	wind = CreateWindowA(tclassname, tclassname, 0, 0, 0, 0, 0, 0, 0, (HINSTANCE) GetModuleHandle(0), 0);
	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	if(!MultiIOHandlerInited) return;
	DestroyWindow(wind);
	wind = 0;
	DeInitMultiIOHandlerCommon();
	MultiIOHandlerInited=false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	long lEvent;
	const long common = FD_OOB | FD_ACCEPT | FD_CONNECT | FD_CLOSE;
	switch(what) {
		case CURL_POLL_NONE:
		case CURL_POLL_REMOVE:
		default:
			lEvent = 0;
			break;
		case CURL_POLL_IN:
			lEvent = FD_READ | common;
			break;
		case CURL_POLL_OUT:
			lEvent = FD_WRITE | common;
			break;
		case CURL_POLL_INOUT:
			lEvent = FD_READ | FD_WRITE | common;
			break;
	}
	WSAAsyncSelect(s, wind, WM_USER, lEvent);
}

#endif

#ifdef RCS_GTKSOCKMODE

struct sock_gsource : public GSource {
	socketmanager *sm;
};

gboolean gs_prepare(GSource *source, gint *timeout) {
	*timeout=-1;
	return false;
}

gboolean gs_check(GSource *source) {
	sock_gsource *gs = (sock_gsource*) source;
	for(auto &it : gs->sm->sockpollmap) {
		if(it.second.revents) return true;
	}
	return false;
}

gboolean gs_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
	sock_gsource *gs = (sock_gsource*) source;
	std::forward_list<std::pair<curl_socket_t,int>> actlist;
	for(auto &it : gs->sm->sockpollmap) {
		if(it.second.revents) {
			int sendbitmask = 0;
			if(it.second.revents&(G_IO_IN|G_IO_PRI)) sendbitmask |= CURL_CSELECT_IN;
			if(it.second.revents&G_IO_OUT) sendbitmask |= CURL_CSELECT_OUT;
			if(it.second.revents&(G_IO_ERR|G_IO_HUP)) sendbitmask |= CURL_CSELECT_ERR;
			actlist.push_front(std::make_pair(it.first, sendbitmask));    //NotifySockEvent is entitled to modify the set of monitored sockets
		}
	}
	for(auto &it : actlist) {
		gs->sm->NotifySockEvent(it.first, it.second);
	}
	return true;
}

GSourceFuncs gsf;

void socketmanager::InitMultiIOHandler() {
	if(MultiIOHandlerInited) return;
	InitMultiIOHandlerCommon();

	memset(&gsf, 0, sizeof(gsf));
	gsf.prepare = &gs_prepare;
	gsf.check = &gs_check;
	gsf.dispatch = &gs_dispatch;

	sock_gsource *sgs = (sock_gsource *) g_source_new(&gsf, sizeof(sock_gsource));
	sgs->sm = this;
	source_id = g_source_attach(sgs, 0);
	gs = sgs;

	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	if(!MultiIOHandlerInited) return;

	g_source_destroy((sock_gsource*) gs);

	DeInitMultiIOHandlerCommon();
	MultiIOHandlerInited = false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	sock_gsource *sgs = (sock_gsource *) gs;
	short events = 0;
	switch(what) {
		case CURL_POLL_NONE:
		case CURL_POLL_REMOVE:
		default:
			events = 0;
			break;
		case CURL_POLL_IN:
			events = G_IO_IN | G_IO_PRI | G_IO_ERR;
			break;
		case CURL_POLL_OUT:
			events = G_IO_OUT | G_IO_ERR;
			break;
		case CURL_POLL_INOUT:
			events = G_IO_IN | G_IO_PRI | G_IO_OUT | G_IO_ERR;
			break;
	}
	auto egp = sgs->sm->sockpollmap.find(s);
	GPollFD *gpfd = nullptr;
	if(egp != sgs->sm->sockpollmap.end()) {
		gpfd = &(egp->second);
		g_source_remove_poll(sgs, gpfd);
		if(!events) sgs->sm->sockpollmap.erase(egp);
	}
	else if(events) gpfd = &sgs->sm->sockpollmap[s];
	if(events) {
		gpfd->fd = s;
		gpfd->events = events;
		gpfd->revents = 0;
		g_source_add_poll(sgs, gpfd);
	}
}

#endif

#ifdef RCS_POLLTHREADMODE

DEFINE_EVENT_TYPE(wxextSOCK_NOTIFY)

wxextSocketNotifyEvent::wxextSocketNotifyEvent(int id)
		: wxEvent(id, wxextSOCK_NOTIFY) {
	fd = 0;
	reenable = false;
	curlbitmask = 0;
}
wxextSocketNotifyEvent::wxextSocketNotifyEvent(const wxextSocketNotifyEvent &src) : wxEvent(src) {
	fd = src.fd;
	reenable = src.reenable;
	curlbitmask = src.curlbitmask;
}

wxEvent *wxextSocketNotifyEvent::Clone() const {
	return new wxextSocketNotifyEvent(*this);
}

void socketmanager::NotifySockEventCmd(wxextSocketNotifyEvent &event) {
	NotifySockEvent((curl_socket_t) event.fd, event.curlbitmask);
	if(event.reenable) {
		socketpollmessage spm;
		spm.type = SPM_ENABLE;
		spm.fd = event.fd;
		write(pipefd, &spm, sizeof(spm));
	}
}

static void AddPendingEventForSocketPollEvent(int fd, short revents, bool reenable=false) {
	if(!sm.MultiIOHandlerInited) return;

	int sendbitmask = 0;
	if(revents & (POLLIN | POLLPRI)) sendbitmask |= CURL_CSELECT_IN;
	if(revents & POLLOUT) sendbitmask |= CURL_CSELECT_OUT;
	if(revents & (POLLERR|POLLHUP)) sendbitmask |= CURL_CSELECT_ERR;

	wxextSocketNotifyEvent event;
	event.curlbitmask = sendbitmask;
	event.fd = fd;
	event.reenable = reenable;

	sm.AddPendingEvent(event);
}

void socketmanager::InitMultiIOHandler() {
	if(MultiIOHandlerInited) return;
	InitMultiIOHandlerCommon();

	int pipefd[2];
	pipe(pipefd);
	socketpollthread *th = new socketpollthread();
	th->pipefd = pipefd[0];
	this->pipefd = pipefd[1];
	th->Create();
	th->Run();
	LogMsgFormat(LOGT::SOCKTRACE, "socketmanager::InitMultiIOHandler(): Created socket poll() thread: %d", th->GetId());

	MultiIOHandlerInited = true;
}

void socketmanager::DeInitMultiIOHandler() {
	if(!MultiIOHandlerInited) return;

	socketpollmessage spm;
	spm.type = SPM_QUIT;
	write(pipefd, &spm, sizeof(spm));
	close(pipefd);

	DeInitMultiIOHandlerCommon();
	MultiIOHandlerInited = false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	socketpollmessage spm;
	spm.type = SPM_FDCHANGE;
	spm.fd = s;
	short events = 0;
	switch(what) {
		case CURL_POLL_NONE:
		case CURL_POLL_REMOVE:
		default:
			events = 0;
			break;
		case CURL_POLL_IN:
			events = POLLIN | POLLPRI;
			break;
		case CURL_POLL_OUT:
			events = POLLOUT;
			break;
		case CURL_POLL_INOUT:
			events = POLLOUT | POLLIN | POLLPRI;
			break;
	}
	spm.events = events;
	write(pipefd, &spm, sizeof(spm));
}

static struct pollfd *getexistpollsetoffsetfromfd(int fd, std::vector<struct pollfd> &pollset) {
	for(size_t i = 0; i < pollset.size(); i++) {
		if(pollset[i].fd == fd) return &pollset[i];
	}
	return nullptr;
}

static size_t insertatendofpollset(int fd, std::vector<struct pollfd> &pollset) {
	pollset.emplace_back();
	size_t offset = pollset.size() - 1;
	memset(&pollset[offset], 0, sizeof(pollset[offset]));
	pollset[offset].fd = fd;
	return offset;
}

static size_t getpollsetoffsetfromfd(int fd, std::vector<struct pollfd> &pollset) {
	for(size_t i = 0; i < pollset.size(); i++) {
		if(pollset[i].fd == fd) return i;
	}
	return insertatendofpollset(fd, pollset);
}

static void removefdfrompollset(int fd, std::vector<struct pollfd> &pollset) {
	for(size_t i = 0; i < pollset.size(); i++) {
		if(pollset[i].fd == fd) {
			if(i != pollset.size() - 1) {
				pollset[i] = pollset[pollset.size() - 1];
			}
			pollset.pop_back();
			return;
		}
	}
}

wxThread::ExitCode socketpollthread::Entry() {
	std::vector<struct pollfd> pollset;
	std::forward_list<struct pollfd> disabled_pollset;
	pollset.emplace_back();
	memset(&pollset[0], 0, sizeof(pollset[0]));
	pollset[0].fd = pipefd;
	pollset[0].events = POLLIN;

	while(true) {
		poll(pollset.data(), pollset.size(), -1);

		for(size_t i = 1; i < pollset.size(); i++) {
			if(pollset[i].revents) {
				AddPendingEventForSocketPollEvent(pollset[i].fd, pollset[i].revents, true);

				//remove fd from pollset temporarily to stop it repeatedly re-firing
				disabled_pollset.push_front(pollset[i]);
				if(i != pollset.size() - 1) {
					pollset[i] = pollset[pollset.size() - 1];
				}
				pollset.pop_back();
				i--;
			}
		}

		if(pollset[0].revents & POLLIN) {
			socketpollmessage spm;
			size_t bytes_to_read = sizeof(spm);
			size_t bytes_read = 0;
			while(bytes_to_read) {
				ssize_t l_bytes_read = read(pipefd, ((char *) &spm) + bytes_read, bytes_to_read);
				if(l_bytes_read >= 0) {
					bytes_read += l_bytes_read;
					bytes_to_read -= l_bytes_read;
				}
				else {
					if(l_bytes_read == EINTR) continue;
					else {
						close(pipefd);
						return 0;
					}
				}
			}
			switch(spm.type) {
				case SPM_QUIT:
					close(pipefd);
					return 0;
				case SPM_FDCHANGE:
					disabled_pollset.remove_if([&](const struct pollfd &pfd) { return pfd.fd == spm.fd; });
					if(spm.events) {
						size_t offset = getpollsetoffsetfromfd(spm.fd, pollset);
						pollset[offset].events = spm.events;
					}
					else {
						removefdfrompollset(spm.fd, pollset);
					}
					break;
				case SPM_ENABLE:
					disabled_pollset.remove_if([&](const struct pollfd &pfd) {
						if(pfd.fd == spm.fd) {
							pollset.push_back(pfd);
							return true;
						}
						else return false;
					});
					break;
			}
		}
	}
}

#endif
