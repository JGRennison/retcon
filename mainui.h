//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
//
//  NOTE: This software is licensed under the GPL. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  Jonathan Rennison (or anybody else) is in no way responsible, or liable
//  for this program or its use in relation to users, 3rd parties or to any
//  persons in any way whatsoever.
//
//  You  should have  received a  copy of  the GNU  General Public
//  License along  with this program; if  not, write to  the Free Software
//  Foundation, Inc.,  59 Temple Place,  Suite 330, Boston,  MA 02111-1307
//  USA
//
//  2012 - j.g.rennison@gmail.com
//==========================================================================

struct tweetpostwin;

enum
{
    ID_Quit = wxID_EXIT,
    ID_About = wxID_ABOUT,
    ID_Settings = 1,
    ID_Accounts,
    ID_Viewlog,
    ID_UserLookup,
};

class mainframe: public wxFrame
{
public:
	tpanelnotebook *auib;
	tweetpostwin *tpw;
	tpanelmenudata tpm;
	wxMenu *tpmenu;
	wxAuiManager *auim;

	mainframe(const wxString& title, const wxPoint& pos, const wxSize& size);
	~mainframe();
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
                               bool noRefresh = false );	//virtual override
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
	wxStaticText *statusst;
	bool isgoodacc;
	bool isshown;
	wxBoxSizer *vbox;
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

	tweetpostwin(wxWindow *parent, mainframe *mparent, wxAuiManager *parentauim=0);
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
	void DoCheckFocusDisplay(bool force=false);
	void OnCloseReplyDescBtn(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

mainframe *GetMainframeAncestor(wxWindow *in, bool passtoplevels=false);
void FreezeAll();
void ThawAll();
void AccountUpdateAllMainframes();
