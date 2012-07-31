typedef unsigned int logflagtype;

enum {
	LFT_CURLVERB	= 1<<0,
	LFT_PARSE	= 1<<1,
	LFT_AUTH	= 1<<2,
	LFT_SOCKTRACE	= 1<<3,
	LFT_SOCKERR	= 1<<4,
	LFT_TPANEL	= 1<<5,
	LFT_TWITACT	= 1<<6,
	LFT_DBTRACE	= 1<<7,
	LFT_ZLIBTRACE	= 1<<8,
};

enum logflagdefs {
	lfd_stringmask=LFT_CURLVERB|LFT_PARSE|LFT_AUTH|LFT_SOCKTRACE|LFT_SOCKERR|LFT_TPANEL|LFT_TWITACT|LFT_DBTRACE|LFT_ZLIBTRACE,
	lfd_allmask=LFT_CURLVERB|LFT_PARSE|LFT_AUTH|LFT_SOCKTRACE|LFT_SOCKERR|LFT_TPANEL|LFT_TWITACT|LFT_DBTRACE|LFT_ZLIBTRACE,
	lfd_defaultwin=LFT_SOCKERR,
};

extern logflagtype currentlogflags;

struct log_object {
	logflagtype lo_flags;
	virtual void log_str(logflagtype logflags, const wxString &str)=0;
	log_object(logflagtype flagmask);
	virtual ~log_object();
};

struct log_window : public log_object, public wxFrame {
	std::queue<wxString> pending;
	bool isshown;
	wxTextCtrl *txtct;

	void log_str(logflagtype logflags, const wxString &str);
	log_window(wxWindow *parent, logflagtype flagmask, bool show=true);
	~log_window();
	void LWShow(bool shown=true);
	bool OnFrameClose(wxFrame *frame);
};

struct log_file : public log_object {
	FILE *fp;
	bool closefpondel;
	log_file(logflagtype flagmask, const char *filename);
	log_file(logflagtype flagmask, FILE *fp_, bool closefpondel_=false);
	~log_file();
	void log_str(logflagtype logflags, const wxString &str);
};

void LogMsgRaw(logflagtype logflags, const wxString &str);
void LogMsgProcess(logflagtype logflags, const wxString &str);
void Update_currentlogflags();

#define LogMsg(l, s) if( currentlogflags & l ) LogMsgProcess(l, s)
#define LogMsgFormat(l, ...) if( currentlogflags & l ) LogMsgProcess(l, wxString::Format(__VA_ARGS__))

extern log_window *globallogwindow;
logflagtype StrToLogFlags(const wxString &str);
