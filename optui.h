struct acc_window: public wxDialog {
	acc_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name = wxT("dialogBox"));
	~acc_window();
	void AccEdit(wxCommandEvent &event);
	void AccDel(wxCommandEvent &event);
	void AccNew(wxCommandEvent &event);
	void AccClose(wxCommandEvent &event);
	void EnDisable(wxCommandEvent &event);
	void ReAuth(wxCommandEvent &event);
	void OnSelChange(wxCommandEvent &event) ;
	void UpdateLB();
	void UpdateButtons() ;
	wxListBox *lb;
	wxButton *editbtn;
	wxButton *endisbtn;
	wxButton *reauthbtn;
	wxButton *delbtn;

	DECLARE_EVENT_TABLE()
};

struct settings_window : public wxDialog {
	wxChoice *lb;
	taccount *current;
	std::map<taccount *, wxStaticBoxSizer *> accmap;
	wxBoxSizer *vbox;
	wxBoxSizer *hbox;
	wxSize initsize;
	std::forward_list<std::pair<wxSizer *,wxWindow *>> advopts;
	std::forward_list<std::pair<wxSizer *,wxWindow *>> veryadvopts;
	wxCheckBox *advoptchkbox;
	wxCheckBox *veryadvoptchkbox;

	settings_window(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style, const wxString& name = wxT("dialogBox"), taccount *defshow=0);
	~settings_window();
	bool TransferDataFromWindow();
	void ChoiceCtrlChange(wxCommandEvent &event);
	void ShowAdvCtrlChange(wxCommandEvent &event);
	void ShowVeryAdvCtrlChange(wxCommandEvent &event);
	void AddSettingRow_String(wxWindow* parent, wxSizer *sizer, const wxString &name, unsigned int flags, genopt &val, genopt &parentval, long style=wxFILTER_NONE, wxValidator *textctrlvalidator=0);
	void AddSettingRow_Bool(wxWindow* parent, wxSizer *sizer, const wxString &name, unsigned int flags, genopt &val, genopt &parentval);
	wxStaticBoxSizer *AddGenoptconfSettingBlock(wxWindow* parent, wxSizer *sizer, const wxString &name, genoptconf &goc, genoptconf &parentgoc, unsigned int flags);
	void AdvOptShowHide(const std::forward_list<std::pair<wxSizer *,wxWindow *>> &opts, bool show);
	void PostAdvOptShowHide();

	DECLARE_EVENT_TABLE()
};
