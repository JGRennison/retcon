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

#include "univdefs.h"
#include "optui.h"
#include "taccount.h"
#include "db.h"
#include "cfg.h"
#include "twit.h"
#include "twitcurlext.h"
#include "alldata.h"
#include "filter/filter-ops.h"
#include "util.h"
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

enum {
	ACCWID_ENDISABLE=1,
	ACCWID_REAUTH,

};

BEGIN_EVENT_TABLE(acc_window, wxDialog)
	EVT_BUTTON(wxID_PROPERTIES, acc_window::AccEdit)
	EVT_BUTTON(wxID_DELETE, acc_window::AccDel)
	EVT_BUTTON(wxID_NEW, acc_window::AccNew)
	EVT_BUTTON(wxID_CLOSE, acc_window::AccClose)
	EVT_BUTTON(ACCWID_ENDISABLE, acc_window::EnDisable)
	EVT_BUTTON(ACCWID_REAUTH, acc_window::ReAuth)
	EVT_LISTBOX(wxID_FILE1, acc_window::OnSelChange)
END_EVENT_TABLE()

std::set<acc_window *> acc_window::currentset;

acc_window::acc_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: wxDialog(parent, id, title, pos, size, style, name) {

	//wxPanel *panel = new wxPanel(this, -1);
	wxWindow *panel=this;

	wxBoxSizer *vbox = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer *hbox1 = new wxStaticBoxSizer(wxHORIZONTAL, panel, wxT("Accounts"));
	wxBoxSizer *hbox2 = new wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer *vboxr = new wxBoxSizer(wxVERTICAL);

	lb=new wxListBox(panel, wxID_FILE1, wxDefaultPosition, wxDefaultSize, 0, 0, wxLB_SINGLE | wxLB_SORT | wxLB_NEEDED_SB);
	UpdateLB();
	editbtn=new wxButton(panel, wxID_PROPERTIES, wxT("Settings"));
	endisbtn=new wxButton(panel, ACCWID_ENDISABLE, wxT("Disable"));
	reauthbtn=new wxButton(panel, ACCWID_REAUTH, wxT("Re-Authenticate"));
	delbtn=new wxButton(panel, wxID_DELETE, wxT("Delete"));
	wxButton *newbtn=new wxButton(panel, wxID_NEW, wxT("Add account"));
	wxButton *clsbtn=new wxButton(panel, wxID_CLOSE, wxT("Close"));

	vbox->Add(hbox1, 1, wxALL | wxEXPAND , 4);
	vbox->Add(hbox2, 0, wxALL | wxEXPAND , 4);
	hbox1->Add(lb, 1, wxALL | wxALIGN_LEFT | wxEXPAND, 4);
	hbox1->Add(vboxr, 0, wxALL, 4);
	vboxr->Add(editbtn, 0, wxALIGN_TOP | wxEXPAND, 0);
	vboxr->Add(delbtn, 0, wxALIGN_TOP | wxEXPAND, 0);
	vboxr->Add(endisbtn, 0, wxALIGN_TOP | wxEXPAND, 0);
	vboxr->Add(reauthbtn, 0, wxALIGN_TOP | wxEXPAND, 0);
	hbox2->Add(newbtn, 0, wxALIGN_LEFT, 0);
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
	int selection=lb->GetSelection();
	editbtn->Enable(selection!=wxNOT_FOUND);
	endisbtn->Enable(selection!=wxNOT_FOUND);
	reauthbtn->Enable(selection!=wxNOT_FOUND);
	delbtn->Enable(selection!=wxNOT_FOUND);
	if(selection!=wxNOT_FOUND) {
		taccount *acc=(taccount *) lb->GetClientData(selection);
		endisbtn->SetLabel(acc->userenabled?wxT("Disable"):wxT("Enable"));
	}
	else endisbtn->SetLabel(wxT("Disable"));
}

void acc_window::UpdateLB() {
	int selection=lb->GetSelection();
	taccount *acc;
	if(selection!=wxNOT_FOUND) acc=(taccount *) lb->GetClientData(selection);
	else acc=0;
	lb->Clear();
	for(auto it=alist.begin(); it != alist.end(); it++ ) {
		wxString accname=(*it)->dispname + wxT(" [") + (*it)->GetStatusString(false) + wxT("]");;
		int index=lb->Append(accname,(*it).get());
		if((*it).get()==acc) lb->SetSelection(index);
	}
}

void acc_window::AccEdit(wxCommandEvent &event) {
	int sel=lb->GetSelection();
	if(sel==wxNOT_FOUND) return;
	taccount *acc=(taccount *) lb->GetClientData(sel);
	settings_window *sw=new settings_window(this, -1, wxT("Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER, wxT("dialogBox"), acc);
	sw->ShowModal();
	sw->Destroy();
}

void acc_window::AccDel(wxCommandEvent &event) {
	int sel=lb->GetSelection();
	if(sel==wxNOT_FOUND) return;
	taccount *acc=(taccount *) lb->GetClientData(sel);
	if(!acc->dbindex) return;
	int answer=wxMessageBox(wxT("Are you sure that you want to delete account: ") + acc->dispname + wxT(".\nThis cannot be undone."), wxT("Confirm Account Deletion"), wxYES_NO | wxICON_EXCLAMATION, this);
	if(answer==wxYES) {
		acc->enabled=acc->userenabled=0;
		acc->Exec();
		dbdelaccmsg *delmsg=new dbdelaccmsg;
		delmsg->dbindex=acc->dbindex;
		dbc.SendMessage(delmsg);
		alist.remove_if([&](const std::shared_ptr<taccount> &a) { return a.get()==acc; });
		lb->SetSelection(wxNOT_FOUND);
		UpdateLB();
	}
}

void acc_window::AccNew(wxCommandEvent &event) {
	std::shared_ptr<taccount> ta(new taccount(&gc.cfg));
	ta->enabled=false;
	ta->dispname=wxT("<new account>");

	int answer=wxMessageBox(wxT("Would you like to review the account settings before authenticating?"), wxT("Account Creation"), wxYES_NO | wxCANCEL | wxICON_QUESTION | wxNO_DEFAULT, this);

	if(answer==wxYES) {
		settings_window *sw=new settings_window(this, -1, wxT("New Account Settings"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER, wxT("dialogBox"), ta.get());
		sw->ShowModal();
		sw->Destroy();
	}
	else if(answer==wxCANCEL) return;

	twitcurlext *twit=ta->GetTwitCurlExt();
	if(ta->TwDoOAuth(this, *twit)) {
		if(twit->TwSyncStartupAccVerify()) {
			ta->userenabled=true;
			ta->beinginsertedintodb=true;
			ta->name=wxString::Format(wxT("%" wxLongLongFmtSpec "d-%d"),ta->usercont->id,time(0));
			alist.push_back(ta);
			UpdateLB();
			dbinsertaccmsg *insmsg=new dbinsertaccmsg;
			insmsg->name=ta->name.ToUTF8();
			insmsg->dispname=ta->dispname.ToUTF8();
			insmsg->userid=ta->usercont->id;
			insmsg->targ=&dbc;
			insmsg->cmdevtype=wxextDBCONN_NOTIFY;
			insmsg->winid=wxDBCONNEVT_ID_INSERTNEWACC;
			dbc.SendMessage(insmsg);
		}
	}
	twit->TwDeInit();
	ta->cp.Standby(twit);
}
void acc_window::AccClose(wxCommandEvent &event) {
	currentset.erase(this);
	EndModal(0);
}

void acc_window::EnDisable(wxCommandEvent &event) {
	int sel=lb->GetSelection();
	if(sel==wxNOT_FOUND) return;
	taccount *acc=(taccount *) lb->GetClientData(sel);
	acc->userenabled=!acc->userenabled;
	acc->CalcEnabled();
	acc->Exec();
	UpdateLB();
	UpdateButtons();
}

void acc_window::ReAuth(wxCommandEvent &event) {
	int sel=lb->GetSelection();
	if(sel==wxNOT_FOUND) return;
	taccount *acc=(taccount *) lb->GetClientData(sel);
	acc->enabled=0;
	acc->Exec();
	twitcurlext *twit=acc->GetTwitCurlExt();
	twit->getOAuth().setOAuthTokenKey("");		//remove existing oauth tokens
	twit->getOAuth().setOAuthTokenSecret("");
	if(acc->TwDoOAuth(this, *twit)) {
		twit->TwSyncStartupAccVerify();
	}
	UpdateLB();
	UpdateButtons();
	acc->CalcEnabled();
	acc->Exec();
}

enum {
	DCBV_HIDDENDEFAULT	= 1<<0,
	DCBV_ISGLOBALCFG	= 1<<1,
	DCBV_ADVOPTION		= 1<<2,
	DCBV_VERYADVOPTION	= 1<<3,
	DCBV_MULTILINE		= 1<<4,
};

struct DefaultChkBoxValidator : public wxValidator {
	genopt &val;
	genopt &parentval;
	wxTextCtrl *txtctrl;
	wxCheckBox *chkbox;
	unsigned int flags;

	DefaultChkBoxValidator(genopt &val_, genopt &parentval_, unsigned int flags_=0, wxTextCtrl *txtctrl_=0, wxCheckBox *chkbox_=0)
		: wxValidator(), val(val_), parentval(parentval_), txtctrl(txtctrl_), chkbox(chkbox_), flags(flags_) { }
	virtual wxObject* Clone() const { return new DefaultChkBoxValidator(val, parentval, flags, txtctrl, chkbox); }
	virtual bool TransferFromWindow() {
		wxCheckBox *chk=(wxCheckBox*) GetWindow();
		val.enable=chk->GetValue();
		return true;
	}
	virtual bool TransferToWindow() {
		wxCheckBox *chk=(wxCheckBox*) GetWindow();
		chk->SetValue(val.enable);
		statechange();
		return true;
	}
	void statechange() {
		wxCheckBox *chk=(wxCheckBox*) GetWindow();
		if(txtctrl) {
			txtctrl->Enable(chk->GetValue());
			if(!chk->GetValue()) {
				if(flags&DCBV_HIDDENDEFAULT) {
					if(flags&DCBV_ISGLOBALCFG) txtctrl->ChangeValue(wxT(""));
					else if(parentval.enable) txtctrl->ChangeValue(parentval.val);
					else txtctrl->ChangeValue(wxT(""));
				}
				else txtctrl->ChangeValue(parentval.val);
			}
		}
		else if(chkbox) {
			chkbox->Enable(chk->GetValue());
			if(!chk->GetValue()) {
				chkbox->SetValue((parentval.val==wxT("1")));
			}
		}
	}
	virtual bool Validate(wxWindow* parent) {
		statechange();
		return true;
	}
	void checkboxchange(wxCommandEvent &event) {
		statechange();
	}

	DECLARE_EVENT_TABLE()
};

BEGIN_EVENT_TABLE(DefaultChkBoxValidator, wxValidator)
EVT_CHECKBOX(wxID_ANY, DefaultChkBoxValidator::checkboxchange)
END_EVENT_TABLE()

struct ValueChkBoxValidator : public wxValidator {
	genopt &val;
	ValueChkBoxValidator(genopt &val_)
		: wxValidator(), val(val_) { }
	virtual wxObject* Clone() const { return new ValueChkBoxValidator(val); }
	virtual bool TransferFromWindow() {
		wxCheckBox *chk=(wxCheckBox*) GetWindow();
		val.val=((chk->GetValue())?wxT("1"):wxT("0"));
		return true;
	}
	virtual bool TransferToWindow() {
		wxCheckBox *chk=(wxCheckBox*) GetWindow();
		chk->SetValue((val.val==wxT("1")));
		return true;
	}
	virtual bool Validate(wxWindow* parent) {
		return true;
	}
};

struct FilterTextValidator : public wxTextValidator {
	filter_set &fs;
	wxString *valPtr;
	std::shared_ptr<filter_set> ownfilter;
	FilterTextValidator(filter_set &fs_, wxString* valPtr_ = NULL)
			: wxTextValidator((long) wxFILTER_NONE, valPtr_), fs(fs_), valPtr(valPtr_) {
	}
	virtual wxObject* Clone() const {
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
	virtual bool Validate(wxWindow* parent) {
		wxTextCtrl *win = (wxTextCtrl *) GetWindow();

		if(!ownfilter) ownfilter = std::make_shared<filter_set>();
		std::string errmsg;
		ParseFilter(stdstrwx(win->GetValue()), *ownfilter, errmsg);
		if(errmsg.empty()) {
			return true;
		}
		else {
			::wxMessageBox(wxT("Filter is not valid, please correct errors.\n") + wxstrstd(errmsg), wxT("Filter Validation Failed"), wxOK | wxICON_EXCLAMATION, parent);
			return false;
		}
	}
};

enum {
	OPTWIN_ALL = 0,
	OPTWIN_DISPLAY,
	OPTWIN_NETWORK,
	OPTWIN_CACHING,
	OPTWIN_TWITTER,
	OPTWIN_SAVING,
	OPTWIN_FILTER,

	OPTWIN_LAST,
};

void settings_window::AddSettingRow_String(unsigned int win, wxWindow* parent, wxSizer *sizer, const wxString &name, unsigned int flags, genopt &val, genopt &parentval, long style, wxValidator *textctrlvalidator) {
	wxTextValidator deftv(style, &val.val);
	if(!textctrlvalidator) textctrlvalidator=&deftv;
	wxStaticText *stat=new wxStaticText(parent, wxID_ANY, name);
	wxTextCtrl *tc=new wxTextCtrl(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, (flags & DCBV_MULTILINE) ? wxTE_MULTILINE : 0, *textctrlvalidator);
	DefaultChkBoxValidator dcbv(val, parentval, flags, tc);
	wxCheckBox *chk=new wxCheckBox(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, dcbv);

	sizer->Add(stat, 0, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	wxSize statsz=stat->GetSize();
	sizer->SetItemMinSize(stat, std::max(200,statsz.GetWidth()), statsz.GetHeight());
	sizer->Add(chk, 0, wxALIGN_CENTRE | wxALIGN_CENTRE_VERTICAL, 4);
	sizer->Add(tc, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	opts.emplace_front(option_item {sizer, stat, win, flags});
	opts.emplace_front(option_item {sizer, tc, win, flags});
	opts.emplace_front(option_item {sizer, chk, win, flags});
}

void settings_window::AddSettingRow_Bool(unsigned int win, wxWindow* parent, wxSizer *sizer, const wxString &name, unsigned int flags, genopt &val, genopt &parentval) {
	ValueChkBoxValidator boolvalidator(val);
	wxStaticText *stat=new wxStaticText(parent, wxID_ANY, name);
	wxCheckBox *chkval=new wxCheckBox(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, boolvalidator);
	DefaultChkBoxValidator dcbv(val, parentval, flags, 0, chkval);
	wxCheckBox *chk=new wxCheckBox(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, dcbv);

	sizer->Add(stat, 0, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	wxSize statsz=stat->GetSize();
	sizer->SetItemMinSize(stat, std::max(200,statsz.GetWidth()), statsz.GetHeight());
	sizer->Add(chk, 0, wxALIGN_CENTRE | wxALIGN_CENTRE_VERTICAL, 4);
	sizer->Add(chkval, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	opts.emplace_front(option_item {sizer, stat, win, flags});
	opts.emplace_front(option_item {sizer, chkval, win, flags});
	opts.emplace_front(option_item {sizer, chk, win, flags});
}

wxStaticBoxSizer *settings_window::AddGenoptconfSettingBlock(wxWindow* parent, wxSizer *sizer, const wxString &name, genoptconf &goc, genoptconf &parentgoc, unsigned int flags) {
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

	AddSettingRow_Bool(OPTWIN_TWITTER, parent, fgs,  wxT("Use SSL (recommended)"), flags, goc.ssl, parentgoc.ssl);
	AddSettingRow_Bool(OPTWIN_TWITTER, parent, fgs,  wxT("Use User Streams (recommended)"), flags, goc.userstreams, parentgoc.userstreams);
	AddSettingRow_String(OPTWIN_TWITTER, parent, fgs, wxT("REST API Polling Interval / seconds"), flags|DCBV_ADVOPTION, goc.restinterval, parentgoc.restinterval, wxFILTER_NUMERIC);
	AddSettingRow_String(OPTWIN_TWITTER, parent, fgs, wxT("Twitter API Consumer Key Override"), flags|DCBV_HIDDENDEFAULT|DCBV_VERYADVOPTION, goc.tokenk, parentgoc.tokenk);
	AddSettingRow_String(OPTWIN_TWITTER, parent, fgs, wxT("Twitter API Consumer Secret Override"), flags|DCBV_HIDDENDEFAULT|DCBV_VERYADVOPTION, goc.tokens, parentgoc.tokens);
	return sbox;
}


BEGIN_EVENT_TABLE(settings_window, wxDialog)
EVT_CHOICE(wxID_FILE1, settings_window::ChoiceCtrlChange)
EVT_CHECKBOX(wxID_FILE2, settings_window::ShowAdvCtrlChange)
EVT_CHECKBOX(wxID_FILE3, settings_window::ShowVeryAdvCtrlChange)
EVT_COMMAND_RANGE(4000, 4000 + OPTWIN_LAST - 1, wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, settings_window::CategoryButtonClick)
END_EVENT_TABLE()

settings_window::settings_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name, taccount *defshow)
	: wxDialog(parent, id, title, pos, size, style, name) {

	//wxPanel *panel = new wxPanel(this, -1);
	wxWindow *panel=this;
	current=0;

	hbox = new wxBoxSizer(wxHORIZONTAL);
	vbox = new wxBoxSizer(wxVERTICAL);

	hbox->Add(vbox, 1, wxALL | wxEXPAND, 0);

	btnbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(btnbox, 0, wxALL | wxALIGN_TOP , 4);

	auto addfgsizerblock = [&](wxString name, wxFlexGridSizer *&fgsr) {
		wxStaticBoxSizer *hbox1 = new wxStaticBoxSizer(wxVERTICAL, panel, name);
		fgsr = new wxFlexGridSizer(3, 2, 5);
		fgsr->SetFlexibleDirection(wxBOTH);
		fgsr->AddGrowableCol(2, 1);

		vbox->Add(hbox1, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);
		hbox1->Add(fgsr, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);

		cat_empty_sizer_op.emplace_back(fgsr, [=](bool show) {
			vbox->Show(hbox1, show);
		});
	};

	wxFlexGridSizer *fgs = 0;
	addfgsizerblock(wxT("General Settings"), fgs);

	cat_buttons.resize(OPTWIN_LAST);
	auto addbtn = [&](unsigned int id, const wxString &name) {
		wxToggleButton *btn = new wxToggleButton(panel, 4000 + id, name);
		btnbox->Add(btn, 0, wxALL, 2);
		if(id == currentcat) btn->SetValue(true);
		cat_buttons[id] = btn;
	};
	addbtn(OPTWIN_DISPLAY, wxT("Display"));
	addbtn(OPTWIN_NETWORK, wxT("Network"));
	addbtn(OPTWIN_CACHING, wxT("Caching"));
	addbtn(OPTWIN_TWITTER, wxT("Twitter"));
	addbtn(OPTWIN_SAVING, wxT("Saving"));
	addbtn(OPTWIN_FILTER, wxT("Filter"));

	wxBoxSizer *hboxfooter = new wxBoxSizer(wxHORIZONTAL);
	wxButton *okbtn=new wxButton(panel, wxID_OK, wxT("OK"));
	wxButton *cancelbtn=new wxButton(panel, wxID_CANCEL, wxT("Cancel"));
	advoptchkbox=new wxCheckBox(panel, wxID_FILE2, wxT("Show Advanced Options"));
	veryadvoptchkbox=new wxCheckBox(panel, wxID_FILE3, wxT("Show Very Advanced Options"));
	wxBoxSizer *advoptbox = new wxBoxSizer(wxVERTICAL);
	advoptbox->Add(advoptchkbox, 0, wxALL | wxALIGN_CENTRE_VERTICAL, 2);
	advoptbox->Add(veryadvoptchkbox, 0, wxALL | wxALIGN_CENTRE_VERTICAL, 2);
	hboxfooter->Add(advoptbox, 0, wxALL | wxALIGN_CENTRE_VERTICAL, 2);
	hboxfooter->AddStretchSpacer();
	hboxfooter->Add(okbtn, 0, wxALL | wxALIGN_BOTTOM | wxALIGN_RIGHT, 2);
	hboxfooter->Add(cancelbtn, 0, wxALL | wxALIGN_BOTTOM | wxALIGN_RIGHT, 2);
	opts.emplace_front(option_item {advoptbox, veryadvoptchkbox, 0, DCBV_ADVOPTION});

	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Max No. of Items to Display in Panel"), DCBV_ISGLOBALCFG, gc.gcfg.maxtweetsdisplayinpanel, gcglobdefaults.maxtweetsdisplayinpanel, wxFILTER_NUMERIC);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Date-Time Format (strftime)"), DCBV_ISGLOBALCFG, gc.gcfg.datetimeformat, gcglobdefaults.datetimeformat);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Max Profile Image Size / px"), DCBV_ISGLOBALCFG, gc.gcfg.maxpanelprofimgsize, gcglobdefaults.maxpanelprofimgsize, wxFILTER_NUMERIC);
	AddSettingRow_Bool(OPTWIN_DISPLAY, panel, fgs,  wxT("Display Native Re-Tweets"), DCBV_ISGLOBALCFG, gc.gcfg.rtdisp, gcglobdefaults.rtdisp);
	AddSettingRow_String(OPTWIN_CACHING, panel, fgs, wxT("Cached User Expiry Time / minutes"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.userexpiretimemins, gcglobdefaults.userexpiretimemins, wxFILTER_NUMERIC);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Tweet display format"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.tweetdispformat, gcglobdefaults.tweetdispformat);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("DM display format"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.dmdispformat, gcglobdefaults.dmdispformat);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Native Re-Tweet display format"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.rtdispformat, gcglobdefaults.rtdispformat);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("User display format"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.userdispformat, gcglobdefaults.userdispformat);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Tweet mouse-over format"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.mouseover_tweetdispformat, gcglobdefaults.mouseover_tweetdispformat);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("DM mouse-over format"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.mouseover_dmdispformat, gcglobdefaults.mouseover_dmdispformat);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Native Re-Tweet mouse-over format"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.mouseover_rtdispformat, gcglobdefaults.mouseover_rtdispformat);
	//AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("User mouse-over format"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.mouseover_userdispformat, gcglobdefaults.mouseover_userdispformat);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs, wxT("Highlight Colour"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.highlight_colourdelta, gcglobdefaults.highlight_colourdelta);
	AddSettingRow_Bool(OPTWIN_CACHING, panel, fgs,  wxT("Cache media image thumbnails"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.cachethumbs, gcglobdefaults.cachethumbs);
	AddSettingRow_Bool(OPTWIN_CACHING, panel, fgs,  wxT("Cache full-size media images"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.cachemedia, gcglobdefaults.cachemedia);
	AddSettingRow_Bool(OPTWIN_CACHING, panel, fgs,  wxT("Check incoming media against cache"), DCBV_ISGLOBALCFG | DCBV_VERYADVOPTION, gc.gcfg.persistentmediacache, gcglobdefaults.persistentmediacache);
	AddSettingRow_Bool(OPTWIN_TWITTER, panel, fgs,  wxT("Assume that mentions are a subset of the home timeline"), DCBV_ISGLOBALCFG | DCBV_VERYADVOPTION, gc.gcfg.assumementionistweet, gcglobdefaults.assumementionistweet);
	AddSettingRow_String(OPTWIN_SAVING, panel, fgs,  wxT("Media Image\nSave Directories\n(1 per line)"), DCBV_ISGLOBALCFG | DCBV_MULTILINE, gc.gcfg.mediasave_directorylist, gcglobdefaults.mediasave_directorylist);
	FilterTextValidator filterval(ad.incoming_filter, &gc.gcfg.incoming_filter.val);
	AddSettingRow_String(OPTWIN_FILTER, panel, fgs,  wxT("Incoming Tweet Filter\nRead Documentation Before Use"), DCBV_ISGLOBALCFG | DCBV_MULTILINE | DCBV_ADVOPTION, gc.gcfg.incoming_filter, gcglobdefaults.incoming_filter, 0, &filterval);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs,  wxT("Unhide Image Thumbnail Time / seconds"), DCBV_ISGLOBALCFG, gc.gcfg.imgthumbunhidetime, gcglobdefaults.imgthumbunhidetime, wxFILTER_NUMERIC);
	AddSettingRow_String(OPTWIN_DISPLAY, panel, fgs,  wxT("No. of tweet replies to load inline"), DCBV_ISGLOBALCFG, gc.gcfg.inlinereplyloadcount, gcglobdefaults.inlinereplyloadcount, wxFILTER_NUMERIC);

	wxFlexGridSizer *proxyfgs = 0;
	addfgsizerblock(wxT("Proxy Settings"), proxyfgs);

	AddSettingRow_Bool(OPTWIN_NETWORK, panel, proxyfgs, wxT("Use proxy settings (applies to new connections).\n(If not set, use system/environment default)"), DCBV_ISGLOBALCFG, gc.gcfg.setproxy, gcglobdefaults.setproxy);

#if LIBCURL_VERSION_NUM >= 0x071507
	wxString proxyurllabel = wxT("Proxy URL, default HTTP. Prefix with socks4:// socks4a://\nsocks5:// or socks5h:// for SOCKS proxying.");
#else
	wxString proxyurllabel = wxT("Proxy URL (HTTP only)");
#endif
	AddSettingRow_String(OPTWIN_NETWORK, panel, proxyfgs, proxyurllabel, DCBV_ISGLOBALCFG, gc.gcfg.proxyurl, gcglobdefaults.proxyurl);
	AddSettingRow_Bool(OPTWIN_NETWORK, panel, proxyfgs, wxT("Use tunnelling HTTP proxy (HTTP CONNECT)."), DCBV_ISGLOBALCFG | DCBV_VERYADVOPTION, gc.gcfg.proxyhttptunnel, gcglobdefaults.proxyhttptunnel);
	AddSettingRow_String(OPTWIN_NETWORK, panel, proxyfgs, wxT("List of host names which should not be proxied.\nSeparate with commas or newlines"), DCBV_ISGLOBALCFG | DCBV_MULTILINE, gc.gcfg.noproxylist, gcglobdefaults.noproxylist);

#if LIBCURL_VERSION_NUM >= 0x071800
	wxString netifacelabel = wxT("Outgoing network interface (interface name, IP or host).\nPrefix with if! or host! to force interface name or host/IP respectively.");
#else
	wxString netifacelabel = wxT("Outgoing network interface (interface name, IP or host)");
#endif
	AddSettingRow_String(OPTWIN_NETWORK, panel, fgs, netifacelabel, DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.netiface, gcglobdefaults.netiface);

	lb=new wxChoice(panel, wxID_FILE1);

	vbox->Add(lb, 0, wxALL, 4);

	wxStaticBoxSizer *defsbox=AddGenoptconfSettingBlock(panel, vbox, wxT("[Defaults for All Accounts]"), gc.cfg, gcdefaults, DCBV_ISGLOBALCFG);
	accmap[0]=defsbox;
	lb->Append(wxT("[Defaults for All Accounts]"), (void *) 0);
	lb->SetSelection(0);

	for(auto it=alist.begin() ; it != alist.end(); it++ ) {
		wxStaticBoxSizer *sbox=AddGenoptconfSettingBlock(panel, vbox, (*it)->dispname, (*it)->cfg, gc.cfg, 0);
		accmap[(*it).get()]=sbox;
		lb->Append((*it)->dispname, (*it).get());
		if((*it).get()==defshow) {
			current=defshow;
			lb->SetSelection(lb->GetCount()-1);
		}
		else {
			vbox->Hide(sbox);
		}
	}
	if(defshow && current!=defshow) {	//for (new) accounts not (yet) in alist
		wxStaticBoxSizer *sbox=AddGenoptconfSettingBlock(panel, vbox, defshow->dispname, defshow->cfg, gc.cfg, 0);
		accmap[defshow]=sbox;
		lb->Append(defshow->dispname, defshow);
		lb->SetSelection(lb->GetCount()-1);
		current=defshow;
	}

	if(current) vbox->Hide(defsbox);

	vbox->Add(hboxfooter, 0, wxALL | wxALIGN_BOTTOM | wxEXPAND, 0);

	OptShowHide(~0);

	panel->SetSizer(hbox);
	hbox->Fit(panel);

	initsize=GetSize();
	SetSizeHints(initsize.GetWidth(), initsize.GetHeight(), 9000, initsize.GetHeight());

	currentcat = 1;
	cat_buttons[1]->SetValue(true);
	SetSizeHints(GetSize().GetWidth(), 1);
	OptShowHide(0);
	PostOptShowHide();
}

settings_window::~settings_window() {
	UpdateAllTweets(false, true);
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		(*it)->Exec();
		(*it)->SetupRestBackfillTimer();
	}
	AccountChangeTrigger();
}

void settings_window::ChoiceCtrlChange(wxCommandEvent &event) {
	Freeze();
	SetSizeHints(GetSize().GetWidth(), 1);
	current=(taccount*) event.GetClientData();
	vbox->Show(accmap[current]);
	OptShowHide((advoptchkbox->IsChecked() ? DCBV_ADVOPTION : 0) | (veryadvoptchkbox->IsChecked() ? DCBV_VERYADVOPTION : 0));
	PostOptShowHide();
	Thaw();
}

void settings_window::ShowAdvCtrlChange(wxCommandEvent &event) {
	Freeze();
	SetSizeHints(GetSize().GetWidth(), 1);
	if(!event.IsChecked()) {
		veryadvoptchkbox->SetValue(false);
	}
	OptShowHide((event.IsChecked() ? DCBV_ADVOPTION : 0) | (veryadvoptchkbox->IsChecked() ? DCBV_VERYADVOPTION : 0));
	PostOptShowHide();
	Thaw();
}
void settings_window::ShowVeryAdvCtrlChange(wxCommandEvent &event) {
	Freeze();
	SetSizeHints(GetSize().GetWidth(), 1);
	OptShowHide((advoptchkbox->IsChecked() ? DCBV_ADVOPTION : 0) | (event.IsChecked() ? DCBV_VERYADVOPTION : 0));
	PostOptShowHide();
	Thaw();
}

void settings_window::OptShowHide(unsigned int setmask) {
	std::unordered_set<wxSizer *> sizerset;
	std::unordered_set<wxSizer *> nonempty_sizerset;
	std::vector<std::function<void()> > ops;
	std::vector<unsigned int> showcount(OPTWIN_LAST, 0);
	for(auto &it : opts) {
		unsigned int allmask = DCBV_ADVOPTION | DCBV_VERYADVOPTION;
		unsigned int maskedflags = it.flags & allmask;
		bool show;
		if(!maskedflags) show = true;
		else show = (bool) (maskedflags & setmask);
		if(show) showcount[it.cat]++;
		if(it.cat && currentcat && it.cat != currentcat) show = false;
		ops.push_back([=] { it.sizer->Show(it.win, show); });
		if(show) nonempty_sizerset.insert(it.sizer);
		sizerset.insert(it.sizer);
	}
	for(auto &it : sizerset) {
		bool isnonempty = nonempty_sizerset.count(it);
		for(auto &jt : cat_empty_sizer_op) {
			if(it == jt.first) {
				jt.second(isnonempty);
			}
		}
	}
	for(auto &it : ops) {
		it();
	}
	for(auto &it : sizerset) it->Layout();
	unsigned int i = OPTWIN_LAST;
	do {
		i--;
		bool show = (bool) showcount[i];
		if(cat_buttons[i]) btnbox->Show(cat_buttons[i], show);
		if(!show && i && i == currentcat) {
			currentcat--;
			if(cat_buttons[i]) cat_buttons[i]->SetValue(false);
			if(cat_buttons[currentcat]) cat_buttons[currentcat]->SetValue(true);
			OptShowHide(setmask);
			return;
		}
	} while(i > 0);
	btnbox->Layout();
}

void settings_window::PostOptShowHide() {
	for(auto it=accmap.begin(); it!=accmap.end(); it++) {
		if(it->first!=current) vbox->Hide(it->second);
	}
	vbox->Layout();
	GetSizer()->Fit(this);
	wxSize cursize=GetSize();
	SetSizeHints(initsize.GetWidth(), cursize.GetHeight(), 9000, cursize.GetHeight());
}

bool settings_window::TransferDataFromWindow() {
	bool retval=wxWindow::TransferDataFromWindow();
	if(retval) {
		AllUsersInheritFromParentIfUnset();
		gc.CFGParamConv();
		for(auto it=alist.begin() ; it != alist.end(); it++ ) (*it)->CFGParamConv();
	}
	return retval;
}

void settings_window::CategoryButtonClick(wxCommandEvent &event) {
	Freeze();
	if(cat_buttons[currentcat]) cat_buttons[currentcat]->SetValue(false);
	currentcat = event.GetId() - 4000;
	cat_buttons[currentcat]->SetValue(true);

	SetSizeHints(GetSize().GetWidth(), 1);
	OptShowHide((advoptchkbox->IsChecked() ? DCBV_ADVOPTION : 0) | (veryadvoptchkbox->IsChecked() ? DCBV_VERYADVOPTION : 0));
	PostOptShowHide();
	Thaw();
}
