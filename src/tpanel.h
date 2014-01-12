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

#ifndef HGUARD_SRC_TPANEL
#define HGUARD_SRC_TPANEL

#include "univdefs.h"
#include "tpanel-common.h"
#include "twit-common.h"
#include "rbfs.h"
#include "uiutil.h"
#include "magic_ptr.h"
#include "flags.h"
#include <wx/statbmp.h>
#include <wx/aui/auibook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/scrolwin.h>
#include <wx/image.h>
#include <list>
#include <map>
#include <deque>
#include <forward_list>

#define BATCH_TIMER_DELAY 200

struct tpanelparentwin;
struct dispscr_base;
struct tpanelscrollwin;
struct mainframe;
struct tweetdispscr;

DECLARE_EVENT_TYPE(wxextRESIZE_UPDATE_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEUP_EVENT, -1)
DECLARE_EVENT_TYPE(wxextTP_PAGEDOWN_EVENT, -1)

struct tweetdispscr_mouseoverwin;

struct tpanelreltimeupdater : public wxTimer {
	void Notify() override;
};

struct tpanelglobal {
	wxBitmap arrow;
	int arrow_dim;
	tpanelreltimeupdater minutetimer;
	wxBitmap infoicon;
	wxImage infoicon_img;
	wxBitmap replyicon;
	wxImage replyicon_img;
	wxBitmap favicon;
	wxImage favicon_img;
	wxBitmap favonicon;
	wxImage favonicon_img;
	wxBitmap retweeticon;
	wxImage retweeticon_img;
	wxBitmap retweetonicon;
	wxImage retweetonicon_img;
	wxBitmap dmreplyicon;
	wxImage dmreplyicon_img;
	wxBitmap proticon;
	wxImage proticon_img;
	wxBitmap verifiedicon;
	wxImage verifiedicon_img;
	wxBitmap closeicon;
	wxBitmap multiunreadicon;

	static std::shared_ptr<tpanelglobal> Get();
	static std::weak_ptr<tpanelglobal> tpg_glob;

	tpanelglobal();	//use Get() instead
};

struct profimg_staticbitmap: public wxStaticBitmap {
	uint64_t userid;
	uint64_t tweetid;
	mainframe *owner;

	enum class PISBF {
		HALF                = 1<<0,
		DONTUSEDEFAULTMF    = 1<<1, //Don't use default mainframe
	};
	flagwrapper<PISBF> pisb_flags;

	inline profimg_staticbitmap(wxWindow* parent, const wxBitmap& label, uint64_t userid_, uint64_t tweetid_, mainframe *owner_ = 0, flagwrapper<PISBF> flags = 0)
		: wxStaticBitmap(parent, wxID_ANY, label, wxPoint(-1000, -1000)), userid(userid_), tweetid(tweetid_), owner(owner_), pisb_flags(flags) { }
	void ClickHandler(wxMouseEvent &event);
	void RightClickHandler(wxMouseEvent &event);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};
template<> struct enum_traits<profimg_staticbitmap::PISBF> { static constexpr bool flags = true; };

struct tpanel : std::enable_shared_from_this<tpanel> {
	std::string name;
	std::string dispname;
	tweetidset tweetlist;
	std::forward_list<tpanelparentwin_nt*> twin;
	flagwrapper<TPF> flags;
	uint64_t upperid;
	uint64_t lowerid;
	cached_id_sets cids;
	std::vector<tpanel_auto> tpautos;

	static std::shared_ptr<tpanel> MkTPanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_ = 0, std::shared_ptr<taccount> *acc = 0);
	static std::shared_ptr<tpanel> MkTPanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_, std::vector<tpanel_auto> tpautos_);
	tpanel(const std::string &name_, const std::string &dispname_, flagwrapper<TPF> flags_, std::vector<tpanel_auto> tpautos_);		//don't use this directly
	~tpanel();

	static void NameDefaults(std::string &name, std::string &dispname, const std::vector<tpanel_auto> &tpautos);

	void PushTweet(const std::shared_ptr<tweet> &t, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	bool RegisterTweet(const std::shared_ptr<tweet> &t);
	tpanelparentwin *MkTPanelWin(mainframe *parent, bool select=false);
	void OnTPanelWinClose(tpanelparentwin_nt *tppw);
	bool IsSingleAccountTPanel() const;
	void TPPWFlagMaskAllTWins(flagwrapper<TPPWF> set, flagwrapper<TPPWF> clear) const;

	private:
	void RecalculateSets();
};

struct tpanelnotebook : public wxAuiNotebook {
	mainframe *owner;

	tpanelnotebook(mainframe *owner_, wxWindow *parent);
	void dragdrophandler(wxAuiNotebookEvent& event);
	void dragdonehandler(wxAuiNotebookEvent& event);
	void tabrightclickhandler(wxAuiNotebookEvent& event);
	void tabclosedhandler(wxAuiNotebookEvent& event);
	void onsizeevt(wxSizeEvent &event);
	void tabnumcheck();
	virtual void Split(size_t page, int direction) override;
	void PostSplitSizeCorrect();
	void FillWindowLayout(unsigned int mainframeindex);

	DECLARE_EVENT_TABLE()
};

struct tppw_scrollfreeze {
	enum class SF {
		ALWAYSFREEZE	= 1<<0,
	};

	dispscr_base *scr = 0;
	int extrapixels = 0;
	flagwrapper<SF> flags = 0;
};
template<> struct enum_traits<tppw_scrollfreeze::SF> { static constexpr bool flags = true; };

enum {	//window IDs
	TPPWID_DETACH = 100,
	TPPWID_DUP,
	TPPWID_DETACHDUP,
	TPPWID_CLOSE,
	TPPWID_TOPBTN,
	TPPWID_SPLIT,
	TPPWID_MARKALLREADBTN,
	TPPWID_NEWESTUNREADBTN,
	TPPWID_OLDESTUNREADBTN,
	TPPWID_NEWESTHIGHLIGHTEDBTN,
	TPPWID_OLDESTHIGHLIGHTEDBTN,
	TPPWID_NEXT_NEWESTUNREADBTN,
	TPPWID_NEXT_OLDESTUNREADBTN,
	TPPWID_NEXT_NEWESTHIGHLIGHTEDBTN,
	TPPWID_NEXT_OLDESTHIGHLIGHTEDBTN,
	TPPWID_UNHIGHLIGHTALLBTN,
	TPPWID_MOREBTN,
	TPPWID_JUMPTONUM,
	TPPWID_JUMPTOID,
	TPPWID_TOGGLEHIDDEN,
	TPPWID_TOGGLEHIDEDELETED,
	TPPWID_TIMER_BATCHMODE,
};

struct panelparentwin_base : public wxPanel, public magic_ptr_base {
	std::shared_ptr<tpanelglobal> tpg;
	wxBoxSizer *sizer;
	size_t displayoffset;
	wxWindow *parent_win;
	tpanelscrollwin *scrollwin;
	wxStaticText *clabel;
	flagwrapper<TPPWF> tppw_flags = 0;
	wxButton *MarkReadBtn;
	wxButton *NewestUnreadBtn;
	wxButton *OldestUnreadBtn;
	wxButton *UnHighlightBtn;
	wxButton *MoreBtn;
	wxBoxSizer* headersizer;
	uint64_t scrolltoid = 0;
	uint64_t scrolltoid_onupdate = 0;
	std::multimap<std::string, wxButton *> showhidemap;
	std::list<std::pair<uint64_t, dispscr_base *> > currentdisp;
	wxString thisname;
	wxTimer batchtimer;

	panelparentwin_base(wxWindow *parent, bool fitnow=true, wxString thisname_ = wxT(""));
	virtual ~panelparentwin_base() { }
	virtual mainframe *GetMainframe();
	virtual void PageUpHandler() { };
	virtual void PageDownHandler() { };
	virtual void PageTopHandler() { };
	void pageupevthandler(wxCommandEvent &event);
	void pagedownevthandler(wxCommandEvent &event);
	void pagetopevthandler(wxCommandEvent &event);
	virtual void UpdateCLabel() { }
	void CLabelNeedsUpdating(flagwrapper<PUSHFLAGS> pushflags);
	void SetNoUpdateFlag();
	void CheckClearNoUpdateFlag();
	virtual void HandleScrollToIDOnUpdate() { }
	void PopTop();
	void PopBottom();
	void StartScrollFreeze(tppw_scrollfreeze &s);
	void EndScrollFreeze(tppw_scrollfreeze &s);
	void SetScrollFreeze(tppw_scrollfreeze &s, dispscr_base *scr);
	virtual bool IsSingleAccountWin() const;
	void ShowHideButtons(std::string type, bool show);
	virtual void NotifyRequestFailed() { }
	inline wxString GetThisName() const { return thisname; }
	uint64_t GetCurrentViewTopID() const;
	virtual void IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const;
	virtual void StartBatchTimerMode() { }

	DECLARE_EVENT_TABLE()

	protected:
	void ResetBatchTimer();

	private:
	void RemoveIndexIntl(size_t offset);
};

struct tpanelparentwin_nt : public panelparentwin_base {
	std::shared_ptr<tpanel> tp;
	tweetdispscr_mouseoverwin *mouseoverwin = 0;
	std::map<int, std::function<void(wxCommandEvent &event)> > btnhandlerlist;
	std::deque<std::pair<std::shared_ptr<tweet>, flagwrapper<PUSHFLAGS> > > pushtweetbatchqueue;

	tpanelparentwin_nt(const std::shared_ptr<tpanel> &tp_, wxWindow *parent, wxString thisname_ = wxT(""));
	virtual ~tpanelparentwin_nt();
	void PushTweet(const std::shared_ptr<tweet> &t, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	tweetdispscr *PushTweetIndex(const std::shared_ptr<tweet> &t, size_t index);
	virtual void LoadMore(unsigned int n, uint64_t lessthanid = 0, uint64_t greaterthanid = 0, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT) { }
	virtual void UpdateCLabel();
	virtual void PageUpHandler() override;
	virtual void PageDownHandler() override;
	virtual void PageTopHandler() override;
	virtual void JumpToTweetID(uint64_t id);
	virtual void HandleScrollToIDOnUpdate() override;
	void markallreadevthandler(wxCommandEvent &event);
	void MarkSetRead();
	void MarkSetRead(tweetidset &&subset);
	void markremoveallhighlightshandler(wxCommandEvent &event);
	void MarkSetUnhighlighted();
	void MarkSetUnhighlighted(tweetidset &&subset);
	void setupnavbuttonhandlers();
	void navbuttondispatchhandler(wxCommandEvent &event);
	void morebtnhandler(wxCommandEvent &event);
	void MarkClearCIDSSetHandler(std::function<tweetidset &(cached_id_sets &)> idsetselector,
			std::function<void(const std::shared_ptr<tweet> &)> existingtweetfunc, const tweetidset &subset);
	virtual bool IsSingleAccountWin() const override { return tp->IsSingleAccountTPanel(); }
	void EnumDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush);
	void UpdateOwnTweet(const tweet &t, bool redrawimg);
	tweetdispscr_mouseoverwin *MakeMouseOverWin();
	virtual void IterateCurrentDisp(std::function<void(uint64_t, dispscr_base *)> func) const override;
	virtual void StartBatchTimerMode() override;
	void OnBatchTimerModeTimer(wxTimerEvent& event);

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin : public tpanelparentwin_nt {
	mainframe *owner;

	enum class TPWF {
		UNREADBITMAPDISP	= 1<<0,
	};
	flagwrapper<TPWF> tpw_flags = 0;

	tpanelparentwin(const std::shared_ptr<tpanel> &tp_, mainframe *parent, bool select = false, wxString thisname_ = wxT(""));
	virtual void LoadMore(unsigned int n, uint64_t lessthanid = 0, uint64_t greaterthanid = 0, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT) override;
	virtual mainframe *GetMainframe() override { return owner; }
	uint64_t PushTweetOrRetLoadId(uint64_t id, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	uint64_t PushTweetOrRetLoadId(const std::shared_ptr<tweet> &tobj, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT);
	void tabdetachhandler(wxCommandEvent &event);
	void tabduphandler(wxCommandEvent &event);
	void tabdetachedduphandler(wxCommandEvent &event);
	void tabclosehandler(wxCommandEvent &event);
	void tabsplitcmdhandler(wxCommandEvent &event);
	virtual void UpdateCLabel() override;

	DECLARE_EVENT_TABLE()
};
template<> struct enum_traits<tpanelparentwin::TPWF> { static constexpr bool flags = true; };

struct tpanelparentwin_usertweets : public tpanelparentwin_nt {
	std::shared_ptr<userdatacontainer> user;
	std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc;
	static std::map<std::pair<uint64_t, RBFS_TYPE>, std::shared_ptr<tpanel> > usertpanelmap;	//use map rather than unordered_map due to the hassle associated with specialising std::hash
	bool havestarted;
	bool failed = false;
	RBFS_TYPE type;

	tpanelparentwin_usertweets(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent,
			std::function<std::shared_ptr<taccount>(tpanelparentwin_usertweets &)> getacc, RBFS_TYPE type_ = RBFS_USER_TIMELINE, wxString thisname_ = wxT(""));
	~tpanelparentwin_usertweets();
	virtual void LoadMore(unsigned int n, uint64_t lessthanid = 0, uint64_t greaterthanid = 0, flagwrapper<PUSHFLAGS> pushflags = PUSHFLAGS::DEFAULT) override;
	virtual void UpdateCLabel() override;
	static std::shared_ptr<tpanel> MkUserTweetTPanel(const std::shared_ptr<userdatacontainer> &user, RBFS_TYPE type_ = RBFS_USER_TIMELINE);
	static std::shared_ptr<tpanel> GetUserTweetTPanel(uint64_t userid, RBFS_TYPE type_ = RBFS_USER_TIMELINE);
	virtual bool IsSingleAccountWin() const override { return true; }
	virtual void NotifyRequestFailed() override;

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin_user : public panelparentwin_base {
	std::deque< std::shared_ptr<userdatacontainer> > userlist;

	static std::multimap<uint64_t, tpanelparentwin_user*> pendingmap;

	tpanelparentwin_user(wxWindow *parent, wxString thisname_ = wxT(""));
	~tpanelparentwin_user();
	bool PushBackUser(const std::shared_ptr<userdatacontainer> &u);
	bool UpdateUser(const std::shared_ptr<userdatacontainer> &u, size_t offset);
	virtual void LoadMoreToBack(unsigned int n) { }
	virtual void PageUpHandler() override;
	virtual void PageDownHandler() override;
	virtual void PageTopHandler() override;
	virtual size_t ItemCount() { return userlist.size(); }

	DECLARE_EVENT_TABLE()
};

struct tpanelparentwin_userproplisting : public tpanelparentwin_user {
	std::deque<uint64_t> useridlist;
	std::shared_ptr<userdatacontainer> user;
	std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc;
	bool havestarted;
	bool failed = false;
	CS_ENUMTYPE type;

	tpanelparentwin_userproplisting(std::shared_ptr<userdatacontainer> &user_, wxWindow *parent,
			std::function<std::shared_ptr<taccount>(tpanelparentwin_userproplisting &)> getacc, CS_ENUMTYPE type_, wxString thisname_ = wxT(""));
	~tpanelparentwin_userproplisting();
	virtual void LoadMoreToBack(unsigned int n) override;
	virtual void UpdateCLabel() override;
	virtual void Init();
	virtual size_t ItemCount() override { return useridlist.size(); }
	virtual void NotifyRequestFailed() override;

	DECLARE_EVENT_TABLE()
};

struct tpanelscrollwin : public wxScrolledWindow {
	panelparentwin_base *parent;
	bool resize_update_pending;
	bool page_scroll_blocked;
	bool fit_inside_blocked;
	wxString thisname;

	tpanelscrollwin(panelparentwin_base *parent_);
	void OnScrollHandler(wxScrollWinEvent &event);
	void resizehandler(wxSizeEvent &event);
	void resizemsghandler(wxCommandEvent &event);
	inline wxString GetThisName() const { return thisname; }

	DECLARE_EVENT_TABLE()
};

bool RedirectMouseWheelEvent(wxMouseEvent &event, wxWindow *avoid=0);
void MakeTPanelMenu(wxMenu *menuP, tpanelmenudata &map);
void TPanelMenuAction(tpanelmenudata &map, int curid, mainframe *parent);
void CheckClearNoUpdateFlag_All();
void StartBatchTimerMode_All();

extern std::forward_list<tpanelparentwin_nt*> tpanelparentwinlist;

#endif
