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
#include "filter-intl.h"
#include "../log.h"
#include "../alldata.h"
#include "../db.h"
#include "../twit.h"
#include "../tpanel.h"
#include "../retcon.h"
#include "../magic_ptr.h"
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <algorithm>
#include <iterator>

enum {
	ID_CHECKBOX_START      = 1000,
	ID_LIMIT_TXT           = 2000,
};

BEGIN_EVENT_TABLE(filter_dlg, wxDialog)
	EVT_COMMAND_RANGE(ID_CHECKBOX_START, ID_CHECKBOX_START + 999, wxEVT_COMMAND_CHECKBOX_CLICKED, filter_dlg::CheckBoxUpdate)
	EVT_BUTTON(wxID_OK, filter_dlg::OnOK)
	EVT_TEXT(ID_LIMIT_TXT, filter_dlg::OnLimitCountUpdate)
END_EVENT_TABLE()

struct selection_category {
	std::function<void(const tweetidset &, selection_category &)> func;
	wxStaticText *stattext = nullptr;
	wxCheckBox *chk = nullptr;
	tweetidset output;
	const tweetidset *output_other = nullptr;

	const tweetidset &GetOutputSet() const {
		if(output_other) return *output_other;
		else return output;
	}
};

struct filter_dlg_shared_state {
	filter_set apply_filter;
	std::string srcname;
	std::unique_ptr<undo::action> db_undo_action;

	// This is to handle the case where the filter_dlg_shared_state is destructed after the app,
	// This can happen if the last shared_ptr was held by an object destructed at exit (e.g. a tweet with a pending filter op).
	magic_ptr_ts<retcon> app;

	filter_dlg_shared_state() {
		app = &(wxGetApp());
	}

	~filter_dlg_shared_state() {
		if(!app)
			return;

		observer_ptr<undo::item> undo_item = app->undo_state.NewItem(string_format("apply filter: %s", cstr(srcname)));
		undo_item->AppendAction(apply_filter.GetUndoAction());
		undo_item->AppendAction(std::move(db_undo_action));
	}
};

struct filter_dlg_gui {
	wxBoxSizer *vbox = nullptr;
	wxBoxSizer *hbox = nullptr;
	wxStaticText *total = nullptr;
	tweetidset selectedset;
	std::shared_ptr<filter_dlg_shared_state> shared_state;
	wxString apply_filter_txt;
	wxButton *filterbtn = nullptr;
	wxCheckBox *limit_count_chk = nullptr;
	wxTextCtrl *limit_count_txt = nullptr;
};

filter_dlg::filter_dlg(wxWindow *parent, wxWindowID id, std::function<const tweetidset *()> getidset_, std::string srcname_, const wxPoint &pos, const wxSize &size)
		: wxDialog(parent, id, wxT("Apply Filter"), pos, size, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER), getidset(getidset_), srcname(std::move(srcname_)) {
	fdg.reset(new filter_dlg_gui);
	fdg->shared_state = std::make_shared<filter_dlg_shared_state>();
	fdg->shared_state->srcname = srcname;
	fdg->hbox = new wxBoxSizer(wxHORIZONTAL);
	fdg->vbox = new wxBoxSizer(wxVERTICAL);

	fdg->hbox->Add(fdg->vbox, 1, wxALL | wxEXPAND, 0);

	if(!srcname.empty()) {
		wxStaticText *srcname_label = new wxStaticText(this, wxID_ANY, wxstrstd(srcname));
		wxBoxSizer *hs = new wxBoxSizer(wxHORIZONTAL);
		hs->AddStretchSpacer();
		hs->Add(srcname_label, 0, wxEXPAND | wxALIGN_CENTRE_VERTICAL | wxALIGN_CENTRE, 0);
		hs->AddStretchSpacer();
		fdg->vbox->Add(hs, 0, wxEXPAND | wxALIGN_CENTRE_VERTICAL | wxALL, 4);
	}

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

	{
		wxFlexGridSizer *lfgs = addblock(wxT("Limit"));
		fdg->limit_count_chk = new wxCheckBox(this, nextcheckboxid, wxT("First N"));
		lfgs->Add(fdg->limit_count_chk, 0, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);

		wxSize chksz = fdg->limit_count_chk->GetSize();
		lfgs->SetItemMinSize(fdg->limit_count_chk, std::max(200, chksz.GetWidth()), chksz.GetHeight());

		fdg->limit_count_txt = new wxTextCtrl(this, ID_LIMIT_TXT, wxT("0"), wxDefaultPosition, wxDefaultSize, 0, wxTextValidator(wxFILTER_NUMERIC));
		lfgs->Add(fdg->limit_count_txt, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	}

	wxStaticBoxSizer *filtersb = new wxStaticBoxSizer(wxVERTICAL, this, wxT("Filter"));
	fdg->vbox->Add(filtersb, 0, wxALL | wxEXPAND | wxALIGN_TOP, 4);

	FilterTextValidator filterval(fdg->shared_state->apply_filter, &fdg->apply_filter_txt);
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
	RefreshCounts();
}

size_t filter_dlg::GetCount() {
	size_t count = fdg->selectedset.size();
	bool limit_count_set = fdg->limit_count_chk->GetValue();
	fdg->limit_count_txt->Enable(limit_count_set);
	if(limit_count_set) {
		// Limit enabled
		size_t limit = 0;
		if(ownstrtonum(limit, cstr(fdg->limit_count_txt->GetValue()), -1)) {
			if(count > limit)
				count = limit;
		}
	}
	return count;
}

void filter_dlg::RefreshCounts() {
	size_t count = GetCount();
	fdg->total->SetLabel(wxString::Format(wxT("%d"), count));
	fdg->filterbtn->SetLabel(wxString::Format(wxT("Filter %d"), count));
	fdg->filterbtn->Enable(count);
}

void filter_dlg::OnLimitCountUpdate(wxCommandEvent &event) {
	RefreshCounts();
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

void filter_dlg::ApplyLimit() {
	size_t count = GetCount();
	if(count < fdg->selectedset.size()) {
		tweetidset orig = std::move(fdg->selectedset);
		std::copy_n(orig.begin(), count, std::inserter(fdg->selectedset, fdg->selectedset.end()));
	}
}

void filter_dlg::OnOK(wxCommandEvent &event) {
	if(Validate() && TransferDataFromWindow()) {
		ApplyLimit();
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
	std::shared_ptr<filter_dlg_shared_state> shared_state;

	applyfilter_pending_op(std::shared_ptr<filter_dlg_shared_state> shared_state_)
			: shared_state(std::move(shared_state_)) {
		preq = PENDING_REQ::USEREXPIRE;
		presult_required = PENDING_RESULT::CONTENT_READY;
	}

	virtual void MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) override {
		FilterOneTweet(shared_state->apply_filter, t);
	}

	virtual std::string dump() override {
		return "Apply filter to tweet";
	}
};

void filter_dlg::ExecFilter() {
	std::unique_ptr<dbseltweetmsg> loadmsg;

	SetNoUpdateFlag_All();

	fdg->shared_state->apply_filter.EnableUndo();

	tweetidset dbset;

	for(auto id : fdg->selectedset) {
		optional_tweet_ptr tobj = ad.GetExistingTweetById(id);
		if(!tobj) {
			if(ad.unloaded_db_tweet_ids.find(id) != ad.unloaded_db_tweet_ids.end()) {
				// This is in DB
				dbset.insert(id);
				continue;
			}

			// fallback, shouldn't happen too often
			tobj = ad.GetTweetById(id);
		}

		if(CheckFetchPendingSingleTweet(tobj, std::shared_ptr<taccount>(), &loadmsg, PENDING_REQ::USEREXPIRE, PENDING_RESULT::CONTENT_READY)) {
			FilterOneTweet(fdg->shared_state->apply_filter, tobj);
		}
		else {
			tobj->AddNewPendingOp(new applyfilter_pending_op(fdg->shared_state));
		}
	}
	if(loadmsg) {
		loadmsg->flags |= DBSTMF::CLEARNOUPDF;
		DBC_PrepareStdTweetLoadMsg(*loadmsg);
		DBC_SendMessage(std::move(loadmsg));
	}
	else CheckClearNoUpdateFlag_All();

	if(!dbset.empty()) {
		filter_set fs;
		LoadFilter(fdg->shared_state->apply_filter.filter_text, fs);

		// Do not send shared_state to the completion callback/DB thread, as this would
		// cause major problems if for whatever reason a reply was not sent and the message and
		// thus callback was destructed in the DB thread, in the case where it held the last reference
		// to the shared_state. The shared_state destructor may not be run in the DB thread.

		static std::map<uint64_t, std::shared_ptr<filter_dlg_shared_state>> pending_db_filters;
		static uint64_t next_id;
		uint64_t this_id = next_id++;
		pending_db_filters[this_id] = fdg->shared_state;

		filter_set::DBFilterTweetIDs(std::move(fs), std::move(dbset), true, [this_id](std::unique_ptr<undo::action> undo) {
			auto it = pending_db_filters.find(this_id);
			if(it != pending_db_filters.end()) {
				it->second->db_undo_action = std::move(undo);
				pending_db_filters.erase(it);
				LogMsgFormat(LOGT::FILTERTRACE, "filter_dlg::ExecFilter: DB filter complete, %zu pending", pending_db_filters.size());
			}
			else {
				LogMsgFormat(LOGT::FILTERERR, "filter_dlg::ExecFilter: DB filter completion: item missing from pending_db_filters!");
			}
		});
	}
}
