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

#ifndef HGUARD_SRC_FILTER_FILTER_VLDTR
#define HGUARD_SRC_FILTER_FILTER_VLDTR

#include "../univdefs.h"
#include "../util.h"
#include "filter-ops.h"
#include <wx/msgdlg.h>
#include <wx/valtext.h>

struct FilterTextValidator : public wxTextValidator {
	filter_set &fs;
	wxString *valPtr;
	std::shared_ptr<filter_set> ownfilter;
	FilterTextValidator(filter_set &fs_, wxString *valPtr_ = nullptr)
			: wxTextValidator((long) wxFILTER_NONE, valPtr_), fs(fs_), valPtr(valPtr_) {
	}
	virtual wxObject *Clone() const {
		FilterTextValidator *newfv = new FilterTextValidator(fs, valPtr);
		newfv->ownfilter = ownfilter;
		return newfv;
	}
	virtual bool TransferFromWindow() {
		bool result = wxTextValidator::TransferFromWindow();
		if(result && ownfilter) {
			fs = std::move(*ownfilter);
		}
		return result;
	}
	virtual bool Validate(wxWindow *parent) {
		wxTextCtrl *win = (wxTextCtrl *) GetWindow();

		if(!ownfilter) ownfilter = std::make_shared<filter_set>();
		std::string errmsg;
		ParseFilter(stdstrwx(win->GetValue()), *ownfilter, errmsg);
		if(errmsg.empty()) {
			return true;
		}
		else {
			::wxMessageBox(wxT("Filter is not valid, please correct errors.\n") + wxstrstd(errmsg),
					wxT("Filter Validation Failed"), wxOK | wxICON_EXCLAMATION, parent);
			return false;
		}
	}
};

#endif
