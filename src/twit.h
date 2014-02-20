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

enum class UPDCF {
	DOWNLOADIMG        = 1<<0,
	USEREXPIRE         = 1<<1,
	DEFAULT            = DOWNLOADIMG | USEREXPIRE,
};
template<> struct enum_traits<UPDCF> { static constexpr bool flags = true; };

inline flagwrapper<UPDCF> ConstUPDCF(flagwrapper<UPDCF> updcf) {
	return updcf & UPDCF::USEREXPIRE;
}

void UnmarkPendingTweet(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags = 0);

struct userdata {
	enum class UF {
		ISPROTECTED           = 1<<0,
		ISVERIFIED            = 1<<1,
		ISDEAD                = 1<<2,
	};

	std::string name;
	std::string screen_name;
	std::string profile_img_url;
	flagwrapper<UF> u_flags;
	time_t createtime;
	std::string description;
	std::string location;
	unsigned int statuses_count;
	unsigned int followers_count;    //users following this account
	unsigned int friends_count;      //users this account is following
	unsigned int favourites_count;   //tweets this account has faved
	std::string userurl;

	userdata() : u_flags(0), createtime(0), statuses_count(0), followers_count(0), friends_count(0), favourites_count(0) { }
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
	uint64_t id;
	userdata user;
	uint64_t lastupdate;
	uint64_t lastupdate_wrotetodb;
	flagwrapper<UDC> udc_flags;

	std::string cached_profile_img_url;
	shb_iptr cached_profile_img_sha1;
	wxBitmap cached_profile_img;
	wxBitmap cached_profile_img_half;
	std::deque<tweet_ptr> pendingtweets;
	std::deque<uint64_t> mention_index;    //append only

	private:
	struct mention_set_data {
		tweetidset mention_set;
		size_t added_offset = 0;
	};
	std::unique_ptr<mention_set_data> msd;

	public:
	bool NeedsUpdating(flagwrapper<UPDCF> updcf_flags, time_t timevalue = 0) const;
	bool IsReady(flagwrapper<UPDCF> updcf_flags, time_t timevalue = 0);
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
	bool ImgIsReady(flagwrapper<UPDCF> updcf_flags);
	bool ImgHalfIsReady(flagwrapper<UPDCF> updcf_flags);
	bool GetUsableAccount(std::shared_ptr<taccount> &tac, bool enabledonly = true) const;
	std::string GetPermalink(bool ssl) const;
	void NotifyProfileImageChange();
	void MakeProfileImageFailurePlaceholder();
	const tweetidset &GetMentionSet();
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

	tweet_perspective(const std::shared_ptr<taccount> &tac) : acc(tac), flags(0) { }
	tweet_perspective() : flags(0) { }
	void Reset(const std::shared_ptr<taccount> &tac) { acc = tac; }
	void Load(unsigned int fl) { flags=fl; }
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
	unsigned int flags;
};

struct pending_op {
	virtual ~pending_op() { }

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) = 0;
	virtual wxString dump()=0;
};

struct rt_pending_op : public pending_op {
	tweet_ptr target_retweet;
	rt_pending_op(tweet_ptr_p t) : target_retweet(t) { }

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags);
	virtual wxString dump();
};

struct tpanelload_pending_op : public pending_op {
	magic_ptr_ts<tpanelparentwin_nt> win;
	std::weak_ptr<tpanel> pushtpanel;
	flagwrapper<PUSHFLAGS> pushflags;

	tpanelload_pending_op(tpanelparentwin_nt* win_, flagwrapper<PUSHFLAGS> pushflags_ = PUSHFLAGS::DEFAULT, std::shared_ptr<tpanel> *pushtpanel_ = 0);

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags);
	virtual wxString dump();
};

struct handlenew_pending_op : public pending_op {
	std::weak_ptr<taccount> tac;
	flagwrapper<ARRIVAL> arr;
	handlenew_pending_op(const std::shared_ptr<taccount> &acc, flagwrapper<ARRIVAL> arr_) : tac(acc), arr(arr_) { }

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags);
	virtual wxString dump();
};

enum class TLF {	//for tweet.lflags
	DYNDIRTY             = 1<<0,
	BEINGLOADEDFROMDB    = 1<<1,
	SAVED_IN_DB          = 1<<3,
	BEINGLOADEDOVERNET   = 1<<4,
	HAVEFIRSTTP          = 1<<5,
	SHOULDSAVEINDB       = 1<<6,
	LOADED_FROM_DB       = 1<<7,
	ISPENDING            = 1<<8,
};
template<> struct enum_traits<TLF> { static constexpr bool flags = true; };

struct tweet {
	uint64_t id = 0;
	uint64_t in_reply_to_status_id = 0;
	unsigned int retweet_count = 0;
	unsigned int favourite_count = 0;
	std::string source;
	std::string text;
	time_t createtime = 0;
	udc_ptr user;		//for DMs this is the sender
	udc_ptr user_recipient;	//for DMs this is the recipient, for tweets, unset
	std::vector<entity> entlist;
	tweet_perspective first_tp;
	std::vector<tweet_perspective> tp_extra_list;
	tweet_ptr rtsrc;				//for retweets, this is the source tweet
	flagwrapper<UPDCF> updcf_flags = UPDCF::DEFAULT;
	std::forward_list<std::unique_ptr<pending_op> > pending_ops;

	tweet_flags flags;
	flagwrapper<TLF> lflags = 0;

	private:
	tweet_flags flags_at_prev_update;

	public:
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

	bool IsReady(flagwrapper<UPDCF> updcf_flags);
	bool IsReady() { return IsReady(updcf_flags); }
	bool IsReadyConst(flagwrapper<UPDCF> updcf_flags) const { return const_cast<tweet *>(this)->IsReady(ConstUPDCF(updcf_flags)); }
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
	void IgnoreChangeToFlagsByMask(unsigned long long mask) {
		tweet_flags tmask(mask);
		flags_at_prev_update &= ~tmask;
		flags_at_prev_update |= flags & tmask;
	}
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
	int start;
	int end;
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

struct media_entity {
	media_id_type media_id; //compound type used to prevent id-clashes between media-entity images and non-media-entity images
	std::string media_url;
	std::string fulldata;	//the full unmodified content of the image data
	wxImage thumbimg;
	std::forward_list<tweet_ptr> tweet_list;
	media_display_win *win = 0;
	shb_iptr full_img_sha1;
	shb_iptr thumb_img_sha1;

	flagwrapper<MEF> flags = 0;

	static wxString cached_full_filename(media_id_type media_id);
	static wxString cached_thumb_filename(media_id_type media_id);
	wxString cached_full_filename() const { return cached_full_filename(media_id); }
	wxString cached_thumb_filename() const { return cached_thumb_filename(media_id); }

	std::function<void(media_entity *, flagwrapper<MELF>)> check_load_thumb_func;

	void CheckLoadThumb(flagwrapper<MELF> melf) {
		if(check_load_thumb_func) check_load_thumb_func(this, melf);
	}

	void PurgeCache(dbsendmsg_list *msglist = 0);
	void ClearPurgeFlag(dbsendmsg_list *msglist = 0);
};

struct userlookup {
	std::forward_list<udc_ptr> users_queried;
	~userlookup();
	void UnMarkAll();
	void Mark(udc_ptr udc);
	void GetIdList(std::string &idlist) const;
};

struct streamconntimeout : public wxTimer {
	twitcurlext *tw;
	streamconntimeout(twitcurlext *tw_) : tw(tw_) { Arm(); };
	void Notify();
	void Arm();
	~streamconntimeout() { Stop(); }
};

struct friendlookup {
	std::string GetTwitterURL() const;

	std::set<uint64_t> ids;
};

void ParseTwitterDate(struct tm *createtm, time_t *createtm_t, const std::string &created_at);

unsigned int TwitterCharCount(const char *in, size_t inlen);
inline unsigned int TwitterCharCount(const std::string &str) {
	return TwitterCharCount(str.c_str(), str.size());
}

bool IsUserMentioned(const char *in, size_t inlen, udc_ptr_p u);
inline bool IsUserMentioned(const std::string &str, udc_ptr_p u) {
	return IsUserMentioned(str.c_str(), str.size(), u);
}

#ifdef __WINDOWS__
#ifndef gmtime_r
	struct tm *gmtime_r (const time_t *timer, struct tm *result);
#endif
#endif

enum class PENDING : unsigned int;
template<> struct enum_traits<PENDING> { static constexpr bool flags = true; };

enum class PENDING : unsigned int {
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

bool CheckMarkPending_GetAcc(tweet_ptr_p t);
flagwrapper<PENDING> CheckTweetPendings(const tweet &t);
inline flagwrapper<PENDING> CheckTweetPendings(tweet_ptr_p t) {
	return CheckTweetPendings(*t);
}
void FastMarkPendingNonAcc(tweet_ptr_p t, flagwrapper<PENDING> mark);
bool FastMarkPendingNoAccFallback(tweet_ptr_p t, flagwrapper<PENDING> mark, const wxString &logprefix);
void GenericMarkPending(tweet_ptr_p t, flagwrapper<PENDING> mark, const wxString &logprefix, flagwrapper<tweet::GUAF> guaflags = 0);

bool MarkPending_TPanelMap(tweet_ptr_p tobj, tpanelparentwin_nt* win_, PUSHFLAGS pushflags = PUSHFLAGS::DEFAULT, std::shared_ptr<tpanel> *pushtpanel_ = 0);
bool CheckFetchPendingSingleTweet(tweet_ptr_p tobj, std::shared_ptr<taccount> acc_hint, dbseltweetmsg **existing_dbsel = 0);
bool CheckLoadSingleTweet(tweet_ptr_p t, std::shared_ptr<taccount> &acc_hint);
void MarkTweetIDSetAsRead(const tweetidset &ids, const tpanel *exclude);
void MarkTweetIDSetCIDS(const tweetidset &ids, const tpanel *exclude, tweetidset cached_id_sets::* idsetptr,
		bool remove, std::function<void(tweet_ptr_p )> existingtweetfunc = std::function<void(tweet_ptr_p )>());
void SendTweetFlagUpdate(const tweet &tw, unsigned long long mask);
void SpliceTweetIDSet(tweetidset &set, tweetidset &out, uint64_t highlim_inc, uint64_t lowlim_inc, bool clearspliced);

#endif
