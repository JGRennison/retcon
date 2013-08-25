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
#include "version.h"
#include <cstdio>
#include <openssl/sha.h>

globconf gc;
std::list<std::shared_ptr<taccount> > alist;
socketmanager sm;
dbconn dbc;
alldata ad;
std::vector<mainframe*> mainframelist;
std::forward_list<tpanelparentwin_nt*> tpanelparentwinlist;

IMPLEMENT_APP(retcon)

bool retcon::OnInit() {
	raii_set rs;
	//wxApp::OnInit();	//don't call this, it just calls the default command line processor
	SetAppName(appname);
	InitWxLogger();
	rs.add([&]() { DeInitWxLogger(); });
	::wxInitAllImageHandlers();
	srand((unsigned int) time(0));
	cmdlineproc(argv, argc);
	if(!globallogwindow) new log_window(0, lfd_defaultwin, false);
	if(!::wxDirExists(wxStandardPaths::Get().GetUserDataDir())) {
		::wxMkdir(wxStandardPaths::Get().GetUserDataDir(), 0777);
	}
	SetTermSigHandler();
	sm.InitMultiIOHandler();
	rs.add([&]() { sm.DeInitMultiIOHandler(); });
	bool res=dbc.Init(std::string((wxStandardPaths::Get().GetUserDataDir() + wxT("/retcondb.sqlite3")).ToUTF8()));
	if(!res) return false;
	rs.add([&]() { dbc.DeInit(); });
	if(term_requested) return false;

	RestoreWindowLayout();
	if(mainframelist.empty()) new mainframe( appversionname, wxPoint(50, 50), wxSize(450, 340) );

	if(term_requested) return false;

	mainframelist[0]->Show(true);
	SetTopWindow(mainframelist[0]);
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		(*it)->CalcEnabled();
		(*it)->Exec();
	}

	if(term_requested) return false;

	rs.cancel();
	return true;
}

int retcon::OnExit() {
	LogMsg(LFT_OTHERTRACE, wxT("retcon::OnExit"));
	for(auto it=alist.begin() ; it != alist.end(); it++) {
		(*it)->cp.ClearAllConns();
	}
	profileimgdlconn::cp.ClearAllConns();
	sm.DeInitMultiIOHandler();
	dbc.DeInit();
	DeInitWxLogger();
	return wxApp::OnExit();
}

int retcon::FilterEvent(wxEvent& event) {
	static unsigned int antirecursion=0;
	if(antirecursion) return -1;

	antirecursion++;
	#ifdef __WINDOWS__
	if(event.GetEventType()==wxEVT_MOUSEWHEEL) {
		if(GetMainframeAncestor((wxWindow *) event.GetEventObject())) {
			if(RedirectMouseWheelEvent((wxMouseEvent &) event)) {
				antirecursion--;
				return 1;
			}
		}
	}
	#endif
	antirecursion--;

	return -1;
}

BEGIN_EVENT_TABLE(taccount, wxEvtHandler)
    EVT_TIMER(TAF_WINID_RESTTIMER, taccount::OnRestTimer)
    EVT_TIMER(TAF_FAILED_PENDING_CONN_RETRY_TIMER, taccount::OnFailedPendingConnRetryTimer)
    EVT_TIMER(TAF_STREAM_RESTART_TIMER, taccount::OnStreamRestartTimer)
END_EVENT_TABLE()

taccount::taccount(genoptconf *incfg)
	: ta_flags(0), max_tweet_id(0), max_recvdm_id(0), max_sentdm_id(0), last_stream_start_time(0), last_stream_end_time(0), pending_failed_conn_retry_timer(0), stream_restart_timer(0), enabled(false), userenabled(false), init(false),
		active(false), streaming_on(false), stream_fail_count(0), rest_on(false), verifycredstatus(ACT_NOTDONE), beinginsertedintodb(false), last_rest_backfill(0), rest_timer(0)  {
	if(incfg) {
		cfg.InheritFromParent(*incfg);
		CFGParamConv();
	}
}

taccount::~taccount() {
	if(pending_failed_conn_retry_timer) {
		delete pending_failed_conn_retry_timer;
		pending_failed_conn_retry_timer=0;
	}
	if(stream_restart_timer) {
		delete stream_restart_timer;
		stream_restart_timer=0;
	}
	DeleteRestBackfillTimer();
}

void taccount::ClearUsersIFollow() {
	for(auto it=user_relations.begin(); it!=user_relations.end(); ++it) {
		it->second.ur_flags|=URF_IFOLLOW_KNOWN;
		it->second.ur_flags&=~URF_IFOLLOW_TRUE;
		it->second.ifollow_updtime=0;
	}
}

void taccount::SetUserRelationship(uint64_t userid, unsigned int flags, const time_t &optime) {
	user_relationship &ur=user_relations[userid];
	if(flags&URF_FOLLOWSME_KNOWN) {
		ur.ur_flags&=~(URF_FOLLOWSME_PENDING|URF_FOLLOWSME_TRUE);
		ur.ur_flags|=URF_FOLLOWSME_KNOWN|(flags&(URF_FOLLOWSME_TRUE|URF_FOLLOWSME_PENDING));
		ur.followsme_updtime=optime;
	}
	if(flags&URF_IFOLLOW_KNOWN) {
		ur.ur_flags&=~(URF_IFOLLOW_PENDING|URF_IFOLLOW_TRUE);
		ur.ur_flags|=URF_IFOLLOW_KNOWN|(flags&(URF_IFOLLOW_TRUE|URF_IFOLLOW_PENDING));
		ur.ifollow_updtime=optime;
	}
	ur.ur_flags&=~URF_QUERY_PENDING;
}

void taccount::LookupFriendships(uint64_t userid) {
	std::unique_ptr<friendlookup> fl(new friendlookup);
	if(userid) {
		if(user_relations[userid].ur_flags&URF_QUERY_PENDING) return;	//already being looked up, don't repeat query
		fl->ids.insert(userid);
	}

	bool opportunist=true;

	if(opportunist) {
		//find out more if users are followed by us or otherwise have a relationship with us
		for(auto it=user_relations.begin(); it!=user_relations.end() && fl->ids.size()<100; ++it) {
			if(it->second.ur_flags&URF_QUERY_PENDING) continue;
			if(!(it->second.ur_flags&URF_FOLLOWSME_KNOWN) || !(it->second.ur_flags&URF_FOLLOWSME_KNOWN)) {
				std::shared_ptr<userdatacontainer> *usp=ad.GetExistingUserContainerById(it->first);
				if(!(usp && (*usp)->GetUser().u_flags&UF_ISDEAD)) {
					fl->ids.insert(it->first);
					it->second.ur_flags|=URF_QUERY_PENDING;
				}
			}
		}

		//fill up the rest of the query with users who we don't know if we have a relationship with
		for(auto it=ad.userconts.begin(); it!=ad.userconts.end() && fl->ids.size()<100; ++it) {
			if(it->second->GetUser().u_flags&UF_ISDEAD) continue;
			if(user_relations.find(it->first)==user_relations.end()) fl->ids.insert(it->first);
			user_relations[it->first].ur_flags|=URF_QUERY_PENDING;
		}
	}

	if(fl->ids.empty()) return;

	twitcurlext *twit=GetTwitCurlExt();
	twit->connmode=CS_FRIENDLOOKUP;
	twit->fl=std::move(fl);
	twit->genurl=twit->fl->GetTwitterURL();
	twit->QueueAsyncExec();
}

void taccount::OnRestTimer(wxTimerEvent& event) {
	SetupRestBackfillTimer();
}

void taccount::SetupRestBackfillTimer() {
	if(!rest_on) return;
	if(!rest_timer) rest_timer=new wxTimer(this, TAF_WINID_RESTTIMER);
	time_t now=time(0);
	time_t targettime=last_rest_backfill+restinterval;
	int timeleft;
	if(targettime<=(now+10)) {				//10s of error margin
		GetRestBackfill();
		timeleft=restinterval;
	}
	else {
		timeleft=targettime-now;
	}
	LogMsgFormat(LFT_OTHERTRACE, wxT("Setting REST timer for %d seconds (%s)"), timeleft, dispname.c_str());
	rest_timer->Start(timeleft*1000, wxTIMER_ONE_SHOT);
}

void taccount::DeleteRestBackfillTimer() {
	if(rest_timer) {
		LogMsgFormat(LFT_OTHERTRACE, wxT("Deleting REST timer (%s)"), dispname.c_str());
		delete rest_timer;
		rest_timer=0;
	}
}

void taccount::GetRestBackfill() {
	auto oktostart=[&](RBFS_TYPE type) {
		for(auto it=cp.activeset.begin(); it!=cp.activeset.end(); ++it) {
			if((*it)->rbfs && (*it)->rbfs->type==type && (*it)->rbfs->end_tweet_id==0) {
				return false;	//already present
			}
		}
		return true;
	};

	last_rest_backfill=time(0);
	if(oktostart(RBFS_TWEETS)) StartRestGetTweetBackfill(GetMaxId(RBFS_TWEETS), 0, 800, RBFS_TWEETS);
	if(oktostart(RBFS_RECVDM)) StartRestGetTweetBackfill(GetMaxId(RBFS_RECVDM), 0, 800, RBFS_RECVDM);
	if(oktostart(RBFS_SENTDM)) StartRestGetTweetBackfill(GetMaxId(RBFS_SENTDM), 0, 800, RBFS_SENTDM);
	if(!gc.assumementionistweet) {
		if(oktostart(RBFS_MENTIONS)) StartRestGetTweetBackfill(GetMaxId(RBFS_MENTIONS), 0, 800, RBFS_MENTIONS);
	}
}

//limits are inclusive
void taccount::StartRestGetTweetBackfill(uint64_t start_tweet_id, uint64_t end_tweet_id, unsigned int max_tweets_to_read, RBFS_TYPE type, uint64_t userid) {
	pending_rbfs_list.emplace_front();
	restbackfillstate *rbfs=&pending_rbfs_list.front();
	rbfs->start_tweet_id=start_tweet_id;
	rbfs->end_tweet_id=end_tweet_id;
	rbfs->max_tweets_left=max_tweets_to_read;
	rbfs->read_again=true;
	rbfs->type=type;
	rbfs->started=false;
	rbfs->lastop_recvcount=0;
	rbfs->userid=userid;
	ExecRBFS(rbfs);
}

void taccount::ExecRBFS(restbackfillstate *rbfs) {
	if(rbfs->started) return;
	twitcurlext *twit=GetTwitCurlExt();
	switch(rbfs->type) {
		case RBFS_TWEETS:
		case RBFS_MENTIONS:
			twit->connmode=CS_TIMELINE;
			break;
		case RBFS_RECVDM:
		case RBFS_SENTDM:
			twit->connmode=CS_DMTIMELINE;
			break;
		case RBFS_USER_TIMELINE:
			twit->connmode=CS_USERTIMELINE;
			break;
		case RBFS_USER_FAVS:
			twit->connmode=CS_USERFAVS;
			break;
	}
	twit->SetNoPerformFlag(true);
	twit->rbfs=rbfs;
	twit->post_action_flags=PAF_RESOLVE_PENDINGS;
	twit->ExecRestGetTweetBackfill();
}

void taccount::StartRestQueryPendings() {
	LogMsgFormat(LFT_PENDTRACE, wxT("taccount::StartRestQueryPendings: pending users: %d, (%s)"), pendingusers.size(), dispname.c_str());
	if(pendingusers.empty()) return;

	std::unique_ptr<userlookup> ul;

	auto it=pendingusers.begin();
	while(it!=pendingusers.end()) {
		unsigned int numusers=0;
		while(it!=pendingusers.end() && numusers<100) {
			auto curit=it;
			std::shared_ptr<userdatacontainer> curobj=curit->second;
			it++;
			if(curobj->udc_flags&UDC_LOOKUP_IN_PROGRESS) ;	//do nothing
			else if(curobj->NeedsUpdating(UPDCF_USEREXPIRE) || curobj->udc_flags&UDC_FORCE_REFRESH) {
				if(!ul) ul.reset(new userlookup());
				ul->Mark(curobj);
				numusers++;
			}
			else {
				pendingusers.erase(curit);		//user not pending, remove from list
				curobj->CheckPendingTweets();
			}
			curobj->udc_flags&=~UDC_FORCE_REFRESH;
		}
		if(numusers && ul) {
			twitcurlext *twit=GetTwitCurlExt();
			twit->connmode=CS_USERLIST;
			twit->ul=std::move(ul);
			ul=0;
			twit->post_action_flags=PAF_RESOLVE_PENDINGS;
			twit->QueueAsyncExec();
		}
	}
}

void taccount::DoPostAction(twitcurlext *lasttce) {
	unsigned int postflags=lasttce->post_action_flags;
	cp.Standby(lasttce);
	DoPostAction(postflags);
}

void taccount::DoPostAction(unsigned int postflags) {
	if(postflags&PAF_RESOLVE_PENDINGS) {
		StartRestQueryPendings();
	}
}

bool taccount::TwDoOAuth(wxWindow *pf, twitcurlext &twit) {
	std::string authUrl;
	twit.SetNoPerformFlag(false);
	twit.oAuthRequestToken(authUrl);
	wxString authUrlWx=wxString::FromUTF8(authUrl.c_str());
	//twit.oAuthHandlePIN(authUrl);
	LogMsgFormat(LFT_OTHERTRACE, wxT("taccount::TwDoOAuth: %s, %s, %s"), cfg.tokenk.val.c_str(), cfg.tokens.val.c_str(), authUrlWx.c_str());
	wxLaunchDefaultBrowser(authUrlWx);
	wxTextEntryDialog *ted=new wxTextEntryDialog(pf, wxT("Enter Twitter PIN"), wxT("Enter Twitter PIN"), wxT(""), wxOK | wxCANCEL);
	int res=ted->ShowModal();
	wxString pin=ted->GetValue();
	ted->Destroy();
	if(res!=wxID_OK) return false;
	if(pin.IsEmpty()) return false;
	twit.getOAuth().setOAuthPin((const char*) pin.utf8_str());
	twit.oAuthAccessToken();
	std::string stdconk;
	std::string stdcons;
	twit.getOAuth().getOAuthTokenKey(stdconk);
	twit.getOAuth().getOAuthTokenSecret(stdcons);
	conk=wxString::FromUTF8(stdconk.c_str());
	cons=wxString::FromUTF8(stdcons.c_str());
	return true;
}

void taccount::PostAccVerifyInit() {
	verifycredstatus=ACT_DONE;
	CalcEnabled();
	Exec();
}

void taccount::Exec() {
	if(init) {
		if(verifycredstatus!=ACT_DONE) {
			streaming_on=false;
			rest_on=false;
			active=false;
			if(verifycredstatus==ACT_INPROGRESS) return;
			twitcurlext *twit=GetTwitCurlExt();
			twit->TwStartupAccVerify();
		}
	}
	else if(enabled) {
		bool target_streaming=userstreams && !stream_fail_count;
		if(!active) {
			for(auto it=pending_rbfs_list.begin(); it!=pending_rbfs_list.end(); ++it) {
				ExecRBFS(&(*it));
			}
		}
		else {
			if(!target_streaming) {
				for(auto it=cp.activeset.begin(); it!=cp.activeset.end(); ++it) {
					if((*it)->tc_flags&TCF_ISSTREAM) {
						(*it)->KillConn();
						cp.Standby(*it);	//kill stream, note this also modifies cp.activeset
						break;
					}
				}
				streaming_on=false;
			}
			else if(target_streaming && rest_on) {
				DeleteRestBackfillTimer();
				rest_on=false;
			}
		}
		active=true;

		if(target_streaming && !streaming_on) {
			streaming_on=true;
			twitcurlext *twit_stream=PrepareNewStreamConn();
			twit_stream->QueueAsyncExec();
		}
		if(!target_streaming && !rest_on) {
			rest_on=true;
			SetupRestBackfillTimer();
		}
	}
	else if(!enabled && (active || (verifycredstatus==ACT_INPROGRESS))) {
		active=false;
		verifycredstatus=ACT_NOTDONE;
		streaming_on=false;
		rest_on=false;
		failed_pending_conns.clear();
		cp.ClearAllConns();
	}
}

twitcurlext *taccount::PrepareNewStreamConn() {
	twitcurlext *twit_stream=GetTwitCurlExt();
	twit_stream->connmode=CS_STREAM;
	twit_stream->tc_flags|=TCF_ISSTREAM;
	twit_stream->post_action_flags|=PAF_STREAM_CONN_READ_BACKFILL;
	return twit_stream;
}

void taccount::CalcEnabled() {
	bool oldenabled=enabled;
	bool oldinit=init;
	if(userenabled && !beinginsertedintodb) {
		enabled=(verifycredstatus==ACT_DONE);
		init=!enabled;
	}
	else {
		enabled=false;
		init=false;
	}

	if(oldenabled!=enabled || oldinit!=init) {
		AccountChangeTrigger();
	}
}

void taccount::MarkUserPending(const std::shared_ptr<userdatacontainer> &user) {
	auto retval=pendingusers.insert(std::make_pair(user->id, user));
	if(retval.second) {
		LogMsgFormat(LFT_PENDTRACE, wxT("Mark Pending: User: %" wxLongLongFmtSpec "d (@%s) for account: %s (%s)"), user->id, wxstrstd(user->GetUser().screen_name).c_str(), name.c_str(), dispname.c_str());
	}
}

wxString taccount::GetStatusString(bool notextifok) {
	if(init) {
		if(verifycredstatus==ACT_FAILED) return wxT("authentication failed");
		else return wxT("authenticating");
	}
	else if(!userenabled) return wxT("disabled");
	else if(!enabled) return wxT("not active");
	else if(!notextifok) return wxT("active");
	else return wxT("");
}

void taccount::CheckFailedPendingConns() {
	if(!failed_pending_conns.empty()) {			//only try one, to avoid excessive connection cycling
								//if it is successful the remainder will be successively queued
		failed_pending_conns.front()->QueueAsyncExec();
		failed_pending_conns.pop_front();
	}
	if(pending_failed_conn_retry_timer) pending_failed_conn_retry_timer->Stop();
	//LogMsgFormat(LFT_SOCKERR, wxT("taccount::CheckFailedPendingConns(), stream_fail_count: %d, enabled: %d, userstreams: %d, streaming_on: %d, for account: %s"), stream_fail_count, enabled, userstreams, streaming_on, dispname.c_str());
	if(stream_fail_count && enabled && userstreams && !streaming_on) {
		if(!stream_restart_timer) stream_restart_timer=new wxTimer(this, TAF_STREAM_RESTART_TIMER);
		if(!stream_restart_timer->IsRunning()) stream_restart_timer->Start(90*1000, wxTIMER_ONE_SHOT);	//give a little time for any other operations to try to connect first
	}
}

void taccount::AddFailedPendingConn(twitcurlext *conn) {
	//LogMsgFormat(LFT_SOCKERR, wxT("Connection failed (account: %s). Next reconnection attempt in 512 seconds, or upon successful network activity on this account (whichever is first)."), dispname.c_str());
	failed_pending_conns.push_back(conn);
	if(!pending_failed_conn_retry_timer) pending_failed_conn_retry_timer=new wxTimer(this, TAF_FAILED_PENDING_CONN_RETRY_TIMER);
	if(!pending_failed_conn_retry_timer->IsRunning()) pending_failed_conn_retry_timer->Start(512*1000, wxTIMER_ONE_SHOT);
}

void taccount::OnFailedPendingConnRetryTimer(wxTimerEvent& event) {
	CheckFailedPendingConns();
}

void taccount::OnStreamRestartTimer(wxTimerEvent& event) {
	//LogMsgFormat(LFT_SOCKERR, wxT("taccount::OnStreamRestartTimer(), stream_fail_count: %d, enabled: %d, userstreams: %d, streaming_on: %d, for account: %s"), stream_fail_count, enabled, userstreams, streaming_on, dispname.c_str());
	for(auto it=cp.activeset.begin(); it!=cp.activeset.end(); ++it) {
		if((*it)->tc_flags&TCF_ISSTREAM) {
			//LogMsgFormat(LFT_SOCKERR, wxT("taccount::OnStreamRestartTimer(), stream connection already active, aborting"));
			return;				//stream connection already present
		}
	}

	if(stream_fail_count && enabled && userstreams && !streaming_on) {
		twitcurlext *twit_stream=PrepareNewStreamConn();
		twit_stream->errorcount=255;	//disable retry attempts
		twit_stream->QueueAsyncExec();
	}
}

twitcurlext *taccount::GetTwitCurlExt() {
	twitcurlext *twit=cp.GetConn();
	twit->TwInit(shared_from_this());
	if(TwitCurlExtHook) TwitCurlExtHook(twit);
	return twit;
}

void taccount::SetGetTwitCurlExtHook(std::function<void(twitcurlext *)> func) {
	TwitCurlExtHook = std::move(func);
}

void taccount::ClearGetTwitCurlExtHook() {
	TwitCurlExtHook = nullptr;
}

bool GetAccByDBIndex(unsigned int dbindex, std::shared_ptr<taccount> &acc) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		if((*it)->dbindex==dbindex) {
			acc=(*it);
			return true;
		}
	}
	return false;
}

void AccountChangeTrigger() {
	user_window::RefreshAll();
	AccountUpdateAllMainframes();
	for(auto it=acc_window::currentset.begin(); it!=acc_window::currentset.end(); ++it) {
		(*it)->UpdateLB();
	}
}

std::shared_ptr<userdatacontainer> &alldata::GetUserContainerById(uint64_t id) {
	std::shared_ptr<userdatacontainer> &usercont=userconts[id];
	if(!usercont) {
		usercont=std::make_shared<userdatacontainer>();
		usercont->id=id;
		usercont->lastupdate=0;
		usercont->udc_flags=0;
		memset(usercont->cached_profile_img_sha1, 0, sizeof(usercont->cached_profile_img_sha1));
	}
	return usercont;
}

std::shared_ptr<userdatacontainer> *alldata::GetExistingUserContainerById(uint64_t id) {
	auto it=userconts.find(id);
	if(it!=userconts.end()) {
		return &(it->second);
	}
	else {
		return 0;
	}
}

std::shared_ptr<tweet> &alldata::GetTweetById(uint64_t id, bool *isnew) {
	std::shared_ptr<tweet> &t=tweetobjs[id];
	if(isnew) *isnew=(!t);
	if(!t) {
		t=std::make_shared<tweet>();
		t->id=id;
	}
	return t;
}

std::shared_ptr<tweet> *alldata::GetExistingTweetById(uint64_t id) {
	auto it=tweetobjs.find(id);
	if(it!=tweetobjs.end()) {
		return &(it->second);
	}
	else {
		return 0;
	}
}

void alldata::UnlinkTweetById(uint64_t id) {
	tweetobjs.erase(id);
}

std::string hexify(const std::string &in) {
	const char hex[]="0123456789ABCDEF";
	size_t len = in.length();
	std::string out;
	out.reserve(2*len);
	for(size_t i=0; i<len; i++) {
		const unsigned char c = (const unsigned char) in[i];
		out.push_back(hex[c>>4]);
		out.push_back(hex[c&15]);
	}
	return out;
}

wxString hexify_wx(const std::string &in) {
	const wxChar hex[]=wxT("0123456789ABCDEF");
	size_t len = in.length();
	wxString out;
	out.Alloc(2*len);
	for(size_t i=0; i<len; i++) {
		const unsigned char c = (const unsigned char) in[i];
		out.Append(hex[c>>4]);
		out.Append(hex[c&15]);
	}
	return out;
}

bool LoadFromFileAndCheckHash(const wxString &filename, const unsigned char *hash, char *&data, size_t &size) {
	wxFile file;
	bool opened=file.Open(filename);
	if(opened) {
		wxFileOffset len=file.Length();
		if(len && len<(50<<20)) {	//don't load empty or absurdly large files
			data=(char*) malloc(len);
			size=file.Read(data, len);
			if(size==len) {
				unsigned char curhash[20];
				SHA1((const unsigned char *) data, (unsigned long) len, curhash);
				if(memcmp(curhash, hash, 20)==0) {
					return true;
				}
			}
			free(data);
		}
	}
	data=0;
	size=0;
	return false;
}

bool LoadImageFromFileAndCheckHash(const wxString &filename, const unsigned char *hash, wxImage &img) {
	char *data=0;
	size_t size;
	bool success=false;
	if(LoadFromFileAndCheckHash(filename, hash, data, size)) {
		wxMemoryInputStream memstream(data, size);
		if(img.LoadFile(memstream, wxBITMAP_TYPE_ANY)) {
			success=true;
		}
	}
	if(data) free(data);
	return success;
}
