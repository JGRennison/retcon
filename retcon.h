#define wxUSE_UNICODE 1
#define _UNICODE 1
#define UNICODE 1

#include "libtwitcurl/twitcurl.h"
#include <memory>
#include <unordered_map>
#include <forward_list>
#include <stack>
#include <unordered_set>
#include <string>
#include <list>
#include <cmath>
#include <stdlib.h>
#include <time.h>
#include <wx/window.h>
#include <wx/app.h>
#include <wx/frame.h>
#include <wx/string.h>
#include <wx/confbase.h>
#include <wx/fileconf.h>
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
struct logwindow;
struct mainframe;
struct tpanelnotebook;

#include "socket.h"
#include "twit.h"
#include "cfg.h"
#include "parse.h"
#include "tpanel.h"
#include "optui.h"

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
	unsigned int restinterval;
	uint64_t max_tweet_id;

	connpool<twitcurlext> cp;

	std::shared_ptr<userdatacontainer> usercont;

	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > usersfollowed;
	std::unordered_map<uint64_t,std::weak_ptr<userdatacontainer> > usersfollowingthis; //partial list

	void ClearUsersFollowed();
	void RemoveUserFollowed(std::shared_ptr<userdatacontainer> ptr);
	void PostRemoveUserFollowed(std::shared_ptr<userdatacontainer> ptr);
	void AddUserFollowed(std::shared_ptr<userdatacontainer> ptr);

	void RemoveUserFollowingThis(std::shared_ptr<userdatacontainer> ptr);
	void AddUserFollowingThis(std::shared_ptr<userdatacontainer> ptr);

	void StartRestGetTweetBackfill(uint64_t start_tweet_id, uint64_t end_tweet_id, unsigned int max_tweets_to_read);
	void StartRestQueryPendings();
	void DoPostAction(twitcurlext *lasttce);
	void DoPostAction(unsigned int postflags);

	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > pendingusers;

	bool enabled;
	bool active;
	bool verifycreddone;
	bool verifycredinprogress;
	void CFGWriteOut(wxConfigBase &twfc);
	void CFGReadIn(wxConfigBase &twfc);
	void CFGParamConv();
	bool TwDoOAuth(wxWindow *pf, twitcurlext &twit);
	void PostAccVerifyInit();
	void Exec();
	taccount(genoptconf *incfg=0);
};

struct alldata {
	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > userconts;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetobjs;
	std::shared_ptr<userdatacontainer> GetUserContainerById(uint64_t id);
	void UpdateUserContainer(std::shared_ptr<userdatacontainer> usercont, std::shared_ptr<userdata> userconts);

	std::map<std::string,std::shared_ptr<tpanel> > tpanels;
};

class retcon: public wxApp
{
    virtual bool OnInit();
    virtual int OnExit();
};

struct tweetpostwin : public wxPanel {

};

class mainframe: public wxFrame
{
public:
	#ifdef USEAUIM
	wxAuiManager *auim;
	#else
	tpanelnotebook *auib;
	#endif
	tweetpostwin *tpw;

	mainframe(const wxString& title, const wxPoint& pos, const wxSize& size);
	~mainframe();
	void OnQuit(wxCommandEvent &event);
	void OnAbout(wxCommandEvent &event);
	void OnSettings(wxCommandEvent &event);
	void OnAccounts(wxCommandEvent &event);
	void OnViewlog(wxCommandEvent &event);
	void OnClose(wxCloseEvent &event);

	DECLARE_EVENT_TABLE()
};

inline wxString wxstrstd(std::string &st) {
	return wxString::FromUTF8(st.c_str());
}
inline wxString wxstrstd(const char *ch) {
	return wxString::FromUTF8(ch);
}

struct logwindow : public wxLogWindow {
	logwindow(wxFrame *parent, const wxChar *title, bool show = true, bool passToOld = true);
	~logwindow();
	bool OnFrameClose(wxFrame *frame);
};

extern std::list<std::shared_ptr<taccount>> alist;
extern alldata ad;
extern std::forward_list<mainframe*> mainframelist;

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
