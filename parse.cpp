#include "retcon.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#include "rapidjson/writer.h"
#include "rapidjson/reader.h"
#pragma GCC diagnostic pop
#include <cstring>

typedef typename rapidjson::Writer<writestream> Handler;

template <typename C> bool IsType(const rapidjson::Value& val);
template <> bool IsType<bool>(const rapidjson::Value& val) { return val.IsBool(); }
template <> bool IsType<unsigned int>(const rapidjson::Value& val) { return val.IsUint(); }
template <> bool IsType<uint64_t>(const rapidjson::Value& val) { return val.IsUint64(); }
template <> bool IsType<const char*>(const rapidjson::Value& val) { return val.IsString(); }
template <> bool IsType<std::string>(const rapidjson::Value& val) { return val.IsString(); }

template <typename C> C GetType(const rapidjson::Value& val);
template <> bool GetType<bool>(const rapidjson::Value& val) { return val.GetBool(); }
template <> unsigned int GetType<unsigned int>(const rapidjson::Value& val) { return val.GetUint(); }
template <> uint64_t GetType<uint64_t>(const rapidjson::Value& val) { return val.GetUint64(); }
template <> const char* GetType<const char*>(const rapidjson::Value& val) { return val.GetString(); }
template <> std::string GetType<std::string>(const rapidjson::Value& val) { return val.GetString(); }

template <typename C, typename D> static bool CheckTransJsonValueDef(C &var, const rapidjson::Value& val, const char *prop, const D def, Handler *handler=0) {
	const rapidjson::Value &subval=val[prop];
	bool res=IsType<C>(subval);
	var=res?GetType<C>(subval):def;
	if(res && handler) {
		handler->String(prop);
		subval.Accept(*handler);
	}
	return res;
}

template <typename C, typename D> static bool CheckTransJsonValueDefFlag(C &var, C flagmask, const rapidjson::Value& val, const char *prop, bool def, Handler *handler=0) {
	const rapidjson::Value &subval=val[prop];
	bool res=IsType<bool>(subval);
	bool flagval=res?GetType<bool>(subval):def;
	if(flagval) var|=flagmask;
	else var&=~flagmask;
	if(res && handler) {
		handler->String(prop);
		subval.Accept(*handler);
	}
	return res;
}

template <typename C, typename D> static C CheckGetJsonValueDef(const rapidjson::Value& val, const char *prop, const D def, Handler *handler=0, bool *hadval=0) {
	const rapidjson::Value &subval=val[prop];
	bool res=IsType<C>(subval);
	if(res && handler) {
		handler->String(prop);
		subval.Accept(*handler);
	}
	if(hadval) *hadval=res;
	return res?GetType<C>(subval):def;
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
					tac->StartRestGetTweetBackfill(0, 0, 10);
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
	CheckTransJsonValueDef(userobj->id, val, "id", 0);
	CheckTransJsonValueDef(userobj->name, val, "name", "");
	CheckTransJsonValueDef(userobj->screen_name, val, "screen_name", "");
	if(tac->ssl) CheckTransJsonValueDef(userobj->profile_img_url, val, "profile_image_url_https", "");
	else CheckTransJsonValueDef(userobj->profile_img_url, val, "profile_img_url", "");
	CheckTransJsonValueDef(userobj->isprotected, val, "protected", false);
	CheckTransJsonValueDef(userobj->created_at, val, "created_at", "");

	auto userdatacont = ad.GetUserContainerById(userobj->id);
	ad.UpdateUserContainer(userdatacont, userobj);

	userobj->Dump();
	return userdatacont;
}

void ParsePerspectivalTweetProps(const rapidjson::Value& val, tweet_perspective *tp, Handler *handler) {
	tp->SetRetweeted(CheckGetJsonValueDef<bool>(val, "retweeted", false, handler));
	tp->SetFavourited(CheckGetJsonValueDef<bool>(val, "favourited", false, handler));
}

std::shared_ptr<tweet> jsonparser::DoTweetParse(const rapidjson::Value& val) {
	uint64_t tweetid;
	std::string json;
	writestream wr(json);
	rapidjson::Writer<writestream> jw(wr);
	jw.StartObject();
	CheckTransJsonValueDef(tweetid, val, "id", 0, &jw);
	
	//todo: make this less inefficient
	std::shared_ptr<tweet> tobj=ad.tweetobjs[tweetid];
	if(!tobj) {
		ad.tweetobjs[tweetid]=tobj=std::make_shared<tweet>();
		tobj->id=tweetid;
	}
	
	tweet_perspective *tp=tobj->AddTPToTweet(tac);
	tp->SetArrivedHere(true);
	ParsePerspectivalTweetProps(val, tp, 0);
	
	if(tac->max_tweet_id<tobj->id) tac->max_tweet_id=tobj->id;
	CheckTransJsonValueDef(tobj->in_reply_to_status_id, val, "in_reply_to_status_id", 0, &jw);
	CheckTransJsonValueDef(tobj->retweet_count, val, "retweet_count", 0, &jw);
	CheckTransJsonValueDef(tobj->source, val, "source", "", &jw);
	CheckTransJsonValueDef(tobj->text, val, "text", "", &jw);
	if(CheckTransJsonValueDef(tobj->created_at, val, "created_at", ""), &jw) {
		//tobj->createtime.ParseDateTime(wxstrstd(tobj->created_at));
		//tobj->createtime.ParseFormat(wxstrstd(tobj->created_at), wxT("%a %b %d %T +0000 %Y"));
		ParseTwitterDate(0, &tobj->createtime_t, tobj->created_at);
	}
	else {
		//tobj->createtime.SetToCurrent();
		tobj->createtime_t=time(0);
	}
	const rapidjson::Value &entv=val["entities"];
	if(entv.IsObject()) {
		DoEntitiesParse(entv, tobj);
		jw.String("entities");
		entv.Accept(jw);
	}

	jw.EndObject();
	tobj->json=std::move(json);
	wxLogWarning(wxT("Wrote json for tweet id: %" wxLongLongFmtSpec "d, %s"), tobj->id, wxstrstd(tobj->json).c_str());

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

static bool ReadEntityIndices(entity &en, const rapidjson::Value& val) {
	return ReadEntityIndices(en.start, en.end, val);
}

void jsonparser::DoEntitiesParse(const rapidjson::Value& val, std::shared_ptr<tweet> t) {
	//wxLogWarning(wxT("jsonparser::DoEntitiesParse"));

	auto &hashtags=val["hashtags"];
	if(hashtags.IsArray()) {
		for(rapidjson::SizeType i = 0; i < hashtags.Size(); i++) {
			t->entlist.emplace_front(ENT_HASHTAG);
			entity *en = &t->entlist.front();
			if(!ReadEntityIndices(*en, hashtags[i])) continue;
			if(!CheckTransJsonValueDef(en->text, hashtags[i], "text", "")) continue;
			en->text="#"+en->text;
		}
	}

	auto &urls=val["urls"];
	if(urls.IsArray()) {
		for(rapidjson::SizeType i = 0; i < urls.Size(); i++) {
			t->entlist.emplace_front(ENT_URL);
			entity *en = &t->entlist.front();
			if(!ReadEntityIndices(*en, urls[i])) continue;
			CheckTransJsonValueDef(en->text, urls[i], "display_url", t->text.substr(en->start, en->end-en->start));
			CheckTransJsonValueDef(en->fullurl, urls[i], "expanded_url", en->text);
		}
	}

	auto &user_mentions=val["user_mentions"];
	if(user_mentions.IsArray()) {
		for(rapidjson::SizeType i = 0; i < user_mentions.Size(); i++) {
			t->entlist.emplace_front(ENT_MENTION);
			entity *en = &t->entlist.front();
			if(!ReadEntityIndices(*en, user_mentions[i])) continue;
			uint64_t userid;
			if(!CheckTransJsonValueDef(userid, user_mentions[i], "id", 0)) continue;
			if(!CheckTransJsonValueDef(en->text, user_mentions[i], "screen_name", "")) continue;
			en->text="@"+en->text;
			en->user=ad.GetUserContainerById(userid);
		}
	}

	t->entlist.sort([](entity &a, entity &b){ return a.start<b.start; });
	for(auto src_it=t->entlist.begin(); src_it!=t->entlist.end(); src_it++) {
		wxLogWarning(wxT("Tweet %" wxLongLongFmtSpec "d, have entity from %d to %d: %s"), t->id, src_it->start,
			src_it->end, wxstrstd(src_it->text).c_str());
	}
}

void userdata::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nname: %s\nscreen_name: %s\npimg: %s\nprotected: %d"),
		id, wxstrstd(name).c_str(), wxstrstd(screen_name).c_str(), wxstrstd(profile_img_url).c_str(), isprotected);
}

void tweet::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nreply_id: %" wxLongLongFmtSpec "d\nretweet_count: %d\n"
		"source: %s\ntext: %s\ncreated_at: %s (%s)"),
		id, in_reply_to_status_id, retweet_count, wxstrstd(source).c_str(),
		wxstrstd(text).c_str(), wxstrstd(created_at).c_str(), wxstrstd(ctime(&createtime_t)).c_str());
	for(auto it=tp_list.begin(); it!=tp_list.end(); it++) {
		wxLogWarning(wxT("Perspectival attributes: %s\nretweeted: %d\nfavourited: %d"), it->acc->dispname.c_str(), it->IsRetweeted(), it->IsFavourited());
	}
}
