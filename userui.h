#include <wx/hyperlink.h>
#include <wx/radiobox.h>
#include <wx/valgen.h>

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

	wxButton *followbtn;
	wxButton *refreshbtn;
	wxButton *dmbtn;
	enum FOLLOWBTNMODE {
		FBM_NONE=0, FBM_FOLLOW=1, FBM_UNFOLLOW, FBM_REMOVE_PENDING,
	};
	FOLLOWBTNMODE follow_btn_mode;

	enum {
		FOLLOWBTN_ID=1,
		REFRESHBTN_ID,
		DMBTN_ID,
	};

	user_window(uint64_t userid_, const std::shared_ptr<taccount> &acc_hint_);
	~user_window();
	void RefreshFollow(bool forcerefresh=false);
	void Refresh(bool refreshimg=false);
	void CheckAccHint();
	void fill_accchoice();
	void OnClose(wxCloseEvent &event);
	void OnSelChange(wxCommandEvent &event);
	void OnRefreshBtn(wxCommandEvent &event);
	void OnFollowBtn(wxCommandEvent &event);
	void OnDMBtn(wxCommandEvent &event);
	static user_window *MkWin(uint64_t userid_, const std::shared_ptr<taccount> &acc_hint_);
	static user_window *GetWin(uint64_t userid_);
	static void CheckRefresh(uint64_t userid_, bool refreshimg=false);
	static void RefreshAllFollow();
	static void RefreshAllAcc();
	static void RefreshAll();
	static void CloseAll();

	std::shared_ptr<user_window_timer> uwt;
	static std::weak_ptr<user_window_timer> uwt_common;

	DECLARE_EVENT_TABLE()
};
