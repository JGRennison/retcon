#include "retcon.h"

#ifdef __WINDOWS__
#include "timegm.cpp"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "strptime.cpp"
#pragma GCC diagnostic pop
#endif
#include <openssl/sha.h>

void HandleNewTweet(const std::shared_ptr<tweet> &t) {
	//do some filtering, etc
	ad.tpanels["[default]"]->PushTweet(t);

	for(auto it=ad.tpanels.begin(); it!=ad.tpanels.end(); ++it) {
		tpanel &tp=*(it->second);
		if(tp.flags&TPF_ISAUTO) {
			if((tp.flags&TPF_AUTO_DM && t->flags.Get('D')) || (tp.flags&TPF_AUTO_TW && t->flags.Get('T'))) {
				if(tp.flags&TPF_AUTO_ALLACCS) tp.PushTweet(t);
				else if(tp.flags&TPF_AUTO_ACC) {
					for(auto jt=t->tp_list.begin(); jt!=t->tp_list.end(); ++jt) {
						if((*jt).acc.get()==tp.assoc_acc.get() && (*jt).IsArrivedHere()) {
							tp.PushTweet(t);
							break;
						}
					}
				}
			}
		}
	}
}

void UpdateTweet(const std::shared_ptr<tweet> &t, bool redrawimg) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		for(auto jt=(*it)->currentdisp.begin(); jt!=(*it)->currentdisp.end(); ++jt) {
			if(jt->first==t->id) {	//found matching entry
				LogMsgFormat(LFT_TPANEL, wxT("UpdateTweet: Found Entry %" wxLongLongFmtSpec "d."), t->id);
				jt->second->DisplayTweet(redrawimg);
				break;
			}
		}
	}
}

void UpdateUsersTweet(uint64_t userid, bool redrawimg) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		for(auto jt=(*it)->currentdisp.begin(); jt!=(*it)->currentdisp.end(); ++jt) {
			tweetdispscr &tds=*(jt->second);
			if((tds.td->user && tds.td->user->id==userid)
			 || (tds.td->user_recipient && tds.td->user_recipient->id==userid)) {
				LogMsgFormat(LFT_TPANEL, wxT("UpdateUsersTweet: Found Entry %" wxLongLongFmtSpec "d."), jt->first);
				jt->second->DisplayTweet(redrawimg);
				break;
			}
		}
	}
}

void UpdateAllTweets(bool redrawimg) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		for(auto jt=(*it)->currentdisp.begin(); jt!=(*it)->currentdisp.end(); ++jt) {
			jt->second->DisplayTweet(redrawimg);
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
	jp.ParseString(str);
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
	auto acc=tacc.lock();
	if(!acc) delete this;

	bool cleanup=false;
	unsigned int tweets_to_get=std::min((unsigned int) 200, rbfs->max_tweets_left);
	if((rbfs->end_tweet_id && rbfs->start_tweet_id>rbfs->end_tweet_id) || !rbfs->read_again) {
		cleanup=true;
	}
	else if(!tweets_to_get) {
		if(rbfs->type==RBFS_TWEETS) {
			rbfs->max_tweets_left=800;
			tweets_to_get=200;
			rbfs->type=RBFS_MENTIONS;
		}
		else cleanup=true;
	}

	if(cleanup) {
		//all done, can now clean up pending rbfs
		acc->pending_rbfs_list.remove_if([&](restbackfillstate &r) { return (&r==rbfs); });
		rbfs=0;
		acc->DoPostAction(this);
	}
	else {
		rbfs->read_again=false;
		struct timelineparams tmps={
			tweets_to_get,
			rbfs->start_tweet_id,
			rbfs->end_tweet_id,
			(signed char) ((rbfs->type==RBFS_TWEETS || rbfs->type==RBFS_MENTIONS)?1:0),
			(signed char) ((rbfs->type==RBFS_TWEETS || rbfs->type==RBFS_MENTIONS)?1:0),
			1,
			0
		};
		LogMsgFormat(LFT_TWITACT, wxT("acc: %s, type: %d, num: %d, start_id: %" wxLongLongFmtSpec "d, end_id: %" wxLongLongFmtSpec "d"),
			acc->dispname.c_str(), rbfs->type, tweets_to_get, rbfs->start_tweet_id, rbfs->end_tweet_id);

		switch(rbfs->type) {
			case RBFS_TWEETS:
				timelineHomeGet(tmps);
				break;
			case RBFS_MENTIONS:
				mentionsGet(tmps);
				break;
			case RBFS_RECVDM:
				directMessageGet(tmps);
				break;
			case RBFS_SENTDM:
				directMessageGetSent(tmps);
				break;
		}
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
	rbfs=0;
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
		jp.ParseString(str);
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
	rbfs=0;
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
			LogMsgFormat(LFT_TWITACT, wxT("Queue AccVerify"));
			accountVerifyCredGet();
			break;
		case CS_TIMELINE:
		case CS_DMTIMELINE:
			return ExecRestGetTweetBackfill();
		case CS_STREAM:
			LogMsgFormat(LFT_TWITACT, wxT("Queue Stream Connection"));
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

bool userdatacontainer::NeedsUpdating(unsigned int updcf_flags) {
	if(!lastupdate) return true;
	else if(updcf_flags&UPDCF_NOUSEREXPIRE && GetUser().screen_name.size()) return false;
	else {
		if((time(0)-lastupdate)>gc.userexpiretime) return true;
		else return false;
	}
}

bool userdatacontainer::ImgIsReady(unsigned int updcf_flags) {
	if(cached_profile_img_url.size() && !(udc_flags&UDC_PROFILE_BITMAP_SET))  {
		wxImage img;
		wxString filename;
		bool success=false;
		GetImageLocalFilename(filename);
		wxFile file;
		bool opened=file.Open(filename);
		if(opened) {
			wxFileOffset len=file.Length();
			if(len && len<(50<<20)) {	//don't load empty absurdly large files
				char *data=(char*) malloc(len);
				size_t res=file.Read(data, len);
				if(res==len) {
					unsigned char hash[20];
					SHA1((const unsigned char *) data, (unsigned long) len, hash);
					if(memcmp(hash, cached_profile_img_sha1, 20)==0) {
						wxMemoryInputStream memstream(data, len);
						if(img.LoadFile(memstream, wxBITMAP_TYPE_ANY)) {
							SetProfileBitmapFromwxImage(img);
							success=true;
						}
					}
				}
				free(data);
			}
		}
		if(!success) {
			LogMsgFormat(LFT_OTHERERR, wxT("userdatacontainer::ImgIsReady, cached profile image file for user id: %" wxLongLongFmtSpec "d (%s), file: %s, url: %s, missing, invalid or failed hash check"),
				id, wxstrstd(GetUser().screen_name).c_str(), filename.c_str(), wxstrstd(cached_profile_img_url).c_str());
			cached_profile_img_url.clear();
			if(updcf_flags&UPDCF_DOWNLOADIMG) {					//the saved image is not loadable, clear cache and re-download
				profileimgdlconn::GetConn(user.profile_img_url, shared_from_this());
			}
			return false;
		}
	}
	if(udc_flags & UDC_IMAGE_DL_IN_PROGRESS) return false;
	return true;
}

bool userdatacontainer::ImgHalfIsReady(unsigned int updcf_flags) {
	bool res=ImgIsReady(updcf_flags);
	if(res && !(udc_flags&UDC_HALF_PROFILE_BITMAP_SET)) {
		wxImage img=cached_profile_img.ConvertToImage();
		cached_profile_img_half=MkProfileBitmapFromwxImage(img, 0.5);
		udc_flags|=UDC_HALF_PROFILE_BITMAP_SET;
	}
	return res;
}

bool userdatacontainer::IsReady(unsigned int updcf_flags) {
	if(!ImgIsReady(updcf_flags)) return false;
	if(NeedsUpdating(updcf_flags)) return false;
	else if( updcf_flags&UPDCF_NOUSEREXPIRE ) return true;
	else if( udc_flags & (UDC_LOOKUP_IN_PROGRESS|UDC_IMAGE_DL_IN_PROGRESS)) return false;
	else return true;
}

void userdatacontainer::CheckPendingTweets() {
	if(IsReady()) {
		FreezeAll();
		pendingtweets.remove_if([&](const std::shared_ptr<tweet> &t) {
			if(!t->flags.Get('D')) {
				UnmarkPending(t);
				return true;
			}
			else {
				if(t->user->IsReady() && t->user_recipient->IsReady()) {
					UnmarkPending(t);
					return true;
				}
				else {
					if(!t->user->IsReady()) t->tp_list.front().acc->MarkPending(t->user->id, t->user, t, true);
					if(!t->user_recipient->IsReady()) t->tp_list.front().acc->MarkPending(t->user_recipient->id, t->user_recipient, t, true);
					return false;
				}
			}
		});
		ThawAll();
	}
}

void userdatacontainer::UnmarkPending(const std::shared_ptr<tweet> &t) {
	if(t->lflags&TLF_PENDINGHANDLENEW) {
		t->lflags&=~TLF_PENDINGHANDLENEW;
		HandleNewTweet(t);
	}
	if(t->lflags&TLF_PENDINGINDBTPANELMAP) {
		t->lflags&=~TLF_PENDINGINDBTPANELMAP;
		t->lflags&=~TLF_BEINGLOADEDFROMDB;
		auto itpair=tpaneldbloadmap.equal_range(t->id);
		for(auto it=itpair.first; it!=itpair.second; ++it) (*it).second->PushTweet(t);
		tpaneldbloadmap.erase(itpair.first, itpair.second);
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
	lastupdate=time(0);
	if(user.profile_img_url.size()) {
		if(cached_profile_img_url!=user.profile_img_url) {
			profileimgdlconn::GetConn(user.profile_img_url, shared_from_this());
		}
	}
}

std::string userdatacontainer::mkjson() {
	std::string json;
	writestream wr(json);
	Handler jw(wr);
	jw.StartObject();
	jw.String("name");
	jw.String(user.name);
	jw.String("screen_name");
	jw.String(user.screen_name);

	if(cached_profile_img_url!=user.profile_img_url) {	//don't bother writing it if it's the same as the cached image url
		jw.String("profile_img_url");
		jw.String(user.profile_img_url);
	}

	jw.String("protected");
	jw.Bool(user.isprotected);
	jw.EndObject();
	return json;
}

wxBitmap userdatacontainer::MkProfileBitmapFromwxImage(const wxImage &img, double limitscalefactor) {
	int maxdim=(gc.maxpanelprofimgsize*limitscalefactor);
	if(img.GetHeight()>maxdim || img.GetWidth()>maxdim) {
		double scalefactor=(double) maxdim / (double) std::max(img.GetHeight(), img.GetWidth());
		int newwidth = (double) img.GetWidth() * scalefactor;
		int newheight = (double) img.GetHeight() * scalefactor;
		return wxBitmap(img.Scale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH));
	}
	else return wxBitmap(img);
}

void userdatacontainer::SetProfileBitmapFromwxImage(const wxImage &img) {
	cached_profile_img=MkProfileBitmapFromwxImage(img, 1.0);
	udc_flags|=UDC_PROFILE_BITMAP_SET;
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

tweet_perspective *tweet::AddTPToTweet(const std::shared_ptr<taccount> &tac, bool *isnew) {
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

void taccount::MarkPendingOrHandle(const std::shared_ptr<tweet> &t) {
	if(t->flags.Get('T')) {
		if(t->user->IsReady()) {
			HandleNewTweet(t);
		}
		else {
			t->lflags|=TLF_PENDINGHANDLENEW;
			MarkPending(t->user->id, t->user, t);
		}
	}
	else if(t->flags.Get('D')) {
		bool isready=true;

		if(!t->user->IsReady()) {
			t->lflags|=TLF_PENDINGHANDLENEW;
			MarkPending(t->user->id, t->user, t);
			isready=false;
		}
		if(!t->user_recipient->IsReady()) {
			t->lflags|=TLF_PENDINGHANDLENEW;
			MarkPending(t->user_recipient->id, t->user_recipient, t);
			isready=false;
		}
		if(isready) {
			HandleNewTweet(t);
		}
	}
}

std::string tweet::mkdynjson() {
	std::string json;
	writestream wr(json, 64);
	Handler jw(wr);
	jw.StartObject();
	jw.String("p");
	jw.StartArray();
	for(auto it=tp_list.begin(); it!=tp_list.end(); ++it) {
		jw.StartObject();
		jw.String("f");
		jw.Uint(it->Save());
		jw.String("a");
		jw.Uint(it->acc->dbindex);
		jw.EndObject();
	}
	jw.EndArray();
	jw.EndObject();
	return json;
}

void StreamCallback( std::string &data, twitCurl* pTwitCurlObj, void *userdata ) {
	twitcurlext *obj=(twitcurlext*) pTwitCurlObj;
	std::shared_ptr<taccount> acc=obj->tacc.lock();

	LogMsgFormat(LFT_SOCKTRACE, wxT("StreamCallback: Received: %s"), wxstrstd(data).c_str());
	jsonparser jp(CS_STREAM, acc, obj);
	jp.ParseString(data);
	data.clear();
}

void StreamActivityCallback( twitCurl* pTwitCurlObj, void *userdata ) {
	twitcurlext *obj=(twitcurlext*) pTwitCurlObj;
	obj->scto->Arm();
	LogMsgFormat(LFT_SOCKTRACE, wxT("Reset timeout on stream connection %p"), obj);
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
	#ifdef __WINDOWS__
	*createtm_t=rttimegm(createtm);
	#else
	char *tz;

	tz = getenv("TZ");
	setenv("TZ", "", 1);
	tzset();
	*createtm_t = mktime(createtm);
	if (tz)
	   setenv("TZ", tz, 1);
	else
	   unsetenv("TZ");
	tzset();
	#endif
}
