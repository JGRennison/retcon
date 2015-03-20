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
#include "primaryclipboard.h"
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
struct is_user_mentioned_cache;
struct tpanelglobal;

enum
{
    ID_Close      = wxID_CLOSE,
    ID_Quit       = wxID_EXIT,
    ID_About      = wxID_ABOUT,
    ID_Settings   = 1,
    ID_Accounts,
    ID_Viewlog,
    ID_UserLookup,
    ID_Undo,
};

class mainframe: public wxFrame
{
	std::map<int, unsigned int> proflookupidmap;
	wxMenu *tpmenu = nullptr;
	wxMenu *lookupmenu = nullptr;
	wxMenu *filemenu = nullptr;

public:
	tpanelnotebook *auib = nullptr;
	tweetpostwin *tpw = nullptr;
	tpanelmenudata tpm;
	wxAuiManager *auim = nullptr;
	wxPoint nominal_pos;
	wxSize nominal_size;
	wxString origtitle;

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
	void OnOwnProfileMenuCmd(wxCommandEvent &event);
	void OnTPanelMenuCmd(wxCommandEvent &event);
	void OnLookupUser(wxCommandEvent &event);
	void OnUndoCmd(wxCommandEvent &event);
	void OnSize(wxSizeEvent &event);
	void OnMove(wxMoveEvent &event);
	static wxString DecorateTitle(wxString basetitle);
	void ResetTitle();
	static void ResetAllTitles();
	static optional_observer_ptr<mainframe> GetLastMenuOpenedMainframe();

	DECLARE_EVENT_TABLE()
};

struct tweetposttextbox : public wxRichTextCtrl {
	tweetpostwin *parent = nullptr;
	int lastheight = 0;

	tweetposttextbox(tweetpostwin *parent_, const wxString &deftext, wxWindowID id);
	~tweetposttextbox();
	void OnTCChar(wxRichTextEvent &event);
	void OnTCUpdate(wxCommandEvent &event);
	void SetScrollbars(int pixelsPerUnitX, int pixelsPerUnitY,
			int noUnitsX, int noUnitsY,
			int xPos = 0, int yPos = 0,
			bool noRefresh = false);    //virtual override
	void SetCursorToEnd();

#if HANDLE_PRIMARY_CLIPBOARD
	void OnLeftUp(wxMouseEvent& event);
	void OnMiddleClick(wxMouseEvent& event);
#endif

	DECLARE_EVENT_TABLE()
};

enum {
	TPWID_TEXTCTRL = 1,
	TPWIN_SENDBTN,
	TPWID_CLOSEREPDESC,
	TPWID_CLEARTEXT,
	TPWID_TOGGLEREPDESCLOCK,
	TPWID_ADDNAMES,
	TPWID_ADDIMG,
	TPWID_DELIMG,
};

struct tweetpostwin : public wxPanel {
	tweetposttextbox *textctrl = nullptr;
	wxWindow *parentwin = nullptr;
	mainframe *mparentwin = nullptr;
	acc_choice *accc = nullptr;
	std::shared_ptr<taccount> curacc;
	wxButton *sendbtn = nullptr;
	wxAuiManager *pauim = nullptr;
	wxStaticText *infost = nullptr;
	bool isgoodacc = false;
	bool isshown = false;
	wxBoxSizer *vbox = nullptr;
	wxBoxSizer *hbox = nullptr;
	bool resize_update_pending = true;
	bool currently_posting = false;
	int tc_has_focus = 0;
	unsigned int current_length = 0;
	bool length_oob = false;
	wxColour infost_colout;
	wxStaticText *replydesc = nullptr;
	wxBitmapButton *replydesclosebtn = nullptr;
	wxBitmapButton *cleartextbtn = nullptr;
	wxBitmapButton *replydeslockbtn = nullptr;
	wxButton *addnamesbtn = nullptr;
	tweet_ptr tweet_reply_targ;
	udc_ptr dm_targ;
	std::unique_ptr<is_user_mentioned_cache> iumc;
	bool replydesc_locked = false;
	std::shared_ptr<tpanelglobal> tpg;

	tweetpostwin(wxWindow *parent, mainframe *mparent, wxAuiManager *parentauim = nullptr);
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
	void SetReplyTarget(tweet_ptr_p targ);
	void SetDMTarget(udc_ptr_p targ);
	void DoCheckFocusDisplay(bool force = false);
	void OnCloseReplyDescBtn(wxCommandEvent &event);
	void OnClearTextBtn(wxCommandEvent &event);
	void OnAddNamesBtn(wxCommandEvent &event);
	void OnToggleReplyDescLockBtn(wxCommandEvent &event);
	wxBitmap &GetReplyDescLockBtnBitmap();
	void CheckAddNamesBtn();
	void AUIMNoLongerValid();
	bool okToSend();

	std::vector<std::string> image_upload_filenames;
	wxBitmapButton *addimagebtn;
	wxBitmapButton *delimagebtn;
	wxStaticText *imagestattxt;
	void AddImageUploadFilename(std::string filename);
	void ClearImageUploadFilenames();
	void ShowHideImageUploadBtns(bool alwayshide = false, bool enabled = true);
	void OnAddImgBtn(wxCommandEvent &event);
	void OnDelImgBtn(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

mainframe *GetMainframeAncestor(wxWindow *in, bool passtoplevels = false);
void FreezeAll();
void ThawAll();
void AccountUpdateAllMainframes();

extern std::vector<mainframe*> mainframelist;

#endif
