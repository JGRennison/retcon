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

#include "univdefs.h"
#include "optui.h"
#include "taccount.h"
#include "db.h"
#include "cfg.h"
#include "twit.h"
#include "twitcurlext.h"
#include "alldata.h"
#include "tpanel.h"
#include "filter/filter-ops.h"
#include "filter/filter-vldtr.h"
#include "util.h"
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <unordered_set>

enum {
	ACCWID_ENDISABLE = 1,
	ACCWID_REAUTH,
	ACCWID_REENABLEALL,
	ACCWID_LISTWIN,
};

BEGIN_EVENT_TABLE(acc_window, wxDialog)
	EVT_BUTTON(wxID_PROPERTIES, acc_window::AccEdit)
	EVT_BUTTON(wxID_DELETE, acc_window::AccDel)
	EVT_BUTTON(wxID_NEW, acc_window::AccNew)
	EVT_BUTTON(wxID_CLOSE, acc_window::AccClose)
	EVT_BUTTON(ACCWID_ENDISABLE, acc_window::EnDisable)
	EVT_BUTTON(ACCWID_REAUTH, acc_window::ReAuth)
	EVT_BUTTON(ACCWID_REENABLEALL, acc_window::ReEnableAll)
	EVT_LISTBOX(ACCWID_LISTWIN, acc_window::OnSelChange)
END_EVENT_TABLE()

std::set<acc_window *> acc_window::currentset;

acc_window::acc_window(wxWindow* parent, wxWindowID id, const wxString &title, const wxPoint &pos, const wxSize &size, long style, const wxString &name)
		: wxDialog(parent, id, title, pos, size, style, name) {

	wxWindow *panel = this;

	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer *hbox1 = new wxStaticBoxSizer(wxHORIZONTAL, panel, wxT("Accounts"));
	wxBoxSizer *hbox2 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer *vboxr = new wxBoxSizer(wxVERTICAL);

	lb = new wxListBox(panel, ACCWID_LISTWIN, wxDefaultPosition, wxDefaultSize, 0, 0, wxLB_SINGLE | wxLB_SORT | wxLB_NEEDED_SB);
	UpdateLB();
	editbtn = new wxButton(panel, wxID_PROPERTIES, wxT("Settings"));
	endisbtn = new wxButton(panel, ACCWID_ENDISABLE, wxT("Disable"));
	reauthbtn = new wxButton(panel, ACCWID_REAUTH, wxT("Re-Authenticate"));
	delbtn = new wxButton(panel, wxID_DELETE, wxT("Delete"));
	wxButton *newbtn = new wxButton(panel, wxID_NEW, wxT("Add account"));
	wxButton *clsbtn = new wxButton(panel, wxID_CLOSE, wxT("Close"));

	vbox->Add(hbox1, 1, wxALL | wxEXPAND , 4);
	vbox->Add(hbox2, 0, wxALL | wxEXPAND , 4);
	hbox1->Add(lb, 1, wxALL | wxALIGN_LEFT | wxEXPAND, 4);
	hbox1->Add(vboxr, 0, wxALL, 4);
	vboxr->Add(editbtn, 0, wxALIGN_TOP | wxEXPAND, 0);
	vboxr->Add(delbtn, 0, wxALIGN_TOP | wxEXPAND, 0);
	vboxr->Add(endisbtn, 0, wxALIGN_TOP | wxEXPAND, 0);
	vboxr->Add(reauthbtn, 0, wxALIGN_TOP | wxEXPAND, 0);
	hbox2->Add(newbtn, 0, wxALIGN_LEFT, 0);

	if (gc.allaccsdisabled) {
		reenableallbtns = new wxButton(panel, ACCWID_REENABLEALL, wxT("*Enable All*"));
		hbox2->Add(reenableallbtns, 0, wxALIGN_LEFT, 0);
	}

	hbox2->AddStretchSpacer(1);
	hbox2->Add(clsbtn, 0, wxALIGN_RIGHT, 0);

	UpdateButtons();

	panel->SetSizer(vbox);
	vbox->Fit(panel);

	wxSize initsize=GetSize();
	SetSizeHints(initsize.GetWidth(), initsize.GetHeight(), 9001, 9001);

	currentset.insert(this);
}

acc_window::~acc_window() {
	currentset.erase(this);
	AccountChangeTrigger();
}

void acc_window::OnSelChange(wxCommandEvent &event) {
	UpdateButtons();
}

void acc_window::UpdateButtons() {
	int selection = lb->GetSelection();
	editbtn->Enable(selection!=wxNOT_FOUND);
	endisbtn->Enable(selection!=wxNOT_FOUND);
	reauthbtn->Enable(selection!=wxNOT_FOUND);
	delbtn->Enable(selection!=wxNOT_FOUND);
	if (selection != wxNOT_FOUND) {
		taccount *acc = static_cast<taccount *>(lb->GetClientData(selection));
		endisbtn->SetLabel(acc->userenabled ? wxT("Disable") : wxT("Enable"));
	}
	else endisbtn->SetLabel(wxT("Disable"));
}

void acc_window::UpdateLB() {
	int selection = lb->GetSelection();
	taccount *acc = nullptr;
	if (selection != wxNOT_FOUND) {
		acc = static_cast<taccount *>(lb->GetClientData(selection));
	}
	lb->Clear();
	for (auto &it : alist) {
		wxString accname = it->dispname + wxT(" [") + it->GetStatusString(false) + wxT("]");;
		int index = lb->Append(accname, it.get());
		if (it.get() == acc) lb->SetSelection(index);
	}
}

void acc_window::AccEdit(wxCommandEvent &event) {
	int sel = lb->GetSelection();
	if (sel == wxNOT_FOUND) return;
	taccount *acc = static_cast<taccount *>(lb->GetClientData(sel));
	settings_window *sw = new settings_window(this, -1, wxT("Settings"), wxDefaultPosition, wxDefaultSize,
			wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER, wxT("dialogBox"), acc);
	sw->ShowModal();
	sw->Destroy();
}

void acc_window::AccDel(wxCommandEvent &event) {
	int sel = lb->GetSelection();
	if (sel == wxNOT_FOUND) return;
	taccount *acc = static_cast<taccount *>(lb->GetClientData(sel));
	if (!acc->dbindex) return;
	int answer = wxMessageBox(wxT("Are you sure that you want to delete account: ") + acc->dispname + wxT(".\nThis cannot be undone."),
			wxT("Confirm Account Deletion"), wxYES_NO | wxICON_EXCLAMATION, this);
	if (answer == wxYES) {
		acc->enabled=acc->userenabled=0;
		acc->Exec();
		std::unique_ptr<dbdelaccmsg> delmsg(new dbdelaccmsg);
		delmsg->dbindex = acc->dbindex;
		DBC_SendMessage(std::move(delmsg));
		alist.remove_if ([&](const std::shared_ptr<taccount> &a) { return a.get() == acc; });
		lb->SetSelection(wxNOT_FOUND);
		UpdateLB();
	}
}

void acc_window::AccNew(wxCommandEvent &event) {
	std::shared_ptr<taccount> ta(new taccount(&gc.cfg));
	ta->enabled = false;
	ta->dispname = wxT("<new account>");

	int answer = wxNO;
	if (gc.askuseraccsettingsonnewacc) {
		answer = wxMessageBox(wxT("Would you like to review the account settings before authenticating?"), wxT("Account Creation"),
				wxYES_NO | wxCANCEL | wxICON_QUESTION | wxNO_DEFAULT, this);

		if (answer == wxYES) {
			settings_window *sw=new settings_window(this, -1, wxT("New Account Settings"), wxDefaultPosition, wxDefaultSize,
					wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER, wxT("dialogBox"), ta.get());
			sw->ShowModal();
			sw->Destroy();
		} else if (answer == wxCANCEL) {
			return;
		}
	}

	std::unique_ptr<twitcurlext_accverify> twit = twitcurlext_accverify::make_new(ta);
	if (ta->TwDoOAuth(this, *twit)) {
		if (twit->TwSyncStartupAccVerify()) {
			ta->userenabled = true;
			ta->beinginsertedintodb = true;
			ta->name = wxString::Format(wxT("%" wxLongLongFmtSpec "d-%d"), ta->usercont->id, time(nullptr));
			alist.push_back(ta);
			UpdateLB();
			std::unique_ptr<dbinsertaccmsg> insmsg(new dbinsertaccmsg);
			insmsg->name = ta->name.ToUTF8();
			insmsg->dispname = ta->dispname.ToUTF8();
			insmsg->userid = ta->usercont->id;
			DBC_SendAccDBUpdate(std::move(insmsg));
		}
	}
}
void acc_window::AccClose(wxCommandEvent &event) {
	currentset.erase(this);
	EndModal(0);
}

void acc_window::EnDisable(wxCommandEvent &event) {
	int sel = lb->GetSelection();
	if (sel == wxNOT_FOUND) return;
	taccount *acc = static_cast<taccount *>(lb->GetClientData(sel));
	acc->userenabled=!acc->userenabled;
	acc->CalcEnabled();
	acc->Exec();
	UpdateLB();
	UpdateButtons();
}

void acc_window::ReAuth(wxCommandEvent &event) {
	int sel = lb->GetSelection();
	if (sel == wxNOT_FOUND) return;
	taccount *acc = static_cast<taccount *>(lb->GetClientData(sel));
	acc->enabled = 0;
	acc->Exec();
	std::unique_ptr<twitcurlext_accverify> twit = twitcurlext_accverify::make_new(acc->shared_from_this());
	twit->getOAuth().setOAuthTokenKey("");		//remove existing oauth tokens
	twit->getOAuth().setOAuthTokenSecret("");
	if (acc->TwDoOAuth(this, *twit)) {
		twit->TwSyncStartupAccVerify();
	}
	UpdateLB();
	UpdateButtons();
	acc->CalcEnabled();
	acc->Exec();
}

void acc_window::ReEnableAll(wxCommandEvent &event) {
	gc.allaccsdisabled = false;
	for (auto &it : alist) {
		it->CalcEnabled();
		it->Exec();
	}
	mainframe::ResetAllTitles();
	if (reenableallbtns) {
		// Hide the button, it's single-use not a toggle
		reenableallbtns->Show(false);
	}
}

struct DefaultChkBoxValidatorCommon : public wxValidator {
	genopt &val;
	genopt &parentval;

	DefaultChkBoxValidatorCommon(genopt &val_, genopt &parentval_)
			: val(val_), parentval(parentval_) { }

	virtual bool TransferFromWindow() override {
		wxCheckBox *chk = (wxCheckBox*) GetWindow();
		val.enable = chk->GetValue();
		return true;
	}

	virtual bool TransferToWindow() override {
		wxCheckBox *chk = (wxCheckBox*) GetWindow();
		chk->SetValue(val.enable);
		statechange();
		return true;
	}

	virtual bool Validate(wxWindow* parent) override {
		statechange();
		return true;
	}

	void checkboxchange(wxCommandEvent &event) {
		statechange();
	}

	virtual void statechange() = 0;

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(DefaultChkBoxValidatorCommon, wxValidator)
EVT_CHECKBOX(wxID_ANY, DefaultChkBoxValidatorCommon::checkboxchange)
END_EVENT_TABLE()

struct DefaultChkBoxValidator : public DefaultChkBoxValidatorCommon {
	wxTextCtrl *txtctrl;
	wxCheckBox *chkbox;
	flagwrapper<DBCV> flags;

	private:
	DefaultChkBoxValidator(genopt &val_, genopt &parentval_, flagwrapper<DBCV> flags_, wxTextCtrl *txtctrl_, wxCheckBox *chkbox_)
			: DefaultChkBoxValidatorCommon(val_, parentval_), txtctrl(txtctrl_), chkbox(chkbox_), flags(flags_) { }

	public:
	DefaultChkBoxValidator(genopt &val_, genopt &parentval_, flagwrapper<DBCV> flags_ = 0, wxTextCtrl *txtctrl_ = nullptr)
			: DefaultChkBoxValidatorCommon(val_, parentval_), txtctrl(txtctrl_), chkbox(nullptr), flags(flags_) { }
	DefaultChkBoxValidator(genopt &val_, genopt &parentval_, flagwrapper<DBCV> flags_ = 0, wxCheckBox *chkbox_ = nullptr)
			: DefaultChkBoxValidatorCommon(val_, parentval_), txtctrl(nullptr), chkbox(chkbox_), flags(flags_) { }


	virtual wxObject* Clone() const override { return new DefaultChkBoxValidator(val, parentval, flags, txtctrl, chkbox); }
	virtual void statechange() override {
		wxCheckBox *chk=(wxCheckBox*) GetWindow();
		if (txtctrl) {
			txtctrl->Enable(chk->GetValue());
			if (!chk->GetValue()) {
				if (flags & DBCV::HIDDENDEFAULT) {
					if (flags & DBCV::ISGLOBALCFG) {
						txtctrl->ChangeValue(wxT(""));
					} else if (parentval.enable) {
						txtctrl->ChangeValue(parentval.val);
					} else {
						txtctrl->ChangeValue(wxT(""));
					}
				} else {
					txtctrl->ChangeValue(parentval.val);
				}
			}
		} else if (chkbox) {
			chkbox->Enable(chk->GetValue());
			if (!chk->GetValue()) {
				chkbox->SetValue((parentval.val == wxT("1")));
			}
		}
	}
};

struct FormatChoiceDefaultChkBoxValidator : public DefaultChkBoxValidatorCommon {
	settings_window *sw;

	FormatChoiceDefaultChkBoxValidator(genopt &val_, genopt &parentval_, settings_window *sw_)
			: DefaultChkBoxValidatorCommon(val_, parentval_), sw(sw_) { }

	virtual wxObject* Clone() const override {
		return new FormatChoiceDefaultChkBoxValidator(val, parentval, sw);
	}

	virtual void statechange() override {
		wxCheckBox *chk = (wxCheckBox*) GetWindow();
		sw->formatdef_lb->Enable(chk->GetValue());
		if (!chk->GetValue()) {
			unsigned long value;
			parentval.val.ToULong(&value);
			sw->formatdef_lb->SetSelection(value);
		}
		if (sw->current_format_set_id != sw->formatdef_lb->GetSelection()) {
			sw->current_format_set_id = sw->formatdef_lb->GetSelection();
			sw->current_format_set = IndexToFormatSet(sw->current_format_set_id);
			sw->Validate(); // NB: partially recursive
		}
	}
};

struct GenericChoiceDefaultChkBoxValidator : public DefaultChkBoxValidatorCommon {
	wxChoice *choice;

	GenericChoiceDefaultChkBoxValidator(genopt &val_, genopt &parentval_, wxChoice *choice_) : DefaultChkBoxValidatorCommon(val_, parentval_), choice(choice_) { }
	virtual wxObject* Clone() const override { return new GenericChoiceDefaultChkBoxValidator(val, parentval, choice); }

	virtual void statechange() override {
		wxCheckBox *chk = (wxCheckBox*) GetWindow();
		choice->Enable(chk->GetValue());
		if (!chk->GetValue()) {
			unsigned long value;
			parentval.val.ToULong(&value);
			choice->SetSelection(value);
		}
	}
};

struct ValueChkBoxValidator : public wxValidator {
	genopt &val;

	ValueChkBoxValidator(genopt &val_)
			: wxValidator(), val(val_) { }

	virtual wxObject* Clone() const override {
		return new ValueChkBoxValidator(val);
	}

	virtual bool TransferFromWindow() override {
		wxCheckBox *chk = (wxCheckBox*) GetWindow();
		val.val = ((chk->GetValue()) ? wxT("1") : wxT("0"));
		return true;
	}

	virtual bool TransferToWindow() override {
		wxCheckBox *chk = (wxCheckBox*) GetWindow();
		chk->SetValue((val.val == wxT("1")));
		return true;
	}

	virtual bool Validate(wxWindow* parent) override {
		return true;
	}
};

struct GenericChoiceValidator : public wxValidator {
	genopt &val;

	GenericChoiceValidator(genopt &val_)
			: wxValidator(), val(val_) { }

	virtual wxObject* Clone() const {
		return new GenericChoiceValidator(val);
	}

	virtual bool TransferFromWindow() {
		wxChoice *choice = (wxChoice*) GetWindow();
		val.val = wxString::Format(wxT("%d"), choice->GetSelection());
		return true;
	}

	virtual bool TransferToWindow() {
		wxChoice *choice = (wxChoice*) GetWindow();
		unsigned long value;
		val.val.ToULong(&value);
		choice->SetSelection(value);
		return true;
	}

	virtual bool Validate(wxWindow* parent) {
		return true;
	}
};

enum {
	OPTWIN_ALL = 0,
	OPTWIN_DISPLAY,
	OPTWIN_FORMAT,
	OPTWIN_IMAGE,
	OPTWIN_NETWORK,
	OPTWIN_CACHING,
	OPTWIN_TWITTER,
	OPTWIN_SAVING,
	OPTWIN_FILTER,
	OPTWIN_MISC,

	OPTWIN_LAST,
};

void settings_window::AddSettingRow_Common(unsigned int win, wxWindow *parent, wxSizer *sizer, const wxString &name,
		flagwrapper<DBCV> flags, wxWindow *item, const wxValidator &dcbv) {
	wxStaticText *stat = new wxStaticText(parent, wxID_ANY, name);
	wxCheckBox *chk = new wxCheckBox(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, dcbv);

	sizer->Add(stat, 0, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	wxSize statsz = stat->GetSize();
	sizer->SetItemMinSize(stat, std::max(200, statsz.GetWidth()), statsz.GetHeight());
	sizer->Add(chk, 0, wxALIGN_CENTRE | wxALIGN_CENTRE_VERTICAL, 4);
	sizer->Add(item, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	opts.emplace_front(option_item {sizer, stat, win, flags});
	opts.emplace_front(option_item {sizer, chk, win, flags});
	opts.emplace_front(option_item {sizer, item, win, flags});
}

void settings_window::AddSettingRow_String(unsigned int win, wxWindow *parent, wxSizer *sizer, const wxString &name,
		flagwrapper<DBCV> flags, genopt &val, genopt &parentval, long style, wxValidator *textctrlvalidator) {
	wxTextValidator deftv(style, &val.val);
	if (!textctrlvalidator) {
		textctrlvalidator = &deftv;
	}
	wxTextCtrl *tc = new wxTextCtrl(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize,
			(flags & DBCV::MULTILINE) ? wxTE_MULTILINE : 0, *textctrlvalidator);
	DefaultChkBoxValidator dcbv(val, parentval, flags, tc);
	AddSettingRow_Common(win, parent, sizer, name, flags, tc, dcbv);
}

void settings_window::AddSettingRow_Bool(unsigned int win, wxWindow* parent, wxSizer *sizer, const wxString &name,
		flagwrapper<DBCV> flags, genopt &val, genopt &parentval) {
	ValueChkBoxValidator boolvalidator(val);
	wxCheckBox *chkval = new wxCheckBox(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, boolvalidator);
	DefaultChkBoxValidator dcbv(val, parentval, flags, chkval);
	AddSettingRow_Common(win, parent, sizer, name, flags, chkval, dcbv);
}

wxStaticBoxSizer *settings_window::AddGenoptconfSettingBlock(wxWindow* parent, wxSizer *sizer, const wxString &name,
		genoptconf &goc, genoptconf &parentgoc, flagwrapper<DBCV> flags) {
	wxStaticBoxSizer *sbox = new wxStaticBoxSizer(wxVERTICAL, parent, wxT("Account Settings - ") + name);
	wxFlexGridSizer *fgs = new wxFlexGridSizer(3, 2, 5);
	fgs->SetFlexibleDirection(wxHORIZONTAL);
	fgs->AddGrowableCol(2, 1);
	sizer->Add(sbox, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);
	sbox->Add(fgs, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);

	cat_empty_sizer_op.emplace_back(fgs, [=](bool show) {
		sizer->Show(sbox, show);
		vbox->Show(lb, show);
	});

	AddSettingRow_Bool(OPTWIN_TWITTER, parent, fgs,  wxT("Use User Streams (recommended)"), flags | DBCV::ADVOPTION, goc.userstreams, parentgoc.userstreams);
	AddSettingRow_String(OPTWIN_TWITTER, parent, fgs, wxT("REST API Polling Interval / seconds"), flags|DBCV::ADVOPTION, goc.restinterval, parentgoc.restinterval, wxFILTER_NUMERIC);

	auto replychoice = new wxChoice(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, 0, GenericChoiceValidator(goc.stream_reply_mode));
	replychoice->Append(wxT("Show standard stream replies"), (void *) nullptr);
	replychoice->Append(wxT("Show standard stream replies + all mentions"), (void *) nullptr);
	replychoice->Append(wxT("Show all stream replies from users you follow + all mentions"), (void *) nullptr);
	replychoice->Append(wxT("Show *all* stream replies"), (void *) nullptr);
	AddSettingRow_Common(OPTWIN_TWITTER, parent, fgs, wxT("Streaming mode replies"), flags|DBCV::ADVOPTION, replychoice, GenericChoiceDefaultChkBoxValidator(goc.stream_reply_mode, parentgoc.stream_reply_mode, replychoice));

	AddSettingRow_Bool(OPTWIN_TWITTER, parent, fgs,  wxT("Drop streamed tweets from blocked users"), flags | DBCV::ADVOPTION, goc.stream_drop_blocked, parentgoc.stream_drop_blocked);
	AddSettingRow_Bool(OPTWIN_TWITTER, parent, fgs,  wxT("Drop streamed tweets from muted users"), flags | DBCV::ADVOPTION, goc.stream_drop_muted, parentgoc.stream_drop_muted);
	AddSettingRow_Bool(OPTWIN_TWITTER, parent, fgs,  wxT("Drop streamed retweets from users with retweets disabled"), flags | DBCV::ADVOPTION, goc.stream_drop_no_rt, parentgoc.stream_drop_no_rt);

	AddSettingRow_Bool(OPTWIN_TWITTER, parent, fgs,  wxT("Use SSL\n(Very strongly recommended)"), flags|DBCV::VERYADVOPTION, goc.ssl, parentgoc.ssl);
	AddSettingRow_String(OPTWIN_TWITTER, parent, fgs, wxT("Twitter API Consumer Key Override"), flags|DBCV::HIDDENDEFAULT|DBCV::VERYADVOPTION, goc.tokenk, parentgoc.tokenk);
	AddSettingRow_String(OPTWIN_TWITTER, parent, fgs, wxT("Twitter API Consumer Secret Override"), flags|DBCV::HIDDENDEFAULT|DBCV::VERYADVOPTION, goc.tokens, parentgoc.tokens);
	return sbox;
}

enum {
	SWID_ACC_CHOICE = 1,
	SWID_ADVCAT_CHK,
	SWID_VADVCAT_CHK,
	SWID_FORMAT_CHOICE,

	SWID_CATRANGE_START = 4000,
};

BEGIN_EVENT_TABLE(settings_window, wxDialog)
EVT_CHOICE(SWID_ACC_CHOICE, settings_window::ChoiceCtrlChange)
EVT_CHECKBOX(SWID_ADVCAT_CHK, settings_window::ShowAdvCtrlChange)
EVT_CHECKBOX(SWID_VADVCAT_CHK, settings_window::ShowVeryAdvCtrlChange)
EVT_COMMAND_RANGE(SWID_CATRANGE_START, SWID_CATRANGE_START + OPTWIN_LAST - 1, wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, settings_window::CategoryButtonClick)
EVT_CHOICE(SWID_FORMAT_CHOICE, settings_window::FormatChoiceCtrlChange)
END_EVENT_TABLE()

settings_window::settings_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name, taccount *defshow)
		: wxDialog(parent, id, title, pos, size, style, name) {

	wxWindow *panel = this;
	current = 0;

	hbox = new wxBoxSizer(wxHORIZONTAL);
	vbox = new wxBoxSizer(wxVERTICAL);

	hbox->Add(vbox, 1, wxALL | wxEXPAND, 0);

	btnbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(btnbox, 0, wxALL | wxALIGN_TOP , 4);

	auto addfgsizerblock = [&](wxString boxname, wxFlexGridSizer *&fgsr) {
		wxStaticBoxSizer *hbox1 = new wxStaticBoxSizer(wxVERTICAL, panel, boxname);
		fgsr = new wxFlexGridSizer(3, 2, 5);
		fgsr->SetFlexibleDirection(wxBOTH);
		fgsr->AddGrowableCol(2, 1);

		vbox->Add(hbox1, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);
		hbox1->Add(fgsr, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);

		cat_empty_sizer_op.emplace_back(fgsr, [=](bool show) {
			vbox->Show(hbox1, show);
		});
	};

	wxFlexGridSizer *fgs = nullptr;
	addfgsizerblock(wxT("General Settings"), fgs);

	cat_buttons.resize(OPTWIN_LAST);
	auto addbtn = [&](unsigned int btnid, const wxString &btnname) {
		wxToggleButton *btn = new wxToggleButton(panel, SWID_CATRANGE_START + btnid, btnname);
		btnbox->Add(btn, 0, wxALL, 2);
		if (btnid == currentcat) {
			btn->SetValue(true);
		}
		cat_buttons[btnid] = btn;
	};
	addbtn(OPTWIN_DISPLAY, wxT("Display"));
	addbtn(OPTWIN_FORMAT, wxT("Format"));
	addbtn(OPTWIN_IMAGE, wxT("Image"));
	addbtn(OPTWIN_NETWORK, wxT("Network"));
	addbtn(OPTWIN_CACHING, wxT("Caching"));
	addbtn(OPTWIN_TWITTER, wxT("Twitter"));
	addbtn(OPTWIN_SAVING, wxT("Saving"));
	addbtn(OPTWIN_FILTER, wxT("Filter"));
	addbtn(OPTWIN_MISC, wxT("Misc"));

	wxBoxSizer *hboxfooter = new wxBoxSizer(wxHORIZONTAL);
	wxButton *okbtn = new wxButton(panel, wxID_OK, wxT("OK"));
	wxButton *cancelbtn = new wxButton(panel, wxID_CANCEL, wxT("Cancel"));
	advoptchkbox = new wxCheckBox(panel, SWID_ADVCAT_CHK, wxT("Show Advanced Options"));
	veryadvoptchkbox = new wxCheckBox(panel, SWID_VADVCAT_CHK, wxT("Show Very Advanced Options"));
	wxBoxSizer *advoptbox = new wxBoxSizer(wxVERTICAL);
	advoptbox->Add(advoptchkbox, 0, wxALL | wxALIGN_CENTRE_VERTICAL, 2);
	advoptbox->Add(veryadvoptchkbox, 0, wxALL | wxALIGN_CENTRE_VERTICAL, 2);
	hboxfooter->Add(advoptbox, 0, wxALL | wxALIGN_CENTRE_VERTICAL, 2);
	hboxfooter->AddStretchSpacer();
	hboxfooter->Add(okbtn, 0, wxALL | wxALIGN_BOTTOM | wxALIGN_RIGHT, 2);
	hboxfooter->Add(cancelbtn, 0, wxALL | wxALIGN_BOTTOM | wxALIGN_RIGHT, 2);
	opts.emplace_front(option_item {advoptbox, veryadvoptchkbox, 0, DBCV::ADVOPTION});

	auto emoji_mode_choice = new wxChoice(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0, nullptr, 0, GenericChoiceValidator(gc.gcfg.emoji_mode));
	emoji_mode_choice->Append(wxT("Disabled"), (void *) nullptr);
	emoji_mode_choice->Append(wxT("16x16 icons"), (void *) nullptr);
	emoji_mode_choice->Append(wxT("36x36 icons"), (void *) nullptr);
	AddSettingRow_Common(OPTWIN_DISPLAY, panel, fgs, wxT("Display emojis"), DBCV::ISGLOBALCFG, emoji_mode_choice, GenericChoiceDefaultChkBoxValidator(gc.gcfg.emoji_mode, gcglobdefaults.emoji_mode, emoji_mode_choice));

	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Max no. of items to display in panel"), DBCV::ISGLOBALCFG, gc.gcfg.maxtweetsdisplayinpanel, gcglobdefaults.maxtweetsdisplayinpanel, wxFILTER_NUMERIC);
	AddSettingRow_Bool(OPTWIN_DISPLAY, panel, fgs,  wxT("Display Native Re-Tweets"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.rtdisp, gcglobdefaults.rtdisp);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs,  wxT("No. of inline tweet replies"), DBCV::ISGLOBALCFG, gc.gcfg.inlinereplyloadcount, gcglobdefaults.inlinereplyloadcount, wxFILTER_NUMERIC);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs,  wxT("No. of inline tweet replies to load on request"), DBCV::ISGLOBALCFG, gc.gcfg.inlinereplyloadmorecount, gcglobdefaults.inlinereplyloadmorecount, wxFILTER_NUMERIC);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs,  wxT("Max quoted tweet recursion depth"), DBCV::ISGLOBALCFG, gc.gcfg.tweet_quote_recursion_max_depth, gcglobdefaults.tweet_quote_recursion_max_depth, wxFILTER_NUMERIC);
	AddSettingRow_Bool(OPTWIN_DISPLAY, panel, fgs, wxT("Show panel Unhighlight All button"), DBCV::ISGLOBALCFG, gc.gcfg.showunhighlightallbtn, gcglobdefaults.showunhighlightallbtn);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Highlight colour"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.highlight_colourdelta, gcglobdefaults.highlight_colourdelta);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Deleted colour"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.deleted_colourdelta, gcglobdefaults.deleted_colourdelta);
	AddSettingRow_Bool(OPTWIN_DISPLAY, panel, fgs, wxT("Show deleted tweets by default"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.showdeletedtweetsbydefault, gcglobdefaults.showdeletedtweetsbydefault);
	AddSettingRow_Bool(OPTWIN_DISPLAY, panel, fgs, wxT("Mark deleted tweets and DMs as read"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.markdeletedtweetsasread, gcglobdefaults.markdeletedtweetsasread);
	AddSettingRow_Bool(OPTWIN_DISPLAY, panel, fgs, wxT("Mark own tweets and DMs as read"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.markowntweetsasread, gcglobdefaults.markowntweetsasread);

	wxFlexGridSizer *scrollparamsfgs = nullptr;
	addfgsizerblock(wxT("Scrolling"), scrollparamsfgs);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, scrollparamsfgs, wxT("Mouse wheel scroll speed"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.mousewheelscrollspeed, gcglobdefaults.mousewheelscrollspeed);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, scrollparamsfgs, wxT("Line scroll speed"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.linescrollspeed, gcglobdefaults.linescrollspeed);

	wxFlexGridSizer *mediawinposfgs = nullptr;
	addfgsizerblock(wxT("Media Window Positioning"), mediawinposfgs);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, mediawinposfgs, wxT("Screen width reduction"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.mediawinscreensizewidthreduction, gcglobdefaults.mediawinscreensizewidthreduction);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, mediawinposfgs, wxT("Screen height reduction"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.mediawinscreensizeheightreduction, gcglobdefaults.mediawinscreensizeheightreduction);

	formatdef_lb = new wxChoice(panel, SWID_FORMAT_CHOICE, wxDefaultPosition, wxDefaultSize, 0, nullptr, 0, GenericChoiceValidator(gc.gcfg.format_default_num));
	formatdef_lb->Append(wxT("Short"), (void *) nullptr);
	formatdef_lb->Append(wxT("Medium"), (void *) nullptr);
	formatdef_lb->Append(wxT("Long"), (void *) nullptr);
	AddSettingRow_Common(OPTWIN_FORMAT, panel, fgs, wxT("Default display formats"), DBCV::ISGLOBALCFG, formatdef_lb, FormatChoiceDefaultChkBoxValidator(gc.gcfg.format_default_num, gcglobdefaults.format_default_num, this));

	AddSettingRow_String(OPTWIN_FORMAT, panel, fgs, wxT("Date/time format (strftime)"), DBCV::ISGLOBALCFG, gc.gcfg.datetimeformat, gcglobdefaults.datetimeformat);
	AddSettingRow_Bool(OPTWIN_FORMAT, panel, fgs, wxT("Display date/times as UTC instead of local"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.datetimeisutc, gcglobdefaults.datetimeisutc);

	AddSettingRow_String(OPTWIN_FORMAT, panel, fgs, wxT("Tweet display format"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.tweetdispformat, current_format_set.tweetdispformat);
	AddSettingRow_String(OPTWIN_FORMAT, panel, fgs, wxT("DM display format"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.dmdispformat, current_format_set.dmdispformat);
	AddSettingRow_String(OPTWIN_FORMAT, panel, fgs, wxT("Native Re-Tweet display format"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.rtdispformat, current_format_set.rtdispformat);
	AddSettingRow_String(OPTWIN_FORMAT, panel, fgs, wxT("User display format"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.userdispformat, current_format_set.userdispformat);
	AddSettingRow_String(OPTWIN_FORMAT, panel, fgs, wxT("Tweet mouse-over format"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.mouseover_tweetdispformat, gcglobdefaults.mouseover_tweetdispformat);
	AddSettingRow_String(OPTWIN_FORMAT, panel, fgs, wxT("DM mouse-over format"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.mouseover_dmdispformat, gcglobdefaults.mouseover_dmdispformat);
	AddSettingRow_String(OPTWIN_FORMAT, panel, fgs, wxT("Native Re-Tweet mouse-over format"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.mouseover_rtdispformat, gcglobdefaults.mouseover_rtdispformat);

	AddSettingRow_String(OPTWIN_IMAGE, panel, fgs, wxT("Max profile image size / px"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.maxpanelprofimgsize, gcglobdefaults.maxpanelprofimgsize, wxFILTER_NUMERIC);
	AddSettingRow_Bool(OPTWIN_IMAGE, panel, fgs, wxT("Display image thumbnails"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.dispthumbs, gcglobdefaults.dispthumbs);
	wxFlexGridSizer *loadthumbfgs = nullptr;
	addfgsizerblock(wxT("Load image thumbnails"), loadthumbfgs);
	AddSettingRow_Bool(OPTWIN_IMAGE, panel, loadthumbfgs, wxT("Twitter image thumbnails"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.disploadthumb_thumb, gcglobdefaults.disploadthumb_thumb);
	AddSettingRow_Bool(OPTWIN_IMAGE, panel, loadthumbfgs, wxT("Full-size images as thumbnails"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.disploadthumb_full, gcglobdefaults.disploadthumb_full);
	wxFlexGridSizer *autoloadthumbfgs = nullptr;
	addfgsizerblock(wxT("Pre-load image thumbnails even when not displayed"), autoloadthumbfgs);
	AddSettingRow_Bool(OPTWIN_IMAGE, panel, autoloadthumbfgs, wxT("Twitter image thumbnails"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.autoloadthumb_thumb, gcglobdefaults.autoloadthumb_thumb);
	AddSettingRow_Bool(OPTWIN_IMAGE, panel, autoloadthumbfgs, wxT("Full-size images as thumbnails"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.autoloadthumb_full, gcglobdefaults.autoloadthumb_full);
	wxFlexGridSizer *hiddenthumbfgs = nullptr;
	addfgsizerblock(wxT("Hidden image thumbnails"), hiddenthumbfgs);
	AddSettingRow_Bool(OPTWIN_IMAGE, panel, hiddenthumbfgs, wxT("Hide all image thumbnails"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.hideallthumbs, gcglobdefaults.hideallthumbs);
	AddSettingRow_String(OPTWIN_IMAGE, panel, hiddenthumbfgs,  wxT("Unhide image thumbnail time / seconds"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.imgthumbunhidetime, gcglobdefaults.imgthumbunhidetime, wxFILTER_NUMERIC);
	AddSettingRow_Bool(OPTWIN_IMAGE, panel, hiddenthumbfgs, wxT("Pre-load hidden thumbnails"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.loadhiddenthumbs, gcglobdefaults.loadhiddenthumbs);

	AddSettingRow_String(OPTWIN_CACHING, panel, fgs, wxT("Cached User Expiry Time / minutes"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.userexpiretimemins, gcglobdefaults.userexpiretimemins, wxFILTER_NUMERIC);
	AddSettingRow_Bool(OPTWIN_CACHING, panel, fgs,  wxT("Cache media image thumbnails"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.cachethumbs, gcglobdefaults.cachethumbs);
	AddSettingRow_Bool(OPTWIN_CACHING, panel, fgs,  wxT("Cache full-size media images"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.cachemedia, gcglobdefaults.cachemedia);
	AddSettingRow_String(OPTWIN_CACHING, panel, fgs,  wxT("Delete cached media images after\nnot being used for this many days"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.mediacachesavedays, gcglobdefaults.mediacachesavedays, wxFILTER_NUMERIC);
	AddSettingRow_String(OPTWIN_CACHING, panel, fgs,  wxT("Delete cached user profile images after\nnot being used for this many days"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.profimgcachesavedays, gcglobdefaults.profimgcachesavedays, wxFILTER_NUMERIC);

	AddSettingRow_Bool(OPTWIN_TWITTER, panel, fgs,  wxT("Assume that mentions are a subset of the home timeline"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.assumementionistweet, gcglobdefaults.assumementionistweet);
	AddSettingRow_Bool(OPTWIN_TWITTER, panel, fgs,  wxT("Ask about changing the settings of new accounts, before authentication.\nThis is useful for creating an account with different Twitter authentication settings."),
			DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.askuseraccsettingsonnewacc, gcglobdefaults.askuseraccsettingsonnewacc);

	AddSettingRow_String(OPTWIN_SAVING, panel, fgs,  wxT("Media image\nsave directories\n(1 per line)"), DBCV::ISGLOBALCFG | DBCV::MULTILINE, gc.gcfg.mediasave_directorylist, gcglobdefaults.mediasave_directorylist);

	wxFlexGridSizer *tweetfilterfgs = nullptr;
	addfgsizerblock(wxT("Tweet Filters - Read Documentation Before Use"), tweetfilterfgs);
	FilterTextValidator filterval(ad.incoming_filter, &gc.gcfg.incoming_filter.val);
	AddSettingRow_String(OPTWIN_FILTER, panel, tweetfilterfgs,  wxT("Timeline Tweet filter\nHome timeline, mentions, DMs"), DBCV::ISGLOBALCFG | DBCV::MULTILINE | DBCV::ADVOPTION, gc.gcfg.incoming_filter, gcglobdefaults.incoming_filter, 0, &filterval);
	FilterTextValidator allt_filterval(ad.alltweet_filter, &gc.gcfg.alltweet_filter.val);
	AddSettingRow_String(OPTWIN_FILTER, panel, tweetfilterfgs,  wxT("All Tweet filter\nAbove, plus inline replies,\nuser timelines, etc."), DBCV::ISGLOBALCFG | DBCV::MULTILINE | DBCV::ADVOPTION, gc.gcfg.alltweet_filter, gcglobdefaults.alltweet_filter, 0, &allt_filterval);

	AddSettingRow_Bool(OPTWIN_MISC, panel, fgs,  wxT("Show Import Stream File menu item"), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.show_import_stream_menu_item, gcglobdefaults.show_import_stream_menu_item);
	AddSettingRow_String(OPTWIN_MISC, panel, fgs, wxT("Thread pool limit, 0 to disable\nDo not set this too high\nRestart retcon for this to take effect"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.threadpoollimit, gcglobdefaults.threadpoollimit, wxFILTER_NUMERIC);
	AddSettingRow_String(OPTWIN_MISC, panel, fgs, wxT("Flush all state to DB interval / mins, 0 to disable\nChanges take effect after the next flush"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.asyncstatewritebackintervalmins, gcglobdefaults.asyncstatewritebackintervalmins, wxFILTER_NUMERIC);
	AddSettingRow_Bool(OPTWIN_MISC, panel, fgs,  wxT("Show debug actions in tweet menu"), DBCV::ISGLOBALCFG | DBCV::VERYADVOPTION, gc.gcfg.tweetdebugactions, gcglobdefaults.tweetdebugactions);

	wxFlexGridSizer *proxyfgs = nullptr;
	addfgsizerblock(wxT("Proxy Settings"), proxyfgs);

	AddSettingRow_Bool(OPTWIN_NETWORK, panel, proxyfgs, wxT("Use proxy settings (applies to new connections).\n(If not set, use system/environment default)"), DBCV::ISGLOBALCFG, gc.gcfg.setproxy, gcglobdefaults.setproxy);

#if LIBCURL_VERSION_NUM >= 0x071507
	wxString proxyurllabel = wxT("Proxy URL, default HTTP. Prefix with socks4:// socks4a://\nsocks5:// or socks5h:// for SOCKS proxying.");
#else
	wxString proxyurllabel = wxT("Proxy URL (HTTP only)");
#endif
	AddSettingRow_String(OPTWIN_NETWORK, panel, proxyfgs, proxyurllabel, DBCV::ISGLOBALCFG, gc.gcfg.proxyurl, gcglobdefaults.proxyurl);
	AddSettingRow_Bool(OPTWIN_NETWORK, panel, proxyfgs, wxT("Use tunnelling HTTP proxy (HTTP CONNECT)."), DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.proxyhttptunnel, gcglobdefaults.proxyhttptunnel);
	AddSettingRow_String(OPTWIN_NETWORK, panel, proxyfgs, wxT("List of host names which should not be proxied.\nSeparate with commas or newlines"), DBCV::ISGLOBALCFG | DBCV::MULTILINE, gc.gcfg.noproxylist, gcglobdefaults.noproxylist);

#if LIBCURL_VERSION_NUM >= 0x071800
	wxString netifacelabel = wxT("Outgoing network interface (interface name, IP or host).\nPrefix with if! or host! to force interface name or host/IP respectively.");
#else
	wxString netifacelabel = wxT("Outgoing network interface (interface name, IP or host)");
#endif
	AddSettingRow_String(OPTWIN_NETWORK, panel, fgs, netifacelabel, DBCV::ISGLOBALCFG | DBCV::ADVOPTION, gc.gcfg.netiface, gcglobdefaults.netiface);

	lb = new wxChoice(panel, SWID_ACC_CHOICE);

	vbox->Add(lb, 0, wxALL, 4);

	wxStaticBoxSizer *defsbox = AddGenoptconfSettingBlock(panel, vbox, wxT("[Defaults for All Accounts]"), gc.cfg, gcdefaults, DBCV::ISGLOBALCFG);
	accmap[0] = defsbox;
	lb->Append(wxT("[Defaults for All Accounts]"), (void *) 0);
	lb->SetSelection(0);

	for (auto &it : alist) {
		wxStaticBoxSizer *sbox = AddGenoptconfSettingBlock(panel, vbox, it->dispname, it->cfg, gc.cfg, 0);
		accmap[it.get()] = sbox;
		lb->Append(it->dispname, it.get());
		if (it.get() == defshow) {
			current = defshow;
			lb->SetSelection(lb->GetCount() - 1);
		} else {
			vbox->Hide(sbox);
		}
	}
	if (defshow && current != defshow) {	//for (new) accounts not (yet) in alist
		wxStaticBoxSizer *sbox = AddGenoptconfSettingBlock(panel, vbox, defshow->dispname, defshow->cfg, gc.cfg, 0);
		accmap[defshow] = sbox;
		lb->Append(defshow->dispname, defshow);
		lb->SetSelection(lb->GetCount() - 1);
		current = defshow;
	}

	if (current) {
		vbox->Hide(defsbox);
	}

	vbox->Add(hboxfooter, 0, wxALL | wxALIGN_BOTTOM | wxEXPAND, 0);

	OptShowHide(static_cast<DBCV>(~0));

	panel->SetSizer(hbox);
	hbox->Fit(panel);

	initsize = GetSize();
	SetSizeHints(initsize.GetWidth(), initsize.GetHeight(), 9000, initsize.GetHeight());

	currentcat = 1;
	cat_buttons[1]->SetValue(true);
	SetSizeHints(GetSize().GetWidth(), 1);
	OptShowHide(0);
	PostOptShowHide();
}

settings_window::~settings_window() {
	UpdateAllTweets(false, true);
	tpanelparentwin_nt::UpdateAllCLabels();
	for (auto &it : alist) {
		it->Exec();
		it->SetupRestBackfillTimer();
	}
	AccountChangeTrigger();
	settings_changed_notifier::NotifyAll();
}

void settings_window::ChoiceCtrlChange(wxCommandEvent &event) {
	Freeze();
	SetSizeHints(GetSize().GetWidth(), 1);
	current = static_cast<taccount*>(event.GetClientData());
	vbox->Show(accmap[current]);
	OptShowHide((advoptchkbox->IsChecked() ? DBCV::ADVOPTION : static_cast<DBCV>(0)) | (veryadvoptchkbox->IsChecked() ? DBCV::VERYADVOPTION : static_cast<DBCV>(0)));
	PostOptShowHide();
	Thaw();
}

void settings_window::ShowAdvCtrlChange(wxCommandEvent &event) {
	Freeze();
	SetSizeHints(GetSize().GetWidth(), 1);
	if (!event.IsChecked()) {
		veryadvoptchkbox->SetValue(false);
	}
	OptShowHide((event.IsChecked() ? DBCV::ADVOPTION : static_cast<DBCV>(0)) | (veryadvoptchkbox->IsChecked() ? DBCV::VERYADVOPTION : static_cast<DBCV>(0)));
	PostOptShowHide();
	Thaw();
}
void settings_window::ShowVeryAdvCtrlChange(wxCommandEvent &event) {
	Freeze();
	SetSizeHints(GetSize().GetWidth(), 1);
	OptShowHide((advoptchkbox->IsChecked() ? DBCV::ADVOPTION : static_cast<DBCV>(0)) | (event.IsChecked() ? DBCV::VERYADVOPTION : static_cast<DBCV>(0)));
	PostOptShowHide();
	Thaw();
}

void settings_window::FormatChoiceCtrlChange(wxCommandEvent &event) {
	if (event.GetSelection() == current_format_set_id) {
		return;
	}
	Freeze();
	current_format_set_id = event.GetSelection();
	current_format_set = IndexToFormatSet(current_format_set_id);
	Validate();
	Thaw();
}

void settings_window::OptShowHide(flagwrapper<DBCV> setmask) {
	std::unordered_set<wxSizer *> sizerset;
	std::unordered_set<wxSizer *> nonempty_sizerset;
	std::vector<std::function<void()> > ops;
	std::vector<unsigned int> showcount(OPTWIN_LAST, 0);
	for (auto &it : opts) {
		flagwrapper<DBCV> allmask = DBCV::ADVOPTION | DBCV::VERYADVOPTION;
		flagwrapper<DBCV> maskedflags = it.flags & allmask;
		bool show;
		if (!maskedflags) {
			show = true;
		} else {
			show = (bool) (maskedflags & setmask);
		}
		if (show) {
			showcount[it.cat]++;
		}
		if (it.cat && currentcat && it.cat != currentcat) {
			show = false;
		}
		ops.push_back([=] { it.sizer->Show(it.win, show); });
		if (show) {
			nonempty_sizerset.insert(it.sizer);
		}
		sizerset.insert(it.sizer);
	}
	for (auto &it : sizerset) {
		bool isnonempty = nonempty_sizerset.count(it);
		for (auto &jt : cat_empty_sizer_op) {
			if (it == jt.first) {
				jt.second(isnonempty);
			}
		}
	}
	for (auto &it : ops) {
		it();
	}
	for (auto &it : sizerset) it->Layout();
	unsigned int i = OPTWIN_LAST;
	do {
		i--;
		bool show = (bool) showcount[i];
		if (cat_buttons[i]) {
			btnbox->Show(cat_buttons[i], show);
		}
		if (!show && i && i == currentcat) {
			currentcat--;
			if (cat_buttons[i]) {
				cat_buttons[i]->SetValue(false);
			}
			if (cat_buttons[currentcat]) {
				cat_buttons[currentcat]->SetValue(true);
			}
			OptShowHide(setmask);
			return;
		}
	} while (i > 0);
	btnbox->Layout();
}

void settings_window::PostOptShowHide() {
	for (auto &it : accmap) {
		if (it.first != current) {
			vbox->Hide(it.second);
		}
	}
	vbox->Layout();
	GetSizer()->Fit(this);
	wxSize cursize = GetSize();
	SetSizeHints(initsize.GetWidth(), cursize.GetHeight(), 9000, cursize.GetHeight());
}

bool settings_window::TransferDataFromWindow() {
	bool retval = wxWindow::TransferDataFromWindow();
	if (retval) {
		AllUsersInheritFromParentIfUnset();
		gc.CFGParamConv();
		for (auto &it : alist) {
			it->CFGParamConv();
		}
	}
	return retval;
}

void settings_window::CategoryButtonClick(wxCommandEvent &event) {
	Freeze();
	if (cat_buttons[currentcat]) {
		cat_buttons[currentcat]->SetValue(false);
	}
	currentcat = event.GetId() - SWID_CATRANGE_START;
	cat_buttons[currentcat]->SetValue(true);

	SetSizeHints(GetSize().GetWidth(), 1);
	OptShowHide((advoptchkbox->IsChecked() ? DBCV::ADVOPTION : static_cast<DBCV>(0)) |
			(veryadvoptchkbox->IsChecked() ? DBCV::VERYADVOPTION : static_cast<DBCV>(0)));
	PostOptShowHide();
	Thaw();
}
