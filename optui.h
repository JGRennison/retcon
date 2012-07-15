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
	settings_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name = wxT("dialogBox"));
	~settings_window();
	
	
	DECLARE_EVENT_TABLE()
};
