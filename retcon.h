#define wxUSE_UNICODE 1
#define _UNICODE 1
#define UNICODE 1

#include "libtwitcurl/twitcurl.h"
#include <memory>
#include <unordered_map>
#include <forward_list>
#include <stack>
#include <unordered_set>
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
#include <wx/datetime.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/aui/aui.h>
#include <wx/stdpaths.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/filefn.h>
#include "rapidjson/document.h"

enum
{
    ID_Quit = 1,
    ID_About,
    ID_Settings,
    ID_Accounts
};

struct genopt {
	wxString val;
	bool enable;
	void CFGWriteOutCurDir(wxConfigBase &twfc, const wxString &name);
	void CFGReadInCurDir(wxConfigBase &twfc, const wxString &name, const wxString &parent);
};

struct genoptconf {
	genopt tokenk;
	genopt tokens;
	genopt ssl;
	genopt userstreams;
	genopt restinterval;
	void CFGWriteOutCurDir(wxConfigBase &twfc);
	void CFGReadInCurDir(wxConfigBase &twfc, const genoptconf &parent);
};

struct genoptglobconf {
	genopt userexpiretimemins;
	void CFGWriteOut(wxConfigBase &twfc);
	void CFGReadIn(wxConfigBase &twfc, const genoptglobconf &parent);
};

struct globconf {
	genoptconf cfg;
	genoptglobconf gcfg;

	unsigned long userexpiretime;

	void CFGWriteOut(wxConfigBase &twfc);
	void CFGReadIn(wxConfigBase &twfc);
	void CFGParamConv();
};

struct userdata;
struct userdatacontainer;
struct taccount;
struct tweet;
struct usevents;
struct tweetdisp;
struct tweetdispscr;
struct tpanelparentwin;
struct tpanelwin;

struct mcurlconn {
	virtual void NotifyDone(CURL *easy, CURLcode res);
	virtual void KillConn();
	virtual CURL *GenGetCurlHandle();
	void setlog(FILE *fs, bool verbose);
};

struct imgdlconn : public mcurlconn {
	CURL* curlHandle;
	std::string imgurl;
	std::shared_ptr<userdatacontainer> user;
	std::string imgdata;

	void NotifyDone(CURL *easy, CURLcode res);
	static int curlCallback(char* data, size_t size, size_t nmemb, imgdlconn *obj);
	imgdlconn(std::string &imgurl_, std::shared_ptr<userdatacontainer> user_);
	void Init(std::string &imgurl_, std::shared_ptr<userdatacontainer> user_);
	~imgdlconn();
	CURL *GenGetCurlHandle() { return curlHandle; }
	void Standby();
	void Reset();
	static void ClearAllConns();
	static imgdlconn *GetConn(std::string &imgurl_, std::shared_ptr<userdatacontainer> user_);
	static std::stack<imgdlconn *> idlestack;
	static std::unordered_set<imgdlconn *> activeset;
};

typedef enum {
	CS_ACCVERIFY=1,
	CS_TIMELINE,
	CS_STREAM,
	CS_USERLIST
} CS_ENUMTYPE;

struct twitcurlext: public twitCurl, public mcurlconn {
	std::weak_ptr<taccount> tacc;
	CS_ENUMTYPE connmode;
	bool inited;

	void NotifyDone(CURL *easy, CURLcode res);
	void TwInit(std::shared_ptr<taccount> acc);
	void TwDeInit();
	void TwStartupAccVerify();
	bool TwSyncStartupAccVerify();
	CURL *GenGetCurlHandle() { return GetCurlHandle(); }
	twitcurlext(std::shared_ptr<taccount> acc);
	twitcurlext();
	~twitcurlext();
};

struct taccount : std::enable_shared_from_this<taccount> {
	twitcurlext twit;
	twitcurlext twit_stream;
	wxString name;
	wxString dispname;
	genoptconf cfg;
	wxString conk;
	wxString cons;
	bool ssl;
	bool userstreams;
	unsigned int restinterval;
	uint64_t max_tweet_id;

	std::shared_ptr<userdatacontainer> usercont;

	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > usersfollowed;
	std::unordered_map<uint64_t,std::weak_ptr<userdatacontainer> > usersfollowingthis; //partial list

	void ClearUsersFollowed();
	void RemoveUserFollowed(std::shared_ptr<userdatacontainer> ptr);
	void PostRemoveUserFollowed(std::shared_ptr<userdatacontainer> ptr);
	void AddUserFollowed(std::shared_ptr<userdatacontainer> ptr);

	void RemoveUserFollowingThis(std::shared_ptr<userdatacontainer> ptr);
	void AddUserFollowingThis(std::shared_ptr<userdatacontainer> ptr);

	void HandleNewTweet(std::shared_ptr<tweet>);

	std::unordered_map<uint64_t,std::shared_ptr<tweet> > pendingtweets;
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

struct acc_window: public wxDialog {
	acc_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name = wxT("dialogBox"));
	~acc_window();
	void AccEdit(wxCommandEvent &event);
	void AccDel(wxCommandEvent &event);
	void AccNew(wxCommandEvent &event);
	void AccClose(wxCommandEvent &event);
	void UpdateLB();
	wxListBox *lb;

	DECLARE_EVENT_TABLE()
};

struct jsonparser {
	rapidjson::Document dc;
	std::shared_ptr<taccount> tac;
	CS_ENUMTYPE type;

	std::shared_ptr<userdatacontainer> DoUserParse(const rapidjson::Value& val);
	void DoEventParse(const rapidjson::Value& val);
	std::shared_ptr<tweet> DoTweetParse(const rapidjson::Value& val);

	jsonparser(CS_ENUMTYPE t, std::shared_ptr<taccount> a) {
		type=t;
		tac=a;
	}
	bool ParseString(char *str);	//modifies str
};

struct userdata {
	uint64_t id;
	std::string name;
	std::string screen_name;
	std::string profile_img_url;
	bool isprotected;
	std::weak_ptr<taccount> acc;
	std::string created_at;		//fill this only once
	wxDateTime createtime;		//fill this only once
	std::string description;

	void Dump();
};

struct userdatacontainer {
	std::shared_ptr<userdata> user;
	uint64_t id;
	long lastupdate;

	std::string cached_profile_img_url;
	std::shared_ptr<wxImage> cached_profile_img;

	bool NeedsUpdating();
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
	wxDateTime createtime;
	std::weak_ptr<taccount> acc;
	std::shared_ptr<userdatacontainer> user;

	void Dump();
};

struct tweetdispscr {
	std::shared_ptr<tweetdisp> td;
	unsigned int currentheight;
};

struct tweetdisp {
	std::shared_ptr<tweet> t;
	std::weak_ptr<tweetdispscr> tdscr;
};

struct tpanel {
	std::string name;
	std::map<uint64_t,std::shared_ptr<tweetdisp> > tweetlist;
	std::weak_ptr<tpanelparentwin> twin;

	tpanel(std::string name_);
	void PushTweet(std::shared_ptr<tweet> t);
};

struct tpanelparentwin : public wxPanel {
	std::shared_ptr<tpanelwin> tpw;
	std::shared_ptr<tpanel> tp;

	tpanelparentwin(std::shared_ptr<tpanel> tp_);
	~tpanelparentwin();
	void PushTweet(std::shared_ptr<tweetdisp> t);
};

struct tpanelwin : public wxRichTextCtrl {
	tpanelparentwin *tppw;
	std::shared_ptr<tpanel> tp;

	tpanelwin(tpanelparentwin *tppw_);
	void PushTweet(std::shared_ptr<tweetdisp> t);
};

struct alldata {
	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > userconts;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetobjs;
	std::shared_ptr<userdatacontainer> GetUserContainerById(uint64_t id);
	void UpdateUserContainer(std::shared_ptr<userdatacontainer> usercont, std::shared_ptr<userdata> userconts);

	std::map<std::string,std::shared_ptr<tpanel> > tpanels;
	std::map<std::string,tpanelparentwin*> tpanelpwin;
};

class sockettimeout : public wxTimer {
	public:
	void Notify();
};

struct socketmanager : public wxEvtHandler {
	socketmanager();
	~socketmanager();
	bool AddConn(CURL* ch, mcurlconn *cs);
	bool AddConn(twitcurlext &cs);
	void RemoveConn(CURL* ch);
	void RegisterSockInterest(CURL *e, curl_socket_t s, int what);
	void NotifySockEvent(curl_socket_t sockfd, int ev_bitmask);
	void NotifySockEventCmd(wxCommandEvent &event);
	void InitMultiIOHandler();
	void DeInitMultiIOHandler();
	bool MultiIOHandlerInited;

	CURLM *curlmulti;
	sockettimeout st;
	int curnumsocks;
	#ifdef __WINDOWS__
	HWND wind;
	#endif
	FILE *loghandle;

	DECLARE_EVENT_TABLE()
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
	wxAuiManager *auim;
	tweetpostwin *tpw;

	mainframe(const wxString& title, const wxPoint& pos, const wxSize& size);
	void OnQuit(wxCommandEvent &event);
	void OnAbout(wxCommandEvent &event);
	void OnSettings(wxCommandEvent &event);
	void OnAccounts(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

void ReadAllCFGIn(wxConfigBase &twfc, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);
void WriteAllCFGOut(wxConfigBase &twfc, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);

void StreamCallback(std::string &data, twitCurl* pTwitCurlObj, void *userdata);

inline wxString wxstrstd(std::string &st) {
	return wxString::FromUTF8(st.c_str());
}

extern globconf gc;
extern std::list<std::shared_ptr<taccount>> alist;
extern socketmanager sm;
extern alldata ad;
extern mainframe *topframe;
