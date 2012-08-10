#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#include "rapidjson/writer.h"
#include "rapidjson/reader.h"
#pragma GCC diagnostic pop

struct writestream {
	writestream(std::string &str_, size_t reshint=512 ) : str(str_) { str.clear(); str.reserve(reshint); }
	std::string &str;
	inline void Put(char ch) { str.push_back(ch); }
};

struct Handler : public rapidjson::Writer<writestream> {
	using rapidjson::Writer<writestream>::String;
	Handler& String(const std::string &str) {
		rapidjson::Writer<writestream>::String(str.c_str(), str.size());
		return *this;
	}
	Handler(writestream &wr) : rapidjson::Writer<writestream>::Writer(wr) { }
};

struct genjsonparser {
	static void ParseTweetStatics(const rapidjson::Value& val, const std::shared_ptr<tweet> &tobj, Handler *jw=0, bool isnew=false, dbsendmsg_list *dbmsglist=0);
	static void DoEntitiesParse(const rapidjson::Value& val, const std::shared_ptr<tweet> &t, bool isnew=false, dbsendmsg_list *dbmsglist=0);
	static void ParseUserContents(const rapidjson::Value& val, userdata &userobj, bool is_ssl=0);
	static void ParseTweetDyn(const rapidjson::Value& val, const std::shared_ptr<tweet> &tobj);
};

struct jsonparser : public genjsonparser {
	std::shared_ptr<taccount> tac;
	CS_ENUMTYPE type;
	twitcurlext *twit;
	rapidjson::Document dc;
	dbsendmsg_list *dbmsglist;

	std::shared_ptr<userdatacontainer> DoUserParse(const rapidjson::Value& val);
	void DoEventParse(const rapidjson::Value& val);
	std::shared_ptr<tweet> DoTweetParse(const rapidjson::Value& val, bool isdm=false);
	void RestTweetUpdateParams(const tweet &t);

	jsonparser(CS_ENUMTYPE t, std::shared_ptr<taccount> a, twitcurlext *tw = 0 /*optional*/)
		: tac(a), type(t), twit(tw), dbmsglist(0) { }
	bool ParseString(const char *str, size_t len);
	bool ParseString(const std::string &str) { return ParseString(str.c_str(), str.size()); }
};

void DisplayParseErrorMsg(rapidjson::Document &dc, const wxString &name, const char *data);
