struct genopt {
	wxString val;
	bool enable;
	void CFGWriteOutCurDir(wxConfigBase &twfc, const wxString &name);
	void CFGReadInCurDir(wxConfigBase &twfc, const wxString &name, const wxString &parent);
	void InheritFromParent(genopt &parent, bool ifunset=false);
};

struct genoptconf {
	genopt tokenk;
	genopt tokens;
	genopt ssl;
	genopt userstreams;
	genopt restinterval;
	void CFGWriteOutCurDir(wxConfigBase &twfc);
	void CFGReadInCurDir(wxConfigBase &twfc, const genoptconf &parent);
	void InheritFromParent(genoptconf &parent, bool ifunset=false);
};

struct genoptglobconf {
	genopt userexpiretimemins;
	genopt datetimeformat;
	genopt maxpanelprofimgsize;
	void CFGWriteOut(wxConfigBase &twfc);
	void CFGReadIn(wxConfigBase &twfc, const genoptglobconf &parent);
};

struct globconf {
	genoptconf cfg;
	genoptglobconf gcfg;

	unsigned long userexpiretime;
	unsigned long maxpanelprofimgsize;

	void CFGWriteOut(wxConfigBase &twfc);
	void CFGReadIn(wxConfigBase &twfc);
	void CFGParamConv();
};

void ReadAllCFGIn(wxConfigBase &twfc, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);
void WriteAllCFGOut(wxConfigBase &twfc, globconf &gc, std::list<std::shared_ptr<taccount>> &alist);
void AllUsersInheritFromParentIfUnset();

extern globconf gc;
extern genoptconf gcdefaults;
extern genoptglobconf gcglobdefaults;
