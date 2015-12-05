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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_UIUTIL
#define HGUARD_SRC_UIUTIL

#include "univdefs.h"
#include "media_id_type.h"
#include "tpanel-common.h"
#include "ptr_types.h"
#include "primaryclipboard.h"
#include "safe_observer_ptr.h"
#include <wx/colour.h>
#include <wx/string.h>
#include <wx/menu.h>
#include <wx/bitmap.h>
#include <wx/richtext/richtextctrl.h>
#include <wx/pen.h>
#include <wx/brush.h>
#include <memory>
#include <map>

struct tweet;
struct tpanelparentwin_nt;
struct userdatacontainer;
struct mainframe;
struct panelparentwin_base;

enum { tpanelmenustartid           = wxID_HIGHEST +  8001 };
enum { tpanelmenuendid             = wxID_HIGHEST + 12000 };
enum { tweetactmenustartid         = wxID_HIGHEST + 12001 };
enum { tweetactmenuendid           = wxID_HIGHEST + 16000 };
enum { lookupprofilestartid        = wxID_HIGHEST + 16001 };
enum { lookupprofileendid          = wxID_HIGHEST + 17000 };

typedef enum {
	TAMI_RETWEET = 1,
	TAMI_FAV,
	TAMI_UNFAV,
	TAMI_REPLY,
	TAMI_BROWSER,
	TAMI_COPYTEXT,
	TAMI_COPYRTTEXT,
	TAMI_COPYID,
	TAMI_COPYLINK,
	TAMI_DELETE,
	TAMI_COPYEXTRA,
	TAMI_BROWSEREXTRA,
	TAMI_MEDIAWIN,
	TAMI_USERWINDOW,
	TAMI_DM,
	TAMI_NULL,
	TAMI_TOGGLEHIGHLIGHT,
	TAMI_MARKREAD,
	TAMI_MARKUNREAD,
	TAMI_MARKNOREADSTATE,
	TAMI_MARKNEWERUNREAD,
	TAMI_MARKOLDERUNREAD,
	TAMI_MARKNEWERUNHIGHLIGHTED,
	TAMI_MARKOLDERUNHIGHLIGHTED,
	TAMI_TOGGLEHIDEIMG,
	TAMI_TOGGLEIMGPREVIEWNOAUTOLOAD,
	TAMI_DELETECACHEDIMG,
	TAMI_TOGGLEHIDDEN,
	TAMI_ADDTOPANEL,
	TAMI_REMOVEFROMPANEL,
	TAMI_DMSETPANEL,
	TAMI_DBG_FORCERELOAD,
	TAMI_COPY_DEBUG_INFO,
} TAMI_TYPE;

struct tweetactmenuitem {
	tweet_ptr tw;
	udc_ptr user;
	TAMI_TYPE type;
	unsigned int dbindex;
	flagwrapper<TPF> flags;
	wxString extra;
	panelparentwin_base *ppwb;
};

typedef std::map<int, std::function<void(mainframe *)> > tpanelmenudata;
typedef std::map<int,tweetactmenuitem> tweetactmenudata;

extern tweetactmenudata tamd;

void AppendToTAMIMenuMap(tweetactmenudata &map, int &nextid, TAMI_TYPE type, tweet_ptr tw,
		unsigned int dbindex = 0, udc_ptr_p user = nullptr,
		flagwrapper<TPF> flags = 0, wxString extra = wxT(""), panelparentwin_base *ppwb = nullptr);
void MakeRetweetMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw);
void MakeFavMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw);
void MakeCopyMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw);
void MakeMarkMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw);
void MakeTPanelMarkMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw, tpanelparentwin_nt *tppw);
void MakeImageMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw);
void MakeDebugMenu(wxMenu *menuP, tweetactmenudata &map, int &nextid, tweet_ptr_p tw);
void TweetActMenuAction(tweetactmenudata &map, int curid, mainframe *mainwin = nullptr);
uint64_t ParseUrlID(wxString url);
media_id_type ParseMediaID(wxString url);
void SaveWindowLayout();
void RestoreWindowLayout();
wxString getreltimestr(time_t timestamp, time_t &updatetime);

struct generic_popup_wrapper_hook {
	virtual void BeforePopup() = 0;
	virtual void AfterPopup() = 0;
};

void GenericPopupWrapper(wxWindow *win, wxMenu *menu, const wxPoint &pos = wxDefaultPosition);

inline void GenericPopupWrapper(wxWindow *win, wxMenu *menu, int x, int y) {
	GenericPopupWrapper(win, menu, wxPoint(x, y));
}

typedef enum {
	CO_SET,
	CO_ADD,
	CO_SUB,
	CO_AND,
	CO_OR,
	CO_RSUB,
	CO_TINT,
} COLOUR_OP;
wxColour ColourOp(const wxColour &in, const wxColour &delta, COLOUR_OP co);
wxColour ColourOp(const wxColour &in, const wxString &co_str);
wxColour NormaliseColour(double br, double bg, double bb);

void DestroyMenuContents(wxMenu *menu);

struct commonRichTextCtrl : public wxRichTextCtrl {
	private:
	bool emojiCheckingEnabled = false;

	void EmojiCheckContentInsertionEventHandler(wxRichTextEvent &event);
	void EmojiCheckCharEventHandler(wxRichTextEvent &event);
	void EmojiCheckRange(long start, long end);

	public:
	bool supress_insert_check = false;

	commonRichTextCtrl(wxWindow *parent_, wxWindowID id = wxID_ANY, const wxString &text = wxEmptyString, long style = wxRE_MULTILINE);

	void WriteBitmapAltText(const wxBitmap& bmp, const wxString &altText);

	void WriteBitmap(const wxBitmap& bmp) {
		WriteBitmapAltText(bmp, wxEmptyString);
	}

	void EnableEmojiChecking(bool enabled);
	void ReplaceAllEmoji();

#if HANDLE_PRIMARY_CLIPBOARD
	void OnLeftUp(wxMouseEvent& event);
	void OnMiddleClick(wxMouseEvent& event);
#endif

	DECLARE_EVENT_TABLE()
};

struct settings_changed_notifier : public safe_observer_ptr_contained<settings_changed_notifier> {
	virtual void NotifySettingsChanged() = 0;

	settings_changed_notifier();
	static void NotifyAll();

	private:
	static safe_observer_ptr_container<settings_changed_notifier> container;
};

struct rounded_box_panel : public wxPanel, safe_observer_ptr_contained<rounded_box_panel> {
	wxBrush fillBrush;
	wxPen linePen;
	int border_radius;
	int horiz_margins;
	int vert_margins;
	wxSize prev_size;

	rounded_box_panel(wxWindow* parent, int border_radius_, int horiz_margins_, int vert_margins_);
	void OnPaint(wxPaintEvent &event);
	void OnSize(wxSizeEvent &event);
	void OnMouseWheel(wxMouseEvent &event);

	DECLARE_EVENT_TABLE()
};

#endif
