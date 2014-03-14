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

#include "../univdefs.h"
#include "filter-dlg.h"
#include "filter-ops.h"
#include "filter-vldtr.h"
#include "../log.h"
#include "../alldata.h"
#include "../db.h"
#include "../twit.h"
#include "../tpanel.h"
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <algorithm>
#include <iterator>

enum {
	ID_CHECKBOX_START      = 1000,
};

BEGIN_EVENT_TABLE(filter_dlg, wxDialog)
	EVT_COMMAND_RANGE(ID_CHECKBOX_START, ID_CHECKBOX_START + 999, wxEVT_COMMAND_CHECKBOX_CLICKED, filter_dlg::CheckBoxUpdate)
	EVT_BUTTON(wxID_OK, filter_dlg::OnOK)
END_EVENT_TABLE()

struct selection_category {
	std::function<void(const tweetidset &, selection_category &)> func;
	wxStaticText *stattext = 0;
	wxCheckBox *chk = 0;
	tweetidset output;
	const tweetidset *output_other = 0;

	const tweetidset &GetOutputSet() const {
		if(output_other) return *output_other;
		else return output;
	}
};

struct filter_dlg_gui {
	wxBoxSizer *vbox = 0;
	wxBoxSizer *hbox = 0;
	wxStaticText *total = 0;
	tweetidset selectedset;
	std::shared_ptr<filter_set> apply_filter;
	wxString apply_filter_txt;
	wxButton *filterbtn = 0;
};

filter_dlg::filter_dlg(wxWindow* parent, wxWindowID id, std::function<const tweetidset *()> getidset_, const wxPoint& pos, const wxSize& size)
		: wxDialog(parent, id, wxT("Apply Filter to Tweets"), pos, size, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), getidset(getidset_) {
	fdg.reset(new filter_dlg_gui);
	fdg->apply_filter = std::make_shared<filter_set>();
	fdg->hbox = new wxBoxSizer(wxHORIZONTAL);
	fdg->vbox = new wxBoxSizer(wxVERTICAL);

	fdg->hbox->Add(fdg->vbox, 1, wxALL | wxEXPAND, 0);

	auto addblock = [&](wxString name) -> wxFlexGridSizer * {
		wxStaticBoxSizer *hbox1 = new wxStaticBoxSizer(wxVERTICAL, this, name);
		wxFlexGridSizer *fgsr = new wxFlexGridSizer(2, 2, 5);
		fgsr->SetFlexibleDirection(wxBOTH);
		fgsr->AddGrowableCol(2, 1);

		fdg->vbox->Add(hbox1, 0, wxALL | wxEXPAND | wxALIGN_TOP, 4);
		hbox1->Add(fgsr, 0, wxALL | wxEXPAND | wxALIGN_TOP, 4);

		return fgsr;
	};

	wxFlexGridSizer *cb = addblock(wxT("Select items to filter"));
	int nextcheckboxid = ID_CHECKBOX_START;
	auto addcheckbox = [&](wxFlexGridSizer *fgs, const wxString &name, std::function<void(const tweetidset &, selection_category &)> func) -> wxCheckBox * {
		wxCheckBox *chk = new wxCheckBox(this, nextcheckboxid, name);
		fgs->Add(chk, 0, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);

		wxSize chksz = chk->GetSize();
		fgs->SetItemMinSize(chk, std::max(200, chksz.GetWidth()), chksz.GetHeight());

		wxStaticText *stattxt = new wxStaticText(this, wxID_ANY, wxT("0"));
		fgs->Add(stattxt, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);

		selection_category &sc = checkboxmap[nextcheckboxid];
		sc.func = std::move(func);
		sc.stattext = stattxt;
		sc.chk = chk;

		nextcheckboxid++;
		return chk;
	};
	auto addcheckbox_intersection = [&](wxFlexGridSizer *fgs, const wxString &name, const tweetidset &intersect_with) -> wxCheckBox * {
		return addcheckbox(fgs, name, [this, &intersect_with](const tweetidset &in, selection_category &sc) {
			std::set_intersection(in.begin(), in.end(), intersect_with.begin(), intersect_with.end(), std::inserter(sc.output, sc.output.end()), in.key_comp());
		});
	};
	addcheckbox(cb, wxT("All"), [this](const tweetidset &in, selection_category &sc) {
		sc.output_other = &in;
	});
	addcheckbox_intersection(cb, wxT("Unread"), ad.cids.unreadids);
	addcheckbox_intersection(cb, wxT("Highlighted"), ad.cids.highlightids);

	wxStaticText *total_label = new wxStaticText(this, wxID_ANY, wxT("Total"));
	cb->Add(total_label, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	fdg->total = new wxStaticText(this, wxID_ANY, wxT("Total selected"));
	cb->Add(fdg->total, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);

	wxStaticBoxSizer *filtersb = new wxStaticBoxSizer(wxVERTICAL, this, wxT("Filter"));
	fdg->vbox->Add(filtersb, 0, wxALL | wxEXPAND | wxALIGN_TOP, 4);

	FilterTextValidator filterval(*(fdg->apply_filter), &fdg->apply_filter_txt);
	wxTextCtrl *filtertc = new wxTextCtrl(this, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE, filterval);
	filtersb->Add(filtertc, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);

	wxBoxSizer *hboxfooter = new wxBoxSizer(wxHORIZONTAL);
	fdg->vbox->Add(hboxfooter, 0, wxALL | wxALIGN_RIGHT | wxALIGN_BOTTOM, 4);
	fdg->filterbtn = new wxButton(this, wxID_OK, wxT(""));
	wxButton *cancelbtn = new wxButton(this, wxID_CANCEL, wxT("Cancel"));
	hboxfooter->Add(fdg->filterbtn, 0, wxALL | wxALIGN_BOTTOM | wxALIGN_RIGHT, 2);
	hboxfooter->Add(cancelbtn, 0, wxALL | wxALIGN_BOTTOM | wxALIGN_RIGHT, 2);

	ReCalculateCategories();

	SetSizer(fdg->hbox);
	fdg->hbox->Fit(this);

	wxSize initsize = GetSize();
	SetSizeHints(initsize.GetWidth(), initsize.GetHeight(), 9000, initsize.GetHeight());
}

filter_dlg::~filter_dlg() { }

void filter_dlg::CheckBoxUpdate(wxCommandEvent &event) {
	RefreshSelection();
}

void filter_dlg::RefreshSelection() {
	fdg->selectedset.clear();
	for(auto &it : checkboxmap) {
		selection_category &sc = it.second;
		if(sc.chk->IsChecked()) {
			const tweetidset &output = sc.GetOutputSet();
			fdg->selectedset.insert(output.begin(), output.end());
		}
	}
	fdg->total->SetLabel(wxString::Format(wxT("%d"), fdg->selectedset.size()));
	fdg->filterbtn->SetLabel(wxString::Format(wxT("Filter %d"), fdg->selectedset.size()));
	fdg->filterbtn->Enable(!fdg->selectedset.empty());
}

void filter_dlg::ReCalculateCategories() {
	const tweetidset *current = getidset();
	for(auto &it : checkboxmap) {
		selection_category &sc = it.second;
		sc.func(*current, it.second);
		sc.stattext->SetLabel(wxString::Format(wxT("%d"), sc.GetOutputSet().size()));
	}
	RefreshSelection();
}

void filter_dlg::OnOK(wxCommandEvent &event) {
	if(Validate() && TransferDataFromWindow()) {
		ExecFilter();
		if(IsModal()) {
			EndModal(wxID_OK);
		}
		else {
			SetReturnCode(wxID_OK);
			Show(false);
		}
	}
}

void FilterOneTweet(filter_set &fs, tweet_ptr_p t) {
	fs.FilterTweet(*t, 0);
	t->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);
}

struct applyfilter_pending_op : public pending_op {
	std::shared_ptr<filter_set> apply_filter;

	applyfilter_pending_op(std::shared_ptr<filter_set> apply_filter_)
			: apply_filter(std::move(apply_filter_)) { }

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) override {
		FilterOneTweet(*apply_filter, t);
	}

	virtual wxString dump() override {
		return wxT("Apply filter to tweet");
	}
};

void filter_dlg::ExecFilter() {
	dbseltweetmsg *loadmsg = 0;

	SetNoUpdateFlag_All();

	for(auto id : fdg->selectedset) {
		tweet_ptr tobj = ad.GetTweetById(id);
		if(CheckFetchPendingSingleTweet(tobj, std::shared_ptr<taccount>(), &loadmsg)) {
			FilterOneTweet(*(fdg->apply_filter), tobj);
		}
		else {
			tobj->lflags |= TLF::ISPENDING;
			tobj->AddNewPendingOp(new applyfilter_pending_op(fdg->apply_filter));
		}
	}
	if(loadmsg) {
		if(!DBC_AllMediaEntitiesLoaded()) loadmsg->flags |= DBSTMF::PULLMEDIA;
		loadmsg->flags |= DBSTMF::CLEARNOUPDF;
		DBC_PrepareStdTweetLoadMsg(loadmsg);
		DBC_SendMessage(loadmsg);
	}
	else CheckClearNoUpdateFlag_All();
}
