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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "twitcurlext.h"
#include "log.h"
#include "taccount.h"
#include "parse.h"
#include "mainui.h"
#include "socket.h"
#include "twit.h"
#include "util.h"
#include "tpanel.h"
#include "alldata.h"
#include <wx/msgdlg.h>

BEGIN_EVENT_TABLE( twitcurlext, mcurlconn )
END_EVENT_TABLE()

void twitcurlext::NotifyDoneSuccess(CURL *easy, CURLcode res) {
	std::shared_ptr<taccount> acc=tacc.lock();
	LogMsgFormat(LOGT::OTHERTRACE, wxT("twitcurlext::NotifyDoneSuccess: for conn: %s, account: %s"), GetConnTypeName().c_str(), acc?acc->dispname.c_str():wxT("none"));
	if(!acc) {
		Reset();
		delete this;
		return;
	}

	jsonparser jp(connmode, acc, this);
	std::string str;
	getLastWebResponseMove(str);
	jp.ParseString(str);
	str.clear();

	if(connmode==CS_STREAM && acc->enabled) {
		LogMsgFormat(LOGT::SOCKERR, wxT("Stream connection interrupted, reconnecting: for account: %s"), acc->dispname.c_str());
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
	if(!acc) {
		Reset();
		delete this;
		return;
	}

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
			case RBFS_NULL:
				break;
		}
		char *cururl;
		curl_easy_getinfo(GenGetCurlHandle(), CURLINFO_EFFECTIVE_URL, &cururl);
		this->url = cururl;
		if(currentlogflags&LOGT::NETACT) {
			LogMsgFormat(LOGT::NETACT, wxT("REST timeline fetch: acc: %s, type: %d, num: %d, start_id: %" wxLongLongFmtSpec "d, end_id: %" wxLongLongFmtSpec "d"),
				acc->dispname.c_str(), rbfs->type, tweets_to_get, rbfs->start_tweet_id, rbfs->end_tweet_id);
			LogMsgFormat(LOGT::NETACT, wxT("Executing API call: for account: %s, url: %s"), acc->dispname.c_str(), wxstrstd(cururl).c_str());
		}
		sm.AddConn(*this);
	}
}

twitcurlext::twitcurlext(std::shared_ptr<taccount> acc) {
	TwInit(acc);
}
twitcurlext::twitcurlext() { }

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
	if(tc_flags & TCF::ISSTREAM) {
		if(auto acc = tacc.lock()) {
			if(acc->ta_flags & taccount::TAF::STREAM_UP) {
				acc->ta_flags &= ~taccount::TAF::STREAM_UP;
				time_t now = time(0);
				acc->last_stream_end_time = now;
				for(auto it = acc->user_relations.begin(); it != acc->user_relations.end(); ++it) {
					if(it->second.ifollow_updtime == 0) it->second.ifollow_updtime = now;
					if(it->second.followsme_updtime == 0) it->second.followsme_updtime = now;
				}
			}
		}
	}
	tc_flags = 0;
	scto.reset();
	rbfs = 0;
	ownermainframe = 0;
	extra_id = 0;
	ul.reset();
	fl.reset();
	post_action_flags = 0;
	mp = 0;
}

void twitcurlext::DoRetry() {
	QueueAsyncExec();
}

void twitcurlext::QueueAsyncExec() {
	auto acc=tacc.lock();
	if(!acc) {
		Reset();
		delete this;
		return;
	}

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
			LogMsgFormat(LOGT::NETACT, wxT("Queue AccVerify"));
			accountVerifyCredGet();
			break;
		case CS_TIMELINE:
		case CS_DMTIMELINE:
		case CS_USERTIMELINE:
		case CS_USERFAVS:
			return ExecRestGetTweetBackfill();
		case CS_STREAM:
			LogMsgFormat(LOGT::NETACT, wxT("Queue Stream Connection"));
			mcflags|=MCF::NOTIMEOUT;
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
			if(currentlogflags&LOGT::NETACT) {
				auto lacc = tacc.lock();
				LogMsgFormat(LOGT::NETACT, wxT("About to lookup users: for account: %s, user ids: %s"),
						lacc ? lacc->dispname.c_str() : wxT(""), wxstrstd(userliststr).c_str());
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
	char *cururl;
	curl_easy_getinfo(GenGetCurlHandle(), CURLINFO_EFFECTIVE_URL, &cururl);
	this->url = cururl;
	if(currentlogflags&LOGT::NETACT) {
		LogMsgFormat(LOGT::NETACT, wxT("Executing API call: for account: %s, url: %s"), acc?acc->dispname.c_str():wxT(""), wxstrstd(cururl).c_str());
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
			case RBFS_NULL:
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
	if(!acc) {
		Reset();
		delete this;
		return;
	}

	auto win=MagicWindowCast<panelparentwin_base>(mp);
	if(win) win->NotifyRequestFailed();

	wxString action=GetConnTypeName();
	bool msgbox=false;
	bool retry=false;
	switch(connmode) {
		case CS_STREAM: {
			bool was_stream_mode=(acc->stream_fail_count==0);
			if(was_stream_mode) {
				acc->last_rest_backfill=time(0);	//don't immediately query REST api
				LogMsgFormat(LOGT::SOCKERR, wxT("Stream connection failed, switching to REST api: for account: %s"), acc->dispname.c_str());
			}
			else {
				LogMsgFormat(LOGT::SOCKERR, wxT("Stream reconnection attempt failed: for account: %s"), acc->dispname.c_str());
			}
			acc->cp.Standby(this);
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
		case CS_FRIENDLOOKUP: {
			if(httpcode==404) {	//we have at least one dead user
				if(fl->ids.size()==1) {
					//this is the one
					udc_ptr u=ad.GetUserContainerById(*(fl->ids.begin()));
					u->GetUser().u_flags|=userdata::UF::ISDEAD;
					LogMsgFormat(LOGT::SOCKERR, wxT("Friend lookup failed, bad account: user id: %" wxLongLongFmtSpec "d (%s), (%s)"), u->id, wxstrstd(u->GetUser().screen_name).c_str(), acc->dispname.c_str());
				}
				else if(fl->ids.size()>1) {
					LogMsgFormat(LOGT::SOCKERR, wxT("Friend lookup failed, bisecting...  (%s)"), acc->dispname.c_str());

					twitcurlext *twit=acc->GetTwitCurlExt();
					twit->connmode=CS_FRIENDLOOKUP;
					twit->fl.reset(new friendlookup);

					//do the bisection
					size_t splice_count=fl->ids.size()/2;
					auto start_it=fl->ids.begin();
					auto end_it=fl->ids.begin();
					std::advance(end_it, splice_count);

					twit->fl->ids.insert(start_it, end_it);
					fl->ids.erase(start_it, end_it);

					twit->genurl=twit->fl->GetTwitterURL();
					twit->QueueAsyncExec();

					genurl=fl->GetTwitterURL();
					QueueAsyncExec();    //note this re-uses the original connection for half of the original lookup
					return;
				}
			}
			break;
		}
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
		case CS_SINGLETWEET: break;
		default: break;
	}
	LogMsgFormat(LOGT::SOCKERR, wxT("%s failed (%s)"), action.c_str(), acc->dispname.c_str());
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
		LogMsgFormat(LOGT::SOCKERR, wxT("Retrying failed request: %s (%s)"), action.c_str(), acc->dispname.c_str());
		acc->AddFailedPendingConn(this);
	}
	else acc->cp.Standby(this);
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

	LogMsgFormat(LOGT::SOCKTRACE, wxT("StreamCallback: Received: %s"), wxstrstd(data).c_str());
	jsonparser jp(CS_STREAM, acc, obj);
	jp.ParseString(data);
	data.clear();
}

void StreamActivityCallback( twitCurl* pTwitCurlObj, void *userdata ) {
	twitcurlext *obj=(twitcurlext*) pTwitCurlObj;
	obj->scto->Arm();
	LogMsgFormat(LOGT::SOCKTRACE, wxT("Reset timeout on stream connection %p"), obj);
	std::shared_ptr<taccount> acc=obj->tacc.lock();
	if(acc) {
		acc->CheckFailedPendingConns();
	}
}
