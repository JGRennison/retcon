#include "retcon.h"
#ifdef __WINDOWS__
#include <windows.h>
#else

#endif

static void check_multi_info(socketmanager *smp) {
	CURLMsg *msg;
	int msgs_left;
	mcurlconn *conn;
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

static int sock_cb(CURL *e, curl_socket_t s, int what, socketmanager *smp, mcurlconn *cs) {
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

DECLARE_EVENT_TYPE(wxextSOCK_NOTIFY, -1)

DEFINE_EVENT_TYPE(wxextSOCK_NOTIFY)

BEGIN_EVENT_TABLE(socketmanager, wxEvtHandler)
  EVT_COMMAND  (wxID_ANY, wxextSOCK_NOTIFY, socketmanager::NotifySockEventCmd)
END_EVENT_TABLE()


socketmanager::socketmanager() {
	loghandle=0;
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

bool socketmanager::AddConn(CURL* ch, mcurlconn *cs) {
	curl_easy_setopt(ch, CURLOPT_PRIVATE, cs);
	bool ret = (CURLM_OK == curl_multi_add_handle(curlmulti, ch));
	curl_multi_socket_action(curlmulti, 0, 0, &curnumsocks);
	return ret;
}

bool socketmanager::AddConn(twitcurlext &cs) {
	return AddConn(cs.GetCurlHandle(), &cs);
}

void socketmanager::RemoveConn(CURL* ch) {
	curl_multi_remove_handle(curlmulti, ch);
	curl_multi_socket_action(curlmulti, 0, 0, &curnumsocks);
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

void socketmanager::NotifySockEventCmd(wxCommandEvent &event) {
	NotifySockEvent((curl_socket_t) event.GetExtraLong(), event.GetInt());
}

void twitcurlext::NotifyDone(CURL *easy, CURLcode res) {
	long httpcode;
	curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpcode);

	KillConn();

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
		jsonparser jp(connmode, acc);
		std::string str;
		getLastWebResponse(str);
		jp.ParseString((char*) str.c_str());	//this modifies the contents of str!!
		str.clear();
	}
}

void mcurlconn::KillConn() {
	sm.RemoveConn(GenGetCurlHandle());
}

void mcurlconn::setlog(FILE *fs, bool verbose) {
        curl_easy_setopt(GenGetCurlHandle(), CURLOPT_STDERR, fs);
        curl_easy_setopt(GenGetCurlHandle(), CURLOPT_VERBOSE, verbose);
}

imgdlconn::imgdlconn(std::string &imgurl_, std::shared_ptr<userdatacontainer> user_) {
	Init(imgurl_, user_);
}

void imgdlconn::Init(std::string &imgurl_, std::shared_ptr<userdatacontainer> user_) {
	imgurl=imgurl_;
	user=user_;
	curlHandle = curl_easy_init();
	curl_easy_setopt(curlHandle, CURLOPT_CAINFO, "./cacert.pem");
	if(sm.loghandle) setlog(sm.loghandle, 1);
	curl_easy_setopt(curlHandle, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(curlHandle, CURLOPT_URL, imgurl.c_str());
        curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, curlCallback );
        curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, this );
	sm.AddConn(curlHandle, this);
}

imgdlconn::~imgdlconn() {
	curl_easy_cleanup(curlHandle);
}

std::stack<imgdlconn *> imgdlconn::idlestack;
std::unordered_set<imgdlconn *> imgdlconn::activeset;

imgdlconn *imgdlconn::GetConn(std::string &imgurl_, std::shared_ptr<userdatacontainer> user_) {
	imgdlconn *res;
	if(idlestack.empty()) {
		res=new imgdlconn(imgurl_, user_);
	}
	else {
		res=idlestack.top();
		idlestack.pop();
		res->Init(imgurl_, user_);
	}
	activeset.insert(res);
	return res;
}

void imgdlconn::Standby() {
	Reset();
	idlestack.push(this);
	activeset.erase(this);
}

void imgdlconn::ClearAllConns() {
	while(!idlestack.empty()) {
		delete idlestack.top();
		idlestack.pop();
	}
	for(auto it=activeset.begin(); it != activeset.end(); it++) {
		(*it)->KillConn();
		delete *it;
	}
}

void imgdlconn::Reset() {
	imgurl.clear();
	imgdata.clear();
	user.reset();
}

int imgdlconn::curlCallback(char* data, size_t size, size_t nmemb, imgdlconn *obj) {
	int writtenSize = 0;
	if( obj && data ) {
		writtenSize = size*nmemb;
		obj->imgdata.append(data, writtenSize);
	}
	return writtenSize;
}

void imgdlconn::NotifyDone(CURL *easy, CURLcode res) {
	long httpcode;
	curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpcode);

	if(httpcode!=200) {

	}
	else {
		if(imgurl==user->user->profile_img_url) {
			wxString filename;
			filename.Printf(wxT("/img_%" wxLongLongFmtSpec "d"), user->id);
			filename.Prepend(wxStandardPaths::Get().GetUserDataDir());
			wxFile file(filename, wxFile::write);
			file.Write(imgdata.data(), imgdata.size());
			wxMemoryInputStream memstream(imgdata.data(), imgdata.size());
			user->cached_profile_img=std::make_shared<wxImage>(memstream);
			user->cached_profile_img_url=imgurl;
			imgdata.clear();
		}
		KillConn();
		Standby();
	}
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


void socketsighandler(int signum, siginfo_t *info, void *ucontext) {
	if(!sm->MultiIOHandlerInited) return;

	int sendbitmask=0;
	if(info->si_band&POLLIN) sendbitmask|=CURL_CSELECT_IN;
	if(info->si_band&POLLOUT) sendbitmask|=CURL_CSELECT_OUT;
	if(info->si_band&POLLERR) sendbitmask|=CURL_CSELECT_ERR;

	wxCommandEvent event(wxextSOCK_NOTIFY);
	event.SetExtraLong((long) info->si_fd);
	event.SetInt(sendbitmask);

	sm->AddPendingEvent(event);
}

void socketmanager::InitMultiIOHandler() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction=&socketsighandler;
	sa.sa_flags=SA_SIGINFO;
	sigfillset(&sa.sa_mask);
	sigaction(SIGRTMAX, &sa, 0);

	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler=SIG_IGN;
	sigaction(SIGRTMAX, &sa, 0);

	MultiIOHandlerInited=false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	if(what) {
		int flags=fcntl(s, F_GETFL);
		fcntl(s, F_SETFL, flags|O_ASYNC);
		fcntl(s, F_SETSIG, SIGRTMAX);
	}
	else {
		int flags=fcntl(s, F_GETFL);
		fcntl(s, F_SETFL, flags&~O_ASYNC);
		fcntl(s, F_SETSIG, 0);
	}
}
#endif
