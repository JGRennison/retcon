#define wxUSE_UNICODE 1
#define _UNICODE 1
#define UNICODE 1

#include "libtwitcurl/twitcurl.h"
#include <memory>
#include <unordered_map>
#include <forward_list>
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
#include "rapidjson/reader.h"

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

struct userdata;
struct userdatacontainer;
struct taccount;
struct tweet;

struct twitcurlext: public twitCurl {
	enum {
		CS_ACCVERIFY=1,
		CS_TIMELINE,
		CS_STREAM,
	};
	std::weak_ptr<taccount> tacc;
	unsigned int connmode;

	void NotifyDone(CURL *easy, CURLcode res);
	void KillConn();
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

	std::forward_list<std::shared_ptr<tweet>> pendingtweets;
	std::forward_list<std::shared_ptr<userdatacontainer>> pendingusers;

	bool enabled;
	void CFGWriteOut(wxConfigBase &twfc);
	void CFGReadIn(wxConfigBase &twfc);
	void CFGParamConv();
	bool TwInit(wxWindow *parent);
	bool PostAccVerifyInit();
	taccount(genoptconf *incfg=0);
};

struct globconf {
	genoptconf cfg;
	void CFGWriteOut(wxConfigBase &twfc);
	void CFGReadIn(wxConfigBase &twfc);
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

struct jsonp {
	rapidjson::Reader rd;
	std::string json;
	enum {
		PJ_NONE,
		PJ_STRING,
		PJ_BOOL,
		PJ_UINT64,
		PJ_INT
	};
	std::shared_ptr<taccount> tac;
	int recdepth;
	bool inobj;

	int mode;
	bool isvalue;

	std::string *curstr;
	bool *curbool;
	uint64_t *curuint64;
	int *curint;

	jsonp();

	void ParseJson(std::shared_ptr<taccount> tac=0);
        void Null();
        void Bool(bool b);
        void Int(int i);
        void Uint(unsigned i);
        void Int64(int64_t i);
        void Uint64(uint64_t i);
        void Double(double d);
        void String(const char* str, rapidjson::SizeType length, bool copy);
        virtual void StartObject();
        virtual void EndObject(rapidjson::SizeType memberCount);
        virtual void StartArray();
        virtual void EndArray(rapidjson::SizeType elementCount);
	virtual void DoProcessValue(const char* str, rapidjson::SizeType length)=0;	//overload this
	virtual void Preparse() { return ; }
	virtual void Postparse() { return ; }

	void CommonPostFunc();
};

struct userdata {
	uint64_t id;
	std::string name;
	std::string screen_name;
	std::string profile_img_url;
	bool isprotected;
	std::weak_ptr<taccount> acc;

	void Dump();
};

struct userdatacontainer {
	std::shared_ptr<userdata> user;
	uint64_t id;
	long lastupdate;
};

struct userdataparse : public jsonp {
	std::forward_list<std::shared_ptr<userdata>> list;
	std::shared_ptr<userdata> current;
	std::shared_ptr<userdata> pop_front();

	void DoProcessValue(const char* str, rapidjson::SizeType length);
	void StartObject();
	void EndObject(rapidjson::SizeType memberCount);
	void StartArray();

	protected:
	bool baseisarray;
	int objdepth;
};

struct tweet {
	uint64_t id;
	uint64_t in_reply_to_status_id;
	int retweet_count;
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

struct tweetparse : public jsonp {
	std::forward_list<std::shared_ptr<tweet>> list;
	std::shared_ptr<tweet> current;
	std::shared_ptr<tweet> pop_front();

	void DoProcessValue(const char* str, rapidjson::SizeType length);
	void StartObject();
	void EndObject(rapidjson::SizeType memberCount);
	void StartArray();

	protected:
	bool baseisarray;
	int objdepth;
};

struct alldata {
	std::unordered_map<uint64_t,std::shared_ptr<userdatacontainer> > userconts;
	std::map<uint64_t,std::shared_ptr<tweet> > tweetobjs;
	std::shared_ptr<userdatacontainer> GetUserContainerById(uint64_t id);
	void UpdateUserContainer(std::shared_ptr<userdatacontainer> usercont, std::shared_ptr<userdata> userconts);
};

class sockettimeout : public wxTimer {
	public:
	void Notify();
};

struct socketmanager {
	socketmanager();
	~socketmanager();
	bool AddConn(CURL* ch, twitcurlext *cs);
	bool AddConn(twitcurlext &cs);
	void RegisterSockInterest(CURL *e, curl_socket_t s, int what);
	void NotifySockEvent(curl_socket_t sockfd, int ev_bitmask);
	void InitMultiIOHandler();
	void DeInitMultiIOHandler();
	bool MultiIOHandlerInited;

	CURLM *curlmulti;
	sockettimeout st;
	int curnumsocks;
	#ifdef __WINDOWS__
	HWND wind;
	#endif
};

class retcon: public wxApp
{
    virtual bool OnInit();
    virtual int OnExit();
};

class mainframe: public wxFrame
{
public:
	mainframe(const wxString& title, const wxPoint& pos, const wxSize& size);
	void OnQuit(wxCommandEvent &event);
	void OnAbout(wxCommandEvent &event);
	void OnSettings(wxCommandEvent &event);
	void OnAccounts(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

void ReadAllCFGIn(wxConfigBase &twfc, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);
void WriteAllCFGOut(wxConfigBase &twfc, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);

void StreamCallback(std::string &data, twitcurlext* pTwitCurlObj, void *userdata);

inline wxString wxstrstd(std::string &st) {
	return wxString::FromUTF8(st.c_str());
}

extern globconf gc;
extern std::list<std::shared_ptr<taccount>> alist;
extern socketmanager sm;
extern alldata ad;
