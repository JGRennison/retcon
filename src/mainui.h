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

#ifndef HGUARD_SRC_MAINUI
#define HGUARD_SRC_MAINUI

#include "univdefs.h"
#include "uiutil.h"
#include <wx/menu.h>
#include <wx/event.h>
#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/colour.h>
#include <wx/stattext.h>
#include <wx/bmpbuttn.h>
#include <wx/sizer.h>
#include <wx/richtext/richtextctrl.h>
#include <vector>
#include <map>

struct tweetpostwin;
struct tpanelnotebook;
struct acc_choice;
struct taccount;
struct tweet;
struct userdatacontainer;

enum
{
    ID_Close      = wxID_CLOSE,
    ID_Quit       = wxID_EXIT,
    ID_About      = wxID_ABOUT,
    ID_Settings   = 1,
    ID_Accounts,
    ID_Viewlog,
    ID_UserLookup,
};

class mainframe: public wxFrame
{
	std::map<int, unsigned int> proflookupidmap;
	wxMenu *tpmenu;
	wxMenu *lookupmenu;

public:
	tpanelnotebook *auib;
	tweetpostwin *tpw;
	tpanelmenudata tpm;
	wxAuiManager *auim;
	wxPoint nominal_pos;
	wxSize nominal_size;

	mainframe(const wxString& title, const wxPoint& pos, const wxSize& size, bool maximise = false);
	~mainframe();
	void OnCloseWindow(wxCommandEvent &event);
	void OnQuit(wxCommandEvent &event);
	void OnAbout(wxCommandEvent &event);
	void OnSettings(wxCommandEvent &event);
	void OnAccounts(wxCommandEvent &event);
	void OnViewlog(wxCommandEvent &event);
	void OnClose(wxCloseEvent &event);
	void OnMouseWheel(wxMouseEvent &event);
	void OnMenuOpen(wxMenuEvent &event);
	void OnTPanelMenuCmd(wxCommandEvent &event);
	void OnLookupUser(wxCommandEvent &event);
	void OnSize(wxSizeEvent &event);
	void OnMove(wxMoveEvent &event);
	void OnOwnProfileMenuCmd(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

struct tweetposttextbox : public wxRichTextCtrl {
	tweetpostwin *parent;
	int lastheight;

	tweetposttextbox(tweetpostwin *parent_, const wxString &deftext, wxWindowID id);
	~tweetposttextbox();
	void OnTCChar(wxRichTextEvent &event);
	void OnTCUpdate(wxCommandEvent &event);
	void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
			int noUnitsX, int noUnitsY,
			int xPos = 0, int yPos = 0,
			bool noRefresh = false);    //virtual override
	void SetCursorToEnd();

	DECLARE_EVENT_TABLE()
};

enum {
	TPWID_TEXTCTRL = 1,
	TPWIN_SENDBTN,
	TPWID_CLOSEREPDESC,
};

struct tweetpostwin : public wxPanel {
	tweetposttextbox *textctrl;
	wxWindow *parentwin;
	mainframe *mparentwin;
	acc_choice *accc;
	std::shared_ptr<taccount> curacc;
	wxButton *sendbtn;
	wxAuiManager *pauim;
	wxStaticText *infost;
	bool isgoodacc;
	bool isshown;
	wxBoxSizer *vbox;
	wxBoxSizer *hbox;
	bool resize_update_pending;
	bool currently_posting;
	bool tc_has_focus;
	unsigned int current_length;
	bool length_oob;
	wxColour infost_colout;
	wxStaticText *replydesc;
	wxBitmapButton *replydesclosebtn;
	std::shared_ptr<tweet> tweet_reply_targ;
	std::shared_ptr<userdatacontainer> dm_targ;

	tweetpostwin(wxWindow *parent, mainframe *mparent, wxAuiManager *parentauim = 0);
	~tweetpostwin();
	void OnSendBtn(wxCommandEvent &event);
	void DoShowHide(bool show);
	void UpdateAccount();
	void CheckEnableSendBtn();
	void OnTCChange();
	void resizemsghandler(wxCommandEvent &event);
	void OnTCFocus(wxFocusEvent &event);
	void OnTCUnFocus(wxFocusEvent &event);
	void NotifyPostResult(bool success);
	void UpdateReplyDesc();
	void SetReplyTarget(const std::shared_ptr<tweet> &targ);
	void SetDMTarget(const std::shared_ptr<userdatacontainer> &targ);
	void DoCheckFocusDisplay(bool force = false);
	void OnCloseReplyDescBtn(wxCommandEvent &event);
	void AUIMNoLongerValid();

	DECLARE_EVENT_TABLE()
};

mainframe *GetMainframeAncestor(wxWindow *in, bool passtoplevels = false);
void FreezeAll();
void ThawAll();
void AccountUpdateAllMainframes();

extern std::vector<mainframe*> mainframelist;

#endif
