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
#include "log-util.h"
#include "libtwitcurl/urlencode.h"
#include <wx/msgdlg.h>

/* * * * * * * * */
/*  twitcurlext  */
/* * * * * * * * */

void twitcurlext::TwInit(std::shared_ptr<taccount> acc) {
	if (inited) {
		return;
	}

	tacc = acc;

	CURL* ch = GetCurlHandle();
	if (ch) {
		SetCurlHandleVerboseState(ch, currentlogflags & LOGT::CURLVERB);
		SetCacerts(ch);
	}

	setTwitterApiType(twitCurlTypes::eTwitCurlApiFormatJson);
	setTwitterProcotolType(acc->ssl ? twitCurlTypes::eTwitCurlProtocolHttps : twitCurlTypes::eTwitCurlProtocolHttp);

	acc->setOAuthParameters(getOAuth());
	inited = true;
	acc->ApplyNewTwitCurlExtHook(this);
}

void twitcurlext::NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	std::shared_ptr<taccount> acc = tacc.lock();
	LogMsgFormat(LOGT::OTHERTRACE, "twitcurlext::NotifyDoneSuccess: for conn: %s, account: %s", cstr(GetConnTypeName()), acc ? cstr(acc->dispname) : "none");
	if (!acc) {
		return;
	}

	if (ownermainframe && std::find(mainframelist.begin(), mainframelist.end(), ownermainframe.get()) == mainframelist.end()) {
		ownermainframe = nullptr;
	}

	if (!(tc_flags & TCF::ISSTREAM)) {
		jsonparser jp(acc, this);
		std::string str;
		getLastWebResponseMove(str);

		bool ok = jp.ParseString(std::move(str));
		if (ok) {
			if (tc_flags & TCF::ALWAYSREPARSE) {
				jp.data->base_sflags |= JDTP::ALWAYSREPARSE;
			}
			ParseHandler(acc, jp);
		}
	}

	NotifyDoneSuccessState state(easy, res, std::move(this_owner));
	NotifyDoneSuccessHandler(acc, state);

	if (state.do_post_actions) {
		acc->DoPostAction(*this);
	}

	acc->CheckFailedPendingConns();
}

void twitcurlext::HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	auto acc = tacc.lock();
	if (!acc) {
		return;
	}

	auto win = dynamic_cast<panelparentwin_base *>(mp.get());
	if (win) {
		win->NotifyRequestFailed();
	}

	std::string action = GetConnTypeName();

	HandleFailureState state(httpcode, res, std::move(this_owner));
	HandleFailureHandler(acc, state);

	if (state.msgbox) {
		wxString msg, errtype;
		if (res == CURLE_OK) {
			errtype.Printf(wxT("HTTP error code: %d"), httpcode);
		} else {
			errtype.Printf(wxT("Socket error: CURL error code: %d, %s"), res, wxstrstd(curl_easy_strerror(res)).c_str());
		}
		msg.Printf(wxT("Twitter API call of type: %s, has failed\n%s"), wxstrstd(action).c_str(), acc->dispname.c_str(), errtype.c_str());
		::wxMessageBox(msg, wxT("Operation Failed"), wxOK | wxICON_ERROR);
	}
	if (state.retry && (acc->enabled || acc->init)) {
		LogMsgFormat(LOGT::SOCKERR, "Retrying failed request: %s (%s)", cstr(action), cstr(acc->dispname));
		acc->AddFailedPendingConn(static_pointer_cast<twitcurlext>(std::move(this_owner)));
	}
}

std::string twitcurlext::GetFailureLogInfo() {
	auto acc = tacc.lock();
	if (!acc) {
		return "";
	}

	std::string output;

	jsonparser jp(acc, this);
	std::string str = getLastWebResponse();
	if (!str.empty() && jp.ParseString(std::move(str))) {
		std::vector<TwitterErrorMsg> twitter_err_msgs;
		jp.ProcessTwitterErrorJson(twitter_err_msgs);
		for (auto &it : twitter_err_msgs) {
			output += string_format(", Twitter error: (%u: %s)", it.code, cstr(it.message));
		}
	}

	return output;
}

std::string twitcurlext::GetConnTypeName() {
	std::string name = GetConnTypeNameBase();

	auto acc = tacc.lock();
	if (acc) {
		name += " (account: " + stdstrwx(acc->dispname) + ")";
	}
	return name;
}

void twitcurlext::DoQueueAsyncExec(std::unique_ptr<mcurlconn> this_owner) {
	auto acc = tacc.lock();
	if (!acc) {
		return;
	}

	if (!IsQueueable(acc)) {
		return;
	}

	SetNoPerformFlag(true);

	HandleQueueAsyncExec(acc, std::move(this_owner));
	if (!this_owner) {
		// HandleQueueAsyncExec has dealt with it, stop here
		LogMsgFormat(LOGT::SOCKTRACE, "twitcurlext::DoQueueAsyncExec HandleQueueAsyncExec took ownership");
		return;
	}

	char *cururl = nullptr;
	curl_easy_getinfo(GenGetCurlHandle(), CURLINFO_EFFECTIVE_URL, &cururl);
	this->url = cururl;
	LogMsgFormat(LOGT::NETACT, "Executing API call: for account: %s, url: %s", cstr(acc->dispname), cstr(cururl));
	sm.AddConn(static_pointer_cast<twitcurlext>(std::move(this_owner)));
}

bool twitcurlext::IsQueueable(const std::shared_ptr<taccount> &acc) {
	return acc->enabled;
}

void twitcurlext::DoRetry(std::unique_ptr<mcurlconn> &&this_owner) {
	assert(this_owner.get() == this);
	DoQueueAsyncExec(std::move(this_owner));
}

/* * * * * * * * * * * * */
/*  twitcurlext_stream   */
/* * * * * * * * * * * * */

std::unique_ptr<twitcurlext_stream> twitcurlext_stream::make_new(std::shared_ptr<taccount> acc) {
	std::unique_ptr<twitcurlext_stream> twit(new twitcurlext_stream());
	twit->TwInit(std::move(acc));
	twit->tc_flags |= TCF::ISSTREAM;
	return std::move(twit);
}

void twitcurlext_stream::NotifyDoneSuccessHandler(const std::shared_ptr<taccount> &acc, twitcurlext::NotifyDoneSuccessState &state) {
	if (acc->enabled) {
		state.do_post_actions = false;
		LogMsgFormat(LOGT::SOCKERR, "Stream connection interrupted, reconnecting: for account: %s", cstr(acc->dispname));
		DoRetry(std::move(state.this_owner));
	}
}

void twitcurlext_stream::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	// this should not be called
	assert(false);
}

void twitcurlext_stream::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	bool was_stream_mode = (acc->stream_fail_count == 0);
	if (was_stream_mode) {
		acc->last_rest_backfill = time(nullptr);	//don't immediately query REST api
		LogMsgFormat(LOGT::SOCKERR, "Stream connection failed, switching to REST api: for account: %s", cstr(acc->dispname));
	} else {
		LogMsgFormat(LOGT::SOCKERR, "Stream reconnection attempt failed: for account: %s", cstr(acc->dispname));
	}
	acc->stream_fail_count++;
	acc->Exec();
}

std::string twitcurlext_stream::GetConnTypeNameBase() {
	return "Stream connection";
}

void twitcurlext_stream::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	LogMsgFormat(LOGT::NETACT, "Queue Stream Connection");
	mcflags |= MCF::NOTIMEOUT;
	scto.reset(new streamconntimeout(this));
	SetStreamApiCallback(&twitcurlext_stream::StreamCallback, 0);
	SetStreamApiActivityCallback(&twitcurlext_stream::StreamActivityCallback);
	acc->stream_currently_reply_all = (acc->stream_reply_mode != SRM::STD_REPLIES);
	UserStreamingApi("followings", acc->stream_currently_reply_all ? "all" : "");
}

void twitcurlext_stream::AddToRetryQueueNotify() {
	if (scto) {
		auto acc = tacc.lock();
		LogMsgFormat(LOGT::SOCKTRACE, "twitcurlext_stream::AddToRetryQueueNotify: Stopping stream connection timer: %s, conn ID: %d", acc ? cstr(acc->dispname) : "", id);
		scto->Stop();
	}
}

void twitcurlext_stream::RemoveFromRetryQueueNotify() {
	if (scto) {
		auto acc = tacc.lock();
		LogMsgFormat(LOGT::SOCKTRACE, "twitcurlext_stream::RemoveFromRetryQueueNotify: Re-arming stream connection timer: %s, conn ID: %d", acc ? cstr(acc->dispname) : "", id);
		scto->Arm();
	}
}

twitcurlext_stream::~twitcurlext_stream() {
	if (auto acc = tacc.lock()) {
		if (acc && acc->ta_flags & taccount::TAF::STREAM_UP) {
			acc->ta_flags &= ~taccount::TAF::STREAM_UP;
			time_t now = time(nullptr);
			acc->last_stream_end_time = now;
			for (auto it = acc->user_relations.begin(); it != acc->user_relations.end(); ++it) {
				if (it->second.ifollow_updtime == 0) {
					it->second.ifollow_updtime = now;
				}
				if (it->second.followsme_updtime == 0) {
					it->second.followsme_updtime = now;
				}
			}
		}
	}
}

void twitcurlext_stream::StreamCallback(std::string &data, twitCurl *pTwitCurlObj, void *userdata) {
	twitcurlext_stream *obj = static_cast<twitcurlext_stream*>(pTwitCurlObj);
	std::shared_ptr<taccount> acc = obj->tacc.lock();
	if (!acc) {
		return;
	}

	if (acc->stream_fail_count) {
		acc->stream_fail_count = 0;
		acc->streaming_on = true;
		acc->Exec();
	}

	// Retry now if flag set, else retry later
	if (!obj->CheckRetryNowOnSuccessFlag()) {
		sm.RetryConnLater();
	}

	LogMsgFormat(LOGT::SOCKTRACE, "StreamCallback: Received: %s, conn ID: %d", cstr(data), obj->id);
	jsonparser jp(acc, obj);
	bool ok = jp.ParseString(std::move(data));
	if (ok) {
		jp.ProcessStreamResponse();
	}
	data.clear();
}

void twitcurlext_stream::StreamActivityCallback(twitCurl *pTwitCurlObj, void *userdata) {
	twitcurlext_stream *obj = static_cast<twitcurlext_stream*>(pTwitCurlObj);
	obj->scto->Arm();
	LogMsgFormat(LOGT::SOCKTRACE, "Reset timeout on stream conn ID: %d", obj->id);
	std::shared_ptr<taccount> acc = obj->tacc.lock();
	if (acc) {
		acc->CheckFailedPendingConns();
	}
}

/* * * * * * * * * * * */
/*  twitcurlext_rbfs   */
/* * * * * * * * * * * */

static twitcurlext_rbfs::CONNTYPE RbfsTypeToConntype(RBFS_TYPE type) {
	using CONNTYPE = twitcurlext_rbfs::CONNTYPE;
	switch (type) {
		case RBFS_TWEETS:
		case RBFS_MENTIONS:
			return CONNTYPE::TIMELINE;

		case RBFS_RECVDM:
		case RBFS_SENTDM:
			return CONNTYPE::DMTIMELINE;

		case RBFS_USER_TIMELINE:
			return CONNTYPE::USERTIMELINE;

		case RBFS_USER_FAVS:
			return CONNTYPE::USERFAVS;

		case RBFS_NULL:
			break;
	}
	assert(false);
	return CONNTYPE::NONE;
}

std::unique_ptr<twitcurlext_rbfs> twitcurlext_rbfs::make_new(std::shared_ptr<taccount> acc, observer_ptr<restbackfillstate> rbfs) {
	std::unique_ptr<twitcurlext_rbfs> twit(new twitcurlext_rbfs());
	twit->TwInit(std::move(acc));
	twit->rbfs = rbfs;
	twit->conntype = RbfsTypeToConntype(rbfs->type);
	return std::move(twit);
}

void twitcurlext_rbfs::NotifyDoneSuccessHandler(const std::shared_ptr<taccount> &acc, NotifyDoneSuccessState &state) {
	if (rbfs) {
		state.do_post_actions = false;
		DoExecRestGetTweetBackfill(std::move(state.this_owner));
	}
}

void twitcurlext_rbfs::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	/* Save RBFS values which we might want to use
	 * This is because if we later need do a deferred parse, this and hence this->rbfs will be long since out of scope
	 */
	if (rbfs) {
		jp.data->rbfs_userid = rbfs->userid;
		jp.data->rbfs_type = rbfs->type;
	}

	switch (conntype) {
		case CONNTYPE::TIMELINE:
			jp.ProcessTimelineResponse(JDTP::ARRIVED | JDTP::TIMELINERECV, rbfs);
			break;

		case CONNTYPE::DMTIMELINE:
			jp.ProcessTimelineResponse(JDTP::ARRIVED | JDTP::TIMELINERECV | JDTP::ISDM, rbfs);
			break;

		case CONNTYPE::USERTIMELINE:
		case CONNTYPE::USERFAVS: {
			auto win = dynamic_cast<tpanelparentwin_usertweets *>(mp.get());
			if (win)
				win->NotifyRequestSuccess();
			jp.ProcessUserTimelineResponse(rbfs);
			break;
		}

		case CONNTYPE::NONE:
			assert(false);
			break;
	}
}

void twitcurlext_rbfs::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	if (rbfs) {
		bool delrbfs = false;

		if (conntype == CONNTYPE::USERTIMELINE || conntype == CONNTYPE::USERFAVS) {
			delrbfs = true;
		}
		if (rbfs->end_tweet_id == 0) {
			delrbfs = true;
		}

		if (delrbfs) {
			acc->pending_rbfs_list.remove_if ([&](restbackfillstate &r) { return (&r == rbfs.get()); });
		} else {
			state.retry = true;
		}
	}
}

void twitcurlext_rbfs::DoExecRestGetTweetBackfill(std::unique_ptr<mcurlconn> this_owner) {
	auto acc = tacc.lock();
	if (!acc) {
		return;
	}

	bool cleanup = false;
	unsigned int tweets_to_get = std::min((unsigned int) 200, rbfs->max_tweets_left);
	if ((rbfs->end_tweet_id && rbfs->start_tweet_id>rbfs->end_tweet_id) || !rbfs->read_again) {
		cleanup = true;
	} else if (!tweets_to_get) {
		if (rbfs->type == RBFS_TWEETS && gc.assumementionistweet) {
			rbfs->max_tweets_left = 800;
			tweets_to_get = 200;
			rbfs->type = RBFS_MENTIONS;
		} else {
			cleanup = true;
		}
	} else if (rbfs->lastop_recvcount > 0 && rbfs->lastop_recvcount < 175) {    //if less than 175 tweets received in last call, assume that there won't be any more
		cleanup = true;
	}

	if (cleanup) {
		//all done, can now clean up pending rbfs
		acc->pending_rbfs_list.remove_if ([&](restbackfillstate &r) { return (&r == rbfs.get()); });
		rbfs = nullptr;
		acc->DoPostAction(*this);
	} else {
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

		switch (rbfs->type) {
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
		char *cururl = nullptr;
		curl_easy_getinfo(GenGetCurlHandle(), CURLINFO_EFFECTIVE_URL, &cururl);
		this->url = cururl;
		if (currentlogflags & LOGT::NETACT) {
			LogMsgFormat(LOGT::NETACT, "REST timeline fetch: acc: %s, type: %d, num: %d, start_id: %" llFmtSpec "d, end_id: %" llFmtSpec "d",
				cstr(acc->dispname), rbfs->type, tweets_to_get, rbfs->start_tweet_id, rbfs->end_tweet_id);
			LogMsgFormat(LOGT::NETACT, "Executing API call: for account: %s, url: %s", cstr(acc->dispname), cstr(cururl));
		}
		sm.AddConn(static_pointer_cast<twitcurlext>(std::move(this_owner)));
	}
}

std::string twitcurlext_rbfs::GetConnTypeNameBase() {
	std::string name;
	switch (conntype) {
		case CONNTYPE::TIMELINE: name = "Tweet timeline retrieval"; break;
		case CONNTYPE::DMTIMELINE: name = "DM timeline retrieval"; break;
		case CONNTYPE::USERTIMELINE: name = "User timeline retrieval"; break;
		case CONNTYPE::USERFAVS: name = "User favourites retrieval"; break;
		case CONNTYPE::NONE: assert(false); break;
	}

	if (rbfs) {
		switch (rbfs->type) {
			case RBFS_TWEETS:
				name += " (home timeline)";
				break;

			case RBFS_MENTIONS:
				name += " (mentions)";
				break;

			case RBFS_RECVDM:
				name += " (received DMs)";
				break;

			case RBFS_SENTDM:
				name += " (sent DMs)";
				break;

			case RBFS_USER_TIMELINE:
				name += " (user timeline)";
				break;

			case RBFS_USER_FAVS:
				name += " (user favourites)";
				break;

			case RBFS_NULL:
				break;
		}
	}
	return name;
}

void twitcurlext_rbfs::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	DoExecRestGetTweetBackfill(std::move(this_owner));

	this_owner.reset();
}

/* * * * * * * * * * * * * */
/*  twitcurlext_accverify  */
/* * * * * * * * * * * * * */

std::unique_ptr<twitcurlext_accverify> twitcurlext_accverify::make_new(std::shared_ptr<taccount> acc) {
	std::unique_ptr<twitcurlext_accverify> twit(new twitcurlext_accverify());
	twit->TwInit(std::move(acc));
	return std::move(twit);
}

void twitcurlext_accverify::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	jp.ProcessAccVerifyResponse();
}

void twitcurlext_accverify::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	acc->verifycredstatus = ACT_FAILED;
	state.retry = true;
	AccountChangeTrigger();
}

std::string twitcurlext_accverify::GetConnTypeNameBase() {
	return "Verifying twitter account credentials";
}

bool twitcurlext_accverify::TwSyncStartupAccVerify() {
	auto acc = tacc.lock();
	if (!acc) {
		return false;
	}

	acc->verifycredstatus = ACT_INPROGRESS;
	SetNoPerformFlag(false);
	accountVerifyCredGet();
	long httpcode;
	curl_easy_getinfo(GetCurlHandle(), CURLINFO_RESPONSE_CODE, &httpcode);
	acc->verifycredstatus = ACT_FAILED;
	if (httpcode == 200) {
		auto acc = tacc.lock();
		jsonparser jp(acc, this);
		bool res = jp.ParseString(getLastWebResponse());
		if (res) {
			jp.ProcessAccVerifyResponse(); // this sets verifycredstatus to ACT_DONE in PostAccVerifyInit if successful
			return acc->verifycredstatus == ACT_DONE;
		}
	}
	return false;
}

bool twitcurlext_accverify::IsQueueable(const std::shared_ptr<taccount> &acc) {
	return acc->init || acc->enabled;
}

void twitcurlext_accverify::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	LogMsgFormat(LOGT::NETACT, "Queue AccVerify");
	acc->verifycredstatus = ACT_INPROGRESS;
	accountVerifyCredGet();
}

/* * * * * * * * * * * * * * */
/*  twitcurlext_postcontent  */
/* * * * * * * * * * * * * * */

std::unique_ptr<twitcurlext_postcontent> twitcurlext_postcontent::make_new(std::shared_ptr<taccount> acc, twitcurlext_postcontent::CONNTYPE type) {
	std::unique_ptr<twitcurlext_postcontent> twit(new twitcurlext_postcontent());
	twit->TwInit(std::move(acc));
	twit->conntype = type;
	return std::move(twit);
}

void twitcurlext_postcontent::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	switch (conntype) {
		case CONNTYPE::POSTTWEET:
			jp.ProcessSingleTweetResponse(JDTP::ARRIVED);
			break;
		case CONNTYPE::SENDDM:
			jp.ProcessSingleTweetResponse(JDTP::ARRIVED | JDTP::ISDM);
			break;
		case CONNTYPE::NONE:
			assert(false);
			break;
	}
	if (ownermainframe && ownermainframe->tpw) {
		ownermainframe->tpw->NotifyPostResult(true);
	}
}

void twitcurlext_postcontent::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	state.msgbox = true;
	if (ownermainframe && ownermainframe->tpw) {
		ownermainframe->tpw->NotifyPostResult(false);
	}
	ownermainframe = nullptr;
}

std::string twitcurlext_postcontent::GetConnTypeNameBase() {
	switch (conntype) {
		case CONNTYPE::POSTTWEET:
			return "Posting tweet";

		case CONNTYPE::SENDDM:
			return "Sending DM";

		case CONNTYPE::NONE:
			assert(false);
			break;
	}
	return "";
}

void twitcurlext_postcontent::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	switch (conntype) {
		case CONNTYPE::POSTTWEET: {
			if (!IsImageUploadingDone()) {
				// Need to upload media first
				// Transfer this ownership to ums
				std::shared_ptr<upload_media_state> ums = std::make_shared<upload_media_state>(static_pointer_cast<twitcurlext_postcontent>(std::move(this_owner)));
				for (auto &it : image_uploads) {
					twitcurlext::QueueAsyncExec(twitcurlext_uploadmedia::make_new(acc, it, ums));
				}
				return;
			}
			std::string reply = replyto_id ? std::to_string(replyto_id) : "";
			std::string media_upload_ids = string_join(image_uploads, ",", [](std::string &out, const std::shared_ptr<upload_item> &item) {
				out += item->upload_id;
			});
			statusUpdate(text, reply, 1, media_upload_ids);
			break;
		}
		case CONNTYPE::SENDDM:
			directMessageSend(std::to_string(dmtarg_id), text, 1);
			break;
		case CONNTYPE::NONE:
			assert(false);
			break;
	}
	has_been_enqueued = true;
}

bool twitcurlext_postcontent::IsImageUploadingDone() const {
	for (auto &it : image_uploads) {
		if (it->upload_id.empty()) {
			return false;
		}
	}
	return true;
}

void twitcurlext_postcontent::SetImageUploads(const std::vector<std::string> filenames) {
	image_uploads.clear();
	for (auto &it : filenames) {
		image_uploads.push_back(std::make_shared<upload_item>(it));
	}
}

twitcurlext_postcontent::~twitcurlext_postcontent() {
	if (!has_been_enqueued) {
		if (ownermainframe && ownermainframe->tpw) {
			ownermainframe->tpw->NotifyPostResult(false);
		}
		ownermainframe = nullptr;
	}
}

/* * * * * * * * * * * * * * * * * * * * * * * * */
/*  twitcurlext_postcontent::upload_media_state  */
/* * * * * * * * * * * * * * * * * * * * * * * * */

twitcurlext_postcontent::upload_media_state::upload_media_state(std::unique_ptr<twitcurlext_postcontent> content_conn_)
		: content_conn(std::move(content_conn_)) { }

void twitcurlext_postcontent::upload_media_state::UploadSuccess() {
	if (!content_conn) {
		return;
	}

	if (content_conn->IsImageUploadingDone()) {
		// All image uploads done, post content
		twitcurlext::QueueAsyncExec(std::move(content_conn));
	}
}

void twitcurlext_postcontent::upload_media_state::UploadFailure() {
	content_conn.reset();
}

/* * * * * * * * * * * * * * */
/*  twitcurlext_uploadmedia  */
/* * * * * * * * * * * * * * */

std::unique_ptr<twitcurlext_uploadmedia> twitcurlext_uploadmedia::make_new(std::shared_ptr<taccount> acc,
		std::shared_ptr<twitcurlext_postcontent::upload_item> item_, std::shared_ptr<twitcurlext_postcontent::upload_media_state> upload_state_) {
	std::unique_ptr<twitcurlext_uploadmedia> twit(new twitcurlext_uploadmedia());
	twit->TwInit(std::move(acc));
	twit->item = std::move(item_);
	twit->upload_state = std::move(upload_state_);
	return std::move(twit);
}

void twitcurlext_uploadmedia::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	item->upload_id = jp.ProcessUploadMediaResponse();
	if (item->upload_id.empty()) {
		upload_state->UploadFailure();
	} else {
		upload_state->UploadSuccess();
	}
}

void twitcurlext_uploadmedia::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	upload_state->UploadFailure();
}

std::string twitcurlext_uploadmedia::GetConnTypeNameBase() {
	return "Uploading media";
}

void twitcurlext_uploadmedia::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	mediaUpload(item->filename);
	has_been_enqueued = true;
}

twitcurlext_uploadmedia::~twitcurlext_uploadmedia() {
	if (!has_been_enqueued) {
		upload_state->UploadFailure();
	}
}

/* * * * * * * * * * * * * */
/*  twitcurlext_userlist   */
/* * * * * * * * * * * * * */

std::unique_ptr<twitcurlext_userlist> twitcurlext_userlist::make_new(std::shared_ptr<taccount> acc, std::unique_ptr<userlookup> ul_) {
	std::unique_ptr<twitcurlext_userlist> twit(new twitcurlext_userlist());
	twit->TwInit(std::move(acc));
	twit->ul = std::move(ul_);
	return std::move(twit);
}

void twitcurlext_userlist::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	jp.ProcessUserListResponse();
}

void twitcurlext_userlist::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	if (state.httpcode == 404) {    // all users in this query appear to be dead
		for (auto &it : ul->users_queried) {
			LogMsgFormat(LOGT::OTHERTRACE, "Marking user: %s, as dead", cstr(user_short_log_line(it->id)));
			it->udc_flags |= UDC::ISDEAD;
		}
	}
}

std::string twitcurlext_userlist::GetConnTypeNameBase() {
	std::string userliststr;
	ul->GetIdList(userliststr);
	return "User lookup: " + userliststr;
}

void twitcurlext_userlist::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	std::string userliststr;
	ul->GetIdList(userliststr);
	if (userliststr.empty()) {
		//nothing left to look up, stop
		this_owner.reset();
		return;
	}
	LogMsgFormat(LOGT::NETACT, "About to lookup users: for account: %s, user ids: %s",
			cstr(acc->dispname), cstr(userliststr));
	userLookup(userliststr, "", 0);
}

/* * * * * * * * * * * * * * * */
/*  twitcurlext_friendlookup   */
/* * * * * * * * * * * * * * * */

std::unique_ptr<twitcurlext_friendlookup> twitcurlext_friendlookup::make_new(std::shared_ptr<taccount> acc, std::unique_ptr<friendlookup> fl_) {
	std::unique_ptr<twitcurlext_friendlookup> twit(new twitcurlext_friendlookup());
	twit->TwInit(std::move(acc));
	twit->fl = std::move(fl_);
	return std::move(twit);
}

void twitcurlext_friendlookup::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	jp.ProcessFriendLookupResponse();
}

void twitcurlext_friendlookup::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	if (state.httpcode == 404) {    //we have at least one dead user
		if (fl->ids.size() == 1) {
			//this is the one
			udc_ptr u = ad.GetUserContainerById(*(fl->ids.begin()));
			u->GetUser().u_flags|=userdata::UF::ISDEAD;
			LogMsgFormat(LOGT::SOCKERR, "Friend lookup failed, bad account: user id: %" llFmtSpec "d (%s), (%s)", u->id, cstr(u->GetUser().screen_name), cstr(acc->dispname));
		} else if (fl->ids.size() > 1) {
			LogMsgFormat(LOGT::SOCKERR, "Friend lookup failed, bisecting...  (%s)", cstr(acc->dispname));

			std::unique_ptr<twitcurlext_friendlookup> twit = twitcurlext_friendlookup::make_new(acc, std::unique_ptr<friendlookup>(new friendlookup));

			//do the bisection
			size_t splice_count = fl->ids.size() / 2;
			auto start_it = fl->ids.begin();
			auto end_it = fl->ids.begin();
			std::advance(end_it, splice_count);

			twit->fl->ids.insert(start_it, end_it);
			fl->ids.erase(start_it, end_it);

			twitcurlext::QueueAsyncExec(std::move(twit));
			twitcurlext::QueueAsyncExec(static_pointer_cast<twitcurlext>(std::move(state.this_owner)));    //note this re-uses the original connection for half of the original lookup
		}
	}
}

std::string twitcurlext_friendlookup::GetConnTypeNameBase() {
	return "Friend/follower lookup";
}

void twitcurlext_friendlookup::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	genericGet(fl->GetTwitterURL());
}

/* * * * * * * * * * * * * * * */
/*  twitcurlext_userlookupwin  */
/* * * * * * * * * * * * * * * */

std::unique_ptr<twitcurlext_userlookupwin> twitcurlext_userlookupwin::make_new(std::shared_ptr<taccount> acc,
		twitcurlext_userlookupwin::LOOKUPMODE mode, std::string search_string) {
	std::unique_ptr<twitcurlext_userlookupwin> twit(new twitcurlext_userlookupwin());
	twit->TwInit(std::move(acc));
	twit->mode = mode;
	twit->search_string = search_string;
	return std::move(twit);
}

void twitcurlext_userlookupwin::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	jp.ProcessUserLookupWinResponse();
}

void twitcurlext_userlookupwin::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	if (state.httpcode == 404) {
		// No such user
		wxString type;
		switch (mode) {
			case LOOKUPMODE::ID:
				type = wxT("ID");
				break;

			case LOOKUPMODE::SCREENNAME:
				type = wxT("screen name");
				break;
		}
		::wxMessageBox(wxString::Format(wxT("Couldn't find user with %s: '%s'"), type.c_str(), wxstrstd(search_string).c_str()),
				wxT("No such user"), wxOK | wxICON_EXCLAMATION);
	} else {
		// Otherwise display generic failure message box
		state.msgbox = true;
	}
}

std::string twitcurlext_userlookupwin::GetConnTypeNameBase() {
	return "User lookup (user window)";
}

void twitcurlext_userlookupwin::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	std::string url = "api.twitter.com/1.1/users/show.json";

	switch (mode) {
		case LOOKUPMODE::ID:
			url += "?user_id=" + urlencode(search_string);
			break;

		case LOOKUPMODE::SCREENNAME:
			url += "?screen_name=" + urlencode(search_string);
			break;
	}

	genericGet(url);
}

/* * * * * * * * * * * * */
/*  twitcurlext_simple   */
/* * * * * * * * * * * * */

std::unique_ptr<twitcurlext_simple> twitcurlext_simple::make_new(std::shared_ptr<taccount> acc, twitcurlext_simple::CONNTYPE type) {
	std::unique_ptr<twitcurlext_simple> twit(new twitcurlext_simple());
	twit->TwInit(std::move(acc));
	twit->conntype = type;
	return std::move(twit);
}

void twitcurlext_simple::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	switch (conntype) {
		case CONNTYPE::FRIENDACTION_FOLLOW:
		case CONNTYPE::FRIENDACTION_UNFOLLOW:
		case CONNTYPE::BLOCK:
		case CONNTYPE::UNBLOCK:
		case CONNTYPE::MUTE:
		case CONNTYPE::UNMUTE:
			jp.ProcessGenericFriendActionResponse();
			break;

		case CONNTYPE::RT:
			jp.ProcessSingleTweetResponse(JDTP::ARRIVED);
			break;

		case CONNTYPE::FAV:
			jp.ProcessSingleTweetResponse(JDTP::FAV);
			break;

		case CONNTYPE::UNFAV:
			jp.ProcessSingleTweetResponse(JDTP::UNFAV);
			break;

		case CONNTYPE::DELETETWEET:
			jp.ProcessSingleTweetResponse(JDTP::DEL);
			break;

		case CONNTYPE::DELETEDM:
			jp.ProcessSingleTweetResponse(JDTP::ISDM | JDTP::DEL);
			break;

		case CONNTYPE::SINGLETWEET:
			jp.ProcessSingleTweetResponse(JDTP::CHECKPENDINGONLY);
			break;

		case CONNTYPE::SINGLEDM:
			jp.ProcessSingleTweetResponse(JDTP::ISDM | JDTP::CHECKPENDINGONLY);
			break;

		case CONNTYPE::USERFOLLOWING:
		case CONNTYPE::USERFOLLOWERS:
		case CONNTYPE::OWNINCOMINGFOLLOWLISTING:
		case CONNTYPE::OWNOUTGOINGFOLLOWLISTING: {
			auto win = dynamic_cast<tpanelparentwin_userproplisting *>(mp.get());
			if (win) {
				jp.ProcessGenericUserFollowListResponse(win);
			}
			break;
		}

		case CONNTYPE::OWNFOLLOWERLISTING:
			jp.ProcessOwnFollowerListingResponse();
			break;

		case CONNTYPE::NONE:
			assert(false);
			break;
	}
}

void twitcurlext_simple::HandleFailureHandler(const std::shared_ptr<taccount> &acc, twitcurlext::HandleFailureState &state) {
	switch (conntype) {
		case CONNTYPE::FRIENDACTION_FOLLOW:
		case CONNTYPE::FRIENDACTION_UNFOLLOW:
		case CONNTYPE::RT:
		case CONNTYPE::FAV:
		case CONNTYPE::UNFAV:
		case CONNTYPE::DELETETWEET:
		case CONNTYPE::DELETEDM:
		case CONNTYPE::BLOCK:
		case CONNTYPE::UNBLOCK:
		case CONNTYPE::MUTE:
		case CONNTYPE::UNMUTE:
			state.msgbox = true;
			break;

		case CONNTYPE::NONE:
			assert(false);
			break;

		default:
			break;
	}
}

std::string twitcurlext_simple::GetConnTypeNameBase() {
	std::string name;
	switch (conntype) {
		case CONNTYPE::FRIENDACTION_FOLLOW: name = "Follow user"; break;
		case CONNTYPE::FRIENDACTION_UNFOLLOW: name = "Unfollow user"; break;
		case CONNTYPE::RT: name = "Retweeting"; break;
		case CONNTYPE::FAV: name = "Favouriting tweet"; break;
		case CONNTYPE::UNFAV: name = "Unfavouriting tweet"; break;
		case CONNTYPE::DELETETWEET: name = "Deleting tweet"; break;
		case CONNTYPE::DELETEDM: name = "Deleting DM"; break;
		case CONNTYPE::USERFOLLOWING: name = "Retrieving following list"; break;
		case CONNTYPE::USERFOLLOWERS: name = "Retrieving followers list"; break;
		case CONNTYPE::OWNINCOMINGFOLLOWLISTING: name = "Retrieving incoming follower request list"; break;
		case CONNTYPE::OWNOUTGOINGFOLLOWLISTING: name = "Retrieving outgoing following request list"; break;
		case CONNTYPE::SINGLETWEET: name = "Retrieving single tweet"; break;
		case CONNTYPE::SINGLEDM: name = "Retrieving single DM"; break;
		case CONNTYPE::OWNFOLLOWERLISTING: name = "Retrieving own followers listing"; break;
		case CONNTYPE::BLOCK: name = "Block user"; break;
		case CONNTYPE::UNBLOCK: name = "Unblock user"; break;
		case CONNTYPE::MUTE: name = "Mute user"; break;
		case CONNTYPE::UNMUTE: name = "Unmute user"; break;
		case CONNTYPE::NONE: assert(false); break;
	}
	return name;
}

void twitcurlext_simple::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	switch (conntype) {
		case CONNTYPE::FRIENDACTION_FOLLOW:
			friendshipCreate(std::to_string(extra_id), true);
			break;

		case CONNTYPE::FRIENDACTION_UNFOLLOW:
			friendshipDestroy(std::to_string(extra_id), true);
			break;

		case CONNTYPE::RT:
			statusReTweet(std::to_string(extra_id), 1);
			break;

		case CONNTYPE::FAV:
			favoriteCreate(std::to_string(extra_id));
			break;

		case CONNTYPE::UNFAV:
			favoriteDestroy(std::to_string(extra_id));
			break;

		case CONNTYPE::DELETETWEET:
			statusDestroyById(std::to_string(extra_id));
			break;

		case CONNTYPE::DELETEDM:
			directMessageDestroyById(std::to_string(extra_id));
			break;

		case CONNTYPE::USERFOLLOWING:
			friendsIdsGet(std::to_string(extra_id), true);
			break;

		case CONNTYPE::USERFOLLOWERS:
			followersIdsGet(std::to_string(extra_id), true);
			break;

		case CONNTYPE::SINGLETWEET:
			statusShowById(std::to_string(extra_id));
			break;

		case CONNTYPE::SINGLEDM:
			directMessageShowById(std::to_string(extra_id));
			break;

		case CONNTYPE::OWNFOLLOWERLISTING:
			followersIdsGet(std::to_string(acc->usercont->id), true);
			break;
		case CONNTYPE::OWNINCOMINGFOLLOWLISTING:
			followersIncomingIdsGet();
			break;

		case CONNTYPE::OWNOUTGOINGFOLLOWLISTING:
			friendsOutgoingIdsGet();
			break;

		case CONNTYPE::BLOCK:
			blockCreate(std::to_string(extra_id), true);
			break;

		case CONNTYPE::UNBLOCK:
			blockDestroy(std::to_string(extra_id), true);
			break;

		case CONNTYPE::MUTE:
			muteCreate(std::to_string(extra_id), true);
			break;

		case CONNTYPE::UNMUTE:
			muteDestroy(std::to_string(extra_id), true);
			break;

		case CONNTYPE::NONE:
			assert(false);
			break;
	}
}

/* * * * * * * * * * * * * * */
/*  twitcurlext_block_list   */
/* * * * * * * * * * * * * * */

std::unique_ptr<twitcurlext_block_list> twitcurlext_block_list::make_new(std::shared_ptr<taccount> acc, BLOCKTYPE type) {
	std::unique_ptr<twitcurlext_block_list> twit(new twitcurlext_block_list());
	twit->TwInit(std::move(acc));
	twit->blocktype = type;
	return std::move(twit);
}

void twitcurlext_block_list::NotifyDoneSuccessHandler(const std::shared_ptr<taccount> &acc, NotifyDoneSuccessState &state) {
	if (!IsCursored() || current_cursor == 0) {
		// all done
		acc->ReplaceBlockList(blocktype, std::move(block_id_list));
		acc->UpdateBlockListFetchTime(blocktype);
	} else {
		// fetch more
		DoQueueAsyncExec(std::move(state.this_owner));
	}
}

void twitcurlext_block_list::ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) {
	if (IsCursored()) {
		current_cursor = jp.ProcessGetBlockListCursoredResponse(block_id_list);
	} else {
		jp.ProcessRawIdListResponse(block_id_list);
	}
}

std::string twitcurlext_block_list::GetConnTypeNameBase() {
	std::string name;
	switch (blocktype) {
		case BLOCKTYPE::BLOCK: name = "Get block list"; break;
		case BLOCKTYPE::MUTE: name = "Get mute list"; break;
		case BLOCKTYPE::NO_RT: name = "Get no RT list"; break;
	}
	return name;
}

bool twitcurlext_block_list::IsCursored() const {
	return blocktype != BLOCKTYPE::NO_RT;
}

void twitcurlext_block_list::HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) {
	const char *url_stem = nullptr;

	switch (blocktype) {
		case BLOCKTYPE::BLOCK:
			url_stem = "api.twitter.com/1.1/blocks/ids.json";
			break;

		case BLOCKTYPE::MUTE:
			url_stem = "api.twitter.com/1.1/mutes/users/ids.json";
			break;

		case BLOCKTYPE::NO_RT:
			url_stem = "api.twitter.com/1.1/friendships/no_retweets/ids.json";
			break;
	}

	if (IsCursored()) {
		genericGet(string_format("%s?cursor=%" llFmtSpec "d", url_stem, current_cursor));
	} else {
		genericGet(url_stem);
	}
}
