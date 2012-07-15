#include "retcon.h"
#ifdef __WINDOWS__
#include <windows.h>
#else
#include <signal.h>
#include <poll.h>
#endif

BEGIN_EVENT_TABLE( mcurlconn, wxEvtHandler )
	EVT_TIMER(MCCT_RETRY, mcurlconn::RetryNotify)
END_EVENT_TABLE()

void mcurlconn::KillConn() {
	wxLogWarning(wxT("KillConn: conn: %p"), this);
	sm.RemoveConn(GenGetCurlHandle());
}

void mcurlconn::setlog(FILE *fs, bool verbose) {
        curl_easy_setopt(GenGetCurlHandle(), CURLOPT_STDERR, fs);
        curl_easy_setopt(GenGetCurlHandle(), CURLOPT_VERBOSE, verbose);
}

void mcurlconn::NotifyDone(CURL *easy, CURLcode res) {
	long httpcode;
	curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpcode);

	if(httpcode!=200 || res!=CURLE_OK) {
		//failed
		if(res==CURLE_OK) {
			wxLogWarning(wxT("Request failed: conn: %p, code: %d"), this, httpcode);
		}
		else {
			wxLogWarning(wxT("Socket error: conn: %p, code: %d, message: %s"), this, res, wxstrstd(curl_easy_strerror(res)).c_str());
		}
		KillConn();
		HandleError(easy, httpcode, res);
	}
	else {
		errorcount=0;
		NotifyDoneSuccess(easy, res);
	}
}

void mcurlconn::HandleError(CURL *easy, long httpcode, CURLcode res) {
	errorcount++;
	MCC_HTTPERRTYPE err=MCC_RETRY;
	if(res==CURLE_OK) {	//http error, not socket error
		err=CheckHTTPErrType(httpcode);
	}
	if(errorcount>=5 && err<MCC_FAILED) {
		err=MCC_FAILED;
	}
	switch(err) {
		case MCC_RETRY:
			tm = new wxTimer(this, MCCT_RETRY);
			tm->Start(2500 * (1<<errorcount), true);	//1 shot timer, 5s for first error, 10s for second, etc
			break;
		case MCC_FAILED:
			HandleFailure();
			break;
	}
}

void mcurlconn::RetryNotify(wxTimerEvent& event) {
	delete tm;
	tm=0;
	DoRetry();
}

void mcurlconn::StandbyTidy() {
	if(tm) {
		delete tm;
		tm=0;
	}
}

BEGIN_EVENT_TABLE( imgdlconn, mcurlconn )
END_EVENT_TABLE()

imgdlconn::imgdlconn() : curlHandle(0) {
}

void imgdlconn::Init(std::string &imgurl_, std::shared_ptr<userdatacontainer> user_) {
	imgurl=imgurl_;
	user=user_;
	user->udc_flags|=UDC_IMAGE_DL_IN_PROGRESS;
	if(!curlHandle) curlHandle = curl_easy_init();
	#ifdef __WINDOWS__
	curl_easy_setopt(curlHandle, CURLOPT_CAINFO, "./cacert.pem");
	#endif
	if(sm.loghandle) setlog(sm.loghandle, 1);
	curl_easy_setopt(curlHandle, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(curlHandle, CURLOPT_URL, imgurl.c_str());
        curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, curlCallback );
        curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, this );
	wxLogWarning(wxT("Fetching image %s for user id %" wxLongLongFmtSpec "d, conn: %p"), wxstrstd(imgurl).c_str(), user_->id, this);
	sm.AddConn(curlHandle, this);
}

void imgdlconn::DoRetry() {
	if(imgurl==user->user->profile_img_url) Init(imgurl, user);
	else cp.Standby(this);
}

void imgdlconn::HandleFailure() {
	if(imgurl==user->user->profile_img_url) {
		if(!user->cached_profile_img) {	//generate a placeholder image
			user->cached_profile_img=std::make_shared<wxBitmap>(48,48,-1);
			wxMemoryDC dc(*user->cached_profile_img);
			dc.SetBackground(wxBrush(wxColour(0,0,0,wxALPHA_TRANSPARENT)));
			dc.Clear();
		}
		user->udc_flags&=~UDC_IMAGE_DL_IN_PROGRESS;
		user->CheckPendingTweets();
		cp.Standby(this);
	}
	else cp.Standby(this);
}

imgdlconn::~imgdlconn() {
	if(curlHandle) curl_easy_cleanup(curlHandle);
	curlHandle=0;
}

void imgdlconn::Reset() {
	imgurl.clear();
	imgdata.clear();
	user.reset();
}

imgdlconn *imgdlconn::GetConn(std::string &imgurl_, std::shared_ptr<userdatacontainer> user_) {
	imgdlconn *res=cp.GetConn();
	res->Init(imgurl_, user_);
	return res;
}

int imgdlconn::curlCallback(char* data, size_t size, size_t nmemb, imgdlconn *obj) {
	int writtenSize = 0;
	if( obj && data ) {
		writtenSize = size*nmemb;
		obj->imgdata.append(data, writtenSize);
	}
	return writtenSize;
}

void imgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res) {
	if(imgurl==user->user->profile_img_url) {
		wxString filename;
		user->GetImageLocalFilename(filename);
		wxFile file(filename, wxFile::write);
		file.Write(imgdata.data(), imgdata.size());
		wxMemoryInputStream memstream(imgdata.data(), imgdata.size());

		//user->cached_profile_img=std::make_shared<wxImage>(memstream);
		wxImage img(memstream);

		if(img.GetHeight()>(int) gc.maxpanelprofimgsize || img.GetWidth()>(int) gc.maxpanelprofimgsize) {
			double scalefactor=(double) gc.maxpanelprofimgsize / (double) std::max(img.GetHeight(), img.GetWidth());
			int newwidth = (double) img.GetWidth() * scalefactor;
			int newheight = (double) img.GetHeight() * scalefactor;
			img.Rescale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH);
		}

		user->cached_profile_img=std::make_shared<wxBitmap>(img);

		user->cached_profile_img_url=imgurl;
		imgdata.clear();
		user->udc_flags&=~UDC_IMAGE_DL_IN_PROGRESS;
		user->CheckPendingTweets();
	}
	KillConn();
	cp.Standby(this);
}

template <typename C> connpool<C>::~connpool() {
	ClearAllConns();
}
template connpool<twitcurlext>::~connpool();

template <typename C> void connpool<C>::ClearAllConns() {
	while(!idlestack.empty()) {
		delete idlestack.top();
		idlestack.pop();
	}
	for(auto it=activeset.begin(); it != activeset.end(); it++) {
		(*it)->KillConn();
		delete *it;
	}
	activeset.clear();
}
template void connpool<imgdlconn>::ClearAllConns();
template void connpool<twitcurlext>::ClearAllConns();

template <typename C> C *connpool<C>::GetConn() {
	C *res;
	if(idlestack.empty()) {
		res=new C();
	}
	else {
		res=idlestack.top();
		idlestack.pop();
	}
	activeset.insert(res);
	return res;
}
template twitcurlext *connpool<twitcurlext>::GetConn();

template <typename C> void connpool<C>::Standby(C *obj) {
	obj->StandbyTidy();
	obj->Reset();
	obj->mcflags=0;
	idlestack.push(obj);
	activeset.erase(obj);
}
template void connpool<twitcurlext>::Standby(twitcurlext *obj);

connpool<imgdlconn> imgdlconn::cp;

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
			long httpcode;
			curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpcode);
			curl_easy_getinfo(easy, CURLINFO_PRIVATE, &conn);
			wxLogWarning(wxT("Socket Done, conn: %p, res: %d, http: %d"), conn, res, httpcode);
			curl_multi_remove_handle(smp->curlmulti, easy);
			conn->NotifyDone(easy, res);
		}
	}
	if(sm.curnumsocks==0) {
		wxLogWarning(wxT("No Sockets Left, Stopping Timer"));
		smp->st.Stop();
	}
}

static int sock_cb(CURL *e, curl_socket_t s, int what, socketmanager *smp, mcurlconn *cs) {
	wxLogWarning(wxT("Socket Interest Change Callback: %d, %d, conn: %p"), s, what, cs);
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
	//long new_timeout_ms;
	//if(timeout_ms<=0 || timeout_ms>90000) new_timeout_ms=90000;
	//else new_timeout_ms=timeout_ms;

	wxLogWarning(wxT("Socket Timer Callback: %d ms"), timeout_ms);

	if(timeout_ms>0) smp->st.Start(timeout_ms,wxTIMER_ONE_SHOT);
	else smp->st.Stop();
	if(!timeout_ms) smp->st.Notify();

	return 0;
}

/*static int progress_cb(void *clientp, double dltotal, double dlnow, double ultotal, double ulnow) {
	wxLogWarning(wxT("progress_cb: %p %g %g %g %g"), clientp, dltotal, dlnow, ultotal, ulnow);
	mcurlconn *conn=(mcurlconn *) clientp;
	if(!conn) return 0;
	time_t now=time(0);	//this is not strictly monotonic, but good enough for calculating rough timeouts
	if(dlnow>0 || ulnow>0) {
		conn->lastactiontime=now;
		return 0;
	}
	else if(conn->lastactiontime>now) {
		conn->lastactiontime=now;
		return 0;
	}
	else if(now>(conn->lastactiontime+90)) {
		//timeout
		return 1;
	}
	else return 0;
}*/

DECLARE_EVENT_TYPE(wxextSOCK_NOTIFY, -1)

DEFINE_EVENT_TYPE(wxextSOCK_NOTIFY)

BEGIN_EVENT_TABLE(socketmanager, wxEvtHandler)
  EVT_COMMAND  (wxID_ANY, wxextSOCK_NOTIFY, socketmanager::NotifySockEventCmd)
END_EVENT_TABLE()


socketmanager::socketmanager() : st(*this) {
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
	//curl_easy_setopt(ch, CURLOPT_LOW_SPEED_LIMIT, 1);
	//curl_easy_setopt(ch, CURLOPT_LOW_SPEED_TIME, 90);
	//curl_easy_setopt(ch, CURLOPT_PROGRESSDATA, cs);
	//curl_easy_setopt(ch, CURLOPT_PROGRESSFUNCTION, progress_cb);
	//curl_easy_setopt(ch, CURLOPT_NOPROGRESS, 0);
	curl_easy_setopt(ch, CURLOPT_TIMEOUT, (cs->mcflags&MCF_NOTIMEOUT)?0:180);
	curl_easy_setopt(ch, CURLOPT_PRIVATE, cs);
	bool ret = (CURLM_OK == curl_multi_add_handle(curlmulti, ch));
	curl_multi_socket_action(curlmulti, 0, 0, &curnumsocks);
	check_multi_info(&sm);
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
	wxLogWarning(wxT("Socket Timer Event"));
	curl_multi_socket_action(sm.curlmulti, CURL_SOCKET_TIMEOUT, 0, &sm.curnumsocks);
	check_multi_info(&sm);
	//if(!IsRunning() && sm.curnumsocks) Start(90000,wxTIMER_ONE_SHOT);
}

void socketmanager::NotifySockEvent(curl_socket_t sockfd, int ev_bitmask) {
	wxLogWarning(wxT("Socket Notify (%d)"), sockfd);
	curl_multi_socket_action(curlmulti, sockfd, ev_bitmask, &curnumsocks);
	check_multi_info(this);
}

void socketmanager::NotifySockEventCmd(wxCommandEvent &event) {
	NotifySockEvent((curl_socket_t) event.GetExtraLong(), event.GetInt());
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
	if(!sm.MultiIOHandlerInited) return;

	int sendbitmask=0;
	if(info->si_band&POLLIN) sendbitmask|=CURL_CSELECT_IN;
	if(info->si_band&POLLOUT) sendbitmask|=CURL_CSELECT_OUT;
	if(info->si_band&POLLERR) sendbitmask|=CURL_CSELECT_ERR;

	wxCommandEvent event(wxextSOCK_NOTIFY);
	event.SetExtraLong((long) info->si_fd);
	event.SetInt(sendbitmask);

	sm.AddPendingEvent(event);
}

void socketmanager::InitMultiIOHandler() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction=&socketsighandler;
	sa.sa_flags=SA_SIGINFO;
	sigfillset(&sa.sa_mask);
	sigaction(SIGRTMIN, &sa, 0);

	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler=SIG_IGN;
	sigaction(SIGRTMIN, &sa, 0);

	MultiIOHandlerInited=false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	if(what) {
		fcntl(s, F_SETOWN, (int) getpid());
		fcntl(s, F_SETSIG, SIGRTMIN);
		int flags=fcntl(s, F_GETFL);
		if(!(flags&O_ASYNC)) {
			fcntl(s, F_SETFL, flags|O_ASYNC);
			//check to see if IO is already waiting
			NotifySockEvent(s, 0);
		}
	}
	else {
		int flags=fcntl(s, F_GETFL);
		fcntl(s, F_SETFL, flags&~O_ASYNC);
		fcntl(s, F_SETSIG, 0);
	}
}
#endif
