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

#ifndef HGUARD_SRC_TPANEL_AUX
#define HGUARD_SRC_TPANEL_AUX

#include "univdefs.h"
#include "twit.h"
#include "flags.h"
#include "magic_ptr.h"
#include <wx/aui/auibook.h>
#include <wx/scrolbar.h>
#include <wx/panel.h>
#include <wx/statbmp.h>
#include <wx/string.h>
#include <wx/brush.h>
#include <memory>

struct mainframe;
struct panelparentwin_base;
struct tpanelscrollwin;
struct tpanelscrollpane;

struct profimg_staticbitmap : public wxStaticBitmap {
	udc_ptr udc;
	tweet_ptr t; // This may be nullptr
	mainframe *owner;

	enum class PISBF {
		HALF                = 1<<0,
		DONTUSEDEFAULTMF    = 1<<1, //Don't use default mainframe
	};
	flagwrapper<PISBF> pisb_flags;

	profimg_staticbitmap(wxWindow* parent, const wxBitmap& label, udc_ptr udc_, tweet_ptr t_, mainframe *owner_ = nullptr, flagwrapper<PISBF> flags = 0);
	~profimg_staticbitmap();
	void ClickHandler(wxMouseEvent &event);
	void RightClickHandler(wxMouseEvent &event);
	void OnTweetActMenuCmd(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};
template<> struct enum_traits<profimg_staticbitmap::PISBF> { static constexpr bool flags = true; };

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

struct tpanel_item : public wxPanel {
	// Outermost: horizontal box sizer
	// Contains: Image on left, vbox on right
	wxBoxSizer *hbox;

	// One level in: vertical box sizer
	// This contains the tweet/user
	// Sizers containing subtweets may be placed underneath
	wxBoxSizer *vbox;


	tpanelscrollpane *parent;
	wxString thisname;

	tpanel_item(tpanelscrollpane *parent_);
	inline wxString GetThisName() const { return thisname; }

	void NotifySizeChange();
	void NotifyLayoutNeeded();
	void mousewheelhandler(wxMouseEvent &event);

	DECLARE_EVENT_TABLE()
};

struct tpanelscrollbar : public wxScrollBar, public magic_paired_ptr_ts<tpanelscrollpane, tpanelscrollbar> {
	panelparentwin_base *parent;
	bool page_scroll_blocked;
	wxString thisname;

	bool scroll_always_freeze = false;
	int scroll_virtual_size = 0;
	int scroll_client_size = 0;

	tpanelscrollbar(panelparentwin_base *parent_);
	void OnScrollHandler(wxScrollEvent &event);
	void OnScrollTrack(wxScrollEvent &event);
	void OnScrollHandlerCommon(bool upok, bool downok, int threshold, int current_position);
	void mousewheelhandler(wxMouseEvent &event);
	void RepositionItems();
	void ScrollItems();
	void ScrollToIndex(unsigned int index, int offset);
	bool ScrollToId(uint64_t id, int offset);
	wxString GetThisName() const { return thisname; }

	private:
	void ScrollItemsForPosition(int current_position);

	DECLARE_EVENT_TABLE()
};

struct tpanelscrollpane : public wxPanel, public magic_paired_ptr_ts<tpanelscrollbar, tpanelscrollpane> {
	panelparentwin_base *parent;
	wxString thisname;
	bool resize_update_pending;

	tpanelscrollpane(panelparentwin_base *parent_);
	void resizehandler(wxSizeEvent &event);
	void resizemsghandler(wxCommandEvent &event);
	void mousewheelhandler(wxMouseEvent &event);
	wxString GetThisName() const { return thisname; }

	DECLARE_EVENT_TABLE()
};

struct tpanel_subtweet_pending_op : public pending_op {
	enum class tspo_type {
		INLINE_REPLY,
		QUOTE,
	};

	struct tspo_action_data {
		wxSizer *vbox;
		magic_ptr_ts<tpanelparentwin_nt> win;
		magic_ptr_ts<tweetdispscr> top_tds;
		magic_ptr_ts<tweetdispscr> source_tds;
		unsigned int load_count = 0;
		tweet_ptr top_tweet;
		tspo_type type;
	};
	std::shared_ptr<tspo_action_data> action_data;

	tpanel_subtweet_pending_op(wxSizer *v, tpanelparentwin_nt *s, tweetdispscr *top_tds_, unsigned int load_count_,
		tweet_ptr top_tweet_, tweetdispscr *source_tds_, tspo_type type_);

	static void CheckLoadTweetReply(tweet_ptr_p t, wxSizer *v, tpanelparentwin_nt *s,
		tweetdispscr *tds, unsigned int load_count, tweet_ptr_p top_tweet, tweetdispscr *top_tds);
	static void CheckLoadQuotedTweet(tweet_ptr_p quote_tweet, wxSizer *v, tpanelparentwin_nt *s,
		tweetdispscr *source_tds, tweetdispscr *top_tds);

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags);
	virtual std::string dump();
	virtual bool IsAlive() const override;
};

#endif
