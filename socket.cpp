#include "retcon.h"
#ifdef __WINDOWS__
#include <windows.h>
#else

#endif

static void check_multi_info(socketmanager *smp) {
	CURLMsg *msg;
	int msgs_left;
	twitcurlext *conn;
	CURL *easy;
	CURLcode res;

	while ((msg = curl_multi_info_read(smp->curlmulti, &msgs_left))) {
		if (msg->msg == CURLMSG_DONE) {
			easy = msg->easy_handle;
			res = msg->data.result;
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
			curl_multi_remove_handle(smp->curlmulti, easy);
			conn->NotifyDone(easy, res);
		}
	}
	if(sm.curnumsocks==0) smp->st.Stop();
}

static int sock_cb(CURL *e, curl_socket_t s, int what, socketmanager *smp, twitcurlext *cs) {
	wxLogWarning(wxT("Socket Interest Change Callback: %p, %d"), s, what);
	if(what!=CURL_POLL_REMOVE) {
		if(!cs) {
			curl_easy_getinfo(e, CURLINFO_PRIVATE, &cs);
			curl_multi_assign(smp->curlmulti, s, cs);
		}
	}
	smp->RegisterSockInterest(e, s, what);
	return 0;
}

static int multi_timer_cb(CURLM *multi, long timeout_ms, socketmanager *smp) {
	wxLogWarning(wxT("Socket Timer Callback: %d ms"), timeout_ms);
	if(timeout_ms>0) {
		smp->st.Start(timeout_ms,wxTIMER_ONE_SHOT);
	}
	else if(timeout_ms==0) {
		smp->st.Stop();
		smp->st.Notify();
	}
	else {
		smp->st.Stop();
	}
	return 0;
}

socketmanager::socketmanager() {
	curlmulti=curl_multi_init();
	curl_multi_setopt(curlmulti, CURLMOPT_SOCKETFUNCTION, sock_cb);
	curl_multi_setopt(curlmulti, CURLMOPT_SOCKETDATA, this);
	curl_multi_setopt(curlmulti, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
	curl_multi_setopt(curlmulti, CURLMOPT_TIMERDATA, this);
	MultiIOHandlerInited=false;

}

socketmanager::~socketmanager() {
	if(MultiIOHandlerInited) DeInitMultiIOHandler();
	curl_multi_cleanup(curlmulti);
}

bool socketmanager::AddConn(CURL* ch, twitcurlext *cs) {
	curl_easy_setopt(ch, CURLOPT_PRIVATE, cs);
	bool ret = (CURLM_OK == curl_multi_add_handle(curlmulti, ch));
	curl_multi_socket_action(curlmulti, 0, 0, &curnumsocks);
	return ret;
}

bool socketmanager::AddConn(twitcurlext &cs) {
	return AddConn(cs.GetCurlHandle(), &cs);
}


void sockettimeout::Notify() {
	wxLogWarning(wxT("Socket Timeout"));
	curl_multi_socket_action(sm.curlmulti, CURL_SOCKET_TIMEOUT, 0, &sm.curnumsocks);
	check_multi_info(&sm);
}

void socketmanager::NotifySockEvent(curl_socket_t sockfd, int ev_bitmask) {
	wxLogWarning(wxT("Socket Notify"));
	curl_multi_socket_action(curlmulti, sockfd, ev_bitmask, &curnumsocks);
	check_multi_info(this);
}

void twitcurlext::NotifyDone(CURL *easy, CURLcode res) {
	long httpcode;
	curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpcode);
	std::shared_ptr<taccount> acc=tacc.lock();
	if(!acc) return;
	if(httpcode!=200) {
		//failed
		acc->enabled=0;
		std::string rettext;
		getLastWebResponse(rettext);
		wxLogWarning(wxT("Request failed: code %d, text: %s"), httpcode, wxstrstd(rettext).c_str());
	}
	else {
		switch(connmode) {
			case CS_ACCVERIFY: {
				userdataparse parse;
				getLastWebResponse(parse.json);
				parse.ParseJson(acc);
				std::shared_ptr<userdata> userobj=parse.pop_front();
				ad.UpdateUserContainer(ad.GetUserContainerById(userobj->id), userobj);
				acc->dispname=wxstrstd(userobj->name);
				userobj->Dump();
				acc->PostAccVerifyInit();
			}
			break;
			case CS_TIMELINE: {
				tweetparse parse;
				getLastWebResponse(parse.json);
				parse.ParseJson(acc);
				//do useful stuff
				for(auto it=parse.list.begin() ; it != parse.list.end(); it++ ) (*it)->Dump();
			}
		}
	}
}

void twitcurlext::KillConn() {
	curl_multi_remove_handle(sm.curlmulti, GetCurlHandle());
}

#ifdef __WINDOWS__

const char *tclassname="____retcon_wsaasyncselect_window";

LRESULT CALLBACK wndproc(

    HWND hwnd,	// handle of window
    UINT uMsg,	// message identifier
    WPARAM wParam,	// first message parameter
    LPARAM lParam 	// second message parameter
   ) {
	if(uMsg==WM_USER) {
		int sendbitmask;
		switch(lParam) {
			case FD_READ:
				sendbitmask=CURL_CSELECT_IN;
				break;
			case FD_WRITE:
				sendbitmask=CURL_CSELECT_OUT;
				break;
			default:
				sendbitmask=0;
				break;
		}
		sm.NotifySockEvent((curl_socket_t) wParam, sendbitmask);
	}
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void socketmanager::InitMultiIOHandler() {
	WNDCLASSA wc = { 0, &wndproc, 0, 0,
			(HINSTANCE) GetModuleHandle(0),
			0, 0, 0, 0,
			tclassname };
	RegisterClassA(&wc);
	wind=CreateWindowA(tclassname, tclassname, 0, 0, 0, 0, 0, 0, 0, (HINSTANCE) GetModuleHandle(0), 0);
	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	DestroyWindow(wind);
	wind=0;
	MultiIOHandlerInited=false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	long lEvent;
	const long common=FD_OOB|FD_ACCEPT|FD_CONNECT|FD_CLOSE;
	switch(what) {
		case CURL_POLL_NONE:
		case CURL_POLL_REMOVE:
		default:
			lEvent=0;
			break;
		case CURL_POLL_IN:
			lEvent=FD_READ|common;
			break;
		case CURL_POLL_OUT:
			lEvent=FD_WRITE|common;
			break;
		case CURL_POLL_INOUT:
			lEvent=FD_READ|FD_WRITE|common;
			break;
	}
	WSAAsyncSelect(s, wind, WM_USER, lEvent);
}
#else

#endif
