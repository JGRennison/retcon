#include "retcon.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#include "rapidjson/writer.h"
#include "rapidjson/reader.h"
#pragma GCC diagnostic pop
#include <cstring>
#include <wx/uri.h>

typedef typename rapidjson::Writer<writestream> Handler;

template <typename C> bool IsType(const rapidjson::Value& val);
template <> bool IsType<bool>(const rapidjson::Value& val) { return val.IsBool(); }
template <> bool IsType<unsigned int>(const rapidjson::Value& val) { return val.IsUint(); }
template <> bool IsType<int>(const rapidjson::Value& val) { return val.IsInt(); }
template <> bool IsType<uint64_t>(const rapidjson::Value& val) { return val.IsUint64(); }
template <> bool IsType<const char*>(const rapidjson::Value& val) { return val.IsString(); }
template <> bool IsType<std::string>(const rapidjson::Value& val) { return val.IsString(); }

template <typename C> C GetType(const rapidjson::Value& val);
template <> bool GetType<bool>(const rapidjson::Value& val) { return val.GetBool(); }
template <> unsigned int GetType<unsigned int>(const rapidjson::Value& val) { return val.GetUint(); }
template <> int GetType<int>(const rapidjson::Value& val) { return val.GetInt(); }
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

void jsonparser::RestTweetUpdateParams(const tweet &t) {
	if(twit && twit->rbfs) {
		if(twit->rbfs->max_tweets_left) twit->rbfs->max_tweets_left--;
		if(!twit->rbfs->end_tweet_id || twit->rbfs->end_tweet_id>=t.id) twit->rbfs->end_tweet_id=t.id-1;
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
			tac->dispname=wxstrstd(tac->usercont->GetUser().name);
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
				for(rapidjson::SizeType i = 0; i < dc.Size(); i++) RestTweetUpdateParams(*DoTweetParse(dc[i]));
			}
			else RestTweetUpdateParams(*DoTweetParse(dc));
			break;
		case CS_DMTIMELINE:
			if(dc.IsArray()) {
				for(rapidjson::SizeType i = 0; i < dc.Size(); i++) RestTweetUpdateParams(*DoTweetParse(dc[i], true));
			}
			else RestTweetUpdateParams(*DoTweetParse(dc, true));
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
					tac->StartRestGetTweetBackfill(0, 0, 30);
				}
			}
			else if(eval.IsString()) {
				DoEventParse(dc);
			}
			else if(ival.IsNumber() && tval.IsString() && dc["recipient"].IsObject() && dc["sender"].IsObject()) {	//assume this is a direct message
				DoTweetParse(dc, true);
			}
			else if(ival.IsNumber() && tval.IsString() && dc["user"].IsObject()) {	//assume that this is a tweet
				DoTweetParse(dc);
			}
			//else do nothing
		}
	}
	return true;
}

//don't use this for perspectival attributes
std::shared_ptr<userdatacontainer> jsonparser::DoUserParse(const rapidjson::Value& val) {
	uint64_t id;
	CheckTransJsonValueDef(id, val, "id", 0);
	auto userdatacont = ad.GetUserContainerById(id);
	userdata &userobj=userdatacont->GetUser();
	userobj.acc=tac;
	CheckTransJsonValueDef(userobj.name, val, "name", "");
	CheckTransJsonValueDef(userobj.screen_name, val, "screen_name", "");
	if(tac->ssl) {
		if(!CheckTransJsonValueDef(userobj.profile_img_url, val, "profile_image_url_https", "")) {
			CheckTransJsonValueDef(userobj.profile_img_url, val, "profile_img_url", "");
		}
	}
	else {
		if(!CheckTransJsonValueDef(userobj.profile_img_url, val, "profile_img_url", "")) {
			CheckTransJsonValueDef(userobj.profile_img_url, val, "profile_image_url_https", "");
		}
	}
	CheckTransJsonValueDef(userobj.isprotected, val, "protected", false);
	if(!userobj.created_at.size()) {
		CheckTransJsonValueDef(userobj.created_at, val, "created_at", "");
		ParseTwitterDate(0, &userobj.createtime_t, userobj.created_at);
	}

	userdatacont->MarkUpdated();

	userdatacont->Dump();
	return userdatacont;
}

void ParsePerspectivalTweetProps(const rapidjson::Value& val, tweet_perspective *tp, Handler *handler) {
	tp->SetRetweeted(CheckGetJsonValueDef<bool>(val, "retweeted", false, handler));
	tp->SetFavourited(CheckGetJsonValueDef<bool>(val, "favourited", false, handler));
}

inline std::shared_ptr<userdatacontainer> CheckParseUserObj(uint64_t id, const rapidjson::Value& val, jsonparser &jp) {
	if(val.HasMember("screen_name")) {	//check to see if this is a trimmed user object
		return jp.DoUserParse(val);
	}
	else {
		return ad.GetUserContainerById(id);
	}
}

std::shared_ptr<tweet> jsonparser::DoTweetParse(const rapidjson::Value& val, bool isdm) {
	uint64_t tweetid;
	if(!CheckTransJsonValueDef(tweetid, val, "id", 0, 0)) return std::make_shared<tweet>();

	std::shared_ptr<tweet> &tobj=ad.tweetobjs[tweetid];
	bool is_new_tweet=!tobj;
	if(!tobj) {
		tobj=std::make_shared<tweet>();
		tobj->id=tweetid;
	}

	if(isdm) tobj->flags.Set('D');
	else tobj->flags.Set('T');

	tweet_perspective *tp=tobj->AddTPToTweet(tac);
	bool is_new_tweet_perspective=!tp->IsArrivedHere();
	tp->SetArrivedHere(true);
	ParsePerspectivalTweetProps(val, tp, 0);

	if(tac->max_tweet_id<tobj->id) tac->max_tweet_id=tobj->id;
	if(is_new_tweet) {
		std::string json;
		writestream wr(json);
		rapidjson::Writer<writestream> jw(wr);
		jw.StartObject();
		jw.String("id");
		jw.Uint64(tobj->id);
		CheckTransJsonValueDef(tobj->in_reply_to_status_id, val, "in_reply_to_status_id", 0, &jw);
		CheckTransJsonValueDef(tobj->retweet_count, val, "retweet_count", 0, &jw);
		CheckTransJsonValueDef(tobj->source, val, "source", "", &jw);
		CheckTransJsonValueDef(tobj->text, val, "text", "", &jw);
		if(CheckTransJsonValueDef(tobj->created_at, val, "created_at", ""), &jw) {
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
		//wxLogWarning(wxT("Wrote json for tweet id: %" wxLongLongFmtSpec "d, %s"), tobj->id, wxstrstd(tobj->json).c_str());
	}

	//wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d, is_new_tweet_perspective: %d, isdm: %d"), tobj->id, is_new_tweet_perspective, isdm);

	if(is_new_tweet_perspective) {	//this filters out duplicate tweets from the same account
		if(!isdm) {
			uint64_t userid=val["user"]["id"].GetUint64();
			tobj->user=CheckParseUserObj(userid, val["user"], *this);

			if(tobj->user->IsReady()) {
				//wxLogWarning(wxT("HandleNewTweet %" wxLongLongFmtSpec "d"), tobj->id);
				HandleNewTweet(tobj);
			}
			else {
				//wxLogWarning(wxT("MarkPending %" wxLongLongFmtSpec "d, user: %" wxLongLongFmtSpec "d"), tobj->id, userid);
				tac->MarkPending(userid, tobj->user, tobj);
			}
		}
		else {	//direct message
			uint64_t senderid=val["sender_id"].GetUint64();
			uint64_t recipientid=val["recipient_id"].GetUint64();
			tobj->user=CheckParseUserObj(senderid, val["sender"], *this);
			tobj->user_recipient=CheckParseUserObj(recipientid, val["recipient"], *this);

			bool isready=true;

			if(!tobj->user->IsReady()) {
				tac->MarkPending(senderid, tobj->user, tobj);
				isready=false;
			}
			if(!tobj->user_recipient->IsReady()) {
				tac->MarkPending(recipientid, tobj->user_recipient, tobj);
				isready=false;
			}
			if(isready) {
				HandleNewTweet(tobj);
			}
		}
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

void jsonparser::DoEntitiesParse(const rapidjson::Value& val, const std::shared_ptr<tweet> &t) {
	//wxLogWarning(wxT("jsonparser::DoEntitiesParse"));

	auto &hashtags=val["hashtags"];
	if(hashtags.IsArray()) {
		for(rapidjson::SizeType i = 0; i < hashtags.Size(); i++) {
			t->entlist.emplace_front(ENT_HASHTAG);
			entity *en = &t->entlist.front();
			if(!ReadEntityIndices(*en, hashtags[i])) { t->entlist.pop_front(); continue; }
			if(!CheckTransJsonValueDef(en->text, hashtags[i], "text", "")) { t->entlist.pop_front(); continue; }
			en->text="#"+en->text;
		}
	}

	auto &urls=val["urls"];
	if(urls.IsArray()) {
		for(rapidjson::SizeType i = 0; i < urls.Size(); i++) {
			t->entlist.emplace_front(ENT_URL);
			entity *en = &t->entlist.front();
			if(!ReadEntityIndices(*en, urls[i])) { t->entlist.pop_front(); continue; }
			CheckTransJsonValueDef(en->text, urls[i], "display_url", t->text.substr(en->start, en->end-en->start));
			CheckTransJsonValueDef(en->fullurl, urls[i], "expanded_url", en->text);

			wxURI url(wxstrstd(en->fullurl));
			wxString end=url.GetPath();
			if(end.EndsWith(wxT(".jpg")) || end.EndsWith(wxT(".png")) || end.EndsWith(wxT(".jpeg")) || end.EndsWith(wxT(".gif"))) {
				en->type=ENT_URL_IMG;
				t->flags.Set('I');

				uint64_t &media_id=ad.img_media_map[en->fullurl];

				if(!media_id) {
					media_id=ad.next_media_id;
					ad.next_media_id++;
				}
				en->media_id=media_id;

				auto it=ad.media_list.find(media_id);
				if(it==ad.media_list.end()) {
					media_entity &me=ad.media_list[media_id];
					me.media_id=media_id;
					me.fullsize.Set(200, 200);
					me.media_url=en->fullurl;
					me.tweet_list.push_front(t);
					new mediaimgdlconn(me.media_url, media_id, MIDC_FULLIMG | MIDC_THUMBIMG | MIDC_REDRAW_TWEETS);
				}
				else {
					media_entity &me=it->second;
					auto res=std::find_if(me.tweet_list.begin(), me.tweet_list.end(), [&](std::shared_ptr<tweet> &tt) {
						return tt->id==t->id;
					});

					wxLogWarning(wxT("Parse: existing media image %s"), wxstrstd(me.media_url).c_str());

					if(res==me.tweet_list.end()) {
						me.tweet_list.push_front(t);
					}
				}
			}
		}
	}

	t->flags.Set('M', false);
	auto &user_mentions=val["user_mentions"];
	if(user_mentions.IsArray()) {
		for(rapidjson::SizeType i = 0; i < user_mentions.Size(); i++) {
			t->entlist.emplace_front(ENT_MENTION);
			entity *en = &t->entlist.front();
			if(!ReadEntityIndices(*en, user_mentions[i])) { t->entlist.pop_front(); continue; }
			uint64_t userid;
			if(!CheckTransJsonValueDef(userid, user_mentions[i], "id", 0)) { t->entlist.pop_front(); continue; }
			if(!CheckTransJsonValueDef(en->text, user_mentions[i], "screen_name", "")) { t->entlist.pop_front(); continue; }
			en->text="@"+en->text;
			en->user=ad.GetUserContainerById(userid);
			if(en->user->udc_flags&UDC_THIS_IS_ACC_USER_HINT) t->flags.Set('M', true);
		}
	}
	auto &media=val["media"];
	if(media.IsArray()) {
		for(rapidjson::SizeType i = 0; i < media.Size(); i++) {
			t->flags.Set('I');
			t->entlist.emplace_front(ENT_MEDIA);
			entity *en = &t->entlist.front();
			if(!ReadEntityIndices(*en, media[i])) { t->entlist.pop_front(); continue; }
			CheckTransJsonValueDef(en->text, media[i], "display_url", t->text.substr(en->start, en->end-en->start));
			CheckTransJsonValueDef(en->fullurl, media[i], "expanded_url", en->text);
			if(!CheckTransJsonValueDef(en->media_id, media[i], "id", 0)) { t->entlist.pop_front(); continue; }
			//auto pair = ad.media_list.emplace(en->media_id);	//emplace is not yet implemented in libstdc
			//if(pair.second) { //new element inserted
			//	media_entity &me=*(pair.first);
			auto it=ad.media_list.find(en->media_id);
			if(it==ad.media_list.end()) {
				media_entity &me=ad.media_list[en->media_id];
				me.media_id=en->media_id;
				if(tac->ssl) {
					if(!CheckTransJsonValueDef(me.media_url, media[i], "media_url_https", "")) {
						CheckTransJsonValueDef(me.media_url, media[i], "media_url", "");
					}
				}
				else {
					if(!CheckTransJsonValueDef(me.media_url, media[i], "media_url", "")) {
						CheckTransJsonValueDef(me.media_url, media[i], "media_url_https", "");
					}
				}
				auto &sizes=media[i]["sizes"]["large"];
				int width;
				int height;
				if(sizes.IsObject()) {
					width=CheckGetJsonValueDef<int>(sizes, "w", -1);
					height=CheckGetJsonValueDef<int>(sizes, "h", -1);
				}
				else width=height=200;
				me.fullsize.Set(width, height);

				std::string thumburl=me.media_url+":thumb";
				me.media_url+=":large";

				wxLogWarning(wxT("Parse: media image %s, w: %d, h: %d"), wxstrstd(me.media_url).c_str(), width, height);

				me.tweet_list.push_front(t);
				new mediaimgdlconn(thumburl, en->media_id, MIDC_THUMBIMG | MIDC_REDRAW_TWEETS);
			}
			else {
				media_entity &me=it->second;
				auto res=std::find_if(me.tweet_list.begin(), me.tweet_list.end(), [&](std::shared_ptr<tweet> &tt) {
					return tt->id==t->id;
				});

				wxLogWarning(wxT("Parse: existing media image %s"), wxstrstd(me.media_url).c_str());

				if(res==me.tweet_list.end()) {
					me.tweet_list.push_front(t);
				}
			}
		}
	}

	t->entlist.sort([](entity &a, entity &b){ return a.start<b.start; });
	for(auto src_it=t->entlist.begin(); src_it!=t->entlist.end(); src_it++) {
		wxLogWarning(wxT("Tweet %" wxLongLongFmtSpec "d, have entity from %d to %d: %s"), t->id, src_it->start,
			src_it->end, wxstrstd(src_it->text).c_str());
	}
}

void userdatacontainer::Dump() {
	wxLogWarning(wxT("id: %" wxLongLongFmtSpec "d\nname: %s\nscreen_name: %s\npimg: %s\nprotected: %d"),
		id, wxstrstd(GetUser().name).c_str(), wxstrstd(GetUser().screen_name).c_str(), wxstrstd(GetUser().profile_img_url).c_str(), GetUser().isprotected);
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
