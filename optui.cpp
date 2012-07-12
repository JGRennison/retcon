#include "retcon.h"

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
	for(auto it=alist.begin() ; it != alist.end(); it++ ) lb->InsertItems(1,&(*it)->name,0);
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
			alist.push_back(ta);
			UpdateLB();
			//opportunity for settings and so on goes here
			ta->enabled=true;
			ta->Exec();
		}
	}
	ta->cp.Standby(twit);
}
void acc_window::AccClose(wxCommandEvent &event) {
	EndModal(0);
}
