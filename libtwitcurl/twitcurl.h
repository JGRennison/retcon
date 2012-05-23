#ifndef _TWITCURL_H_
#define _TWITCURL_H_

#include <string>
#include <sstream>
#include <cstring>
#include "oauthlib.h"
#include "curl/curl.h"

class twitCurl;

namespace twitCurlTypes
{
    typedef enum _eTwitCurlApiFormatType
    {
        eTwitCurlApiFormatXml = 0,
        eTwitCurlApiFormatJson,
        eTwitCurlApiFormatMax
    } eTwitCurlApiFormatType;

    typedef enum _eTwitCurlProtocolType
    {
        eTwitCurlProtocolHttp = 0,
        eTwitCurlProtocolHttps,
        eTwitCurlProtocolMax
    } eTwitCurlProtocolType;

    typedef void ( *fpStreamApiCallback )( std::string &data, twitCurl* pTwitCurlObj, void *userdata );

};

/* Default values used in twitcurl */
namespace twitCurlDefaults
{
    /* Constants */
    const int TWITCURL_DEFAULT_BUFFSIZE = 1024;
    const std::string TWITCURL_COLON = ":";
    const char TWITCURL_EOS = '\0';
    const unsigned int MAX_TIMELINE_TWEET_COUNT = 200;

    /* Miscellaneous data used to build twitter URLs*/
    const std::string TWITCURL_STATUSSTRING = "status=";
    const std::string TWITCURL_TEXTSTRING = "text=";
    const std::string TWITCURL_QUERYSTRING = "query=";
    const std::string TWITCURL_SEARCHQUERYSTRING = "q=";
    const std::string TWITCURL_SCREENNAME = "screen_name=";
    const std::string TWITCURL_USERID = "user_id=";
    const std::string TWITCURL_EXTENSIONFORMATS[2] = { ".xml",
                                                       ".json"
                                                     };
    const std::string TWITCURL_PROTOCOLS[2] =        { "http://",
                                                       "https://"
                                                     };
    const std::string TWITCURL_TARGETSCREENNAME = "target_screen_name=";
    const std::string TWITCURL_TARGETUSERID = "target_id=";
    const std::string TWITCURL_SINCEID = "since_id=";
    const std::string TWITCURL_MAXID = "max_id=";
    const std::string TWITCURL_TRIMUSER = "trim_user=";
    const std::string TWITCURL_INCRETWEETS = "include_rts=";
    const std::string TWITCURL_COUNT = "count=";
    const std::string TWITCURL_EXCREPLIES = "exclude_replies=";
    const std::string TWITCURL_INCENTITIES = "include_entities=";
    const std::string TWITCURL_DELIMIT = "delimited=length";
    const std::string TWITCURL_STALLWARN = "stall_warnings=true";
    const std::string TWITCURL_WITH = "with=";
    const std::string TWITCURL_REPLIES = "replies=";
    const std::string TWITCURL_FOLLOW = "follow=";
    const std::string TWITCURL_TRACK = "track=";
    const std::string TWITCURL_LOCATIONS = "locations=";
    const std::string TWITCURL_REPLYSTATUSID = "in_reply_to_status_id=";

    const std::string TWITCURL_USERAGENT = "libcurl " LIBCURL_VERSION;

    /* URL separators */
    const std::string TWITCURL_URL_SEP_AMP = "&";
    const std::string TWITCURL_URL_SEP_QUES = "?";
};

/* Default twitter URLs */
namespace twitterDefaults
{

    /* Search URLs */
    const std::string TWITCURL_SEARCH_URL = "search.twitter.com/search";

    /* Status URLs */
    const std::string TWITCURL_STATUSUPDATE_URL = "api.twitter.com/1/statuses/update";
    const std::string TWITCURL_STATUSSHOW_URL = "api.twitter.com/1/statuses/show/";
    const std::string TWITCURL_STATUDESTROY_URL = "api.twitter.com/1/statuses/destroy/";

    /* Timeline URLs */
    const std::string TWITCURL_HOME_TIMELINE_URL = "api.twitter.com/1/statuses/home_timeline";
    const std::string TWITCURL_FEATURED_USERS_URL = "api.twitter.com/1/statuses/featured";
    const std::string TWITCURL_MENTIONS_URL = "api.twitter.com/1/statuses/mentions";
    const std::string TWITCURL_USERTIMELINE_URL = "api.twitter.com/1/statuses/user_timeline";

    /* Users URLs */
    const std::string TWITCURL_SHOWUSERS_URL = "api.twitter.com/1/users/show";
    const std::string TWITCURL_SHOWFRIENDS_URL = "api.twitter.com/1/statuses/friends";
    const std::string TWITCURL_SHOWFOLLOWERS_URL = "api.twitter.com/1/statuses/followers";

    /* Direct messages URLs */
    const std::string TWITCURL_DIRECTMESSAGES_URL = "api.twitter.com/1/direct_messages";
    const std::string TWITCURL_DIRECTMESSAGENEW_URL = "api.twitter.com/1/direct_messages/new";
    const std::string TWITCURL_DIRECTMESSAGESSENT_URL = "api.twitter.com/1/direct_messages/sent";
    const std::string TWITCURL_DIRECTMESSAGEDESTROY_URL = "api.twitter.com/1/direct_messages/destroy/";

    /* Friendships URLs */
    const std::string TWITCURL_FRIENDSHIPSCREATE_URL = "api.twitter.com/1/friendships/create";
    const std::string TWITCURL_FRIENDSHIPSDESTROY_URL = "api.twitter.com/1/friendships/destroy";
    const std::string TWITCURL_FRIENDSHIPSSHOW_URL = "api.twitter.com/1/friendships/show";

    /* Social graphs URLs */
    const std::string TWITCURL_FRIENDSIDS_URL = "api.twitter.com/1/friends/ids";
    const std::string TWITCURL_FOLLOWERSIDS_URL = "api.twitter.com/1/followers/ids";

    /* Account URLs */
    const std::string TWITCURL_ACCOUNTRATELIMIT_URL = "api.twitter.com/1/account/rate_limit_status";
    const std::string TWITCURL_ACCOUNTVERIFYCRED_URL = "api.twitter.com/1/account/verify_credentials";

    /* Favorites URLs */
    const std::string TWITCURL_FAVORITESGET_URL = "api.twitter.com/1/favorites";
    const std::string TWITCURL_FAVORITECREATE_URL = "api.twitter.com/1/favorites/create/";
    const std::string TWITCURL_FAVORITEDESTROY_URL = "api.twitter.com/1/favorites/destroy/";

    /* Block URLs */
    const std::string TWITCURL_BLOCKSCREATE_URL = "api.twitter.com/1/blocks/create/";
    const std::string TWITCURL_BLOCKSDESTROY_URL = "api.twitter.com/1/blocks/destroy/";

    /* Saved Search URLs */
    const std::string TWITCURL_SAVEDSEARCHGET_URL = "api.twitter.com/1/saved_searches";
    const std::string TWITCURL_SAVEDSEARCHSHOW_URL = "api.twitter.com/1/saved_searches/show/";
    const std::string TWITCURL_SAVEDSEARCHCREATE_URL = "api.twitter.com/1/saved_searches/create";
    const std::string TWITCURL_SAVEDSEARCHDESTROY_URL = "api.twitter.com/1/saved_searches/destroy/";

    /* Trends URLs */
    const std::string TWITCURL_TRENDS_URL = "api.twitter.com/1/trends";
    const std::string TWITCURL_TRENDSDAILY_URL = "api.twitter.com/1/trends/daily";
    const std::string TWITCURL_TRENDSCURRENT_URL = "api.twitter.com/1/trends/current";
    const std::string TWITCURL_TRENDSWEEKLY_URL = "api.twitter.com/1/trends/weekly";
    const std::string TWITCURL_TRENDSAVAILABLE_URL = "api.twitter.com/1/trends/available";

    /* Streaming API URLs */
    const std::string TWITCURL_USERSTREAM_URL = "https://userstream.twitter.com/2/user.json";
    const std::string TWITCURL_PUBLICFILTERSTREAM_URL = "https://stream.twitter.com/1/statuses/filter.json";
    const std::string TWITCURL_PUBLICSAMPLESTREAM_URL = "https://stream.twitter.com/1/statuses/sample.json";

};

struct timelineparams {
	unsigned int count;		//ignored if 0
	uint64_t since;			//"
	uint64_t max;			//"
	signed char trim_user;		//set to 1 for true, -1 for false
	signed char include_rts;	//"
	signed char include_entities;	//"
	signed char exclude_replies;	//"

};

/* twitCurl class */
class twitCurl
{
public:
    twitCurl();
    ~twitCurl();

    void setlog(FILE *fs, bool verbose);

    /* Twitter OAuth authorization methods */
    oAuth& getOAuth();
    bool oAuthRequestToken( std::string& authorizeUrl /* out */ );
    bool oAuthAccessToken();
    bool oAuthHandlePIN( const std::string& authorizeUrl /* in */ );

    /* Twitter login APIs, set once and forget */
    std::string& getTwitterUsername();
    std::string& getTwitterPassword();
    void setTwitterUsername( std::string& userName /* in */ );
    void setTwitterPassword( std::string& passWord /* in */ );

    /* Twitter API type */
    void setTwitterApiType( twitCurlTypes::eTwitCurlApiFormatType eType );
    void setTwitterProcotolType( twitCurlTypes::eTwitCurlProtocolType eType );

    /* Twitter search APIs */
    bool search( std::string& searchQuery /* in */ );

    /* Twitter status APIs */
    bool statusUpdate( std::string& newStatus, std::string in_reply_to_status_id = "" ); /* all parameters in */
    bool statusShowById( std::string& statusId /* in */ );
    bool statusDestroyById( std::string& statusId /* in */ );

    /* Twitter timeline APIs */
    bool timelineHomeGet( const struct timelineparams &tmps );
    bool timelineUserGet( const struct timelineparams &tmps, std::string userInfo = "" /* in */, bool isUserId = false /* in */ );
    bool featuredUsersGet();
    bool mentionsGet( std::string sinceId = "" /* in */ );

    /* Twitter user APIs */
    bool userGet( std::string& userInfo /* in */, bool isUserId = false /* in */ );
    bool friendsGet( std::string userInfo = "" /* in */, bool isUserId = false /* in */ );
    bool followersGet( std::string userInfo = "" /* in */, bool isUserId = false /* in */ );

    /* Twitter direct message APIs */
    bool directMessageGet();
    bool directMessageSend( std::string& userInfo /* in */, std::string& dMsg /* in */, bool isUserId = false /* in */ );
    bool directMessageGetSent();
    bool directMessageDestroyById( std::string& dMsgId /* in */ );

    /* Twitter friendships APIs */
    bool friendshipCreate( std::string& userInfo /* in */, bool isUserId = false /* in */ );
    bool friendshipDestroy( std::string& userInfo /* in */, bool isUserId = false /* in */ );
    bool friendshipShow( std::string& userInfo /* in */, bool isUserId = false /* in */ );

    /* Twitter social graphs APIs */
    bool friendsIdsGet( std::string& userInfo /* in */, bool isUserId = false /* in */ );
    bool followersIdsGet( std::string& userInfo /* in */, bool isUserId = false /* in */ );

    /* Twitter account APIs */
    bool accountRateLimitGet();
    bool accountVerifyCredGet();

    /* Twitter favorites APIs */
    bool favoriteGet();
    bool favoriteCreate( std::string& statusId /* in */ );
    bool favoriteDestroy( std::string& statusId /* in */ );

    /* Twitter block APIs */
    bool blockCreate( std::string& userInfo /* in */ );
    bool blockDestroy( std::string& userInfo /* in */ );

    /* Twitter search APIs */
    bool savedSearchGet();
    bool savedSearchCreate( std::string& query /* in */ );
    bool savedSearchShow( std::string& searchId /* in */ );
    bool savedSearchDestroy( std::string& searchId /* in */ );

    /* Twitter trends APIs (JSON) */
    bool trendsGet();
    bool trendsDailyGet();
    bool trendsWeeklyGet();
    bool trendsCurrentGet();
    bool trendsAvailableGet();

    /* cURL APIs */
    bool isCurlInit();
    void getLastWebResponse( std::string& outWebResp /* out */ );
    void getLastCurlError( std::string& outErrResp /* out */);

    /* Internal cURL related methods */
    int saveLastWebResponse( char*& data, size_t size );

    /* cURL proxy APIs */
    std::string& getProxyServerIp();
    std::string& getProxyServerPort();
    std::string& getProxyUserName();
    std::string& getProxyPassword();
    void setProxyServerIp( std::string& proxyServerIp /* in */ );
    void setProxyServerPort( std::string& proxyServerPort /* in */ );
    void setProxyUserName( std::string& proxyUserName /* in */ );
    void setProxyPassword( std::string& proxyPassword /* in */ );

    /* Streaming APIs */
    bool UserStreamingApi( std::string with="", std::string replies="", std::string follow="", std::string track="", std::string locations="", bool accept_encoding=true, bool stall_warnings=false ); /* all parameters in */
    bool PublicFilterStreamingApi( std::string follow="", std::string track="", std::string locations="", bool accept_encoding=true, bool stall_warnings=false ); /* all parameters in */
    bool PublicSampleStreamingApi( bool accept_encoding=true, bool stall_warnings=false );
    void SetStreamApiCallback( twitCurlTypes::fpStreamApiCallback func, void *userdata );

    /* Methods to allow the use of the Curl multi API (asynchronous IO) */
    CURL* GetCurlHandle() { return m_curlHandle; }
    void SetNoPerformFlag(bool noperform) { m_noperform = noperform; }

private:
    /* cURL data */
    CURL* m_curlHandle;
    char m_errorBuffer[twitCurlDefaults::TWITCURL_DEFAULT_BUFFSIZE];
    std::string m_callbackData;
    struct curl_slist* m_pOAuthHeaderList;	/* This is a member to prevent it going out of scope for multi-IO */

    /* cURL flags */
    bool m_curlProxyParamsSet;
    bool m_curlLoginParamsSet;
    bool m_curlCallbackParamsSet;
    bool m_noperform;		/* Instead of easy performing, do nothing (useful for curl multi IO) */

    /* cURL proxy data */
    std::string m_proxyServerIp;
    std::string m_proxyServerPort;
    std::string m_proxyUserName;
    std::string m_proxyPassword;

    /* Twitter data */
    std::string m_twitterUsername;
    std::string m_twitterPassword;

    /* Twitter API type */
    twitCurlTypes::eTwitCurlApiFormatType m_eApiFormatType;
    twitCurlTypes::eTwitCurlProtocolType m_eProtocolType;

    /* OAuth data */
    oAuth m_oAuth;

    /* Streaming API data */
    unsigned int m_curchunklength;
    twitCurlTypes::fpStreamApiCallback m_streamapicallback;
    void *m_streamapicallback_data;

    /* Private methods */
    void clearCurlCallbackBuffers();
    void prepareCurlProxy();
    void prepareCurlCallback();
    void prepareCurlUserPass();
    void prepareStandardParams();
    bool performGet( const std::string& getUrl );
    bool performGet( const std::string& getUrl, const std::string& oAuthHttpHeader );
    bool performDelete( const std::string& deleteUrl );
    bool performPost( const std::string& postUrl, std::string dataStr = "" );
    bool curl_gen_exec(CURL *easy_handle);
    void UtilTimelineProcessParams( const struct timelineparams &tmps, std::string &buildUrl );
    bool PostStreamingApiGeneric( std::string streamurl, std::string with, std::string replies, std::string follow , std::string track, std::string locations, bool accept_encoding, bool stall_warnings );
    void StreamingApiGenericPrepare( bool accept_encoding );

    /* Internal cURL related methods */
    static int curlCallback( char* data, size_t size, size_t nmemb, twitCurl* pTwitCurlObj );
    static int curlStreamingCallback( char* data, size_t size, size_t nmemb, twitCurl* pTwitCurlObj );
};


/* Private functions */
void utilMakeCurlParams( std::string& outStr, std::string& inParam1, std::string& inParam2 );
void utilMakeUrlForUser( std::string& outUrl, const std::string& baseUrl, std::string& userInfo, bool isUserId );

#endif // _TWITCURL_H_
