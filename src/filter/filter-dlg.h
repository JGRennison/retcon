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

#ifndef HGUARD_SRC_FILTER_FILTER_DLG
#define HGUARD_SRC_FILTER_FILTER_DLG

#include "../univdefs.h"
#include "../twit-common.h"
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <memory>
#include <map>

struct selection_category;
struct filter_dlg_gui;

class filter_dlg : public wxDialog {
	std::unique_ptr<filter_dlg_gui> fdg;
	std::function<const tweetidset *()> getidset;
	std::map<int, selection_category> checkboxmap;

	public:
	filter_dlg(wxWindow *parent, wxWindowID id, std::function<const tweetidset *()> getidset_, const wxPoint &pos = wxDefaultPosition,
			const wxSize &size = wxDefaultSize);
	~filter_dlg();
	void CheckBoxUpdate(wxCommandEvent &event);
	void RefreshSelection();
	void ReCalculateCategories();
	void OnOK(wxCommandEvent &event);
	void ExecFilter();

	DECLARE_EVENT_TABLE()
};

#endif
