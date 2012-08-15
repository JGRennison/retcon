#include <wx/hyperlink.h>

struct user_window_timer: public wxTimer {
	void Notify();
};

struct user_window: public wxDialog {
	uint64_t userid;
	std::shared_ptr<userdatacontainer> u;
	std::weak_ptr<taccount> acc_hint;
	
	wxStaticBitmap *usericon;
	wxStaticText *name;
	wxStaticText *screen_name;
	wxStaticText *name2;
	wxStaticText *screen_name2;
	wxStaticText *desc;
	wxStaticText *isprotected;
	wxStaticText *isverified;
	wxStaticText *tweets;
	wxStaticText *followers;
	wxStaticText *follows;
	wxStaticText *createtime;
	wxHyperlinkCtrl *url;
	wxStaticText *lastupdate;
	wxStaticText *id_str;
	
	wxStaticText *ifollow;
	wxStaticText *followsme;
	wxChoice *accchoice;
	
	user_window(uint64_t userid_, const std::shared_ptr<taccount> &acc_hint_);
	~user_window();
	void RefreshFollow();
	void Refresh(bool refreshimg=false);
	void CheckAccHint();
	void fill_accchoice();
	void OnClose(wxCloseEvent &event);
	void OnSelChange(wxCommandEvent &event);
	static user_window *MkWin(uint64_t userid_, const std::shared_ptr<taccount> &acc_hint_);
	static user_window *GetWin(uint64_t userid_);
	static void CheckRefresh(uint64_t userid_, bool refreshimg=false);
	static void RefreshAllFollow();
	static void RefreshAllAcc();
	static void RefreshAll();
	static void CloseAll();
	
	static user_window_timer uwt;
	
	DECLARE_EVENT_TABLE()
};