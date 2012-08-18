#define wxUSE_UNICODE 1
#define _UNICODE 1
#define UNICODE 1

#include "libtwitcurl/twitcurl.h"
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
struct tpanelparentwin;
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

#include "socket.h"
#include "twit.h"
#include "cfg.h"
#include "parse.h"
#include "tpanel.h"
#include "optui.h"
#include "db.h"
#include "log.h"
#include "cmdline.h"
#include "userui.h"

enum
{
    ID_Quit = wxID_EXIT,
    ID_About = wxID_ABOUT,
    ID_Settings = 1,
    ID_Accounts,
    ID_Viewlog,
};

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

struct taccount : std::enable_shared_from_this<taccount> {
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
	uint64_t max_recvdm_id;
	uint64_t max_sentdm_id;

	uint64_t &GetMaxId(RBFS_TYPE type) {
		switch(type) {
			case RBFS_TWEETS: return max_tweet_id;
			case RBFS_MENTIONS: return max_tweet_id;
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

	bool enabled;
	bool userenabled;
	bool active;
	bool verifycreddone;
	bool verifycredinprogress;
	bool beinginsertedintodb;

	void ClearUsersIFollow();
	void SetUserRelationship(uint64_t userid, unsigned int flags, const time_t &optime);

	void StartRestGetTweetBackfill(uint64_t start_tweet_id, uint64_t end_tweet_id, unsigned int max_tweets_to_read, RBFS_TYPE type=RBFS_TWEETS);
	void ExecRBFS(restbackfillstate *rbfs);
	void StartRestQueryPendings();
	void DoPostAction(twitcurlext *lasttce);
	void DoPostAction(unsigned int postflags);
	void GetRestBackfill();
	void LookupFriendships(uint64_t userid);

	void MarkPending(uint64_t userid, const std::shared_ptr<userdatacontainer> &user, const std::shared_ptr<tweet> &t, bool checkfirst=false);
	void MarkPendingOrHandle(const std::shared_ptr<tweet> &t);
	bool CheckMarkPending(const std::shared_ptr<tweet> &t, bool checkfirst=false);
	void FastMarkPending(const std::shared_ptr<tweet> &t, unsigned int mark, bool checkfirst=false);

	void CFGWriteOut(DBWriteConfig &twfc);
	void CFGReadIn(DBReadConfig &twfc);
	void CFGParamConv();
	bool TwDoOAuth(wxWindow *pf, twitcurlext &twit);
	void PostAccVerifyInit();
	void Exec();
	void CalcEnabled();
	taccount(genoptconf *incfg=0);
};

struct alldata {
	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > userconts;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetobjs;
	std::map<std::string,std::shared_ptr<tpanel> > tpanels;
	std::unordered_map<media_id_type,media_entity> media_list;
	std::unordered_map<std::string,media_id_type> img_media_map;
	unsigned int next_media_id;

	std::shared_ptr<userdatacontainer> &GetUserContainerById(uint64_t id);
	std::shared_ptr<tweet> &GetTweetById(uint64_t id, bool *isnew=0);

	alldata() : next_media_id(1) { }
};

class retcon: public wxApp
{
    virtual bool OnInit();
    virtual int OnExit();
    int FilterEvent(wxEvent& event);
};

struct tweetpostwin : public wxPanel {

};

class mainframe: public wxFrame
{
public:
	tpanelnotebook *auib;
	tweetpostwin *tpw;
	tpanelmenudata tpm;
	wxMenu *tpmenu;

	mainframe(const wxString& title, const wxPoint& pos, const wxSize& size);
	~mainframe();
	void OnQuit(wxCommandEvent &event);
	void OnAbout(wxCommandEvent &event);
	void OnSettings(wxCommandEvent &event);
	void OnAccounts(wxCommandEvent &event);
	void OnViewlog(wxCommandEvent &event);
	void OnClose(wxCloseEvent &event);
	void OnMouseWheel(wxMouseEvent &event);
	void OnMenuOpen(wxMenuEvent &event);
	void OnTPanelMenuCmd(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

inline wxString wxstrstd(const std::string &st) {
	return wxString::FromUTF8(st.c_str());
}
inline wxString wxstrstd(const char *ch) {
	return wxString::FromUTF8(ch);
}
inline wxString wxstrstd(const char *ch, size_t len) {
	return wxString::FromUTF8(ch, len);
}
std::string hexify(const std::string &in);
wxString hexify_wx(const std::string &in);

mainframe *GetMainframeAncestor(wxWindow *in, bool passtoplevels=false);
void FreezeAll();
void ThawAll();
bool LoadImageFromFileAndCheckHash(const wxString &filename, const unsigned char *hash, wxImage &img);
bool LoadFromFileAndCheckHash(const wxString &filename, const unsigned char *hash, char *&data, size_t &size);

extern std::list<std::shared_ptr<taccount>> alist;
extern socketmanager sm;
extern dbconn dbc;
extern alldata ad;
extern std::forward_list<mainframe*> mainframelist;
extern std::forward_list<tpanelparentwin*> tpanelparentwinlist;

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
