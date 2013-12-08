//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_USERUI
#define HGUARD_SRC_USERUI

#include "univdefs.h"
#include "magic_ptr.h"
#include <wx/hyperlink.h>
#include <wx/radiobox.h>
#include <wx/valgen.h>
#include <wx/notebook.h>
#include <wx/event.h>
#include <wx/timer.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <memory>
#include <forward_list>

struct acc_choice;
struct tpanelparentwin_usertweets;
struct tpanelparentwin_userproplisting;
struct userdatacontainer;
struct taccount;

struct notebook_event_prehandler : public wxEvtHandler {
	void OnPageChange(wxNotebookEvent &event);
	wxNotebook *nb;
	std::forward_list<tpanelparentwin_usertweets *> timeline_pane_list;
	std::forward_list<tpanelparentwin_userproplisting *> userlist_pane_list;

	DECLARE_EVENT_TABLE()
};

struct user_window_timer: public wxTimer {
	void Notify();
};

struct user_window: public wxDialog, public magic_ptr_base {
	uint64_t userid;
	std::shared_ptr<userdatacontainer> u;
	std::weak_ptr<taccount> acc_hint;

	wxFlexGridSizer *if_grid;

	wxStaticBitmap *usericon;
	wxStaticText *name;
	wxStaticText *screen_name;
	wxStaticText *name2;
	wxStaticText *screen_name2;
	wxStaticText *desc;
	wxStaticText *location;
	wxStaticText *isprotected;
	wxStaticText *isverified;
	wxStaticText *tweets;
	wxStaticText *followers;
	wxStaticText *follows;
	wxStaticText *faved;
	wxStaticText *createtime;
	wxStaticText *url_label;
	wxHyperlinkCtrl *url;
	wxHyperlinkCtrl *profileurl;
	wxStaticText *lastupdate;
	wxStaticText *id_str;
	tpanelparentwin_usertweets *timeline_pane;
	tpanelparentwin_usertweets *fav_timeline_pane;
	tpanelparentwin_userproplisting *followers_pane;
	tpanelparentwin_userproplisting *friends_pane;
	notebook_event_prehandler nb_prehndlr;
	wxNotebook *nb;

	wxStaticText *ifollow;
	wxStaticText *followsme;
	wxChoice *accchoice;

	wxButton *followbtn;
	wxButton *refreshbtn;
	wxButton *dmbtn;
	enum FOLLOWBTNMODE {
		FBM_NONE = 0, FBM_FOLLOW = 1, FBM_UNFOLLOW, FBM_REMOVE_PENDING,
	};
	FOLLOWBTNMODE follow_btn_mode;

	enum {
		FOLLOWBTN_ID = 1,
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
	static void CheckRefresh(uint64_t userid_, bool refreshimg = false);
	static void RefreshAllFollow();
	static void RefreshAllAcc();
	static void RefreshAll();
	static void CloseAll();

	std::shared_ptr<user_window_timer> uwt;
	static std::weak_ptr<user_window_timer> uwt_common;

	DECLARE_EVENT_TABLE()
};

enum {
	ACCCF_OKBTNCTRL     = 1<<0,
	ACCCF_NOACCITEM     = 1<<1,
};

typedef void (*acc_choice_callback)(void *, acc_choice *, bool);

struct acc_choice: public wxChoice {
	std::shared_ptr<taccount> &curacc;
	unsigned int flags;
	acc_choice_callback fnptr;
	void *fnextra;

	acc_choice(wxWindow *parent, std::shared_ptr<taccount> &acc, unsigned int flags_, int winid = wxID_ANY, acc_choice_callback callbck = 0, void *extra = 0);
	void UpdateSel();
	void OnSelChange(wxCommandEvent &event);
	void fill_acc();
	void TrySetSel(const taccount *tac);

	DECLARE_EVENT_TABLE()
};

struct user_lookup_dlg: public wxDialog {
	std::shared_ptr<taccount> &curacc;

	user_lookup_dlg(wxWindow *parent, int *type, wxString *value, std::shared_ptr<taccount> &acc);
	void OnTCEnter(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

#endif
