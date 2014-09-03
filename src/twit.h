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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TWIT
#define HGUARD_SRC_TWIT

#include "univdefs.h"
#include "magic_ptr.h"
#include "twit-common.h"
#include "twitcurlext-common.h"
#include "tpanel-common.h"
#include "flags.h"
#include "hash.h"
#include "media_id_type.h"
#include "set.h"
#include "observer_ptr.h"
#include "map.h"
#include <memory>
#include <functional>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/timer.h>
#include <vector>
#include <string>
#include <forward_list>
#include <deque>

struct tpanelparentwin_nt;
struct tpanel;
struct entity;
struct mainframe;
struct tweetdispscr;
struct media_display_win;
struct twitcurlext;
struct tweet;
struct taccount;
class wxSizer;
struct dbseltweetmsg;
enum class MEF : unsigned int;
struct dbsendmsg_list;

void HandleNewTweet(tweet_ptr_p t, const std::shared_ptr<taccount> &acc, flagwrapper<ARRIVAL> arr);

enum class PENDING_BITS : unsigned char;
template<> struct enum_traits<PENDING_BITS> { static constexpr bool flags = true; };

enum class PENDING_REQ {
	PROFIMG_NEED          = 1<<0,
	PROFIMG_DOWNLOAD_FLAG = 1<<1,
	USEREXPIRE            = 1<<2,
	PROFIMG_DOWNLOAD      = PROFIMG_NEED | PROFIMG_DOWNLOAD_FLAG,
	GUI_DEFAULT           = PROFIMG_NEED | PROFIMG_DOWNLOAD | USEREXPIRE,
	DEFAULT               = GUI_DEFAULT,
};
template<> struct enum_traits<PENDING_REQ> { static constexpr bool flags = true; };

enum class PENDING_RESULT {
	CONTENT_READY          = 1<<0,
	PROFIMG_READY          = 1<<1,

	CONTENT_NOT_READY      = 1<<2,
	PROFIMG_NOT_READY      = 1<<3,

	READY                  = CONTENT_READY | PROFIMG_READY,
	NOT_READY              = CONTENT_NOT_READY | PROFIMG_NOT_READY,
	GUI_DEFAULT            = READY,
	DEFAULT                = GUI_DEFAULT,
};
template<> struct enum_traits<PENDING_RESULT> { static constexpr bool flags = true; };

struct PENDING_RESULT_combiner {
	flagwrapper<PENDING_RESULT> &targ;

	PENDING_RESULT_combiner(flagwrapper<PENDING_RESULT> &targ_) : targ(targ_) { }
	void Combine(flagwrapper<PENDING_RESULT> other) {
		targ &= other & PENDING_RESULT::READY;
		targ |= other & PENDING_RESULT::NOT_READY;
	}
};

inline flagwrapper<PENDING_REQ> ConstPendingReq(flagwrapper<PENDING_REQ> preq) {
	return preq & PENDING_REQ::USEREXPIRE;
}

enum class PENDING_BITS : unsigned char {
	T_U                = 1<<0,
	T_UR               = 1<<1,
	RT_RTU             = 1<<2,
	U                  = 1<<3,
	UR                 = 1<<4,
	RTU                = 1<<5,
	RT_MISSING         = 1<<6,
	ACCMASK            = U | UR | RTU,
	NONACCMASK         = T_U | T_UR | RT_RTU | RT_MISSING,
};

struct userdata {
	enum class UF {
		ISPROTECTED           = 1<<0,
		ISVERIFIED            = 1<<1,
		ISDEAD                = 1<<2,
	};

	std::string name;
	std::string screen_name;
	std::string profile_img_url;
	flagwrapper<UF> u_flags = 0;
	time_t createtime = 0;
	std::string description;
	std::string location;
	unsigned int statuses_count = 0;
	unsigned int followers_count = 0;    //users following this account
	unsigned int friends_count = 0;      //users this account is following
	unsigned int favourites_count = 0;   //tweets this account has faved
	std::string userurl;

	//Any change to any other member of this struct, should increment this value
	unsigned int revision_number = 0;
};
template<> struct enum_traits<userdata::UF> { static constexpr bool flags = true; };

enum class UDC {
	LOOKUP_IN_PROGRESS        = 1<<0,
	IMAGE_DL_IN_PROGRESS      = 1<<1,
	THIS_IS_ACC_USER_HINT     = 1<<2,
	PROFILE_BITMAP_SET        = 1<<3,
	HALF_PROFILE_BITMAP_SET   = 1<<4,
	WINDOWOPEN                = 1<<5,
	FORCE_REFRESH             = 1<<6,
	FRIENDACT_IN_PROGRESS     = 1<<7,
	CHECK_USERLISTWIN         = 1<<8,
	PROFILE_IMAGE_DL_FAILED   = 1<<9,
};
template<> struct enum_traits<UDC> { static constexpr bool flags = true; };

struct userdatacontainer {
	uint64_t id = 0;
	userdata user;
	uint64_t lastupdate = 0;
	uint64_t lastupdate_wrotetodb = 0;
	flagwrapper<UDC> udc_flags;

	std::string cached_profile_img_url;
	shb_iptr cached_profile_img_sha1;
	wxBitmap cached_profile_img;
	wxBitmap cached_profile_img_half;
	std::deque<tweet_ptr> pendingtweets;
	std::deque<uint64_t> mention_index;    //append only

	uint64_t profile_img_last_used = 0;
	uint64_t profile_img_last_used_db = 0;

	private:
	struct mention_set_data {
		tweetidset mention_set;
		size_t added_offset = 0;
	};
	std::unique_ptr<mention_set_data> msd;

	struct dm_set_data {
		tweetidset dm_set;
	};
	std::unique_ptr<dm_set_data> dsd;

	public:
	bool NeedsUpdating(flagwrapper<PENDING_REQ> preq, time_t timevalue = 0) const;
	flagwrapper<PENDING_RESULT> GetPending(flagwrapper<PENDING_REQ> preq = PENDING_REQ::DEFAULT, time_t timevalue = 0);
	bool IsReady(flagwrapper<PENDING_REQ> preq = PENDING_REQ::DEFAULT, time_t timevalue = 0) {
		return !(GetPending(preq, timevalue) & PENDING_RESULT::NOT_READY);
	}
	void CheckPendingTweets(flagwrapper<UMPTF> umpt_flags = 0);
	void MarkTweetPending(tweet_ptr_p t);
	std::shared_ptr<taccount> GetAccountOfUser() const;
	void GetImageLocalFilename(wxString &filename)  const;
	inline userdata &GetUser() { return user; }
	inline const userdata &GetUser() const { return user; }
	void MarkUpdated();
	std::string mkjson() const;
	static wxImage ScaleImageToProfileSize(const wxImage &img, double limitscalefactor = 1.0);
	void SetProfileBitmap(const wxBitmap &bmp);
	void Dump() const;
	bool ImgIsReady(flagwrapper<PENDING_REQ> preq);
	bool ImgHalfIsReady(flagwrapper<PENDING_REQ> preq);
	bool GetUsableAccount(std::shared_ptr<taccount> &tac, bool enabledonly = true) const;
	std::string GetPermalink(bool ssl) const;
	void NotifyProfileImageChange();
	void MakeProfileImageFailurePlaceholder();
	const tweetidset &GetMentionSet();
	const tweetidset &GetDMSet();
	void SetDMSet(tweetidset dmset);
	void AddDMIdToSet(uint64_t id);
	inline bool MayHaveDMSet() const { return (bool) dsd; }
};

class tweet_perspective {
	enum {
		TP_IAH           = 1<<0,
		TP_FAV           = 1<<1,
		TP_RET           = 1<<2,
		TP_RECV          = 1<<3,
		TP_RECV_DEL      = 1<<4,
		TP_RECV_CPO      = 1<<5,
		TP_RECV_UT       = 1<<6,
		TP_RECV_NORM     = 1<<7,
		TP_RECV_RTSRC    = 1<<8,
	};

	public:
	std::shared_ptr<taccount> acc;

	tweet_perspective(const std::shared_ptr<taccount> &tac) : acc(tac) { }
	tweet_perspective() { }
	void Reset(const std::shared_ptr<taccount> &tac) { acc = tac; }
	void Load(unsigned int fl) { flags = fl; }
	unsigned int Save() const { return flags; }

	bool IsArrivedHere() const { return flags & TP_IAH; }
	bool IsFavourited() const { return flags & TP_FAV; }
	bool IsRetweeted() const { return flags & TP_RET; }
	bool IsReceivedHere() const { return flags & TP_RECV; }
	void SetArrivedHere(bool val) { if(val) flags |= TP_IAH; else flags &= ~TP_IAH; }
	void SetFavourited(bool val) { if(val) flags |= TP_FAV; else flags &= ~TP_FAV; }
	void SetRetweeted(bool val) { if(val) flags |= TP_RET; else flags &= ~TP_RET; }
	void SetReceivedHere(bool val) { if(val) flags |= TP_RECV; else flags &= ~TP_RECV; }
	void SetRecvTypeDel(bool val) { if(val) flags |= TP_RECV_DEL; else flags &= ~TP_RECV_DEL; }
	void SetRecvTypeCPO(bool val) { if(val) flags |= TP_RECV_CPO; else flags &= ~TP_RECV_CPO; }
	void SetRecvTypeUT(bool val) { if(val) flags |= TP_RECV_UT; else flags &= ~TP_RECV_UT; }
	void SetRecvTypeNorm(bool val) { if(val) flags |= TP_RECV_NORM; else flags &= ~TP_RECV_NORM; }
	void SetRecvTypeRTSrc(bool val) { if(val) flags |= TP_RECV_RTSRC; else flags &= ~TP_RECV_RTSRC; }

	std::string GetFlagString() const;
	std::string GetFlagStringWithName(bool always = false) const;

	protected:
	unsigned int flags = 0;
};

struct pending_op {
	//give these sensible defaults
	flagwrapper<PENDING_REQ> preq = PENDING_REQ::DEFAULT;
	flagwrapper<PENDING_RESULT> presult_required = PENDING_RESULT::DEFAULT;

	virtual ~pending_op() { }

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) = 0;
	virtual std::string dump() = 0;
};

struct rt_pending_op : public pending_op {
	tweet_ptr target_retweet;
	rt_pending_op(tweet_ptr_p t) : target_retweet(t) { }

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags);
	virtual std::string dump();
};

struct tpanelload_pending_op : public pending_op {
	magic_ptr_ts<tpanelparentwin_nt> win;
	std::weak_ptr<tpanel> pushtpanel;
	flagwrapper<PUSHFLAGS> pushflags;

	tpanelload_pending_op(tpanelparentwin_nt* win_, flagwrapper<PUSHFLAGS> pushflags_ = PUSHFLAGS::DEFAULT, std::shared_ptr<tpanel> *pushtpanel_ = nullptr);

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags);
	virtual std::string dump();
};

struct handlenew_pending_op : public pending_op {
	std::weak_ptr<taccount> tac;
	flagwrapper<ARRIVAL> arr;
	handlenew_pending_op(const std::shared_ptr<taccount> &acc, flagwrapper<ARRIVAL> arr_) : tac(acc), arr(arr_) { }

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags);
	virtual std::string dump();
};

enum class TLF {    //for tweet.lflags
	DYNDIRTY             = 1<<0,
	BEINGLOADEDFROMDB    = 1<<1,
	SAVED_IN_DB          = 1<<3,
	BEINGLOADEDOVERNET   = 1<<4,
	HAVEFIRSTTP          = 1<<5,
	SHOULDSAVEINDB       = 1<<6,
	LOADED_FROM_DB       = 1<<7,
	ISPENDING            = 1<<8,
	REFCOUNT_WENT_GT1    = 1<<9,
};
template<> struct enum_traits<TLF> { static constexpr bool flags = true; };

struct tweet_pending {
	flagwrapper<PENDING_RESULT> result;
	flagwrapper<PENDING_BITS> bits;

	bool IsReady(flagwrapper<PENDING_RESULT> required = PENDING_RESULT::DEFAULT) {
		return (result & required) == required;
	}
};

struct tweet {
	uint64_t id = 0;
	uint64_t in_reply_to_status_id = 0;
	unsigned int retweet_count = 0;
	unsigned int favourite_count = 0;
	std::string source;
	std::string text;
	time_t createtime = 0;
	udc_ptr user;                    //for DMs this is the sender
	udc_ptr user_recipient;          //for DMs this is the recipient, for tweets, unset
	std::vector<entity> entlist;
	tweet_perspective first_tp;
	std::vector<tweet_perspective> tp_extra_list;
	tweet_ptr rtsrc;                 //for retweets, this is the source tweet
	std::vector<std::unique_ptr<pending_op> > pending_ops;

	tweet_flags flags;
	flagwrapper<TLF> lflags = 0;

	private:
	tweet_flags flags_at_prev_update;

	int refcount = 0;

	public:
	void intrusive_ptr_increment() {
		refcount++;
		if(refcount > 1) lflags |= TLF::REFCOUNT_WENT_GT1;
	};
	void intrusive_ptr_decrement() {
		refcount--;
		if(refcount == 0) delete this;
	};

	tweet() { }
	void Dump() const;
	tweet_perspective *AddTPToTweet(const std::shared_ptr<taccount> &tac, bool *isnew = 0);
	tweet_perspective *GetTweetTP(const std::shared_ptr<taccount> &tac);
	std::string mkdynjson() const;

	enum class GUAF {
		CHECKEXISTING        = 1<<0,
		NOERR                = 1<<1,
		USERENABLED          = 1<<2,  // Use this account even if not enabled, if userenabled
	};
	bool GetUsableAccount(std::shared_ptr<taccount> &tac, flagwrapper<GUAF> guaflags = 0) const;

	tweet_pending IsPending(flagwrapper<PENDING_REQ> preq = PENDING_REQ::DEFAULT);
	tweet_pending IsPendingConst(flagwrapper<PENDING_REQ> preq = PENDING_REQ::DEFAULT) const { return const_cast<tweet *>(this)->IsPending(ConstPendingReq(preq)); }
	bool IsFavouritable() const;
	bool IsRetweetable() const;
	bool IsArrivedHereAnyPerspective() const;
	std::string GetPermalink() const;
	void MarkFlagsAsRead();
	inline void IterateTP(std::function<void(const tweet_perspective &)> f) const {
		if(lflags & TLF::HAVEFIRSTTP) f(first_tp);
		for(auto &it : tp_extra_list) f(it);
	}
	inline void IterateTP(std::function<void(tweet_perspective &)> f) {
		if(lflags & TLF::HAVEFIRSTTP) f(first_tp);
		for(auto &it : tp_extra_list) f(it);
	}

	//If mask is zero, it is not used
	void GetMediaEntities(std::vector<media_entity *> &out, flagwrapper<MEF> mask = 0) const;

	enum class CFUF {
		SEND_DB_UPDATE             = 1<<0,
		SEND_DB_UPDATE_ALWAYS      = 1<<1,
		UPDATE_TWEET               = 1<<2,
		SET_NOUPDF_ALL             = 1<<3,
	};
	void CheckFlagsUpdated(flagwrapper<CFUF> cfuflags = 0);

	//! Use with caution
	//! Intended for bulk CIDS operations which do their own state tracking
	//! This is also used when loading tweets out of the DB to set flags_at_prev_update to flags
	void IgnoreChangeToFlagsByMask(unsigned long long mask) {
		tweet_flags tmask(mask);
		flags_at_prev_update &= ~tmask;
		flags_at_prev_update |= flags & tmask;
	}

	tweet_flags GetFlagsAtPrevUpdate() const { return flags_at_prev_update; }

	void AddNewPendingOp(pending_op *op) {
		pending_ops.emplace_back(op);
	}
	bool HasPendingOps() const {
		return !pending_ops.empty();
	}

	int GetRefcount() const {
		return refcount;
	};
};
template<> struct enum_traits<tweet::GUAF> { static constexpr bool flags = true; };
template<> struct enum_traits<tweet::CFUF> { static constexpr bool flags = true; };

typedef enum {
	ENT_HASHTAG = 1,
	ENT_URL,
	ENT_URL_IMG,
	ENT_MENTION,
	ENT_MEDIA,
} ENT_ENUMTYPE;

struct entity {
	ENT_ENUMTYPE type;
	int start = 0;
	int end = 0;
	std::string text;
	std::string fullurl;
	udc_ptr user;
	media_id_type media_id;

	entity(ENT_ENUMTYPE t) : type(t) {}
};

template<> struct enum_traits<MEF> { static constexpr bool flags = true; };

enum class MEF : unsigned int {
	HAVE_THUMB           = 1<<0,  //Have loaded thumbnail into memory
	HAVE_FULL            = 1<<1,
	FULL_FAILED          = 1<<2,
	LOAD_THUMB           = 1<<5,  //Can load thumbnail from file
	LOAD_FULL            = 1<<6,
	IN_DB                = 1<<7,
	THUMB_NET_INPROGRESS = 1<<8,
	FULL_NET_INPROGRESS  = 1<<9,
	THUMB_FAILED         = 1<<10,
	MANUALLY_PURGED      = 1<<11,

	DB_SAVE_MASK         = MANUALLY_PURGED,
};

enum class MELF {
	LOADTIME             = 1<<0,
	DISPTIME             = 1<<1,
	FORCE                = 1<<2,
	NONETLOAD            = 1<<3,
};
template<> struct enum_traits<MELF> { static constexpr bool flags = true; };

struct media_entity_raii_updater {
	observer_ptr<media_entity> me;

	media_entity_raii_updater(observer_ptr<media_entity> me_) : me(me_) { }
	~media_entity_raii_updater();
};

struct media_entity {
	media_id_type media_id; //compound type used to prevent id-clashes between media-entity images and non-media-entity images
	std::string media_url;
	std::string fulldata;	//the full unmodified content of the image data
	wxImage thumbimg;
	std::forward_list<tweet_ptr> tweet_list;
	media_display_win *win = nullptr;
	shb_iptr full_img_sha1;
	shb_iptr thumb_img_sha1;
	uint64_t lastused = 0;
	std::weak_ptr<taccount> dm_media_acc;

	flagwrapper<MEF> flags = 0;

	static wxString cached_full_filename(media_id_type media_id);
	static wxString cached_thumb_filename(media_id_type media_id);
	wxString cached_full_filename() const { return cached_full_filename(media_id); }
	wxString cached_thumb_filename() const { return cached_thumb_filename(media_id); }

	std::function<void(media_entity *, flagwrapper<MELF>)> check_load_thumb_func;

	void CheckLoadThumb(flagwrapper<MELF> melf) {
		if(check_load_thumb_func) check_load_thumb_func(this, melf);
	}

	void PurgeCache(observer_ptr<dbsendmsg_list> msglist = nullptr);
	void ClearPurgeFlag(observer_ptr<dbsendmsg_list> msglist = nullptr);

	static observer_ptr<media_entity> MakeNew(media_id_type mid, std::string url);

	void UpdateLastUsed(observer_ptr<dbsendmsg_list> msglist = nullptr);
};

struct userlookup {
	std::forward_list<udc_ptr> users_queried;
	~userlookup();
	void UnMarkAll();
	void Mark(udc_ptr udc);
	void GetIdList(std::string &idlist) const;
};

struct streamconntimeout : public wxTimer {
	time_t last_activity = 0;
	unsigned int triggercount = 0;
	observer_ptr<twitcurlext> tw;
	streamconntimeout(observer_ptr<twitcurlext> tw_) : tw(tw_) { Arm(); };
	void Notify();
	void Arm();
	~streamconntimeout() { Stop(); }

	private:
	void ArmTimer();
};

struct friendlookup {
	std::string GetTwitterURL() const;

	container::set<uint64_t> ids;
};

void ParseTwitterDate(struct tm *createtm, time_t *createtm_t, const std::string &created_at);

unsigned int TwitterCharCount(const char *in, size_t inlen, unsigned int img_uploads = 0);
inline unsigned int TwitterCharCount(const std::string &str, unsigned int img_uploads = 0) {
	return TwitterCharCount(str.c_str(), str.size(), img_uploads);
}

struct is_user_mentioned_cache {
	protected:
	is_user_mentioned_cache() { }

	public:
	virtual ~is_user_mentioned_cache() { }
	virtual void clear() = 0;
};

bool IsUserMentioned(const char *in, size_t inlen, udc_ptr_p u, std::unique_ptr<is_user_mentioned_cache> *cache = nullptr);
inline bool IsUserMentioned(const std::string &str, udc_ptr_p u, std::unique_ptr<is_user_mentioned_cache> *cache = nullptr) {
	return IsUserMentioned(str.c_str(), str.size(), u, cache);
}
bool IsTweetAReply(const char *in, size_t inlen);
inline bool IsTweetAReply(const std::string &str) {
	return IsTweetAReply(str.c_str(), str.size());
}

#ifdef __WINDOWS__
#ifndef gmtime_r
	struct tm *gmtime_r (const time_t *timer, struct tm *result);
#endif
#endif

bool CheckMarkPending_GetAcc(tweet_ptr_p t, flagwrapper<PENDING_REQ> preq = PENDING_REQ::DEFAULT, flagwrapper<PENDING_RESULT> presult = PENDING_RESULT::DEFAULT);
flagwrapper<PENDING_BITS> TryUnmarkPendingTweet(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags = 0);
void FastMarkPendingNonAcc(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark);
bool FastMarkPendingNoAccFallback(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark, const std::string &logprefix);
void GenericMarkPending(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark, const std::string &logprefix, flagwrapper<tweet::GUAF> guaflags = 0);

bool MarkPending_TPanelMap(tweet_ptr_p tobj, tpanelparentwin_nt *win_, PUSHFLAGS pushflags = PUSHFLAGS::DEFAULT, std::shared_ptr<tpanel> *pushtpanel_ = nullptr);
bool CheckFetchPendingSingleTweet(tweet_ptr_p tobj, std::shared_ptr<taccount> acc_hint, std::unique_ptr<dbseltweetmsg> *existing_dbsel = nullptr,
		flagwrapper<PENDING_REQ> preq = PENDING_REQ::DEFAULT, flagwrapper<PENDING_RESULT> presult = PENDING_RESULT::DEFAULT);
bool CheckLoadSingleTweet(tweet_ptr_p t, std::shared_ptr<taccount> &acc_hint);
void MarkTweetIDSetCIDS(const tweetidset &ids, const tpanel *exclude, tweetidset cached_id_sets::* idsetptr,
		bool remove, std::function<void(tweet_ptr_p )> existingtweetfunc = std::function<void(tweet_ptr_p)>());
void SendTweetFlagUpdate(const tweet &tw, unsigned long long mask);
void SpliceTweetIDSet(tweetidset &set, tweetidset &out, uint64_t highlim_inc, uint64_t lowlim_inc, bool clearspliced);

container::map<std::string, udc_ptr> GetDMConversationMap();

#endif
