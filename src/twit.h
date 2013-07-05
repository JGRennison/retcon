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

void HandleNewTweet(const std::shared_ptr<tweet> &t);
void UpdateTweet(const tweet &t, bool redrawimg=false);
void UpdateAllTweets(bool redrawimg=false);
void UpdateUsersTweet(uint64_t userid, bool redrawimg=false);
bool CheckMarkPending_GetAcc(const std::shared_ptr<tweet> &t, bool checkfirst=false);
unsigned int CheckTweetPendings(const std::shared_ptr<tweet> &t);
bool MarkPending_TPanelMap(const std::shared_ptr<tweet> &tobj, tpanelparentwin_nt* win_, unsigned int pushflags=0, std::shared_ptr<tpanel> *pushtpanel_=0);
bool CheckFetchPendingSingleTweet(const std::shared_ptr<tweet> &tobj, std::shared_ptr<taccount> acc_hint);
void MarkTweetIdAsRead(uint64_t id, const tpanel *exclude);
void MarkTweetIDSetAsRead(const tweetidset &ids, const tpanel *exclude);

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
	tweet_flags() : bits() { }
	tweet_flags(unsigned long long val) : bits(val) { }
	tweet_flags(const tweet_flags &cpysrc) : bits(cpysrc.Save()) { }

	static constexpr unsigned long long GetFlagValue(char in) { return ((uint64_t) 1)<<GetFlagNum(in); }
	static constexpr ssize_t GetFlagNum(char in) { return (in>='0' && in<='9')?in-'0':((in>='a' && in<='z')?10+in-'a':((in>='A' && in<='Z')?10+26+in-'A':-1)); }
	static constexpr char GetFlagChar(size_t in) { return (in<10)?in+'0':((in>=10 && in<36)?in+'a'-10:((in>=36 && in<62)?in+'A'-36:'?')); }
	bool Get(char in) const {
		ssize_t num=GetFlagNum(in);
		if(num>=0) return bits.test(num);
		else return 0;
	}
	void Set(char in, bool value=true) {
		ssize_t num=GetFlagNum(in);
		if(num>=0) bits.set(num, value);
	}
	std::string GetString() const;
	unsigned long long Save() const { return bits.to_ullong(); }
	protected:
	std::bitset<62> bits;


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

	tpanelload_pending_op(tpanelparentwin_nt* win_, unsigned int pushflags_=0, std::shared_ptr<tpanel> *pushtpanel_=0) : win(win_), pushflags(pushflags_) {
		if(pushtpanel_) pushtpanel=*pushtpanel_;
	}

	virtual void MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags);
	virtual wxString dump();
};

struct tpanel_subtweet_pending_op : public pending_op {
	wxSizer *vbox;
	magic_ptr_ts<tpanelparentwin_nt> win;
	magic_ptr_ts<tweetdispscr> parent_td;

	tpanel_subtweet_pending_op(wxSizer *v, tpanelparentwin_nt *s, tweetdispscr *t) : vbox(v), win(s), parent_td(t) { }

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

typedef enum {
	CS_NULL=0,
	CS_ACCVERIFY=1,
	CS_TIMELINE,
	CS_STREAM,
	CS_USERLIST,
	CS_DMTIMELINE,
	CS_FRIENDLOOKUP,
	CS_USERLOOKUPWIN,
	CS_FRIENDACTION_FOLLOW,
	CS_FRIENDACTION_UNFOLLOW,
	CS_POSTTWEET,
	CS_SENDDM,
	CS_FAV,
	CS_UNFAV,
	CS_RT,
	CS_DELETETWEET,
	CS_DELETEDM,
	CS_USERTIMELINE,
	CS_USERFAVS,
	CS_USERFOLLOWING,
	CS_USERFOLLOWERS,
	CS_SINGLETWEET,
} CS_ENUMTYPE;

//for post_action_flags
enum {
	PAF_RESOLVE_PENDINGS		= 1<<0,
	PAF_STREAM_CONN_READ_BACKFILL	= 1<<1,
};

typedef enum {			//do not change these values, they are saved/loaded to/from the DB
	RBFS_MIN = 1,
	RBFS_TWEETS = 1,
	RBFS_MENTIONS,
	RBFS_RECVDM,
	RBFS_SENTDM,
	RBFS_USER_TIMELINE,
	RBFS_USER_FAVS,
	RBFS_MAX = RBFS_USER_FAVS,
} RBFS_TYPE;

struct restbackfillstate {
	uint64_t start_tweet_id;	//exclusive limit
	uint64_t end_tweet_id;		//inclusive limit
	uint64_t userid;
	unsigned int max_tweets_left;
	unsigned int lastop_recvcount;
	RBFS_TYPE type;
	bool read_again;
	bool started;
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

struct twitcurlext: public twitCurl, public mcurlconn {
	std::weak_ptr<taccount> tacc;
	CS_ENUMTYPE connmode;
	bool inited;
	unsigned int tc_flags;
	unsigned int post_action_flags;
	std::shared_ptr<streamconntimeout> scto;
	restbackfillstate *rbfs;
	std::unique_ptr<userlookup> ul;
	std::string genurl;
	std::string extra1;
	uint64_t extra_id;
	mainframe *ownermainframe;
	magic_ptr mp;
	std::unique_ptr<friendlookup> fl;

	void NotifyDoneSuccess(CURL *easy, CURLcode res);
	void TwInit(std::shared_ptr<taccount> acc);
	void TwDeInit();
	void TwStartupAccVerify();
	bool TwSyncStartupAccVerify();
	CURL *GenGetCurlHandle() { return GetCurlHandle(); }
	twitcurlext(std::shared_ptr<taccount> acc);
	twitcurlext();
	virtual ~twitcurlext();
	void Reset();
	void DoRetry();
	void HandleFailure(long httpcode, CURLcode res);
	void QueueAsyncExec();
	void ExecRestGetTweetBackfill();
	virtual wxString GetConnTypeName();

	DECLARE_EVENT_TABLE()
};

void StreamCallback(std::string &data, twitCurl* pTwitCurlObj, void *userdata);
void StreamActivityCallback(twitCurl* pTwitCurlObj, void *userdata);

void ParseTwitterDate(struct tm *createtm, time_t *createtm_t, const std::string &created_at);
unsigned int TwitterCharCount(const char *in, size_t inlen);
inline unsigned int TwitterCharCount(const std::string &str) { return TwitterCharCount(str.c_str(), str.size()); }
bool IsUserMentioned(const char *in, size_t inlen, const std::shared_ptr<userdatacontainer> &u);
inline bool IsUserMentioned(const std::string &str, const std::shared_ptr<userdatacontainer> &u) { return IsUserMentioned(str.c_str(), str.size(), u); }
#ifdef __WINDOWS__
	struct tm *gmtime_r (const time_t *timer, struct tm *result);
#endif
