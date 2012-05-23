#include "retcon.h"
#include <cstdio>

globconf gc;
std::list<std::shared_ptr<taccount>> alist;
socketmanager sm;
alldata ad;

IMPLEMENT_APP(retcon)

bool retcon::OnInit() {
	wxApp::OnInit();
	wxConfigBase *wfc=new wxFileConfig(wxT("retcon"),wxT(""),wxT(""),wxT(""),wxCONFIG_USE_LOCAL_FILE,wxConvUTF8);
	mainframe *frame = new mainframe( wxT("Hello World"), wxPoint(50, 50), wxSize(450, 340) );
	new wxLogWindow(frame, wxT("Logs"));
	wxConfigBase::Set(wfc);
	sm.InitMultiIOHandler();
	ReadAllCFGIn(*wfc, gc, alist);
	frame->Show(true);
	SetTopWindow(frame);
	return true;
}

int retcon::OnExit() {
	for(auto it=alist.begin() ; it != alist.end(); it++ ) {
		(*it)->twit.KillConn();
		(*it)->twit_stream.KillConn();
	}
	sm.DeInitMultiIOHandler();
	wxConfigBase *wfc=wxConfigBase::Get();
	WriteAllCFGOut(*wfc, gc, alist);
	wfc->Flush();
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

	new wxPanel(this, -1);

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

bool taccount::TwInit(wxWindow *pf) {
	bool didauth;
	CURL *ch=twit.GetCurlHandle();
	curl_easy_setopt(ch,CURLOPT_CAINFO,"./cacert.pem");
	ch=twit_stream.GetCurlHandle();
	curl_easy_setopt(ch,CURLOPT_CAINFO,"./cacert.pem");
	twit.setTwitterApiType(twitCurlTypes::eTwitCurlApiFormatJson);
	twit.setTwitterProcotolType(ssl?twitCurlTypes::eTwitCurlProtocolHttps:twitCurlTypes::eTwitCurlProtocolHttp);
	FILE *tl=fopen("retconcurllog.txt","a");
	twit.setlog(tl, 1);
	twit_stream.setlog(tl, 1);
	twit.getOAuth().setConsumerKey((const char*) cfg.tokenk.val.utf8_str());
	twit.getOAuth().setConsumerSecret((const char*) cfg.tokens.val.utf8_str());
	twit_stream.getOAuth().setConsumerKey((const char*) cfg.tokenk.val.utf8_str());
	twit_stream.getOAuth().setConsumerSecret((const char*) cfg.tokens.val.utf8_str());
	if(conk.size() && cons.size()) {
		twit.getOAuth().setOAuthTokenKey((const char*) conk.utf8_str());
		twit.getOAuth().setOAuthTokenSecret((const char*) cons.utf8_str());
		twit_stream.getOAuth().setOAuthTokenKey((const char*) conk.utf8_str());
		twit_stream.getOAuth().setOAuthTokenSecret((const char*) cons.utf8_str());
		didauth=false;

		//temporary
		//return PostAccVerifyInit();
	}
	else {
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
		twit_stream.getOAuth().setConsumerKey(stdconk);
		twit_stream.getOAuth().setConsumerSecret(stdcons);
		conk=wxString::FromUTF8(stdconk.c_str());
		cons=wxString::FromUTF8(stdcons.c_str());
		didauth=true;
	}

	if(didauth) {
		twit.accountVerifyCredGet();

		long httpcode;
		curl_easy_getinfo(twit.GetCurlHandle(), CURLINFO_RESPONSE_CODE, &httpcode);

		if(httpcode!=200) {
			//failed
			enabled=0;
			return false;
		}
		else {
			userdataparse parse;
			twit.getLastWebResponse(parse.json);
			//wxString tmp=wxString::FromUTF8(userobj->json.c_str());
			//wxLogWarning(wxT("%s"), tmp.c_str());
			parse.ParseJson(shared_from_this());
			std::shared_ptr<userdata> userobj=parse.pop_front();
			ad.UpdateUserContainer(ad.GetUserContainerById(userobj->id), userobj);
			dispname=wxstrstd(userobj->name);
			//userobj->Dump();
			PostAccVerifyInit();
		}
	}
	else {
		twit.connmode=twitcurlext::CS_ACCVERIFY;
		twit.tacc=shared_from_this();
		twit.SetNoPerformFlag(true);
		twit.accountVerifyCredGet();
		sm.AddConn(twit);
		wxLogWarning(wxT("Queue AccVerify"));
	}


	return true;
}

bool taccount::PostAccVerifyInit() {

	//streams test
	twit_stream.connmode=twitcurlext::CS_STREAM;
	twit_stream.tacc=shared_from_this();
	twit_stream.SetNoPerformFlag(true);
	twit_stream.SetStreamApiCallback((twitCurlTypes::fpStreamApiCallback) StreamCallback, 0);
	twit_stream.UserStreamingApi();
	sm.AddConn(twit_stream);

	return true;

	twit.connmode=twitcurlext::CS_TIMELINE;
	twit.tacc=shared_from_this();
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
	//twit.timelineUserGet(tmps, "Sethvir", false);
	sm.AddConn(twit);
	wxLogWarning(wxT("Queue Initial Tweet Read"));
	return true;
}

void StreamCallback( std::string &data, twitcurlext* pTwitCurlObj, void *userdata ) {
	wxLogWarning(wxT("Received: %s"), wxstrstd(data).c_str());
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
}
