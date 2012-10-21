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

#ifdef __WINDOWS__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "strptime.cpp"
#pragma GCC diagnostic pop
#endif
#include <openssl/sha.h>
#include "utf8proc/utf8proc.h"
#include "utf8.h"
#define PCRE_STATIC
#include <pcre.h>
#include <wx/msgdlg.h>

void HandleNewTweet(const std::shared_ptr<tweet> &t) {
	//do some filtering, etc

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

static void EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		(*it)->Freeze();
		bool checkupdateflag=false;
		if(setnoupdateonpush) {
			checkupdateflag=!((*it)->tppw_flags&TPPWF_NOUPDATEONPUSH);
			(*it)->tppw_flags|=TPPWF_NOUPDATEONPUSH;
		}
		for(auto jt=(*it)->currentdisp.begin(); jt!=(*it)->currentdisp.end(); ++jt) {
			tweetdispscr *tds=(tweetdispscr *) jt->second;
			bool continueflag=func(tds);
			for(auto kt=tds->subtweets.begin(); kt!=tds->subtweets.end(); ++kt) {
				if(kt->get()) {
					func(kt->get());
				}
			}
			if(!continueflag) break;
		}
		(*it)->Thaw();
		if(checkupdateflag) (*it)->CheckClearNoUpdateFlag();
	}
}

void UpdateAllTweets(bool redrawimg) {
	EnumDisplayedTweets([&](tweetdispscr *tds) {
		tds->DisplayTweet(redrawimg);
		return true;
	}, true);
}

void UpdateUsersTweet(uint64_t userid, bool redrawimg) {
	EnumDisplayedTweets([&](tweetdispscr *tds) {
		bool found=false;
		if((tds->td->user && tds->td->user->id==userid)
		|| (tds->td->user_recipient && tds->td->user_recipient->id==userid)) found=true;
		if(tds->td->rtsrc) {
			if((tds->td->rtsrc->user && tds->td->rtsrc->user->id==userid)
			|| (tds->td->rtsrc->user_recipient && tds->td->rtsrc->user_recipient->id==userid)) found=true;
		}
		if(found) {
			LogMsgFormat(LFT_TPANEL, wxT("UpdateUsersTweet: Found Entry %" wxLongLongFmtSpec "d."), tds->td->id);
			tds->DisplayTweet(redrawimg);
		}
		return true;
	}, true);
}

void UpdateTweet(const tweet &t, bool redrawimg) {
	EnumDisplayedTweets([&](tweetdispscr *tds) {
		if(tds->td->id==t.id || tds->rtid==t.id) {	//found matching entry
			LogMsgFormat(LFT_TPANEL, wxT("UpdateTweet: Found Entry %" wxLongLongFmtSpec "d."), t.id);
			tds->DisplayTweet(redrawimg);
			return false;
		}
		else return true;
	}, true);
}

wxString media_entity::cached_full_filename() const {
	return wxString::Format(wxT("%s%s%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), wxStandardPaths::Get().GetUserDataDir().c_str(), wxT("/media_"), media_id.m_id, media_id.t_id);
}
wxString media_entity::cached_thumb_filename() const {
	return wxString::Format(wxT("%s%s%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), wxStandardPaths::Get().GetUserDataDir().c_str(), wxT("/mediathumb_"), media_id.m_id, media_id.t_id);
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

void userlookup::GetIdList(std::string &idlist) const {
	idlist.clear();
	if(users_queried.empty()) return;
	auto it=users_queried.cbegin();
	while(true) {
		idlist+=std::to_string((*it)->id);
		it++;
		if(it==users_queried.cend()) break;
		idlist+=",";
	}
}

BEGIN_EVENT_TABLE( twitcurlext, mcurlconn )
END_EVENT_TABLE()

void twitcurlext::NotifyDoneSuccess(CURL *easy, CURLcode res) {
	std::shared_ptr<taccount> acc=tacc.lock();
	LogMsgFormat(LFT_OTHERTRACE, wxT("twitcurlext::NotifyDoneSuccess: for conn: %s, account: %s"), GetConnTypeName().c_str(), acc?acc->dispname.c_str():wxT("none"));
	if(!acc) {
		KillConn();
		return;
	}

	jsonparser jp(connmode, acc, this);
	std::string str;
	getLastWebResponseMove(str);
	jp.ParseString(str);
	str.clear();

	KillConn();
	if(connmode==CS_STREAM && acc->enabled) {
		LogMsgFormat(LFT_SOCKERR, wxT("Stream connection interrupted, reconnecting: for account: %s"), acc->dispname.c_str());
		DoRetry();
	}
	else if(rbfs) {
		ExecRestGetTweetBackfill();
	}
	else {
		acc->DoPostAction(this);
	}

	acc->CheckFailedPendingConns();
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
		if(rbfs->type==RBFS_TWEETS && gc.assumementionistweet) {
			rbfs->max_tweets_left=800;
			tweets_to_get=200;
			rbfs->type=RBFS_MENTIONS;
		}
		else cleanup=true;
	}
	else if(rbfs->lastop_recvcount>0 && rbfs->lastop_recvcount<175) {	//if less than 175 tweets received in last call, assume that there won't be any more
		cleanup=true;
	}

	if(cleanup) {
		//all done, can now clean up pending rbfs
		acc->pending_rbfs_list.remove_if([&](restbackfillstate &r) { return (&r==rbfs); });
		rbfs=0;
		acc->DoPostAction(this);
	}
	else {
		rbfs->lastop_recvcount=0;
		struct timelineparams tmps={
			tweets_to_get,
			rbfs->start_tweet_id,
			rbfs->end_tweet_id,
			(signed char) ((rbfs->type==RBFS_TWEETS || rbfs->type==RBFS_MENTIONS)?1:0),
			(signed char) ((rbfs->type==RBFS_TWEETS || rbfs->type==RBFS_MENTIONS)?1:0),
			1,
			0
		};

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
			case RBFS_USER_TIMELINE:
				timelineUserGet(tmps, std::to_string(rbfs->userid), true);
				break;
			case RBFS_USER_FAVS:
				favoriteGet(tmps, std::to_string(rbfs->userid), true);
				break;
		}
		if(currentlogflags&LFT_NETACT) {
			char *url;
			curl_easy_getinfo(GenGetCurlHandle(), CURLINFO_EFFECTIVE_URL, &url);
			LogMsgFormat(LFT_NETACT, wxT("REST timeline fetch: acc: %s, type: %d, num: %d, start_id: %" wxLongLongFmtSpec "d, end_id: %" wxLongLongFmtSpec "d"),
				acc->dispname.c_str(), rbfs->type, tweets_to_get, rbfs->start_tweet_id, rbfs->end_tweet_id);
			LogMsgFormat(LFT_NETACT, wxT("Executing API call: for account: %s, url: %s"), acc->dispname.c_str(), wxstrstd(url).c_str());
		}
		sm.AddConn(*this);
	}
}

twitcurlext::twitcurlext(std::shared_ptr<taccount> acc) {
	inited=false;
	post_action_flags=0;
	extra_id=0;
	rbfs=0;
	ownermainframe=0;
	extra_id=0;
	TwInit(acc);
}

twitcurlext::twitcurlext() {
	inited=false;
	post_action_flags=0;
	extra_id=0;
	rbfs=0;
	ownermainframe=0;
	extra_id=0;
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
	tacc.lock()->verifycredstatus=ACT_INPROGRESS;
	connmode=CS_ACCVERIFY;
	QueueAsyncExec();
}

bool twitcurlext::TwSyncStartupAccVerify() {
	tacc.lock()->verifycredstatus=ACT_INPROGRESS;
	SetNoPerformFlag(false);
	accountVerifyCredGet();
	long httpcode;
	curl_easy_getinfo(GetCurlHandle(), CURLINFO_RESPONSE_CODE, &httpcode);
	if(httpcode==200) {
		auto acc=tacc.lock();
		jsonparser jp(CS_ACCVERIFY, acc, this);
		std::string str;
		getLastWebResponse(str);
		bool res=jp.ParseString(str);
		str.clear();
		acc->verifycredstatus=ACT_DONE;
		return res;
	}
	else {
		tacc.lock()->verifycredstatus=ACT_FAILED;
		return false;
	}
}

void twitcurlext::Reset() {
	if(tc_flags&TCF_ISSTREAM) {
		if(auto acc=tacc.lock()) {
			if(acc->ta_flags&TAF_STREAM_UP) {
				acc->ta_flags&=~TAF_STREAM_UP;
				time_t now=time(0);
				acc->last_stream_end_time=now;
				for(auto it=acc->user_relations.begin(); it!=acc->user_relations.end(); ++it) {
					if(it->second.ifollow_updtime==0) it->second.ifollow_updtime=now;
					if(it->second.followsme_updtime==0) it->second.followsme_updtime=now;
				}
			}
		}
	}
	tc_flags=0;
	scto.reset();
	rbfs=0;
	ownermainframe=0;
	extra_id=0;
	ul.reset();
	post_action_flags=0;
}

void twitcurlext::DoRetry() {
	QueueAsyncExec();
}

void twitcurlext::QueueAsyncExec() {
	auto acc=tacc.lock();
	if(!acc) delete this;
	if(acc->init && connmode==CS_ACCVERIFY) { }	//OK
	else if(!acc->enabled) {
		if(connmode==CS_POSTTWEET || connmode==CS_SENDDM) {
			if(ownermainframe && ownermainframe->tpw) ownermainframe->tpw->NotifyPostResult(false);
		}
		acc->cp.Standby(this);
		return;
	}

	SetNoPerformFlag(true);
	switch(connmode) {
		case CS_ACCVERIFY:
			LogMsgFormat(LFT_NETACT, wxT("Queue AccVerify"));
			accountVerifyCredGet();
			break;
		case CS_TIMELINE:
		case CS_DMTIMELINE:
		case CS_USERTIMELINE:
		case CS_USERFAVS:
			return ExecRestGetTweetBackfill();
		case CS_STREAM:
			LogMsgFormat(LFT_NETACT, wxT("Queue Stream Connection"));
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
				acc->cp.Standby(this);
				return;
			}
			if(currentlogflags&LFT_NETACT) {
				auto acc=tacc.lock();
				LogMsgFormat(LFT_NETACT, wxT("About to lookup users: for account: %s, user ids: %s"), acc?acc->dispname.c_str():wxT(""), wxstrstd(userliststr).c_str());
			}
			userLookup(userliststr, "", 0);
			break;
			}
		case CS_FRIENDLOOKUP:
		case CS_USERLOOKUPWIN:
			genericGet(genurl);
			break;
		case CS_FRIENDACTION_FOLLOW:
			friendshipCreate(std::to_string(extra_id), true);
			break;
		case CS_FRIENDACTION_UNFOLLOW:
			friendshipDestroy(std::to_string(extra_id), true);
			break;
		case CS_POSTTWEET:
			statusUpdate(extra1, extra_id?std::to_string(extra_id):"", 1);
			break;
		case CS_SENDDM:
			directMessageSend(std::to_string(extra_id), extra1, 1);
			break;
		case CS_RT:
			statusReTweet(std::to_string(extra_id), 1);
			break;
		case CS_FAV:
			favoriteCreate(std::to_string(extra_id));
			break;
		case CS_UNFAV:
			favoriteDestroy(std::to_string(extra_id));
			break;
		case CS_DELETETWEET:
			statusDestroyById(std::to_string(extra_id));
			break;
		case CS_DELETEDM:
			directMessageDestroyById(std::to_string(extra_id));
			break;
		case CS_USERFOLLOWING:
			friendsIdsGet(std::to_string(extra_id), true);
			break;
		case CS_USERFOLLOWERS:
			followersIdsGet(std::to_string(extra_id), true);
			break;
		case CS_SINGLETWEET:
			statusShowById(std::to_string(extra_id));
			break;
		case CS_NULL:
			break;
	}
	if(currentlogflags&LFT_NETACT) {
		char *url;
		curl_easy_getinfo(GenGetCurlHandle(), CURLINFO_EFFECTIVE_URL, &url);
		LogMsgFormat(LFT_NETACT, wxT("Executing API call: for account: %s, url: %s"), acc?acc->dispname.c_str():wxT(""), wxstrstd(url).c_str());
	}
	sm.AddConn(*this);
}

wxString twitcurlext::GetConnTypeName() {
	wxString action;
	switch(connmode) {
		case CS_STREAM: action=wxT("Stream connection"); break;
		case CS_ACCVERIFY: action=wxT("Verifying twitter account credentials"); break;
		case CS_TIMELINE: action=wxT("Tweet timeline retrieval"); break;
		case CS_DMTIMELINE: action=wxT("DM timeline retrieval"); break;
		case CS_USERTIMELINE: action=wxT("User timeline retrieval"); break;
		case CS_USERFAVS: action=wxT("User favourites retrieval"); break;
		case CS_USERLIST: action=wxT("User lookup"); break;
		case CS_FRIENDLOOKUP: action=wxT("Friend/follower lookup"); break;
		case CS_USERLOOKUPWIN: action=wxT("User lookup (user window)"); break;
		case CS_FRIENDACTION_FOLLOW: action=wxT("Follow user"); break;
		case CS_FRIENDACTION_UNFOLLOW: action=wxT("Unfollow user"); break;
		case CS_POSTTWEET: action=wxT("Posting tweet"); break;
		case CS_SENDDM: action=wxT("Sending DM"); break;
		case CS_RT: action=wxT("Retweeting"); break;
		case CS_FAV: action=wxT("Favouriting tweet"); break;
		case CS_DELETETWEET: action=wxT("Deleting tweet"); break;
		case CS_DELETEDM: action=wxT("Deleting DM"); break;
		case CS_USERFOLLOWING: action=wxT("Retrieving follower list"); break;
		case CS_USERFOLLOWERS: action=wxT("Retrieving friends list"); break;
		case CS_SINGLETWEET: action=wxT("Retrieving single tweet"); break;
		default: action=wxT("Generic twitter API call"); break;
	}
	if(rbfs) {
		switch(rbfs->type) {
			case RBFS_TWEETS:
				action+=wxT(" (home timeline)");
				break;
			case RBFS_MENTIONS:
				action+=wxT(" (mentions)");
				break;
			case RBFS_RECVDM:
				action+=wxT(" (received DMs)");
				break;
			case RBFS_SENTDM:
				action+=wxT(" (sent DMs)");
				break;
			case RBFS_USER_TIMELINE:
				action+=wxT(" (user timeline)");
				break;
			case RBFS_USER_FAVS:
				action+=wxT(" (user favourites)");
				break;
		}
	}
	auto acc=tacc.lock();
	if(acc) {
		action+=wxT(" (account: ") + acc->dispname + wxT(")");
	}
	return action;
}

void twitcurlext::HandleFailure(long httpcode, CURLcode res) {
	auto acc=tacc.lock();
	if(!acc) return;

	wxString action=GetConnTypeName();
	bool msgbox=false;
	bool retry=false;
	switch(connmode) {
		case CS_STREAM: {
			bool was_stream_mode=(acc->stream_fail_count==0);
			if(was_stream_mode) {
				acc->last_rest_backfill=time(0);	//don't immediately query REST api
				LogMsgFormat(LFT_SOCKERR, wxT("Stream connection failed, switching to REST api: for account: %s"), acc->dispname.c_str());
			}
			else {
				LogMsgFormat(LFT_SOCKERR, wxT("Stream reconnection attempt failed: for account: %s"), acc->dispname.c_str());
			}
			acc->stream_fail_count++;
			acc->Exec();
			return;
		}
		case CS_ACCVERIFY: {
			acc->verifycredstatus=ACT_FAILED;
			retry=true;
			AccountChangeTrigger();
			break;
		}
		case CS_TIMELINE: break;
		case CS_DMTIMELINE: break;
		case CS_USERTIMELINE: break;
		case CS_USERFAVS: break;
		case CS_USERLIST: break;
		case CS_FRIENDLOOKUP: break;
		case CS_USERLOOKUPWIN: break;
		case CS_FRIENDACTION_FOLLOW: msgbox=true; break;
		case CS_FRIENDACTION_UNFOLLOW: msgbox=true; break;
		case CS_POSTTWEET: {
			msgbox=true;
			if(ownermainframe && ownermainframe->tpw) ownermainframe->tpw->NotifyPostResult(false);
			break;
		}
		case CS_SENDDM: {
			msgbox=true;
			if(ownermainframe && ownermainframe->tpw) ownermainframe->tpw->NotifyPostResult(false);
			break;
		}
		case CS_RT: msgbox=true; break;
		case CS_FAV: msgbox=true; break;
		case CS_DELETETWEET: msgbox=true; break;
		case CS_DELETEDM: msgbox=true; break;
		case CS_USERFOLLOWING: break;
		case CS_USERFOLLOWERS: break;
		case CS_SINGLETWEET: retry=true; break;
		default: break;
	}
	LogMsgFormat(LFT_SOCKERR, wxT("%s failed"), action.c_str(), acc->dispname.c_str());
	if(msgbox) {
		wxString msg, errtype;
		if(res==CURLE_OK) errtype.Printf(wxT("HTTP error code: %d"), httpcode);
		else errtype.Printf(wxT("Socket error: CURL error code: %d, %s"), res, wxstrstd(curl_easy_strerror(res)).c_str());
		msg.Printf(wxT("Twitter API call of type: %s, has failed\n%s"), action.c_str(), acc->dispname.c_str(), errtype.c_str());
		::wxMessageBox(msg, wxT("Operation Failed"), wxOK | wxICON_ERROR);
	}
	if(rbfs) {
		bool delrbfs=false;
		if(connmode==CS_USERTIMELINE || connmode==CS_USERFAVS) delrbfs=true;
		if(rbfs->end_tweet_id==0) delrbfs=true;
		if(delrbfs) {
			acc->pending_rbfs_list.remove_if([&](restbackfillstate &r) { return (&r==rbfs); });
		}
		else retry=true;
	}
	if(retry && (acc->enabled || acc->init)) {
		acc->AddFailedPendingConn(this);
	}
	else acc->cp.Standby(this);
}

void streamconntimeout::Arm() {
	Start(90000, wxTIMER_ONE_SHOT);
}

void streamconntimeout::Notify() {
	tw->KillConn();
	tw->HandleError(tw->GetCurlHandle(),0,CURLE_OPERATION_TIMEDOUT);
}

bool userdatacontainer::NeedsUpdating(unsigned int updcf_flags) const {
	if(!lastupdate) return true;
	else if(!(updcf_flags&UPDCF_USEREXPIRE) && GetUser().screen_name.size()) return false;
	else {
		if((time(0)-lastupdate)>gc.userexpiretime) return true;
		else return false;
	}
}

bool userdatacontainer::ImgIsReady(unsigned int updcf_flags) {
	if(udc_flags & UDC_IMAGE_DL_IN_PROGRESS) return false;
	if(user.profile_img_url.size()) {
		if(cached_profile_img_url!=user.profile_img_url) {
			if(updcf_flags&UPDCF_DOWNLOADIMG) profileimgdlconn::GetConn(user.profile_img_url, shared_from_this());
			return false;
		}
		else if(cached_profile_img_url.size() && !(udc_flags&UDC_PROFILE_BITMAP_SET))  {
			wxImage img;
			wxString filename;
			GetImageLocalFilename(filename);
			bool success=LoadImageFromFileAndCheckHash(filename, cached_profile_img_sha1, img);
			if(success) {
				SetProfileBitmapFromwxImage(img);
				return true;
			}
			else {
				LogMsgFormat(LFT_OTHERERR, wxT("userdatacontainer::ImgIsReady, cached profile image file for user id: %" wxLongLongFmtSpec "d (%s), file: %s, url: %s, missing, invalid or failed hash check"),
					id, wxstrstd(GetUser().screen_name).c_str(), filename.c_str(), wxstrstd(cached_profile_img_url).c_str());
				cached_profile_img_url.clear();
				if(updcf_flags&UPDCF_DOWNLOADIMG) {					//the saved image is not loadable, clear cache and re-download
					profileimgdlconn::GetConn(user.profile_img_url, shared_from_this());
				}
				return false;
			}
		}
		else {
			return true;
		}
	}
	else return false;
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
	else if( !(updcf_flags&UPDCF_USEREXPIRE) ) return true;
	else if( udc_flags & (UDC_LOOKUP_IN_PROGRESS|UDC_IMAGE_DL_IN_PROGRESS)) return false;
	else return true;
}

void userdatacontainer::CheckPendingTweets(unsigned int umpt_flags) {
	FreezeAll();
	std::forward_list<std::pair<int, std::shared_ptr<tweet> > > stillpending;
	for(auto it=pendingtweets.begin(); it!=pendingtweets.end(); ++it) {
		int res=CheckTweetPendings(*it);
		if(res==0) {
			UnmarkPendingTweet(*it, umpt_flags);
		}
		else {
			stillpending.push_front(std::make_pair(res, *it));
		}
	}
	pendingtweets.clear();

	for(auto it=stillpending.begin(); it!=stillpending.end(); ++it) {
		std::shared_ptr<taccount> curacc;
		if(it->second->GetUsableAccount(curacc, true)) {
			curacc->FastMarkPending(it->second, it->first, true);
		}
	}

	if(udc_flags&UDC_WINDOWOPEN) {
		user_window *uw=user_window::GetWin(id);
		if(uw) uw->Refresh();
	}
	if(udc_flags&UDC_CHECK_USERLISTWIN) {
		auto pit=tpanelparentwin_user::pendingmap.equal_range(id);
		for(auto it=pit.first; it!=pit.second; ++it) {
			it->second->PushBackUser(shared_from_this());
		}
	}
	ThawAll();
}

void userdatacontainer::MarkTweetPending(const std::shared_ptr<tweet> &t, bool checkfirst) {
	if(checkfirst) {
		if(std::find_if(pendingtweets.begin(), pendingtweets.end(), [&](const std::shared_ptr<tweet> &tw) {
			return (t->id==tw->id);
		})!=pendingtweets.end()) {
			return;
		}
	}
	pendingtweets.push_front(t);
	LogMsgFormat(LFT_PENDTRACE, wxT("Mark Pending: User: %" wxLongLongFmtSpec "d (@%s) --> Tweet: %" wxLongLongFmtSpec "d (%.15s...)"), id, wxstrstd(GetUser().screen_name).c_str(), t->id, wxstrstd(t->text).c_str());
}

void rt_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags) {
	if(target_retweet->IsReady()) UnmarkPendingTweet(target_retweet, umpt_flags);
}

wxString rt_pending_op::dump() {
	return wxString::Format(wxT("Retweet depends on this: %" wxLongLongFmtSpec "d (%.20s...)"), target_retweet->id, wxstrstd(target_retweet->text).c_str());
}

void tpanelload_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags) {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	if(tp) tp->PushTweet(t);
	tpanelparentwin_nt *window=win.get();
	if(window) {
		if(umpt_flags&UMPTF_TPDB_NOUPDF) window->tppw_flags|=TPPWF_NOUPDATEONPUSH;
		window->PushTweet(t, pushflags);
	}
}

wxString tpanelload_pending_op::dump() {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	tpanelparentwin_nt *window=win.get();
	return wxString::Format(wxT("Push tweet to tpanel: %s, window: %p, pushflags: 0x%X"), (tp)?wxstrstd(tp->dispname).c_str():wxT("N/A"), window, pushflags);
}

void tpanel_subtweet_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags) {
	tweetdispscr *tds=parent_td.get();
	tpanelparentwin_nt *window=win.get();
	if(!tds || !window) return;

	if(umpt_flags&UMPTF_TPDB_NOUPDF) window->tppw_flags|=TPPWF_NOUPDATEONPUSH;

	wxBoxSizer *subhbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(subhbox, 0, wxALL | wxEXPAND, 1);

	tweetdispscr *subtd=new tweetdispscr(t, window->scrollwin, window, subhbox);

	tds->subtweets.emplace_front(subtd);

	if(t->rtsrc && gc.rtdisp) {
		t->rtsrc->user->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
		subtd->bm = new profimg_staticbitmap(window->scrollwin, t->rtsrc->user->cached_profile_img_half, t->rtsrc->user->id, t->id, window->GetMainframe());
	}
	else {
		t->user->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
		subtd->bm = new profimg_staticbitmap(window->scrollwin, t->user->cached_profile_img_half, t->user->id, t->id, window->GetMainframe());
	}
	subhbox->Add(subtd->bm, 0, wxALL, 1);
	subhbox->Add(subtd, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);

	wxTextAttrEx tae(subtd->GetDefaultStyleEx());
	wxFont newfont(tae.GetFont());
	int newsize=((newfont.GetPointSize()*3)+2)/4;
	if(!newsize) newsize=7;
	newfont.SetPointSize(newsize);
	tae.SetFont(newfont);
	subtd->SetFont(newfont);
	subtd->SetDefaultStyle(tae);

	subtd->DisplayTweet();

	if(!(window->tppw_flags&TPPWF_NOUPDATEONPUSH)) window->scrollwin->FitInside();
}

wxString tpanel_subtweet_pending_op::dump() {
	return wxString::Format(wxT("Push inline tweet reply to tpanel: %p, %p, %p"), vbox, win.get(), parent_td.get());
}

void UnmarkPendingTweet(const std::shared_ptr<tweet> &t, unsigned int umpt_flags) {
	LogMsgFormat(LFT_PENDTRACE, wxT("Unmark Pending: Tweet: %" wxLongLongFmtSpec "d (%.15s...), lflags: %X, updcf_flags: %X"), t->id, wxstrstd(t->text).c_str(), t->lflags, t->updcf_flags);
	t->lflags&=~TLF_BEINGLOADEDFROMDB;
	if(t->lflags&TLF_PENDINGHANDLENEW) {
		t->lflags&=~TLF_PENDINGHANDLENEW;
		HandleNewTweet(t);
	}
	for(auto it=t->pending_ops.begin(); it!=t->pending_ops.end(); ++it) {
		(*it)->MarkUnpending(t, umpt_flags);
	}
	t->pending_ops.clear();
	t->updcf_flags&=~UPDCF_USEREXPIRE;
}

std::shared_ptr<taccount> userdatacontainer::GetAccountOfUser() const {
	for(auto it=alist.begin() ; it != alist.end(); it++ ) if( (*it)->usercont.get()==this ) return *it;
	return std::shared_ptr<taccount>();
}

void userdatacontainer::GetImageLocalFilename(wxString &filename) const {
	filename.Printf(wxT("/img_%" wxLongLongFmtSpec "d"), id);
	filename.Prepend(wxStandardPaths::Get().GetUserDataDir());
}

void userdatacontainer::MarkUpdated() {
	lastupdate=time(0);
	ImgIsReady(UPDCF_DOWNLOADIMG);
}

std::string userdatacontainer::mkjson() const {
	std::string json;
	writestream wr(json);
	Handler jw(wr);
	jw.StartObject();
	jw.String("name");
	jw.String(user.name);
	jw.String("screen_name");
	jw.String(user.screen_name);
	jw.String("description");
	jw.String(user.description);
	jw.String("url");
	jw.String(user.userurl);
	if(cached_profile_img_url!=user.profile_img_url) {	//don't bother writing it if it's the same as the cached image url
		jw.String("profile_img_url");
		jw.String(user.profile_img_url);
	}
	jw.String("protected");
	jw.Bool(user.u_flags&UF_ISPROTECTED);
	jw.String("verified");
	jw.Bool(user.u_flags&UF_ISVERIFIED);
	jw.String("followers_count");
	jw.Uint(user.followers_count);
	jw.String("statuses_count");
	jw.Uint(user.statuses_count);
	jw.String("friends_count");
	jw.Uint(user.friends_count);
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

bool userdatacontainer::GetUsableAccount(std::shared_ptr<taccount> &tac, bool enabledonly) const {
	if(!tac) {
		for(auto it=alist.begin(); it!=alist.end(); ++it) {	//look for users who we follow, or who follow us
			taccount &acc=**it;
			if(!enabledonly || acc.enabled) {
				auto rel=acc.user_relations.find(id);
				if(rel!=acc.user_relations.end()) {
					if(rel->second.ur_flags&(URF_FOLLOWSME_TRUE|URF_IFOLLOW_TRUE)) {
						tac=*it;
						break;
					}
				}
			}
		}
	}
	if(!tac) {					//otherwise find the first enabled account
		for(auto it=alist.begin(); it!=alist.end(); ++it) {
			if((*it)->enabled) {
				tac=*it;
				break;
			}
		}
	}
	if(!tac) {					//otherwise find the first account
		if(!alist.empty()) tac=alist.front();
	}
	if(!tac) return false;
	if(!enabledonly || tac->enabled) return true;
	else return false;
}

std::string tweet_flags::GetString() const {
	std::string out;
	uint64_t bitint=bits.to_ullong();
	while(bitint) {
		int offset=__builtin_ctzll(bitint);
		bitint&=~((uint64_t) 1<<offset);
		out+=GetFlagChar(offset);
	}
	return out;
}

bool tweet::GetUsableAccount(std::shared_ptr<taccount> &tac, unsigned int guaflags) const {
	if(guaflags&GUAF_CHECKEXISTING) {
		if(tac && tac->enabled) return true;
	}
	for(auto it=tp_list.begin(); it!=tp_list.end(); ++it) {
		if(it->IsArrivedHere()) {
			if(it->acc->enabled || (guaflags&GUAF_USERENABLED && it->acc->userenabled)) {
				tac=it->acc;
				return true;
			}
		}
	}
	//try again, but use any associated account
	for(auto it=tp_list.begin(); it!=tp_list.end(); ++it) {
		if(it->acc->enabled || (guaflags&GUAF_USERENABLED && it->acc->userenabled)) {
			tac=it->acc;
			return true;
		}
	}
	//use the first account which is actually enabled
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		if((*it)->enabled || (guaflags&GUAF_USERENABLED && (*it)->userenabled)) {
			tac=*it;
			return true;
		}
	}
	if(!(guaflags&GUAF_NOERR)) {
		LogMsgFormat(LFT_OTHERERR, wxT("Tweet: %" wxLongLongFmtSpec "d (%.15s...), has no usable enabled account, cannot perform network actions on tweet"), id, wxstrstd(text).c_str());
	}
	return false;
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

tweet_perspective *tweet::GetTweetTP(const std::shared_ptr<taccount> &tac) {
	for(auto it=tp_list.begin(); it!=tp_list.end(); it++) {
		if(it->acc.get()==tac.get()) {
			return &(*it);
		}
	}
	return 0;
}

void tweet::UpdateMarkedAsRead(const tpanel *exclude) {
	if(!flags.Get('r')) {
		flags.Set('r');
		UpdateTweet(*this, false);
	}
}

void MarkTweetIdAsRead(uint64_t id, const tpanel *exclude) {
	for(auto it=ad.tpanels.begin(); it!=ad.tpanels.end(); ++it) {
		if(it->second.get()==exclude) continue;

		tweetidset &unread=(it->second)->unreadtweetids;
		auto unread_it=unread.find(id);
		if(unread_it!=unread.end()) {
			unread.erase(*unread_it);
			for(auto jt=it->second->twin.begin(); jt!=it->second->twin.end(); ++jt) {
				(*jt)->tppw_flags|=TPPWF_CLABELUPDATEPENDING;
			}
		}
	}
	ad.unreadids.erase(id);
}

void MarkTweetIDSetAsRead(const tweetidset &ids, const tpanel *exclude) {
	tweetidset cached_ids=ids;
	for(auto it=cached_ids.begin(); it!=cached_ids.end(); ++it) {
		MarkTweetIdAsRead(*it, exclude);
		auto twshpp=ad.GetExistingTweetById(*it);
		if(twshpp) {
			(*twshpp)->UpdateMarkedAsRead(exclude);
		}
	}
	dbupdatetweetsetflagsmsg *msg=new dbupdatetweetsetflagsmsg(std::move(cached_ids), ((uint64_t) 1)<<tweet_flags::GetFlagNum('r'), 0);
	dbc.SendMessage(msg);
}

//the following set of procedures should be kept in sync

//returns true is ready, false is pending
bool taccount::CheckMarkPending(const std::shared_ptr<tweet> &t, bool checkfirst) {
	unsigned int res=CheckTweetPendings(t);
	if(!res) return true;
	else {
		FastMarkPending(t, res, checkfirst);
		return false;
	}
}

//mark *must* be exactly right
void taccount::FastMarkPending(const std::shared_ptr<tweet> &t, unsigned int mark, bool checkfirst) {
	if(mark&1) t->user->MarkTweetPending(t, checkfirst);
	if(mark&2) t->user_recipient->MarkTweetPending(t, checkfirst);
	if(mark&4) {
		bool insertnewrtpo=true;
		for(auto it=t->rtsrc->pending_ops.begin(); it!=t->rtsrc->pending_ops.end(); ++it) {
			rt_pending_op *rtpo = dynamic_cast<rt_pending_op*>((*it).get());
			if(rtpo && rtpo->target_retweet==t) {
				insertnewrtpo=false;
				break;
			}
		}
		if(insertnewrtpo) t->rtsrc->pending_ops.emplace_front(new rt_pending_op(t));
		t->rtsrc->user->MarkTweetPending(t->rtsrc, checkfirst);
	}

	if(mark&8) MarkUserPending(t->user);
	if(mark&16) MarkUserPending(t->user_recipient);
	if(mark&32) MarkUserPending(t->rtsrc->user);
}

//returns non-zero if pending
unsigned int CheckTweetPendings(const std::shared_ptr<tweet> &t) {
	unsigned int retval=0;
	if(t->user && !t->user->IsReady(t->updcf_flags)) {
		if(t->user->NeedsUpdating(t->updcf_flags)) retval|=8;
		retval|=1;
	}
	if(t->flags.Get('D') && t->user_recipient && !(t->user_recipient->IsReady(t->updcf_flags))) {
		if(t->user_recipient->NeedsUpdating(t->updcf_flags)) retval|=16;
		retval|=2;
	}
	if(t->rtsrc && t->rtsrc->user && !t->rtsrc->user->IsReady(t->rtsrc->updcf_flags)) {
		if(t->rtsrc->user->NeedsUpdating(t->rtsrc->updcf_flags)) retval|=32;
		retval|=4;
	}
	return retval;
}

void RemoveUserFromAccPendingLists(uint64_t userid) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		(*it)->pendingusers.erase(userid);
	}
}

//returns true is ready, false is pending
bool CheckMarkPending_GetAcc(const std::shared_ptr<tweet> &t, bool checkfirst) {
	unsigned int res=CheckTweetPendings(t);
	if(!res) return true;
	else {
		std::shared_ptr<taccount> curacc;
		if(t->GetUsableAccount(curacc, true)) {
			curacc->FastMarkPending(t, res, checkfirst);
			return false;
		}
		else return true;
	}
}

bool MarkPending_TPanelMap(const std::shared_ptr<tweet> &tobj, tpanelparentwin_nt* win_, unsigned int pushflags, std::shared_ptr<tpanel> *pushtpanel_) {
	tpanel *tp=0;
	if(pushtpanel_) tp=(*pushtpanel_).get();
	bool found=false;
	for(auto it=tobj->pending_ops.begin(); it!=tobj->pending_ops.end(); ++it) {
		tpanelload_pending_op *op=dynamic_cast<tpanelload_pending_op *>((*it).get());
		if(!op) continue;
		if(win_ && op->win.get()!=win_) continue;
		if(tp) {
			std::shared_ptr<tpanel> test_tp=op->pushtpanel.lock();
			if(test_tp.get()!=tp) continue;
		}
		found=true;
		break;
	}
	if(!found) tobj->pending_ops.emplace_front(new tpanelload_pending_op(win_, pushflags, pushtpanel_));
	return found;
}

//return true if ready now
bool CheckFetchPendingSingleTweet(const std::shared_ptr<tweet> &tobj, std::shared_ptr<taccount> acc_hint) {
	if(tobj->text.size()) {
		unsigned int res=CheckTweetPendings(tobj);
		if(!res) return true;
		else {
			if(tobj->GetUsableAccount(acc_hint, GUAF_CHECKEXISTING|GUAF_NOERR) ||
					tobj->GetUsableAccount(acc_hint, GUAF_CHECKEXISTING|GUAF_NOERR|GUAF_USERENABLED)) {
				acc_hint->FastMarkPending(tobj, res, true);
				return false;
			}
			else return true;
		}
	}
	else {	//tweet not loaded at all
		if(!(tobj->lflags&TLF_BEINGLOADEDFROMDB) && !(tobj->lflags&TLF_BEINGLOADEDOVERNET)) {
			dbseltweetmsg_netfallback *loadmsg=new dbseltweetmsg_netfallback;
			loadmsg->id_set.insert(tobj->id);
			loadmsg->targ=&dbc;
			loadmsg->cmdevtype=wxextDBCONN_NOTIFY;
			loadmsg->winid=wxDBCONNEVT_ID_TPANELTWEETLOAD;
			loadmsg->flags|=DBSTMF_NO_ERR;
			if(acc_hint) loadmsg->dbindex=acc_hint->dbindex;
			if(!gc.persistentmediacache) loadmsg->flags|=DBSTMF_PULLMEDIA;
			dbc.SendMessage(loadmsg);
		}
		return false;
	}
}

//returns true is ready, false is pending
bool tweet::IsReady() {
	bool isready=true;

	if(rtsrc) {
		bool rtsrcisready=rtsrc->IsReady();
		if(!rtsrcisready) isready=false;
	}
	if(!user) isready=false;
	else if(!user->IsReady(updcf_flags)) isready=false;
	if(flags.Get('D')) {
		if(!user_recipient) isready=false;
		else if(!(user_recipient->IsReady(updcf_flags))) isready=false;
	}
	return isready;
}

void taccount::MarkPendingOrHandle(const std::shared_ptr<tweet> &t) {
	bool isready=CheckMarkPending(t);
	if(isready) HandleNewTweet(t);
	else t->lflags|=TLF_PENDINGHANDLENEW;
}

std::string tweet::mkdynjson() const {
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

std::string tweet::GetPermalink() const {
	if(!user || !user->GetUser().screen_name.size()) return "";
	return "http" + std::string(flags.Get('s')?"s":"") + "://twitter.com/" + user->GetUser().screen_name + "/status/" + std::to_string(id);
}

std::string userdatacontainer::GetPermalink(bool ssl) const {
	if(!GetUser().screen_name.size()) return "";
	return "http" + std::string(ssl?"s":"") + "://twitter.com/" + GetUser().screen_name;
}

bool tweet::IsFavouritable() const {
	return flags.Get('T');
}

bool tweet::IsRetweetable() const {
	return (flags.Get('T') && (rtsrc || !(user->GetUser().u_flags&UF_ISPROTECTED)));
}

void StreamCallback( std::string &data, twitCurl* pTwitCurlObj, void *userdata ) {
	twitcurlext *obj=(twitcurlext*) pTwitCurlObj;
	std::shared_ptr<taccount> acc=obj->tacc.lock();
	if(!acc) return;

	if(acc->stream_fail_count) {
		acc->stream_fail_count=0;
		acc->streaming_on=true;
		acc->Exec();
	}
	sm.RetryConnLater();

	LogMsgFormat(LFT_SOCKTRACE, wxT("StreamCallback: Received: %s"), wxstrstd(data).c_str());
	jsonparser jp(CS_STREAM, acc, obj);
	jp.ParseString(data);
	data.clear();
}

void StreamActivityCallback( twitCurl* pTwitCurlObj, void *userdata ) {
	twitcurlext *obj=(twitcurlext*) pTwitCurlObj;
	obj->scto->Arm();
	LogMsgFormat(LFT_SOCKTRACE, wxT("Reset timeout on stream connection %p"), obj);
	std::shared_ptr<taccount> acc=obj->tacc.lock();
	if(acc) {
		acc->CheckFailedPendingConns();
	}
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
	*createtm_t=_mkgmtime(createtm);
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

//adapted from twitter code: https://github.com/twitter/twitter-text-java/blob/master/src/com/twitter/Regex.java
#define URL_VALID_PRECEEDING_CHARS "(?:[^A-Z0-9@\\x{FF20}$#\\x{FF03}\\x{202A}-\\x{202E}]|^)"
#define LATIN_ACCENTS_CHARS \
	"\\x{00c0}-\\x{00d6}\\x{00d8}-\\x{00f6}\\x{00f8}-\\x{00ff}" \
	"\\x{0100}-\\x{024f}" \
	"\\x{0253}\\x{0254}\\x{0256}\\x{0257}\\x{0259}\\x{025b}\\x{0263}\\x{0268}\\x{026f}\\x{0272}\\x{0289}\\x{028b}" \
	"\\x{02bb}" \
	"\\x{0300}-\\x{036f}" \
	"\\x{1e00}-\\x{1eff}"
#define URL_VALID_CHARS_NC "\\p{Xan}" LATIN_ACCENTS_CHARS
#define URL_VALID_CHARS "[" URL_VALID_CHARS_NC "]"
#define URL_VALID_SUBDOMAIN "(?:(?:" URL_VALID_CHARS "[\\-" URL_VALID_CHARS_NC "_]*)?" URL_VALID_CHARS "\\.)"
#define URL_VALID_DOMAIN_NAME "(?:(?:" URL_VALID_CHARS "[\\-" URL_VALID_CHARS_NC "]*)?" URL_VALID_CHARS "\\.)"
#define URL_VALID_GTLD \
      "(?:(?:aero|asia|biz|cat|com|coop|edu|gov|info|int|jobs|mil|mobi|museum|name|net|org|pro|tel|travel|xxx)(?!\\p{Xan}))"
#define  URL_VALID_CCTLD \
      "(?:(?:ac|ad|ae|af|ag|ai|al|am|an|ao|aq|ar|as|at|au|aw|ax|az|ba|bb|bd|be|bf|bg|bh|bi|bj|bm|bn|bo|br|bs|bt|" \
      "bv|bw|by|bz|ca|cc|cd|cf|cg|ch|ci|ck|cl|cm|cn|co|cr|cs|cu|cv|cx|cy|cz|dd|de|dj|dk|dm|do|dz|ec|ee|eg|eh|" \
      "er|es|et|eu|fi|fj|fk|fm|fo|fr|ga|gb|gd|ge|gf|gg|gh|gi|gl|gm|gn|gp|gq|gr|gs|gt|gu|gw|gy|hk|hm|hn|hr|ht|" \
      "hu|id|ie|il|im|in|io|iq|ir|is|it|je|jm|jo|jp|ke|kg|kh|ki|km|kn|kp|kr|kw|ky|kz|la|lb|lc|li|lk|lr|ls|lt|" \
      "lu|lv|ly|ma|mc|md|me|mg|mh|mk|ml|mm|mn|mo|mp|mq|mr|ms|mt|mu|mv|mw|mx|my|mz|na|nc|ne|nf|ng|ni|nl|no|np|" \
      "nr|nu|nz|om|pa|pe|pf|pg|ph|pk|pl|pm|pn|pr|ps|pt|pw|py|qa|re|ro|rs|ru|rw|sa|sb|sc|sd|se|sg|sh|si|sj|sk|" \
      "sl|sm|sn|so|sr|ss|st|su|sv|sy|sz|tc|td|tf|tg|th|tj|tk|tl|tm|tn|to|tp|tr|tt|tv|tw|tz|ua|ug|uk|us|uy|uz|" \
      "va|vc|ve|vg|vi|vn|vu|wf|ws|ye|yt|za|zm|zw)(?!\\p{Xan}))"
#define URL_PUNYCODE "(?:xn--[0-9a-z]+)"
#define URL_VALID_UNICODE_CHARS "(?:\\.|[^\\s\\p{Z}\\p{P}])"
// \\p{Punct}\\p{InGeneralPunctuation}
#define URL_VALID_DOMAIN \
    "(?:" \
        URL_VALID_SUBDOMAIN "+" URL_VALID_DOMAIN_NAME \
        "(?:" URL_VALID_GTLD "|" URL_VALID_CCTLD "|" URL_PUNYCODE ")" \
      ")" \
    "|(?:" \
      URL_VALID_DOMAIN_NAME \
      "(?:" URL_VALID_GTLD "|" URL_PUNYCODE ")" \
    ")" \
    "|(" "(?<=http://)" \
      "(?:" \
        "(?:" URL_VALID_DOMAIN_NAME URL_VALID_CCTLD ")" \
        "|(?:" \
          URL_VALID_UNICODE_CHARS "+\\." \
          "(?:" URL_VALID_GTLD "|" URL_VALID_CCTLD ")" \
        ")" \
      ")" \
    ")" \
    "|(" "(?<=https://)" \
      "(?:" \
        "(?:" URL_VALID_DOMAIN_NAME URL_VALID_CCTLD ")" \
        "|(?:" \
          URL_VALID_UNICODE_CHARS "+\\." \
          "(?:" URL_VALID_GTLD "|" URL_VALID_CCTLD ")" \
        ")" \
      ")" \
    ")" \
    "|(?:" \
      URL_VALID_DOMAIN_NAME URL_VALID_CCTLD "(?=/)" \
    ")"

#define URL_VALID_PORT_NUMBER "[0-9]++"
#define URL_VALID_GENERAL_PATH_CHARS "[a-z0-9!\\*';:=\\+,.\\$/%#\\x{5B}\\x{5D}\\-_~\\|&" LATIN_ACCENTS_CHARS "]"
#define URL_BALANCED_PARENS "\\(" URL_VALID_GENERAL_PATH_CHARS "+\\)"
#define URL_VALID_PATH_ENDING_CHARS "[a-z0-9=_#/\\-\\+" LATIN_ACCENTS_CHARS "]|(?:" URL_BALANCED_PARENS ")"
#define URL_VALID_PATH "(?:" \
    "(?:" \
      URL_VALID_GENERAL_PATH_CHARS "*" \
      "(?:" URL_BALANCED_PARENS URL_VALID_GENERAL_PATH_CHARS "*)*" \
      URL_VALID_PATH_ENDING_CHARS \
    ")|(?:@" URL_VALID_GENERAL_PATH_CHARS "+/)" \
  ")"
#define URL_VALID_URL_QUERY_CHARS "[a-z0-9!?\\*'\\(\\);:&=\\+\\$/%#\\x{5B}\\x{5D}\\-_\\.,~\\|]"
#define URL_VALID_URL_QUERY_ENDING_CHARS "[a-z0-9_&=#/]"
#define VALID_URL_PATTERN_STRING \
  "(?:" \
    "(" URL_VALID_PRECEEDING_CHARS ")" \
    "(" \
      "(https?://)?" \
      "(?:" URL_VALID_DOMAIN ")" \
      "(?::(?:" URL_VALID_PORT_NUMBER "))?" \
      "(?:/" \
        URL_VALID_PATH "*+" \
      ")?" \
      "(?:\\?" URL_VALID_URL_QUERY_CHARS "*" \
              URL_VALID_URL_QUERY_ENDING_CHARS ")?" \
    ")" \
  ")"
#define INVALID_URL_WITHOUT_PROTOCOL_MATCH_BEGIN "[-_./]$"
#define VALID_TCO_URL "^https?://t\\.co/[a-z0-9]+"

#define UNICODE_SPACES "[\\x{0009}-\\x{000d}\\x{0020}\\x{0085}\\x{00a0}\\x{1680}\\x{180E}\\x{2000}-\\x{200a}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}]"
#define AT_SIGNS_CHARS "@\\x{FF20}"
#define AT_SIGNS "[" AT_SIGNS_CHARS "]"
#define VALID_REPLY "^(?:" UNICODE_SPACES ")*" AT_SIGNS "([a-z0-9_]{1,20})"
#define VALID_MENTION_OR_LIST "([^a-z0-9_!#$%&*" AT_SIGNS_CHARS "]|^|RT:?)(" AT_SIGNS "+)([a-z0-9_]{1,20})(/[a-z][a-z0-9_\\-]{0,24})?"
#define INVALID_MENTION_MATCH_END "^(?:[" AT_SIGNS_CHARS LATIN_ACCENTS_CHARS "]|://)"
#define MENTION_NEG_ASSERT "(?![" AT_SIGNS_CHARS LATIN_ACCENTS_CHARS "]|://)"
#define VALID_MENTION_OR_LIST_ASSERT VALID_MENTION_OR_LIST MENTION_NEG_ASSERT
#define IS_USER_MENTIONED_1ST "([^a-z0-9_!#$%&*" AT_SIGNS_CHARS "]|^|RT:?)(" AT_SIGNS "+)("
#define IS_USER_MENTIONED_2ND ")(/[a-z][a-z0-9_\\-]{0,24})?" MENTION_NEG_ASSERT

unsigned int TwitterCharCount(const char *in, size_t inlen) {
	static pcre *pattern=0;
	static pcre_extra *patextra=0;
	static pcre *invprotpattern=0;
	static pcre *tcopattern=0;

	if(!inlen) return 0;

	if(!pattern) {
		const char *errptr;
		int erroffset;
		const char *pat=VALID_URL_PATTERN_STRING;
		pattern=pcre_compile(pat, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!pattern) {
			LogMsgFormat(LFT_OTHERERR, wxT("TwitterCharCount: pcre_compile failed: %s (%d)\n%s"), wxstrstd(errptr).c_str(), erroffset, wxstrstd(pat).c_str());
			return 0;
		}
		patextra=pcre_study(pattern, 0, &errptr);
		invprotpattern=pcre_compile(INVALID_URL_WITHOUT_PROTOCOL_MATCH_BEGIN, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		tcopattern=pcre_compile(VALID_TCO_URL, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!invprotpattern || !tcopattern) return 0;
	}

	char *comp=0;
	unsigned int outsize=0;
	ssize_t len=utf8proc_map((const uint8_t *) in, inlen, (uint8_t **) &comp, UTF8PROC_STABLE | UTF8PROC_COMPOSE);
	if(len>0) {
		outsize=strlen_utf8(comp);
	}
	if(outsize) {
		int startoffset=0;
		int rc;
		do {
			bool https=false;
			int ovector[30];
			rc=pcre_exec(pattern, patextra, comp, len, startoffset, 0, ovector, 30);
			if(rc<=0) break;
			startoffset=ovector[1];
			if(rc<4 || ovector[6]==-1 ) {
				int inv_ovector[30];
				int rc_inv=pcre_exec(invprotpattern, 0, comp+ovector[2], ovector[3]-ovector[2], 0, 0, inv_ovector, 30);
				if(rc_inv>0) continue;
			}
			else {
				if(strncasecmp(comp+ovector[6], "https://", ovector[7]-ovector[6])==0) https=true;
			}
			int tc_ovector[30];
			int tc_inv=pcre_exec(tcopattern, 0, comp+ovector[4], ovector[5]-ovector[4], 0, 0, tc_ovector, 30);
			const char *start;
			size_t bytes;
			if(tc_inv>0) {
				start=comp+ovector[4]+tc_ovector[0];
				bytes=tc_ovector[1]-tc_ovector[0];
			}
			else {
				start=comp+ovector[0];
				bytes=ovector[5]-ovector[4];
			}
			size_t urllen=strlen_utf8(start, bytes);
			outsize-=urllen;
			outsize+=(https)?21:20;
		} while(true);
	}
	free(comp);
	return outsize;
}

bool IsUserMentioned(const char *in, size_t inlen, const std::shared_ptr<userdatacontainer> &u) {
	const char *errptr;
	int erroffset;
	std::string pat=IS_USER_MENTIONED_1ST + u->GetUser().screen_name + IS_USER_MENTIONED_2ND;
	pcre *pattern=pcre_compile(pat.c_str(), PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
	if(!pattern) {
		LogMsgFormat(LFT_OTHERERR, wxT("IsUserMentioned: pcre_compile failed: %s (%d)\n%s"), wxstrstd(errptr).c_str(), erroffset, wxstrstd(pat).c_str());
		return 0;
	}
	int ovector[30];
	int rc=pcre_exec(pattern, 0, in, inlen, 0, 0, ovector, 30);
	return (rc>0);
}
