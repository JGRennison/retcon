void HandleNewTweet(std::shared_ptr<tweet>);

struct userdata {
	uint64_t id;
	std::string name;
	std::string screen_name;
	std::string profile_img_url;
	bool isprotected;
	std::weak_ptr<taccount> acc;
	std::string created_at;		//fill this only once
	time_t createtime_t;
	std::string description;

	void Dump();
};

enum {
	UDC_LOOKUP_IN_PROGRESS		= 1<<0,
	UDC_IMAGE_DL_IN_PROGRESS	= 1<<1,
	UDC_THIS_IS_ACC_USER_HINT	= 1<<2
};

struct userdatacontainer {
	std::shared_ptr<userdata> user;
	uint64_t id;
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
};

struct tweet {
	uint64_t id;
	uint64_t in_reply_to_status_id;
	unsigned int retweet_count;
	bool retweeted;
	std::string source;
	std::string text;
	bool favourited;
	std::string created_at;
	time_t createtime_t;
	std::weak_ptr<taccount> acc;
	std::shared_ptr<userdatacontainer> user;
	std::forward_list<std::shared_ptr<entity> > entlist;

	void Dump();
};

typedef enum {
	ENT_HASHTAG = 1,
	ENT_URL,
	ENT_MENTION,
} ENT_ENUMTYPE;

struct entity {
	ENT_ENUMTYPE type;
	int start;
	int end;
	std::string text;
	std::string fullurl;
	std::shared_ptr<userdatacontainer> user;

	entity(ENT_ENUMTYPE t) : type(t) {}
};


typedef enum {
	CS_ACCVERIFY=1,
	CS_TIMELINE,
	CS_STREAM,
	CS_USERLIST
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
