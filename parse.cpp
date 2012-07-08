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

bool jsonparser::ParseString(char *str) {
	if (dc.ParseInsitu<0>(str).HasParseError())
		return false;

	switch(type) {
		case CS_ACCVERIFY:
			tac->usercont=DoUserParse(dc);
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
				for(rapidjson::SizeType i = 0; i < dc.Size(); i++) DoTweetParse(dc[i]);
			}
			else DoTweetParse(dc);
			break;
		case CS_STREAM: {
			const rapidjson::Value& fval=dc["friends"];
			const rapidjson::Value& eval=dc["event"];
			const rapidjson::Value& ival=dc["id"];
			const rapidjson::Value& tval=dc["text"];
			if(fval.IsArray()) {
				tac->ClearUsersFollowed();
				for(rapidjson::SizeType i = 0; i < fval.Size(); i++) tac->AddUserFollowed(ad.GetUserContainerById(fval[i].GetUint64()));
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
		tobj->createtime.ParseDateTime(wxstrstd(tobj->created_at));
	}
	else tobj->createtime.SetToCurrent();

	bool ispending;
	uint64_t userid=val["user"]["id"].GetUint64();
	if(val["user"].HasMember("screen_name")) {	//check to see if this is a trimmed user object
		tobj->user=DoUserParse(val["user"]);
		ispending=false;
	}
	else {
		auto userobj=ad.GetUserContainerById(userid);
		if(userobj->NeedsUpdating()) {
			tac->pendingtweets[tobj->id]=tobj;
			tac->pendingusers[userid]=userobj;
			ispending=true;
		}
		else ispending=false;
	}

	if(!ispending) {
		tac->HandleNewTweet(tobj);
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

void userdata::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nname: %s\nscreen_name: %s\npimg: %s\nprotected: %d"),
		id, wxstrstd(name).c_str(), wxstrstd(screen_name).c_str(), wxstrstd(profile_img_url).c_str(), isprotected);
}

void tweet::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nreply_id: %" wxLongLongFmtSpec "d\nretweet_count: %d\retweeted: %d\n"
		"source: %s\ntext: %s\nfavourited: %d\ncreated_at: %s"),
		id, in_reply_to_status_id, retweet_count, retweeted, wxstrstd(source).c_str(),
		wxstrstd(text).c_str(), favourited, wxstrstd(created_at).c_str());
}
