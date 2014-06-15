//  retcon
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

#ifndef HGUARD_SRC_TPANEL
#define HGUARD_SRC_TPANEL

#include "univdefs.h"
#include "tpanel-common.h"
#include "twit-common.h"
#include "rbfs.h"
#include "uiutil.h"
#include "magic_ptr.h"
#include "flags.h"
#include <wx/panel.h>
#include <forward_list>
#include <functional>
#include <list>

#define BATCH_TIMER_DELAY 100

struct tpanelparentwin;
struct dispscr_base;
struct tpanelscrollwin;
struct mainframe;
struct tweetdispscr;
struct tpanel;
struct tweetdispscr;
struct bindwxevt_win;
struct tpanel_item;

DECLARE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEUP_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT, -1)

struct tweetdispscr_mouseoverwin;

struct tpanel_disp_item {
	uint64_t id;
	dispscr_base *disp;
	tpanel_item *item;
};

typedef std::list<tpanel_disp_item> tpanel_disp_item_list;

struct panelparentwin_base_impl;

struct panelparentwin_base : public wxPanel, public magic_ptr_base {
	std::unique_ptr<panelparentwin_base_impl> pimpl_ptr;

	std::unique_ptr<bindwxevt_win> evtbinder;
	const panelparentwin_base_impl *pimpl() const;
	panelparentwin_base_impl *pimpl() { return const_cast<panelparentwin_base_impl *>(const_cast<const panelparentwin_base*>(this)->pimpl()); }

	panelparentwin_base(wxWindow *parent, bool fitnow=true, wxString thisname_ = wxT(""), panelparentwin_base_impl *privimpl = 0);
	virtual ~panelparentwin_base();
	mainframe *GetMainframe();
	void UpdateCLabel();
	void UpdateCLabelLater();
	void SetNoUpdateFlag();
	void SetClabelUpdatePendingFlag();
	void CheckClearNoUpdateFlag();
	virtual bool IsSingleAccountWin() const;
	virtual void NotifyRequestFailed() { }
	wxString GetThisName() const;
	uint64_t GetCurrentViewTopID() const;
	void IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const;
	int IDToCurrentDispIndex(uint64_t id) const;
	const tpanel_disp_item_list &GetCurrentDisp() const;
	flagwrapper<TPPWF> GetTPPWFlags() const;
};

struct tpanelparentwin_nt_impl;
struct tpanelparentwin_nt : public panelparentwin_base {
	const tpanelparentwin_nt_impl *pimpl() const;
	tpanelparentwin_nt_impl *pimpl() { return const_cast<tpanelparentwin_nt_impl *>(const_cast<const tpanelparentwin_nt*>(this)->pimpl()); }

	tpanelparentwin_nt(const std::shared_ptr<tpanel> &tp_, wxWindow *parent, wxString thisname_ = wxT(""), tpanelparentwin_nt_impl *privimpl = 0);
	virtual ~tpanelparentwin_nt();
	void PushTweet(tweet_ptr_p t, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	void RemoveTweet(uint64_t id, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	void JumpToTweetID(uint64_t id);
	virtual bool IsSingleAccountWin() const override;
	void EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush);
	void UpdateOwnTweet(uint64_t id, bool redrawimg);
	void UpdateOwnTweet(const tweet &t, bool redrawimg);
	tweetdispscr_mouseoverwin *MakeMouseOverWin();
	void GenericAction(std::function<void(tpanelparentwin_nt *)> func);
	std::shared_ptr<tpanel> GetTP();

	//These are for TweetActMenuAction
	void MarkSetRead(tweetidset &&subset);
	void MarkSetUnhighlighted(tweetidset &&subset);

	//These are primarily for for tweetdispscr PanelInsertEvt/PanelRemoveEvt
	void IncTweetIDRefCounts(uint64_t tid, uint64_t rtid);
	void DecTweetIDRefCounts(uint64_t tid, uint64_t rtid);

	//Calls UpdateCLabel for all tpanelparentwin_nt
	static void UpdateAllCLabels();
};

struct tpanelparentwin_impl;
struct tpanelparentwin : public tpanelparentwin_nt {
	const tpanelparentwin_impl *pimpl() const;
	tpanelparentwin_impl *pimpl() { return const_cast<tpanelparentwin_impl *>(const_cast<const tpanelparentwin*>(this)->pimpl()); }

	tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select = false, wxString thisname_ = wxT(""), tpanelparentwin_impl *privimpl = 0);
};
struct tpanelparentwin_usertweets_impl;

struct tpanelparentwin_usertweets : public tpanelparentwin_nt {
	tpanelparentwin_usertweets_impl *pimpl();

	tpanelparentwin_usertweets(udc_ptr &user_, wxWindow *parent,
			std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc,
			RBFS_TYPE type_ = RBFS_USER_TIMELINE, wxString thisname_ = wxT(""), tpanelparentwin_usertweets_impl *privimpl = 0);
	~tpanelparentwin_usertweets();
	static std::shared_ptr<tpanel> MkUserTweetTPanel(udc_ptr_p user, RBFS_TYPE type_ = RBFS_USER_TIMELINE);
	static std::shared_ptr<tpanel> GetUserTweetTPanel(uint64_t userid, RBFS_TYPE type_ = RBFS_USER_TIMELINE);
	virtual bool IsSingleAccountWin() const override { return true; }
	virtual void NotifyRequestFailed() override;
	void InitLoading();
};

struct tpanelparentwin_user_impl;
struct tpanelparentwin_user : public panelparentwin_base {
	tpanelparentwin_user_impl *pimpl();

	tpanelparentwin_user(wxWindow *parent, wxString thisname_ = wxT(""), tpanelparentwin_user_impl *privimpl = 0);
	~tpanelparentwin_user();
	bool PushBackUser(udc_ptr_p u);
	void LoadMoreToBack(unsigned int n);
	static void CheckPendingUser(udc_ptr_p u);
};

struct tpanelparentwin_userproplisting_impl;
struct tpanelparentwin_userproplisting : public tpanelparentwin_user {
	tpanelparentwin_userproplisting_impl *pimpl();

	tpanelparentwin_userproplisting(udc_ptr_p user_, wxWindow *parent,
			std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc,
			CS_ENUMTYPE type_, wxString thisname_ = wxT(""), tpanelparentwin_userproplisting_impl *privimpl = 0);
	~tpanelparentwin_userproplisting();
	virtual void NotifyRequestFailed() override;
	void PushUserIDToBack(uint64_t id);
	void InitLoading();
};

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid=0);
void MakeTPanelMenu(wxMenu *menuP, tpanelmenudata &map);
void TPanelMenuAction(tpanelmenudata &map, int curid, mainframe *parent);
void TPanelMenuActionCustom(mainframe *parent, flagwrapper<TPF> flags);
void CheckClearNoUpdateFlag_All();
void SetNoUpdateFlag_All();

void EnumAllDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush);

extern std::forward_list<tpanelparentwin_nt*> tpanelparentwinlist;

#endif
