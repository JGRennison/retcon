struct writestream {
	writestream(std::string &str_, size_t reshint=512 ) : str(str_) { str.clear(); str.reserve(reshint); }
	std::string &str;
	inline void Put(char ch) { str.push_back(ch); }
};

struct jsonparser {
	rapidjson::Document dc;
	std::shared_ptr<taccount> tac;
	CS_ENUMTYPE type;
	twitcurlext *twit;

	std::shared_ptr<userdatacontainer> DoUserParse(const rapidjson::Value& val);
	void DoEventParse(const rapidjson::Value& val);
	std::shared_ptr<tweet> DoTweetParse(const rapidjson::Value& val);
	void DoEntitiesParse(const rapidjson::Value& val, std::shared_ptr<tweet> t);
	void RestTweetUpdateParams(std::shared_ptr<tweet> t);

	jsonparser(CS_ENUMTYPE t, std::shared_ptr<taccount> a, twitcurlext *tw = 0 /*optional*/)
		: type(t), tac(a), twit(tw) { }
	bool ParseString(char *str);	//modifies str
};

