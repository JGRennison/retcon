//  retcon
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "parse.h"
#include "twit.h"
#include "db.h"
#include "log.h"
#include "log-util.h"
#include "util.h"
#include "taccount.h"
#include "alldata.h"
#include "tpanel.h"
#include "tpanel-data.h"
#include "socket-ops.h"
#include "twitcurlext.h"
#include "mainui.h"
#include "userui.h"
#include "retcon.h"
#include <cstring>
#include <limits>
#include <wx/uri.h>
#include <wx/msgdlg.h>
#include <algorithm>

template <typename C> bool IsType(const rapidjson::Value &val);
template <> bool IsType<bool>(const rapidjson::Value &val) { return val.IsBool(); }
template <> bool IsType<unsigned int>(const rapidjson::Value &val) { return val.IsUint(); }
template <> bool IsType<int>(const rapidjson::Value &val) { return val.IsInt(); }
template <> bool IsType<uint64_t>(const rapidjson::Value &val) { return val.IsUint64(); }
template <> bool IsType<int64_t>(const rapidjson::Value &val) { return val.IsInt64(); }
template <> bool IsType<const char*>(const rapidjson::Value &val) { return val.IsString(); }
template <> bool IsType<std::string>(const rapidjson::Value &val) { return val.IsString(); }

template <typename C> C GetType(const rapidjson::Value &val);
template <> bool GetType<bool>(const rapidjson::Value &val) { return val.GetBool(); }
template <> unsigned int GetType<unsigned int>(const rapidjson::Value &val) { return val.GetUint(); }
template <> int GetType<int>(const rapidjson::Value &val) { return val.GetInt(); }
template <> uint64_t GetType<uint64_t>(const rapidjson::Value &val) { return val.GetUint64(); }
template <> int64_t GetType<int64_t>(const rapidjson::Value &val) { return val.GetInt64(); }
template <> const char* GetType<const char*>(const rapidjson::Value &val) { return val.GetString(); }
template <> std::string GetType<std::string>(const rapidjson::Value &val) { return val.GetString(); }

template <typename C, typename D> static bool CheckTransJsonValueDef(C &var, const rapidjson::Value &val,
		const char *prop, const D def, Handler *handler = nullptr) {
	const rapidjson::Value &subval = val[prop];
	bool res = IsType<C>(subval);
	var = res ? GetType<C>(subval) : def;
	if(res && handler) {
		handler->String(prop);
		subval.Accept(*handler);
	}
	return res;
}

template <typename C, typename D> static bool CheckTransJsonValueDefFlag(C &var, D flagmask, const rapidjson::Value &val,
		const char *prop, bool def, Handler *handler = nullptr) {
	const rapidjson::Value &subval = val[prop];
	bool res = IsType<bool>(subval);
	bool flagval = res ? GetType<bool>(subval):def;
	if(flagval) var |= flagmask;
	else var &= ~flagmask;
	if(res && handler) {
		handler->String(prop);
		subval.Accept(*handler);
	}
	return res;
}

template <typename C, typename D> static bool CheckTransJsonValueDefTrackChanges(bool &changeflag, C &var, const rapidjson::Value &val,
		const char *prop, const D def, Handler *handler = nullptr) {
	C oldvar = var;
	bool result = CheckTransJsonValueDef(var, val, prop, def, handler);
	if(var != oldvar) changeflag = true;
	return result;
}

template <typename C, typename D> static bool CheckTransJsonValueDefFlagTrackChanges(bool &changeflag, C &var, D flagmask, const rapidjson::Value &val,
		const char *prop, bool def, Handler *handler = nullptr) {
	C oldvar = var;
	bool result = CheckTransJsonValueDefFlag(var, flagmask, val, prop, def, handler);
	if((var & flagmask) != (oldvar & flagmask)) changeflag = true;
	return result;
}

template <typename C, typename D> static C CheckGetJsonValueDef(const rapidjson::Value &val, const char *prop, const D def,
		Handler *handler = nullptr, bool *hadval = nullptr) {
	const rapidjson::Value &subval = val[prop];
	bool res = IsType<C>(subval);
	if(res && handler) {
		handler->String(prop);
		subval.Accept(*handler);
	}
	if(hadval) *hadval = res;
	return res?GetType<C>(subval):def;
}

void DisplayParseErrorMsg(rapidjson::Document &dc, const std::string &name, const char *data) {
	std::string errjson;
	writestream wr(errjson);
	Handler jw(wr);
	jw.StartArray();
	jw.String(data);
	jw.EndArray();
	LogMsgFormat(LOGT::PARSEERR, "JSON parse error: %s, message: %s, offset: %d, data:\n%s", name.c_str(), dc.GetParseError(), dc.GetErrorOffset(), cstr(errjson));
}

//if jw, caller should already have called jw->StartObject(), etc
void genjsonparser::ParseTweetStatics(const rapidjson::Value &val, tweet_ptr_p tobj, Handler *jw, bool isnew, optional_observer_ptr<dbsendmsg_list> dbmsglist, bool parse_entities) {
	CheckTransJsonValueDef(tobj->in_reply_to_status_id, val, "in_reply_to_status_id", 0, jw);
	CheckTransJsonValueDef(tobj->retweet_count, val, "retweet_count", 0);
	CheckTransJsonValueDef(tobj->favourite_count, val, "favorite_count", 0);
	CheckTransJsonValueDef(tobj->source, val, "source", "", jw);
	CheckTransJsonValueDef(tobj->text, val, "text", "", jw);

	const rapidjson::Value &entv = val["entities"];
	const rapidjson::Value &entvex = val["extended_entities"];
	if(entv.IsObject()) {
		if(parse_entities) {
			DoEntitiesParse(entv, entvex.IsObject() ? &entvex : nullptr, tobj, isnew, dbmsglist);
		}
		if(jw) {
			jw->String("entities");
			entv.Accept(*jw);
		}
	}
	if(entvex.IsObject()) {
		if(jw) {
			jw->String("extended_entities");
			entvex.Accept(*jw);
		}
	}
}

//this is paired with tweet::mkdynjson
void genjsonparser::ParseTweetDyn(const rapidjson::Value &val, tweet_ptr_p tobj) {
	const rapidjson::Value &p = val["p"];
	if(p.IsArray()) {
		for(rapidjson::SizeType i = 0; i < p.Size(); i++) {
			unsigned int dbindex=CheckGetJsonValueDef<unsigned int>(p[i], "a", 0);
			for(auto &it : alist) {
				if(it->dbindex == dbindex) {
					tweet_perspective *tp = tobj->AddTPToTweet(it);
					tp->Load(CheckGetJsonValueDef<unsigned int>(p[i], "f", 0));
					break;
				}
			}
		}
	}

	const rapidjson::Value &r = val["r"];
	if(r.IsUint()) tobj->retweet_count = r.GetUint();

	const rapidjson::Value &f = val["f"];
	if(f.IsUint()) tobj->favourite_count = f.GetUint();
}

//returns true on success
static bool ReadEntityIndices(int &start, int &end, const rapidjson::Value &val) {
	auto &ar = val["indices"];
	if(ar.IsArray() && ar.Size() == 2) {
		if(ar[(rapidjson::SizeType) 0].IsInt() && ar[1].IsInt()) {
			start = ar[(rapidjson::SizeType) 0].GetInt();
			end = ar[1].GetInt();
			return true;
		}
	}
	return false;
}

static bool ReadEntityIndices(entity &en, const rapidjson::Value &val) {
	return ReadEntityIndices(en.start, en.end, val);
}

static std::string ProcessMediaURL(std::string url, const wxURI &wxuri) {
	if(!wxuri.HasServer()) return url;

	wxString host = wxuri.GetServer();
	if(host == wxT("dropbox.com") || host.EndsWith(wxT(".dropbox.com"), nullptr)) {
		if(wxuri.GetPath().StartsWith(wxT("/s/")) && !wxuri.HasQuery()) {
			return url + "?dl=1";
		}
	}
	return url;
}

void genjsonparser::DoEntitiesParse(const rapidjson::Value &val, optional_observer_ptr<const rapidjson::Value> val_ex, tweet_ptr_p t, bool isnew, optional_observer_ptr<dbsendmsg_list> dbmsglist) {
	LogMsg(LOGT::PARSE, "jsonparser::DoEntitiesParse");

	auto &hashtags = val["hashtags"];
	auto &urls = val["urls"];
	auto &user_mentions = val["user_mentions"];

	optional_observer_ptr<const rapidjson::Value> media_array = nullptr;
	auto &media_std = val["media"];
	if(media_std.IsArray()) media_array = &media_std;
	if(val_ex) {
		auto &media_ex = (*val_ex)["media"];
		if(media_ex.IsArray()) media_array = &media_ex;
	}

	t->entlist.clear();

	unsigned int entity_count = 0;
	if(hashtags.IsArray()) entity_count += hashtags.Size();
	if(urls.IsArray()) entity_count += urls.Size();
	if(user_mentions.IsArray()) entity_count += user_mentions.Size();
	if(media_array) entity_count += media_array->Size();
	t->entlist.reserve(entity_count);

	if(hashtags.IsArray()) {
		for(rapidjson::SizeType i = 0; i < hashtags.Size(); i++) {
			t->entlist.emplace_back(ENT_HASHTAG);
			entity *en = &t->entlist.back();
			if(!ReadEntityIndices(*en, hashtags[i])) { t->entlist.pop_back(); continue; }
			if(!CheckTransJsonValueDef(en->text, hashtags[i], "text", "")) { t->entlist.pop_back(); continue; }
			en->text = "#" + en->text;
		}
	}

	auto mk_media_thumb_load_func = [&](std::string url, flagwrapper<MIDC> net_flags, flagwrapper<MELF> netloadmask) {
		netloadmask |= MELF::FORCE;
		return [url, net_flags, netloadmask](media_entity *me, flagwrapper<MELF> mel_flags) {
			struct local {
				static void try_net_dl(media_entity *me, std::string url, flagwrapper<MIDC> net_flags, flagwrapper<MELF> netloadmask, flagwrapper<MELF> mel_flags) {
					if(mel_flags & MELF::NONETLOAD) return;
					if(!(me->flags & MEF::HAVE_THUMB) && !(url.empty()) && (netloadmask & mel_flags) && !(me->flags & MEF::THUMB_NET_INPROGRESS) && !(me->flags & MEF::THUMB_FAILED)) {
						std::shared_ptr<taccount> acc = me->dm_media_acc.lock();
						mediaimgdlconn::NewConnWithOptAccOAuth(url, me->media_id, net_flags, acc.get());
					}
				};
			};

			if(me->flags & MEF::LOAD_THUMB && !(me->flags & MEF::HAVE_THUMB)) {
				//Don't bother loading a cached thumb now, that can wait
				if(mel_flags & MELF::LOADTIME) return;

				//try to load from file
				me->flags |= MEF::THUMB_NET_INPROGRESS;
				struct loadimg_job_data_struct {
					shb_iptr hash;
					media_id_type media_id;
					wxImage img;
					bool ok;
				};
				auto job_data = std::make_shared<loadimg_job_data_struct>();
				job_data->hash = me->thumb_img_sha1;
				job_data->media_id = me->media_id;

				LogMsgFormat(LOGT::FILEIOTRACE, "genjsonparser::DoEntitiesParse::mk_media_thumb_load_func, about to load cached media thumbnail from file: %s, url: %s",
						cstr(media_entity::cached_thumb_filename(job_data->media_id)), cstr(url));
				wxGetApp().EnqueueThreadJob([job_data]() {
					job_data->ok = LoadImageFromFileAndCheckHash(media_entity::cached_thumb_filename(job_data->media_id), job_data->hash, job_data->img);
				},
				[job_data, url, net_flags, netloadmask, mel_flags]() {
					auto it = ad.media_list.find(job_data->media_id);
					if(it != ad.media_list.end()) {
						media_entity &m = *(it->second);

						m.flags &= ~MEF::THUMB_NET_INPROGRESS;
						if(job_data->ok) {
							LogMsgFormat(LOGT::FILEIOTRACE, "genjsonparser::DoEntitiesParse::mk_media_thumb_load_func, successfully loaded cached media thumbnail file: %s, url: %s",
									cstr(media_entity::cached_thumb_filename(job_data->media_id)), cstr(url));
							m.thumbimg = job_data->img;
							m.flags |= MEF::HAVE_THUMB;
							for(auto &jt : m.tweet_list) {
								UpdateTweet(*jt);
							}
						}
						else {
							LogMsgFormat(LOGT::FILEIOERR, "genjsonparser::DoEntitiesParse::mk_media_thumb_load_func, cached media thumbnail file: %s, url: %s, missing, invalid or failed hash check",
									cstr(media_entity::cached_thumb_filename(job_data->media_id)), cstr(url));
							m.flags &= ~MEF::LOAD_THUMB;
							local::try_net_dl(&m, url, net_flags, netloadmask, mel_flags);
						}
					}
				});
			}
			else local::try_net_dl(me, url, net_flags, netloadmask, mel_flags);
		};
	};

	if(urls.IsArray()) {
		for(rapidjson::SizeType i = 0; i < urls.Size(); i++) {
			t->entlist.emplace_back(ENT_URL);
			entity *en = &t->entlist.back();
			if(!ReadEntityIndices(*en, urls[i])) {
				t->entlist.pop_back();
				continue;
			}
			CheckTransJsonValueDef(en->text, urls[i], "display_url", t->text.substr(en->start, en->end-en->start));
			CheckTransJsonValueDef(en->fullurl, urls[i], "expanded_url", en->text);

			wxURI wxuri(wxstrstd(en->fullurl));
			wxString end = wxuri.GetPath();
			if(end.EndsWith(wxT(".jpg")) || end.EndsWith(wxT(".png")) || end.EndsWith(wxT(".jpeg")) || end.EndsWith(wxT(".gif"))) {
				en->type = ENT_URL_IMG;
				t->flags.Set('I');

				std::string url = ProcessMediaURL(en->fullurl, wxuri);

				observer_ptr<media_entity> me = ad.img_media_map[url];
				if(!me) {
					media_id_type media_id;
					media_id.m_id = ad.next_media_id;
					ad.next_media_id++;
					media_id.t_id = t->id;
					me = media_entity::MakeNew(media_id, url);
					if(gc.cachethumbs || gc.cachemedia) DBC_InsertMedia(*me, dbmsglist);
				}
				auto res = std::find_if(me->tweet_list.begin(), me->tweet_list.end(), [&](tweet_ptr_p tt) {
					return tt->id == t->id;
				});
				if(res == me->tweet_list.end()) {
					me->tweet_list.push_front(t);
				}

				flagwrapper<MELF> netloadmask = 0;
				if(gc.autoloadthumb_full) netloadmask |= MELF::LOADTIME;
				if(gc.disploadthumb_full) netloadmask |= MELF::DISPTIME;
				me->check_load_thumb_func = mk_media_thumb_load_func(me->media_url, MIDC::FULLIMG | MIDC::THUMBIMG | MIDC::REDRAW_TWEETS, netloadmask);
				me->CheckLoadThumb(MELF::LOADTIME);
			}
		}
	}

	t->flags.Set('M', false);
	if(user_mentions.IsArray()) {
		for(rapidjson::SizeType i = 0; i < user_mentions.Size(); i++) {
			t->entlist.emplace_back(ENT_MENTION);
			entity *en = &t->entlist.back();
			if(!ReadEntityIndices(*en, user_mentions[i])) {
				t->entlist.pop_back();
				continue;
			}
			uint64_t userid;
			if(!CheckTransJsonValueDef(userid, user_mentions[i], "id", 0)) {
				t->entlist.pop_back();
				continue;
			}
			if(!CheckTransJsonValueDef(en->text, user_mentions[i], "screen_name", "")) {
				t->entlist.pop_back();
				continue;
			}
			en->user = ad.GetUserContainerById(userid);
			if(en->user->GetUser().screen_name.empty()) en->user->GetUser().screen_name = en->text;
			en->text = "@" + en->text;
			if(en->user->udc_flags & UDC::THIS_IS_ACC_USER_HINT) t->flags.Set('M', true);
			if(isnew) {
				en->user->mention_index.push_back(t->id);
				en->user->lastupdate_wrotetodb = 0;		//force flush of user to DB
			}
		}
	}

	if(media_array) {
		auto &media = *media_array;
		for(rapidjson::SizeType i = 0; i < media.Size(); i++) {
			t->flags.Set('I');
			t->entlist.emplace_back(ENT_MEDIA);
			entity *en = &t->entlist.back();
			if(!ReadEntityIndices(*en, media[i])) {
				t->entlist.pop_back();
				continue;
			}
			CheckTransJsonValueDef(en->text, media[i], "display_url", t->text.substr(en->start, en->end-en->start));
			CheckTransJsonValueDef(en->fullurl, media[i], "expanded_url", en->text);
			if(!CheckTransJsonValueDef(en->media_id.m_id, media[i], "id", 0)) {
				t->entlist.pop_back();
				continue;
			}
			en->media_id.t_id = 0;

			observer_ptr<media_entity> me = nullptr;
			auto it = ad.media_list.find(en->media_id);
			if(it != ad.media_list.end()) {
				me = it->second.get();
			}
			else {
				std::string url;
				if(t->flags.Get('s')) {
					if(!CheckTransJsonValueDef(url, media[i], "media_url_https", "")) {
						CheckTransJsonValueDef(url, media[i], "media_url", "");
					}
				}
				else {
					if(!CheckTransJsonValueDef(url, media[i], "media_url", "")) {
						CheckTransJsonValueDef(url, media[i], "media_url_https", "");
					}
				}
				url += ":large";
				me = media_entity::MakeNew(en->media_id, url);
				if(gc.cachethumbs || gc.cachemedia) DBC_InsertMedia(*me, dbmsglist);
			}

			auto res = std::find_if(me->tweet_list.begin(), me->tweet_list.end(), [&](tweet_ptr_p tt) {
				return tt->id == t->id;
			});
			if(res == me->tweet_list.end()) {
				me->tweet_list.push_front(t);
			}

			// Test this here as well as in TweetFormatProc as we may want to load the thumbnail immediately below
			if(t->flags.Get('D')) {
				// This is a media entity in a DM
				// This requires an oAuth token to access
				// Set the media entity dm_media_acc field to something sensible
				std::shared_ptr<taccount> acc = me->dm_media_acc.lock();
				t->GetUsableAccount(acc, tweet::GUAF::CHECKEXISTING | tweet::GUAF::NOERR);
				me->dm_media_acc = acc;
			}

			std::string thumburl;
			if(me->media_url.size() > 6) {
				thumburl = me->media_url.substr(0, me->media_url.size() - 6) + ":thumb";
			}
			flagwrapper<MELF> netloadmask = 0;
			if(gc.autoloadthumb_thumb) netloadmask |= MELF::LOADTIME;
			if(gc.disploadthumb_thumb) netloadmask |= MELF::DISPTIME;
			me->check_load_thumb_func = mk_media_thumb_load_func(thumburl, MIDC::THUMBIMG | MIDC::REDRAW_TWEETS, netloadmask);
			me->CheckLoadThumb(MELF::LOADTIME);
		}
	}

	std::sort(t->entlist.begin(), t->entlist.end(), [](const entity &a, const entity &b) { return a.start < b.start; });
	for(auto &src_it : t->entlist) {
		LogMsgFormat(LOGT::PARSE, "Tweet %" llFmtSpec "d, have entity from %d to %d: %s", t->id, src_it.start,
			src_it.end, cstr(src_it.text));
	}
}

void genjsonparser::ParseUserContents(const rapidjson::Value &val, userdata &userobj, bool is_ssl, bool is_db_load) {
	bool changed = false;
	CheckTransJsonValueDefTrackChanges(changed, userobj.name, val, "name", "");
	CheckTransJsonValueDefTrackChanges(changed, userobj.screen_name, val, "screen_name", "");
	CheckTransJsonValueDefTrackChanges(changed, userobj.description, val, "description", "");
	CheckTransJsonValueDefTrackChanges(changed, userobj.location, val, "location", "");
	CheckTransJsonValueDefTrackChanges(changed, userobj.userurl, val, "url", "");
	if(is_ssl) {
		if(!CheckTransJsonValueDefTrackChanges(changed, userobj.profile_img_url, val, "profile_image_url_https", "")) {
			CheckTransJsonValueDefTrackChanges(changed, userobj.profile_img_url, val, "profile_img_url", "");
		}
	}
	else {
		if(!CheckTransJsonValueDefTrackChanges(changed, userobj.profile_img_url, val, "profile_img_url", "")) {
			CheckTransJsonValueDefTrackChanges(changed, userobj.profile_img_url, val, "profile_image_url_https", "");
		}
	}
	CheckTransJsonValueDefFlagTrackChanges(changed, userobj.u_flags, userdata::UF::ISPROTECTED, val, "protected", false);
	CheckTransJsonValueDefFlagTrackChanges(changed, userobj.u_flags, userdata::UF::ISVERIFIED, val, "verified", false);
	CheckTransJsonValueDefTrackChanges(changed, userobj.followers_count, val, "followers_count", userobj.followers_count);
	CheckTransJsonValueDefTrackChanges(changed, userobj.statuses_count, val, "statuses_count", userobj.statuses_count);
	CheckTransJsonValueDefTrackChanges(changed, userobj.friends_count, val, "friends_count", userobj.friends_count);
	CheckTransJsonValueDefTrackChanges(changed, userobj.favourites_count, val, "favourites_count", userobj.favourites_count);
	if(is_db_load) {
		CheckTransJsonValueDefTrackChanges(changed, userobj.notes, val, "retcon_notes", "");
	}
	if(changed) userobj.revision_number++;
}

jsonparser::jsonparser(std::shared_ptr<taccount> a, optional_observer_ptr<twitcurlext> tw)
		: tac(a), twit(tw) { }

jsonparser::~jsonparser() {
	if(dbmsglist && !dbmsglist->msglist.empty())
		DBC_SendMessage(std::move(dbmsglist));
}

void jsonparser::RestTweetUpdateParams(const tweet &t, optional_observer_ptr<restbackfillstate> rbfs) {
	if(rbfs) {
		if(rbfs->max_tweets_left)
			rbfs->max_tweets_left--;
		if(!rbfs->end_tweet_id || rbfs->end_tweet_id >= t.id)
			rbfs->end_tweet_id = t.id - 1;
		rbfs->read_again = true;
		rbfs->lastop_recvcount++;
	}
}

void jsonparser::RestTweetPreParseUpdateParams(optional_observer_ptr<restbackfillstate> rbfs) {
	if(rbfs)
		rbfs->read_again = false;
}

void jsonparser::DoFriendLookupParse(const rapidjson::Value &val) {
	using URF = user_relationship::URF;
	time_t optime = (tac->ta_flags & taccount::TAF::STREAM_UP) ? 0 : time(nullptr);
	if(val.IsArray()) {
		for(rapidjson::SizeType i = 0; i < val.Size(); i++) {
			uint64_t userid = CheckGetJsonValueDef<uint64_t>(val[i], "id", 0);
			if(userid) {
				const rapidjson::Value& cons = val[i]["connections"];
				if(cons.IsArray()) {
					tac->SetUserRelationship(userid, URF::IFOLLOW_KNOWN | URF::FOLLOWSME_KNOWN, optime);
					for(rapidjson::SizeType j = 0; j < cons.Size(); j++) {
						if(cons[j].IsString()) {
							std::string conn_type = cons[j].GetString();
							if(conn_type == "following") tac->SetUserRelationship(userid, URF::IFOLLOW_KNOWN | URF::IFOLLOW_TRUE, optime);
							else if(conn_type == "following_requested") tac->SetUserRelationship(userid, URF::IFOLLOW_KNOWN | URF::IFOLLOW_PENDING, optime);
							else if(conn_type == "followed_by") tac->SetUserRelationship(userid, URF::FOLLOWSME_KNOWN | URF::FOLLOWSME_TRUE, optime);
							//else if(conn_type == "none") tac->SetUserRelationship(userid, URF::IFOLLOW_KNOWN | URF::FOLLOWSME_KNOWN, optime);
							//This last line is redundant, as we initialise to that value anyway
						}
					}
				}
			}
		}
	}
}

bool jsonparser::ParseString(std::string str) {
	data = std::make_shared<parse_data>();
	data->json.assign(str.begin(), str.end());
	data->json.push_back(0);
	data->source_str = std::move(str);
	rapidjson::Document &dc = data->doc;

	if(dc.ParseInsitu<0>(data->json.data()).HasParseError()) {
		DisplayParseErrorMsg(dc, "jsonparser::ParseString", data->json.data());
		return false;
	}

	return true;
}

void jsonparser::ProcessTimelineResponse(flagwrapper<JDTP> sflags, optional_observer_ptr<restbackfillstate> rbfs) {
	const rapidjson::Document &dc = data->doc;
	RestTweetPreParseUpdateParams(rbfs);
	if(dc.IsArray()) {
		dbmsglist.reset(new dbsendmsg_list());
		for(rapidjson::SizeType i = 0; i < dc.Size(); i++)
			RestTweetUpdateParams(*DoTweetParse(dc[i], sflags), rbfs);
	}
	else
		RestTweetUpdateParams(*DoTweetParse(dc, sflags), rbfs);
}

void jsonparser::ProcessUserTimelineResponse(optional_observer_ptr<restbackfillstate> rbfs) {
	const rapidjson::Document &dc = data->doc;
	RestTweetPreParseUpdateParams(rbfs);
	if(dc.IsArray()) {
		for(rapidjson::SizeType i = 0; i < dc.Size(); i++)
			DoTweetParse(dc[i], JDTP::USERTIMELINE);
	}
	else
		DoTweetParse(dc, JDTP::USERTIMELINE);
	CheckClearNoUpdateFlag_All();
}

void jsonparser::ProcessStreamResponse() {
	const rapidjson::Document &dc = data->doc;
	const rapidjson::Value &fval = dc["friends"];
	const rapidjson::Value &eval = dc["event"];
	const rapidjson::Value &ival = dc["id"];
	const rapidjson::Value &tval = dc["text"];
	const rapidjson::Value &dmval = dc["direct_message"];
	const rapidjson::Value &delval = dc["delete"];
	if(fval.IsArray()) {
		tac->ta_flags |= taccount::TAF::STREAM_UP;
		tac->last_stream_start_time = time(nullptr);

		std::vector<uint64_t> following;
		following.reserve(fval.Size());
		for(rapidjson::SizeType i = 0; i < fval.Size(); i++) {
			following.push_back(fval[i].GetUint64());
		}
		tac->HandleUserIFollowList(std::move(following), true);

		if(twit && (twit->post_action_flags & PAF::STREAM_CONN_READ_BACKFILL)) {
			tac->GetRestBackfill();
		}
		user_window::RefreshAllFollow();
		tac->GetUsersFollowingMeList();
	}
	else if(eval.IsString()) {
		DoEventParse(dc);
	}
	else if(dmval.IsObject()) {
		DoTweetParse(dmval, JDTP::ISDM);
	}
	else if(delval.IsObject() && delval["status"].IsObject()) {
		DoTweetParse(delval["status"], JDTP::DEL);
	}
	else if(ival.IsNumber() && tval.IsString() && dc["recipient"].IsObject() && dc["sender"].IsObject()) {    //assume this is a direct message
		DoTweetParse(dc, JDTP::ISDM);
	}
	else if(ival.IsNumber() && tval.IsString() && dc["user"].IsObject()) {    //assume that this is a tweet
		if(DoStreamTweetPreFilter(dc)) {
			DoTweetParse(dc);
		}
	}
	else {
		LogMsgFormat(LOGT::PARSEERR, "Stream Event Parser: Can't identify event: %s", cstr(data->source_str));
	}
}

void jsonparser::ProcessSingleTweetResponse(flagwrapper<JDTP> sflags) {
	DoTweetParse(data->doc, sflags);
}

void jsonparser::ProcessAccVerifyResponse() {
	udc_ptr auser = DoUserParse(data->doc);
	for(auto &it : alist) {
		if(it == tac) continue;
		if(auser->id == it->usercont->id) {
			wxString message = wxString::Format(wxT("Error, attempted to assign more than one account to the same twitter account: %s, @%s, id: %" wxLongLongFmtSpec "d.\n"
					"This account will be disabled, or not created. Re-authenticate or delete the offending account(s)."),
				wxstrstd(auser->GetUser().name).c_str(), wxstrstd(auser->GetUser().screen_name).c_str(), auser->id);
			LogMsg(LOGT::OTHERERR, stdstrwx(message));
			wxMessageBox(message, wxT("Authentication Error"), wxOK | wxICON_ERROR);
			tac->userenabled = false;
			return;
		}
	}

	if(tac->usercont && tac->usercont->id && tac->usercont->id != auser->id) {
		wxString message = wxString::Format(wxT("Error, attempted to re-assign account to a different twitter account.\n"
				"Attempted to assign to: %s, @%s, id: %" wxLongLongFmtSpec "d\nInstead of: %s, @%s, id: %" wxLongLongFmtSpec "d\n"
				"This account will be disabled. Re-authenticate the account to the correct twitter account."),
			wxstrstd(auser->GetUser().name).c_str(), wxstrstd(auser->GetUser().screen_name).c_str(), auser->id,
			wxstrstd(tac->usercont->GetUser().name).c_str(), wxstrstd(tac->usercont->GetUser().screen_name).c_str(), tac->usercont->id);
		LogMsg(LOGT::OTHERERR, stdstrwx(message));
		wxMessageBox(message, wxT("Authentication Error"), wxOK | wxICON_ERROR);
		tac->userenabled = false;
		return;
	}

	tac->usercont = auser;
	tac->usercont->udc_flags |= UDC::THIS_IS_ACC_USER_HINT | UDC::NON_PURGABLE;
	tac->SetName();
	tac->PostAccVerifyInit();
}

void jsonparser::ProcessUserListResponse() {
	const rapidjson::Document &dc = data->doc;
	if(dc.IsArray()) {
		dbmsglist.reset(new dbsendmsg_list());
		for(rapidjson::SizeType i = 0; i < dc.Size(); i++)
			DoUserParse(dc[i], UMPTF::TPDB_NOUPDF | UMPTF::RMV_LKPINPRGFLG);
		CheckClearNoUpdateFlag_All();
	}
	else DoUserParse(dc);
}

void jsonparser::ProcessFriendLookupResponse() {
	DoFriendLookupParse(data->doc);
	user_window::RefreshAllFollow();
}

void jsonparser::ProcessUserLookupWinResponse() {
	user_window::MkWin(DoUserParse(data->doc)->id, tac);
}

void jsonparser::ProcessGenericFriendActionResponse() {
	udc_ptr u = DoUserParse(data->doc);
	u->udc_flags &= ~UDC::FRIENDACT_IN_PROGRESS;
	tac->LookupFriendships(u->id);
}

void jsonparser::ProcessGenericUserFollowListResponse(observer_ptr<tpanelparentwin_userproplisting> win) {
	if(!win)
		return;

	const rapidjson::Document &dc = data->doc;
	if(dc.IsObject()) {
		auto &dci = dc["ids"];
		if(dci.IsArray()) {
			for(rapidjson::SizeType i = 0; i < dci.Size(); i++)
				win->PushUserIDToBack(dci[i].GetUint64());
		}
	}
	win->LoadMoreToBack(gc.maxtweetsdisplayinpanel);
}

void jsonparser::ProcessOwnFollowerListingResponse() {
	const rapidjson::Document &dc = data->doc;

	if(!dc.IsObject())
		return;
	auto &dci = dc["ids"];
	if(!dci.IsArray())
		return;

	std::vector<uint64_t> followers;
	followers.reserve(dci.Size());
	for(rapidjson::SizeType i = 0; i < dci.Size(); i++) {
		followers.push_back(dci[i].GetUint64());
	}
	int64_t nextcursor = CheckGetJsonValueDef<int64_t>(dc, "next_cursor", -1);

	tac->HandleUsersFollowingMeList(std::move(followers), nextcursor == 0); // listing is complete if next cursor is 0, otherwise there are more pages
}

std::string jsonparser::ProcessUploadMediaResponse() {
	const rapidjson::Document &dc = data->doc;

	if(!dc.IsObject())
		return "";

	return CheckGetJsonValueDef<std::string>(dc, "media_id_string", "");
}

//don't use this for perspectival attributes
udc_ptr jsonparser::DoUserParse(const rapidjson::Value &val, flagwrapper<UMPTF> umpt_flags) {
	uint64_t id;
	CheckTransJsonValueDef(id, val, "id", 0);
	auto userdatacont = ad.GetUserContainerById(id);
	if(umpt_flags & UMPTF::RMV_LKPINPRGFLG) {
		userdatacont->udc_flags &= ~UDC::LOOKUP_IN_PROGRESS;
	}

	if(ad.unloaded_db_user_ids.find(id) != ad.unloaded_db_user_ids.end()) {
		// See equivalent section in DoTweetParse for rationale

		LogMsgFormat(LOGT::PARSE | LOGT::DBTRACE, "jsonparser::DoUserParse: User id: %" llFmtSpec "d, is in DB but not loaded. Loading and deferring parse.", id);

		userdatacont->udc_flags |= UDC::BEING_LOADED_FROM_DB;

		std::unique_ptr<dbselusermsg> msg(new dbselusermsg());
		msg->id_set.insert(id);

		struct funcdata {
			udc_ptr udc;
			std::weak_ptr<taccount> acc;
			std::shared_ptr<jsonparser::parse_data> jp_data;
			const rapidjson::Value *val;
			flagwrapper<UMPTF> umpt_flags;
		};
		std::shared_ptr<funcdata> pdata = std::make_shared<funcdata>();
		pdata->udc = userdatacont;
		pdata->acc = tac;
		pdata->jp_data = this->data;
		pdata->val = &val;
		pdata->umpt_flags = umpt_flags;

		DBC_SetDBSelUserMsgHandler(*msg, [pdata](dbselusermsg &pmsg, dbconn *dbc) {
			//Do not use *this, it will have long since gone out of scope

			LogMsgFormat(LOGT::PARSE | LOGT::DBTRACE, "jsonparser::DoUserParse: User id: %" llFmtSpec "d, now doing deferred parse.", pdata->udc->id);

			DBC_DBSelUserReturnDataHandler(std::move(pmsg.data), HDBSF::NOPENDINGS);

			std::shared_ptr<taccount> acc = pdata->acc.lock();
			if(acc) {
				jsonparser jp(acc);
				jp.SetData(pdata->jp_data);
				jp.DoUserParse(*(pdata->val), pdata->umpt_flags);
			}
			else {
				LogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, "jsonparser::DoUserParse: User id: %" llFmtSpec "d, deferred parse failed as account no longer exists.", pdata->udc->id);
			}
		});
		DBC_SendMessageBatched(std::move(msg));
		return userdatacont;
	}

	userdata &userobj = userdatacont->GetUser();
	ParseUserContents(val, userobj, tac->ssl, false);
	if(!userobj.createtime) {				//this means that the object is new
		std::string created_at;
		CheckTransJsonValueDef(created_at, val, "created_at", "");
		ParseTwitterDate(0, &userobj.createtime, created_at);
		DBC_InsertUser(userdatacont, make_observer(dbmsglist));
	}

	userdatacont->MarkUpdated();
	userdatacont->CheckPendingTweets(umpt_flags);

	if(userdatacont->udc_flags & UDC::WINDOWOPEN) user_window::CheckRefresh(id, false);

	if(currentlogflags & LOGT::PARSE) userdatacont->Dump();

	return userdatacont;
}

void ParsePerspectivalTweetProps(const rapidjson::Value &val, tweet_perspective *tp, Handler *handler) {
	bool propvalue;
	if(CheckTransJsonValueDef<bool>(propvalue, val, "retweeted", false, handler))
		tp->SetRetweeted(propvalue);
	if(CheckTransJsonValueDef<bool>(propvalue, val, "favorited", false, handler))
		tp->SetFavourited(propvalue);
}

inline udc_ptr CheckParseUserObj(uint64_t id, const rapidjson::Value &val, jsonparser &jp) {
	if(val.HasMember("screen_name")) {    //check to see if this is a trimmed user object
		return jp.DoUserParse(val);
	}
	else {
		return ad.GetUserContainerById(id);
	}
}

// Returns true if tweet is OK to be used
bool jsonparser::DoStreamTweetPreFilter(const rapidjson::Value& val) {
	if(tac->stream_reply_mode == SRM::ALL_REPLIES) return true;

	uint64_t tweetid;
	CheckTransJsonValueDef(tweetid, val, "id", 0, 0);

	const rapidjson::Value& userobj = val["user"];
	if(!userobj.IsObject()) return false;
	const rapidjson::Value& useridval = userobj["id"];
	if(!useridval.IsUint64()) return false;
	uint64_t uid = useridval.GetUint64();

	auto is_userid_own_account = [&](uint64_t id) -> bool {
		auto u = ad.GetExistingUserContainerById(id);
		return (u && u->udc_flags & UDC::THIS_IS_ACC_USER_HINT);
	};

	auto do_i_follow_userid = [&](uint64_t id) -> bool {
		auto it = tac->user_relations.find(id);
		if(it != tac->user_relations.end()) {
			user_relationship &ur = it->second;
			if((ur.ur_flags & user_relationship::URF::IFOLLOW_KNOWN) && (ur.ur_flags & user_relationship::URF::IFOLLOW_TRUE)) {
				return true;
			}
		}
		return false;
	};

	const rapidjson::Value& text = val["text"];

	auto pre_bin = [&]() {
		if(currentlogflags & LOGT::FILTERTRACE) {
			std::string shorttext = text.IsString() ? truncate_tweet_text(text.GetString()) : std::string("???");
			std::string screen_name = CheckGetJsonValueDef<std::string>(userobj, "screen_name", "???");
			LogMsgFormat(LOGT::FILTERTRACE, "jsonparser::DoStreamTweetPreFilter: Binning tweet: %" llFmtSpec "d (@%s): %" llFmtSpec "d (%s)",
					uid, cstr(screen_name), tweetid, cstr(shorttext));
			if(tac->stream_reply_mode == SRM::STD_REPLIES) {
				LogMsgFormat(LOGT::FILTERERR, "jsonparser::DoStreamTweetPreFilter: Warning: Binning tweet: %" llFmtSpec "d (@%s): %" llFmtSpec "d (%s), "
						"even though we are in standard replies mode, this may (or may not) be a bug.",
						uid, cstr(screen_name), tweetid, cstr(shorttext));
			}
		}
	};

	if(is_userid_own_account(uid)) {
		// This is one of our own tweets
		return true;
	}

	bool is_reply = (text.IsString() && IsTweetAReply(text.GetString()));
	int first_reply_offset = std::numeric_limits<int>::max();
	uint64_t first_reply_uid = 0;

	if(is_reply || tac->stream_reply_mode == SRM::ALL_MENTIONS) {
		//Check user mentions
		const rapidjson::Value &entv = val["entities"];
		if(entv.IsObject()) {
			const rapidjson::Value &um = entv["user_mentions"];
			if(um.IsArray()) {
				for(rapidjson::SizeType i = 0; i < um.Size(); i++) {
					const rapidjson::Value &umi = um[i];
					if(umi.IsObject()) {
						const rapidjson::Value &umid = umi["id"];
						if(umid.IsUint64()) {
							if(tac->stream_reply_mode != SRM::STD_REPLIES) {
								if(is_userid_own_account(umid.GetUint64())) {
									// This tweet mentions us, and we're in all mentions mode
									return true;
								}
							}
							if(is_reply) {
								int start, end;
								if(ReadEntityIndices(start, end, umi)) {
									if(start < first_reply_offset) {
										first_reply_offset = start;
										first_reply_uid = umid.GetUint64();
									}
								}
							}
						}
					}
				}
				if(is_reply && first_reply_uid && tac->stream_reply_mode != SRM::ALL_MENTIONS_FOLLOWS) {
					if(!is_userid_own_account(first_reply_uid) && !do_i_follow_userid(first_reply_uid)) {
						// This is a reply to someone else who we don't follow
						pre_bin();
						return false;
					}
				}
			}
		}
	}

	if(do_i_follow_userid(uid)) {
		// This account follows the tweet author
		return true;
	}

	// Bin it
	pre_bin();
	return false;
}

tweet_ptr jsonparser::DoTweetParse(const rapidjson::Value &val, flagwrapper<JDTP> sflags) {
	uint64_t tweetid;
	if(!CheckTransJsonValueDef(tweetid, val, "id", 0, 0)) {
		LogMsgFormat(LOGT::PARSEERR, "jsonparser::DoTweetParse: No ID present in document.");
		return tweet_ptr();
	}

	tweet_ptr tobj = ad.GetTweetById(tweetid);

	if(ad.unloaded_db_tweet_ids.find(tweetid) != ad.unloaded_db_tweet_ids.end()) {
		/* Oops, we're about to parse a received tweet which is already in the database,
		 * but not loaded in memory.
		 * This is bad news as the two versions of the tweet will likely diverge.
		 * To deal with this, load the existing tweet out of the DB first, then apply the
		 * new update on top, then write back as usual.
		 * This is slightly awkward, but should occur relatively infrequently.
		 * The main culprit is user profile tweet lookups.
		 * Note that twit is not usable in a deferred parse as it'll be out of scope, use values stored in data.
		 */

		LogMsgFormat(LOGT::PARSE | LOGT::DBTRACE, "jsonparser::DoTweetParse: Tweet id: %" llFmtSpec "d, is in DB but not loaded. Loading and deferring parse.", tobj->id);

		tobj->lflags |= TLF::BEINGLOADEDFROMDB;

		std::unique_ptr<dbseltweetmsg> msg(new dbseltweetmsg());
		msg->id_set.insert(tweetid);

		struct funcdata {
			std::weak_ptr<taccount> acc;
			std::shared_ptr<jsonparser::parse_data> jp_data;
			const rapidjson::Value *val;
			flagwrapper<JDTP> sflags;
			uint64_t tweetid;
		};
		std::shared_ptr<funcdata> pdata = std::make_shared<funcdata>();
		pdata->acc = tac;
		pdata->jp_data = this->data;
		pdata->val = &val;
		pdata->sflags = sflags;
		pdata->tweetid = tweetid;

		DBC_SetDBSelTweetMsgHandler(*msg, [pdata](dbseltweetmsg &pmsg, dbconn *dbc) {
			//Do not use *this, it will have long since gone out of scope

			LogMsgFormat(LOGT::PARSE | LOGT::DBTRACE, "jsonparser::DoTweetParse: Tweet id: %" llFmtSpec "d, now doing deferred parse.", pdata->tweetid);

			DBC_HandleDBSelTweetMsg(pmsg, HDBSF::NOPENDINGS);

			std::shared_ptr<taccount> acc = pdata->acc.lock();
			if(acc) {
				jsonparser jp(acc);
				jp.SetData(pdata->jp_data);
				jp.DoTweetParse(*(pdata->val), pdata->sflags | JDTP::POSTDBLOAD);
			}
			else {
				LogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, "jsonparser::DoTweetParse: Tweet id: %" llFmtSpec "d, deferred parse failed as account no longer exists.", pdata->tweetid);
			}
		});
		DBC_SendMessageBatched(std::move(msg));
		return tobj;
	}

	sflags |= data->base_sflags;

	if(sflags & JDTP::ISDM) tobj->flags.Set('D');
	else tobj->flags.Set('T');
	if(tac->ssl) tobj->flags.Set('s');
	if(sflags & JDTP::DEL) {
		tobj->flags.Set('X');
		if(gc.markdeletedtweetsasread) {
			tobj->flags.Set('r');
			tobj->flags.Set('u', false);
		}
		tobj->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);

		if(!tobj->user || tobj->createtime == 0) {
			//delete received where tweet incomplete or not in memory before
			return tweet_ptr();
		}
	}

	//Clear net load flag
	//Even if this is not the response to the same request which set the flag, we have the tweet now
	tobj->lflags &= ~TLF::BEINGLOADEDOVERNET;

	tweet_perspective *tp = tobj->AddTPToTweet(tac);
	bool is_new_tweet_perspective = false;
	bool has_just_arrived = false;
	flagwrapper<ARRIVAL> arr = 0;

	if(!(sflags & JDTP::DEL)) {
		is_new_tweet_perspective = !tp->IsReceivedHere();
		if(!(sflags & JDTP::USERTIMELINE) && !(sflags & JDTP::CHECKPENDINGONLY) && !(sflags & JDTP::ISRTSRC)) {
			has_just_arrived = !tp->IsArrivedHere();
			tp->SetArrivedHere(true);

			if(!(sflags & JDTP::ISDM)) tac->tweet_ids.insert(tweetid);
			else tac->dm_ids.insert(tweetid);
		}
		tp->SetReceivedHere(true);
		ParsePerspectivalTweetProps(val, tp, 0);
	}
	else tp->SetRecvTypeDel(true);

	if(is_new_tweet_perspective) arr |= ARRIVAL::RECV;

	if(sflags & JDTP::FAV) tp->SetFavourited(true);
	if(sflags & JDTP::UNFAV) tp->SetFavourited(false);

	std::string json;
	if(!(sflags & JDTP::DEL)) {
		if((tobj->createtime == 0) || (sflags & JDTP::ALWAYSREPARSE)) {
			// this is a better test than merely whether the tweet object is new

			writestream wr(json);
			Handler jw(wr);
			jw.StartObject();
			ParseTweetStatics(val, tobj, &jw, true, make_observer(dbmsglist));
			jw.EndObject();
			std::string created_at;
			if(CheckTransJsonValueDef(created_at, val, "created_at", "", 0)) {
				ParseTwitterDate(0, &tobj->createtime, created_at);
			}
			else {
				tobj->createtime = time(nullptr);
			}
			auto &rtval = val["retweeted_status"];
			if(rtval.IsObject()) {
				tobj->rtsrc = DoTweetParse(rtval, sflags|JDTP::ISRTSRC);
				if(tobj->rtsrc) tobj->flags.Set('R');
			}
		}
		else {
			//These properties are also checked in ParseTweetStatics.
			//Previous versions stored the retweet count in the DB static string
			//so these should not be removed from there, as otherwise old tweets
			//in the DB will lose their retweet count info when loaded.
			CheckTransJsonValueDef(tobj->retweet_count, val, "retweet_count", 0);
			CheckTransJsonValueDef(tobj->favourite_count, val, "favorite_count", 0);
		}
	}

	auto &possiblysensitive = val["possibly_sensitive"];
	if(possiblysensitive.IsBool() && possiblysensitive.GetBool()) {
		tobj->flags.Set('P');
	}

	LogMsgFormat(LOGT::PARSE, "id: %" llFmtSpec "d, is_new_tweet_perspective: %d, has_just_arrived: %d, isdm: %d, sflags: 0x%X",
			tobj->id, is_new_tweet_perspective, has_just_arrived, !!(sflags & JDTP::ISDM), sflags);

	if(is_new_tweet_perspective) {	//this filters out duplicate tweets from the same account
		if(!(sflags & JDTP::ISDM)) {
			const rapidjson::Value& userobj = val["user"];
			if(userobj.IsObject()) {
				const rapidjson::Value& useridval = userobj["id"];
				if(useridval.IsUint64()) {
					uint64_t userid = useridval.GetUint64();
					tobj->user = CheckParseUserObj(userid, userobj, *this);
					if(tobj->user->udc_flags & UDC::THIS_IS_ACC_USER_HINT) tobj->flags.Set('O', true);
				}
			}
		}
		else {	//direct message
			auto adduserdmindex = [&](udc_ptr_p u) {
				ad.GetUserDMIndexById(u->id).AddDMId(tweetid);
				u->udc_flags |= UDC::NON_PURGABLE;
			};
			if(val["sender_id"].IsUint64() && val["sender"].IsObject()) {
				uint64_t senderid = val["sender_id"].GetUint64();
				tobj->user = CheckParseUserObj(senderid, val["sender"], *this);
				adduserdmindex(tobj->user);
			}
			if(val["recipient_id"].IsUint64() && val["recipient"].IsObject()) {
				uint64_t recipientid = val["recipient_id"].GetUint64();
				tobj->user_recipient = CheckParseUserObj(recipientid, val["recipient"], *this);
				adduserdmindex(tobj->user_recipient);
			}
		}
	}
	else UpdateTweet(*tobj);

	if(!(sflags & JDTP::CHECKPENDINGONLY) && !(sflags & JDTP::ISRTSRC) && !(sflags & JDTP::USERTIMELINE)) {
		if(sflags & JDTP::ISDM) {
			if(tobj->user_recipient.get() == tac->usercont.get()) {    //received DM
				if(tac->max_recvdm_id < tobj->id) tac->max_recvdm_id = tobj->id;
			}
			else {
				if(tac->max_sentdm_id < tobj->id) tac->max_sentdm_id = tobj->id;
				tobj->flags.Set('S');
			}
		}
		else {
			if(data->rbfs_type != RBFS_NULL) {
				if(data->rbfs_type == RBFS_TWEETS) {
					if(tac->max_tweet_id < tobj->id) tac->max_tweet_id = tobj->id;
				}
				else if(data->rbfs_type == RBFS_MENTIONS) {
					if(tac->max_mention_id < tobj->id) tac->max_mention_id = tobj->id;
				}
			}
			else {	//streaming mode
				if(tac->max_tweet_id < tobj->id) tac->max_tweet_id = tobj->id;
				if(tac->max_mention_id < tobj->id) tac->max_mention_id = tobj->id;
			}
		}
	}

	if(currentlogflags & LOGT::PARSE) tobj->Dump();

	if(tobj->lflags & TLF::SHOULDSAVEINDB) tobj->flags.Set('B');

	if(sflags & JDTP::ISRTSRC) tp->SetRecvTypeRTSrc(true);

	bool have_checked_pending = false;

	if(sflags & JDTP::CHECKPENDINGONLY) {
		tp->SetRecvTypeCPO(true);
	}
	else if(sflags & JDTP::USERTIMELINE) {
		if(data->rbfs_type != RBFS_NULL && !(sflags & JDTP::ISRTSRC)) {
			tp->SetRecvTypeUT(true);
			std::shared_ptr<tpanel> usertp = tpanelparentwin_usertweets::GetUserTweetTPanel(data->rbfs_userid, data->rbfs_type);
			if(usertp) {
				have_checked_pending = true;
				bool is_ready = tac->MarkPendingOrHandle(tobj, arr);
				if(is_ready)
					usertp->PushTweet(tobj, PUSHFLAGS::USERTL | PUSHFLAGS::BELOW);
				else
					MarkPending_TPanelMap(tobj, 0, PUSHFLAGS::USERTL | PUSHFLAGS::BELOW, &usertp);
			}
		}
	}
	else if(!(sflags & JDTP::DEL)) {
		tp->SetRecvTypeNorm(true);
		tobj->lflags |= TLF::SHOULDSAVEINDB;
		tobj->flags.Set('B');

		/* The JDTP::POSTDBLOAD test is because in the event that the program is not terminated cleanly,
		 * the tweet, once reloaded from the DB, will be marked as already arrived here, but will not be in the appropriate
		 * ID lists, in particular the timeline list, as those were not written out.
		 * If everything was written out, we would not be loading the same timeline tweet again.
		 */
		if((has_just_arrived || (sflags & JDTP::POSTDBLOAD)) && !(sflags & JDTP::ISRTSRC) && !(sflags & JDTP::USERTIMELINE)) {
			if(gc.markowntweetsasread && !tobj->flags.Get('u') && (tobj->flags.Get('O') || tobj->flags.Get('S'))) {
				//tweet is marked O or S, is own tweet or DM, mark read if not already unread
				tobj->flags.Set('r');
			}
			if(!tobj->flags.Get('r')) tobj->flags.Set('u');
			have_checked_pending = true;
			tac->MarkPendingOrHandle(tobj, arr | ARRIVAL::NEW);
		}
	}

	if(!have_checked_pending)
		tac->MarkPendingOrHandle(tobj, arr);
	flagwrapper<PENDING_BITS> res = TryUnmarkPendingTweet(tobj, 0);
	if(res)
		GenericMarkPending(tobj, res, "jsonparser::DoTweetParse");

	if(tobj->lflags & TLF::SHOULDSAVEINDB || tobj->lflags & TLF::SAVED_IN_DB) {
		if(!(tobj->lflags & TLF::SAVED_IN_DB) || (sflags & JDTP::ALWAYSREPARSE)) {
			if(json.empty()) {
				writestream wr(json);
				Handler jw(wr);
				jw.StartObject();
				ParseTweetStatics(val, tobj, &jw, false, 0, false);
				jw.EndObject();
			}
			DBC_InsertNewTweet(tobj, std::move(json), make_observer(dbmsglist));
			tobj->lflags |= TLF::SAVED_IN_DB;
		}
		else DBC_UpdateTweetDyn(tobj, make_observer(dbmsglist));
	}

	return tobj;
}

void jsonparser::DoEventParse(const rapidjson::Value &val) {
	using URF = user_relationship::URF;

	auto follow_update = [&](bool nowfollowing) {
		auto targ = DoUserParse(val["target"]);
		auto src = DoUserParse(val["source"]);

		if(src->id == tac->usercont->id) {
			URF flags = SetOrClearBits(URF::IFOLLOW_KNOWN, URF::IFOLLOW_TRUE, nowfollowing);
			tac->SetUserRelationship(targ->id, flags, 0);
			tac->NotifyUserRelationshipChange(targ->id, flags);
		}
		if(targ->id == tac->usercont->id) {
			URF flags = SetOrClearBits(URF::FOLLOWSME_KNOWN, URF::FOLLOWSME_TRUE, nowfollowing);
			tac->SetUserRelationship(src->id, flags, 0);
			tac->NotifyUserRelationshipChange(src->id, flags);
		}
	};

	auto favourite_update = [&](bool nowfavourited) {
		auto targ = DoUserParse(val["target"]);
		auto src = DoUserParse(val["source"]);

		flagwrapper<JDTP> sflags = JDTP::CHECKPENDINGONLY;
		if(src->id == tac->usercont->id) {
			// This user (un)favourited the tweet
			sflags |= nowfavourited ? JDTP::FAV : JDTP::UNFAV;
		}
		auto targ_tweet = DoTweetParse(val["target_object"], sflags);

		if(targ->id == tac->usercont->id && targ_tweet) {
			// Someone (un)favourited one of the user's tweets
			tac->NotifyTweetFavouriteEvent(targ_tweet->id, src->id, !nowfavourited);
		}
	};

	std::string str = val["event"].GetString();
	if(str == "user_update") {
		DoUserParse(val["target"]);
	}
	else if(str == "follow") {
		follow_update(true);
	}
	else if(str == "unfollow") {
		follow_update(false);
	}
	else if(str == "favorite") {
		favourite_update(true);
	}
	else if(str == "unfavorite") {
		favourite_update(false);
	}
}

void userdatacontainer::Dump() const {
	time_t now = time(nullptr);
	LogMsgFormat(LOGT::PARSE, "id: %" llFmtSpec "d, name: %s, screen_name: %s\nprofile image url: %s, cached url: %s\n"
			"protected: %d, verified: %d, udc_flags: 0x%X, last update: %" llFmtSpec "ds ago, last DB update %" llFmtSpec "ds ago",
			id, cstr(GetUser().name), cstr(GetUser().screen_name), cstr(GetUser().profile_img_url), cstr(cached_profile_img_url),
			(bool) (GetUser().u_flags & userdata::userdata::UF::ISPROTECTED), (bool) (GetUser().u_flags & userdata::userdata::UF::ISVERIFIED), udc_flags,
			(int64_t) (now - lastupdate), (int64_t) (now - lastupdate_wrotetodb));
}

void tweet::Dump() const {
	LogMsgFormat(LOGT::PARSE, "id: %" llFmtSpec "d\nreply_id: %" llFmtSpec "d\nretweet_count: %d\nfavourite_count: %d\n"
			"source: %s\ntext: %s\ncreated_at: %s",
			id, in_reply_to_status_id, retweet_count, favourite_count, cstr(source),
			cstr(text), cstr(ctime(&createtime)));
	IterateTP([&](const tweet_perspective &tp) {
		LogMsgFormat(LOGT::PARSE, "Perspectival attributes: %s\nretweeted: %d\nfavourited: %d\nFlags: %s",
				cstr(tp.acc->dispname), tp.IsRetweeted(), tp.IsFavourited(), cstr(tp.GetFlagString()));
	});
}
