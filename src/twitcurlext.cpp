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

void twitcurlext::NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	std::shared_ptr<taccount> acc=tacc.lock();
	LogMsgFormat(LOGT::OTHERTRACE, "twitcurlext::NotifyDoneSuccess: for conn: %s, account: %s", cstr(GetConnTypeName()), acc ? cstr(acc->dispname) : "none");
	if(!acc) {
		return;
	}

	jsonparser jp(connmode, acc, this);
	std::string str;
	getLastWebResponseMove(str);
	jp.ParseString(str);
	str.clear();

	if(connmode == CS_STREAM && acc->enabled) {
		LogMsgFormat(LOGT::SOCKERR, "Stream connection interrupted, reconnecting: for account: %s", cstr(acc->dispname));
		DoRetry(std::move(this_owner));
	}
	else if(rbfs) {
		ExecRestGetTweetBackfill(static_pointer_cast<twitcurlext>(std::move(this_owner)));
	}
	else {
		acc->DoPostAction(*this);
	}

	acc->CheckFailedPendingConns();
}

void twitcurlext::ExecRestGetTweetBackfill(std::unique_ptr<twitcurlext> conn) {
	// This is to make sure we don't dereference conn to get the vtable/this,
	// after moving the unique_ptr into the argument
	twitcurlext *ptr = conn.get();
	ptr->DoExecRestGetTweetBackfill(std::move(conn));
}

void twitcurlext::DoExecRestGetTweetBackfill(std::unique_ptr<twitcurlext> this_owner) {
	auto acc = tacc.lock();
	if(!acc) {
		return;
	}

	bool cleanup = false;
	unsigned int tweets_to_get = std::min((unsigned int) 200, rbfs->max_tweets_left);
	if((rbfs->end_tweet_id && rbfs->start_tweet_id>rbfs->end_tweet_id) || !rbfs->read_again) {
		cleanup = true;
	}
	else if(!tweets_to_get) {
		if(rbfs->type == RBFS_TWEETS && gc.assumementionistweet) {
			rbfs->max_tweets_left = 800;
			tweets_to_get = 200;
			rbfs->type = RBFS_MENTIONS;
		}
		else cleanup = true;
	}
	else if(rbfs->lastop_recvcount > 0 && rbfs->lastop_recvcount < 175) {    //if less than 175 tweets received in last call, assume that there won't be any more
		cleanup = true;
	}

	if(cleanup) {
		//all done, can now clean up pending rbfs
		acc->pending_rbfs_list.remove_if([&](restbackfillstate &r) { return (&r == rbfs.get()); });
		rbfs = nullptr;
		acc->DoPostAction(*this);
	}
	else {
		rbfs->lastop_recvcount = 0;
		struct timelineparams tmps = {
			tweets_to_get,
			rbfs->start_tweet_id,
			rbfs->end_tweet_id,
			(signed char) ((rbfs->type == RBFS_TWEETS || rbfs->type == RBFS_MENTIONS) ? 1 : 0),
			(signed char) ((rbfs->type == RBFS_TWEETS || rbfs->type == RBFS_MENTIONS) ? 1 : 0),
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
			LogMsgFormat(LOGT::NETACT, "REST timeline fetch: acc: %s, type: %d, num: %d, start_id: %" llFmtSpec "d, end_id: %" llFmtSpec "d",
				cstr(acc->dispname), rbfs->type, tweets_to_get, rbfs->start_tweet_id, rbfs->end_tweet_id);
			LogMsgFormat(LOGT::NETACT, "Executing API call: for account: %s, url: %s", cstr(acc->dispname), cstr(cururl));
		}
		sm.AddConn(std::move(this_owner));
	}
}

twitcurlext::twitcurlext(std::shared_ptr<taccount> acc) : twitcurlext() {
	TwInit(acc);
}
twitcurlext::twitcurlext() {
	CURL* ch = GetCurlHandle();
	if(ch) {
		SetCurlHandleVerboseState(ch, currentlogflags & LOGT::CURLVERB);
	}
}

twitcurlext::~twitcurlext() {
	if(tc_flags & TCF::ISSTREAM) {
		if(auto acc = tacc.lock()) {
			if(acc->ta_flags & taccount::TAF::STREAM_UP) {
				acc->ta_flags &= ~taccount::TAF::STREAM_UP;
				time_t now = time(nullptr);
				acc->last_stream_end_time = now;
				for(auto it = acc->user_relations.begin(); it != acc->user_relations.end(); ++it) {
					if(it->second.ifollow_updtime == 0) it->second.ifollow_updtime = now;
					if(it->second.followsme_updtime == 0) it->second.followsme_updtime = now;
				}
			}
		}
	}
}

void twitcurlext::TwInit(std::shared_ptr<taccount> acc) {
	if(inited) return;
	tacc = acc;
	SetCacerts(GetCurlHandle());

	setTwitterApiType(twitCurlTypes::eTwitCurlApiFormatJson);
	setTwitterProcotolType(acc->ssl?twitCurlTypes::eTwitCurlProtocolHttps:twitCurlTypes::eTwitCurlProtocolHttp);

	acc->setOAuthParameters(getOAuth());
	inited = true;
}

void twitcurlext::TwStartupAccVerify(std::unique_ptr<twitcurlext> conn) {
	// This is to make sure we don't dereference conn to get the vtable/this,
	// after moving the unique_ptr into the argument
	twitcurlext *ptr = conn.get();
	ptr->DoTwStartupAccVerify(std::move(conn));
}

void twitcurlext::DoTwStartupAccVerify(std::unique_ptr<twitcurlext> this_owner) {
	tacc.lock()->verifycredstatus = ACT_INPROGRESS;
	connmode = CS_ACCVERIFY;
	QueueAsyncExec(std::move(this_owner));
}

bool twitcurlext::TwSyncStartupAccVerify() {
	tacc.lock()->verifycredstatus = ACT_INPROGRESS;
	SetNoPerformFlag(false);
	accountVerifyCredGet();
	long httpcode;
	curl_easy_getinfo(GetCurlHandle(), CURLINFO_RESPONSE_CODE, &httpcode);
	if(httpcode == 200) {
		auto acc=tacc.lock();
		jsonparser jp(CS_ACCVERIFY, acc, this);
		std::string str;
		getLastWebResponse(str);
		bool res = jp.ParseString(str);
		str.clear();
		acc->verifycredstatus = ACT_DONE;
		return res;
	}
	else {
		tacc.lock()->verifycredstatus = ACT_FAILED;
		return false;
	}
}

void twitcurlext::DoRetry(std::unique_ptr<mcurlconn> &&this_owner) {
	assert(this_owner.get() == this);
	QueueAsyncExec(std::unique_ptr<twitcurlext>(static_cast<twitcurlext *>(this_owner.release())));
}

void twitcurlext::QueueAsyncExec(std::unique_ptr<twitcurlext> conn) {
	// This is to make sure we don't dereference conn to get the vtable/this,
	// after moving the unique_ptr into the argument
	twitcurlext *ptr = conn.get();
	ptr->DoQueueAsyncExec(std::move(conn));
}

void twitcurlext::DoQueueAsyncExec(std::unique_ptr<twitcurlext> this_owner) {
	auto acc = tacc.lock();
	if(!acc) {
		return;
	}

	if(acc->init && connmode == CS_ACCVERIFY) { }	//OK
	else if(!acc->enabled) {
		if(connmode == CS_POSTTWEET || connmode == CS_SENDDM) {
			if(ownermainframe && ownermainframe->tpw) ownermainframe->tpw->NotifyPostResult(false);
		}
		return;
	}

	SetNoPerformFlag(true);
	switch(connmode) {
		case CS_ACCVERIFY:
			LogMsgFormat(LOGT::NETACT, "Queue AccVerify");
			accountVerifyCredGet();
			break;
		case CS_TIMELINE:
		case CS_DMTIMELINE:
		case CS_USERTIMELINE:
		case CS_USERFAVS:
			twitcurlext::ExecRestGetTweetBackfill(std::move(this_owner));
			return;
		case CS_STREAM:
			LogMsgFormat(LOGT::NETACT, "Queue Stream Connection");
			mcflags |= MCF::NOTIMEOUT;
			scto = std::make_shared<streamconntimeout>(this);
			SetStreamApiCallback(&StreamCallback, 0);
			SetStreamApiActivityCallback(&StreamActivityCallback);
			acc->stream_currently_reply_all = (acc->stream_reply_mode != SRM::STD_REPLIES);
			UserStreamingApi("followings", acc->stream_currently_reply_all ? "all" : "");
			break;
		case CS_USERLIST: {
			std::string userliststr;
			ul->GetIdList(userliststr);
			if(userliststr.empty()) {	//nothing left to look up
				return;
			}
			if(currentlogflags&LOGT::NETACT) {
				auto lacc = tacc.lock();
				LogMsgFormat(LOGT::NETACT, "About to lookup users: for account: %s, user ids: %s",
						lacc ? cstr(lacc->dispname) : "", cstr(userliststr));
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
		case CS_POSTTWEET: {
			std::string reply = extra_id ? std::to_string(extra_id) : "";
			if(extra_array.size()) {
				statusUpdateWithMedia(extra1, extra_array, reply, 1);
			}
			else {
				statusUpdate(extra1, reply, 1);
			}
			break;
		}
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
		LogMsgFormat(LOGT::NETACT, "Executing API call: for account: %s, url: %s", acc ? cstr(acc->dispname) : "", cstr(cururl));
	}
	sm.AddConn(std::move(this_owner));
}

std::string twitcurlext::GetConnTypeName() {
	std::string action;
	switch(connmode) {
		case CS_STREAM: action = "Stream connection"; break;
		case CS_ACCVERIFY: action = "Verifying twitter account credentials"; break;
		case CS_TIMELINE: action = "Tweet timeline retrieval"; break;
		case CS_DMTIMELINE: action = "DM timeline retrieval"; break;
		case CS_USERTIMELINE: action = "User timeline retrieval"; break;
		case CS_USERFAVS: action = "User favourites retrieval"; break;
		case CS_USERLIST: action = "User lookup"; break;
		case CS_FRIENDLOOKUP: action = "Friend/follower lookup"; break;
		case CS_USERLOOKUPWIN: action = "User lookup (user window"; break;
		case CS_FRIENDACTION_FOLLOW: action = "Follow user"; break;
		case CS_FRIENDACTION_UNFOLLOW: action = "Unfollow user"; break;
		case CS_POSTTWEET: action = "Posting tweet"; break;
		case CS_SENDDM: action = "Sending DM"; break;
		case CS_RT: action = "Retweeting"; break;
		case CS_FAV: action = "Favouriting tweet"; break;
		case CS_DELETETWEET: action = "Deleting tweet"; break;
		case CS_DELETEDM: action = "Deleting DM"; break;
		case CS_USERFOLLOWING: action = "Retrieving following list"; break;
		case CS_USERFOLLOWERS: action = "Retrieving followers list"; break;
		case CS_SINGLETWEET: action = "Retrieving single tweet"; break;
		default: action = "Generic twitter API call"; break;
	}
	if(rbfs) {
		switch(rbfs->type) {
			case RBFS_TWEETS:
				action += " (home timeline";
				break;
			case RBFS_MENTIONS:
				action += " (mentions";
				break;
			case RBFS_RECVDM:
				action += " (received DMs";
				break;
			case RBFS_SENTDM:
				action += " (sent DMs";
				break;
			case RBFS_USER_TIMELINE:
				action += " (user timeline";
				break;
			case RBFS_USER_FAVS:
				action += " (user favourites";
				break;
			case RBFS_NULL:
				break;
		}
	}
	auto acc = tacc.lock();
	if(acc) {
		action += " (account: " + stdstrwx(acc->dispname) + ")";
	}
	return action;
}

void twitcurlext::HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	auto acc = tacc.lock();
	if(!acc) {
		return;
	}

	auto win = MagicWindowCast<panelparentwin_base>(mp);
	if(win) win->NotifyRequestFailed();

	std::string action = GetConnTypeName();
	bool msgbox = false;
	bool retry = false;
	switch(connmode) {
		case CS_STREAM: {
			bool was_stream_mode = (acc->stream_fail_count == 0);
			if(was_stream_mode) {
				acc->last_rest_backfill = time(nullptr);	//don't immediately query REST api
				LogMsgFormat(LOGT::SOCKERR, "Stream connection failed, switching to REST api: for account: %s", cstr(acc->dispname));
			}
			else {
				LogMsgFormat(LOGT::SOCKERR, "Stream reconnection attempt failed: for account: %s", cstr(acc->dispname));
			}
			acc->stream_fail_count++;
			acc->Exec();
			return;
		}
		case CS_ACCVERIFY: {
			acc->verifycredstatus = ACT_FAILED;
			retry = true;
			AccountChangeTrigger();
			break;
		}
		case CS_TIMELINE: break;
		case CS_DMTIMELINE: break;
		case CS_USERTIMELINE: break;
		case CS_USERFAVS: break;
		case CS_USERLIST: break;
		case CS_FRIENDLOOKUP: {
			if(httpcode == 404) {    //we have at least one dead user
				if(fl->ids.size() == 1) {
					//this is the one
					udc_ptr u = ad.GetUserContainerById(*(fl->ids.begin()));
					u->GetUser().u_flags|=userdata::UF::ISDEAD;
					LogMsgFormat(LOGT::SOCKERR, "Friend lookup failed, bad account: user id: %" llFmtSpec "d (%s), (%s)", u->id, cstr(u->GetUser().screen_name), cstr(acc->dispname));
				}
				else if(fl->ids.size() > 1) {
					LogMsgFormat(LOGT::SOCKERR, "Friend lookup failed, bisecting...  (%s)", cstr(acc->dispname));

					std::unique_ptr<twitcurlext> twit = acc->GetTwitCurlExt();
					twit->connmode = CS_FRIENDLOOKUP;
					twit->fl.reset(new friendlookup);

					//do the bisection
					size_t splice_count = fl->ids.size() / 2;
					auto start_it = fl->ids.begin();
					auto end_it = fl->ids.begin();
					std::advance(end_it, splice_count);

					twit->fl->ids.insert(start_it, end_it);
					fl->ids.erase(start_it, end_it);

					twit->genurl = twit->fl->GetTwitterURL();
					twitcurlext::QueueAsyncExec(std::move(twit));

					genurl = fl->GetTwitterURL();
					twitcurlext::QueueAsyncExec(static_pointer_cast<twitcurlext>(std::move(this_owner)));    //note this re-uses the original connection for half of the original lookup
					return;
				}
			}
			break;
		}
		case CS_USERLOOKUPWIN: {
			if(httpcode == 404) {
				// No such user
				wxString type = wxT("screen name");
				if(genurl.find("?screen_name=") == std::string::npos) type = wxT("ID");
				::wxMessageBox(wxString::Format(wxT("Couldn't find user with %s: '%s'"), type.c_str(), wxstrstd(extra1).c_str()), wxT("No such user"), wxOK | wxICON_EXCLAMATION);
			}
			else {
				// Otherwise display generic failure message box
				msgbox = true;
			}
			break;
		}
		case CS_FRIENDACTION_FOLLOW: msgbox = true; break;
		case CS_FRIENDACTION_UNFOLLOW: msgbox = true; break;
		case CS_POSTTWEET: {
			msgbox = true;
			if(ownermainframe && ownermainframe->tpw) ownermainframe->tpw->NotifyPostResult(false);
			break;
		}
		case CS_SENDDM: {
			msgbox = true;
			if(ownermainframe && ownermainframe->tpw) ownermainframe->tpw->NotifyPostResult(false);
			break;
		}
		case CS_RT: msgbox = true; break;
		case CS_FAV: msgbox = true; break;
		case CS_DELETETWEET: msgbox = true; break;
		case CS_DELETEDM: msgbox = true; break;
		case CS_USERFOLLOWING: break;
		case CS_USERFOLLOWERS: break;
		case CS_SINGLETWEET: break;
		default: break;
	}
	LogMsgFormat(LOGT::SOCKERR, "%s failed (%s)", cstr(action), cstr(acc->dispname));
	if(msgbox) {
		wxString msg, errtype;
		if(res == CURLE_OK) errtype.Printf(wxT("HTTP error code: %d"), httpcode);
		else errtype.Printf(wxT("Socket error: CURL error code: %d, %s"), res, wxstrstd(curl_easy_strerror(res)).c_str());
		msg.Printf(wxT("Twitter API call of type: %s, has failed\n%s"), wxstrstd(action).c_str(), acc->dispname.c_str(), errtype.c_str());
		::wxMessageBox(msg, wxT("Operation Failed"), wxOK | wxICON_ERROR);
	}
	if(rbfs) {
		bool delrbfs = false;
		if(connmode == CS_USERTIMELINE || connmode == CS_USERFAVS) delrbfs = true;
		if(rbfs->end_tweet_id == 0) delrbfs = true;
		if(delrbfs) {
			acc->pending_rbfs_list.remove_if([&](restbackfillstate &r) { return (&r == rbfs.get()); });
		}
		else retry = true;
	}
	if(retry && (acc->enabled || acc->init)) {
		LogMsgFormat(LOGT::SOCKERR, "Retrying failed request: %s (%s)", cstr(action), cstr(acc->dispname));
		acc->AddFailedPendingConn(static_pointer_cast<twitcurlext>(std::move(this_owner)));
	}
}

void twitcurlext::AddToRetryQueueNotify() {
	auto acc = tacc.lock();
	if(scto) {
		LogMsgFormat(LOGT::SOCKTRACE, "twitcurlext::AddToRetryQueueNotify: Stopping stream connection timer: %s, conn ID: %d", acc ? cstr(acc->dispname) : "", id);
		scto->Stop();
	}
}

void twitcurlext::RemoveFromRetryQueueNotify() {
	auto acc = tacc.lock();
	if(scto) {
		LogMsgFormat(LOGT::SOCKTRACE, "twitcurlext::RemoveFromRetryQueueNotify: Re-arming stream connection timer: %s, conn ID: %d", acc ? cstr(acc->dispname) : "", id);
		scto->Arm();
	}
}

void StreamCallback(std::string &data, twitCurl *pTwitCurlObj, void *userdata) {
	twitcurlext *obj = static_cast<twitcurlext*>(pTwitCurlObj);
	std::shared_ptr<taccount> acc = obj->tacc.lock();
	if(!acc) return;

	if(acc->stream_fail_count) {
		acc->stream_fail_count = 0;
		acc->streaming_on = true;
		acc->Exec();
	}
	sm.RetryConnLater();

	LogMsgFormat(LOGT::SOCKTRACE, "StreamCallback: Received: %s, conn ID: %d", cstr(data), obj->id);
	jsonparser jp(CS_STREAM, acc, obj);
	jp.ParseString(data);
	data.clear();
}

void StreamActivityCallback(twitCurl *pTwitCurlObj, void *userdata) {
	twitcurlext *obj = static_cast<twitcurlext*>(pTwitCurlObj);
	obj->scto->Arm();
	LogMsgFormat(LOGT::SOCKTRACE, "Reset timeout on stream conn ID: %d", obj->id);
	std::shared_ptr<taccount> acc = obj->tacc.lock();
	if(acc) {
		acc->CheckFailedPendingConns();
	}
}
