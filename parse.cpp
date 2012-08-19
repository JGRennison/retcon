#include "retcon.h"
#include <cstring>
#include <wx/uri.h>
#include <wx/msgdlg.h>

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

template <typename C> static bool CheckTransJsonValueDefFlag(C &var, C flagmask, const rapidjson::Value& val, const char *prop, bool def, Handler *handler=0) {
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

void DisplayParseErrorMsg(rapidjson::Document &dc, const wxString &name, const char *data) {
	std::string errjson;
	writestream wr(errjson);
	Handler jw(wr);
	jw.StartArray();
	jw.String(data);
	jw.EndArray();
	LogMsgFormat(LFT_PARSEERR, wxT("JSON parse error: %s, message: %s, offset: %d, data:\n%s"), name.c_str(), wxstrstd(dc.GetParseError()).c_str(), dc.GetErrorOffset(), wxstrstd(errjson).c_str());
}

//if jw, caller should already have called jw->StartObject(), etc
void genjsonparser::ParseTweetStatics(const rapidjson::Value& val, const std::shared_ptr<tweet> &tobj, Handler *jw, bool isnew, dbsendmsg_list *dbmsglist) {
	CheckTransJsonValueDef(tobj->in_reply_to_status_id, val, "in_reply_to_status_id", 0, jw);
	CheckTransJsonValueDef(tobj->retweet_count, val, "retweet_count", 0, jw);
	CheckTransJsonValueDef(tobj->source, val, "source", "", jw);
	CheckTransJsonValueDef(tobj->text, val, "text", "", jw);
	const rapidjson::Value &entv=val["entities"];
	if(entv.IsObject()) {
		DoEntitiesParse(entv, tobj, isnew, dbmsglist);
		if(jw) {
			jw->String("entities");
			entv.Accept(*jw);
		}
	}
}

void genjsonparser::ParseTweetDyn(const rapidjson::Value& val, const std::shared_ptr<tweet> &tobj) {
	const rapidjson::Value &p=val["p"];
	if(p.IsArray()) {
		for(rapidjson::SizeType i = 0; i < p.Size(); i++) {
			unsigned int dbindex=CheckGetJsonValueDef<unsigned int>(p[i], "a", 0);
			for(auto it=alist.begin(); it!=alist.end(); ++it) {
				if((*it)->dbindex==dbindex) {
					tweet_perspective *tp=tobj->AddTPToTweet(*it);
					tp->Load(CheckGetJsonValueDef<unsigned int>(p[i], "f", 0));
					break;
				}
			}
		}
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

void genjsonparser::DoEntitiesParse(const rapidjson::Value& val, const std::shared_ptr<tweet> &t, bool isnew, dbsendmsg_list *dbmsglist) {
	LogMsg(LFT_PARSE, wxT("jsonparser::DoEntitiesParse"));

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

				media_id_type &media_id=ad.img_media_map[en->fullurl];

				if(!media_id.m_id) {
					media_id.m_id=ad.next_media_id;
					ad.next_media_id++;
					media_id.t_id=t->id;
				}
				en->media_id=media_id;

				media_entity *me=0;
				auto it=ad.media_list.find(media_id);
				if(it==ad.media_list.end()) {
					me=&ad.media_list[media_id];
					me->media_id=media_id;
					me->media_url=en->fullurl;
					if(gc.cachethumbs || gc.cachemedia) dbc.InsertMedia(*me, dbmsglist);
				}
				else me=&it->second;

				auto res=std::find_if(me->tweet_list.begin(), me->tweet_list.end(), [&](const std::shared_ptr<tweet> &tt) {
					return tt->id==t->id;
				});
				if(res==me->tweet_list.end()) {
					me->tweet_list.push_front(t);
				}

				if(me->flags&ME_LOAD_THUMB && !(me->flags&ME_HAVE_THUMB)) {
					//try to load from file
					if(LoadImageFromFileAndCheckHash(me->cached_thumb_filename(), me->thumb_img_sha1, me->thumbimg)) me->flags|=ME_HAVE_THUMB;
				}
				if(!(me->flags&ME_HAVE_THUMB)) {
					new mediaimgdlconn(me->media_url, media_id, MIDC_FULLIMG | MIDC_THUMBIMG | MIDC_REDRAW_TWEETS);
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
			if(isnew) {
				en->user->mention_index.push_back(t->id);
				en->user->lastupdate_wrotetodb=0;		//force flush of user to DB
			}
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
			if(!CheckTransJsonValueDef(en->media_id.m_id, media[i], "id", 0)) { t->entlist.pop_front(); continue; }
			en->media_id.t_id=0;
			//auto pair = ad.media_list.emplace(en->media_id);	//emplace is not yet implemented in libstdc
			//if(pair.second) { //new element inserted
			//	media_entity &me=*(pair.first);

			media_entity *me=0;
			auto it=ad.media_list.find(en->media_id);
			if(it==ad.media_list.end()) {
				me=&ad.media_list[en->media_id];
				me->media_id=en->media_id;
				if(t->flags.Get('s')) {
					if(!CheckTransJsonValueDef(me->media_url, media[i], "media_url_https", "")) {
						CheckTransJsonValueDef(me->media_url, media[i], "media_url", "");
					}
				}
				else {
					if(!CheckTransJsonValueDef(me->media_url, media[i], "media_url", "")) {
						CheckTransJsonValueDef(me->media_url, media[i], "media_url_https", "");
					}
				}
				me->media_url+=":large";
				if(gc.cachethumbs || gc.cachemedia) dbc.InsertMedia(*me, dbmsglist);
			}
			else me=&it->second;

			auto res=std::find_if(me->tweet_list.begin(), me->tweet_list.end(), [&](const std::shared_ptr<tweet> &tt) {
				return tt->id==t->id;
			});
			if(res==me->tweet_list.end()) {
				me->tweet_list.push_front(t);
			}

			if(me->flags&ME_LOAD_THUMB && !(me->flags&ME_HAVE_THUMB)) {
				//try to load from file
				if(LoadImageFromFileAndCheckHash(me->cached_thumb_filename(), me->thumb_img_sha1, me->thumbimg)) me->flags|=ME_HAVE_THUMB;
			}
			if(!(me->flags&ME_HAVE_THUMB) && me->media_url.size()>6) {
				std::string thumburl=me->media_url.substr(0, me->media_url.size()-6)+":thumb";
				new mediaimgdlconn(thumburl, en->media_id, MIDC_THUMBIMG | MIDC_REDRAW_TWEETS);
			}
		}
	}

	t->entlist.sort([](entity &a, entity &b){ return a.start<b.start; });
	for(auto src_it=t->entlist.begin(); src_it!=t->entlist.end(); src_it++) {
		LogMsgFormat(LFT_PARSE, wxT("Tweet %" wxLongLongFmtSpec "d, have entity from %d to %d: %s"), t->id, src_it->start,
			src_it->end, wxstrstd(src_it->text).c_str());
	}
}

void genjsonparser::ParseUserContents(const rapidjson::Value& val, userdata &userobj, bool is_ssl) {
	CheckTransJsonValueDef(userobj.name, val, "name", "");
	CheckTransJsonValueDef(userobj.screen_name, val, "screen_name", "");
	CheckTransJsonValueDef(userobj.description, val, "description", "");
	CheckTransJsonValueDef(userobj.userurl, val, "url", "");
	if(is_ssl) {
		if(!CheckTransJsonValueDef(userobj.profile_img_url, val, "profile_image_url_https", "")) {
			CheckTransJsonValueDef(userobj.profile_img_url, val, "profile_img_url", "");
		}
	}
	else {
		if(!CheckTransJsonValueDef(userobj.profile_img_url, val, "profile_img_url", "")) {
			CheckTransJsonValueDef(userobj.profile_img_url, val, "profile_image_url_https", "");
		}
	}
	CheckTransJsonValueDefFlag(userobj.u_flags, (unsigned int) UF_ISPROTECTED, val, "protected", false);
	CheckTransJsonValueDefFlag(userobj.u_flags, (unsigned int) UF_ISVERIFIED, val, "verified", false);
	CheckTransJsonValueDef(userobj.followers_count, val, "followers_count", userobj.followers_count);
	CheckTransJsonValueDef(userobj.statuses_count, val, "statuses_count", userobj.statuses_count);
	CheckTransJsonValueDef(userobj.friends_count, val, "friends_count", userobj.friends_count);
}

void jsonparser::RestTweetUpdateParams(const tweet &t) {
	if(twit && twit->rbfs) {
		if(twit->rbfs->max_tweets_left) twit->rbfs->max_tweets_left--;
		if(!twit->rbfs->end_tweet_id || twit->rbfs->end_tweet_id>=t.id) twit->rbfs->end_tweet_id=t.id-1;
		twit->rbfs->read_again=true;
		twit->rbfs->lastop_recvcount++;
	}
}

void jsonparser::DoFriendLookupParse(const rapidjson::Value& val) {
	time_t optime=(tac->ta_flags&TAF_STREAM_UP)?0:time(0);
	if(val.IsArray()) {
		for(rapidjson::SizeType i = 0; i < val.Size(); i++) {
			uint64_t userid=CheckGetJsonValueDef<uint64_t>(val[i], "id", 0);
			if(userid) {
				const rapidjson::Value& cons=val[i]["connections"];
				if(cons.IsArray()) {
					tac->SetUserRelationship(userid, URF_IFOLLOW_KNOWN|URF_FOLLOWSME_KNOWN, optime);
					for(rapidjson::SizeType j = 0; j < cons.Size(); j++) {
						if(cons[j].IsString()) {
							std::string type=cons[j].GetString();
							if(type=="following") tac->SetUserRelationship(userid, URF_IFOLLOW_KNOWN|URF_IFOLLOW_TRUE, optime);
							else if(type=="following_requested") tac->SetUserRelationship(userid, URF_IFOLLOW_KNOWN|URF_IFOLLOW_PENDING, optime);
							else if(type=="followed_by") tac->SetUserRelationship(userid, URF_FOLLOWSME_KNOWN|URF_FOLLOWSME_TRUE, optime);
							//else if(type=="none") tac->SetUserRelationship(userid, URF_IFOLLOW_KNOWN|URF_FOLLOWSME_KNOWN, optime);
						}
					}
				}
			}
		}
	}
}

bool jsonparser::ParseString(const char *str, size_t len) {
	//char *json=strndup(str, len);	//not supported on all systems
	char *json=(char *) malloc(len+1);
	memcpy(json, str, len);
	json[len]=0;

	if (dc.ParseInsitu<0>(json).HasParseError()) {
		DisplayParseErrorMsg(dc, wxT("jsonparser::ParseString"), json);
		free(json);
		return false;
	}

	switch(type) {
		case CS_ACCVERIFY: {
			std::shared_ptr<userdatacontainer> auser=DoUserParse(dc);
			for(auto it=alist.begin(); it!=alist.end(); ++it) {
				if(*it==tac) continue;
				if(auser->id==(*it)->usercont->id) {
					wxString message=wxString::Format(wxT("Error, attempted to assign more than one account to the same twitter account: %s, @%s, id: %" wxLongLongFmtSpec "d.\nThis account will be disabled, or not created. Re-authenticate or delete the offending account(s)."),
						wxstrstd(auser->GetUser().name).c_str(), wxstrstd(auser->GetUser().screen_name).c_str(), auser->id);
					LogMsg(LFT_OTHERERR, message);
					wxMessageBox(message, wxT("Authentication Error"), wxOK | wxICON_ERROR);
					free(json);
					tac->userenabled=false;
					return false;
				}
			}
			
			if(tac->usercont && tac->usercont->id && tac->usercont->id!=auser->id) {
				wxString message=wxString::Format(wxT("Error, attempted to re-assign account to a different twitter account.\nAttempted to assign to: %s, @%s, id: %" wxLongLongFmtSpec "d\nInstead of: %s, @%s, id: %" wxLongLongFmtSpec "d\nThis account will be disabled. Re-authenticate the account to the correct twitter account."),
					wxstrstd(auser->GetUser().name).c_str(), wxstrstd(auser->GetUser().screen_name).c_str(), auser->id,
					wxstrstd(tac->usercont->GetUser().name).c_str(), wxstrstd(tac->usercont->GetUser().screen_name).c_str(), tac->usercont->id);
				LogMsg(LFT_OTHERERR, message);
				wxMessageBox(message, wxT("Authentication Error"), wxOK | wxICON_ERROR);
				free(json);
				tac->userenabled=false;
				return false;
			}

			tac->usercont=auser;
			tac->usercont->udc_flags|=UDC_THIS_IS_ACC_USER_HINT;
			if(tac->usercont->GetUser().name.size()) tac->dispname=wxstrstd(tac->usercont->GetUser().name);
			else tac->dispname=wxstrstd(tac->usercont->GetUser().screen_name);
			tac->PostAccVerifyInit();
			break;
		}
		case CS_USERLIST:
			if(dc.IsArray()) {
				dbmsglist=new dbsendmsg_list();
				for(rapidjson::SizeType i = 0; i < dc.Size(); i++) DoUserParse(dc[i]);
			}
			else DoUserParse(dc);
			break;
		case CS_TIMELINE:
			if(dc.IsArray()) {
				dbmsglist=new dbsendmsg_list();
				for(rapidjson::SizeType i = 0; i < dc.Size(); i++) RestTweetUpdateParams(*DoTweetParse(dc[i]));
			}
			else RestTweetUpdateParams(*DoTweetParse(dc));
			break;
		case CS_DMTIMELINE:
			if(dc.IsArray()) {
				dbmsglist=new dbsendmsg_list();
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
				tac->ta_flags|=TAF_STREAM_UP;
				tac->last_stream_start_time=time(0);
				tac->ClearUsersIFollow();
				time_t optime=0;
				for(rapidjson::SizeType i = 0; i < fval.Size(); i++) tac->SetUserRelationship(fval[i].GetUint64(), URF_IFOLLOW_KNOWN | URF_IFOLLOW_TRUE, optime);
				if(twit && (twit->post_action_flags&PAF_STREAM_CONN_READ_BACKFILL)) {
					tac->GetRestBackfill();
				}
				user_window::RefreshAllFollow();
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
			break;
		}
		case CS_FRIENDLOOKUP:
			DoFriendLookupParse(dc);
			user_window::RefreshAllFollow();
			break;
		case CS_USERLOOKUPWIN:
			user_window::MkWin(DoUserParse(dc)->id, tac);
			break;
		case CS_FRIENDACTION_FOLLOW:
		case CS_FRIENDACTION_UNFOLLOW: {
			std::shared_ptr<userdatacontainer> u=DoUserParse(dc);
			u->udc_flags&=~UDC_FRIENDACT_IN_PROGRESS;
			tac->LookupFriendships(u->id);
			break;
		}
	}
	if(dbmsglist) {
		if(!dbmsglist->msglist.empty()) dbc.SendMessage(dbmsglist);
		else delete dbmsglist;
		dbmsglist=0;
	}
	free(json);
	return true;
}

//don't use this for perspectival attributes
std::shared_ptr<userdatacontainer> jsonparser::DoUserParse(const rapidjson::Value& val) {
	uint64_t id;
	CheckTransJsonValueDef(id, val, "id", 0);
	auto userdatacont = ad.GetUserContainerById(id);
	userdata &userobj=userdatacont->GetUser();
	ParseUserContents(val, userobj, tac->ssl);
	if(!userobj.createtime) {				//this means that the object is new
		std::string created_at;
		CheckTransJsonValueDef(created_at, val, "created_at", "");
		ParseTwitterDate(0, &userobj.createtime, created_at);
		dbc.InsertUser(userdatacont, dbmsglist);
	}
	if(userdatacont->udc_flags&UDC_WINDOWOPEN) user_window::CheckRefresh(id, false);

	userdatacont->MarkUpdated();
	userdatacont->CheckPendingTweets();

	if(currentlogflags&LFT_PARSE) userdatacont->Dump();
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

std::shared_ptr<tweet> jsonparser::DoTweetParse(const rapidjson::Value& val, unsigned int sflags) {
	uint64_t tweetid;
	if(!CheckTransJsonValueDef(tweetid, val, "id", 0, 0)) return std::make_shared<tweet>();

	bool is_new_tweet;
	std::shared_ptr<tweet> &tobj=ad.GetTweetById(tweetid, &is_new_tweet);

	if(sflags&JDTP_ISDM) tobj->flags.Set('D');
	else tobj->flags.Set('T');
	if(tac->ssl) tobj->flags.Set('s');

	tweet_perspective *tp=tobj->AddTPToTweet(tac);
	bool is_new_tweet_perspective=!tp->IsArrivedHere();
	tp->SetArrivedHere(true);
	ParsePerspectivalTweetProps(val, tp, 0);

	std::string json;
	if(is_new_tweet) {
		writestream wr(json);
		Handler jw(wr);
		jw.StartObject();
		ParseTweetStatics(val, tobj, &jw, true, dbmsglist);
		std::string created_at;
		if(CheckTransJsonValueDef(created_at, val, "created_at", "", 0)) {
			ParseTwitterDate(0, &tobj->createtime, created_at);
		}
		else {
			//tobj->createtime.SetToCurrent();
			tobj->createtime=time(0);
		}
		jw.EndObject();
		auto &rtval=val["retweeted_status"];
		if(rtval.IsObject()) {
			tobj->rtsrc=DoTweetParse(rtval, sflags|JDTP_ISRTSRC);
			tobj->flags.Set('R');
		}
	}

	LogMsgFormat(LFT_PARSE, wxT("id: %" wxLongLongFmtSpec "d, is_new_tweet_perspective: %d, isdm: %d"), tobj->id, is_new_tweet_perspective, !!(sflags&JDTP_ISDM));

	if(is_new_tweet_perspective) {	//this filters out duplicate tweets from the same account
		if(!(sflags&JDTP_ISDM)) {
			if(!(sflags&JDTP_ISRTSRC)) tac->tweet_ids.insert(tweetid);
			uint64_t userid=val["user"]["id"].GetUint64();
			tobj->user=CheckParseUserObj(userid, val["user"], *this);
		}
		else {	//direct message
			tac->dm_ids.insert(tweetid);
			uint64_t senderid=val["sender_id"].GetUint64();
			uint64_t recipientid=val["recipient_id"].GetUint64();
			tobj->user=CheckParseUserObj(senderid, val["sender"], *this);
			tobj->user_recipient=CheckParseUserObj(recipientid, val["recipient"], *this);
		}
		tobj->updcf_flags|=UPDCF_USEREXPIRE;
		if(!(sflags&JDTP_ISRTSRC)) tac->MarkPendingOrHandle(tobj);
	}

	if(!(sflags&JDTP_ISRTSRC)) {
		if(sflags&JDTP_ISDM) {
			if(tobj->user_recipient.get()==tac->usercont.get()) {	//received DM
				if(tac->max_recvdm_id<tobj->id) tac->max_recvdm_id=tobj->id;
			}
			else {
				if(tac->max_sentdm_id<tobj->id) tac->max_sentdm_id=tobj->id;
				tobj->flags.Set('S');
			}
		}
		else {
			if(tac->max_tweet_id<tobj->id) tac->max_tweet_id=tobj->id;
		}
	}

	if(currentlogflags&LFT_PARSE) tobj->Dump();

	if(is_new_tweet) dbc.InsertNewTweet(tobj, std::move(json), dbmsglist);
	else dbc.UpdateTweetDyn(tobj, dbmsglist);

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
		time_t optime=0;
		if(src->id==tac->usercont->id) tac->SetUserRelationship(targ->id, URF_IFOLLOW_KNOWN | URF_IFOLLOW_TRUE, optime);
		if(targ->id==tac->usercont->id) tac->SetUserRelationship(targ->id, URF_FOLLOWSME_KNOWN | URF_FOLLOWSME_TRUE, optime);
	}
}

void userdatacontainer::Dump() {
	LogMsgFormat(LFT_PARSE, wxT("id: %" wxLongLongFmtSpec "d\nname: %s\nscreen_name: %s\npimg: %s\nprotected: %d\nverified: %d"),
		id, wxstrstd(GetUser().name).c_str(), wxstrstd(GetUser().screen_name).c_str(), wxstrstd(GetUser().profile_img_url).c_str(), (bool) (GetUser().u_flags&UF_ISPROTECTED), (bool) (GetUser().u_flags&UF_ISVERIFIED));
}

void tweet::Dump() {
	LogMsgFormat(LFT_PARSE, wxT("id: %" wxLongLongFmtSpec "d\nreply_id: %" wxLongLongFmtSpec "d\nretweet_count: %d\n"
		"source: %s\ntext: %s\ncreated_at: %s"),
		id, in_reply_to_status_id, retweet_count, wxstrstd(source).c_str(),
		wxstrstd(text).c_str(), wxstrstd(ctime(&createtime)).c_str());
	for(auto it=tp_list.begin(); it!=tp_list.end(); it++) {
		LogMsgFormat(LFT_PARSE, wxT("Perspectival attributes: %s\nretweeted: %d\nfavourited: %d"), it->acc->dispname.c_str(), it->IsRetweeted(), it->IsFavourited());
	}
}
