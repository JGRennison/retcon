#include "retcon.h"

#include "timegm.cpp"
#ifdef __WINDOWS__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "strptime.cpp"
#pragma GCC diagnostic pop
#endif

void HandleNewTweet(const std::shared_ptr<tweet> &t) {
	//do some filtering, etc
	ad.tpanels["[default]"]->PushTweet(t);
}

void UpdateTweet(const std::shared_ptr<tweet> &t) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		for(auto jt=(*it)->currentdisp.begin(); jt!=(*it)->currentdisp.end(); ++jt) {
			if(jt->first==t->id) {	//found matching entry
				wxLogWarning(wxT("UpdateTweet: Found Entry %" wxLongLongFmtSpec "d."), t->id);
				jt->second->DisplayTweet();
				break;
			}
		}
	}
}

userlookup::~userlookup() {
	UnMarkAll();
}

void userlookup::UnMarkAll() {
	while(!users_queried.empty()) {
		users_queried.front()->udc_flags&=~UDC_LOOKUP_IN_PROGRESS;
		users_queried.pop_front();
	}
}

void userlookup::Mark(std::shared_ptr<userdatacontainer> udc) {
	udc->udc_flags|=UDC_LOOKUP_IN_PROGRESS;
	users_queried.push_front(udc);
}

void userlookup::GetIdList(std::string &idlist) {
	idlist.clear();
	if(users_queried.empty()) return;
	auto it=users_queried.cbegin();
	while(true) {
		idlist+=std::to_string((*it)->id);
		if(it!=users_queried.cend()) break;
		idlist+=",";
		it++;
	}
}

BEGIN_EVENT_TABLE( twitcurlext, mcurlconn )
END_EVENT_TABLE()

void twitcurlext::NotifyDoneSuccess(CURL *easy, CURLcode res) {
	std::shared_ptr<taccount> acc=tacc.lock();
	if(!acc) return;

	jsonparser jp(connmode, acc, this);
	std::string str;
	getLastWebResponseMove(str);
	jp.ParseString((char*) str.c_str());	//this modifies the contents of str!!
	str.clear();

	KillConn();
	if(connmode==CS_STREAM && acc->enabled) {
		DoRetry();
	}
	else if(rbfs) {
		ExecRestGetTweetBackfill();
	}
	else {
		acc->DoPostAction(this);
	}
}

void twitcurlext::ExecRestGetTweetBackfill() {
	unsigned int tweets_to_get=std::min((unsigned int) 200, rbfs->max_tweets_left);
	if(rbfs->start_tweet_id>rbfs->end_tweet_id || !tweets_to_get || !rbfs->read_again) {
		auto acc=tacc.lock();
		if(!acc) delete this;
		else {
			acc->DoPostAction(this);
		}
	}
	else {
		struct timelineparams tmps={
			tweets_to_get,
			rbfs->start_tweet_id,
			rbfs->end_tweet_id,
			1,
			1,
			1,
			0
		};
		timelineHomeGet(tmps);
		sm.AddConn(*this);
	}
}

twitcurlext::twitcurlext(std::shared_ptr<taccount> acc) {
	inited=false;
	post_action_flags=0;
	TwInit(acc);
}

twitcurlext::twitcurlext() {
	inited=false;
	post_action_flags=0;
}
twitcurlext::~twitcurlext() {
	TwDeInit();
}

void twitcurlext::TwInit(std::shared_ptr<taccount> acc) {
	if(inited) return;
	tacc=acc;
	#ifdef __WINDOWS__
	curl_easy_setopt(GetCurlHandle(), CURLOPT_CAINFO, "./cacert.pem");
	#endif
	if(sm.loghandle) setlog(sm.loghandle, 1);

	setTwitterApiType(twitCurlTypes::eTwitCurlApiFormatJson);
	setTwitterProcotolType(acc->ssl?twitCurlTypes::eTwitCurlProtocolHttps:twitCurlTypes::eTwitCurlProtocolHttp);

	getOAuth().setConsumerKey((const char*) acc->cfg.tokenk.val.utf8_str());
	getOAuth().setConsumerSecret((const char*) acc->cfg.tokens.val.utf8_str());

	if(acc->conk.size() && acc->cons.size()) {
		getOAuth().setOAuthTokenKey((const char*) acc->conk.utf8_str());
		getOAuth().setOAuthTokenSecret((const char*) acc->cons.utf8_str());
	}
	inited=true;
}

void twitcurlext::TwDeInit() {
	inited=false;
}

void twitcurlext::TwStartupAccVerify() {
	tacc.lock()->verifycredinprogress=true;
	connmode=CS_ACCVERIFY;
	QueueAsyncExec();
}

bool twitcurlext::TwSyncStartupAccVerify() {
	tacc.lock()->verifycredinprogress=true;
	SetNoPerformFlag(false);
	accountVerifyCredGet();
	long httpcode;
	curl_easy_getinfo(GetCurlHandle(), CURLINFO_RESPONSE_CODE, &httpcode);
	if(httpcode==200) {
		jsonparser jp(CS_ACCVERIFY, tacc.lock(), this);
		std::string str;
		getLastWebResponse(str);
		jp.ParseString((char*) str.c_str());	//this modifies the contents of str!!
		str.clear();
		tacc.lock()->verifycredinprogress=false;
		return true;
	}
	else {
		tacc.lock()->verifycredinprogress=false;
		return false;
	}
}

void twitcurlext::Reset() {
	scto.reset();
	rbfs.reset();
	ul.reset();
	post_action_flags=0;
}

void twitcurlext::DoRetry() {
	QueueAsyncExec();
}

void twitcurlext::QueueAsyncExec() {
	SetNoPerformFlag(true);
	switch(connmode) {
		case CS_ACCVERIFY:
			wxLogWarning(wxT("Queue AccVerify"));
			accountVerifyCredGet();
			break;
		case CS_TIMELINE:
			return ExecRestGetTweetBackfill();
		case CS_STREAM:
			wxLogWarning(wxT("Queue Stream Connection"));
			mcflags|=MCF_NOTIMEOUT;
			scto=std::make_shared<streamconntimeout>(this);
			SetStreamApiCallback(&StreamCallback, 0);
			SetStreamApiActivityCallback(&StreamActivityCallback);
			UserStreamingApi("followings");
			break;
		case CS_USERLIST: {
			std::string userliststr;
			ul->GetIdList(userliststr);
			if(userliststr.empty()) {	//nothing left to look up
				auto acc=tacc.lock();
				if(!acc) delete this;
				else {
					acc->cp.Standby(this);
				}
				return;
			}
			userLookup(userliststr, "", 0);
			break;
			}
	}
	sm.AddConn(*this);
}

void twitcurlext::HandleFailure() {

}

void streamconntimeout::Arm() {
	Start(90000, wxTIMER_ONE_SHOT);
}

void streamconntimeout::Notify() {
	tw->KillConn();
	tw->HandleError(tw->GetCurlHandle(),0,CURLE_OPERATION_TIMEDOUT);
}

bool userdatacontainer::NeedsUpdating() {
	if(!lastupdate) return true;
	else {
		if((wxGetUTCTime()-lastupdate)>gc.userexpiretime) return true;
		else return false;
	}
}

bool userdatacontainer::IsReady() {
	if(NeedsUpdating()) return false;
	else if( udc_flags & (UDC_LOOKUP_IN_PROGRESS|UDC_IMAGE_DL_IN_PROGRESS)) return false;
	else return true;
}

void userdatacontainer::CheckPendingTweets() {
	if(IsReady()) {
		pendingtweets.remove_if([&](const std::shared_ptr<tweet> &t) {
			if(!t->flags.Get('D')) {
				HandleNewTweet(t);
				return true;
			}
			else {
				if(t->user->IsReady() && t->user_recipient->IsReady()) {
					HandleNewTweet(t);
					return true;
				}
				else {
					if(!t->user->IsReady()) t->tp_list.front().acc->MarkPending(t->user->id, t->user, t, true);
					if(!t->user_recipient->IsReady()) t->tp_list.front().acc->MarkPending(t->user_recipient->id, t->user_recipient, t, true);
					return false;
				}
			}
		});
	}
}

std::shared_ptr<taccount> userdatacontainer::GetAccountOfUser() {
	for(auto it=alist.begin() ; it != alist.end(); it++ ) if( (*it)->usercont.get()==this ) return *it;
	return std::shared_ptr<taccount>();
}

void userdatacontainer::GetImageLocalFilename(wxString &filename) {
	filename.Printf(wxT("/img_%" wxLongLongFmtSpec "d"), id);
	filename.Prepend(wxStandardPaths::Get().GetUserDataDir());
}

void userdatacontainer::MarkUpdated() {
	lastupdate=wxGetUTCTime();
	if(user.profile_img_url.size()) {
		if(cached_profile_img_url!=user.profile_img_url) {
			profileimgdlconn::GetConn(user.profile_img_url, shared_from_this());
		}
	}
}

std::string tweet_flags::GetString() {
	std::string out;
	uint64_t bitint=bits.to_ullong();
	while(bitint) {
		int offset=__builtin_ctzll(bitint);
		bitint&=~((uint64_t) 1<<offset);
		out+=GetFlagChar(offset);
	}
	return out;
}

tweet_perspective *tweet::AddTPToTweet(std::shared_ptr<taccount> &tac, bool *isnew) {
	for(auto it=tp_list.begin(); it!=tp_list.end(); it++) {
		if(it->acc.get()==tac.get()) {
			if(isnew) *isnew=false;
			return &(*it);
		}
	}
	tp_list.emplace_front(tac);
	if(isnew) *isnew=true;
	return &tp_list.front();
}

void StreamCallback( std::string &data, twitCurl* pTwitCurlObj, void *userdata ) {
	twitcurlext *obj=(twitcurlext*) pTwitCurlObj;
	std::shared_ptr<taccount> acc=obj->tacc.lock();

	wxLogWarning(wxT("Received: %s"), wxstrstd(data).c_str());
	jsonparser jp(CS_STREAM, acc, obj);
	jp.ParseString((char*) data.c_str());	//this modifies the contents of data!!
	data.clear();
}

void StreamActivityCallback( twitCurl* pTwitCurlObj, void *userdata ) {
	twitcurlext *obj=(twitcurlext*) pTwitCurlObj;
	obj->scto->Arm();
	wxLogWarning(wxT("Reset timeout on stream connection %p"), obj);
}

#ifdef __WINDOWS__
struct tm *gmtime_r (const time_t *timer, struct tm *result) {
	struct tm *local_result;
	local_result = gmtime(timer);

	if(!local_result || !result) return 0;

	memcpy (result, local_result, sizeof (struct tm));
	return result;
}
#endif

//wxDateTime performs some braindead timezone adjustments and so is unusable
//mktime and friends also have onerous timezone behaviour
//use strptime and an implementation of timegm instead
void ParseTwitterDate(struct tm *createtm, time_t *createtm_t, const std::string &created_at) {
	struct tm tmp_tm;
	time_t tmp_time;
	if(!createtm) createtm=&tmp_tm;
	if(!createtm_t) createtm_t=&tmp_time;

	memset(createtm, 0, sizeof(struct tm));
	*createtm_t=0;
	strptime(created_at.c_str(), "%a %b %d %T +0000 %Y", createtm);
	*createtm_t=rttimegm(createtm);
}
