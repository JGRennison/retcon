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

#ifndef HGUARD_SRC_OPTUI
#define HGUARD_SRC_OPTUI

#include "univdefs.h"
#include <wx/tglbtn.h>
#include <wx/listbox.h>
#include <wx/dialog.h>
#include <wx/string.h>
#include <wx/button.h>
#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/window.h>
#include <wx/checkbox.h>
#include <wx/validate.h>
#include <wx/valtext.h>
#include <set>
#include <map>
#include <forward_list>
#include <functional>
#include <utility>
#include <vector>

struct taccount;
struct genopt;
struct genoptconf;

struct acc_window: public wxDialog {
	acc_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name = wxT("dialogBox"));
	~acc_window();
	void AccEdit(wxCommandEvent &event);
	void AccDel(wxCommandEvent &event);
	void AccNew(wxCommandEvent &event);
	void AccClose(wxCommandEvent &event);
	void EnDisable(wxCommandEvent &event);
	void ReAuth(wxCommandEvent &event);
	void OnSelChange(wxCommandEvent &event) ;
	void UpdateLB();
	void UpdateButtons() ;
	wxListBox *lb;
	wxButton *editbtn;
	wxButton *endisbtn;
	wxButton *reauthbtn;
	wxButton *delbtn;

	static std::set<acc_window *> currentset;

	DECLARE_EVENT_TABLE()
};

struct settings_window : public wxDialog {
	wxChoice *lb;
	taccount *current;
	std::map<taccount *, wxStaticBoxSizer *> accmap;
	wxBoxSizer *vbox;
	wxBoxSizer *hbox;
	wxSize initsize;
	unsigned int currentcat = 0;
	wxBoxSizer *btnbox;

	struct option_item {
		wxSizer *sizer;
		wxWindow *win;
		unsigned int cat;
		unsigned int flags;
	};

	std::forward_list<option_item> opts;
	wxCheckBox *advoptchkbox;
	wxCheckBox *veryadvoptchkbox;
	std::vector<wxToggleButton *> cat_buttons;
	std::vector<std::pair<wxSizer *, std::function<void (bool)> > > cat_empty_sizer_op;

	settings_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name = wxT("dialogBox"), taccount *defshow=0);
	~settings_window();
	bool TransferDataFromWindow();
	void ChoiceCtrlChange(wxCommandEvent &event);
	void ShowAdvCtrlChange(wxCommandEvent &event);
	void ShowVeryAdvCtrlChange(wxCommandEvent &event);
	void AddSettingRow_String(unsigned int win, wxWindow* parent, wxSizer *sizer, const wxString &name, unsigned int flags, genopt &val, genopt &parentval, long style=wxFILTER_NONE, wxValidator *textctrlvalidator=0);
	void AddSettingRow_Bool(unsigned int win, wxWindow* parent, wxSizer *sizer, const wxString &name, unsigned int flags, genopt &val, genopt &parentval);
	wxStaticBoxSizer *AddGenoptconfSettingBlock(wxWindow* parent, wxSizer *sizer, const wxString &name, genoptconf &goc, genoptconf &parentgoc, unsigned int flags);
	void OptShowHide(unsigned int setmask);
	void PostOptShowHide();
	void CategoryButtonClick(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};

#endif
