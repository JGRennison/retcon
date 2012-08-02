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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wuninitialized"
#include "rapidjson/document.h"
#pragma GCC diagnostic pop

struct userdata;
struct userdatacontainer;
struct twitcurlext;
struct taccount;
struct tweet;
struct entity;
struct usevents;
struct tweetdispscr;
struct tpanelparentwin;
struct mcurlconn;
struct socketmanager;
struct mainframe;
struct tpanelnotebook;
struct media_display_win;
struct tpanel;
struct dbsendmsg_list;
struct DBWriteConfig;
struct DBReadConfig;

typedef std::set<uint64_t, std::greater<uint64_t> > tweetidset;		//std::set, sorted in opposite order

#include "socket.h"
#include "twit.h"
#include "cfg.h"
#include "parse.h"
#include "tpanel.h"
#include "optui.h"
#include "db.h"
#include "log.h"
#include "cmdline.h"

enum
{
    ID_Quit = wxID_EXIT,
    ID_About = wxID_ABOUT,
    ID_Settings = 1,
    ID_Accounts,
    ID_Viewlog,
};

struct taccount : std::enable_shared_from_this<taccount> {
	wxString name;
	wxString dispname;
	genoptconf cfg;
	wxString conk;
	wxString cons;
	bool ssl;
	bool userstreams;
	unsigned long restinterval;	//seconds
	uint64_t max_tweet_id;
	uint64_t max_recvdm_id;
	uint64_t max_sentdm_id;

	uint64_t &GetMaxId(RBFS_TYPE type) {
		switch(type) {
			case RBFS_TWEETS: return max_tweet_id;
			case RBFS_RECVDM: return max_recvdm_id;
			case RBFS_SENTDM: return max_sentdm_id;
			default: return max_tweet_id;
		}
	}

	unsigned int dbindex;

	connpool<twitcurlext> cp;

	std::shared_ptr<userdatacontainer> usercont;

	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > usersfollowed;
	std::unordered_map<uint64_t,std::weak_ptr<userdatacontainer> > usersfollowingthis; //partial list

	//any tweet or DM in this list *must* be either in ad.tweetobjs, or in the database
	tweetidset tweet_ids;
	tweetidset dm_ids;

	void ClearUsersFollowed();
	void RemoveUserFollowed(std::shared_ptr<userdatacontainer> ptr);
	void PostRemoveUserFollowed(std::shared_ptr<userdatacontainer> ptr);
	void AddUserFollowed(std::shared_ptr<userdatacontainer> ptr);

	void RemoveUserFollowingThis(std::shared_ptr<userdatacontainer> ptr);
	void AddUserFollowingThis(std::shared_ptr<userdatacontainer> ptr);

	void StartRestGetTweetBackfill(uint64_t start_tweet_id, uint64_t end_tweet_id, unsigned int max_tweets_to_read, RBFS_TYPE type=RBFS_TWEETS);
	void StartRestQueryPendings();
	void DoPostAction(twitcurlext *lasttce);
	void DoPostAction(unsigned int postflags);
	void GetRestBackfill();

	void MarkPending(uint64_t userid, const std::shared_ptr<userdatacontainer> &user, const std::shared_ptr<tweet> &t, bool checkfirst=false);
	void MarkPendingOrHandle(const std::shared_ptr<tweet> &t);

	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > pendingusers;

	bool enabled;
	bool active;
	bool verifycreddone;
	bool verifycredinprogress;
	void CFGWriteOut(DBWriteConfig &twfc);
	void CFGReadIn(DBReadConfig &twfc);
	void CFGParamConv();
	bool TwDoOAuth(wxWindow *pf, twitcurlext &twit);
	void PostAccVerifyInit();
	void Exec();
	taccount(genoptconf *incfg=0);
};

struct alldata {
	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > userconts;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetobjs;
	std::map<std::string,std::shared_ptr<tpanel> > tpanels;
	std::unordered_map<uint64_t,media_entity> media_list;
	std::unordered_map<std::string,uint64_t> img_media_map;
	unsigned int next_media_id;

	std::shared_ptr<userdatacontainer> GetUserContainerById(uint64_t id);

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

mainframe *GetMainframeAncestor(wxWindow *in, bool passtoplevels=false);

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
