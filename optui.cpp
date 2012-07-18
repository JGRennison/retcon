#include "retcon.h"
#include <wx/stattext.h>

BEGIN_EVENT_TABLE(acc_window, wxDialog)
	EVT_BUTTON(wxID_PROPERTIES, acc_window::AccEdit)
	EVT_BUTTON(wxID_DELETE, acc_window::AccDel)
	EVT_BUTTON(wxID_NEW, acc_window::AccNew)
	EVT_BUTTON(wxID_CLOSE, acc_window::AccClose)
END_EVENT_TABLE()

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
	wxButton *editbtn=new wxButton(panel, wxID_PROPERTIES, wxT("Edit"));
	wxButton *delbtn=new wxButton(panel, wxID_DELETE, wxT("Delete"));
	wxButton *newbtn=new wxButton(panel, wxID_NEW, wxT("Add account"));
	wxButton *clsbtn=new wxButton(panel, wxID_CLOSE, wxT("Close"));

	vbox->Add(hbox1, 0, wxALL | wxEXPAND , 4);
	vbox->Add(hbox2, 0, wxALL | wxEXPAND , 4);
	hbox1->Add(lb, 1, wxALL | wxALIGN_LEFT, 4);
	hbox1->Add(vboxr, 0, wxALL, 4);
	vboxr->Add(editbtn, 0, wxALIGN_TOP, 0);
	vboxr->Add(delbtn, 0, wxALIGN_TOP, 0);
	hbox2->Add(newbtn, 0, wxALIGN_LEFT, 0);
	hbox2->AddStretchSpacer(1);
	hbox2->Add(clsbtn, 0, wxALIGN_RIGHT, 0);

	panel->SetSizer(vbox);
	vbox->Fit(panel);
}

acc_window::~acc_window() {

}

void acc_window::UpdateLB() {
	lb->Set(0, 0);
	for(auto it=alist.begin() ; it != alist.end(); it++ ) lb->InsertItems(1,&(*it)->dispname,0);
}

void acc_window::AccEdit(wxCommandEvent &event) {

}
void acc_window::AccDel(wxCommandEvent &event) {

}
void acc_window::AccNew(wxCommandEvent &event) {
	std::shared_ptr<taccount> ta(new taccount(&gc.cfg));
	ta->enabled=false;
	ta->dispname=wxT("<new account>");
	//opportunity for OAuth settings and so on goes here
	twitcurlext *twit=ta->cp.GetConn();
	twit->TwInit(ta);
	if(ta->TwDoOAuth(this, *twit)) {
		if(twit->TwSyncStartupAccVerify()) {
			ta->name=wxString::Format(wxT("%" wxLongLongFmtSpec "d-%d"),ta->usercont->id,time(0));
			alist.push_back(ta);
			UpdateLB();
			//opportunity for settings and so on goes here
			ta->enabled=true;
			ta->Exec();
		}
	}
	twit->TwDeInit();
	ta->cp.Standby(twit);
}
void acc_window::AccClose(wxCommandEvent &event) {
	EndModal(0);
}

enum {
	DCBV_HIDDENDEFAULT	= 1<<0,
	DCBV_ISGLOBALCFG	= 1<<1,
	DCBV_ADVOPTION		= 1<<2,
	DCBV_VERYADVOPTION	= 1<<3,
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
		//wxLogWarning(wxT("DefaultChkBoxValidator::statechange %p %p %d"), chk, txtctrl, chk->GetValue());
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

void settings_window::AddSettingRow_String(wxWindow* parent, wxSizer *sizer, const wxString &name, unsigned int flags, genopt &val, genopt &parentval, long style, wxValidator *textctrlvalidator) {
	wxTextValidator deftv(style, &val.val);
	if(!textctrlvalidator) textctrlvalidator=&deftv;
	wxStaticText *stat=new wxStaticText(parent, wxID_ANY, name);
	wxTextCtrl *tc=new wxTextCtrl(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, *textctrlvalidator);
	DefaultChkBoxValidator dcbv(val, parentval, flags, tc);
	wxCheckBox *chk=new wxCheckBox(parent, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0, dcbv);

	sizer->Add(stat, 0, wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	wxSize statsz=stat->GetSize();
	sizer->SetItemMinSize(stat, std::max(200,statsz.GetWidth()), statsz.GetHeight());
	sizer->Add(chk, 0, wxALIGN_CENTRE | wxALIGN_CENTRE_VERTICAL, 4);
	sizer->Add(tc, 0, wxEXPAND | wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL, 4);
	if(flags&DCBV_ADVOPTION) {
		advopts.push_front(std::make_pair(sizer, stat));
		advopts.push_front(std::make_pair(sizer, tc));
		advopts.push_front(std::make_pair(sizer, chk));
	}
	if(flags&DCBV_VERYADVOPTION) {
		veryadvopts.push_front(std::make_pair(sizer, stat));
		veryadvopts.push_front(std::make_pair(sizer, tc));
		veryadvopts.push_front(std::make_pair(sizer, chk));
	}
}

void settings_window::AddSettingRow_Bool(wxWindow* parent, wxSizer *sizer, const wxString &name, unsigned int flags, genopt &val, genopt &parentval) {
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
	if(flags&DCBV_ADVOPTION) {
		advopts.push_front(std::make_pair(sizer, stat));
		advopts.push_front(std::make_pair(sizer, chkval));
		advopts.push_front(std::make_pair(sizer, chk));
	}
	if(flags&DCBV_VERYADVOPTION) {
		veryadvopts.push_front(std::make_pair(sizer, stat));
		veryadvopts.push_front(std::make_pair(sizer, chkval));
		veryadvopts.push_front(std::make_pair(sizer, chk));
	}
}

wxStaticBoxSizer *settings_window::AddGenoptconfSettingBlock(wxWindow* parent, wxSizer *sizer, const wxString &name, genoptconf &goc, genoptconf &parentgoc, unsigned int flags) {
	wxStaticBoxSizer *sbox = new wxStaticBoxSizer(wxVERTICAL, parent, wxT("Account Settings - ") + name);
	wxFlexGridSizer *fgs = new wxFlexGridSizer(3, 2, 5);
	fgs->SetFlexibleDirection(wxHORIZONTAL);
	fgs->AddGrowableCol(2, 1);
	sizer->Add(sbox, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);
	sbox->Add(fgs, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);

	AddSettingRow_Bool(parent, fgs,  wxT("Use SSL (recommended)"), flags, goc.ssl, parentgoc.ssl);
	AddSettingRow_Bool(parent, fgs,  wxT("Use User Streams (recommended)"), flags, goc.userstreams, parentgoc.userstreams);
	AddSettingRow_String(parent, fgs, wxT("REST API Polling Interval / seconds"), flags|DCBV_ADVOPTION, goc.restinterval, parentgoc.restinterval, wxFILTER_NUMERIC);
	AddSettingRow_String(parent, fgs, wxT("Twitter API Consumer Key Override"), flags|DCBV_HIDDENDEFAULT|DCBV_VERYADVOPTION, goc.tokenk, parentgoc.tokenk);
	AddSettingRow_String(parent, fgs, wxT("Twitter API Consumer Secret Override"), flags|DCBV_HIDDENDEFAULT|DCBV_VERYADVOPTION, goc.tokens, parentgoc.tokens);
	return sbox;
}


BEGIN_EVENT_TABLE(settings_window, wxDialog)
EVT_CHOICE(wxID_FILE1, settings_window::ChoiceCtrlChange)
EVT_CHECKBOX(wxID_FILE2, settings_window::ShowAdvCtrlChange)
EVT_CHECKBOX(wxID_FILE3, settings_window::ShowVeryAdvCtrlChange)
END_EVENT_TABLE()

settings_window::settings_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name, taccount *defshow)
	: wxDialog(parent, id, title, pos, size, style, name) {

	//wxPanel *panel = new wxPanel(this, -1);
	wxWindow *panel=this;
	current=0;

	hbox = new wxBoxSizer(wxHORIZONTAL);
	vbox = new wxBoxSizer(wxVERTICAL);
	wxStaticBoxSizer *hbox1 = new wxStaticBoxSizer(wxVERTICAL, panel, wxT("General Settings"));
	wxFlexGridSizer *fgs = new wxFlexGridSizer(3, 2, 5);
	fgs->SetFlexibleDirection(wxHORIZONTAL);
	fgs->AddGrowableCol(2, 1);
	hbox->Add(vbox, 1, wxALL | wxEXPAND, 0);
	vbox->Add(hbox1, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);
	hbox1->Add(fgs, 0, wxALL | wxEXPAND | wxALIGN_TOP , 4);

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
	advopts.push_front(std::make_pair(advoptbox, veryadvoptchkbox));

	AddSettingRow_String(panel, fgs, wxT("Date-time format (strftime)"), DCBV_ISGLOBALCFG, gc.gcfg.datetimeformat, gcglobdefaults.datetimeformat);
	AddSettingRow_String(panel, fgs, wxT("Max profile image size / px"), DCBV_ISGLOBALCFG, gc.gcfg.maxpanelprofimgsize, gcglobdefaults.maxpanelprofimgsize, wxFILTER_NUMERIC);
	AddSettingRow_String(panel, fgs, wxT("Cached User Expiry Time / min"), DCBV_ISGLOBALCFG | DCBV_ADVOPTION, gc.gcfg.userexpiretimemins, gcglobdefaults.userexpiretimemins, wxFILTER_NUMERIC);

	lb=new wxChoice(panel, wxID_FILE1);

	vbox->Add(lb, 0, wxALL, 4);

	wxStaticBoxSizer *defsbox=AddGenoptconfSettingBlock(panel, vbox, wxT("[Global Defaults]"), gc.cfg, gcdefaults, DCBV_ISGLOBALCFG);
	accmap[0]=defsbox;
	lb->Append(wxT("[Global Defaults]"), (void *) 0);
	lb->SetSelection(0);

	for(auto it=alist.begin() ; it != alist.end(); it++ ) {
		wxStaticBoxSizer *sbox=AddGenoptconfSettingBlock(panel, vbox, (*it)->dispname, (*it)->cfg, gc.cfg, 0);
		accmap[(*it).get()]=sbox;
		lb->Append((*it)->dispname, (*it).get());
		if((*it).get()==defshow) {
			current=defshow;
		}
		else {
			vbox->Hide(sbox);
		}
	}

	if(current) vbox->Hide(defsbox);

	vbox->Add(hboxfooter, 0, wxALL | wxALIGN_BOTTOM | wxEXPAND, 0);

	AdvOptShowHide(advopts, false);
	AdvOptShowHide(veryadvopts, false);

	panel->SetSizer(hbox);
	hbox->Fit(panel);

	initsize=GetSize();
	SetSizeHints(initsize.GetWidth(), initsize.GetHeight(), 9000, initsize.GetHeight());
	//InitDialog();
}

settings_window::~settings_window() {

}

void settings_window::ChoiceCtrlChange(wxCommandEvent &event) {
	Freeze();
	vbox->Hide(accmap[current]);
	current=(taccount*) event.GetClientData();
	vbox->Show(accmap[current]);

	vbox->Layout();
	Thaw();
}

void settings_window::ShowAdvCtrlChange(wxCommandEvent &event) {
	Freeze();
	SetSizeHints(-1, -1);
	AdvOptShowHide(advopts, event.IsChecked());
	if(!event.IsChecked()) {
		veryadvoptchkbox->SetValue(false);
		AdvOptShowHide(veryadvopts, false);
	}
	PostAdvOptShowHide();
	Thaw();
}
void settings_window::ShowVeryAdvCtrlChange(wxCommandEvent &event) {
	Freeze();
	SetSizeHints(-1, -1);
	AdvOptShowHide(veryadvopts, event.IsChecked());
	PostAdvOptShowHide();
	Thaw();
}

void settings_window::AdvOptShowHide(const std::forward_list<std::pair<wxSizer *,wxWindow *>> &opts, bool show) {
	std::unordered_set<wxSizer *> sizerset;
	for(auto it=opts.begin(); it!=opts.end(); it++) {
		wxSizer *sz=it->first;
		wxWindow *win=it->second;
		sz->Show(win, show);
		sizerset.insert(sz);
	}
	for(auto it=sizerset.begin(); it!=sizerset.end(); it++) {
		(*it)->Layout();
	}
}

void settings_window::PostAdvOptShowHide() {
	for(auto it=accmap.begin(); it!=accmap.end(); it++) {
		if(it->first && it->first!=current) vbox->Hide(it->second);
	}
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
