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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"

#include "libtwitcurl/twitcurl.h"
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <forward_list>
#include <stack>
#include <unordered_set>
#include <set>
#include <string>
#include <list>
#include <vector>
#include <cmath>
#include <algorithm>
#include <bitset>
#include <queue>
#include <deque>
#include <stdlib.h>
#include <time.h>
#include <wx/window.h>
#include <wx/app.h>
#include <wx/frame.h>
#include <wx/string.h>
#include <wx/menu.h>
#include <wx/event.h>
#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/log.h>
#include <wx/timer.h>
#include <wx/textdlg.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/aui/aui.h>
#include <wx/stdpaths.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/filefn.h>
#include <wx/statbmp.h>
#include <wx/bmpbuttn.h>
#include <wx/aui/auibook.h>
#include <wx/dcmemory.h>
#include <wx/brush.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <wx/file.h>
#include <wx/version.h>

#ifdef _GNU_SOURCE
#include <pthread.h>
#endif

#if wxCHECK_GCC_VERSION(4, 6)	//in old gccs, just leave the warnings turned off
#pragma GCC diagnostic push
#endif
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wuninitialized"
#if wxCHECK_GCC_VERSION(4, 7)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "rapidjson/document.h"
#if wxCHECK_GCC_VERSION(4, 6)
#pragma GCC diagnostic pop
#endif


struct userdata;
struct userdatacontainer;
struct twitcurlext;
struct taccount;
struct tweet;
struct entity;
struct usevents;
struct tweetdispscr;
struct dispscr_base;
struct tpanelparentwin;
struct tpanelparentwin_nt;
struct tpanelparentwin_user;
struct panelparentwin_base;
struct tpanelscrollwin;
struct mcurlconn;
struct socketmanager;
struct mainframe;
struct tpanelnotebook;
struct media_display_win;
struct tpanel;
struct dbsendmsg_list;
struct DBWriteConfig;
struct DBReadConfig;
struct media_id_type;

typedef std::set<uint64_t, std::greater<uint64_t> > tweetidset;		//std::set, sorted in opposite order

typedef enum { ACT_NOTDONE, ACT_INPROGRESS, ACT_FAILED, ACT_DONE } ACT_STATUS;

struct media_id_type {
	uint64_t m_id;
	uint64_t t_id;
	media_id_type() : m_id(0), t_id(0) { }
	operator bool() const { return m_id || t_id; }
};

inline bool operator==(const media_id_type &m1, const media_id_type &m2) {
	return (m1.m_id==m2.m_id) && (m1.t_id==m2.t_id);
}

namespace std {
  template <> struct hash<media_id_type> : public unary_function<media_id_type, size_t>
  {
    inline size_t operator()(const media_id_type & x) const
    {
      return (hash<uint64_t>()(x.m_id)<<1) ^ hash<uint64_t>()(x.t_id);
    }
  };
}

struct cached_id_sets {
	tweetidset unreadids;
	tweetidset highlightids;
	inline void foreach(std::function<void(tweetidset &)> f) {
		f(unreadids);
		f(highlightids);
	}
	inline void foreach(cached_id_sets &cid2, std::function<void(tweetidset &, tweetidset &)> f) {
		f(unreadids, cid2.unreadids);
		f(highlightids, cid2.highlightids);
	}
};

#include "raii.h"
#include "magic_ptr.h"
#include "socket.h"
#include "twit.h"
#include "cfg.h"
#include "parse.h"
#include "uiutil.h"
#include "tpanel.h"
#include "dispscr.h"
#include "mediawin.h"
#include "optui.h"
#include "db.h"
#include "log.h"
#include "cmdline.h"
#include "userui.h"
#include "mainui.h"
#include "signal.h"

//flags for user_relationship::ur_flags
enum {
	URF_FOLLOWSME_KNOWN	= 1<<0,
	URF_FOLLOWSME_TRUE	= 1<<1,
	URF_IFOLLOW_KNOWN	= 1<<2,
	URF_IFOLLOW_TRUE	= 1<<3,
	URF_FOLLOWSME_PENDING	= 1<<4,
	URF_IFOLLOW_PENDING	= 1<<5,
	URF_QUERY_PENDING	= 1<<6,
};

struct user_relationship {
	unsigned int ur_flags;
	time_t followsme_updtime;	//if these are 0 and the corresponding known flag is set, then the value is known to be correct whilst the stream is still up
	time_t ifollow_updtime;
	user_relationship() : ur_flags(0), followsme_updtime(0), ifollow_updtime(0) { }
};

//flags for taccount::ta_flags
enum {
	TAF_STREAM_UP			= 1<<0,
};

enum {
	TAF_WINID_RESTTIMER=1,
	TAF_FAILED_PENDING_CONN_RETRY_TIMER,
	TAF_STREAM_RESTART_TIMER,
};

struct taccount : public wxEvtHandler, std::enable_shared_from_this<taccount> {
	wxString name;
	wxString dispname;
	genoptconf cfg;
	wxString conk;
	wxString cons;
	bool ssl;
	bool userstreams;
	unsigned int ta_flags;
	unsigned long restinterval;	//seconds
	uint64_t max_tweet_id;
	uint64_t max_mention_id;
	uint64_t max_recvdm_id;
	uint64_t max_sentdm_id;

	uint64_t &GetMaxId(RBFS_TYPE type) {
		switch(type) {
			case RBFS_TWEETS: return max_tweet_id;
			case RBFS_MENTIONS: return (gc.assumementionistweet)?max_tweet_id:max_mention_id;
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
	void SetUserRelationship(uint64_t userid, unsigned int flags, const time_t &optime);

	void StartRestGetTweetBackfill(uint64_t start_tweet_id /*lower limit, exclusive*/, uint64_t end_tweet_id /*upper limit, inclusive*/, unsigned int max_tweets_to_read, RBFS_TYPE type=RBFS_TWEETS, uint64_t userid=0);
	void ExecRBFS(restbackfillstate *rbfs);
	void StartRestQueryPendings();
	void DoPostAction(twitcurlext *lasttce);
	void DoPostAction(unsigned int postflags);
	void GetRestBackfill();
	void LookupFriendships(uint64_t userid);

	void MarkUserPending(const std::shared_ptr<userdatacontainer> &user);
	void MarkPendingOrHandle(const std::shared_ptr<tweet> &t);
	bool CheckMarkPending(const std::shared_ptr<tweet> &t, bool checkfirst=false);
	void FastMarkPending(const std::shared_ptr<tweet> &t, unsigned int mark, bool checkfirst=false);

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
	wxString GetStatusString(bool notextifok=false);
	taccount(genoptconf *incfg=0);
	~taccount();
	twitcurlext *GetTwitCurlExt();
	void SetGetTwitCurlExtHook(std::function<void(twitcurlext *)> func);
	void ClearGetTwitCurlExtHook();

	DECLARE_EVENT_TABLE()
};

struct alldata {
	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > userconts;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetobjs;
	std::map<std::string,std::shared_ptr<tpanel> > tpanels;
	std::unordered_map<media_id_type,media_entity> media_list;
	std::unordered_map<std::string,media_id_type> img_media_map;
	unsigned int next_media_id;
	cached_id_sets cids;
	std::vector<twin_layout_desc> twinlayout;
	std::vector<mf_layout_desc> mflayout;
	bool twinlayout_final = false;

	std::shared_ptr<userdatacontainer> &GetUserContainerById(uint64_t id);
	std::shared_ptr<userdatacontainer> *GetExistingUserContainerById(uint64_t id);
	std::shared_ptr<tweet> &GetTweetById(uint64_t id, bool *isnew=0);
	std::shared_ptr<tweet> *GetExistingTweetById(uint64_t id);
	void UnlinkTweetById(uint64_t id);

	alldata() : next_media_id(1) { }
};

class retcon: public wxApp
{
    virtual bool OnInit();
    virtual int OnExit();
    int FilterEvent(wxEvent& event);

public:
    bool term_requested = false;
};

DECLARE_APP(retcon)

inline wxString wxstrstd(const std::string &st) {
	return wxString::FromUTF8(st.c_str());
}
inline wxString wxstrstd(const char *ch) {
	return wxString::FromUTF8(ch);
}
inline wxString wxstrstd(const char *ch, size_t len) {
	return wxString::FromUTF8(ch, len);
}
inline std::string stdstrwx(const wxString &st) {
	return std::string(st.ToUTF8());
}
std::string hexify(const std::string &in);
wxString hexify_wx(const std::string &in);

bool LoadImageFromFileAndCheckHash(const wxString &filename, const unsigned char *hash, wxImage &img);
bool LoadFromFileAndCheckHash(const wxString &filename, const unsigned char *hash, char *&data, size_t &size);

void AccountChangeTrigger();
bool GetAccByDBIndex(unsigned int dbindex, std::shared_ptr<taccount> &acc);

extern std::list<std::shared_ptr<taccount>> alist;
extern socketmanager sm;
extern dbconn dbc;
extern alldata ad;
extern std::vector<mainframe*> mainframelist;
extern std::forward_list<tpanelparentwin_nt*> tpanelparentwinlist;

//fix for MinGW, from http://pastebin.com/7rhvv92A
#ifdef __MINGW32__

#include <string>
#include <sstream>

namespace std
{
    template <typename T>
    string to_string(const T & value)
    {
        stringstream stream;
        stream << value;
        return stream.str();// string_stream;
    }
}
#endif
#ifdef __WINDOWS__
#define strncasecmp _strnicmp
#endif

template <typename C, typename D> inline void ownstrtonum(C &val, D *str, ssize_t len) {
	val=0;
	for(ssize_t i=0; len<0 || i<len; i++) {
		if(str[i]>='0' && str[i]<='9') {
			val*=10;
			val+=str[i]-'0';
		}
		else break;
	}
}
