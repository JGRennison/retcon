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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TPANEL_PIMPL
#define HGUARD_SRC_TPANEL_PIMPL

#include "univdefs.h"
#include "tpanel.h"
#include "bind_wxevt.h"
#include "twit.h"
#include "map.h"
#include <list>
#include <map>
#include <deque>
#include <string>
#include <wx/timer.h>
#include <wx/string.h>

struct tpanelscrollbar;
struct tpanelscrollpane;
class wxBoxSizer;
class wxWindow;
class wxStaticText;
class wxButton;
class wxBoxSizer;
struct tweetdispscr_mouseoverwin;
struct tpanelglobal;

struct panelparentwin_base;

struct panelparentwin_base_impl : public bindwxevt {
	friend panelparentwin_base;

	panelparentwin_base *base_ptr;
	panelparentwin_base *base() { return base_ptr; }

	panelparentwin_base_impl(panelparentwin_base *base_)
			: base_ptr(base_) { }

	void PopTop();
	void PopBottom();
	void RemoveIndex(size_t offset);
	void pageupevthandler(wxCommandEvent &event);
	void pagedownevthandler(wxCommandEvent &event);
	void pagetopevthandler(wxCommandEvent &event);
	virtual void PageUpHandler() { }
	virtual void PageDownHandler() { }
	virtual void PageTopHandler() { }
	void ShowHideButtons(std::string type, bool show);
	virtual void HandleScrollToIDOnUpdate() { }
	void SetNoUpdateFlag();
	void UpdateCLabelLater();
	void SetClabelUpdatePendingFlag();
	void CheckClearNoUpdateFlag();
	virtual void UpdateCLabel() { }
	void CLabelNeedsUpdating(flagwrapper<PUSHFLAGS> pushflags);
	uint64_t GetCurrentViewTopID(optional_observer_ptr<int> offset = nullptr) const;
	virtual void IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const;
	int IDToCurrentDispIndex(uint64_t id) const;
	wxString GetThisName() const;
	virtual mainframe *GetMainframe();
	tpanel_disp_item *CreateItemAtPosition(tpanel_disp_item_list::iterator iter, uint64_t id);

	void ResetBatchTimer();

	//this calls ResetBatchTimer if TPPWF::NOUPDATEONPUSH is not set
	//the idea is to prevent excessive handling of the timer
	void UpdateBatchTimer();

	private:
	void RemoveIndexIntl(size_t offset);

	public:
	std::shared_ptr<tpanelglobal> tpg;
	flagwrapper<TPPWF> tppw_flags = 0;
	ssize_t displayoffset = 0; // negative indicates not currently valid
	wxWindow *parent_win = nullptr;
	tpanelscrollbar *scrollbar = nullptr;
	tpanelscrollpane *scrollpane = nullptr;
	wxStaticText *clabel = nullptr;
	wxButton *MarkReadBtn = nullptr;
	wxButton *NewestUnreadBtn = nullptr;
	wxButton *OldestUnreadBtn = nullptr;
	wxButton *UnHighlightBtn = nullptr;
	wxButton *MoreBtn = nullptr;
	wxBoxSizer* headersizer = nullptr;
	uint64_t scrolltoid = 0;
	uint64_t scrolltoid_onupdate = 0;
	std::multimap<std::string, wxButton *> showhidemap;
	tpanel_disp_item_list currentdisp;
	wxString thisname;
	wxTimer batchtimer;

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin_nt;

struct tpanelparentwin_nt_impl : public panelparentwin_base_impl {
	tpanelparentwin_nt *base() { return static_cast<tpanelparentwin_nt *>(base_ptr); }

	tpanelparentwin_nt_impl(tpanelparentwin_nt *base_)
			: panelparentwin_base_impl(base_) { }

	std::shared_ptr<tpanel> tp;
	magic_ptr_ts<tweetdispscr_mouseoverwin> mouseoverwin;

	struct tweetbatchqueue_item {
		uint64_t id;
		optional_tweet_ptr t;
		flagwrapper<PUSHFLAGS> pushflags;
	};
	std::deque<tweetbatchqueue_item> pushtweetbatchqueue;
	std::deque<std::pair<uint64_t, flagwrapper<PUSHFLAGS>>> removetweetbatchqueue;
	container::map<uint64_t, bool> updatetweetbatchqueue;
	std::deque<std::function<void(tpanelparentwin_nt *)> > batchedgenericactions;

	//These hold tweet IDs and retweet source IDs
	container::map<uint64_t, unsigned int> tweetid_count_map;
	static container::map<uint64_t, unsigned int> all_tweetid_count_map;

	void PushTweet(uint64_t id, optional_tweet_ptr t, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	void PushTweet(tweet_ptr_p t, flagwrapper<PUSHFLAGS> pushflags) {
		PushTweet(t->id, t, pushflags);
	}

	void RemoveTweet(uint64_t id, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	tweetdispscr *CreateTweetInItem(tweet_ptr_p t, tpanel_disp_item &tpdi);
	tweetdispscr *CreateSubTweetInItemHbox(tweet_ptr_p t, tweetdispscr *parent_tds, wxBoxSizer *subhbox, wxWindow *parent);
	void JumpToTweetID(uint64_t id);
	virtual void LoadMore(unsigned int n, uint64_t lessthanid = 0, uint64_t greaterthanid = 0, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT) { }
	virtual void PageUpHandler() override;
	virtual void PageDownHandler() override;
	virtual void PageTopHandler() override;
	virtual void HandleScrollToIDOnUpdate() override;
	void markallreadevthandler(wxCommandEvent &event);
	void markremoveallhighlightshandler(wxCommandEvent &event);
	void setupnavbuttonhandlers();
	void morebtnhandler(wxCommandEvent &event);
	void OnBatchTimerModeTimer(wxTimerEvent& event);
	virtual void IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const override;
	virtual void UpdateCLabel() override;
	void EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush);
	void UpdateOwnTweet(uint64_t id, bool redrawimg);
	tweetdispscr_mouseoverwin *MakeMouseOverWin();
	void GenericAction(std::function<void(tpanelparentwin_nt *)> func);
	void RecalculateDisplayOffset();

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin;

struct tpanelparentwin_impl : public tpanelparentwin_nt_impl {
	tpanelparentwin *base() { return static_cast<tpanelparentwin *>(base_ptr); }

	tpanelparentwin_impl(tpanelparentwin *base_)
			: tpanelparentwin_nt_impl(base_) { }

	enum class TPWF {
		UNREADBITMAPDISP	= 1<<0,
	};
	flagwrapper<TPWF> tpw_flags = 0;
	mainframe *owner = 0;

	virtual void LoadMore(unsigned int n, uint64_t lessthanid = 0, uint64_t greaterthanid = 0, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT) override;
	void tabdetachhandler(wxCommandEvent &event);
	void tabduphandler(wxCommandEvent &event);
	void tabdetachedduphandler(wxCommandEvent &event);
	void tabclosehandler(wxCommandEvent &event);
	void tabsplitcmdhandler(wxCommandEvent &event);
	virtual void UpdateCLabel() override;
	virtual mainframe *GetMainframe() override { return owner; }

	DECLARE_EVENT_TABLE()
};
template<> struct enum_traits<tpanelparentwin_impl::TPWF> { static constexpr bool flags = true; };

struct tpanelparentwin_usertweets;

struct tpanelparentwin_usertweets_impl : public tpanelparentwin_nt_impl {
	tpanelparentwin_usertweets *base() { return static_cast<tpanelparentwin_usertweets *>(base_ptr); }

	tpanelparentwin_usertweets_impl(tpanelparentwin_usertweets *base_)
			: tpanelparentwin_nt_impl(base_) { }

	udc_ptr user;
	std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc;
	static std::map<std::pair<uint64_t, RBFS_TYPE>, std::shared_ptr<tpanel> > usertpanelmap;	//use map rather than unordered_map due to the hassle associated with specialising std::hash
	bool havestarted = false;
	bool failed = false;
	bool inprogress = false;
	RBFS_TYPE type = RBFS_NULL;

	virtual void LoadMore(unsigned int n, uint64_t lessthanid = 0, uint64_t greaterthanid = 0, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT) override;
	virtual void UpdateCLabel() override;
};

struct tpanelparentwin_user;

struct tpanelparentwin_user_impl : public panelparentwin_base_impl {
	tpanelparentwin_user *base() { return static_cast<tpanelparentwin_user *>(base_ptr); }

	tpanelparentwin_user_impl(tpanelparentwin_user *base_)
			: panelparentwin_base_impl(base_) { }

	std::deque<udc_ptr> userlist;
	static std::multimap<uint64_t, tpanelparentwin_user*> pendingmap;

	virtual void PageUpHandler() override;
	virtual void PageDownHandler() override;
	virtual void PageTopHandler() override;
	virtual size_t ItemCount() { return userlist.size(); }
	bool PushBackUser(udc_ptr_p u);
	bool UpdateUser(udc_ptr_p u);
	virtual void LoadMoreToBack(unsigned int n) { }
};

struct tpanelparentwin_userproplisting;

struct tpanelparentwin_userproplisting_impl : public tpanelparentwin_user_impl {
	tpanelparentwin_userproplisting *base() { return static_cast<tpanelparentwin_userproplisting *>(base_ptr); }

	tpanelparentwin_userproplisting_impl(tpanelparentwin_userproplisting *base_)
			: tpanelparentwin_user_impl(base_) { }

	std::deque<uint64_t> useridlist;
	udc_ptr user;
	std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc;
	bool havestarted;
	bool failed = false;
	bool inprogress = false;
	tpanelparentwin_userproplisting::TYPE type;

	virtual void Init();
	virtual size_t ItemCount() override { return useridlist.size(); }
	virtual void UpdateCLabel() override;
	virtual void LoadMoreToBack(unsigned int n) override;
};

#endif
