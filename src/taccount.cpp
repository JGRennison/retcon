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
#include "taccount.h"
#include "twit.h"
#include "alldata.h"
#include "twitcurlext.h"
#include "util.h"
#include "log.h"
#include "userui.h"
#include "mainui.h"
#include "log-util.h"
#include "optui.h"
#include "raii.h"
#include "db.h"
#include "libtwitcurl/oauthlib.h"
#include <wx/timer.h>
#include <wx/dialog.h>
#include <wx/clipbrd.h>

std::vector<std::shared_ptr<taccount> > alist;

BEGIN_EVENT_TABLE(taccount, wxEvtHandler)
	EVT_TIMER(TAF_WINID_RESTTIMER, taccount::OnRestTimer)
	EVT_TIMER(TAF_FAILED_PENDING_CONN_RETRY_TIMER, taccount::OnFailedPendingConnRetryTimer)
	EVT_TIMER(TAF_STREAM_RESTART_TIMER, taccount::OnStreamRestartTimer)
	EVT_TIMER(TAF_NOACC_PENDING_CONTENT_TIMER, taccount::OnNoAccPendingContentTimer)
END_EVENT_TABLE()

taccount::taccount(genoptconf *incfg) {
	if (incfg) {
		cfg.InheritFromParent(*incfg);
		CFGParamConv();
	}
}

void taccount::Setup() {
	if (dispname.Trim(false).Trim(true).IsEmpty()) {
		SetName();
		LogMsgFormat(LOGT::OTHERTRACE, "taccount::Setup: dispname is missing for account: %s, setting to new value: %s", cstr(name), cstr(dispname));
	}
}

void taccount::SetName() {
	auto checkstr = [&](wxString str) -> bool {
		str.Trim(false).Trim(true);
		if (str.IsEmpty()) return false;

		dispname = str;
		return true;
	};

	//NB: short-circuit logic
	checkstr(wxstrstd(usercont->GetUser().name))
		|| checkstr(wxstrstd(usercont->GetUser().screen_name))
		|| checkstr(dispname)
		|| checkstr(wxString::Format(wxT("Account with user ID: %" wxLongLongFmtSpec "d"), usercont->id));
}

void taccount::ClearAllUserRelationshipsByType(user_relationship::UR_TYPE type, std::vector<uint64_t> *currentset, std::vector<uint64_t> *pendingset) {
	using URF = user_relationship::URF;

	auto exec = [&](URF knownflag, URF trueflag, URF pendingflag, time_t user_relationship::* timeptr) {
		for (auto &it : user_relations) {
			if (currentset && it.second.ur_flags & knownflag && it.second.ur_flags & trueflag) {
				currentset->push_back(it.first);
			}
			if (pendingset && it.second.ur_flags & knownflag && it.second.ur_flags & pendingflag) {
				pendingset->push_back(it.first);
			}
			it.second.ur_flags |= knownflag;
			it.second.ur_flags &= ~trueflag;
			it.second.*timeptr = 0;
		}
	};

	switch (type) {
		case user_relationship::UR_TYPE::IFOLLOW:
			exec(URF::IFOLLOW_KNOWN, URF::IFOLLOW_TRUE, URF::IFOLLOW_PENDING, &user_relationship::ifollow_updtime);
			break;

		case user_relationship::UR_TYPE::FOLLOWSME:
			exec(URF::FOLLOWSME_KNOWN, URF::FOLLOWSME_TRUE, URF::FOLLOWSME_PENDING, &user_relationship::followsme_updtime);
			break;
	}
}

void taccount::GetSetUserRelationshipsByType(user_relationship::UR_TYPE type, std::vector<uint64_t> *currentset, std::vector<uint64_t> *pendingset) {
	using URF = user_relationship::URF;

	auto exec = [&](URF knownflag, URF trueflag, URF pendingflag) {
		for (auto &it : user_relations) {
			if (currentset && it.second.ur_flags & knownflag && it.second.ur_flags & trueflag) {
				currentset->push_back(it.first);
			}
			if (pendingset && it.second.ur_flags & knownflag && it.second.ur_flags & pendingflag) {
				pendingset->push_back(it.first);
			}
		}
	};

	switch (type) {
		case user_relationship::UR_TYPE::IFOLLOW:
			exec(URF::IFOLLOW_KNOWN, URF::IFOLLOW_TRUE, URF::IFOLLOW_PENDING);
			break;

		case user_relationship::UR_TYPE::FOLLOWSME:
			exec(URF::FOLLOWSME_KNOWN, URF::FOLLOWSME_TRUE, URF::FOLLOWSME_PENDING);
			break;
	}
}

void taccount::SetUserRelationship(uint64_t userid, flagwrapper<user_relationship::URF> flags, time_t optime) {
	using URF = user_relationship::URF;
	user_relationship &ur = user_relations[userid];
	if (flags & URF::FOLLOWSME_KNOWN) {
		ur.ur_flags &= ~(URF::FOLLOWSME_PENDING | URF::FOLLOWSME_TRUE);
		ur.ur_flags |= URF::FOLLOWSME_KNOWN | (flags & (URF::FOLLOWSME_TRUE | URF::FOLLOWSME_PENDING));
		ur.followsme_updtime = optime;
	}
	if (flags & URF::IFOLLOW_KNOWN) {
		ur.ur_flags &= ~(URF::IFOLLOW_PENDING | URF::IFOLLOW_TRUE);
		ur.ur_flags |= URF::IFOLLOW_KNOWN | (flags&(URF::IFOLLOW_TRUE | URF::IFOLLOW_PENDING));
		ur.ifollow_updtime = optime;
	}
}

bool taccount::IsFollowingUser(uint64_t userid) {
	using URF = user_relationship::URF;

	auto it = user_relations.find(userid);
	if (it == user_relations.end()) {
		return false;
	}

	user_relationship &ur = it->second;
	return (ur.ur_flags & URF::IFOLLOW_KNOWN && ur.ur_flags & URF::IFOLLOW_TRUE);
}

void taccount::LookupFriendships(uint64_t userid) {
	using URF = user_relationship::URF;
	std::unique_ptr<friendlookup> fl(new friendlookup);
	if (userid) {
		if (user_relations[userid].ur_flags & URF::QUERY_PENDING) {
			return;	//already being looked up, don't repeat query
		}
		fl->ids.insert(userid);
		user_relations[userid].ur_flags |= URF::QUERY_PENDING;
	}

	bool opportunist = true;

	if (opportunist) {
		//find out more if users are followed by us or otherwise have a relationship with us

		time_t threshold = time(nullptr) - (7 * 24 * 60 * 60); // last checked 7 days ago
		auto check_update_time = [&](time_t t) -> bool {
			return t > 0 && t < threshold;
		};
		for (auto it = user_relations.begin(); it != user_relations.end() && fl->ids.size() < 100; ++it) {
			if (it->second.ur_flags & URF::QUERY_PENDING) {
				continue;
			}
			if (!(it->second.ur_flags & URF::FOLLOWSME_KNOWN) || !(it->second.ur_flags & URF::IFOLLOW_KNOWN)
					|| check_update_time(it->second.followsme_updtime) || check_update_time(it->second.ifollow_updtime)) {
				udc_ptr usp = ad.GetExistingUserContainerById(it->first);
				if (!(usp && usp->GetUser().u_flags & userdata::UF::ISDEAD)) {
					fl->ids.insert(it->first);
					it->second.ur_flags |= URF::QUERY_PENDING;
				}
			}
		}
	}

	if (fl->ids.empty()) {
		return;
	}

	std::unique_ptr<twitcurlext_friendlookup> twit = twitcurlext_friendlookup::make_new(shared_from_this(), std::move(fl));
	twitcurlext::QueueAsyncExec(std::move(twit));
}

void taccount::GetUsersFollowingMeList() {
	std::unique_ptr<twitcurlext_simple> twit = twitcurlext_simple::make_new(shared_from_this(), twitcurlext_simple::CONNTYPE::OWNFOLLOWERLISTING);
	twitcurlext::QueueAsyncExec(std::move(twit));
}

void taccount::HandleUsersFollowingMeList(std::vector<uint64_t> userids, bool complete) {
	using URF = user_relationship::URF;
	HandleUserRelationshipListCommon(std::move(userids), complete, user_relationship::UR_TYPE::FOLLOWSME, ur_followsme_have_list, URF::FOLLOWSME_KNOWN | URF::FOLLOWSME_TRUE);
}

void taccount::HandleUserIFollowList(std::vector<uint64_t> userids, bool complete) {
	using URF = user_relationship::URF;
	HandleUserRelationshipListCommon(std::move(userids), complete, user_relationship::UR_TYPE::IFOLLOW, ur_ifollow_have_list, URF::IFOLLOW_KNOWN | URF::IFOLLOW_TRUE);
}

void taccount::HandleUserRelationshipListCommon(std::vector<uint64_t> userids, bool complete, user_relationship::UR_TYPE type, bool &listvalid, user_relationship::URF setto) {
	std::vector<uint64_t> oldset, oldpending;
	if (complete) {
		ClearAllUserRelationshipsByType(type, &oldset, &oldpending);
	} else {
		GetSetUserRelationshipsByType(type, &oldset, &oldpending);
	}

	for (uint64_t id : userids) {
		SetUserRelationship(id, setto, 0);
	}

	// Don't notify if this is the first time
	// No point telling the user that they're suddenly following/followed by umpteen users if they add a new account with existing follows/followers
	if (listvalid) {
		NotifyDiffUserRelationshipList(type, oldset, oldpending);
	}
	listvalid = true;
}

void taccount::NotifyDiffUserRelationshipList(user_relationship::UR_TYPE type, const std::vector<uint64_t> &oldset, const std::vector<uint64_t> &oldpending) {
	using URF = user_relationship::URF;

	std::vector<uint64_t> newset, newpending;
	GetSetUserRelationshipsByType(type, &newset, &newpending);

	auto exec = [&](URF knownflag, URF trueflag, URF pendingflag) {
		std::vector<uint64_t> symdiffset, symdiffpending, symdiff;
		std::set_symmetric_difference(
			oldset.begin(), oldset.end(),
			newset.begin(), newset.end(),
			std::back_inserter(symdiffset));
		std::set_symmetric_difference(
			oldpending.begin(), oldpending.end(),
			newpending.begin(), newpending.end(),
			std::back_inserter(symdiffpending));
		std::set_union(
			symdiffset.begin(), symdiffset.end(),
			symdiffpending.begin(), symdiffpending.end(),
			std::back_inserter(symdiff));
		for (uint64_t id : symdiff) {
			user_relationship &ur = user_relations[id];
			if (ur.ur_flags & knownflag) {
				NotifyUserRelationshipChange(id, knownflag | (ur.ur_flags & (trueflag | pendingflag)));
			}
		}
	};

	switch (type) {
		case user_relationship::UR_TYPE::IFOLLOW:
			exec(URF::IFOLLOW_KNOWN, URF::IFOLLOW_TRUE, URF::IFOLLOW_PENDING);
			break;

		case user_relationship::UR_TYPE::FOLLOWSME:
			exec(URF::FOLLOWSME_KNOWN, URF::FOLLOWSME_TRUE, URF::FOLLOWSME_PENDING);
			break;
	}
}

void taccount::NotifyUserRelationshipChange(uint64_t userid, user_relationship::URF flags) {
	udc_ptr u = ad.GetUserContainerById(userid);
	auto acc = shared_from_this();
	auto ready = std::make_shared<exec_on_ready>();
	ready->UserReady(u, exec_on_ready::EOR_UR::CHECK_DB | exec_on_ready::EOR_UR::FETCH_NET | exec_on_ready::EOR_UR::FAST, acc);
	ready->Execute([userid, flags, acc, u]() {
		using URF = user_relationship::URF;
		std::string evttype;
		if (flags & URF::FOLLOWSME_KNOWN) {
			if (flags & URF::FOLLOWSME_TRUE) {
				evttype += ", Followed you";
			} else if (flags & URF::FOLLOWSME_PENDING) {
				evttype += ", Followed you (pending)";
			} else {
				evttype += ", Unfollowed you";
			}
		}
		if (flags & URF::IFOLLOW_KNOWN) {
			if (flags & URF::IFOLLOW_TRUE) {
				evttype += ", You followed";
			} else if (flags & URF::IFOLLOW_PENDING) {
				evttype += ", You followed (pending)";
			} else {
				evttype += ", You unfollowed";
			}
		}
		LogMsgFormat(LOGT::NOTIFYEVT, "taccount::NotifyUserRelationshipChange: %s: %s%s",
				cstr(acc->dispname), cstr(user_short_log_line(userid)), cstr(evttype));
	});

	using URF = user_relationship::URF;
	if (flags & URF::FOLLOWSME_KNOWN) {
		DB_EVENTLOG_TYPE type;
		if (flags & URF::FOLLOWSME_TRUE) {
			type = DB_EVENTLOG_TYPE::FOLLOWED_ME;
		} else if (flags & URF::FOLLOWSME_PENDING) {
			type = DB_EVENTLOG_TYPE::FOLLOWED_ME_PENDING;
		} else {
			type = DB_EVENTLOG_TYPE::UNFOLLOWED_ME;
		}
		DBC_InsertNewEventLogEntry(DBC_GetMessageBatchQueue(), this, type, 0, userid);
	}
	if (flags & URF::IFOLLOW_KNOWN) {
		DB_EVENTLOG_TYPE type;
		if (flags & URF::IFOLLOW_TRUE) {
			type = DB_EVENTLOG_TYPE::I_FOLLOWED;
		} else if (flags & URF::IFOLLOW_PENDING) {
			type = DB_EVENTLOG_TYPE::I_FOLLOWED_PENDING;
		} else {
			type = DB_EVENTLOG_TYPE::I_UNFOLLOWED;
		}
		DBC_InsertNewEventLogEntry(DBC_GetMessageBatchQueue(), this, type, 0, userid);
	}
	user_window::CheckRefresh(userid, false, true);
}

void taccount::NotifyTweetFavouriteEvent(uint64_t tweetid, uint64_t userid, bool unfavourite) {
	auto acc = shared_from_this();
	udc_ptr u = ad.GetUserContainerById(userid);
	auto ready = std::make_shared<exec_on_ready>();
	ready->TweetReady(ad.GetTweetById(tweetid), shared_from_this(), nullptr, PENDING_REQ::USEREXPIRE, PENDING_RESULT::CONTENT_READY);
	ready->UserReady(u, exec_on_ready::EOR_UR::CHECK_DB | exec_on_ready::EOR_UR::FETCH_NET | exec_on_ready::EOR_UR::FAST, acc);
	ready->Execute([tweetid, userid, unfavourite, acc, u]() {
		LogMsgFormat(LOGT::NOTIFYEVT, "taccount::NotifyTweetFavouriteEvent: %s: %s %s %s",
				cstr(acc->dispname), cstr(user_short_log_line(userid)), unfavourite ? "unfavourited" : "favourited", cstr(tweet_short_log_line(tweetid)));
	});
}

void taccount::NotifyBlockListChange(BLOCKTYPE type, uint64_t userid, bool now_blocked) {
	std::string evttype;
	switch (type) {
		case BLOCKTYPE::BLOCK:
			evttype = now_blocked ? "has been blocked" : "has been unblocked";
			break;

		case BLOCKTYPE::MUTE:
			evttype = now_blocked ? "has been muted" : "has been unmuted";
			break;

		case BLOCKTYPE::NO_RT:
			evttype = now_blocked ? "has had retweets disabled" : "has had retweets enabled";
			break;
	}

	auto acc = shared_from_this();
	udc_ptr u = ad.GetUserContainerById(userid);
	auto ready = std::make_shared<exec_on_ready>();
	ready->UserReady(u, exec_on_ready::EOR_UR::CHECK_DB | exec_on_ready::EOR_UR::FETCH_NET | exec_on_ready::EOR_UR::FAST, acc);
	ready->Execute([userid, acc, evttype]() {
		LogMsgFormat(LOGT::NOTIFYEVT, "taccount::NotifyBlockListChange: %s: %s %s",
				cstr(acc->dispname), cstr(user_short_log_line(userid)), cstr(evttype));
	});

	if (u->udc_flags & UDC::WINDOWOPEN) {
		user_window::CheckRefresh(userid, false);
	}
}

useridset &taccount::GetBlockList(BLOCKTYPE type) {
	switch (type) {
		case BLOCKTYPE::BLOCK:
			return blocked_users;

		case BLOCKTYPE::MUTE:
			return muted_users;

		case BLOCKTYPE::NO_RT:
			return no_rt_users;
	}
	__builtin_unreachable();
}

void taccount::UpdateBlockListFetchTime(BLOCKTYPE type) {
	switch (type) {
		case BLOCKTYPE::BLOCK:
			last_block_fetch_time = time(nullptr);
			break;

		case BLOCKTYPE::MUTE:
			last_mute_fetch_time = time(nullptr);
			break;

		case BLOCKTYPE::NO_RT:
			last_no_rt_fetch_time = time(nullptr);
			break;
	}
}

void taccount::ReplaceBlockList(BLOCKTYPE type, useridset new_ids) {
	useridset &current_ids = GetBlockList(type);
	std::vector<uint64_t> symdiff;
	std::set_symmetric_difference(
			current_ids.begin(), current_ids.end(),
			new_ids.begin(), new_ids.end(),
			std::back_inserter(symdiff));
	current_ids = std::move(new_ids);

	for (uint64_t id : symdiff) {
		NotifyBlockListChange(type, id, current_ids.count(id));
	}
}

void taccount::SetUserIdBlockedState(uint64_t user_id, BLOCKTYPE type, bool blocked) {
	useridset &current_ids = GetBlockList(type);
	if (blocked) {
		if (current_ids.insert(user_id).second) {
			NotifyBlockListChange(type, user_id, true);
		}
	} else {
		if (current_ids.erase(user_id)) {
			NotifyBlockListChange(type, user_id, false);
		}
	}
}

void taccount::OnRestTimer(wxTimerEvent& event) {
	SetupRestBackfillTimer();
}

void taccount::SetupRestBackfillTimer() {
	if (!rest_on) return;
	if (!rest_timer) {
		rest_timer.reset(new wxTimer(this, TAF_WINID_RESTTIMER));
	}
	time_t now = time(nullptr);
	time_t targettime = last_rest_backfill + restinterval;
	int timeleft;
	if (targettime <= (now + 10)) {    //10s of error margin
		GetRestBackfill();
		CheckUpdateBlockLists();
		timeleft = restinterval;
	} else {
		timeleft = targettime - now;
	}
	LogMsgFormat(LOGT::OTHERTRACE, "Setting REST timer for %d seconds (%s)", timeleft, cstr(dispname));
	rest_timer->Start(timeleft * 1000, wxTIMER_ONE_SHOT);
}

void taccount::DeleteRestBackfillTimer() {
	if (rest_timer) {
		LogMsgFormat(LOGT::OTHERTRACE, "Deleting REST timer (%s)", cstr(dispname));
		rest_timer.reset();
	}
}

void taccount::GetRestBackfill() {
	auto oktostart = [&](RBFS_TYPE type) {
		bool result = true;
		twitcurlext::IterateConnsByAcc<twitcurlext_rbfs>(shared_from_this(), [&](const twitcurlext_rbfs &it) {
			if (it.rbfs && it.rbfs->type == type && it.rbfs->end_tweet_id == 0) {
				result = false;    //already present
				return true;
			}
			return false;
		});
		return result;
	};

	last_rest_backfill = time(nullptr);
	if (oktostart(RBFS_TWEETS)) {
		StartRestGetTweetBackfill(GetMaxId(RBFS_TWEETS), 0, 800, RBFS_TWEETS);
	}
	if (false && oktostart(RBFS_RECVDM)) {
		StartRestGetTweetBackfill(GetMaxId(RBFS_RECVDM), 0, 800, RBFS_RECVDM);
	}
	if (false && oktostart(RBFS_SENTDM)) {
		StartRestGetTweetBackfill(GetMaxId(RBFS_SENTDM), 0, 800, RBFS_SENTDM);
	}
	if (!gc.assumementionistweet) {
		if (oktostart(RBFS_MENTIONS)) StartRestGetTweetBackfill(GetMaxId(RBFS_MENTIONS), 0, 800, RBFS_MENTIONS);
	}
}

void taccount::CheckUpdateBlockLists() {
	auto oktostart = [&](BLOCKTYPE type) {
		bool result = true;
		twitcurlext::IterateConnsByAcc<twitcurlext_block_list>(shared_from_this(), [&](const twitcurlext_block_list &it) {
			if (it.blocktype == type) {
				result = false;    //already present
				return true;
			}
			return false;
		});
		return result;
	};

	auto trytostart = [&](BLOCKTYPE type, uint64_t last_update_time) {
		if (time(nullptr) > (time_t) (last_update_time + (4 * 60 * 60)) && oktostart(type)) {
			twitcurlext::QueueAsyncExec(twitcurlext_block_list::make_new(shared_from_this(), type));
		}
	};

	trytostart(BLOCKTYPE::BLOCK, last_block_fetch_time);
	trytostart(BLOCKTYPE::MUTE, last_mute_fetch_time);
	trytostart(BLOCKTYPE::NO_RT, last_no_rt_fetch_time);
}

//limits are inclusive
void taccount::StartRestGetTweetBackfill(uint64_t start_tweet_id, uint64_t end_tweet_id, unsigned int max_tweets_to_read, RBFS_TYPE type, uint64_t userid) {
	pending_rbfs_list.emplace_front();
	restbackfillstate *rbfs = &pending_rbfs_list.front();
	rbfs->start_tweet_id = start_tweet_id;
	rbfs->end_tweet_id = end_tweet_id;
	rbfs->max_tweets_left = max_tweets_to_read;
	rbfs->read_again = true;
	rbfs->type = type;
	rbfs->started = false;
	rbfs->lastop_recvcount = 0;
	rbfs->userid = userid;
	ExecRBFS(rbfs);
}

void taccount::ExecRBFS(observer_ptr<restbackfillstate> rbfs) {
	if (rbfs->started) {
		return;
	}
	std::unique_ptr<twitcurlext_rbfs> twit = twitcurlext_rbfs::make_new(shared_from_this(), rbfs);
	twit->post_action_flags = PAF::RESOLVE_PENDINGS;
	twitcurlext::QueueAsyncExec(std::move(twit));
}

void taccount::StartRestQueryPendings() {
	LogMsgFormat(LOGT::PENDTRACE, "taccount::StartRestQueryPendings: pending users: %d, (%s)", pendingusers.size(), cstr(dispname));
	if (pendingusers.empty()) return;

	std::unique_ptr<userlookup> ul;

	auto it = pendingusers.begin();
	while (it != pendingusers.end()) {
		unsigned int numusers = 0;
		while (it != pendingusers.end() && numusers < 100) {
			auto curit = it;
			udc_ptr curobj = curit->second;
			it++;
			if (curobj->udc_flags & UDC::LOOKUP_IN_PROGRESS) {
				//do nothing
			} else if (curobj->udc_flags & UDC::BEING_LOADED_FROM_DB) {
				//do nothing
			} else if (curobj->udc_flags & UDC::ISDEAD) {
				//do nothing
			} else {
				bool ok = false;
				if (curobj->NeedsUpdating(PENDING_REQ::USEREXPIRE) || curobj->udc_flags & UDC::FORCE_REFRESH) {
					ok = true;
				} else {
					// user probably not pending, remove from list for now
					pendingusers.erase(curit);

					// properly check
					curobj->CheckPendingTweets(0, this);

					if (pendingusers.find(curobj->id) != pendingusers.end()) {
						// CheckPendingTweets re-added it to the list
						ok = true;
					}
				}
				if (ok) {
					if (!ul) {
						ul.reset(new userlookup());
					}
					ul->Mark(curobj);
					numusers++;
				}
			}
			curobj->udc_flags &= ~UDC::FORCE_REFRESH;
		}
		if (numusers && ul) {
			std::unique_ptr<twitcurlext_userlist> twit = twitcurlext_userlist::make_new(shared_from_this(), std::move(ul));
			twit->post_action_flags = PAF::RESOLVE_PENDINGS;
			twitcurlext::QueueAsyncExec(std::move(twit));
		}
	}
}

void taccount::DoPostAction(twitcurlext &lasttce) {
	DoPostAction(lasttce.post_action_flags);
}

void taccount::DoPostAction(flagwrapper<PAF> postflags) {
	if (postflags & PAF::RESOLVE_PENDINGS) {
		StartRestQueryPendings();
	}
}

namespace {
	enum {
		BTN_COPYCLIP = 1,
		BTN_BROWSERWIN,
	};

	//broadly based on wxTextEntryDialog
	class OAuthPinDialog : public wxDialog {
		wxTextCtrl *textctrl;
		wxString authurl;

		public:
		OAuthPinDialog(wxWindow *parent, wxString authurl_, wxString &pinout)
				: wxDialog(parent, wxID_ANY, wxT("Enter Twitter PIN"), wxDefaultPosition, wxDefaultSize), authurl(authurl_) {

			wxBoxSizer *vsizer = new wxBoxSizer(wxVERTICAL);
			wxSizerFlags flagsBorder2;
			flagsBorder2.DoubleBorder();

			vsizer->Add(new wxStaticText(this, wxID_ANY, wxT("Enter Twitter PIN")), flagsBorder2);

			textctrl = new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxSize(300, wxDefaultCoord), 0, wxTextValidator(wxFILTER_NUMERIC, &pinout));
			vsizer->Add(textctrl, wxSizerFlags().Expand().TripleBorder(wxLEFT | wxRIGHT));

			wxBoxSizer *btnsizer = new wxBoxSizer(wxHORIZONTAL);
			btnsizer->Add(new wxButton(this, wxID_OK), 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, 3);
			btnsizer->Add(new wxButton(this, BTN_COPYCLIP, wxT("&Copy URL to Clipboard")), 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, 3);
			btnsizer->Add(new wxButton(this, BTN_BROWSERWIN, wxT("&Open Browser Again")), 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, 3);
			btnsizer->Add(new wxButton(this, wxID_CANCEL), 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, 3);

			vsizer->Add(btnsizer, wxSizerFlags(flagsBorder2).Expand());

			SetAutoLayout(true);
			SetSizer(vsizer);

			vsizer->SetSizeHints(this);
			vsizer->Fit(this);

			textctrl->SetSelection(-1, -1);
			textctrl->SetFocus();
		}

		void OnOK(wxCommandEvent& event) {
			if (Validate() && TransferDataFromWindow()) {
				EndModal(wxID_OK);
			}
		}

		void OnCopyToClipboard(wxCommandEvent& event) {
			if (wxTheClipboard->Open()) {
				wxTheClipboard->SetData(new wxTextDataObject(authurl));
				wxTheClipboard->Close();
			}
		}

		void OnBrowserWin(wxCommandEvent& event) {
			wxLaunchDefaultBrowser(authurl);
		}

		DECLARE_EVENT_TABLE()
	};

	BEGIN_EVENT_TABLE(OAuthPinDialog, wxDialog)
		EVT_BUTTON(wxID_OK, OAuthPinDialog::OnOK)
		EVT_BUTTON(BTN_COPYCLIP, OAuthPinDialog::OnCopyToClipboard)
		EVT_BUTTON(BTN_BROWSERWIN, OAuthPinDialog::OnBrowserWin)
	END_EVENT_TABLE()

}

bool taccount::TwDoOAuth(wxWindow *pf, twitcurlext &twit) {
	std::string authUrl;
	twit.SetNoPerformFlag(false);
	twit.oAuthRequestToken(authUrl);
	wxString authUrlWx = wxString::FromUTF8(authUrl.c_str());
	LogMsgFormat(LOGT::OTHERTRACE, "taccount::TwDoOAuth: %s, %s, %s", cstr(cfg.tokenk.val), cstr(cfg.tokens.val), cstr(authUrlWx));
	wxLaunchDefaultBrowser(authUrlWx);
	wxString pin;
	OAuthPinDialog *ted = new OAuthPinDialog(pf, authUrlWx, pin);
	int res = ted->ShowModal();
	ted->Destroy();
	if (res != wxID_OK) return false;
	if (pin.IsEmpty()) return false;
	twit.getOAuth().setOAuthPin((const char*) pin.utf8_str());
	twit.oAuthAccessToken();
	std::string stdconk;
	std::string stdcons;
	twit.getOAuth().getOAuthTokenKey(stdconk);
	twit.getOAuth().getOAuthTokenSecret(stdcons);
	conk = wxString::FromUTF8(stdconk.c_str());
	cons = wxString::FromUTF8(stdcons.c_str());
	return true;
}

void taccount::PostAccVerifyInit() {
	verifycredstatus = ACT_DONE;
	CalcEnabled();
	Exec();
}

std::string taccount::DumpStateString() const {
	return string_format("enabled: %d, userenabled: %d, init: %d, active: %d, streaming_on: %d, stream_fail_count: %u, rest_on: %d, "
			"verifycredstatus: %d, beinginsertedintodb: %d, last_rest_backfill: %u, ssl: %d, userstreams: %d, "
			"stream_reply_mode: %d, stream_currently_reply_all: %d",
			enabled, userenabled, init, active, streaming_on, stream_fail_count, rest_on,
			verifycredstatus, beinginsertedintodb, last_rest_backfill, ssl, userstreams,
			static_cast<int>(stream_reply_mode), stream_currently_reply_all);
}

void taccount::LogStateChange(const std::string &tag, raii_set *finaliser) {
	LogMsgFormat(LOGT::OTHERTRACE, "%s (account: %s). State:            %s", cstr(tag), cstr(dispname), cstr(DumpStateString()));
	if ((currentlogflags & LOGT::OTHERTRACE) && finaliser) {
		auto state = [this]() {
			return std::make_tuple(enabled, userenabled, init, active, streaming_on, stream_fail_count, rest_on,
			verifycredstatus, beinginsertedintodb, last_rest_backfill, ssl, userstreams, stream_reply_mode, stream_currently_reply_all);
		};
		auto oldstate = state();
		finaliser->add([=]() {
			if (oldstate != state()) {
				LogMsgFormat(LOGT::OTHERTRACE, "%s (account: %s). State changed to: %s", cstr(tag), cstr(dispname), cstr(DumpStateString()));
			}
		});
	}
}

void taccount::Exec() {
	raii_set finalisers;
	LogStateChange("taccount::Exec", &finalisers);

	if (init) {
		if (verifycredstatus != ACT_DONE) {
			streaming_on = false;
			rest_on = false;
			active = false;
			if (verifycredstatus == ACT_INPROGRESS) {
				return;
			}
			std::unique_ptr<twitcurlext_accverify> twit = twitcurlext_accverify::make_new(shared_from_this());
			twitcurlext::QueueAsyncExec(std::move(twit));
		}
	}
	else if (enabled) {
		bool target_streaming = userstreams && !stream_fail_count;
		if (!active) {
			for (auto &it : pending_rbfs_list) {
				ExecRBFS(&it);
			}
			NoAccPendingContentCheck();
		} else {
			bool stream_should_reply_all = (stream_reply_mode != SRM::STD_REPLIES);
			if (!target_streaming || stream_currently_reply_all != stream_should_reply_all) {
				twitcurlext::IterateConnsByAcc<twitcurlext_stream>(shared_from_this(), [&](twitcurlext_stream &conn) {
					LogMsgFormat(LOGT::SOCKTRACE, "taccount::Exec(): Closing stream connection: type: %s, conn ID: %d, url: %s",
							cstr(conn.GetConnTypeName()), conn.id, cstr(conn.url));
					conn.KillConn();
					return true;
				});
				streaming_on = false;
			}
			if (target_streaming && rest_on) {
				DeleteRestBackfillTimer();
				rest_on = false;
			}
		}
		active = true;

		if (target_streaming && !streaming_on) {
			streaming_on = true;
			std::unique_ptr<twitcurlext> twit_stream = PrepareNewStreamConn();
			twitcurlext::QueueAsyncExec(std::move(twit_stream));
		}
		if (!target_streaming && !rest_on) {
			rest_on = true;
			SetupRestBackfillTimer();
		}
	} else if (!enabled && (active || (verifycredstatus == ACT_INPROGRESS))) {
		active = false;
		verifycredstatus = ACT_NOTDONE;
		streaming_on = false;
		rest_on = false;
		failed_pending_conns.clear();

		// This is to avoid issues around iterating over the list whilst changing it
		// Build a list of connections to kill, then kill them individually
		std::vector<twitcurlext *> killlist;
		twitcurlext::IterateConnsByAcc<twitcurlext>(shared_from_this(), [&](twitcurlext &conn) {
			killlist.push_back(&conn);
			return false;
		});
		for (auto &it : killlist) {
			it->KillConn();
		}
	}
}

std::unique_ptr<twitcurlext> taccount::PrepareNewStreamConn() {
	std::unique_ptr<twitcurlext_stream> twit_stream = twitcurlext_stream::make_new(shared_from_this());
	twit_stream->post_action_flags |= PAF::STREAM_CONN_READ_BACKFILL;
	return twit_stream;
}

void taccount::CalcEnabled() {
	raii_set finalisers;
	LogStateChange("taccount::CalcEnabled", &finalisers);

	bool oldenabled = enabled;
	bool oldinit = init;
	if (userenabled && !beinginsertedintodb && !gc.allaccsdisabled) {
		enabled = (verifycredstatus == ACT_DONE);
		init = !enabled;
	} else {
		enabled = false;
		init = false;
	}

	if (oldenabled != enabled || oldinit != init) {
		AccountChangeTrigger();
	}
}

void taccount::MarkUserPending(udc_ptr_p user) {
	auto retval = pendingusers.insert(std::make_pair(user->id, user));
	if (retval.second) {
		LogMsgFormat(LOGT::PENDTRACE, "Mark Pending: User: %" llFmtSpec "d (@%s) for account: %s (%s)", user->id, cstr(user->GetUser().screen_name), cstr(name), cstr(dispname));
	}
}

wxString taccount::GetStatusString(bool notextifok) {
	if (init) {
		if (verifycredstatus == ACT_FAILED) {
			return wxT("authentication failed");
		} else {
			return wxT("authenticating");
		}
	} else if (!userenabled) {
		return wxT("disabled");
	} else if (!enabled) {
		return wxT("not active");
	} else if (!notextifok) {
		return wxT("active");
	} else {
		return wxT("");
	}
}

void taccount::CheckFailedPendingConns() {
	if (!failed_pending_conns.empty()) {
		// only try one, to avoid excessive connection cycling
		// if it is successful the remainder will be successively queued
		std::unique_ptr<twitcurlext> conn = std::move(failed_pending_conns.front());
		failed_pending_conns.pop_front();
		twitcurlext::QueueAsyncExec(std::move(conn));
	}
	if (pending_failed_conn_retry_timer) {
		pending_failed_conn_retry_timer->Stop();
	}
	LogMsgFormat(LOGT::SOCKTRACE, "taccount::CheckFailedPendingConns(), stream_fail_count: %d, enabled: %d, userstreams: %d, streaming_on: %d, for account: %s",
			stream_fail_count, enabled, userstreams, streaming_on, cstr(dispname));
	if (CanRestartStreamingConn()) {
		if (!stream_restart_timer) {
			stream_restart_timer.reset(new wxTimer(this, TAF_STREAM_RESTART_TIMER));
		}
		if (!stream_restart_timer->IsRunning()) {
			LogMsgFormat(LOGT::SOCKTRACE, "taccount::CheckFailedPendingConns(), starting stream retry timer");
			stream_restart_timer->Start(90 * 1000, wxTIMER_ONE_SHOT);    //give a little time for any other operations to try to connect first
		}
	}
}

bool taccount::CanRestartStreamingConn() const {
	return stream_fail_count && enabled && userstreams && !streaming_on;
}

void taccount::AddFailedPendingConn(std::unique_ptr<twitcurlext> conn) {
	LogMsgFormat(LOGT::SOCKTRACE, "Connection failed (account: %s). Next reconnection attempt in 512 seconds, or upon successful network activity on this account (whichever is first).",
			cstr(dispname));
	failed_pending_conns.push_back(std::move(conn));
	if (!pending_failed_conn_retry_timer) {
		pending_failed_conn_retry_timer.reset(new wxTimer(this, TAF_FAILED_PENDING_CONN_RETRY_TIMER));
	}
	if (!pending_failed_conn_retry_timer->IsRunning()) {
		pending_failed_conn_retry_timer->Start(512 * 1000, wxTIMER_ONE_SHOT);
	}
}

void taccount::OnFailedPendingConnRetryTimer(wxTimerEvent& event) {
	CheckFailedPendingConns();
}

void taccount::OnStreamRestartTimer(wxTimerEvent& event) {
	TryRestartStreamingConnNow();
}

void taccount::TryRestartStreamingConnNow() {
	LogMsgFormat(LOGT::SOCKTRACE, "taccount::TryRestartStreamingConnNow(), stream_fail_count: %d, enabled: %d, userstreams: %d, streaming_on: %d, for account: %s",
			stream_fail_count, enabled, userstreams, streaming_on, cstr(dispname));

	if (stream_restart_timer) {
		stream_restart_timer->Stop();
	}

	bool have_stream = false;
	twitcurlext::IterateConnsByAcc<twitcurlext_stream>(shared_from_this(), [&](twitcurlext_stream &conn) {
		//stream connection already present
		LogMsgFormat(LOGT::SOCKTRACE, "taccount::TryRestartStreamingConnNow(), stream connection already active, aborting");
		have_stream = true;
		return true;
	});
	if (have_stream) return;

	if (CanRestartStreamingConn()) {
		std::unique_ptr<twitcurlext> twit_stream = PrepareNewStreamConn();
		twit_stream->errorcount = 255;    //disable retry attempts
		twit_stream->mcflags |= mcurlconn::MCF::RETRY_NOW_ON_SUCCESS;
		twitcurlext::QueueAsyncExec(std::move(twit_stream));
	}
}

void taccount::ApplyNewTwitCurlExtHook(observer_ptr<twitcurlext> tce) {
	if (TwitCurlExtHook) {
		TwitCurlExtHook(tce);
	}
}

void taccount::SetNewTwitCurlExtHook(std::function<void(observer_ptr<twitcurlext>)> func) {
	TwitCurlExtHook = std::move(func);
}

void taccount::ClearNewTwitCurlExtHook() {
	TwitCurlExtHook = nullptr;
}

void taccount::OnNoAccPendingContentTimer(wxTimerEvent& event) {
	NoAccPendingContentEvent();
}

void taccount::NoAccPendingContentEvent() {
	if (ad.noacc_pending_tweetobjs.empty() && ad.noacc_pending_userconts.empty()) {
		return;
	}
	LogMsgFormat(LOGT::PENDTRACE, "taccount::NoAccPendingContentEvent: account: %s, About to process %d tweets and %d users",
			cstr(dispname), ad.noacc_pending_tweetobjs.size(), ad.noacc_pending_userconts.size());

	container::hash_map<uint64_t, tweet_ptr> unhandled_tweets;
	container::hash_map<uint64_t, udc_ptr> unhandled_users;

	for (auto &it : ad.noacc_pending_tweetobjs) {
		tweet_ptr t = it.second;
		std::shared_ptr<taccount> curacc;
		if (t->GetUsableAccount(curacc, tweet::GUAF::NOERR)) {
			t->lflags |= TLF::BEINGLOADEDOVERNET;
			std::unique_ptr<twitcurlext_simple> twit = twitcurlext_simple::make_new(curacc, twitcurlext_simple::CONNTYPE::SINGLETWEET);
			twit->extra_id = t->id;
			twitcurlext::QueueAsyncExec(std::move(twit));
		} else {
			unhandled_tweets[t->id] = t;
		}
	}
	ad.noacc_pending_tweetobjs = std::move(unhandled_tweets);

	container::hash_map<unsigned int, taccount *> queried_accs;

	for (auto &it : ad.noacc_pending_userconts) {
		udc_ptr &u = it.second;
		std::shared_ptr<taccount> curacc;
		if (u->GetUsableAccount(curacc, true)) {
			curacc->MarkUserPending(u);
			queried_accs[curacc->dbindex] = curacc.get();
		} else {
			unhandled_users[u->id] = u;
		}
	}
	ad.noacc_pending_userconts = std::move(unhandled_users);

	for (auto &it : queried_accs) {
		it.second->StartRestQueryPendings();
	}

	if (ad.noacc_pending_tweetobjs.empty() && ad.noacc_pending_userconts.empty()) {
		return;
	}
	LogMsgFormat(LOGT::PENDTRACE, "taccount::NoAccPendingContentEvent: account: %s, %d tweets and %d users remain unprocessed",
			cstr(dispname), ad.noacc_pending_tweetobjs.size(), ad.noacc_pending_userconts.size());
}

void taccount::NoAccPendingContentCheck() {
	if (ad.noacc_pending_tweetobjs.empty() && ad.noacc_pending_userconts.empty()) {
		return;
	}

	//check if there are other accounts waiting to become enabled
	bool otheracc = false;
	for (auto &it : alist) {
		if (it.get() == this) {
			continue;
		}
		if (it->userenabled && !it->enabled) {
			otheracc = true;
			break;
		}
	}

	if (otheracc) {
		//delay action to give other accounts a chance to catch up
		if (!noacc_pending_content_timer) {
			noacc_pending_content_timer.reset(new wxTimer(this, TAF_NOACC_PENDING_CONTENT_TIMER));
		}
		noacc_pending_content_timer->Start(90 * 1000, wxTIMER_ONE_SHOT);
	} else {
		NoAccPendingContentEvent();
	}
}

void taccount::setOAuthParameters(oAuth &auth) const {
	auth.setConsumerKey((const char*) cfg.tokenk.val.utf8_str());
	auth.setConsumerSecret((const char*) cfg.tokens.val.utf8_str());

	if (conk.size() && cons.size()) {
		auth.setOAuthTokenKey((const char*) conk.utf8_str());
		auth.setOAuthTokenSecret((const char*) cons.utf8_str());
	}
}

user_relationship_change_guard::user_relationship_change_guard(taccount &acc_, uint64_t userid_)
		: acc(acc_), userid(userid_) {
	rel = acc.user_relations[userid].ur_flags;
}

user_relationship_change_guard::~user_relationship_change_guard() {
	using URF = user_relationship::URF;
	flagwrapper<URF> new_rel = acc.user_relations[userid].ur_flags;

	flagwrapper<URF> follow_mask = URF::IFOLLOW_TRUE | URF::IFOLLOW_PENDING;
	if ((new_rel & URF::IFOLLOW_KNOWN) && (new_rel & follow_mask) != (rel & follow_mask)) {
		acc.NotifyUserRelationshipChange(userid, URF::IFOLLOW_KNOWN | (new_rel & follow_mask));
	}
	flagwrapper<URF> follows_me_mask = URF::FOLLOWSME_TRUE | URF::FOLLOWSME_PENDING;
	if ((new_rel & URF::FOLLOWSME_KNOWN) && (new_rel & follows_me_mask) != (rel & follows_me_mask)) {
		acc.NotifyUserRelationshipChange(userid, URF::FOLLOWSME_KNOWN | (new_rel & follows_me_mask));
	}
}

bool GetAccByDBIndex(unsigned int dbindex, std::shared_ptr<taccount> &acc) {
	for (auto &it : alist) {
		if (it->dbindex == dbindex) {
			acc = it;
			return true;
		}
	}
	return false;
}

void AccountChangeTrigger() {
	user_window::RefreshAll();
	AccountUpdateAllMainframes();
	for (auto &it : acc_window::currentset) {
		it->UpdateLB();
	}
}

void SortAccounts() {
	std::sort(alist.begin(), alist.end(), [](const std::shared_ptr<taccount> &a, const std::shared_ptr<taccount> &b) {
		return a->sort_order < b->sort_order;
	});
}
