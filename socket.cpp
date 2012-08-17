#include "retcon.h"
#include <openssl/sha.h>
#ifdef RCS_WSAASYNCSELMODE
	#include <windows.h>
#endif
#ifdef RCS_SIGNALMODE
	#include <signal.h>
#endif
#ifdef RCS_POLLTHREADMODE
	#include <poll.h>
#endif
#ifdef RCS_GTKSOCKMODE
	#include <glib.h>
#endif


BEGIN_EVENT_TABLE( mcurlconn, wxEvtHandler )
	EVT_TIMER(MCCT_RETRY, mcurlconn::RetryNotify)
END_EVENT_TABLE()

int curl_debug_func(CURL *cl, curl_infotype ci, char *txt, size_t len, void *extra) {
	if(ci==CURLINFO_TEXT) {
		LogMsgProcess(LFT_CURLVERB, wxString::FromUTF8(txt, len));
	}
	return 0;
}

void SetCurlHandleVerboseState(CURL *easy, bool verbose) {
	if(verbose) curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, &curl_debug_func);
	curl_easy_setopt(easy, CURLOPT_VERBOSE, verbose);
}

void mcurlconn::KillConn() {
	LogMsgFormat(LFT_SOCKTRACE, wxT("KillConn: conn: %p"), this);
	sm.RemoveConn(GenGetCurlHandle());
}

void mcurlconn::NotifyDone(CURL *easy, CURLcode res) {
	long httpcode;
	curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &httpcode);

	if(httpcode!=200 || res!=CURLE_OK) {
		//failed
		if(res==CURLE_OK) {
			char *url;
			curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &url);
			LogMsgFormat(LFT_SOCKERR, wxT("Request failed: conn: %p, code: %d, url: %s"), this, httpcode, wxstrstd(url).c_str());
		}
		else {
			LogMsgFormat(LFT_SOCKERR, wxT("Socket error: conn: %p, code: %d, message: %s"), this, res, wxstrstd(curl_easy_strerror(res)).c_str());
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

MCC_HTTPERRTYPE mcurlconn::CheckHTTPErrType(long httpcode) {
	if(httpcode>=400 && httpcode<420) return MCC_FAILED;
	return MCC_RETRY;
}

dlconn::dlconn() : curlHandle(0) {
}

dlconn::~dlconn() {
	if(curlHandle) curl_easy_cleanup(curlHandle);
	curlHandle=0;
}

void dlconn::Reset() {
	url.clear();
	data.clear();
}

void dlconn::Init(const std::string &url_) {
	url=url_;
	if(!curlHandle) curlHandle = curl_easy_init();
	#ifdef __WINDOWS__
	curl_easy_setopt(curlHandle, CURLOPT_CAINFO, "./cacert.pem");
	#endif
	curl_easy_setopt(curlHandle, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(curlHandle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, curlCallback );
        curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, this );
	sm.AddConn(curlHandle, this);
}

int dlconn::curlCallback(char* data, size_t size, size_t nmemb, dlconn *obj) {
	int writtenSize = 0;
	if( obj && data ) {
		writtenSize = size*nmemb;
		obj->data.append(data, writtenSize);
	}
	return writtenSize;
}

void profileimgdlconn::Init(const std::string &imgurl_, const std::shared_ptr<userdatacontainer> &user_) {
	user=user_;
	user->udc_flags|=UDC_IMAGE_DL_IN_PROGRESS;
	LogMsgFormat(LFT_SOCKTRACE, wxT("Fetching image %s for user id %" wxLongLongFmtSpec "d, conn: %p"), wxstrstd(imgurl_).c_str(), user_->id, this);
	dlconn::Init(imgurl_);
}

void profileimgdlconn::DoRetry() {
	if(url==user->GetUser().profile_img_url) Init(url, user);
	else cp.Standby(this);
}

void profileimgdlconn::HandleFailure() {
	if(url==user->GetUser().profile_img_url) {
		if(!user->udc_flags&UDC_PROFILE_BITMAP_SET) {	//generate a placeholder image
			user->cached_profile_img.Create(48,48,-1);
			wxMemoryDC dc(user->cached_profile_img);
			dc.SetBackground(wxBrush(wxColour(0,0,0,wxALPHA_TRANSPARENT)));
			dc.Clear();
			user->udc_flags|=UDC_PROFILE_BITMAP_SET;
		}
		user->udc_flags&=~UDC_IMAGE_DL_IN_PROGRESS;
		user->udc_flags&=~UDC_HALF_PROFILE_BITMAP_SET;
		user->CheckPendingTweets();
		cp.Standby(this);
	}
	else cp.Standby(this);
}

void profileimgdlconn::Reset() {
	dlconn::Reset();
	user.reset();
}

profileimgdlconn *profileimgdlconn::GetConn(const std::string &imgurl_, const std::shared_ptr<userdatacontainer> &user_) {
	profileimgdlconn *res=cp.GetConn();
	res->Init(imgurl_, user_);
	return res;
}

void profileimgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res) {
	if(url==user->GetUser().profile_img_url) {
		wxString filename;
		user->GetImageLocalFilename(filename);
		wxFile file(filename, wxFile::write);
		file.Write(data.data(), data.size());
		wxMemoryInputStream memstream(data.data(), data.size());

		//user->cached_profile_img=std::make_shared<wxImage>(memstream);
		wxImage img(memstream);
		user->SetProfileBitmapFromwxImage(img);

		user->cached_profile_img_url=url;
		SHA1((const unsigned char *) data.data(), (unsigned long) data.size(), user->cached_profile_img_sha1);
		user->lastupdate_wrotetodb=0;		//force user to be written out to database
		dbc.InsertUser(user);
		data.clear();
		user->udc_flags&=~UDC_IMAGE_DL_IN_PROGRESS;
		user->udc_flags&=~UDC_HALF_PROFILE_BITMAP_SET;
		user->CheckPendingTweets();
		UpdateUsersTweet(user->id, true);
		if(user->udc_flags&UDC_WINDOWOPEN) user_window::CheckRefresh(user->id, true);
	}
	KillConn();
	cp.Standby(this);
}

void mediaimgdlconn::Init(const std::string &imgurl_, media_id_type media_id_, unsigned int flags_) {
	media_id=media_id_;
	flags=flags_;
	LogMsgFormat(LFT_SOCKTRACE, wxT("Fetching media image %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d, flags: %X, conn: %p"), wxstrstd(imgurl_).c_str(), media_id_.m_id, media_id_.t_id, flags_, this);
	dlconn::Init(imgurl_);
}

void mediaimgdlconn::DoRetry() {
	Init(url, media_id, flags);
}

void mediaimgdlconn::HandleFailure() {
	auto it=ad.media_list.find(media_id);
	if(it!=ad.media_list.end()) {
		media_entity &me=it->second;
		if(flags&MIDC_FULLIMG) {
			me.flags|=ME_FULL_FAILED;
			if(me.win) me.win->Update();
		}
	}
	delete this;
}

void mediaimgdlconn::Reset() {
	dlconn::Reset();
}

void mediaimgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res) {

	LogMsgFormat(LFT_SOCKTRACE, wxT("Media image downloaded %s, id: %" wxLongLongFmtSpec "d, flags: %X"), wxstrstd(url).c_str(), media_id, flags);

	auto it=ad.media_list.find(media_id);
	if(it!=ad.media_list.end()) {
		media_entity &me=it->second;

		if(flags&MIDC_OPPORTUNIST_THUMB && !(flags&MIDC_THUMBIMG)) {
			flags|=MIDC_THUMBIMG;
			if(flags&MIDC_OPPORTUNIST_REDRAW_TWEETS) flags|=MIDC_REDRAW_TWEETS;
		}

		if(flags&MIDC_THUMBIMG) {
			wxMemoryInputStream memstream(data.data(), data.size());
			wxImage img(memstream);
			const int maxdim=64;
			if(img.GetHeight()>maxdim || img.GetWidth()>maxdim) {
				double scalefactor=(double) maxdim / (double) std::max(img.GetHeight(), img.GetWidth());
				int newwidth = (double) img.GetWidth() * scalefactor;
				int newheight = (double) img.GetHeight() * scalefactor;
				me.thumbimg=img.Scale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH);
			}
			else me.thumbimg=img;
			me.flags|=ME_HAVE_THUMB;
			if(gc.cachethumbs) {
				wxMemoryOutputStream memstr;
				me.thumbimg.SaveFile(memstr, wxBITMAP_TYPE_PNG);
				const unsigned char *data=(const unsigned char *) memstr.GetOutputStreamBuffer()->GetBufferStart();
				size_t size=memstr.GetSize();
				wxFile file(me.cached_thumb_filename(), wxFile::write);
				file.Write(data, size);
				SHA1(data, size, me.thumb_img_sha1);
				dbc.UpdateMediaChecksum(me, false);
			}
		}

		if(flags&MIDC_FULLIMG) {
			me.fulldata=std::move(data);
			me.flags|=ME_HAVE_FULL;
			if(me.win) me.win->Update();
			if(gc.cachemedia) {
				wxFile file(me.cached_full_filename(), wxFile::write);
				file.Write(me.fulldata.data(), me.fulldata.size());
				SHA1((const unsigned char *) me.fulldata.data(), (unsigned long) me.fulldata.size(), me.full_img_sha1);
				dbc.UpdateMediaChecksum(me, true);
			}
		}

		if(flags&MIDC_REDRAW_TWEETS) {
			for(auto it=me.tweet_list.begin(); it!=me.tweet_list.end(); ++it) {
				LogMsgFormat(LFT_SOCKTRACE, wxT("Media: UpdateTweet"));
				UpdateTweet(*it);
			}
		}
	}

	data.clear();

	KillConn();
	delete this;
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
template void connpool<profileimgdlconn>::ClearAllConns();
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

connpool<profileimgdlconn> profileimgdlconn::cp;

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
			LogMsgFormat(LFT_SOCKTRACE, wxT("Socket Done, conn: %p, res: %d, http: %d"), conn, res, httpcode);
			curl_multi_remove_handle(smp->curlmulti, easy);
			conn->NotifyDone(easy, res);
		}
	}
	if(sm.curnumsocks==0) {
		LogMsgFormat(LFT_SOCKTRACE, wxT("No Sockets Left, Stopping Timer"));
		smp->st.Stop();
	}
}

static int sock_cb(CURL *e, curl_socket_t s, int what, socketmanager *smp, mcurlconn *cs) {
	LogMsgFormat(LFT_SOCKTRACE, wxT("Socket Interest Change Callback: %d, %d, conn: %p"), s, what, cs);
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

	LogMsgFormat(LFT_SOCKTRACE, wxT("Socket Timer Callback: %d ms"), timeout_ms);

	if(timeout_ms>0) smp->st.Start(timeout_ms,wxTIMER_ONE_SHOT);
	else smp->st.Stop();
	if(!timeout_ms) smp->st.Notify();

	return 0;
}

socketmanager::socketmanager() : st(*this), curnumsocks(0) {
	MultiIOHandlerInited=false;
}

socketmanager::~socketmanager() {
	DeInitMultiIOHandler();
}

bool socketmanager::AddConn(CURL* ch, mcurlconn *cs) {
	connlist.push_front(ch);
	SetCurlHandleVerboseState(ch, currentlogflags&LFT_CURLVERB);
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
	connlist.remove(ch);
	curl_multi_socket_action(curlmulti, 0, 0, &curnumsocks);
}

void sockettimeout::Notify() {
	LogMsgFormat(LFT_SOCKTRACE, wxT("Socket Timer Event"));
	curl_multi_socket_action(sm.curlmulti, CURL_SOCKET_TIMEOUT, 0, &sm.curnumsocks);
	check_multi_info(&sm);
	//if(!IsRunning() && sm.curnumsocks) Start(90000,wxTIMER_ONE_SHOT);
}

void socketmanager::NotifySockEvent(curl_socket_t sockfd, int ev_bitmask) {
	LogMsgFormat(LFT_SOCKTRACE, wxT("Socket Notify (%d)"), sockfd);
	curl_multi_socket_action(curlmulti, sockfd, ev_bitmask, &curnumsocks);
	check_multi_info(this);
}

BEGIN_EVENT_TABLE(socketmanager, wxEvtHandler)
#if defined(RCS_POLLTHREADMODE) || defined(RCS_SIGNALMODE)
  EVT_EXTSOCKETNOTIFY(wxID_ANY, socketmanager::NotifySockEventCmd)
#endif
END_EVENT_TABLE()

void socketmanager::InitMultiIOHandlerCommon() {
	LogMsg(LFT_SOCKTRACE, wxT("socketmanager::InitMultiIOHandlerCommon"));
	curlmulti=curl_multi_init();
	curl_multi_setopt(curlmulti, CURLMOPT_SOCKETFUNCTION, sock_cb);
	curl_multi_setopt(curlmulti, CURLMOPT_SOCKETDATA, this);
	curl_multi_setopt(curlmulti, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
	curl_multi_setopt(curlmulti, CURLMOPT_TIMERDATA, this);
}

void socketmanager::DeInitMultiIOHandlerCommon() {
	LogMsg(LFT_SOCKTRACE, wxT("socketmanager::DeInitMultiIOHandlerCommon"));
	curl_multi_cleanup(curlmulti);
}

#ifdef RCS_WSAASYNCSELMODE

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
	if(MultiIOHandlerInited) return;
	InitMultiIOHandlerCommon();
	WNDCLASSA wc = { 0, &wndproc, 0, 0,
			(HINSTANCE) GetModuleHandle(0),
			0, 0, 0, 0,
			tclassname };
	RegisterClassA(&wc);
	wind=CreateWindowA(tclassname, tclassname, 0, 0, 0, 0, 0, 0, 0, (HINSTANCE) GetModuleHandle(0), 0);
	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	if(!MultiIOHandlerInited) return;
	DestroyWindow(wind);
	wind=0;
	DeInitMultiIOHandlerCommon();
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
	sock_gsource *gs=(sock_gsource*) source;
	for(auto it=gs->sm->sockpollmap.begin(); it!=gs->sm->sockpollmap.end(); ++it) {
		if(it->second.revents) return true;
	}
	return false;
}

gboolean gs_dispatch(GSource *source, GSourceFunc callback, gpointer user_data) {
	sock_gsource *gs=(sock_gsource*) source;
	std::forward_list<std::pair<curl_socket_t,int> > actlist;
	for(auto it=gs->sm->sockpollmap.begin(); it!=gs->sm->sockpollmap.end(); ++it) {
		if(it->second.revents) {
			int sendbitmask=0;
			if(it->second.revents&(G_IO_IN|G_IO_PRI)) sendbitmask|=CURL_CSELECT_IN;
			if(it->second.revents&G_IO_OUT) sendbitmask|=CURL_CSELECT_OUT;
			if(it->second.revents&(G_IO_ERR|G_IO_HUP)) sendbitmask|=CURL_CSELECT_ERR;
			actlist.push_front(std::make_pair(it->first, sendbitmask));	//NotifySockEvent is entitled to modify the set of monitored sockets
		}
	}
	for(auto it=actlist.begin(); it!=actlist.end(); ++it) {
		gs->sm->NotifySockEvent(it->first, it->second);
	}
	return true;
}

GSourceFuncs gsf;

void socketmanager::InitMultiIOHandler() {
	if(MultiIOHandlerInited) return;
	InitMultiIOHandlerCommon();

	memset(&gsf, 0, sizeof(gsf));
	gsf.prepare=&gs_prepare;
	gsf.check=&gs_check;
	gsf.dispatch=&gs_dispatch;

	sock_gsource *sgs=(sock_gsource *) g_source_new(&gsf, sizeof(sock_gsource));
	sgs->sm=this;
	source_id=g_source_attach(sgs, 0);
	gs=sgs;

	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	if(!MultiIOHandlerInited) return;

	g_source_destroy((sock_gsource*) gs);

	DeInitMultiIOHandlerCommon();
	MultiIOHandlerInited=false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	sock_gsource *sgs=(sock_gsource *) gs;
	short events=0;
	switch(what) {
		case CURL_POLL_NONE:
		case CURL_POLL_REMOVE:
		default:
			events=0;
			break;
		case CURL_POLL_IN:
			events=G_IO_IN|G_IO_PRI|G_IO_ERR;
			break;
		case CURL_POLL_OUT:
			events=G_IO_OUT|G_IO_ERR;
			break;
		case CURL_POLL_INOUT:
			events=G_IO_IN|G_IO_PRI|G_IO_OUT|G_IO_ERR;
			break;
	}
	auto egp=sgs->sm->sockpollmap.find(s);
	GPollFD *gpfd;
	if(egp!=sgs->sm->sockpollmap.end()) {
		gpfd=&(egp->second);
		g_source_remove_poll(sgs, gpfd);
		if(!events) sgs->sm->sockpollmap.erase(egp);
	}
	else if(events) gpfd=&sgs->sm->sockpollmap[s];
	if(events) {
		gpfd->fd=s;
		gpfd->events=events;
		gpfd->revents=0;
		g_source_add_poll(sgs, gpfd);
	}
}

#endif

#if defined(RCS_POLLTHREADMODE) || defined(RCS_SIGNALMODE)

DEFINE_EVENT_TYPE(wxextSOCK_NOTIFY)

wxextSocketNotifyEvent::wxextSocketNotifyEvent( int id )
: wxEvent(id, wxextSOCK_NOTIFY) {
	fd=0;
	reenable=false;
	curlbitmask=0;
}
wxextSocketNotifyEvent::wxextSocketNotifyEvent( const wxextSocketNotifyEvent &src ) : wxEvent( src ) {
	fd=src.fd;
	reenable=src.reenable;
	curlbitmask=src.curlbitmask;
}

wxEvent *wxextSocketNotifyEvent::Clone() const {
	return new wxextSocketNotifyEvent(*this);
}

void socketmanager::NotifySockEventCmd(wxextSocketNotifyEvent &event) {
	NotifySockEvent((curl_socket_t) event.fd, event.curlbitmask);
	if(event.reenable) {
		socketpollmessage spm;
		spm.type=SPM_ENABLE;
		spm.fd=event.fd;
		write(pipefd, &spm, sizeof(spm));
	}
}

static void AddPendingEventForSocketPollEvent(int fd, short revents, bool reenable=false) {
	if(!sm.MultiIOHandlerInited) return;

	int sendbitmask=0;
	if(revents&(POLLIN|POLLPRI)) sendbitmask|=CURL_CSELECT_IN;
	if(revents&POLLOUT) sendbitmask|=CURL_CSELECT_OUT;
	if(revents&(POLLERR|POLLHUP)) sendbitmask|=CURL_CSELECT_ERR;

	wxextSocketNotifyEvent event;
	event.curlbitmask=sendbitmask;
	event.fd=fd;
	event.reenable=reenable;

	sm.AddPendingEvent(event);
}

#endif

#ifdef RCS_SIGNALMODE

void socketsighandler(int signum, siginfo_t *info, void *ucontext) {
	AddPendingEventForSocketPollEvent(info->si_fd, info->si_band);
}

void socketmanager::InitMultiIOHandler() {
	if(MultiIOHandlerInited) return;
	InitMultiIOHandlerCommon();

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction=&socketsighandler;
	sa.sa_flags=SA_SIGINFO|SA_RESTART;
	sigfillset(&sa.sa_mask);
	sigaction(SIGRTMIN, &sa, 0);

	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	if(!MultiIOHandlerInited) return;

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler=SIG_IGN;
	sigaction(SIGRTMIN, &sa, 0);

	DeInitMultiIOHandlerCommon();
	MultiIOHandlerInited=false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	if(what) {
		fcntl(s, F_SETOWN, (int) getpid());	//at present, wxWidgets doesn't mask signals around the event critical section
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

#ifdef RCS_POLLTHREADMODE

void socketmanager::InitMultiIOHandler() {
	if(MultiIOHandlerInited) return;
	InitMultiIOHandlerCommon();

	int pipefd[2];
	pipe(pipefd);
	socketpollthread *th=new socketpollthread();
	th->pipefd=pipefd[0];
	this->pipefd=pipefd[1];
	th->Create();
	th->Run();
	LogMsgFormat(LFT_SOCKTRACE, wxT("socketmanager::InitMultiIOHandler(): Created socket poll() thread: %d"), th->GetId());

	MultiIOHandlerInited=true;
}

void socketmanager::DeInitMultiIOHandler() {
	if(!MultiIOHandlerInited) return;

	socketpollmessage spm;
	spm.type=SPM_QUIT;
	write(pipefd, &spm, sizeof(spm));
	close(pipefd);

	DeInitMultiIOHandlerCommon();
	MultiIOHandlerInited=false;
}

void socketmanager::RegisterSockInterest(CURL *e, curl_socket_t s, int what) {
	socketpollmessage spm;
	spm.type=SPM_FDCHANGE;
	spm.fd=s;
	short events=0;
	switch(what) {
		case CURL_POLL_NONE:
		case CURL_POLL_REMOVE:
		default:
			events=0;
			break;
		case CURL_POLL_IN:
			events=POLLIN|POLLPRI;
			break;
		case CURL_POLL_OUT:
			events=POLLOUT;
			break;
		case CURL_POLL_INOUT:
			events=POLLOUT|POLLIN|POLLPRI;
			break;
	}
	spm.events=events;
	write(pipefd, &spm, sizeof(spm));
}

static struct pollfd *getexistpollsetoffsetfromfd(int fd, std::vector<struct pollfd> &pollset) {
	for(size_t i=0; i<pollset.size(); i++) {
		if(pollset[i].fd==fd) return &pollset[i];
	}
	return 0;
}

static size_t insertatendofpollset(int fd, std::vector<struct pollfd> &pollset) {
	pollset.emplace_back();
	size_t offset=pollset.size()-1;
	memset(&pollset[offset], 0, sizeof(pollset[offset]));
	pollset[offset].fd=fd;
	return offset;
}

static size_t getpollsetoffsetfromfd(int fd, std::vector<struct pollfd> &pollset) {
	for(size_t i=0; i<pollset.size(); i++) {
		if(pollset[i].fd==fd) return i;
	}
	return insertatendofpollset(fd, pollset);
}

static void removefdfrompollset(int fd, std::vector<struct pollfd> &pollset) {
	for(size_t i=0; i<pollset.size(); i++) {
		if(pollset[i].fd==fd) {
			if(i!=pollset.size()-1) {
				pollset[i]=pollset[pollset.size()-1];
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
	pollset[0].fd=pipefd;
	pollset[0].events=POLLIN;

	while(true) {
		poll(pollset.data(), pollset.size(), -1);

		for(size_t i=1; i<pollset.size(); i++) {
			if(pollset[i].revents) {
				AddPendingEventForSocketPollEvent(pollset[i].fd, pollset[i].revents, true);

				//remove fd from pollset temporarily to stop it repeatedly re-firing
				disabled_pollset.push_front(pollset[i]);
				if(i!=pollset.size()-1) {
					pollset[i]=pollset[pollset.size()-1];
				}
				pollset.pop_back();
				i--;
			}
		}

		if(pollset[0].revents&POLLIN) {
			socketpollmessage spm;
			size_t bytes_to_read=sizeof(spm);
			size_t bytes_read=0;
			while(bytes_to_read) {
				ssize_t l_bytes_read=read(pipefd, ((char *) &spm)+bytes_read, bytes_to_read);
				if(l_bytes_read>=0) {
					bytes_read+=l_bytes_read;
					bytes_to_read-=l_bytes_read;
				}
				else {
					if(l_bytes_read==EINTR) continue;
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
					disabled_pollset.remove_if([&](const struct pollfd &pfd) { return pfd.fd==spm.fd; });
					if(spm.events) {
						size_t offset=getpollsetoffsetfromfd(spm.fd, pollset);
						pollset[offset].events=spm.events;
					}
					else {
						removefdfrompollset(spm.fd, pollset);
					}
					break;
				case SPM_ENABLE:
					disabled_pollset.remove_if([&](const struct pollfd &pfd) {
						if(pfd.fd==spm.fd) {
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
