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

#ifndef HGUARD_SRC_TWIT
#define HGUARD_SRC_TWIT

#include "univdefs.h"
#include "magic_ptr.h"
#include "twit-common.h"
#include <memory>
#include <functional>
#include <bitset>
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

void HandleNewTweet(const std::shared_ptr<tweet> &t);
void UpdateTweet(const tweet &t, bool redrawimg=false);
void UpdateAllTweets(bool redrawimg=false, bool resethighlight=false);
void UpdateUsersTweet(uint64_t userid, bool redrawimg=false);
bool CheckMarkPending_GetAcc(const std::shared_ptr<tweet> &t, bool checkfirst=false);
unsigned int CheckTweetPendings(const std::shared_ptr<tweet> &t);
bool MarkPending_TPanelMap(const std::shared_ptr<tweet> &tobj, tpanelparentwin_nt* win_, unsigned int pushflags=0, std::shared_ptr<tpanel> *pushtpanel_=0);
bool CheckFetchPendingSingleTweet(const std::shared_ptr<tweet> &tobj, std::shared_ptr<taccount> acc_hint);
void MarkTweetIDSetAsRead(const tweetidset &ids, const tpanel *exclude);
void MarkTweetIDSetCIDS(const tweetidset &ids, const tpanel *exclude, std::function<tweetidset &(cached_id_sets &)> idsetselector, bool remove, std::function<void(const std::shared_ptr<tweet> &)> existingtweetfunc = std::function<void(const std::shared_ptr<tweet> &)>());
void SendTweetFlagUpdate(const std::shared_ptr<tweet> &tw, unsigned long long mask);
void UpdateSingleTweetUnreadState(const std::shared_ptr<tweet> &tw);
void UpdateSingleTweetHighlightState(const std::shared_ptr<tweet> &tw);

enum {	//for UnmarkPendingTweet: umpt_flags
	UMPTF_TPDB_NOUPDF	= 1<<0,
	UMPTF_RMV_LKPINPRGFLG	= 1<<1,
};

void UnmarkPendingTweet(const std::shared_ptr<tweet> &t, unsigned int umpt_flags=0);

enum {	//for u_flags
	UF_ISPROTECTED	= 1<<0,
	UF_ISVERIFIED	= 1<<1,
	UF_ISDEAD	= 1<<2,
};

struct userdata {
	std::string name;
	std::string screen_name;
	std::string profile_img_url;
	unsigned int u_flags;
	time_t createtime;
	std::string description;
	std::string location;
	unsigned int statuses_count;
	unsigned int followers_count;	//users following this account
	unsigned int friends_count;	//users this account is following
	unsigned int favourites_count;	//tweets this account has faved
	std::string userurl;

	userdata() : u_flags(0), createtime(0), statuses_count(0), followers_count(0), friends_count(0), favourites_count(0) { }
};

enum {
	UDC_LOOKUP_IN_PROGRESS		= 1<<0,
	UDC_IMAGE_DL_IN_PROGRESS	= 1<<1,
	UDC_THIS_IS_ACC_USER_HINT	= 1<<2,
	UDC_PROFILE_BITMAP_SET		= 1<<3,
	UDC_HALF_PROFILE_BITMAP_SET	= 1<<4,
	UDC_WINDOWOPEN			= 1<<5,
	UDC_FORCE_REFRESH		= 1<<6,
	UDC_FRIENDACT_IN_PROGRESS	= 1<<7,
	UDC_CHECK_USERLISTWIN		= 1<<8,
	UDC_PROFILE_IMAGE_DL_FAILED	= 1<<9,
};

struct userdatacontainer : std::enable_shared_from_this<userdatacontainer> {
	uint64_t id;
	userdata user;
	uint64_t lastupdate;
	uint64_t lastupdate_wrotetodb;
	unsigned int udc_flags;

	std::string cached_profile_img_url;
	unsigned char cached_profile_img_sha1[20];
	wxBitmap cached_profile_img;
	wxBitmap cached_profile_img_half;
	std::forward_list<std::shared_ptr<tweet> > pendingtweets;
	std::deque<uint64_t> mention_index;

	bool NeedsUpdating(unsigned int updcf_flags) const;
	bool IsReady(unsigned int updcf_flags);
	void CheckPendingTweets(unsigned int umpt_flags=0);
	void MarkTweetPending(const std::shared_ptr<tweet> &t, bool checkfirst=false);
	std::shared_ptr<taccount> GetAccountOfUser() const;
	void GetImageLocalFilename(wxString &filename)  const;
	inline userdata &GetUser() { return user; }
	inline const userdata &GetUser() const { return user; }
	void MarkUpdated();
	std::string mkjson() const;
	wxBitmap MkProfileBitmapFromwxImage(const wxImage &img, double limitscalefactor);
	void SetProfileBitmapFromwxImage(const wxImage &img);
	void Dump() const;
	bool ImgIsReady(unsigned int updcf_flags);
	bool ImgHalfIsReady(unsigned int updcf_flags);
	bool GetUsableAccount(std::shared_ptr<taccount> &tac, bool enabledonly=true) const;
	std::string GetPermalink(bool ssl) const;
};

struct tweet_flags {
	protected:
	std::bitset<62> bits;

	public:
	tweet_flags() : bits() { }
	tweet_flags(unsigned long long val) : bits(val) { }
	tweet_flags(const tweet_flags &cpysrc) : bits(cpysrc.Save()) { }

	//Note that the below functions do minimal, if any, error checking

	static constexpr unsigned long long GetFlagValue(char in) { return ((uint64_t) 1)<<GetFlagNum(in); }
	static constexpr ssize_t GetFlagNum(char in) { return (in>='0' && in<='9')?in-'0':((in>='a' && in<='z')?10+in-'a':((in>='A' && in<='Z')?10+26+in-'A':-1)); }
	static constexpr char GetFlagChar(size_t in) { return (in<10)?in+'0':((in>=10 && in<36)?in+'a'-10:((in>=36 && in<62)?in+'A'-36:'?')); }

	static unsigned long long GetFlagStringValue(const std::string &in) {
		unsigned long long out = 0;
		for(auto &it : in) {
			out |= GetFlagValue(it);
		}
		return out;
	}
	static std::string GetValueString(unsigned long long val);

	bool Get(char in) const {
		ssize_t num=GetFlagNum(in);
		if(num>=0) return bits.test(num);
		else return 0;
	}

	void Set(char in, bool value=true) {
		ssize_t num=GetFlagNum(in);
		if(num>=0) bits.set(num, value);
	}

	bool Toggle(char in) {
		ssize_t num=GetFlagNum(in);
		if(num>=0) {
			bits.flip(num);
			return bits.test(num);
		}
		else return 0;
	}

	unsigned long long Save() const { return bits.to_ullong(); }
	std::string GetString() const {
		return GetValueString(Save());
	}
};

struct tweet_perspective {
	std::shared_ptr<taccount> acc;
	enum {
		TP_IAH	= 1<<0,
		TP_FAV	= 1<<1,
		TP_RET	= 1<<2,
		TP_RECV	= 1<<3,
	};

	tweet_perspective(const std::shared_ptr<taccount> &tac) : acc(tac), flags(0) { }
	tweet_perspective() : flags(0) { }
	void Reset(const std::shared_ptr<taccount> &tac) { acc = tac; }
	void Load(unsigned int fl) { flags=fl; }
	unsigned int Save() const { return flags; }

	bool IsArrivedHere() const { return flags&TP_IAH; }
	bool IsFavourited() const { return flags&TP_FAV; }
	bool IsRetweeted() const { return flags&TP_RET; }
	bool IsReceivedHere() const { return flags&TP_RECV; }
	void SetArrivedHere(bool val) { if(val) flags|=TP_IAH; else flags&=~TP_IAH; }
	void SetFavourited(bool val) { if(val) flags|=TP_FAV; else flags&=~TP_FAV; }
	void SetRetweeted(bool val) { if(val) flags|=TP_RET; else flags&=~TP_RET; }
	void SetReceivedHere(bool val) { if(val) flags|=TP_RECV; else flags&=~TP_RECV; }

	protected:
	unsigned int flags;

	//bool arrived_here;
	//bool favourited;
	//bool retweeted;
};

enum {	//for tweet.lflags
	TLF_DYNDIRTY		= 1<<0,
	TLF_BEINGLOADEDFROMDB	= 1<<1,
	TLF_PENDINGHANDLENEW	= 1<<2,
	TLF_SAVED_IN_DB		= 1<<3,
	TLF_BEINGLOADEDOVERNET	= 1<<4,
	TLF_HAVEFIRSTTP		= 1<<5,
	TLF_SHOULDSAVEINDB	= 1<<6,
};

enum {	//for tweet.updcf_flags
	UPDCF_DOWNLOADIMG	= 1<<0,
	UPDCF_USEREXPIRE	= 1<<1,
	UPDCF_DEFAULT = UPDCF_DOWNLOADIMG,
};

struct pending_op {
	virtual ~pending_op() { }

	virtual void MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags)=0;
	virtual wxString dump()=0;
};

struct rt_pending_op : public pending_op {
	std::shared_ptr<tweet> target_retweet;
	rt_pending_op(const std::shared_ptr<tweet> &t) : target_retweet(t) { }

	virtual void MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags);
	virtual wxString dump();
};

struct tpanelload_pending_op : public pending_op {
	magic_ptr_ts<tpanelparentwin_nt> win;
	std::weak_ptr<tpanel> pushtpanel;
	unsigned int pushflags;

	tpanelload_pending_op(tpanelparentwin_nt* win_, unsigned int pushflags_=0, std::shared_ptr<tpanel> *pushtpanel_=0);

	virtual void MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags);
	virtual wxString dump();
};

struct tpanel_subtweet_pending_op : public pending_op {
	wxSizer *vbox;
	magic_ptr_ts<tpanelparentwin_nt> win;
	magic_ptr_ts<tweetdispscr> parent_td;

	tpanel_subtweet_pending_op(wxSizer *v, tpanelparentwin_nt *s, tweetdispscr *t);

	virtual void MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags);
	virtual wxString dump();
};

enum {
	GUAF_CHECKEXISTING	= 1<<0,
	GUAF_NOERR			= 1<<1,
	GUAF_USERENABLED	= 1<<2,
};

struct tweet {
	uint64_t id;
	uint64_t in_reply_to_status_id;
	unsigned int retweet_count;
	std::string source;
	std::string text;
	time_t createtime;
	std::shared_ptr<userdatacontainer> user;		//for DMs this is the sender
	std::shared_ptr<userdatacontainer> user_recipient;	//for DMs this is the recipient, for tweets, unset
	std::vector<entity> entlist;
	tweet_perspective first_tp;
	std::vector<tweet_perspective> tp_extra_list;
	std::shared_ptr<tweet> rtsrc;				//for retweets, this is the source tweet
	unsigned int updcf_flags;
	std::forward_list<std::unique_ptr<pending_op> > pending_ops;

	tweet_flags flags;
	unsigned int lflags;

	tweet() : updcf_flags(UPDCF_DEFAULT), lflags(0) { };
	void Dump() const;
	tweet_perspective *AddTPToTweet(const std::shared_ptr<taccount> &tac, bool *isnew=0);
	tweet_perspective *GetTweetTP(const std::shared_ptr<taccount> &tac);
	std::string mkdynjson() const;
	bool GetUsableAccount(std::shared_ptr<taccount> &tac, unsigned int guaflags=0) const;
	bool IsReady();
	bool IsFavouritable() const;
	bool IsRetweetable() const;
	bool IsArrivedHereAnyPerspective() const;
	std::string GetPermalink() const;
	void UpdateMarkedAsRead(const tpanel *exclude=0);
	inline void IterateTP(std::function<void(const tweet_perspective &)> f) const {
		if(lflags & TLF_HAVEFIRSTTP) f(first_tp);
		for(auto &it : tp_extra_list) f(it);
	}
	inline void IterateTP(std::function<void(tweet_perspective &)> f) {
		if(lflags & TLF_HAVEFIRSTTP) f(first_tp);
		for(auto &it : tp_extra_list) f(it);
	}
};

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
	std::shared_ptr<userdatacontainer> user;
	media_id_type media_id;

	entity(ENT_ENUMTYPE t) : type(t) {}
};

enum {
	ME_HAVE_THUMB	= 1<<0,
	ME_HAVE_FULL	= 1<<1,
	ME_FULL_FAILED	= 1<<2,
	ME_SAVED_THUMB	= 1<<3,
	ME_SAVED_FULL	= 1<<4,
	ME_LOAD_THUMB	= 1<<5,
	ME_LOAD_FULL	= 1<<6,
	ME_IN_DB	= 1<<7,
};

struct media_entity {
	media_id_type media_id; //compound type used to prevent id-clashes between media-entity images and non-media-entity images
	std::string media_url;
	std::string fulldata;	//the full unmodified content of the image data
	wxImage thumbimg;
	std::forward_list<std::shared_ptr<tweet> > tweet_list;
	media_display_win *win;
	unsigned int flags;
	unsigned char full_img_sha1[20];
	unsigned char thumb_img_sha1[20];

	wxString cached_full_filename() const;
	wxString cached_thumb_filename() const;

	media_entity() : win(0), flags(0) { }
};

//for post_action_flags
enum {
	PAF_RESOLVE_PENDINGS		= 1<<0,
	PAF_STREAM_CONN_READ_BACKFILL	= 1<<1,
};

struct userlookup {
	std::forward_list<std::shared_ptr<userdatacontainer> > users_queried;
	~userlookup();
	void UnMarkAll();
	void Mark(std::shared_ptr<userdatacontainer> udc);
	void GetIdList(std::string &idlist) const;
};

struct streamconntimeout : public wxTimer {
	twitcurlext *tw;
	streamconntimeout(twitcurlext *tw_) : tw(tw_) { Arm(); };
	void Notify();
	void Arm();
	~streamconntimeout() { Stop(); }
};

enum {
	TCF_ISSTREAM		= 1<<0,
};

struct friendlookup {
	std::string GetTwitterURL() const;

	std::set<uint64_t> ids;
};

void ParseTwitterDate(struct tm *createtm, time_t *createtm_t, const std::string &created_at);
unsigned int TwitterCharCount(const char *in, size_t inlen);
inline unsigned int TwitterCharCount(const std::string &str) { return TwitterCharCount(str.c_str(), str.size()); }
bool IsUserMentioned(const char *in, size_t inlen, const std::shared_ptr<userdatacontainer> &u);
inline bool IsUserMentioned(const std::string &str, const std::shared_ptr<userdatacontainer> &u) { return IsUserMentioned(str.c_str(), str.size(), u); }
#ifdef __WINDOWS__
	struct tm *gmtime_r (const time_t *timer, struct tm *result);
#endif

void SpliceTweetIDSet(tweetidset &set, tweetidset &out, uint64_t highlim_inc, uint64_t lowlim_inc, bool clearspliced);

#endif
