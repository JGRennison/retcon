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
#include "json-util.h"
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
#include <pcre.h>

using namespace parse_util;

void parse_util::DisplayParseErrorMsg(rapidjson::Document &dc, const std::string &name, const char *data) {
	std::string errjson;
	writestream wr(errjson);
	Handler jw(wr);
	jw.StartArray();
	jw.String(data);
	jw.EndArray();
	LogMsgFormat(LOGT::PARSEERR, "JSON parse error: %s, message: %s, offset: %d, data:\n%s", name.c_str(), dc.GetParseError(), dc.GetErrorOffset(), cstr(errjson));
}

bool parse_util::ParseStringInPlace(rapidjson::Document &dc, char *mutable_string, const std::string &name) {
	if (dc.ParseInsitu<0>(mutable_string).HasParseError()) {
		DisplayParseErrorMsg(dc, name, mutable_string);
		return false;
	}
	return true;
}

//if jw, caller should already have called jw->StartObject(), etc
void genjsonparser::ParseTweetStatics(const rapidjson::Value &val, tweet_ptr_p tobj, Handler *jw, bool isnew, optional_observer_ptr<dbsendmsg_list> dbmsglist, bool parse_entities) {
	CheckTransJsonValueDef(tobj->in_reply_to_status_id, val, "in_reply_to_status_id", 0, jw);
	CheckTransJsonValueDef(tobj->in_reply_to_user_id, val, "in_reply_to_user_id", 0, jw);
	CheckTransJsonValue(tobj->retweet_count, val, "retweet_count");
	CheckTransJsonValue(tobj->favourite_count, val, "favorite_count");
	CheckTransJsonValueDef(tobj->source, val, "source", "", jw);
	CheckTransJsonValueDef(tobj->text, val, "text", "", jw);

	uint64_t quoted_status_id;
	CheckTransJsonValueDef(quoted_status_id, val, "quoted_status_id", 0, jw);
	if (quoted_status_id) {
		tobj->AddQuotedTweetId(quoted_status_id);
	}

	const rapidjson::Value &entv = val["entities"];
	const rapidjson::Value &entvex = val["extended_entities"];
	if (entv.IsObject()) {
		if (parse_entities) {
			DoEntitiesParse(entv, entvex.IsObject() ? &entvex : nullptr, tobj, isnew, dbmsglist);
		}
		if (jw) {
			jw->String("entities");
			entv.Accept(*jw);
		}
	}
	if (entvex.IsObject()) {
		if (jw) {
			jw->String("extended_entities");
			entvex.Accept(*jw);
		}
	}

	LogMsgFormat(LOGT::PARSE, "genjsonparser::ParseTweetStatics: id: %" llFmtSpec "d, RTs: %u, favs: %u",
			tobj->id, tobj->retweet_count, tobj->favourite_count);
}

//this is paired with tweet::mkdynjson
void genjsonparser::ParseTweetDyn(const rapidjson::Value &val, tweet_ptr_p tobj) {
	const rapidjson::Value &p = val["p"];
	if (p.IsArray()) {
		for (rapidjson::SizeType i = 0; i < p.Size(); i++) {
			unsigned int dbindex = CheckGetJsonValueDef<unsigned int>(p[i], "a", 0);
			for (auto &it : alist) {
				if (it->dbindex == dbindex) {
					tweet_perspective *tp = tobj->AddTPToTweet(it);
					tp->Load(CheckGetJsonValueDef<unsigned int>(p[i], "f", 0));
					break;
				}
			}
		}
	}

	CheckTransJsonValue(tobj->retweet_count, val, "r");
	CheckTransJsonValue(tobj->favourite_count, val, "f");

	LogMsgFormat(LOGT::PARSE, "genjsonparser::ParseTweetDyn: id: %" llFmtSpec "d, RTs: %u, favs: %u",
			tobj->id, tobj->retweet_count, tobj->favourite_count);
}

//returns true on success
static bool ReadEntityIndices(int &start, int &end, const rapidjson::Value &val) {
	auto &ar = val["indices"];
	if (ar.IsArray() && ar.Size() == 2) {
		if (ar[(rapidjson::SizeType) 0].IsInt() && ar[1].IsInt()) {
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
	if (!wxuri.HasServer()) return url;

	wxString host = wxuri.GetServer();
	if (host == wxT("dropbox.com") || host.EndsWith(wxT(".dropbox.com"), nullptr)) {
		if (wxuri.GetPath().StartsWith(wxT("/s/")) && !wxuri.HasQuery()) {
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
	if (media_std.IsArray()) {
		media_array = &media_std;
	}
	if (val_ex) {
		auto &media_ex = (*val_ex)["media"];
		if (media_ex.IsArray()) {
			media_array = &media_ex;
		}
	}

	t->entlist.clear();

	unsigned int entity_count = 0;
	if (hashtags.IsArray()) {
		entity_count += hashtags.Size();
	}
	if (urls.IsArray()) {
		entity_count += urls.Size();
	}
	if (user_mentions.IsArray()) {
		entity_count += user_mentions.Size();
	}
	if (media_array) {
		entity_count += media_array->Size();
	}
	t->entlist.reserve(entity_count);

	if (hashtags.IsArray()) {
		for (rapidjson::SizeType i = 0; i < hashtags.Size(); i++) {
			t->entlist.emplace_back(ENT_HASHTAG);
			entity *en = &t->entlist.back();
			if (!ReadEntityIndices(*en, hashtags[i])) {
				t->entlist.pop_back();
				continue;
			}
			if (!CheckTransJsonValueDef(en->text, hashtags[i], "text", "")) {
				t->entlist.pop_back();
				continue;
			}
			en->text = "#" + en->text;
		}
	}

	auto mk_media_thumb_load_func = [&](std::string url, flagwrapper<MIDC> net_flags, flagwrapper<MELF> netloadmask) {
		netloadmask |= MELF::FORCE;
		return [url, net_flags, netloadmask](media_entity *me, flagwrapper<MELF> mel_flags) {
			struct local {
				static void try_net_dl(media_entity *me, std::string url, flagwrapper<MIDC> net_flags, flagwrapper<MELF> netloadmask, flagwrapper<MELF> mel_flags) {
					if (mel_flags & MELF::NONETLOAD) return;
					if (!(me->flags & MEF::HAVE_THUMB) && !(url.empty()) && (netloadmask & mel_flags) &&
							!(me->flags & MEF::THUMB_NET_INPROGRESS) && !(me->flags & MEF::THUMB_FAILED)) {
						std::shared_ptr<taccount> acc = me->dm_media_acc.lock();
						mediaimgdlconn::NewConnWithOptAccOAuth(url, me->media_id, net_flags, acc.get());
					}
				};
			};

			if (me->flags & MEF::LOAD_THUMB && !(me->flags & MEF::HAVE_THUMB)) {
				//Don't bother loading a cached thumb now, that can wait
				if (mel_flags & MELF::LOADTIME) return;

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
					observer_ptr<media_entity> me = media_entity::GetExisting(job_data->media_id);
					if (me) {
						media_entity &m = *me;

						m.flags &= ~MEF::THUMB_NET_INPROGRESS;
						if (job_data->ok) {
							LogMsgFormat(LOGT::FILEIOTRACE, "genjsonparser::DoEntitiesParse::mk_media_thumb_load_func, successfully loaded cached media thumbnail file: %s, url: %s",
									cstr(media_entity::cached_thumb_filename(job_data->media_id)), cstr(url));
							m.thumbimg = wxBitmap(job_data->img);
							m.flags |= MEF::HAVE_THUMB;
							for (auto &jt : m.tweet_list) {
								UpdateTweet(*jt);
							}
						} else {
							LogMsgFormat(LOGT::FILEIOERR, "genjsonparser::DoEntitiesParse::mk_media_thumb_load_func, cached media thumbnail file: %s, url: %s, missing, invalid or failed hash check",
									cstr(media_entity::cached_thumb_filename(job_data->media_id)), cstr(url));
							m.flags &= ~MEF::LOAD_THUMB;
							local::try_net_dl(&m, url, net_flags, netloadmask, mel_flags);
						}
					}
				});
			} else {
				local::try_net_dl(me, url, net_flags, netloadmask, mel_flags);
			}
		};
	};

	if (urls.IsArray()) {
		for (rapidjson::SizeType i = 0; i < urls.Size(); i++) {
			t->entlist.emplace_back(ENT_URL);
			entity *en = &t->entlist.back();
			if (!ReadEntityIndices(*en, urls[i])) {
				t->entlist.pop_back();
				continue;
			}
			CheckTransJsonValueDef(en->text, urls[i], "display_url", t->text.substr(en->start, en->end-en->start));
			CheckTransJsonValueDef(en->fullurl, urls[i], "expanded_url", en->text);

			wxURI wxuri(wxstrstd(en->fullurl));
			wxString end = wxuri.GetPath();
			if (end.EndsWith(wxT(".jpg")) || end.EndsWith(wxT(".png")) || end.EndsWith(wxT(".jpeg")) || end.EndsWith(wxT(".gif"))) {
				en->type = ENT_URL_IMG;
				t->flags.Set('I');

				std::string url = ProcessMediaURL(en->fullurl, wxuri);

				observer_ptr<media_entity> me = ad.img_media_map[url];
				if (!me) {
					media_id_type media_id;
					media_id.m_id = ad.next_media_id;
					ad.next_media_id++;
					media_id.t_id = t->id;
					me = media_entity::MakeNew(media_id, url);
					if (gc.cachethumbs || gc.cachemedia) {
						DBC_InsertMedia(*me, dbmsglist);
					}
				}
				auto res = std::find_if (me->tweet_list.begin(), me->tweet_list.end(), [&](tweet_ptr_p tt) {
					return tt->id == t->id;
				});
				if (res == me->tweet_list.end()) {
					me->tweet_list.push_front(t);
				}

				flagwrapper<MELF> netloadmask = 0;
				if (gc.autoloadthumb_full) {
					netloadmask |= MELF::LOADTIME;
				}
				if (gc.disploadthumb_full) {
					netloadmask |= MELF::DISPTIME;
				}
				me->check_load_thumb_func = mk_media_thumb_load_func(me->media_url, MIDC::FULLIMG | MIDC::THUMBIMG | MIDC::REDRAW_TWEETS, netloadmask);
				me->CheckLoadThumb(MELF::LOADTIME);
			}

			if (wxuri.GetServer() == wxT("twitter.com") && !wxuri.HasQuery()) {
				static pcre *pattern = 0;
				static pcre_extra *patextra = 0;
				static const char patsyntax[] = "^/[^/]+/status/(\\d+)$";

				if (!pattern) {
					const char *errptr;
					int erroffset;
					pattern = pcre_compile(patsyntax, PCRE_NO_UTF8_CHECK | PCRE_UTF8, &errptr, &erroffset, 0);
					if (!pattern) {
						LogMsgFormat(LOGT::OTHERERR, "genjsonparser::DoEntitiesParse: twitter URL: pcre_compile failed: %s (%d)\n%s",
								cstr(errptr), erroffset, cstr(patsyntax));
						continue;
					}
					patextra = pcre_study(pattern, 0, &errptr);
				}

				std::string path = stdstrwx(wxuri.GetPath());

				const int ovecsize = 9;
				int ovector[9];
				if (pcre_exec(pattern, patextra, path.c_str(), path.size(), 0, 0, ovector, ovecsize) >= 2) {
					uint64_t id = 0;
					bool ok = ownstrtonum(id, path.c_str() + ovector[2], ovector[3] - ovector[2]);
					if (ok) {
						t->AddQuotedTweetId(id);
					}
				}
			}
		}
	}

	t->flags.Set('M', false);
	if (user_mentions.IsArray()) {
		for (rapidjson::SizeType i = 0; i < user_mentions.Size(); i++) {
			t->entlist.emplace_back(ENT_MENTION);
			entity *en = &t->entlist.back();
			if (!ReadEntityIndices(*en, user_mentions[i])) {
				t->entlist.pop_back();
				continue;
			}
			uint64_t userid;
			if (!CheckTransJsonValueDef(userid, user_mentions[i], "id", 0)) {
				t->entlist.pop_back();
				continue;
			}
			if (!CheckTransJsonValueDef(en->text, user_mentions[i], "screen_name", "")) {
				t->entlist.pop_back();
				continue;
			}
			en->user = ad.GetUserContainerById(userid);
			if (en->user->GetUser().screen_name.empty()) {
				en->user->GetUser().screen_name = en->text;
			}
			en->text = "@" + en->text;
			if (t->flags.Get('D')) {
				// DMs should not also be set as mentions
				continue;
			}
			if (en->user->udc_flags & UDC::THIS_IS_ACC_USER_HINT) {
				t->flags.Set('M', true);
			}
			if (isnew) {
				en->user->mention_set.insert(t->id);
				en->user->lastupdate_wrotetodb = 0;		//force flush of user to DB
			}
		}
	}

	if (media_array) {
		auto &media = *media_array;
		for (rapidjson::SizeType i = 0; i < media.Size(); i++) {
			t->flags.Set('I');
			t->entlist.emplace_back(ENT_MEDIA);
			entity *en = &t->entlist.back();
			if (!ReadEntityIndices(*en, media[i])) {
				t->entlist.pop_back();
				continue;
			}
			CheckTransJsonValueDef(en->text, media[i], "display_url", t->text.substr(en->start, en->end-en->start));
			CheckTransJsonValueDef(en->fullurl, media[i], "expanded_url", en->text);
			if (!CheckTransJsonValueDef(en->media_id.m_id, media[i], "id", 0)) {
				t->entlist.pop_back();
				continue;
			}
			en->media_id.t_id = 0;

			observer_ptr<media_entity> me = media_entity::GetExisting(en->media_id);
			if (!me) {
				std::string url;
				if (t->flags.Get('s')) {
					if (!CheckTransJsonValueDef(url, media[i], "media_url_https", "")) {
						CheckTransJsonValueDef(url, media[i], "media_url", "");
					}
				} else {
					if (!CheckTransJsonValueDef(url, media[i], "media_url", "")) {
						CheckTransJsonValueDef(url, media[i], "media_url_https", "");
					}
				}
				url += ":large";
				me = media_entity::MakeNew(en->media_id, url);
				if (gc.cachethumbs || gc.cachemedia) {
					DBC_InsertMedia(*me, dbmsglist);
				}
			}

			const rapidjson::Value &videoinfo = media[i]["video_info"];
			if (videoinfo.IsObject()) {
				std::unique_ptr<video_entity> ve(new video_entity());
				const rapidjson::Value &aspect = videoinfo["aspect_ratio"];
				if (aspect.IsArray() && aspect.Size() == 2) {
					CheckTransJsonValueDef(ve->aspect_w, aspect[static_cast<rapidjson::SizeType>(0)], nullptr, 0);
					CheckTransJsonValueDef(ve->aspect_h, aspect[static_cast<rapidjson::SizeType>(1)], nullptr, 0);
				}
				CheckTransJsonValueDef(ve->duration_ms, videoinfo, "duration_millis", 0);
				const rapidjson::Value &variants = videoinfo["variants"];
				if (variants.IsArray()) {
					for (rapidjson::SizeType j = 0; j < variants.Size(); j++) {
						const rapidjson::Value &var = variants[j];
						if (!var.IsObject()) {
							continue;
						}
						ve->variants.emplace_back();
						auto &v = ve->variants.back();
						CheckTransJsonValueDef(v.content_type, var, "content_type", "");
						CheckTransJsonValueDef(v.url, var, "url", "");
						CheckTransJsonValueDef(v.bitrate, var, "bitrate", 0);
					}
				}
				const rapidjson::Value &sizes = media[i]["sizes"];
				if (sizes.IsObject()) {
					const rapidjson::Value &size_large = sizes["large"];
					if (size_large.IsObject()) {
						CheckTransJsonValueDef(ve->size_w, size_large, "w", 0);
						CheckTransJsonValueDef(ve->size_h, size_large, "h", 0);
					}
				}

				me->video = std::move(ve);
			}

			auto res = std::find_if (me->tweet_list.begin(), me->tweet_list.end(), [&](tweet_ptr_p tt) {
				return tt->id == t->id;
			});
			if (res == me->tweet_list.end()) {
				me->tweet_list.push_front(t);
			}

			// Test this here as well as in TweetFormatProc as we may want to load the thumbnail immediately below
			if (t->flags.Get('D')) {
				// This is a media entity in a DM
				// This requires an oAuth token to access
				// Set the media entity dm_media_acc field to something sensible
				std::shared_ptr<taccount> acc = me->dm_media_acc.lock();
				t->GetUsableAccount(acc, tweet::GUAF::CHECKEXISTING | tweet::GUAF::NOERR);
				me->dm_media_acc = acc;
			}

			std::string thumburl;
			if (me->media_url.size() > 6) {
				thumburl = me->media_url.substr(0, me->media_url.size() - 6) + ":thumb";
			}
			flagwrapper<MELF> netloadmask = 0;
			if (gc.autoloadthumb_thumb) {
				netloadmask |= MELF::LOADTIME;
			}
			if (gc.disploadthumb_thumb) {
				netloadmask |= MELF::DISPTIME;
			}
			me->check_load_thumb_func = mk_media_thumb_load_func(thumburl, MIDC::THUMBIMG | MIDC::REDRAW_TWEETS, netloadmask);
			me->CheckLoadThumb(MELF::LOADTIME);
		}
	}

	std::sort(t->entlist.begin(), t->entlist.end(), [](const entity &a, const entity &b) { return a.start < b.start; });
	for (auto &src_it : t->entlist) {
		LogMsgFormat(LOGT::PARSE, "Tweet %" llFmtSpec "d, have entity from %d to %d: %s", t->id, src_it.start,
				src_it.end, cstr(src_it.text));
	}
}

flagwrapper<genjsonparser::USERPARSERESULT> genjsonparser::ParseUserContents(const rapidjson::Value &val, userdata &userobj, flagwrapper<genjsonparser::USERPARSEFLAGS> flags) {
	bool changed = false;
	bool img_changed = false;
	CheckTransJsonValueDefTrackChanges(changed, userobj.name, val, "name", "");
	CheckTransJsonValueDefTrackChanges(changed, userobj.screen_name, val, "screen_name", "");
	CheckTransJsonValueDefTrackChanges(changed, userobj.description, val, "description", "");
	CheckTransJsonValueDefTrackChanges(changed, userobj.location, val, "location", "");
	CheckTransJsonValueDefTrackChanges(changed, userobj.userurl, val, "url", "");
	if (flags & USERPARSEFLAGS::IS_SSL) {
		if (!CheckTransJsonValueDefTrackChanges(img_changed, userobj.profile_img_url, val, "profile_image_url_https", "")) {
			CheckTransJsonValueDefTrackChanges(img_changed, userobj.profile_img_url, val, "profile_img_url", "");
		}
	} else {
		if (!CheckTransJsonValueDefTrackChanges(img_changed, userobj.profile_img_url, val, "profile_img_url", "")) {
			CheckTransJsonValueDefTrackChanges(img_changed, userobj.profile_img_url, val, "profile_image_url_https", "");
		}
	}
	CheckTransJsonValueDefFlagTrackChanges(changed, userobj.u_flags, userdata::UF::ISPROTECTED, val, "protected", false);
	CheckTransJsonValueDefFlagTrackChanges(changed, userobj.u_flags, userdata::UF::ISVERIFIED, val, "verified", false);
	CheckTransJsonValueDefTrackChanges(changed, userobj.followers_count, val, "followers_count", userobj.followers_count);
	CheckTransJsonValueDefTrackChanges(changed, userobj.statuses_count, val, "statuses_count", userobj.statuses_count);
	CheckTransJsonValueDefTrackChanges(changed, userobj.friends_count, val, "friends_count", userobj.friends_count);
	CheckTransJsonValueDefTrackChanges(changed, userobj.favourites_count, val, "favourites_count", userobj.favourites_count);
	if (flags & USERPARSEFLAGS::IS_DB_LOAD) {
		CheckTransJsonValueDefTrackChanges(changed, userobj.notes, val, "retcon_notes", "");
	}
	flagwrapper<USERPARSERESULT> result = 0;
	if (changed) {
		result |= USERPARSERESULT::OTHER_UPDATED;
	}
	if (img_changed) {
		result |= USERPARSERESULT::PROFIMG_UPDATED;
	}
	if (changed || img_changed) {
		userobj.revision_number++;
	}
	return result;
}

jsonparser::jsonparser(std::shared_ptr<taccount> a, optional_observer_ptr<twitcurlext> tw)
		: tac(a), twit(tw) { }

jsonparser::~jsonparser() {
	if (dbmsglist && !dbmsglist->msglist.empty()) {
		DBC_SendMessage(std::move(dbmsglist));
	}
}

void jsonparser::RestTweetUpdateParams(const tweet &t, optional_observer_ptr<restbackfillstate> rbfs) {
	if (rbfs) {
		if (rbfs->max_tweets_left) {
			rbfs->max_tweets_left--;
		}
		if (!rbfs->end_tweet_id || rbfs->end_tweet_id >= t.id) {
			rbfs->end_tweet_id = t.id - 1;
		}
		rbfs->read_again = true;
		rbfs->lastop_recvcount++;
	}
}

void jsonparser::RestTweetPreParseUpdateParams(optional_observer_ptr<restbackfillstate> rbfs) {
	if (rbfs) {
		rbfs->read_again = false;
	}
}

void jsonparser::DoFriendLookupParse(const rapidjson::Value &val) {
	using URF = user_relationship::URF;
	time_t optime = (tac->ta_flags & taccount::TAF::STREAM_UP) ? 0 : time(nullptr);
	if (val.IsArray()) {
		for (rapidjson::SizeType i = 0; i < val.Size(); i++) {
			uint64_t userid = CheckGetJsonValueDef<uint64_t>(val[i], "id", 0);
			if (userid) {
				const rapidjson::Value& cons = val[i]["connections"];
				if (cons.IsArray()) {
					tac->SetUserRelationship(userid, URF::IFOLLOW_KNOWN | URF::FOLLOWSME_KNOWN, optime);
					bool is_muting = false;
					bool is_blocked = false;
					for (rapidjson::SizeType j = 0; j < cons.Size(); j++) {
						if (cons[j].IsString()) {
							std::string conn_type = cons[j].GetString();
							if (conn_type == "following") {
								tac->SetUserRelationship(userid, URF::IFOLLOW_KNOWN | URF::IFOLLOW_TRUE, optime);
							} else if (conn_type == "following_requested") {
								tac->SetUserRelationship(userid, URF::IFOLLOW_KNOWN | URF::IFOLLOW_PENDING, optime);
							} else if (conn_type == "followed_by") {
								tac->SetUserRelationship(userid, URF::FOLLOWSME_KNOWN | URF::FOLLOWSME_TRUE, optime);
							} else if (conn_type == "following_received") {
								tac->SetUserRelationship(userid, URF::FOLLOWSME_KNOWN | URF::FOLLOWSME_PENDING, optime);
							} else if (conn_type == "none") {
								// no action, as we initialise to this value anyway
							} else if (conn_type == "muting") {
								is_muting = true;
							} else if (conn_type == "blocking") {
								is_blocked = true;
							} else {
								LogMsgFormat(LOGT::PARSEERR, "taccount::DoFriendLookupParse: %s: %s: unexpected friendship type: '%s'",
										cstr(tac->dispname), cstr(user_short_log_line(userid)), cstr(conn_type));
							}
						}
					}
					tac->SetUserIdBlockedState(userid, BLOCKTYPE::MUTE, is_muting);
					tac->SetUserIdBlockedState(userid, BLOCKTYPE::BLOCK, is_blocked);
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

	return ParseStringInPlace(dc, data->json.data(), "jsonparser::ParseString");
}

void jsonparser::ProcessTimelineResponse(flagwrapper<JDTP> sflags, optional_observer_ptr<restbackfillstate> rbfs) {
	const rapidjson::Document &dc = data->doc;
	RestTweetPreParseUpdateParams(rbfs);
	if (dc.IsArray()) {
		if (!dbmsglist) dbmsglist.reset(new dbsendmsg_list());
		for (rapidjson::SizeType i = 0; i < dc.Size(); i++) {
			RestTweetUpdateParams(*DoTweetParse(dc[i], sflags), rbfs);
		}
	} else {
		RestTweetUpdateParams(*DoTweetParse(dc, sflags), rbfs);
	}
}

void jsonparser::ProcessUserTimelineResponse(optional_observer_ptr<restbackfillstate> rbfs) {
	const rapidjson::Document &dc = data->doc;
	RestTweetPreParseUpdateParams(rbfs);
	if (dc.IsArray()) {
		for (rapidjson::SizeType i = 0; i < dc.Size(); i++) {
			DoTweetParse(dc[i], JDTP::USERTIMELINE);
		}
	} else {
		DoTweetParse(dc, JDTP::USERTIMELINE);
	}
	CheckClearNoUpdateFlag_All();
}

void jsonparser::ProcessStreamResponse(bool out_of_date_parse) {
	const rapidjson::Document &dc = data->doc;
	const rapidjson::Value &fval = dc["friends"];
	const rapidjson::Value &eval = dc["event"];
	const rapidjson::Value &ival = dc["id"];
	const rapidjson::Value &tval = dc["text"];
	const rapidjson::Value &dmval = dc["direct_message"];
	const rapidjson::Value &delval = dc["delete"];

	intrusive_ptr<out_of_date_data> out_of_date_state;
	if (out_of_date_parse) {
		out_of_date_state.reset(new out_of_date_data());
		out_of_date_state->CheckEventToplevelJson(dc);
	}

	if (fval.IsArray()) {
		if (out_of_date_parse) {
			return;
		}

		tac->ta_flags |= taccount::TAF::STREAM_UP;
		tac->last_stream_start_time = time(nullptr);

		std::vector<uint64_t> following;
		following.reserve(fval.Size());
		for (rapidjson::SizeType i = 0; i < fval.Size(); i++) {
			following.push_back(fval[i].GetUint64());
		}
		tac->HandleUserIFollowList(std::move(following), true);

		if (twit && (twit->post_action_flags & PAF::STREAM_CONN_READ_BACKFILL)) {
			tac->GetRestBackfill();
			tac->CheckUpdateBlockLists();
		}
		user_window::RefreshAllFollow();
		tac->GetUsersFollowingMeList();
	} else if (eval.IsString()) {
		if (out_of_date_parse) {
			return;
		}

		DoEventParse(dc);
	} else if (dmval.IsObject()) {
		DoTweetParse(dmval, JDTP::ARRIVED | JDTP::TIMELINERECV | JDTP::ISDM, out_of_date_state);
	} else if (delval.IsObject() && delval["status"].IsObject()) {
		if (out_of_date_parse) {
			// delete events seem to have the timestamp_ms inside the delete object, instead of at the top level
			out_of_date_state->CheckEventToplevelJson(delval);
		}
		DoTweetParse(delval["status"], JDTP::DEL, out_of_date_state);
	} else if (ival.IsNumber() && tval.IsString() && dc["recipient"].IsObject() && dc["sender"].IsObject()) {    //assume this is a direct message
		DoTweetParse(dc, JDTP::ARRIVED | JDTP::TIMELINERECV | JDTP::ISDM, out_of_date_state);
	} else if (ival.IsNumber() && tval.IsString() && dc["user"].IsObject()) {    //assume that this is a tweet
		if (DoStreamTweetPreFilter(dc)) {
			DoTweetParse(dc, JDTP::ARRIVED | JDTP::TIMELINERECV, out_of_date_state);
		}
	} else {
		LogMsgFormat(LOGT::PARSEERR, "Stream Event Parser: Can't identify event: %s", cstr(data->source_str));
	}
}

void jsonparser::ProcessSingleTweetResponse(flagwrapper<JDTP> sflags) {
	DoTweetParse(data->doc, sflags);
}

void jsonparser::ProcessAccVerifyResponse() {
	udc_ptr auser = DoUserParse(data->doc);
	for (auto &it : alist) {
		if (it == tac) continue;
		if (auser->id == it->usercont->id) {
			wxString message = wxString::Format(wxT("Error, attempted to assign more than one account to the same twitter account: %s, @%s"
						", id: %" wxLongLongFmtSpec "d.\n"
						"This account will be disabled, or not created. Re-authenticate or delete the offending account(s)."),
					wxstrstd(auser->GetUser().name).c_str(), wxstrstd(auser->GetUser().screen_name).c_str(), auser->id);
			LogMsg(LOGT::OTHERERR, stdstrwx(message));
			wxMessageBox(message, wxT("Authentication Error"), wxOK | wxICON_ERROR);
			tac->userenabled = false;
			return;
		}
	}

	if (tac->usercont && tac->usercont->id && tac->usercont->id != auser->id) {
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
	if (dc.IsArray()) {
		if (!dbmsglist) dbmsglist.reset(new dbsendmsg_list());
		for (rapidjson::SizeType i = 0; i < dc.Size(); i++) {
			DoUserParse(dc[i], UMPTF::TPDB_NOUPDF | UMPTF::RMV_LKPINPRGFLG);
		}
		CheckClearNoUpdateFlag_All();
	} else {
		DoUserParse(dc);
	}
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
	tac->LookupFriendships(u->id);
}

void jsonparser::ProcessFriendshipShowResponse() {
	const rapidjson::Document &dc = data->doc;
	if (!dc.IsObject()) return;

	auto &dcr = dc["relationship"];
	if (!dcr.IsObject()) return;

	auto &dcs = dcr["source"];
	if (!dcs.IsObject()) return;

	auto &dct = dcr["target"];
	if (!dct.IsObject()) return;

	uint64_t uid;
	if (!CheckTransJsonValue<uint64_t>(uid, dct, "id")) return;

	bool want_rts;
	if (CheckTransJsonValue<bool>(want_rts, dcs, "want_retweets")) {
		tac->SetUserIdBlockedState(uid, BLOCKTYPE::NO_RT, !want_rts);
	}
	bool blocking;
	if (CheckTransJsonValue<bool>(blocking, dcs, "blocking")) {
		tac->SetUserIdBlockedState(uid, BLOCKTYPE::BLOCK, blocking);
	}
	bool muting;
	if (CheckTransJsonValue<bool>(muting, dcs, "muting")) {
		tac->SetUserIdBlockedState(uid, BLOCKTYPE::MUTE, muting);
	}

	user_window::RefreshAllFollow();
}

void jsonparser::ProcessGenericUserFollowListResponse(observer_ptr<tpanelparentwin_userproplisting> win) {
	if (!win) {
		return;
	}

	const rapidjson::Document &dc = data->doc;
	if (dc.IsObject()) {
		auto &dci = dc["ids"];
		if (dci.IsArray()) {
			for (rapidjson::SizeType i = 0; i < dci.Size(); i++) {
				win->PushUserIDToBack(dci[i].GetUint64());
			}
		}
	}
	win->LoadMoreToBack(gc.maxtweetsdisplayinpanel);
}

void jsonparser::ProcessOwnFollowerListingResponse() {
	const rapidjson::Document &dc = data->doc;

	if (!dc.IsObject()) {
		return;
	}
	auto &dci = dc["ids"];
	if (!dci.IsArray()) {
		return;
	}

	std::vector<uint64_t> followers;
	followers.reserve(dci.Size());
	for (rapidjson::SizeType i = 0; i < dci.Size(); i++) {
		followers.push_back(dci[i].GetUint64());
	}
	int64_t nextcursor = CheckGetJsonValueDef<int64_t>(dc, "next_cursor", -1);

	tac->HandleUsersFollowingMeList(std::move(followers), nextcursor == 0); // listing is complete if next cursor is 0, otherwise there are more pages
}

std::string jsonparser::ProcessUploadMediaResponse() {
	const rapidjson::Document &dc = data->doc;

	if (!dc.IsObject()) {
		return "";
	}

	return CheckGetJsonValueDef<std::string>(dc, "media_id_string", "");
}

void jsonparser::ProcessTwitterErrorJson(std::vector<TwitterErrorMsg> &msgs) {
	const rapidjson::Document &dc = data->doc;

	if (!dc.IsObject()) {
		return;
	}
	auto &dci = dc["errors"];
	if (!dci.IsArray()) {
		return;
	}

	for (rapidjson::SizeType i = 0; i < dci.Size(); i++) {
		auto &err = dci[i];
		if (err.IsObject()) {
			TwitterErrorMsg msg;
			if (CheckTransJsonValue(msg.code, err, "code") &&
					CheckTransJsonValue(msg.message, err, "message")) {
				msgs.emplace_back(std::move(msg));
			}
		}
	}
}

int64_t jsonparser::ProcessGetBlockListCursoredResponse(useridset &block_id_list) {
	const rapidjson::Document &dc = data->doc;

	if (!dc.IsObject()) {
		return 0;
	}
	auto &dci = dc["ids"];
	if (!dci.IsArray()) {
		return 0;
	}

	for (rapidjson::SizeType i = 0; i < dci.Size(); i++) {
		block_id_list.insert(dci[i].GetUint64());
	}

	return CheckGetJsonValueDef<int64_t>(dc, "next_cursor", 0);
}

void jsonparser::ProcessRawIdListResponse(useridset &id_list) {
	const rapidjson::Document &dc = data->doc;
	if (!dc.IsArray()) return;

	for (rapidjson::SizeType i = 0; i < dc.Size(); i++) {
		id_list.insert(dc[i].GetUint64());
	}
}

//don't use this for perspectival attributes
udc_ptr jsonparser::DoUserParse(const rapidjson::Value &val, flagwrapper<UMPTF> umpt_flags, optional_cref_intrusive_ptr<out_of_date_data> out_of_date_state) {
	uint64_t id;
	CheckTransJsonValueDef(id, val, "id", 0);
	auto userdatacont = ad.GetUserContainerById(id);
	if (umpt_flags & UMPTF::RMV_LKPINPRGFLG) {
		userdatacont->udc_flags &= ~UDC::LOOKUP_IN_PROGRESS;
	}

	userdatacont->udc_flags &= ~UDC::ISDEAD;

	if (ad.unloaded_db_user_ids.find(id) != ad.unloaded_db_user_ids.end()) {
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
			optional_intrusive_ptr<out_of_date_data> out_of_date_state;
		};
		std::shared_ptr<funcdata> pdata = std::make_shared<funcdata>();
		pdata->udc = userdatacont;
		pdata->acc = tac;
		pdata->jp_data = this->data;
		pdata->val = &val;
		pdata->umpt_flags = umpt_flags;
		pdata->out_of_date_state = out_of_date_state;

		if (!this->data->db_pending_guard) {
			this->data->db_pending_guard.reset(new db_handle_msg_pending_guard());
		}

		DBC_SetDBSelUserMsgHandler(*msg, [pdata](dbselusermsg &pmsg, dbconn *dbc) {
			//Do not use *this, it will have long since gone out of scope

			LogMsgFormat(LOGT::PARSE | LOGT::DBTRACE, "jsonparser::DoUserParse: User id: %" llFmtSpec "d, now doing deferred parse.", pdata->udc->id);

			DBC_DBSelUserReturnDataHandler(std::move(pmsg.data), pdata->jp_data->db_pending_guard.get());

			std::shared_ptr<taccount> acc = pdata->acc.lock();
			if (acc) {
				jsonparser jp(acc);
				jp.SetData(pdata->jp_data);
				jp.DoUserParse(*(pdata->val), pdata->umpt_flags, pdata->out_of_date_state);
			} else {
				LogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, "jsonparser::DoUserParse: User id: %" llFmtSpec "d, deferred parse failed as account no longer exists.", pdata->udc->id);
			}
		});
		DBC_SendMessageBatched(std::move(msg));
		return userdatacont;
	}

	userdata &userobj = userdatacont->GetUser();

	if (out_of_date_state && userobj.createtime && out_of_date_state->time_estimate < (time_t) userdatacont->lastupdate) {
		// info appears to be too out of date, don't bother parsing
		return userdatacont;
	}

	flagwrapper<genjsonparser::USERPARSEFLAGS> parseflags;
	if (tac->ssl)
		parseflags |= genjsonparser::USERPARSEFLAGS::IS_SSL;
	auto parseresult = ParseUserContents(val, userobj, parseflags);
	if (parseresult & genjsonparser::USERPARSERESULT::PROFIMG_UPDATED) {
		if (userdatacont->udc_flags & UDC::PROFILE_IMAGE_DL_FAILED) {
			// Profile image was previously marked as failed
			// The URL has changed, so download the new one now
			userdatacont->udc_flags &= ~UDC::PROFILE_IMAGE_DL_FAILED;
			userdatacont->ImgIsReady(PENDING_REQ::PROFIMG_DOWNLOAD);
		}
	}
	if (!userobj.createtime) {				//this means that the object is new
		std::string created_at;
		CheckTransJsonValueDef(created_at, val, "created_at", "");
		ParseTwitterDate(0, &userobj.createtime, created_at);
		DBC_InsertUser(userdatacont, make_observer(dbmsglist));
	}

	if (out_of_date_state) {
		userdatacont->lastupdate = out_of_date_state->time_estimate;
	} else {
		userdatacont->MarkUpdated();
	}
	userdatacont->CheckPendingTweets(umpt_flags);

	if (userdatacont->udc_flags & UDC::WINDOWOPEN) {
		user_window::CheckRefresh(id, false);
	}

	if (currentlogflags & LOGT::PARSE) {
		userdatacont->Dump();
	}

	return userdatacont;
}

void ParsePerspectivalTweetProps(const rapidjson::Value &val, tweet_perspective *tp, Handler *handler) {
	bool propvalue;
	if (CheckTransJsonValueDef<bool>(propvalue, val, "retweeted", false, handler)) {
		tp->SetRetweeted(propvalue);
	}
	if (CheckTransJsonValueDef<bool>(propvalue, val, "favorited", false, handler)) {
		tp->SetFavourited(propvalue);
	}
}

inline udc_ptr CheckParseUserObj(uint64_t id, const rapidjson::Value &val, jsonparser &jp,
		optional_cref_intrusive_ptr<jsonparser::out_of_date_data> out_of_date_state) {
	if (val.HasMember("screen_name")) {    //check to see if this is a trimmed user object
		return jp.DoUserParse(val, 0, out_of_date_state);
	} else {
		return ad.GetUserContainerById(id);
	}
}

// Returns true if tweet is OK to be used
bool jsonparser::DoStreamTweetPreFilter(const rapidjson::Value& val) {
	uint64_t tweetid;
	CheckTransJsonValueDef(tweetid, val, "id", 0, 0);

	const rapidjson::Value& userobj = val["user"];
	if (!userobj.IsObject()) return false;
	const rapidjson::Value& useridval = userobj["id"];
	if (!useridval.IsUint64()) return false;
	uint64_t uid = useridval.GetUint64();

	auto is_userid_own_account = [&](uint64_t id) -> bool {
		auto u = ad.GetExistingUserContainerById(id);
		return (u && u->udc_flags & UDC::THIS_IS_ACC_USER_HINT);
	};

	auto do_i_follow_userid = [&](uint64_t id) -> bool {
		auto it = tac->user_relations.find(id);
		if (it != tac->user_relations.end()) {
			user_relationship &ur = it->second;
			if ((ur.ur_flags & user_relationship::URF::IFOLLOW_KNOWN) && (ur.ur_flags & user_relationship::URF::IFOLLOW_TRUE)) {
				return true;
			}
		}
		return false;
	};

	auto is_blocked_userid = [&](uint64_t id) -> bool {
		if (tac->stream_drop_blocked && tac->blocked_users.count(id)) return true;
		if (tac->stream_drop_muted && tac->muted_users.count(id)) return true;
		return false;
	};

	const rapidjson::Value& text = val["text"];

	auto pre_bin = [&]() {
		if (currentlogflags & LOGT::FILTERTRACE || tac->stream_reply_mode == SRM::STD_REPLIES) {
			std::string shorttext = text.IsString() ? truncate_tweet_text(text.GetString()) : std::string("???");
			std::string screen_name = CheckGetJsonValueDef<std::string>(userobj, "screen_name", "???");
			LogMsgFormat(LOGT::FILTERTRACE, "jsonparser::DoStreamTweetPreFilter: Binning tweet: %" llFmtSpec "d (@%s): %" llFmtSpec "d (%s)",
					uid, cstr(screen_name), tweetid, cstr(shorttext));
			if (tac->stream_reply_mode == SRM::STD_REPLIES) {
				LogMsgFormat(LOGT::FILTERERR, "jsonparser::DoStreamTweetPreFilter: Warning: Binning tweet: %" llFmtSpec "d (@%s): %" llFmtSpec "d (%s), "
						"even though we are in standard replies mode, this may (or may not) be a bug.",
						uid, cstr(screen_name), tweetid, cstr(shorttext));
			}
		}
	};

	if (is_userid_own_account(uid)) {
		// This is one of our own tweets
		return true;
	}

	if (is_blocked_userid(uid)) {
		// user is blocked/muted
		pre_bin();
		return false;
	}
	auto &rtval = val["retweeted_status"];
	if (rtval.IsObject()) {
		const rapidjson::Value& rtuserobj = rtval["user"];
		if (rtuserobj.IsObject()) {
			const rapidjson::Value& rtuseridval = rtuserobj["id"];
			if (rtuseridval.IsUint64()) {
				if (is_blocked_userid(rtuseridval.GetUint64())) {
					// retweet source user is blocked/muted
					pre_bin();
					return false;
				}
			}
		}
		if (tac->stream_drop_no_rt && tac->no_rt_users.count(uid)) {
			// retweeting user has retweets disabled
			pre_bin();
			return false;
		}
	}

	if (tac->stream_reply_mode == SRM::ALL_REPLIES) return true;

	bool is_reply = (text.IsString() && IsTweetAReply(text.GetString()));
	int first_reply_offset = std::numeric_limits<int>::max();
	uint64_t first_reply_uid = 0;

	if (is_reply || tac->stream_reply_mode == SRM::ALL_MENTIONS) {
		//Check user mentions
		const rapidjson::Value &entv = val["entities"];
		if (entv.IsObject()) {
			const rapidjson::Value &um = entv["user_mentions"];
			if (um.IsArray()) {
				for (rapidjson::SizeType i = 0; i < um.Size(); i++) {
					const rapidjson::Value &umi = um[i];
					if (umi.IsObject()) {
						const rapidjson::Value &umid = umi["id"];
						if (umid.IsUint64()) {
							if (tac->stream_reply_mode != SRM::STD_REPLIES) {
								if (is_userid_own_account(umid.GetUint64())) {
									// This tweet mentions us, and we're in all mentions mode
									return true;
								}
							}
							if (is_reply) {
								int start, end;
								if (ReadEntityIndices(start, end, umi)) {
									if (start < first_reply_offset) {
										first_reply_offset = start;
										first_reply_uid = umid.GetUint64();
									}
								}
							}
						}
					}
				}
				if (is_reply && first_reply_uid && tac->stream_reply_mode != SRM::ALL_MENTIONS_FOLLOWS) {
					if (!is_userid_own_account(first_reply_uid) && !do_i_follow_userid(first_reply_uid)) {
						// This is a reply to someone else who we don't follow
						pre_bin();
						return false;
					}
				}
			}
		}
	}

	if (do_i_follow_userid(uid)) {
		// This account follows the tweet author
		return true;
	}

	// Bin it
	pre_bin();
	return false;
}

tweet_ptr jsonparser::DoTweetParse(const rapidjson::Value &val, flagwrapper<JDTP> sflags, optional_cref_intrusive_ptr<out_of_date_data> out_of_date_state) {
	uint64_t tweetid;
	if (!CheckTransJsonValueDef(tweetid, val, "id", 0, 0)) {
		LogMsgFormat(LOGT::PARSEERR, "jsonparser::DoTweetParse: No ID present in document.");
		return tweet_ptr();
	}

	tweet_ptr tobj = ad.GetTweetById(tweetid);

	if (ad.unloaded_db_tweet_ids.find(tweetid) != ad.unloaded_db_tweet_ids.end()) {
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
			optional_intrusive_ptr<out_of_date_data> out_of_date_state;
		};
		std::shared_ptr<funcdata> pdata = std::make_shared<funcdata>();
		pdata->acc = tac;
		pdata->jp_data = this->data;
		pdata->val = &val;
		pdata->sflags = sflags;
		pdata->tweetid = tweetid;
		pdata->out_of_date_state = out_of_date_state;

		if (!this->data->db_pending_guard) {
			this->data->db_pending_guard.reset(new db_handle_msg_pending_guard());
		}

		DBC_SetDBSelTweetMsgHandler(*msg, [pdata](dbseltweetmsg &pmsg, dbconn *dbc) {
			//Do not use *this, it will have long since gone out of scope

			LogMsgFormat(LOGT::PARSE | LOGT::DBTRACE, "jsonparser::DoTweetParse: Tweet id: %" llFmtSpec "d, now doing deferred parse.", pdata->tweetid);

			DBC_HandleDBSelTweetMsg(pmsg, pdata->jp_data->db_pending_guard.get());

			std::shared_ptr<taccount> acc = pdata->acc.lock();
			if (acc) {
				jsonparser jp(acc);
				jp.SetData(pdata->jp_data);
				jp.DoTweetParse(*(pdata->val), pdata->sflags | JDTP::POSTDBLOAD, pdata->out_of_date_state);
			} else {
				LogMsgFormat(LOGT::PARSEERR | LOGT::DBERR, "jsonparser::DoTweetParse: Tweet id: %" llFmtSpec "d, deferred parse failed as account no longer exists.", pdata->tweetid);
			}
		});
		DBC_SendMessageBatched(std::move(msg));
		return tobj;
	}

	sflags |= data->base_sflags;

	if (sflags & JDTP::ISDM) {
		tobj->flags.Set('D');
	} else {
		tobj->flags.Set('T');
	}
	if (tac->ssl) {
		tobj->flags.Set('s');
	}
	if (sflags & JDTP::DEL) {
		tobj->flags.Set('X');
		if (gc.markdeletedtweetsasread) {
			tobj->flags.Set('r');
			tobj->flags.Set('u', false);
		}
		tobj->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);

		if (!tobj->user || tobj->createtime == 0) {
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

	if (!(sflags & JDTP::DEL)) {
		is_new_tweet_perspective = !tp->IsReceivedHere();
		if (sflags & JDTP::ARRIVED) {
			has_just_arrived = !tp->IsArrivedHere();
			tp->SetArrivedHere(true);

			if (!(sflags & JDTP::ISDM)) {
				tac->tweet_ids.insert(tweetid);
			} else {
				tac->dm_ids.insert(tweetid);
			}
		}
		tp->SetReceivedHere(true);
		ParsePerspectivalTweetProps(val, tp, 0);
	} else {
		tp->SetRecvTypeDel(true);
	}

	if (is_new_tweet_perspective) {
		arr |= ARRIVAL::RECV;
	}

	if (sflags & JDTP::FAV) {
		tp->SetFavourited(true);
	}
	if (sflags & JDTP::UNFAV) {
		tp->SetFavourited(false);
	}

	std::string json;
	if (!(sflags & JDTP::DEL)) {
		if ((tobj->createtime == 0) || (sflags & JDTP::ALWAYSREPARSE)) {
			// this is a better test than merely whether the tweet object is new

			writestream wr(json);
			Handler jw(wr);
			jw.StartObject();
			ParseTweetStatics(val, tobj, &jw, true, make_observer(dbmsglist));
			jw.EndObject();
			std::string created_at;
			if (CheckTransJsonValueDef(created_at, val, "created_at", "", 0)) {
				ParseTwitterDate(0, &tobj->createtime, created_at);
			} else {
				tobj->createtime = time(nullptr);
			}
			auto &rtval = val["retweeted_status"];
			if (rtval.IsObject()) {
				tobj->rtsrc = DoTweetParse(rtval, (sflags & JDTP::SAVE_MASK) | JDTP::ISRTSRC, out_of_date_state);
				if (tobj->rtsrc) {
					tobj->flags.Set('R');
				}
			}
		} else {
			//These properties are also checked in ParseTweetStatics.
			//Previous versions stored the retweet count in the DB static string
			//so these should not be removed from there, as otherwise old tweets
			//in the DB will lose their retweet count info when loaded.

			//Don't update these if out of date, and we have the values already
			if (!out_of_date_state) {
				CheckTransJsonValueDef(tobj->retweet_count, val, "retweet_count", 0);
				CheckTransJsonValueDef(tobj->favourite_count, val, "favorite_count", 0);
			}
		}
		if (out_of_date_state && !(out_of_date_state->flags & OODPEF::HAVE_REAL_TIME) && tobj->createtime > out_of_date_state->time_estimate) {
			out_of_date_state->time_estimate = tobj->createtime;
		}
		auto &quoteval = val["quoted_status"];
		if (quoteval.IsObject()) {
			DoTweetParse(quoteval, (sflags & JDTP::SAVE_MASK) | JDTP::ISQUOTE, out_of_date_state);
		}
	}

	auto &possiblysensitive = val["possibly_sensitive"];
	if (possiblysensitive.IsBool() && possiblysensitive.GetBool()) {
		tobj->flags.Set('P');
	}

	LogMsgFormat(LOGT::PARSE, "jsonparser::DoTweetParse: id: %" llFmtSpec "d, is_new_tweet_perspective: %d, has_just_arrived: %d, isdm: %d, sflags: 0x%X, RTs: %u, favs: %u",
			tobj->id, is_new_tweet_perspective, has_just_arrived, !!(sflags & JDTP::ISDM), sflags,
			tobj->retweet_count, tobj->favourite_count);

	if (is_new_tweet_perspective) {	//this filters out duplicate tweets from the same account
		if (!(sflags & JDTP::ISDM)) {
			const rapidjson::Value& userobj = val["user"];
			if (userobj.IsObject()) {
				const rapidjson::Value& useridval = userobj["id"];
				if (useridval.IsUint64()) {
					uint64_t userid = useridval.GetUint64();
					tobj->user = CheckParseUserObj(userid, userobj, *this, out_of_date_state);
					if (tobj->user->udc_flags & UDC::THIS_IS_ACC_USER_HINT) {
						tobj->flags.Set('O', true);
					}
				}
			}
		} else {	//direct message
			auto adduserdmindex = [&](udc_ptr_p u) {
				ad.GetUserDMIndexById(u->id).AddDMId(tweetid);
				u->udc_flags |= UDC::NON_PURGABLE;
			};
			if (val["sender_id"].IsUint64() && val["sender"].IsObject()) {
				uint64_t senderid = val["sender_id"].GetUint64();
				tobj->user = CheckParseUserObj(senderid, val["sender"], *this, out_of_date_state);
				adduserdmindex(tobj->user);
			}
			if (val["recipient_id"].IsUint64() && val["recipient"].IsObject()) {
				uint64_t recipientid = val["recipient_id"].GetUint64();
				tobj->user_recipient = CheckParseUserObj(recipientid, val["recipient"], *this, out_of_date_state);
				adduserdmindex(tobj->user_recipient);
			}
		}
	} else {
		UpdateTweet(*tobj);
	}

	if (sflags & JDTP::ISDM && tobj->user_recipient.get() != tac->usercont.get()) {
		tobj->flags.Set('S');
	}

	if (sflags & JDTP::TIMELINERECV) {
		if (sflags & JDTP::ISDM) {
			if (tobj->user_recipient.get() == tac->usercont.get()) {    //received DM
				if (tac->max_recvdm_id < tobj->id) {
					tac->max_recvdm_id = tobj->id;
				}
			} else {
				if (tac->max_sentdm_id < tobj->id) {
					tac->max_sentdm_id = tobj->id;
				}
			}
		} else {
			if (data->rbfs_type != RBFS_NULL) {
				if (data->rbfs_type == RBFS_TWEETS) {
					if (tac->max_tweet_id < tobj->id) {
						tac->max_tweet_id = tobj->id;
					}
				} else if (data->rbfs_type == RBFS_MENTIONS) {
					if (tac->max_mention_id < tobj->id) {
						tac->max_mention_id = tobj->id;
					}
				}
			}
			else {	//streaming mode
				if (tac->max_tweet_id < tobj->id) {
					tac->max_tweet_id = tobj->id;
				}
				if (tac->max_mention_id < tobj->id) {
					tac->max_mention_id = tobj->id;
				}
			}
		}
	}

	if (currentlogflags & LOGT::PARSE) {
		tobj->Dump();
	}

	if (tobj->lflags & TLF::SHOULDSAVEINDB) {
		tobj->flags.Set('B');
	}

	if (sflags & JDTP::ISRTSRC) {
		tp->SetRecvTypeRTSrc(true);
	}
	if (sflags & JDTP::ISQUOTE) {
		tp->SetRecvTypeQuote(true);
	}

	bool have_checked_pending = false;

	if (sflags & JDTP::CHECKPENDINGONLY) {
		tp->SetRecvTypeCPO(true);
	} else if (sflags & JDTP::USERTIMELINE) {
		if (data->rbfs_type != RBFS_NULL && !(sflags & JDTP::ISRTSRC) && !(sflags & JDTP::ISQUOTE)) {
			tp->SetRecvTypeUT(true);
			std::shared_ptr<tpanel> usertp = tpanelparentwin_usertweets::GetUserTweetTPanel(data->rbfs_userid, data->rbfs_type);
			if (usertp) {
				have_checked_pending = true;
				bool is_ready = tac->MarkPendingOrHandle(tobj, arr);
				if (is_ready) {
					usertp->PushTweet(tobj, PUSHFLAGS::USERTL | PUSHFLAGS::BELOW);
				} else {
					MarkPending_TPanelMap(tobj, 0, PUSHFLAGS::USERTL | PUSHFLAGS::BELOW, &usertp);
				}
			}
		}
	} else if (!(sflags & JDTP::DEL)) {
		tp->SetRecvTypeNorm(true);
		tobj->lflags |= TLF::SHOULDSAVEINDB;
		tobj->flags.Set('B');

		/* The JDTP::POSTDBLOAD test is because in the event that the program is not terminated cleanly,
		 * the tweet, once reloaded from the DB, will be marked as already arrived here, but will not be in the appropriate
		 * ID lists, in particular the timeline list, as those were not written out.
		 * If everything was written out, we would not be loading the same timeline tweet again.
		 */
		if ((has_just_arrived || (sflags & JDTP::POSTDBLOAD)) && sflags & JDTP::ARRIVED) {
			if (gc.markowntweetsasread && !tobj->flags.Get('u') && (tobj->flags.Get('O') || tobj->flags.Get('S'))) {
				//tweet is marked O or S, is own tweet or DM, mark read if not already unread
				tobj->flags.Set('r');
			}
			if (!tobj->flags.Get('r')) {
				tobj->flags.Set('u');
			}
			have_checked_pending = true;
			tac->MarkPendingOrHandle(tobj, arr | ARRIVAL::NEW);
		}
	}

	if (!have_checked_pending) {
		tac->MarkPendingOrHandle(tobj, arr);
	}
	TryUnmarkPendingTweet(tobj, 0);

	if (tobj->lflags & TLF::SHOULDSAVEINDB || tobj->lflags & TLF::SAVED_IN_DB) {
		if (!(tobj->lflags & TLF::SAVED_IN_DB) || (sflags & JDTP::ALWAYSREPARSE)) {
			if (json.empty()) {
				writestream wr(json);
				Handler jw(wr);
				jw.StartObject();
				ParseTweetStatics(val, tobj, &jw, false, 0, false);
				jw.EndObject();
			}
			DBC_InsertNewTweet(tobj, std::move(json), make_observer(dbmsglist));
			tobj->lflags |= TLF::SAVED_IN_DB;
		} else {
			DBC_UpdateTweetDyn(tobj, make_observer(dbmsglist));
		}
		tobj->uninserted_db_json.reset();
	} else if (!json.empty()) {
		if (!tobj->uninserted_db_json) tobj->uninserted_db_json.reset(new tweet_db_json());
		tobj->uninserted_db_json->json = std::move(json);
	}

	return tobj;
}

void jsonparser::DoEventParse(const rapidjson::Value &val) {
	using URF = user_relationship::URF;

	auto follow_update = [&](bool nowfollowing) {
		auto targ = DoUserParse(val["target"]);
		auto src = DoUserParse(val["source"]);

		if (src->id == tac->usercont->id) {
			URF flags = SetOrClearBits(URF::IFOLLOW_KNOWN, URF::IFOLLOW_TRUE, nowfollowing);
			tac->SetUserRelationship(targ->id, flags, 0);
			tac->NotifyUserRelationshipChange(targ->id, flags);
		}
		if (targ->id == tac->usercont->id) {
			URF flags = SetOrClearBits(URF::FOLLOWSME_KNOWN, URF::FOLLOWSME_TRUE, nowfollowing);
			tac->SetUserRelationship(src->id, flags, 0);
			tac->NotifyUserRelationshipChange(src->id, flags);
		}
	};

	auto favourite_update = [&](bool nowfavourited) {
		auto targ = DoUserParse(val["target"]);
		auto src = DoUserParse(val["source"]);

		flagwrapper<JDTP> sflags = JDTP::CHECKPENDINGONLY;
		if (src->id == tac->usercont->id) {
			// This user (un)favourited the tweet
			sflags |= nowfavourited ? JDTP::FAV : JDTP::UNFAV;
		}
		auto targ_tweet = DoTweetParse(val["target_object"], sflags);

		if (targ->id == tac->usercont->id && targ_tweet) {
			// Someone (un)favourited one of the user's tweets
			tac->NotifyTweetFavouriteEvent(targ_tweet->id, src->id, !nowfavourited);
		}
	};

	auto block_update = [&](BLOCKTYPE type, bool now_blocked) {
		auto targ = DoUserParse(val["target"]);
		if (targ->id != tac->usercont->id) {
			tac->SetUserIdBlockedState(targ->id, type, now_blocked);
		}
	};

	std::string str = val["event"].GetString();
	if (str == "user_update") {
		DoUserParse(val["target"]);
	} else if (str == "follow") {
		follow_update(true);
	} else if (str == "unfollow") {
		follow_update(false);
	} else if (str == "favorite") {
		favourite_update(true);
	} else if (str == "unfavorite") {
		favourite_update(false);
	} else if (str == "block") {
		block_update(BLOCKTYPE::BLOCK, true);
	} else if (str == "unblock") {
		block_update(BLOCKTYPE::BLOCK, false);
	} else if (str == "mute") {
		block_update(BLOCKTYPE::MUTE, true);
	} else if (str == "unmute") {
		block_update(BLOCKTYPE::MUTE, false);
	}
}

void jsonparser::out_of_date_data::CheckEventToplevelJson(const rapidjson::Value& val) {
	uint64_t timestamp_ms = 0;
	bool found = CheckTransJsonValue(timestamp_ms, val, "timestamp_ms");
	if (found && timestamp_ms > 0) {
		time_estimate = timestamp_ms / 1000;
		flags |= OODPEF::HAVE_REAL_TIME;
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
	LogMsgFormat(LOGT::PARSE, "id: %" llFmtSpec "u\nreply_id: %" llFmtSpec "u\nreply_user_id: %" llFmtSpec "u\n"
			"retweet_count: %u\nfavourite_count: %u\n"
			"source: %s\ntext: %s\ncreated_at: %s",
			id, in_reply_to_status_id, in_reply_to_user_id, retweet_count, favourite_count, cstr(source),
			cstr(text), cstr(ctime(&createtime)));
	IterateTP([&](const tweet_perspective &tp) {
		LogMsgFormat(LOGT::PARSE, "Perspectival attributes: %s\nretweeted: %d\nfavourited: %d\nFlags: %s",
				cstr(tp.acc->dispname), tp.IsRetweeted(), tp.IsFavourited(), cstr(tp.GetFlagString()));
	});
}
