#include "retcon.h"
#include <cstdio>

globconf gc;
std::list<std::shared_ptr<taccount>> alist;
socketmanager sm;
alldata ad;
mainframe *topframe;

IMPLEMENT_APP(retcon)

bool retcon::OnInit() {
	wxApp::OnInit();
	SetAppName(wxT("retcon"));
	::wxInitAllImageHandlers();
	if(!::wxDirExists(wxStandardPaths::Get().GetUserDataDir())) {
		::wxMkdir(wxStandardPaths::Get().GetUserDataDir(), 077);
	}
	wxConfigBase *wfc=new wxFileConfig(wxT(""),wxT(""),wxStandardPaths::Get().GetUserDataDir() + wxT("\\retcon.ini"),wxT(""),wxCONFIG_USE_LOCAL_FILE,wxConvUTF8);
	topframe = new mainframe( wxT("Retcon"), wxPoint(50, 50), wxSize(450, 340) );
	new wxLogWindow(topframe, wxT("Logs"));
	wxConfigBase::Set(wfc);
	sm.loghandle=fopen("retconcurllog.txt","a");
	sm.InitMultiIOHandler();
	ReadAllCFGIn(*wfc, gc, alist);

	ad.tpanels["[default]"]=std::make_shared<tpanel>("[default]");
	new tpanelparentwin(ad.tpanels["[default]"]);
	ad.tpanels["[default2]"]=std::make_shared<tpanel>("[default2]");
	new tpanelparentwin(ad.tpanels["[default2]"]);

	topframe->Show(true);
	SetTopWindow(topframe);
	for(auto it=alist.begin() ; it != alist.end(); it++ ) (*it)->Exec();
	return true;
}

int retcon::OnExit() {
	for(auto it=alist.begin() ; it != alist.end(); it++ ) {
		(*it)->twit.KillConn();
		(*it)->twit_stream.KillConn();
	}
	imgdlconn::ClearAllConns();
	sm.DeInitMultiIOHandler();
	wxConfigBase *wfc=wxConfigBase::Get();
	WriteAllCFGOut(*wfc, gc, alist);
	wfc->Flush();
	topframe=0;
	return wxApp::OnExit();
}

BEGIN_EVENT_TABLE(mainframe, wxFrame)
	EVT_MENU(ID_Quit,  mainframe::OnQuit)
	EVT_MENU(ID_About, mainframe::OnAbout)
	EVT_MENU(ID_Settings, mainframe::OnSettings)
	EVT_MENU(ID_Accounts, mainframe::OnAccounts)
END_EVENT_TABLE()

mainframe::mainframe(const wxString& title, const wxPoint& pos, const wxSize& size)
       : wxFrame(NULL, -1, title, pos, size)
{
	wxMenu *menuH = new wxMenu;
	menuH->Append( ID_About, wxT("&About"));
	wxMenu *menuF = new wxMenu;
	menuF->Append( ID_Quit, wxT("E&xit"));
	wxMenu *menuO = new wxMenu;
	menuO->Append( ID_Settings, wxT("&Settings"));
	menuO->Append( ID_Accounts, wxT("&Accounts"));

	wxMenuBar *menuBar = new wxMenuBar;
	menuBar->Append(menuF, wxT("&File"));
	menuBar->Append(menuO, wxT("&Options"));
	menuBar->Append(menuH, wxT("&Help"));

	auim = new wxAuiManager(this,wxAUI_MGR_DEFAULT|wxAUI_MGR_RECTANGLE_HINT);
	auim->SetDockSizeConstraint(1.0, 1.0);
	tpw=new tweetpostwin();
	auim->AddPane(tpw, wxAuiPaneInfo().Resizable().Centre().Caption(wxT("Post Tweet")).Dockable(false).Floatable(false));

	SetMenuBar( menuBar );
	return;
}
void mainframe::OnQuit(wxCommandEvent &event) {
	Close(true);
}
void mainframe::OnAbout(wxCommandEvent &event) {

}
void mainframe::OnSettings(wxCommandEvent &event) {

}
void mainframe::OnAccounts(wxCommandEvent &event) {
	acc_window *acc=new acc_window(this, -1, wxT("Accounts"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE);
	//acc->Show(true);
	acc->ShowModal();
	acc->Destroy();
	//delete acc;
}

void taccount::ClearUsersFollowed() {
	for(auto it=usersfollowed.begin() ; it != usersfollowed.end(); it++ ) {
		PostRemoveUserFollowed((*it).second);
	}
	usersfollowed.clear();
}

void taccount::RemoveUserFollowed(std::shared_ptr<userdatacontainer> ptr) {
	if(usersfollowed.erase(ptr->id)) {
		PostRemoveUserFollowed(ptr);
	}
}
void taccount::PostRemoveUserFollowed(std::shared_ptr<userdatacontainer> ptr) {
	//action goes here
}
void taccount::AddUserFollowed(std::shared_ptr<userdatacontainer> ptr) {
	if(usersfollowed.count(ptr->id)==0) {
		usersfollowed[ptr->id]=ptr;
		//action goes here
	}
}

void taccount::RemoveUserFollowingThis(std::shared_ptr<userdatacontainer> ptr) {
	if(usersfollowingthis.erase(ptr->id)) {
		//action goes here
	}
}
void taccount::AddUserFollowingThis(std::shared_ptr<userdatacontainer> ptr) {
	if(usersfollowingthis.count(ptr->id)==0) {
		usersfollowingthis[ptr->id]=ptr;
		//action goes here
	}
}

twitcurlext::twitcurlext(std::shared_ptr<taccount> acc) {
	inited=false;
	TwInit(acc);
}

twitcurlext::twitcurlext() {
	inited=false;
}
twitcurlext::~twitcurlext() {
	TwDeInit();
}

void twitcurlext::TwInit(std::shared_ptr<taccount> acc) {
	if(inited) return;
	tacc=acc;
	CURL *ch=GetCurlHandle();
	curl_easy_setopt(ch,CURLOPT_CAINFO,"./cacert.pem");
	if(sm.loghandle) setlog(sm.loghandle, 1);

	setTwitterApiType(twitCurlTypes::eTwitCurlApiFormatJson);
	setTwitterProcotolType(acc->ssl?twitCurlTypes::eTwitCurlProtocolHttps:twitCurlTypes::eTwitCurlProtocolHttp);

	getOAuth().setConsumerKey((const char*) acc->cfg.tokenk.val.utf8_str());
	getOAuth().setConsumerSecret((const char*) acc->cfg.tokens.val.utf8_str());

	if(acc->conk.size() && acc->cons.size()) {
		getOAuth().setOAuthTokenKey((const char*) acc->conk.utf8_str());
		getOAuth().setOAuthTokenSecret((const char*) acc->cons.utf8_str());
	}
	inited=true;
}

void twitcurlext::TwDeInit() {
	inited=false;
}

bool taccount::TwDoOAuth(wxWindow *pf, twitcurlext &twit) {
	std::string authUrl;
	twit.oAuthRequestToken(authUrl);
	wxString authUrlWx=wxString::FromUTF8(authUrl.c_str());
	//twit.oAuthHandlePIN(authUrl);
	wxLogWarning(wxT("%s, %s, %s"), cfg.tokenk.val.c_str(), cfg.tokens.val.c_str(), authUrlWx.c_str());
	wxLaunchDefaultBrowser(authUrlWx);
	wxTextEntryDialog *ted=new wxTextEntryDialog(pf, wxT("Enter Twitter PIN"), wxT("Enter Twitter PIN"), wxT(""), wxOK | wxCANCEL);
	int res=ted->ShowModal();
	wxString pin=ted->GetValue();
	ted->Destroy();
	if(res!=wxID_OK) return false;
	if(pin.IsEmpty()) return false;
	twit.getOAuth().setOAuthPin((const char*) pin.utf8_str());
	twit.oAuthAccessToken();
	std::string stdconk;
	std::string stdcons;
	twit.getOAuth().getOAuthTokenKey(stdconk);
	twit.getOAuth().getOAuthTokenSecret(stdcons);
	conk=wxString::FromUTF8(stdconk.c_str());
	cons=wxString::FromUTF8(stdcons.c_str());
	return true;
}


void twitcurlext::TwStartupAccVerify() {
	tacc.lock()->verifycredinprogress=true;
	connmode=CS_ACCVERIFY;
	SetNoPerformFlag(true);
	accountVerifyCredGet();
	sm.AddConn(*this);
	wxLogWarning(wxT("Queue AccVerify"));
}

bool twitcurlext::TwSyncStartupAccVerify() {
	tacc.lock()->verifycredinprogress=true;
	SetNoPerformFlag(false);
	accountVerifyCredGet();
	long httpcode;
	curl_easy_getinfo(GetCurlHandle(), CURLINFO_RESPONSE_CODE, &httpcode);
	if(httpcode==200) {
		jsonparser jp(CS_ACCVERIFY, tacc.lock());
		std::string str;
		getLastWebResponse(str);
		jp.ParseString((char*) str.c_str());	//this modifies the contents of str!!
		str.clear();
		tacc.lock()->verifycredinprogress=false;
		return true;
	}
	else {
		tacc.lock()->verifycredinprogress=false;
		return false;
	}
}

void taccount::PostAccVerifyInit() {
	verifycreddone=true;
	verifycredinprogress=false;
	Exec();
}

void taccount::Exec() {
	if(enabled && !verifycreddone) {
		twit.TwInit(shared_from_this());
		twit_stream.TwInit(shared_from_this());

		twit.TwStartupAccVerify();
	}
	else if(enabled && !active) {
		active=true;

		twit.TwInit(shared_from_this());
		twit_stream.TwInit(shared_from_this());


		//streams test
		twit_stream.connmode=CS_STREAM;
		twit_stream.SetNoPerformFlag(true);
		twit_stream.SetStreamApiCallback(&StreamCallback, 0);
		//twit_stream.UserStreamingApi("followings", "all");
		twit_stream.UserStreamingApi("followings");
		sm.AddConn(twit_stream);

		twit.connmode=CS_TIMELINE;
		twit.SetNoPerformFlag(true);
		struct timelineparams tmps={
			200,
			max_tweet_id,
			0,
			1,
			0,
			0,
			0
		};
		twit.timelineHomeGet(tmps);
		sm.AddConn(twit);
		wxLogWarning(wxT("Queue Initial Tweet Read"));
		return;
	}
	else if(!enabled && (active || verifycredinprogress)) {
		active=false;
		verifycredinprogress=false;
		twit.KillConn();
		twit.TwDeInit();
		twit_stream.KillConn();
		twit_stream.TwDeInit();
	}
}

void StreamCallback( std::string &data, twitCurl* pTwitCurlObj, void *userdata ) {
	twitcurlext *obj=(twitcurlext*) pTwitCurlObj;
	std::shared_ptr<taccount> acc=obj->tacc.lock();
	wxLogWarning(wxT("Received: %s"), wxstrstd(data).c_str());
	jsonparser jp(CS_STREAM, acc);
	jp.ParseString((char*) data.c_str());	//this modifies the contents of data!!
	data.clear();
}

std::shared_ptr<userdatacontainer> alldata::GetUserContainerById(uint64_t id) {
	std::shared_ptr<userdatacontainer> usercont=userconts[id];
	if(!usercont) {
		usercont=std::make_shared<userdatacontainer>();
		usercont->id=id;
		usercont->lastupdate=0;
	}
	return usercont;
}

void alldata::UpdateUserContainer(std::shared_ptr<userdatacontainer> usercont, std::shared_ptr<userdata> userobj) {
	usercont->user=userobj;
	usercont->lastupdate=wxGetUTCTime();
	if(userobj && userobj->profile_img_url.size() && usercont->cached_profile_img_url!=userobj->profile_img_url) {
		imgdlconn::GetConn(userobj->profile_img_url, usercont);
	}
}

bool userdatacontainer::NeedsUpdating() {
	if(!user) return true;
	else {
		if((wxGetUTCTime()-lastupdate)>gc.userexpiretime) return true;
		else return false;
	}
}

void taccount::HandleNewTweet(std::shared_ptr<tweet>) {

}
