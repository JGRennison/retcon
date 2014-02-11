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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TPANEL_AUX
#define HGUARD_SRC_TPANEL_AUX

#include "univdefs.h"
#include "twit.h"
#include "flags.h"
#include "magic_ptr.h"
#include <wx/aui/auibook.h>
#include <wx/scrolwin.h>
#include <wx/statbmp.h>
#include <wx/string.h>
#include <memory>

struct mainframe;
struct panelparentwin_base;

struct profimg_staticbitmap : public wxStaticBitmap {
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

struct tpanel_subtweet_pending_op : public pending_op {
	struct tspo_action_data {
		wxSizer *vbox;
		magic_ptr_ts<tpanelparentwin_nt> win;
		magic_ptr_ts<tweetdispscr> top_tds;
		unsigned int load_count = 0;
		std::shared_ptr<tweet> top_tweet;
	};
	std::shared_ptr<tspo_action_data> action_data;

	tpanel_subtweet_pending_op(wxSizer *v, tpanelparentwin_nt *s, tweetdispscr *top_tds_, unsigned int load_count_,
		std::shared_ptr<tweet> top_tweet_);

	static void CheckLoadTweetReply(const std::shared_ptr<tweet> &t, wxSizer *v, tpanelparentwin_nt *s,
		tweetdispscr *tds, unsigned int load_count, const std::shared_ptr<tweet> &top_tweet, tweetdispscr *top_tds);

	virtual void MarkUnpending(const std::shared_ptr<tweet> &t, flagwrapper<UMPTF> umpt_flags);
	virtual wxString dump();
};

#endif
