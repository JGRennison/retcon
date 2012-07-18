struct acc_window: public wxDialog {
	acc_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name = wxT("dialogBox"));
	~acc_window();
	void AccEdit(wxCommandEvent &event);
	void AccDel(wxCommandEvent &event);
	void AccNew(wxCommandEvent &event);
	void AccClose(wxCommandEvent &event);
	void UpdateLB();
	wxListBox *lb;

	DECLARE_EVENT_TABLE()
};

struct settings_window : public wxDialog {
	wxChoice *lb;
	taccount *current;
	std::map<taccount *, wxStaticBoxSizer *> accmap;
	wxBoxSizer *vbox;

	settings_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name = wxT("dialogBox"), taccount *defshow=0);
	~settings_window();
	bool TransferDataFromWindow();
	void ChoiceCtrlChange(wxCommandEvent &event);

	DECLARE_EVENT_TABLE()
};
