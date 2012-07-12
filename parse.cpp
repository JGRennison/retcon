#include "retcon.h"
#include <cstring>

static bool CheckTransJsonValueDef_Bool(bool &var, const rapidjson::Value& val, const char *prop, bool def) {
	const rapidjson::Value &subval=val[prop];
	bool res=subval.IsBool();
	var=res?subval.GetBool():def;
	return res;
}
static bool CheckTransJsonValueDef_Uint(unsigned int &var, const rapidjson::Value& val, const char *prop, unsigned int def) {
	const rapidjson::Value &subval=val[prop];
	bool res=subval.IsUint();
	var=res?subval.GetUint():def;
	return res;
}
static bool CheckTransJsonValueDef_Uint64(uint64_t &var, const rapidjson::Value& val, const char *prop, uint64_t def) {
	const rapidjson::Value &subval=val[prop];
	bool res=subval.IsUint64();
	var=res?subval.GetUint64():def;
	return res;
}
static bool CheckTransJsonValueDef_String(std::string &var, const rapidjson::Value& val, const char *prop, const std::string &def) {
	const rapidjson::Value &subval=val[prop];
	bool res=subval.IsString();
	var=res?subval.GetString():def;
	return res;
}

void jsonparser::RestTweetUpdateParams(std::shared_ptr<tweet> t) {
	if(twit && twit->rbfs) {
		if(twit->rbfs->max_tweets_left) twit->rbfs->max_tweets_left--;
		if(!twit->rbfs->end_tweet_id || twit->rbfs->end_tweet_id>=t->id) twit->rbfs->end_tweet_id=t->id-1;
		twit->rbfs->read_again=true;
	}
}

bool jsonparser::ParseString(char *str) {
	if (dc.ParseInsitu<0>(str).HasParseError())
		return false;

	switch(type) {
		case CS_ACCVERIFY:
			tac->usercont=DoUserParse(dc);
			tac->usercont->udc_flags|=UDC_THIS_IS_ACC_USER_HINT;
			tac->dispname=wxstrstd(tac->usercont->user->name);
			tac->PostAccVerifyInit();
			break;
		case CS_USERLIST:
			if(dc.IsArray()) {
				for(rapidjson::SizeType i = 0; i < dc.Size(); i++) DoUserParse(dc[i]);
			}
			else DoUserParse(dc);
			break;
		case CS_TIMELINE:
			if(dc.IsArray()) {
				for(rapidjson::SizeType i = 0; i < dc.Size(); i++) RestTweetUpdateParams(DoTweetParse(dc[i]));
			}
			else RestTweetUpdateParams(DoTweetParse(dc));
			break;
		case CS_STREAM: {
			const rapidjson::Value& fval=dc["friends"];
			const rapidjson::Value& eval=dc["event"];
			const rapidjson::Value& ival=dc["id"];
			const rapidjson::Value& tval=dc["text"];
			if(fval.IsArray()) {
				tac->ClearUsersFollowed();
				for(rapidjson::SizeType i = 0; i < fval.Size(); i++) tac->AddUserFollowed(ad.GetUserContainerById(fval[i].GetUint64()));
				if(twit && (twit->post_action_flags&PAF_STREAM_CONN_READ_BACKFILL)) {
					tac->StartRestGetTweetBackfill(0, 0, 45);
				}
			}
			else if(eval.IsString()) {
				DoEventParse(dc);
			}
			else if(tval.IsString() && ival.IsNumber()) {	//assume that this is a tweet
				DoTweetParse(dc);
			}
			//else do nothing
		}
	}
	return true;
}

//don't use this for perspectival attributes
std::shared_ptr<userdatacontainer> jsonparser::DoUserParse(const rapidjson::Value& val) {
	auto userobj=std::make_shared<userdata>();
	userobj->acc=tac;
	userobj->id=val["id"].GetUint64();
	CheckTransJsonValueDef_Uint64(userobj->id, val, "id", 0);
	CheckTransJsonValueDef_String(userobj->name, val, "name", "");
	CheckTransJsonValueDef_String(userobj->screen_name, val, "screen_name", "");
	if(tac->ssl) CheckTransJsonValueDef_String(userobj->profile_img_url, val, "profile_image_url_https", "");
	else CheckTransJsonValueDef_String(userobj->profile_img_url, val, "profile_img_url", "");
	CheckTransJsonValueDef_Bool(userobj->isprotected, val, "protected", false);
	CheckTransJsonValueDef_String(userobj->created_at, val, "created_at", "");

	auto userdatacont = ad.GetUserContainerById(userobj->id);
	ad.UpdateUserContainer(userdatacont, userobj);

	userobj->Dump();
	return userdatacont;
}

std::shared_ptr<tweet> jsonparser::DoTweetParse(const rapidjson::Value& val) {
	auto tobj=std::make_shared<tweet>();
	tobj->acc=tac;
	tobj->id=val["id"].GetUint64();
	if(tac->max_tweet_id<tobj->id) tac->max_tweet_id=tobj->id;
	CheckTransJsonValueDef_Uint64(tobj->in_reply_to_status_id, val, "in_reply_to_status_id", 0);
	CheckTransJsonValueDef_Uint(tobj->retweet_count, val, "retweet_count", 0);
	CheckTransJsonValueDef_Bool(tobj->retweeted, val, "retweeted", false);
	CheckTransJsonValueDef_String(tobj->source, val, "source", "");
	CheckTransJsonValueDef_String(tobj->text, val, "text", "");
	CheckTransJsonValueDef_Bool(tobj->favourited, val, "favourited", false);
	if(CheckTransJsonValueDef_String(tobj->created_at, val, "created_at", "")) {
		//tobj->createtime.ParseDateTime(wxstrstd(tobj->created_at));
		//tobj->createtime.ParseFormat(wxstrstd(tobj->created_at), wxT("%a %b %d %T +0000 %Y"));
		ParseTwitterDate(0, &tobj->createtime_t, tobj->created_at);
	}
	else {
		//tobj->createtime.SetToCurrent();
		tobj->createtime_t=time(0);
	}
	if(val.HasMember("entities")) DoEntitiesParse(val["entities"], tobj);

	uint64_t userid=val["user"]["id"].GetUint64();
	if(val["user"].HasMember("screen_name")) {	//check to see if this is a trimmed user object
		tobj->user=DoUserParse(val["user"]);
	}
	else {
		tobj->user=ad.GetUserContainerById(userid);
	}

	if(tobj->user->IsReady()) {
		HandleNewTweet(tobj);
	}
	else {
		tac->pendingusers[userid]=tobj->user;
		tobj->user->pendingtweets.push_front(tobj);
	}

	tobj->Dump();

	return tobj;
}

void jsonparser::DoEventParse(const rapidjson::Value& val) {
	std::string str=val["event"].GetString();
	if(str=="user_update") {
		DoUserParse(val["target"]);
	}
	else if(str=="follow") {
		auto targ=DoUserParse(val["target"]);
		auto src=DoUserParse(val["source"]);
		if(src->id==tac->usercont->id) tac->AddUserFollowed(targ);
		if(targ->id==tac->usercont->id) tac->AddUserFollowingThis(targ);
	}
}

//returns true on success
static bool ReadEntityIndices(int &start, int &end, const rapidjson::Value& val) {
	auto &ar=val["indices"];
	if(ar.IsArray() && ar.Size()==2) {
		if(ar[(rapidjson::SizeType) 0].IsInt() && ar[1].IsInt()) {
			start=ar[(rapidjson::SizeType) 0].GetInt();
			end=ar[1].GetInt();
			return true;
		}
	}
	return false;
}

static bool ReadEntityIndices(std::shared_ptr<entity> en, const rapidjson::Value& val) {
	return ReadEntityIndices(en->start, en->end, val);
}

void jsonparser::DoEntitiesParse(const rapidjson::Value& val, std::shared_ptr<tweet> t) {
	//wxLogWarning(wxT("jsonparser::DoEntitiesParse"));
	std::map<int, std::shared_ptr<entity> > entmap;

	auto &hashtags=val["hashtags"];
	if(hashtags.IsArray()) {
		for(rapidjson::SizeType i = 0; i < hashtags.Size(); i++) {
			std::shared_ptr<entity> en = std::make_shared<entity>(ENT_HASHTAG);
			if(!ReadEntityIndices(en, hashtags[i])) continue;
			if(!CheckTransJsonValueDef_String(en->text, hashtags[i], "text", "")) continue;
			en->text="#"+en->text;
			entmap[en->start]=en;
		}
	}

	auto &urls=val["urls"];
	if(urls.IsArray()) {
		for(rapidjson::SizeType i = 0; i < urls.Size(); i++) {
			std::shared_ptr<entity> en = std::make_shared<entity>(ENT_URL);
			if(!ReadEntityIndices(en, urls[i])) continue;
			CheckTransJsonValueDef_String(en->text, urls[i], "display_url", t->text.substr(en->start, en->end-en->start));
			CheckTransJsonValueDef_String(en->fullurl, urls[i], "expanded_url", en->text);
			entmap[en->start]=en;
		}
	}

	auto &user_mentions=val["user_mentions"];
	if(user_mentions.IsArray()) {
		for(rapidjson::SizeType i = 0; i < user_mentions.Size(); i++) {
			std::shared_ptr<entity> en = std::make_shared<entity>(ENT_MENTION);
			if(!ReadEntityIndices(en, user_mentions[i])) continue;
			uint64_t userid;
			if(!CheckTransJsonValueDef_Uint64(userid, user_mentions[i], "id", 0)) continue;
			if(!CheckTransJsonValueDef_String(en->text, user_mentions[i], "screen_name", "")) continue;
			en->text="@"+en->text;
			en->user=ad.GetUserContainerById(userid);
			entmap[en->start]=en;
		}
	}

	t->entlist.clear();
	auto targ_it=t->entlist.before_begin();
	for(auto src_it=entmap.begin(); src_it!=entmap.end(); src_it++) {
		wxLogWarning(wxT("Tweet %" wxLongLongFmtSpec "d, have entity from %d to %d: %s"), t->id, src_it->second->start,
			src_it->second->end, wxstrstd(src_it->second->text).c_str());
		targ_it=t->entlist.insert_after(targ_it, src_it->second);
	}
}

void userdata::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nname: %s\nscreen_name: %s\npimg: %s\nprotected: %d"),
		id, wxstrstd(name).c_str(), wxstrstd(screen_name).c_str(), wxstrstd(profile_img_url).c_str(), isprotected);
}

void tweet::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nreply_id: %" wxLongLongFmtSpec "d\nretweet_count: %d\retweeted: %d\n"
		"source: %s\ntext: %s\nfavourited: %d\ncreated_at: %s (%s)"),
		id, in_reply_to_status_id, retweet_count, retweeted, wxstrstd(source).c_str(),
		wxstrstd(text).c_str(), favourited, wxstrstd(created_at).c_str(), wxstrstd(ctime(&createtime_t)).c_str());
}
