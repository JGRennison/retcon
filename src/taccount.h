//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
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

#ifndef HGUARD_SRC_TACCOUNT
#define HGUARD_SRC_TACCOUNT

#include "univdefs.h"
#include "cfg.h"
#include "socket-common.h"
#include "user_relationship.h"
#include "twit-common.h"
#include "twitcurlext-common.h"
#include "rbfs.h"
#include "flags.h"
#include <wx/event.h>
#include <wx/string.h>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <forward_list>
#include <list>

struct twitcurlext;
struct userdatacontainer;
struct restbackfillstate;
struct tweet;

class wxTimer;
class wxTimerEvent;

typedef enum { ACT_NOTDONE, ACT_INPROGRESS, ACT_FAILED, ACT_DONE } ACT_STATUS;

enum {
	TAF_WINID_RESTTIMER=1,
	TAF_FAILED_PENDING_CONN_RETRY_TIMER,
	TAF_STREAM_RESTART_TIMER,
	TAF_NOACC_PENDING_CONTENT_TIMER,
};

struct taccount : public wxEvtHandler, std::enable_shared_from_this<taccount> {
	wxString name;
	wxString dispname;
	genoptconf cfg;
	wxString conk;
	wxString cons;
	bool ssl;
	bool userstreams;
	enum class TAF {
		STREAM_UP			= 1<<0,
	};
	flagwrapper<TAF> ta_flags;
	unsigned long restinterval;	//seconds
	uint64_t max_tweet_id;
	uint64_t max_mention_id;
	uint64_t max_recvdm_id;
	uint64_t max_sentdm_id;

	uint64_t &GetMaxId(RBFS_TYPE type) {
		switch(type) {
			case RBFS_TWEETS: return max_tweet_id;
			case RBFS_MENTIONS: return (gc.assumementionistweet) ? max_tweet_id : max_mention_id;
			case RBFS_RECVDM: return max_recvdm_id;
			case RBFS_SENTDM: return max_sentdm_id;
			default: return max_tweet_id;
		}
	}

	time_t last_stream_start_time;
	time_t last_stream_end_time;
	unsigned int dbindex;
	connpool<twitcurlext> cp;
	std::shared_ptr<userdatacontainer> usercont;
	std::unordered_map<uint64_t,user_relationship> user_relations;

	//any tweet or DM in this list *must* be either in ad.tweetobjs, or in the database
	tweetidset tweet_ids;
	tweetidset dm_ids;

	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > pendingusers;
	std::forward_list<restbackfillstate> pending_rbfs_list;

	std::deque<twitcurlext *> failed_pending_conns;	//strict subset of cp.activeset
	wxTimer *pending_failed_conn_retry_timer;
	wxTimer *stream_restart_timer;
	void CheckFailedPendingConns();
	void AddFailedPendingConn(twitcurlext *conn);
	void OnFailedPendingConnRetryTimer(wxTimerEvent& event);
	void OnStreamRestartTimer(wxTimerEvent& event);

	bool enabled;
	bool userenabled;
	bool init;
	bool active;
	bool streaming_on;
	unsigned int stream_fail_count;
	bool rest_on;
	ACT_STATUS verifycredstatus;
	bool beinginsertedintodb;
	time_t last_rest_backfill;
	wxTimer *rest_timer;

	std::function<void(twitcurlext *)> TwitCurlExtHook;

	void ClearUsersIFollow();
	void SetUserRelationship(uint64_t userid, flagwrapper<user_relationship::URF> flags, const time_t &optime);

	void StartRestGetTweetBackfill(uint64_t start_tweet_id /*lower limit, exclusive*/, uint64_t end_tweet_id /*upper limit, inclusive*/,
			unsigned int max_tweets_to_read, RBFS_TYPE type=RBFS_TWEETS, uint64_t userid = 0);
	void ExecRBFS(restbackfillstate *rbfs);
	void StartRestQueryPendings();
	void DoPostAction(twitcurlext *lasttce);
	void DoPostAction(flagwrapper<PAF> postflags);
	void GetRestBackfill();
	void LookupFriendships(uint64_t userid);

	void MarkUserPending(const std::shared_ptr<userdatacontainer> &user);
	bool MarkPendingOrHandle(const std::shared_ptr<tweet> &t);
	bool CheckMarkPending(const std::shared_ptr<tweet> &t, bool checkfirst = false);
	void FastMarkPending(const std::shared_ptr<tweet> &t, unsigned int mark, bool checkfirst = false);

	void OnRestTimer(wxTimerEvent& event);
	void SetupRestBackfillTimer();
	void DeleteRestBackfillTimer();

	void CFGWriteOut(DBWriteConfig &twfc);
	void CFGReadIn(DBReadConfig &twfc);
	void CFGParamConv();
	bool TwDoOAuth(wxWindow *pf, twitcurlext &twit);
	void PostAccVerifyInit();
	void Exec();
	void CalcEnabled();
	twitcurlext *PrepareNewStreamConn();
	wxString GetStatusString(bool notextifok = false);
	taccount(genoptconf *incfg = 0);
	~taccount();
	twitcurlext *GetTwitCurlExt();
	void SetGetTwitCurlExtHook(std::function<void(twitcurlext *)> func);
	void ClearGetTwitCurlExtHook();

	void OnNoAccPendingContentTimer(wxTimerEvent& event);
	void NoAccPendingContentEvent();
	void NoAccPendingContentCheck();
	wxTimer *noacc_pending_content_timer;

	DECLARE_EVENT_TABLE()
};
template<> struct enum_traits<taccount::TAF> { static constexpr bool flags = true; };

void AccountChangeTrigger();
bool GetAccByDBIndex(unsigned int dbindex, std::shared_ptr<taccount> &acc);

extern std::list<std::shared_ptr<taccount>> alist;

#endif
