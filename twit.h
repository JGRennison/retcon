void HandleNewTweet(const std::shared_ptr<tweet> &t);
void UpdateTweet(const std::shared_ptr<tweet> &t);

struct userdata {
	std::string name;
	std::string screen_name;
	std::string profile_img_url;
	bool isprotected;
	std::weak_ptr<taccount> acc;
	std::string created_at;		//fill this only once
	time_t createtime_t;
	std::string description;

	std::string json;
};

enum {
	UDC_LOOKUP_IN_PROGRESS		= 1<<0,
	UDC_IMAGE_DL_IN_PROGRESS	= 1<<1,
	UDC_THIS_IS_ACC_USER_HINT	= 1<<2
};

struct userdatacontainer : std::enable_shared_from_this<userdatacontainer> {
	uint64_t id;
	userdata user;
	long lastupdate;
	unsigned int udc_flags;

	std::string json;

	std::string cached_profile_img_url;
	std::shared_ptr<wxBitmap> cached_profile_img;
	//std::shared_ptr<wxImage> cached_profile_img;

	std::forward_list<std::shared_ptr<tweet> > pendingtweets;

	bool NeedsUpdating();
	bool IsReady();
	void CheckPendingTweets();
	std::shared_ptr<taccount> GetAccountOfUser();
	void GetImageLocalFilename(wxString &filename);
	inline userdata &GetUser() { return user; }
	void MarkUpdated();
	void Dump();
};

struct tweet_flags {
	tweet_flags() : bits() { }
	tweet_flags(unsigned long long val) : bits(val) { }
	static constexpr ssize_t GetFlagNum(char in) { return (in>='0' && in<='9')?in-'0':((in>='a' && in<='z')?10+in-'a':((in>='A' && in<='Z')?10+26+in-'A':-1)); }
	static constexpr char GetFlagChar(size_t in) { return (in<10)?in+'0':((in>=10 && in<36)?in+'a'-10:((in>=36 && in<62)?in+'A'-36:'?')); }
	bool Get(char in) {
		ssize_t num=GetFlagNum(in);
		if(num>=0) return bits.test(num);
		else return 0;
	}
	void Set(char in, bool value=true) {
		ssize_t num=GetFlagNum(in);
		if(num>=0) bits.set(num, value);
	}
	std::string GetString();
	protected:
	std::bitset<62> bits;


};

struct tweet_perspective {
	tweet_perspective(std::shared_ptr<taccount> &tac) : acc(tac) { }
	std::shared_ptr<taccount> acc;

	enum {
		TP_IAH	= 1<<0,
		TP_FAV	= 1<<1,
		TP_RET	= 1<<2,
	};

	bool IsArrivedHere() { return flags&TP_IAH; }
	bool IsFavourited() { return flags&TP_FAV; }
	bool IsRetweeted() { return flags&TP_RET; }
	void SetArrivedHere(bool val) { if(val) flags|=TP_IAH; else flags&=~TP_IAH; }
	void SetFavourited(bool val) { if(val) flags|=TP_FAV; else flags&=~TP_FAV; }
	void SetRetweeted(bool val) { if(val) flags|=TP_RET; else flags&=~TP_RET; }

	protected:
	unsigned int flags;

	//bool arrived_here;
	//bool favourited;
	//bool retweeted;
};

struct tweet {
	uint64_t id;
	uint64_t in_reply_to_status_id;
	unsigned int retweet_count;
	std::string source;
	std::string text;
	std::string created_at;
	time_t createtime_t;
	std::shared_ptr<userdatacontainer> user;		//for DMs this is the sender
	std::shared_ptr<userdatacontainer> user_recipient;	//for DMs this is the recipient, for tweets, unset
	std::forward_list<entity> entlist;
	std::forward_list<tweet_perspective> tp_list;

	std::string json;

	tweet_flags flags;

	void Dump();
	tweet_perspective *AddTPToTweet(std::shared_ptr<taccount> &tac, bool *isnew=0);
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
	uint64_t media_id;

	entity(ENT_ENUMTYPE t) : type(t) {}
};

enum {
	ME_HAVE_THUMB	= 1<<0,
	ME_HAVE_FULL	= 1<<1,
};
	
struct media_entity {
	uint64_t media_id;
	std::string media_url;
	wxSize fullsize;
	wxImage thumbimg;
	wxImage fullimg;
	std::forward_list<std::shared_ptr<tweet> > tweet_list;
	media_display_win *win;
	unsigned int flags;

	media_entity() : win(0) { }
};

typedef enum {
	CS_ACCVERIFY=1,
	CS_TIMELINE,
	CS_STREAM,
	CS_USERLIST,
	CS_DMTIMELINE
} CS_ENUMTYPE;

//for post_action_flags
enum {
	PAF_RESOLVE_PENDINGS		= 1<<0,
	PAF_STREAM_CONN_READ_BACKFILL	= 1<<1,
};

struct restbackfillstate {
	uint64_t start_tweet_id;
	uint64_t end_tweet_id;
	unsigned int max_tweets_left;
	bool read_again;
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

struct twitcurlext: public twitCurl, public mcurlconn {
	std::weak_ptr<taccount> tacc;
	CS_ENUMTYPE connmode;
	bool inited;
	unsigned int post_action_flags;
	std::shared_ptr<streamconntimeout> scto;

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

	std::shared_ptr<restbackfillstate> rbfs;
	void ExecRestGetTweetBackfill();

	std::shared_ptr<userlookup> ul;

	DECLARE_EVENT_TABLE()
};

void StreamCallback(std::string &data, twitCurl* pTwitCurlObj, void *userdata);
void StreamActivityCallback(twitCurl* pTwitCurlObj, void *userdata);

void ParseTwitterDate(struct tm *createtm, time_t *createtm_t, const std::string &created_at);
#ifdef __WINDOWS__
	struct tm *gmtime_r (const time_t *timer, struct tm *result);
#endif
