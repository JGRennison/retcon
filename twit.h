void HandleNewTweet(const std::shared_ptr<tweet> &t);
void UpdateTweet(const std::shared_ptr<tweet> &t, bool redrawimg=false);
void UpdateAllTweets(bool redrawimg=false);
void UpdateUsersTweet(uint64_t userid, bool redrawimg=false);
void UnmarkPendingTweet(const std::shared_ptr<tweet> &t);
bool CheckMarkPending_GetAcc(const std::shared_ptr<tweet> &t, bool checkfirst=false);
unsigned int CheckTweetPendings(const std::shared_ptr<tweet> &t);

enum {
	UF_ISPROTECTED	= 1<<0,
	UF_ISVERIFIED	= 1<<1,
};

struct userdata {
	std::string name;
	std::string screen_name;
	std::string profile_img_url;
	unsigned int u_flags;
	time_t createtime;
	std::string description;
	unsigned int statuses_count;
	unsigned int followers_count;	//users following this account
	unsigned int friends_count;	//users this account is following
	std::string userurl;

	userdata() : u_flags(0), createtime(0), statuses_count(0), followers_count(0), friends_count(0) { }
};

enum {
	UDC_LOOKUP_IN_PROGRESS		= 1<<0,
	UDC_IMAGE_DL_IN_PROGRESS	= 1<<1,
	UDC_THIS_IS_ACC_USER_HINT	= 1<<2,
	UDC_PROFILE_BITMAP_SET		= 1<<3,
	UDC_HALF_PROFILE_BITMAP_SET	= 1<<4,
	UDC_WINDOWOPEN			= 1<<5,
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

	bool NeedsUpdating(unsigned int updcf_flags);
	bool IsReady(unsigned int updcf_flags);
	void CheckPendingTweets();
	std::shared_ptr<taccount> GetAccountOfUser();
	void GetImageLocalFilename(wxString &filename);
	inline userdata &GetUser() { return user; }
	void MarkUpdated();
	std::string mkjson() const;
	wxBitmap MkProfileBitmapFromwxImage(const wxImage &img, double limitscalefactor);
	void SetProfileBitmapFromwxImage(const wxImage &img);
	void Dump();
	bool ImgIsReady(unsigned int updcf_flags);
	bool ImgHalfIsReady(unsigned int updcf_flags);
};

struct tweet_flags {
	tweet_flags() : bits() { }
	tweet_flags(unsigned long long val) : bits(val) { }
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
	};

	tweet_perspective(const std::shared_ptr<taccount> &tac) : acc(tac), flags(0) { }
	void Load(unsigned int fl) { flags=fl; }
	unsigned int Save() const { return flags; }

	bool IsArrivedHere() const { return flags&TP_IAH; }
	bool IsFavourited() const { return flags&TP_FAV; }
	bool IsRetweeted() const { return flags&TP_RET; }
	void SetArrivedHere(bool val) { if(val) flags|=TP_IAH; else flags&=~TP_IAH; }
	void SetFavourited(bool val) { if(val) flags|=TP_FAV; else flags&=~TP_FAV; }
	void SetRetweeted(bool val) { if(val) flags|=TP_RET; else flags&=~TP_RET; }

	protected:
	unsigned int flags;

	//bool arrived_here;
	//bool favourited;
	//bool retweeted;
};

enum {	//for tweet.lflags
	TLF_DYNDIRTY		= 1<<0,
	TLF_BEINGLOADEDFROMDB	= 1<<1,
	TLF_PENDINGINDBTPANELMAP= 1<<2,
	TLF_PENDINGHANDLENEW	= 1<<3,
	TLF_PENDINGINRTMAP	= 1<<4,
};

enum {	//for tweet.updcf_flags
	UPDCF_DOWNLOADIMG	= 1<<0,
	UPDCF_USEREXPIRE	= 1<<1,
	UPDCF_DEFAULT = UPDCF_DOWNLOADIMG,
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
	std::forward_list<entity> entlist;
	std::forward_list<tweet_perspective> tp_list;
	std::shared_ptr<tweet> rtsrc;				//for retweets, this is the source tweet
	unsigned int updcf_flags;

	tweet_flags flags;
	unsigned int lflags;

	void Dump();
	tweet_perspective *AddTPToTweet(const std::shared_ptr<taccount> &tac, bool *isnew=0);
	std::string mkdynjson() const;
	bool GetUsableAccount(std::shared_ptr<taccount> &tac);
	bool IsReady();
	tweet() : updcf_flags(UPDCF_DEFAULT), lflags(0) { };
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

	wxString cached_full_filename();
	wxString cached_thumb_filename();

	media_entity() : win(0), flags(0) { }
};

typedef enum {
	CS_ACCVERIFY=1,
	CS_TIMELINE,
	CS_STREAM,
	CS_USERLIST,
	CS_DMTIMELINE,
	CS_FRIENDLOOKUP,
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
	RBFS_MAX = RBFS_SENTDM,
} RBFS_TYPE;

struct restbackfillstate {
	uint64_t start_tweet_id;	//exclusive limit
	uint64_t end_tweet_id;		//inclusive limit
	unsigned int max_tweets_left;
	RBFS_TYPE type;
	bool read_again;
	bool started;
};

struct userlookup {
	std::forward_list<std::shared_ptr<userdatacontainer> > users_queried;
	~userlookup();
	void UnMarkAll();
	void Mark(std::shared_ptr<userdatacontainer> udc);
	void GetIdList(std::string &idlist);
};

struct streamconntimeout : public wxTimer {
	twitcurlext *tw;
	streamconntimeout(twitcurlext *tw_) : tw(tw_) { Arm(); };
	void Notify();
	void Arm();
	~streamconntimeout() { Stop(); }
};

enum {
	TCF_ISSTREAM	= 1<<0,
};

struct twitcurlext: public twitCurl, public mcurlconn {
	std::weak_ptr<taccount> tacc;
	CS_ENUMTYPE connmode;
	bool inited;
	unsigned int tc_flags;
	unsigned int post_action_flags;
	std::shared_ptr<streamconntimeout> scto;
	restbackfillstate *rbfs;
	std::shared_ptr<userlookup> ul;
	std::string genurl;

	void NotifyDoneSuccess(CURL *easy, CURLcode res);
	void TwInit(std::shared_ptr<taccount> acc);
	void TwDeInit();
	void TwStartupAccVerify();
	bool TwSyncStartupAccVerify();
	CURL *GenGetCurlHandle() { return GetCurlHandle(); }
	twitcurlext(std::shared_ptr<taccount> acc);
	twitcurlext();
	~twitcurlext();
	void Reset();
	void DoRetry();
	void HandleFailure();
	void QueueAsyncExec();
	void ExecRestGetTweetBackfill();

	DECLARE_EVENT_TABLE()
};

void StreamCallback(std::string &data, twitCurl* pTwitCurlObj, void *userdata);
void StreamActivityCallback(twitCurl* pTwitCurlObj, void *userdata);

void ParseTwitterDate(struct tm *createtm, time_t *createtm_t, const std::string &created_at);
#ifdef __WINDOWS__
	struct tm *gmtime_r (const time_t *timer, struct tm *result);
#endif

extern std::unordered_multimap<uint64_t, uint64_t> rtpendingmap;
