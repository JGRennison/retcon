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
#include "twit.h"
#include "version.h"
#include "taccount.h"
#include "parse.h"
#include "log.h"
#include "db.h"
#include "tpanel.h"
#include "tpanel-data.h"
#include "dispscr.h"
#include "util.h"
#include "userui.h"
#include "alldata.h"
#include "twitcurlext.h"
#include "socket-ops.h"
#include "mainui.h"
#include "log-util.h"
#include "mediawin.h"
#include "hash.h"

#ifdef __WINDOWS__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "../deps/strptime.cpp"
#pragma GCC diagnostic pop
#endif
#include "utf8proc/utf8proc.h"
#include "utf8.h"
#include "retcon.h"
#define PCRE_STATIC
#include <pcre.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/dcmemory.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <algorithm>
#include <unordered_map>

//Do not assume that *acc is non-null
void HandleNewTweet(tweet_ptr_p t, const std::shared_ptr<taccount> &acc, flagwrapper<ARRIVAL> arr) {
	if(arr & ARRIVAL::RECV) ad.alltweet_filter.FilterTweet(*t, acc.get());
	if(arr & ARRIVAL::NEW) ad.incoming_filter.FilterTweet(*t, acc.get());
	t->CheckFlagsUpdated(tweet::CFUF::SEND_DB_UPDATE | tweet::CFUF::UPDATE_TWEET);

	if(arr & ARRIVAL::NEW) {
		for(auto &it : ad.tpanels) {
			tpanel &tp = *(it.second);
			if(tp.TweetMatches(t, acc)) {
				tp.PushTweet(t);
			}
		}
	}
}

media_entity_raii_updater::~media_entity_raii_updater() {
	me->UpdateLastUsed();
}

wxString media_entity::cached_full_filename(media_id_type media_id) {
	return wxFileName(wxstrstd(wxGetApp().datadir), wxString::Format(wxT("media_%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), media_id.m_id, media_id.t_id)).GetFullPath();
}
wxString media_entity::cached_thumb_filename(media_id_type media_id) {
	return wxFileName(wxstrstd(wxGetApp().datadir), wxString::Format(wxT("mediathumb_%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), media_id.m_id, media_id.t_id)).GetFullPath();
}

std::string media_entity::cached_video_filename(media_id_type media_id, const std::string &url) {
	sha1_hash_block url_hash;
	hash_block(url_hash, url.data(), url.size());
	wxString hash_str = hexify_wx((const char *) url_hash.hash_sha1, sizeof(url_hash.hash_sha1));
	wxFileName filename(wxstrstd(wxGetApp().tmpdir), wxString::Format(wxT("retcon_video_%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d_%s"), media_id.m_id, media_id.t_id, hash_str.c_str()));
	return stdstrwx(filename.GetFullPath());
}

void media_entity::PurgeCache(observer_ptr<dbsendmsg_list> msglist) {
	if(win) win->Close(true);
	flags &= ~(MEF::HAVE_THUMB | MEF::HAVE_FULL | MEF::LOAD_THUMB | MEF::LOAD_FULL);
	flags |= MEF::MANUALLY_PURGED;
	full_img_sha1.reset();
	thumb_img_sha1.reset();
	fulldata.clear();

	::wxRemoveFile(cached_full_filename());
	::wxRemoveFile(cached_thumb_filename());

	std::unique_ptr<dbsendmsg_list> ownbatch;
	if(!msglist) {
		ownbatch.reset(new dbsendmsg_list());
		msglist = make_observer(ownbatch);
	}
	DBC_UpdateMedia(*this, DBUMMT::THUMBCHECKSUM, msglist);
	DBC_UpdateMedia(*this, DBUMMT::FULLCHECKSUM, msglist);
	DBC_UpdateMedia(*this, DBUMMT::FLAGS, msglist);
	if(ownbatch) DBC_SendMessage(std::move(ownbatch));
}

void media_entity::ClearPurgeFlag(observer_ptr<dbsendmsg_list> msglist) {
	if(flags & MEF::MANUALLY_PURGED) {
		flags &= ~MEF::MANUALLY_PURGED;
		DBC_UpdateMedia(*this, DBUMMT::FLAGS, msglist);
	}
}

observer_ptr<media_entity> media_entity::MakeNew(media_id_type mid, std::string url) {
	observer_ptr<media_entity> me = new media_entity;
	ad.media_list[mid].reset(me.get());  // ad.media_list now holds ownership

	me->media_id = mid;
	me->media_url = std::move(url);
	me->lastused = time(nullptr);
	return me;
}

observer_ptr<media_entity> media_entity::GetExisting(media_id_type mid) {
	auto it = ad.media_list.find(mid);
	if(it != ad.media_list.end())
		return it->second.get();
	else
		return nullptr;
}


void media_entity::UpdateLastUsed(observer_ptr<dbsendmsg_list> msglist) {
	uint64_t now = time(nullptr);
	if((now - lastused) >= (60 * 60)) {
		//don't bother sending updates for small timestamp changes
		lastused = now;
		DBC_UpdateMedia(*this, DBUMMT::LASTUSED, msglist ? msglist : DBC_GetMessageBatchQueue());
	}
}

void media_entity::NotifyVideoLoadStarted(const std::string &url) {
	video_file_cache[url].Reset();
}

void media_entity::NotifyVideoLoadSuccess(const std::string &url, temp_file_holder video_file) {
	auto &vc = video_file_cache[url];
	vc = std::move(video_file);
	vc.AddToSet(wxGetApp().temp_file_set);
	if(win)
		win->NotifyVideoLoadSuccess(url);

	CheckVideoLoadSaveActions(url);
}

void media_entity::NotifyVideoLoadFailure(const std::string &url) {
	video_file_cache.erase(url);
	if(win)
		win->NotifyVideoLoadFailure(url);

	auto iterpair = pending_video_save_requests.equal_range(url);
	if(iterpair.first != iterpair.second) {
		// pending save actions
		wxString message = wxT("Couldn't save video file to:");
		for(auto it = iterpair.first; it != iterpair.second; ++it) {
			const wxString &target = it->second;
			message += wxT("\n\t");
			message += target;
		}
		message += wxT("\n\nDownload failed.");
		::wxMessageBox(message, wxT("Save Failed"), wxOK | wxICON_ERROR);
		pending_video_save_requests.erase(iterpair.first, iterpair.second);
	}
}

void media_entity::CheckVideoLoadSaveActions(const std::string &url) {
	auto iterpair = pending_video_save_requests.equal_range(url);
	if(iterpair.first == iterpair.second)
		return; // no pending save actions

	auto vc = video_file_cache.find(url);
	if(vc != video_file_cache.end() && vc->second.IsValid()) {
		// File is ready
		for(auto it = iterpair.first; it != iterpair.second; ++it) {
			const wxString &target = it->second;
			if(!::wxCopyFile(wxstrstd(vc->second.GetFilename()), target, true))
				::wxMessageBox(wxString::Format(wxT("Couldn't save video file to:\n\t%s\n\nIs directory writable?"), target.c_str()),
						wxT("Save Failed"), wxOK | wxICON_ERROR);
		}
		pending_video_save_requests.erase(iterpair.first, iterpair.second);
	}
}

std::multimap<std::string, wxString> media_entity::pending_video_save_requests;

userlookup::~userlookup() {
	UnMarkAll();
}

void userlookup::UnMarkAll() {
	while(!users_queried.empty()) {
		users_queried.front()->udc_flags &= ~UDC::LOOKUP_IN_PROGRESS;
		users_queried.pop_front();
	}
}

void userlookup::Mark(udc_ptr udc) {
	udc->udc_flags |= UDC::LOOKUP_IN_PROGRESS;
	users_queried.push_front(udc);
}

void userlookup::GetIdList(std::string &idlist) const {
	idlist = string_join(users_queried, ",", [](std::string &out, udc_ptr_p u) {
		out += std::to_string(u->id);
	});
}

std::string friendlookup::GetTwitterURL() const {
	std::string idlist = "api.twitter.com/1.1/friendships/lookup.json?user_id=";
	idlist += string_join(ids, ",", [](std::string &out, uint64_t id) {
		out += std::to_string(id);
	});
	return idlist;
}

//Trigger after 45 seconds, time-out stream if no activity after 90s (ie. 2 time periods).
//After each 45-second time period, check for odd clock activity, which would indicate suspend/other power-management activity, and if found, retry immediately
void streamconntimeout::Arm() {
	last_activity = time(nullptr);
	triggercount = 0;
	ArmTimer();
}

void streamconntimeout::ArmTimer() {
	Start(45000, wxTIMER_ONE_SHOT);
}

void streamconntimeout::Notify() {
	auto acc = tw->tacc.lock();

	//Check for clock weirdness
	time_t now = time(nullptr);
	time_t delta = now - last_activity;
	time_t expected = 45 * (triggercount + 1);
	if(delta - expected > 30) {
		//clock has jumped > 30 seconds into the future, this indicates weirdness
		LogMsgFormat(LOGT::SOCKERR, "Clock jump detected: Last activity %ds ago, expected ~%ds. Forcibly re-trying stream connection: %s, conn ID: %d",
				(int) delta, (int) expected, acc ? cstr(acc->dispname) : "", tw->id);
		std::unique_ptr<mcurlconn> mc = tw->RemoveConn();
		tw->DoRetry(std::move(mc));
		return;
	}

	if(triggercount < 1) {
		//45s in
		triggercount++;
		ArmTimer();
	}
	else {
		//90s in, timed-out
		LogMsgFormat(LOGT::SOCKERR, "Stream connection timed out: %s, conn ID: %d",
				acc ? cstr(acc->dispname) : "", tw->id);
		std::unique_ptr<mcurlconn> mc = tw->RemoveConn();
		tw->HandleError(tw->GetCurlHandle(), 0, CURLE_OPERATION_TIMEDOUT, std::move(mc));
	}
}

bool userdatacontainer::NeedsUpdating(flagwrapper<PENDING_REQ> preq, time_t timevalue) const {
	if(!lastupdate) return true;
	if(!GetUser().screen_name.size()) return true;
	if(!timevalue) timevalue = time(nullptr);
	if(preq & PENDING_REQ::USEREXPIRE) {
		if((uint64_t) timevalue > (lastupdate + gc.userexpiretime)) return true;
		else return false;
	}
	else return false;
}

bool userdatacontainer::ImgIsReady(flagwrapper<PENDING_REQ> preq) {
	if(udc_flags & UDC::IMAGE_DL_IN_PROGRESS) {
		LogMsgFormat(LOGT::OTHERTRACE, "userdatacontainer::ImgIsReady, not downloading profile image url: %s for user id %" llFmtSpec "d (@%s), "
				"as download is already in progress",
				cstr(GetUser().profile_img_url), id, cstr(GetUser().screen_name));
		return false;
	}
	if(!(preq & PENDING_REQ::PROFIMG_NEED)) return true;
	if(user.profile_img_url.size()) {
		if(cached_profile_img_url != user.profile_img_url) {
			if(udc_flags & UDC::PROFILE_IMAGE_DL_FAILED) return true;
			if(preq & PENDING_REQ::PROFIMG_DOWNLOAD_FLAG) {
				profileimgdlconn::NewConn(user.profile_img_url, udc_ptr_p(this));

				// New image, bump last used timestamp to prevent it being evicted prior to display
				profile_img_last_used = time(nullptr);
			}
			else {
				LogMsgFormat(LOGT::OTHERTRACE, "userdatacontainer::ImgIsReady, not downloading profile image url: %s for user id %" llFmtSpec "d (@%s), "
						"as PENDING_REQ::PROFIMG_DOWNLOAD_FLAG is not set",
						cstr(GetUser().profile_img_url), id, cstr(GetUser().screen_name));
			}
			return false;
		}
		else if(cached_profile_img_url.size() && !(udc_flags & UDC::PROFILE_BITMAP_SET))  {
			struct job_data {
				wxImage img;
				wxString filename;
				std::string url;
				udc_ptr u;
				shb_iptr hash;
				bool success;
			};
			auto data = std::make_shared<job_data>();
			GetImageLocalFilename(data->filename);
			data->url = cached_profile_img_url;
			data->u = this;
			data->hash = cached_profile_img_sha1;

			LogMsgFormat(LOGT::FILEIOTRACE, "userdatacontainer::ImgIsReady, about to load cached profile image for user id: %" llFmtSpec "d (%s), file: %s, url: %s",
					id, cstr(GetUser().screen_name), cstr(data->filename), cstr(cached_profile_img_url));

			udc_flags |= UDC::IMAGE_DL_IN_PROGRESS;
			wxGetApp().EnqueueThreadJob([this, data]() {
				wxImage img;
				data->success = LoadImageFromFileAndCheckHash(data->filename, data->hash, img);
				if(data->success) data->img = userdatacontainer::ScaleImageToProfileSize(img);
			},
			[data, preq]() {
				udc_ptr &u = data->u;

				u->udc_flags &= ~UDC::IMAGE_DL_IN_PROGRESS;

				if(data->url != u->cached_profile_img_url) {
					LogMsgFormat(LOGT::OTHERERR, "userdatacontainer::ImgIsReady, cached profile image read from file, which did correspond to url: %s for user id %" llFmtSpec "d (@%s), "
							"does not match current url of: %s. Maybe user updated profile during read?",
							cstr(data->url), u->id, cstr(u->GetUser().screen_name), cstr(u->GetUser().profile_img_url));
					//Try again:
					u->ImgIsReady(preq);
					return;
				}

				if(data->success) {
					u->SetProfileBitmap(wxBitmap(data->img));
					u->NotifyProfileImageChange();
				}
				else {
					LogMsgFormat(LOGT::FILEIOERR, "userdatacontainer::ImgIsReady, cached profile image file for user id: %" llFmtSpec "d (%s), file: %s, url: %s, missing, invalid or failed hash check",
						u->id, cstr(u->GetUser().screen_name), cstr(data->filename), cstr(u->cached_profile_img_url));
					u->cached_profile_img_url.clear();
					if(preq & PENDING_REQ::PROFIMG_DOWNLOAD_FLAG) {    //the saved image is not loadable, clear cache and re-download
						profileimgdlconn::NewConn(u->GetUser().profile_img_url, u);
					}
				}
			});
			return false;
		}
		else {
			return true;
		}
	}
	else {
		LogMsgFormat(LOGT::OTHERTRACE, "userdatacontainer::ImgIsReady, profile_img_url is empty for user id %" llFmtSpec "d (@%s), ",
				id, cstr(GetUser().screen_name));
		return false;
	}
}

bool userdatacontainer::ImgHalfIsReady(flagwrapper<PENDING_REQ> preq) {
	bool res = ImgIsReady(preq);
	if(res && !(udc_flags & UDC::HALF_PROFILE_BITMAP_SET)) {
		wxImage img = cached_profile_img.ConvertToImage();
		cached_profile_img_half = wxBitmap(ScaleImageToProfileSize(img, 0.5));
		udc_flags |= UDC::HALF_PROFILE_BITMAP_SET;
	}
	return res;
}

flagwrapper<PENDING_RESULT> userdatacontainer::GetPending(flagwrapper<PENDING_REQ> preq, time_t timevalue) {
	flagwrapper<PENDING_RESULT> result;
	if(preq & PENDING_REQ::PROFIMG_NEED) {
		if(ImgIsReady(preq)) {
			result |= PENDING_RESULT::PROFIMG_READY;
		}
		else {
			result |= PENDING_RESULT::PROFIMG_NOT_READY;
		}
	}
	if(!NeedsUpdating(preq, timevalue) && !(udc_flags & UDC::BEING_LOADED_FROM_DB)
			&& !((udc_flags & UDC::LOOKUP_IN_PROGRESS) && (preq & PENDING_REQ::USEREXPIRE))
			&& !(GetUser().profile_img_url.empty() && (preq & PENDING_REQ::PROFIMG_NEED))) {
		result |= PENDING_RESULT::CONTENT_READY;
	}
	else {
		result |= PENDING_RESULT::CONTENT_NOT_READY;
	}
	return result;
}

void userdatacontainer::CheckPendingTweets(flagwrapper<UMPTF> umpt_flags, optional_observer_ptr<taccount> acc) {
	FreezeAll();
	std::vector<tweet_pending_bits_guard> stillpending;
	stillpending.reserve(pendingtweets.size());
	for(auto &it : pendingtweets) {
		stillpending.emplace_back(TryUnmarkPendingTweet(it, umpt_flags, acc));
	}
	pendingtweets.clear();

	// All the guards will now run
	// Do this here instead of in loop to avoid editing pendingtweets
	// whilst iterating over it
	stillpending.clear();

	if(udc_flags & UDC::WINDOWOPEN) {
		user_window *uw = user_window::GetWin(id);
		if(uw) uw->Refresh();
	}
	if(udc_flags & UDC::CHECK_USERLISTWIN) {
		udc_flags &= ~UDC::CHECK_USERLISTWIN;
		tpanelparentwin_user::CheckPendingUser(udc_ptr_p(this));
	}
	if(udc_flags & UDC::CHECK_STDFUNC_LIST) {
		udc_flags &= ~UDC::CHECK_STDFUNC_LIST;
		auto range = ad.user_load_pending_funcs.equal_range(id);
		for(auto it = range.first; it != range.second; ++it) {
			it->second(udc_ptr_p(this));
		}
		ad.user_load_pending_funcs.erase(range.first, range.second);
	}
	ThawAll();
}

void userdatacontainer::MarkTweetPending(tweet_ptr_p t) {
	if(std::find(pendingtweets.begin(), pendingtweets.end(), t) != pendingtweets.end()) {
		return;
	}
	pendingtweets.push_back(t);
	LogMsgFormat(LOGT::PENDTRACE, "Mark Pending: User: %" llFmtSpec "d (@%s) --> %s", id, cstr(GetUser().screen_name), cstr(tweet_log_line(t.get())));
}

void rt_pending_op::MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) {
	TryUnmarkPendingTweet(target_retweet, umpt_flags);
}

std::string rt_pending_op::dump() {
	return string_format("Retweet depends on this: %s", cstr(tweet_log_line(target_retweet.get())));
}

handlenew_pending_op::handlenew_pending_op(const std::shared_ptr<taccount> &acc, flagwrapper<ARRIVAL> arr_, uint64_t tweet_id_)
		: tac(acc), arr(arr_), tweet_id(tweet_id_) {
	ad.handlenew_pending_ops.insert(this);
}

void handlenew_pending_op::MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) {
	HandleNewTweet(t, tac.lock(), arr);
}

std::string handlenew_pending_op::dump() {
	std::shared_ptr<taccount> acc = tac.lock();
	return string_format("Handle arrived on account: %s, 0x%X", acc ? cstr(acc->dispname) : "N/A", arr.get());
}

tweet_pending_bits_guard::tweet_pending_bits_guard(tweet_pending_bits_guard &&other) {
	bits = std::move(other.bits);
	acc = std::move(other.acc);
	t = std::move(other.t);
	other.reset();
}

tweet_pending_bits_guard::~tweet_pending_bits_guard() {
	if(bits && t)
		DoMarkTweetPending(t, bits, acc);
}

void tweet_pending_bits_guard::reset() {
	bits = 0;
	acc.reset();
	t.reset();
}

tweet_pending_bits_guard TryUnmarkPendingTweet(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags, optional_observer_ptr<taccount> acc) {
	LogMsgFormat(LOGT::PENDTRACE, "Try Unmark Pending: %s, pending ops: %zu", cstr(tweet_log_line(t.get())), t->pending_ops.size());
	tweet_pending_bits_guard result;
	std::vector<std::unique_ptr<pending_op> > still_pending;
	for(auto &it : t->pending_ops) {
		tweet_pending tp = t->IsPending(it->preq);
		if(tp.IsReady(it->presult_required))
			it->MarkUnpending(t, umpt_flags);
		else {
			still_pending.emplace_back(std::move(it));
			result.bits |= tp.bits;
		}
	}
	t->pending_ops = std::move(still_pending);
	if(t->pending_ops.empty())
		t->lflags &= ~TLF::BEINGLOADEDFROMDB;

	if(result.bits) {
		result.acc = acc;
		result.t = t;
	}

	LogMsgFormat(LOGT::PENDTRACE, "Try Unmark Pending: end: for %" llFmtSpec "u, still pending ops: %zu, still pending: 0x%X",
			t->id, t->pending_ops.size(), static_cast<unsigned int>(result.bits.get()));
	return result;
}

std::shared_ptr<taccount> userdatacontainer::GetAccountOfUser() const {
	for(auto &it : alist) {
		if(it->usercont.get() == this) return it;
	}
	return std::shared_ptr<taccount>();
}

void userdatacontainer::GetImageLocalFilename(uint64_t id, wxString &filename) {
	filename.Printf(wxT("/img_%" wxLongLongFmtSpec "d"), id);
	filename.Prepend(wxstrstd(wxGetApp().datadir));
}

void userdatacontainer::MarkUpdated() {
	lastupdate = time(nullptr);
}

std::string userdatacontainer::mkjson() const {
	std::string json;
	writestream wr(json);
	Handler jw(wr);
	jw.StartObject();
	jw.String("name");
	jw.String(user.name);
	jw.String("screen_name");
	jw.String(user.screen_name);
	jw.String("description");
	jw.String(user.description);
	jw.String("url");
	jw.String(user.userurl);
	jw.String("profile_img_url");
	jw.String(user.profile_img_url);
	jw.String("protected");
	jw.Bool(user.u_flags & userdata::userdata::UF::ISPROTECTED);
	jw.String("verified");
	jw.Bool(user.u_flags & userdata::userdata::UF::ISVERIFIED);
	jw.String("followers_count");
	jw.Uint(user.followers_count);
	jw.String("statuses_count");
	jw.Uint(user.statuses_count);
	jw.String("friends_count");
	jw.Uint(user.friends_count);
	if(!user.notes.empty()) {
		jw.String("retcon_notes");
		jw.String(user.notes);
	}
	jw.EndObject();
	return json;
}

wxImage userdatacontainer::ScaleImageToProfileSize(const wxImage &img, double limitscalefactor) {
	int maxdim = (gc.maxpanelprofimgsize * limitscalefactor);
	if(img.GetHeight()>maxdim || img.GetWidth()>maxdim) {
		double scalefactor = (double) maxdim / (double) std::max(img.GetHeight(), img.GetWidth());
		int newwidth = (double) img.GetWidth() * scalefactor;
		int newheight = (double) img.GetHeight() * scalefactor;
		return img.Scale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH);
	}
	else return img;
}

void userdatacontainer::SetProfileBitmap(const wxBitmap &bmp) {
	cached_profile_img = bmp;
	udc_flags |= UDC::PROFILE_BITMAP_SET;
}

bool userdatacontainer::GetUsableAccount(std::shared_ptr<taccount> &tac, bool enabledonly) const {
	using URF = user_relationship::URF;

	if(tac) {
		if(!enabledonly || tac->enabled)
			return true;
		else
			tac.reset(); // suuplied account not enabled as required
	}

	//look through pending tweets for new arrivals
	for(auto &it : pendingtweets) {
		for(auto &jt : it->pending_ops) {
			handlenew_pending_op *hnop = dynamic_cast<handlenew_pending_op *>(jt.get());
			if(hnop) {
				std::shared_ptr<taccount> acc = hnop->tac.lock();
				if(acc && (!enabledonly || acc->enabled)) {
					tac = std::move(acc);
					return true;
				}
			}
		}
	}

	//look for users who we follow, or who follow us
	for(auto &it : alist) {
		taccount &acc = *it;
		if(!enabledonly || acc.enabled) {
			auto rel = acc.user_relations.find(id);
			if(rel != acc.user_relations.end()) {
				if(rel->second.ur_flags & (URF::FOLLOWSME_TRUE | URF::IFOLLOW_TRUE)) {
					tac = it;
					return true;
				}
			}
		}
	}

	//otherwise find the first enabled account
	for(auto &it : alist) {
		if(it->enabled) {
			tac = it;
			return true;
		}
	}

	//otherwise find the first account
	if(!alist.empty())
		tac = alist.front();

	if(!tac)
		return false;
	if(!enabledonly || tac->enabled)
		return true;
	else
		return false;
}

const tweetidset &userdatacontainer::GetMentionSet() {
	if(!msd) msd.reset(new mention_set_data);
	if(msd->added_offset < mention_index.size()) {
		msd->mention_set.insert(mention_index.begin() + msd->added_offset, mention_index.end());
		msd->added_offset = mention_index.size();
	}
	return msd->mention_set;
}

void user_dm_index::AddDMId(uint64_t id) {
	flags |= UDIF::ISDIRTY;
	ids.insert(id);
}

std::string tweet_flags::GetValueString(unsigned long long bitint) {
	std::string out;
	while(bitint) {
		int offset = __builtin_ctzll(bitint);
		bitint &= ~(((uint64_t) 1) << offset);
		out += GetFlagChar(offset);
	}
	return out;
}

std::string tweet_perspective::GetFlagString() const {
	std::string output;
	output.reserve(8);
	unsigned int bit = 0;
	auto addchar = [&](char c) {
		if(flags & (1 << bit)) output.push_back(c);
		else output.push_back('-');
		bit++;
	};
	addchar('A');
	addchar('F');
	addchar('R');
	addchar('r');
	addchar('D');
	addchar('P');
	addchar('U');
	addchar('N');
	addchar('T');
	return std::move(output);
}

std::string tweet_perspective::GetFlagStringWithName(bool always) const {
	if(flags || always) {
		return stdstrwx(acc->dispname) + ": " + GetFlagString();
	}
	else return "";
}

bool tweet::GetUsableAccount(std::shared_ptr<taccount> &tac, flagwrapper<GUAF> guaflags) const {
	if(guaflags & GUAF::CHECKEXISTING) {
		if(tac && tac->enabled) return true;
	}
	bool retval = false;
	IterateTP([&](const tweet_perspective &tp) {
		if(tp.IsArrivedHere()) {
			if(tp.acc->enabled || (guaflags & GUAF::USERENABLED && tp.acc->userenabled)) {
				tac = tp.acc;
				retval = true;
			}
		}
	});
	if(retval) return true;

	//try again, but use any associated account
	IterateTP([&](const tweet_perspective &tp) {
		if(tp.acc->enabled || (guaflags & GUAF::USERENABLED && tp.acc->userenabled)) {
			tac = tp.acc;
			retval = true;
		}
	});
	if(retval) return true;

	//use the first account which is actually enabled
	for(auto &it : alist) {
		if(it->enabled || (guaflags & GUAF::USERENABLED && it->userenabled)) {
			tac = it;
			return true;
		}
	}
	if(!(guaflags & GUAF::NOERR)) {
		LogMsgFormat(LOGT::OTHERERR, "Tweet has no usable enabled account, cannot perform network actions on tweet: %s", cstr(tweet_log_line(this)));
	}
	return false;
}

tweet_perspective *tweet::AddTPToTweet(const std::shared_ptr<taccount> &tac, bool *isnew) {
	if(!(lflags & TLF::HAVEFIRSTTP)) {
		first_tp.Reset(tac);
		if(isnew) *isnew = true;
		lflags |= TLF::HAVEFIRSTTP;
		return &first_tp;
	}
	else if(first_tp.acc.get() == tac.get()) {
		if(isnew) *isnew = false;
		return &first_tp;
	}

	for(auto &it : tp_extra_list) {
		if(it.acc.get() == tac.get()) {
			if(isnew) *isnew = false;
			return &it;
		}
	}
	tp_extra_list.emplace_back(tac);
	if(isnew) *isnew = true;
	return &tp_extra_list.back();
}

tweet_perspective *tweet::GetTweetTP(const std::shared_ptr<taccount> &tac) {
	if(lflags & TLF::HAVEFIRSTTP && first_tp.acc.get() == tac.get()) {
		return &first_tp;
	}
	for(auto &it : tp_extra_list) {
		if(it.acc.get() == tac.get()) {
			return &it;
		}
	}
	return nullptr;
}

void tweet::MarkFlagsAsRead() {
	flags.Set('r', true);
	flags.Set('u', false);
}

void tweet::MarkFlagsAsUnread() {
	flags.Set('r', false);
	flags.Set('u', true);
}

void tweet::ClearDeadPendingOps() {
	container_unordered_remove_if(pending_ops, [](const std::unique_ptr<pending_op> &op) {
		return !op->IsAlive();
	});
}

void cached_id_sets::CheckTweet(tweet &tw) {
	IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
		if(tw.flags.ToULLong() & flagvalue) (this->*mptr).insert(tw.id);
		else (this->*mptr).erase(tw.id);
	});
}

void cached_id_sets::RemoveTweet(uint64_t id) {
	IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
		(this->*mptr).erase(id);
	});
}

std::string cached_id_sets::DumpInfo() {
	std::string out;
	IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
		if(!out.empty()) {
			out += ", ";
		}
		out += string_format("%s: %zu", cstr(name), (this->*ptr).size());
	});
	return out;
}

void MarkTweetIDSetCIDS(const tweetidset &ids, tpanel *exclude, tweetidset cached_id_sets::* idsetptr, bool remove, std::function<void(tweet_ptr_p )> existingtweetfunc) {
	tweetidset &globset = ad.cids.*idsetptr;

	if(remove) {
		for(auto &tweet_id : ids) globset.erase(tweet_id);
	}
	else globset.insert(ids.begin(), ids.end());

	if(exclude)
		exclude->NotifyCIDSChange_AddRemove_Bulk(ids, idsetptr, !remove);

	for(auto &tpiter : ad.tpanels) {
		tpanel *tp = tpiter.second.get();
		if(tp == exclude) continue;

		tweetidset &tpset = tp->cids.*idsetptr;
		bool updatetp = false;

		for(auto &tweet_id : ids) {
			tp->NotifyCIDSChange_AddRemove(tweet_id, idsetptr, !remove, PUSHFLAGS::SETNOUPDATEFLAG);
			if(remove) {
				auto item_it = tpset.find(tweet_id);
				if(item_it != tpset.end()) {
					tpset.erase(item_it);
					updatetp = true;
				}
			}
			else if(tp->tweetlist.find(tweet_id) != tp->tweetlist.end()) {
				auto res = tpset.insert(tweet_id);
				if(res.second) updatetp = true;
			}
		}
		if(updatetp) {
			tp->SetNoUpdateFlag_TP();
			tp->SetClabelUpdatePendingFlag_TP();
		}
	}

	if(existingtweetfunc) {
		for(auto &tweet_id : ids) {
			auto twshpp = ad.GetExistingTweetById(tweet_id);
			if(twshpp) {
				existingtweetfunc(twshpp);
			}
		}
	}
}

void SendTweetFlagUpdate(const tweet &tw, unsigned long long mask) {
	tweetidset ids;
	ids.insert(tw.id);
	unsigned long long setmask = mask & tw.flags.ToULLong();
	unsigned long long unsetmask = mask & (~tw.flags.ToULLong());
	std::unique_ptr<dbupdatetweetsetflagsmsg>msg(new dbupdatetweetsetflagsmsg(std::move(ids), setmask, unsetmask));
	DBC_SendMessageBatched(std::move(msg));
}

namespace pending_detail {
	void TweetMarkPendingNonAcc(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark) {
		if(mark & PENDING_BITS::T_U) t->user->MarkTweetPending(t);
		if(mark & PENDING_BITS::T_UR) t->user_recipient->MarkTweetPending(t);
		if(mark & PENDING_BITS::RT_RTU) t->rtsrc->user->MarkTweetPending(t->rtsrc);
		if(mark & PENDING_BITS::RT_MISSING) {
			bool insertnewrtpo = true;
			for(auto &it : t->rtsrc->pending_ops) {
				rt_pending_op *rtpo = dynamic_cast<rt_pending_op*>(it.get());
				if(rtpo && rtpo->target_retweet == t) {
					insertnewrtpo = false;
					break;
				}
			}
			if(insertnewrtpo) t->rtsrc->AddNewPendingOp(new rt_pending_op(t));
		}
	}

	//return true if successfully marked pending
	bool TweetMarkPendingNoAccFallback(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark) {
		TweetMarkPendingNonAcc(t, mark);

		if(mark & PENDING_BITS::ACCMASK) {
			bool have_set_noacc_pending = false;
			auto do_mark = [&](udc_ptr_p u) {
				if(!CheckIfUserAlreadyInDBAndLoad(u)) {
					ad.noacc_pending_userconts[u->id] = u;
					have_set_noacc_pending = true;
				}
			};

			if(mark & PENDING_BITS::U) do_mark(t->user);
			if(mark & PENDING_BITS::UR) do_mark(t->user_recipient);
			if(mark & PENDING_BITS::RTU) do_mark(t->rtsrc->user);

			if(have_set_noacc_pending) {
				LogMsgFormat(LOGT::PENDTRACE, "FastMarkPendingNoAccFallback: Cannot mark pending as there is no usable account, %s", cstr(tweet_log_line(t.get())));
				return false;
			}
		}
		return true;
	}


	void TweetMarkPendingAcc(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark, taccount &acc) {
		TweetMarkPendingNonAcc(t, mark);

		auto do_mark = [&](udc_ptr_p u) {
			if(!CheckIfUserAlreadyInDBAndLoad(u))
				acc.MarkUserPending(u);
		};

		if(mark & PENDING_BITS::U) do_mark(t->user);
		if(mark & PENDING_BITS::UR) do_mark(t->user_recipient);
		if(mark & PENDING_BITS::RTU) do_mark(t->rtsrc->user);
	}

	template<typename F> bool GenericCheckMarkTweetPending(tweet_ptr_p t, flagwrapper<PENDING_REQ> preq, flagwrapper<PENDING_RESULT> presult, F function) {
		tweet_pending tp = t->IsPending(preq);
		if(tp.IsReady(presult)) {
			return true;
		}
		else {
			function(tp.bits);
			return false;
		}
	}
}

void DoMarkTweetPending(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark, optional_observer_ptr<taccount> acc) {
	if(mark & PENDING_BITS::ACCMASK) {
		if(acc)
			pending_detail::TweetMarkPendingAcc(t, mark, *acc);
		else {
			std::shared_ptr<taccount> curacc;
			DoMarkTweetPending_AccHint(t, mark, curacc, 0);
		}
	}
	else
		pending_detail::TweetMarkPendingNonAcc(t, mark);
}


void DoMarkTweetPending_AccHint(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark,
		std::shared_ptr<taccount> &acc_hint, flagwrapper<tweet::GUAF> guaflags) {

	if(mark & PENDING_BITS::ACCMASK) {
		if(t->GetUsableAccount(acc_hint, guaflags))
			pending_detail::TweetMarkPendingAcc(t, mark, *acc_hint);
		else
			pending_detail::TweetMarkPendingNoAccFallback(t, mark);
	}
	else
		pending_detail::TweetMarkPendingNonAcc(t, mark);
}

// returns true if ready, false is pending
bool CheckMarkTweetPending(tweet_ptr_p t, optional_observer_ptr<taccount> acc, flagwrapper<PENDING_REQ> preq, flagwrapper<PENDING_RESULT> presult) {
	return pending_detail::GenericCheckMarkTweetPending(t, preq, presult, [&](flagwrapper<PENDING_BITS> mark) {
		DoMarkTweetPending(t, mark, acc);
	});
}

// returns true if ready, false is pending
bool CheckMarkTweetPending_AccHint(tweet_ptr_p t, std::shared_ptr<taccount> &acc_hint, flagwrapper<tweet::GUAF> guaflags,
		flagwrapper<PENDING_REQ> preq, flagwrapper<PENDING_RESULT> presult) {

	return pending_detail::GenericCheckMarkTweetPending(t, preq, presult, [&](flagwrapper<PENDING_BITS> mark) {
		DoMarkTweetPending_AccHint(t, mark, acc_hint, guaflags);
	});
}

// returns true if already in DB, and DB load issued *or* if DB load already in progress
bool CheckIfUserAlreadyInDBAndLoad(udc_ptr_p u) {
	if(u->udc_flags & UDC::BEING_LOADED_FROM_DB)
		return true;
	if(ad.unloaded_db_user_ids.find(u->id) != ad.unloaded_db_user_ids.end()) {
		u->udc_flags |= UDC::BEING_LOADED_FROM_DB;

		LogMsgFormat(LOGT::PENDTRACE, "CheckIfUserAlreadyInDBAndLoad: Issuing asynchronous load for: %" llFmtSpec "u", u->id);
		std::unique_ptr<dbselusermsg> msg(new dbselusermsg());
		msg->id_set.insert(u->id);
		DBC_PrepareStdUserLoadMsg(*msg);
		DBC_SendMessageBatched(std::move(msg));
		return true;
	}
	return false;
}

bool MarkPending_TPanelMap(tweet_ptr_p tobj, tpanelparentwin_nt* win_, PUSHFLAGS pushflags, std::shared_ptr<tpanel> *pushtpanel_) {
	tpanel *tp = nullptr;
	if(pushtpanel_) tp = (*pushtpanel_).get();
	bool found = false;
	for(auto &it : tobj->pending_ops) {
		tpanelload_pending_op *op = dynamic_cast<tpanelload_pending_op *>(it.get());
		if(!op) continue;
		if(win_ && op->win.get() != win_) continue;
		if(tp) {
			std::shared_ptr<tpanel> test_tp = op->pushtpanel.lock();
			if(test_tp.get() != tp) continue;
		}
		found = true;
		break;
	}
	if(!found)
		tobj->AddNewPendingOp(new tpanelload_pending_op(win_, pushflags, pushtpanel_));
	return found;
}

//Returns true if ready now
//If existing_dbsel is given, any DB lookup message is stored in/added to it
//Otherwise any individual DB lookup is executed batched
bool CheckFetchPendingSingleTweet(tweet_ptr_p tobj, std::shared_ptr<taccount> acc_hint, std::unique_ptr<dbseltweetmsg> *existing_dbsel,
		flagwrapper<PENDING_REQ> preq, flagwrapper<PENDING_RESULT> presult) {
	using GUAF = tweet::GUAF;

	bool isready = false;

	if(tobj->text.size()) {
		isready = CheckMarkTweetPending_AccHint(tobj, acc_hint, GUAF::CHECKEXISTING | GUAF::NOERR, preq, presult);
	}
	else {	//tweet not loaded at all
		if(!(tobj->lflags & TLF::BEINGLOADEDFROMDB) && !(tobj->lflags & TLF::BEINGLOADEDOVERNET)) {
			if(ad.unloaded_db_tweet_ids.find(tobj->id) == ad.unloaded_db_tweet_ids.end()) {
				//tweet is not listed as stored in DB, don't bother querying it first
				std::shared_ptr<taccount> curacc = acc_hint;
				CheckLoadSingleTweet(tobj, curacc);
			}
			else {
				std::unique_ptr<dbseltweetmsg> own_loadmsg;
				dbseltweetmsg *loadmsg;
				if(existing_dbsel && *existing_dbsel) {
					loadmsg = existing_dbsel->get();
				}
				else if(existing_dbsel) {
					loadmsg = new dbseltweetmsg;
					existing_dbsel->reset(loadmsg);
				}
				else {
					loadmsg = new dbseltweetmsg;
					own_loadmsg.reset(loadmsg);
				}

				tobj->lflags |= TLF::BEINGLOADEDFROMDB;

				loadmsg->id_set.insert(tobj->id);
				DBC_PrepareStdTweetLoadMsg(*loadmsg);
				loadmsg->flags |= DBSTMF::NO_ERR | DBSTMF::CLEARNOUPDF;
				if(own_loadmsg) DBC_SendMessageBatched(std::move(own_loadmsg));
			}
		}
	}
	return isready;
}

//returns true on success, otherwise add tweet to pending list
//modifies acc_hint to account actually used
bool CheckLoadSingleTweet(tweet_ptr_p t, std::shared_ptr<taccount> &acc_hint) {
	if(t->GetUsableAccount(acc_hint, tweet::GUAF::CHECKEXISTING)) {
		t->lflags |= TLF::BEINGLOADEDOVERNET;
		std::unique_ptr<twitcurlext_simple> twit = twitcurlext_simple::make_new(acc_hint, twitcurlext_simple::CONNTYPE::SINGLETWEET);
		twit->extra_id = t->id;
		twitcurlext::QueueAsyncExec(std::move(twit));
		return true;
	}
	else {
		ad.noacc_pending_tweetobjs[t->id] = t;
		LogMsgFormat(LOGT::OTHERERR, "Cannot lookup tweet: id: %" llFmtSpec "d, no usable account, marking pending.", t->id);
		return false;
	}
}

tweet_pending tweet::IsPending(flagwrapper<PENDING_REQ> preq) {
	tweet_pending result;
	PENDING_RESULT_combiner presult(result.result);

	if(user) {
		result.result = user->GetPending(preq, createtime);
		if(result.result & PENDING_RESULT::NOT_READY) result.bits |= PENDING_BITS::T_U;
		if(result.result & PENDING_RESULT::CONTENT_NOT_READY) result.bits |= PENDING_BITS::U;
	}
	else presult.Combine(PENDING_RESULT::CONTENT_NOT_READY);

	if(flags.Get('D')) {
		if(!user_recipient) presult.Combine(PENDING_RESULT::CONTENT_NOT_READY);
		else {
			flagwrapper<PENDING_RESULT> user_result = user_recipient->GetPending(preq, createtime);
			presult.Combine(user_result);
			if(user_result & PENDING_RESULT::NOT_READY) result.bits |= PENDING_BITS::T_UR;
			if(user_result & PENDING_RESULT::CONTENT_NOT_READY) result.bits |= PENDING_BITS::UR;
		}
	}

	if(rtsrc) {
		if(rtsrc->createtime == 0 || !rtsrc->user) {
			//Retweet source is not inited at all
			result.bits |= PENDING_BITS::RT_MISSING;
			presult.Combine(PENDING_RESULT::CONTENT_NOT_READY);
		}
		else {
			tweet_pending rtp = rtsrc->IsPending(preq);
			presult.Combine(rtp.result);
			if(rtp.bits & PENDING_BITS::U) result.bits |= PENDING_BITS::RTU;
			if(rtp.bits & PENDING_BITS::T_U) result.bits |= PENDING_BITS::RT_RTU | PENDING_BITS::RT_MISSING;
		}
	}

	return result;
}

bool taccount::MarkPendingOrHandle(tweet_ptr_p t, flagwrapper<ARRIVAL> arr) {
	bool isready = CheckMarkTweetPending(t, this);
	if(arr) {
		if(isready)
			HandleNewTweet(t, shared_from_this(), arr);
		else
			t->AddNewPendingOp(new handlenew_pending_op(shared_from_this(), arr, t->id));
	}
	return isready;
}

//this is paired with genjsonparser::ParseTweetDyn
std::string tweet::mkdynjson() const {
	std::string json;
	writestream wr(json, 64);
	Handler jw(wr);
	jw.StartObject();
	jw.String("p");
	jw.StartArray();
	IterateTP([&](const tweet_perspective &tp) {
		jw.StartObject();
		jw.String("f");
		jw.Uint(tp.Save());
		jw.String("a");
		jw.Uint(tp.acc->dbindex);
		jw.EndObject();
	});
	jw.EndArray();
	if(retweet_count) {
		jw.String("r");
		jw.Uint(retweet_count);
	}
	if(favourite_count) {
		jw.String("f");
		jw.Uint(favourite_count);
	}
	jw.EndObject();
	return json;
}

std::string tweet::GetPermalink() const {
	if(!user || !user->GetUser().screen_name.size()) return "";
	return "http" + std::string(flags.Get('s') ? "s" : "") + "://twitter.com/" + user->GetUser().screen_name + "/status/" + std::to_string(id);
}

void tweet::GetMediaEntities(std::vector<media_entity *> &out, flagwrapper<MEF> mask) const {
	for(const entity &et  : entlist) {
		if((et.type == ENT_MEDIA || et.type == ENT_URL_IMG) && et.media_id) {
			media_entity &me = *(ad.media_list[et.media_id]);
			if(!mask || me.flags & mask) out.push_back(&me);
		}
	}
}

void tweet::CheckFlagsUpdated(flagwrapper<tweet::CFUF> cfuflags) {
	unsigned long long changemask = flags_at_prev_update.ToULLong() ^ flags.ToULLong();
	if(!changemask) return;

	flags_at_prev_update = flags;

	cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
		if(changemask & flagvalue) {
			if(flags.ToULLong() & flagvalue) {
				auto result = (ad.cids.*mptr).insert(id);
				if(result.second) {
					//new insertion
					for(auto &tpiter : ad.tpanels) {
						tpiter.second->NotifyCIDSChange(id, mptr, true, 0);
					}
				}
			}
			else {
				size_t erasecount = (ad.cids.*mptr).erase(id);
				if(erasecount) {
					//did remove
					for(auto &tpiter : ad.tpanels) {
						tpiter.second->NotifyCIDSChange(id, mptr, false, 0);
					}
				}
			}
		}
	});

	if(cfuflags & CFUF::SET_NOUPDF_ALL) CheckClearNoUpdateFlag_All();
	if(cfuflags & CFUF::UPDATE_TWEET) UpdateTweet(*this, false);
	if(cfuflags & CFUF::SEND_DB_UPDATE_ALWAYS) SendTweetFlagUpdate(*this, changemask);
	else if(cfuflags & CFUF::SEND_DB_UPDATE) {
		if(lflags & TLF::SAVED_IN_DB) SendTweetFlagUpdate(*this, changemask);
	}
}

std::string userdatacontainer::GetPermalink(bool ssl) const {
	if(!GetUser().screen_name.size()) return "";
	return "http" + std::string(ssl ? "s" : "") + "://twitter.com/" + GetUser().screen_name;
}

void userdatacontainer::NotifyProfileImageChange() {
	LogMsgFormat(LOGT::OTHERTRACE, "userdatacontainer::NotifyProfileImageChange %" llFmtSpec "d", id);
	CheckPendingTweets();
	UpdateUsersTweet(id, true);
	if(udc_flags & UDC::WINDOWOPEN) user_window::CheckRefresh(id, true);
}

void userdatacontainer::MakeProfileImageFailurePlaceholder() {
	if(!(udc_flags & UDC::PROFILE_BITMAP_SET)) {	//generate a placeholder image
		cached_profile_img.Create(48, 48, -1);
		wxMemoryDC dc(cached_profile_img);
		dc.SetBackground(wxBrush(wxColour(0, 0, 0, wxALPHA_TRANSPARENT)));
		dc.Clear();
		udc_flags |= UDC::PROFILE_BITMAP_SET;
	}
	udc_flags &= ~UDC::IMAGE_DL_IN_PROGRESS;
	udc_flags &= ~UDC::HALF_PROFILE_BITMAP_SET;
	udc_flags |= UDC::PROFILE_IMAGE_DL_FAILED;
	CheckPendingTweets();
}

bool tweet::IsFavouritable() const {
	return flags.Get('T');
}

bool tweet::IsRetweetable() const {
	return (flags.Get('T') && (rtsrc || !(user->GetUser().u_flags & userdata::userdata::UF::ISPROTECTED)));
}

bool tweet::IsArrivedHereAnyPerspective() const {
	bool res = false;
	IterateTP([&](const tweet_perspective &tp) {
		if(tp.IsArrivedHere()) res = true;
	});
	return res;
}

#ifdef __WINDOWS__
#ifndef gmtime_r
struct tm *gmtime_r(const time_t *timer, struct tm *result) {
	struct tm *local_result;
	local_result = gmtime(timer);

	if(!local_result || !result) return nullptr;

	memcpy (result, local_result, sizeof (struct tm));
	return result;
}
#endif

// This function is from https://web.nlcindia.com/gpsd/gpsd-3.1/gpsutils.c (BSD license)
static time_t our_mkgmtime(struct tm * t)
/* struct tm to seconds since Unix epoch */
{
	const int MONTHSPERYEAR = 12;

	int year;
	time_t result;
	static const int cumdays[MONTHSPERYEAR] =
		{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

	/*@ +matchanyintegral @*/
	year = 1900 + t->tm_year + t->tm_mon / MONTHSPERYEAR;
	result = (year - 1970) * 365 + cumdays[t->tm_mon % MONTHSPERYEAR];
	result += (year - 1968) / 4;
	result -= (year - 1900) / 100;
	result += (year - 1600) / 400;
	if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0) &&
		(t->tm_mon % MONTHSPERYEAR) < 2)
	result--;
	result += t->tm_mday - 1;
	result *= 24;
	result += t->tm_hour;
	result *= 60;
	result += t->tm_min;
	result *= 60;
	result += t->tm_sec;
	/*@ -matchanyintegral @*/
	return (result);
}

#endif

//wxDateTime performs some braindead timezone adjustments and so is unusable
//mktime and friends also have onerous timezone behaviour
//use strptime and an implementation of timegm instead
void ParseTwitterDate(struct tm *createtm, time_t *createtm_t, const std::string &created_at) {
	struct tm tmp_tm;
	time_t tmp_time;
	if(!createtm) createtm = &tmp_tm;
	if(!createtm_t) createtm_t = &tmp_time;

	memset(createtm, 0, sizeof(struct tm));
	*createtm_t = 0;
	strptime(created_at.c_str(), "%a %b %d %T +0000 %Y", createtm);
	#ifdef __WINDOWS__
	*createtm_t = our_mkgmtime(createtm);
	#else
	char *tz;

	tz = getenv("TZ");
	setenv("TZ", "", 1);
	tzset();
	*createtm_t = mktime(createtm);
	if (tz)
	   setenv("TZ", tz, 1);
	else
	   unsetenv("TZ");
	tzset();
	#endif
}

#define TCO_LINK_LENGTH 22
#define TCO_LINK_LENGTH_HTTPS 23

//adapted from twitter code: https://github.com/twitter/twitter-text-java/blob/master/src/com/twitter/Regex.java
//last updated: commit d1acbbe4ee7d2ec2529e665456a141fccebf5ecb

#define URL_VALID_PRECEEDING_CHARS "(?:[^A-Z0-9@\\x{FF20}$#\\x{FF03}\\x{202A}-\\x{202E}]|^)"
#define LATIN_ACCENTS_CHARS \
	"\\x{00c0}-\\x{00d6}\\x{00d8}-\\x{00f6}\\x{00f8}-\\x{00ff}" \
	"\\x{0100}-\\x{024f}" \
	"\\x{0253}\\x{0254}\\x{0256}\\x{0257}\\x{0259}\\x{025b}\\x{0263}\\x{0268}\\x{026f}\\x{0272}\\x{0289}\\x{028b}" \
	"\\x{02bb}" \
	"\\x{0300}-\\x{036f}" \
	"\\x{1e00}-\\x{1eff}"
#define URL_VALID_CHARS_NC "\\p{Xan}" LATIN_ACCENTS_CHARS
#define URL_VALID_CHARS "[" URL_VALID_CHARS_NC "]"
#define URL_VALID_SUBDOMAIN "(?:(?:" URL_VALID_CHARS "[\\-" URL_VALID_CHARS_NC "_]*)?" URL_VALID_CHARS "\\.)"
#define URL_VALID_DOMAIN_NAME "(?:(?:" URL_VALID_CHARS "[\\-" URL_VALID_CHARS_NC "]*)?" URL_VALID_CHARS "\\.)"
#define URL_VALID_GTLD \
      "(?:(?:academy|actor|aero|agency|arpa|asia|bar|bargains|berlin|best|bid|bike|biz|blue|boutique|build|builders|" \
      "buzz|cab|camera|camp|cards|careers|cat|catering|center|ceo|cheap|christmas|cleaning|clothing|club|codes|" \
      "coffee|com|community|company|computer|construction|contractors|cool|coop|cruises|dance|dating|democrat|" \
      "diamonds|directory|domains|edu|education|email|enterprises|equipment|estate|events|expert|exposed|farm|fish|" \
      "flights|florist|foundation|futbol|gallery|gift|glass|gov|graphics|guitars|guru|holdings|holiday|house|" \
      "immobilien|industries|info|institute|int|international|jobs|kaufen|kim|kitchen|kiwi|koeln|kred|land|lighting|" \
      "limo|link|luxury|management|mango|marketing|menu|mil|mobi|moda|monash|museum|nagoya|name|net|neustar|ninja|" \
      "okinawa|onl|org|partners|parts|photo|photography|photos|pics|pink|plumbing|post|pro|productions|properties|" \
      "pub|qpon|recipes|red|rentals|repair|report|reviews|rich|ruhr|sexy|shiksha|shoes|singles|social|solar|" \
      "solutions|supplies|supply|support|systems|tattoo|technology|tel|tienda|tips|today|tokyo|tools|training|" \
      "travel|uno|vacations|ventures|viajes|villas|vision|vote|voting|voto|voyage|wang|watch|wed|wien|wiki|works|" \
      "xxx|xyz|zone|\\x{0434}\\x{0435}\\x{0442}\\x{0438}|\\x{043E}\\x{043D}\\x{043B}\\x{0430}\\x{0439}\\x{043D}|\\x{043E}\\x{0440}" \
	  "\\x{0433}|\\x{0441}\\x{0430}\\x{0439}\\x{0442}|\\x{0628}\\x{0627}\\x{0632}\\x{0627}\\x{0631}|\\x{0634}\\x{0628}\\x{0643}\\x{0629}" \
	  "|\\x{307F}\\x{3093}\\x{306A}|\\x{4E2D}\\x{4FE1}|\\x{4E2D}\\x{6587}\\x{7F51}|\\x{516C}\\x{53F8}|\\x{516C}\\x{76CA}|\\x{5728}\\x{7EBF}" \
	  "|\\x{6211}\\x{7231}\\x{4F60}|\\x{653F}\\x{52A1}|\\x{6E38}\\x{620F}|\\x{79FB}\\x{52A8}|\\x{7F51}\\x{7EDC}|\\x{96C6}\\x{56E2}|\\x{C0BC}\\x{C131}" \
      ")(?![\\p{Xan}@]))"

#define  URL_VALID_CCTLD \
      "(?:(?:ac|ad|ae|af|ag|ai|al|am|an|ao|aq|ar|as|at|au|aw|ax|az|ba|bb|bd|be|bf|bg|bh|bi|bj|bm|bn|bo|br|bs|bt|" \
      "bv|bw|by|bz|ca|cc|cd|cf|cg|ch|ci|ck|cl|cm|cn|co|cr|cs|cu|cv|cx|cy|cz|dd|de|dj|dk|dm|do|dz|ec|ee|eg|eh|" \
      "er|es|et|eu|fi|fj|fk|fm|fo|fr|ga|gb|gd|ge|gf|gg|gh|gi|gl|gm|gn|gp|gq|gr|gs|gt|gu|gw|gy|hk|hm|hn|hr|ht|" \
      "hu|id|ie|il|im|in|io|iq|ir|is|it|je|jm|jo|jp|ke|kg|kh|ki|km|kn|kp|kr|kw|ky|kz|la|lb|lc|li|lk|lr|ls|lt|" \
      "lu|lv|ly|ma|mc|md|me|mg|mh|mk|ml|mm|mn|mo|mp|mq|mr|ms|mt|mu|mv|mw|mx|my|mz|na|nc|ne|nf|ng|ni|nl|no|np|" \
      "nr|nu|nz|om|pa|pe|pf|pg|ph|pk|pl|pm|pn|pr|ps|pt|pw|py|qa|re|ro|rs|ru|rw|sa|sb|sc|sd|se|sg|sh|si|sj|sk|" \
      "sl|sm|sn|so|sr|ss|st|su|sv|sx|sy|sz|tc|td|tf|tg|th|tj|tk|tl|tm|tn|to|tp|tr|tt|tv|tw|tz|ua|ug|uk|us|uy|uz|" \
      "va|vc|ve|vg|vi|vn|vu|wf|ws|ye|yt|za|zm|zw|" \
	  "\\x{043C}\\x{043E}\\x{043D}|\\x{0440}\\x{0444}|\\x{0441}\\x{0440}\\x{0431}|\\x{0443}\\x{043A}\\x{0440}|\\x{049B}\\x{0430}\\x{0437}|" \
	  "\\x{0627}\\x{0644}\\x{0627}\\x{0631}\\x{062F}\\x{0646}|\\x{0627}\\x{0644}\\x{062C}\\x{0632}\\x{0627}\\x{0626}\\x{0631}|\\x{0627}\\x{0644}" \
	  "\\x{0633}\\x{0639}\\x{0648}\\x{062F}\\x{064A}\\x{0629}|\\x{0627}\\x{0644}\\x{0645}\\x{063A}\\x{0631}\\x{0628}|\\x{0627}\\x{0645}\\x{0627}" \
	  "\\x{0631}\\x{0627}\\x{062A}|\\x{0627}\\x{06CC}\\x{0631}\\x{0627}\\x{0646}|\\x{0628}\\x{06BE}\\x{0627}\\x{0631}\\x{062A}|\\x{062A}\\x{0648}" \
	  "\\x{0646}\\x{0633}|\\x{0633}\\x{0648}\\x{062F}\\x{0627}\\x{0646}|\\x{0633}\\x{0648}\\x{0631}\\x{064A}\\x{0629}|\\x{0639}\\x{0645}\\x{0627}" \
	  "\\x{0646}|\\x{0641}\\x{0644}\\x{0633}\\x{0637}\\x{064A}\\x{0646}|\\x{0642}\\x{0637}\\x{0631}|\\x{0645}\\x{0635}\\x{0631}|\\x{0645}\\x{0644}" \
	  "\\x{064A}\\x{0633}\\x{064A}\\x{0627}|\\x{067E}\\x{0627}\\x{06A9}\\x{0633}\\x{062A}\\x{0627}\\x{0646}|" \
	  "\\x{092D}\\x{093E}\\x{0930}\\x{0924}|\\x{09AC}\\x{09BE}\\x{0982}\\x{09B2}\\x{09BE}|\\x{09AD}\\x{09BE}\\x{09B0}\\x{09A4}|\\x{0A2D}\\x{0A3E}" \
	  "\\x{0A30}\\x{0A24}|\\x{0AAD}\\x{0ABE}\\x{0AB0}\\x{0AA4}|\\x{0B87}\\x{0BA8}\\x{0BCD}\\x{0BA4}\\x{0BBF}\\x{0BAF}\\x{0BBE}|\\x{0B87}\\x{0BB2}" \
	  "\\x{0B99}\\x{0BCD}\\x{0B95}\\x{0BC8}|\\x{0B9A}\\x{0BBF}\\x{0B99}\\x{0BCD}\\x{0B95}\\x{0BAA}\\x{0BCD}\\x{0BAA}\\x{0BC2}\\x{0BB0}\\x{0BCD}" \
	  "|\\x{0C2D}\\x{0C3E}\\x{0C30}\\x{0C24}\\x{0C4D}|\\x{0DBD}\\x{0D82}\\x{0D9A}\\x{0DCF}|\\x{0E44}\\x{0E17}\\x{0E22}|\\x{10D2}\\x{10D4}|\\x{4E2D}" \
	  "\\x{56FD}|\\x{4E2D}\\x{570B}|\\x{53F0}\\x{6E7E}|\\x{53F0}\\x{7063}|\\x{65B0}\\x{52A0}\\x{5761}|\\x{9999}\\x{6E2F}|\\x{D55C}\\x{AD6D}" \
	  ")(?![\\p{Xan}@]))"

#define URL_PUNYCODE "(?:xn--[0-9a-z]+)"
#define URL_VALID_UNICODE_CHARS "(?:\\.|[^\\s\\p{Z}\\p{P}])"
// \\p{Punct}\\p{InGeneralPunctuation}
#define URL_VALID_DOMAIN \
    "(?:" \
        URL_VALID_SUBDOMAIN "+" URL_VALID_DOMAIN_NAME \
        "(?:" URL_VALID_GTLD "|" URL_VALID_CCTLD "|" URL_PUNYCODE ")" \
      ")" \
    "|(?:" \
      URL_VALID_DOMAIN_NAME \
      "(?:" URL_VALID_GTLD "|" URL_PUNYCODE ")" \
    ")" \
    "|(" "(?<=http://)" \
      "(?:" \
        "(?:" URL_VALID_DOMAIN_NAME URL_VALID_CCTLD ")" \
        "|(?:" \
          URL_VALID_UNICODE_CHARS "+\\." \
          "(?:" URL_VALID_GTLD "|" URL_VALID_CCTLD ")" \
        ")" \
      ")" \
    ")" \
    "|(" "(?<=https://)" \
      "(?:" \
        "(?:" URL_VALID_DOMAIN_NAME URL_VALID_CCTLD ")" \
        "|(?:" \
          URL_VALID_UNICODE_CHARS "+\\." \
          "(?:" URL_VALID_GTLD "|" URL_VALID_CCTLD ")" \
        ")" \
      ")" \
    ")" \
    "|(?:" \
      URL_VALID_DOMAIN_NAME URL_VALID_CCTLD "(?=/)" \
    ")"

#define URL_VALID_PORT_NUMBER "[0-9]++"
#define URL_VALID_GENERAL_PATH_CHARS "[a-z0-9!\\*';:=\\+,.\\$/%#\\x{5B}\\x{5D}\\-_~\\|&@" LATIN_ACCENTS_CHARS "]"

#define URL_BALANCED_PARENS "\\(" \
    "(?:" \
      URL_VALID_GENERAL_PATH_CHARS "+" \
      "|" \
      "(?:" \
        URL_VALID_GENERAL_PATH_CHARS "*" \
        "\\(" \
          URL_VALID_GENERAL_PATH_CHARS "+" \
        "\\)" \
        URL_VALID_GENERAL_PATH_CHARS "*" \
      ")" \
    ")" \
  "\\)"

#define URL_VALID_PATH_ENDING_CHARS "[a-z0-9=_#/\\-\\+" LATIN_ACCENTS_CHARS "]|(?:" URL_BALANCED_PARENS ")"
#define URL_VALID_PATH "(?:" \
    "(?:" \
      URL_VALID_GENERAL_PATH_CHARS "*" \
      "(?:" URL_BALANCED_PARENS URL_VALID_GENERAL_PATH_CHARS "*)*" \
      URL_VALID_PATH_ENDING_CHARS \
    ")|(?:@" URL_VALID_GENERAL_PATH_CHARS "+/)" \
  ")"
#define URL_VALID_URL_QUERY_CHARS "[a-z0-9!?\\*'\\(\\);:&=\\+\\$/%#\\x{5B}\\x{5D}\\-_\\.,~\\|@]"
#define URL_VALID_URL_QUERY_ENDING_CHARS "[a-z0-9_&=#/]"
#define VALID_URL_PATTERN_STRING \
  "(?:" \
    "(" URL_VALID_PRECEEDING_CHARS ")" \
    "(" \
      "(https?://)?" \
      "(?:" URL_VALID_DOMAIN ")" \
      "(?::(?:" URL_VALID_PORT_NUMBER "))?" \
      "(?:/" \
        URL_VALID_PATH "*+" \
      ")?" \
      "(?:\\?" URL_VALID_URL_QUERY_CHARS "*" \
              URL_VALID_URL_QUERY_ENDING_CHARS ")?" \
    ")" \
  ")"
#define INVALID_URL_WITHOUT_PROTOCOL_MATCH_BEGIN "[-_./]$"
#define VALID_TCO_URL "^https?://t\\.co/[a-z0-9]+"

#define UNICODE_SPACES "[\\x{0009}-\\x{000d}\\x{0020}\\x{0085}\\x{00a0}\\x{1680}\\x{180E}\\x{2000}-\\x{200a}\\x{2028}\\x{2029}\\x{202F}\\x{205F}\\x{3000}]"
#define AT_SIGNS_CHARS "@\\x{FF20}"
#define AT_SIGNS "[" AT_SIGNS_CHARS "]"
#define VALID_REPLY "^(?:" UNICODE_SPACES ")*" AT_SIGNS "([a-z0-9_]{1,20})"
#define VALID_MENTION_OR_LIST "([^a-z0-9_!#$%&*" AT_SIGNS_CHARS "]|^|RT:?)(" AT_SIGNS "+)([a-z0-9_]{1,20})(/[a-z][a-z0-9_\\-]{0,24})?"
#define INVALID_MENTION_MATCH_END "^(?:[" AT_SIGNS_CHARS LATIN_ACCENTS_CHARS "]|://)"
#define MENTION_NEG_ASSERT "(?![" AT_SIGNS_CHARS LATIN_ACCENTS_CHARS "]|://)"
#define VALID_MENTION_OR_LIST_ASSERT VALID_MENTION_OR_LIST MENTION_NEG_ASSERT
#define IS_USER_MENTIONED_1ST "([^a-z0-9_!#$%&*" AT_SIGNS_CHARS "]|^|RT:?)(" AT_SIGNS "+)("
#define IS_USER_MENTIONED_2ND ")(/[a-z][a-z0-9_\\-]{0,24})?" MENTION_NEG_ASSERT

unsigned int TwitterCharCount(const char *in, size_t inlen, unsigned int img_uploads) {
	static pcre *pattern = nullptr;
	static pcre_extra *patextra = nullptr;
	static pcre *invprotpattern = nullptr;
	static pcre *tcopattern = nullptr;

	unsigned int outsize = 0;

	if(img_uploads) {
		outsize += img_uploads * (TCO_LINK_LENGTH + 1);

		// Rationale: if there is no text, then there does need to be an initial seperating space, so remove it
		// This does not handle the case of trailing whitespace in the input text
		if(!inlen) outsize--;
	}

	if(!inlen) return outsize;

	if(!pattern) {
		const char *errptr;
		int erroffset;
		const char *pat = VALID_URL_PATTERN_STRING;
		pattern = pcre_compile(pat, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!pattern) {
			LogMsgFormat(LOGT::OTHERERR, "TwitterCharCount: pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(pat));
			return 0;
		}
		patextra = pcre_study(pattern, 0, &errptr);
		invprotpattern = pcre_compile(INVALID_URL_WITHOUT_PROTOCOL_MATCH_BEGIN, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		tcopattern = pcre_compile(VALID_TCO_URL, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!invprotpattern || !tcopattern) return 0;
	}

	char *comp = nullptr;
	ssize_t len = utf8proc_map((const uint8_t *) in, inlen, (uint8_t **) &comp, UTF8PROC_STABLE | UTF8PROC_COMPOSE);
	if(len > 0) {
		outsize += strlen_utf8(comp);
	}
	if(outsize) {
		int startoffset = 0;
		do {
			bool https = false;
			int ovector[30];
			int rc = pcre_exec(pattern, patextra, comp, len, startoffset, 0, ovector, 30);
			if(rc <= 0) break;
			startoffset = ovector[1];
			if(rc < 4 || ovector[6] == -1) {
				int inv_ovector[30];
				int rc_inv = pcre_exec(invprotpattern, 0, comp+ovector[2], ovector[3] - ovector[2], 0, 0, inv_ovector, 30);
				if(rc_inv > 0) continue;
			}
			else {
				if(strncasecmp(comp+ovector[6], "https://", ovector[7] - ovector[6]) == 0) https = true;
			}
			int tc_ovector[30];
			int tc_inv = pcre_exec(tcopattern, 0, comp+ovector[4], ovector[5] - ovector[4], 0, 0, tc_ovector, 30);
			const char *start;
			size_t bytes;
			if(tc_inv > 0) {
				start = comp+ovector[4] + tc_ovector[0];
				bytes = tc_ovector[1] - tc_ovector[0];
			}
			else {
				start = comp + ovector[0];
				bytes = ovector[5] - ovector[4];
			}
			size_t urllen = strlen_utf8(start, bytes);
			outsize -= urllen;
			outsize += (https) ? TCO_LINK_LENGTH_HTTPS : TCO_LINK_LENGTH;
		} while(true);
	}
	free(comp);
	return outsize;
}

struct is_user_mentioned_cache_real : public is_user_mentioned_cache {
	std::unordered_map<std::string, pcre *> ptn_cache;

	virtual ~is_user_mentioned_cache_real() {
		clear();
	}

	virtual void clear() override {
		for(auto &it : ptn_cache) {
			pcre_free(it.second);
		}
		ptn_cache.clear();
	}
};

bool IsUserMentioned(const char *in, size_t inlen, udc_ptr_p u, std::unique_ptr<is_user_mentioned_cache> *cache) {
	std::string pat = IS_USER_MENTIONED_1ST + u->GetUser().screen_name + IS_USER_MENTIONED_2ND;

	pcre *pattern = nullptr;
	pcre **pattern_store = nullptr;
	if(cache) {
		if(!*cache) cache->reset(new is_user_mentioned_cache_real);
		is_user_mentioned_cache_real &rcache = *static_cast<is_user_mentioned_cache_real*>(cache->get());

		auto &it = rcache.ptn_cache[pat];
		pattern_store = &it;
		pattern = *pattern_store;
	}

	//no cache, or not in cache
	if(!pattern) {
		const char *errptr;
		int erroffset;
		pattern = pcre_compile(pat.c_str(), PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!pattern) {
			LogMsgFormat(LOGT::OTHERERR, "IsUserMentioned: pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(pat));
			return 0;
		}
	}

	//actually test input
	int ovector[30];
	int rc = pcre_exec(pattern, 0, in, inlen, 0, 0, ovector, 30);

	if(pattern_store) *pattern_store = pattern;
	else pcre_free(pattern);

	return (rc > 0);
}

bool IsTweetAReply(const char *in, size_t inlen) {
	static pcre *pattern = nullptr;
	static pcre_extra *patextra = nullptr;

	if(!pattern) {
		const char *errptr;
		int erroffset;
		const char *pat = VALID_REPLY;
		pattern = pcre_compile(pat, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!pattern) {
			LogMsgFormat(LOGT::OTHERERR, "IsTweetAReply: pcre_compile failed: %s (%d)\n%s", cstr(errptr), erroffset, cstr(pat));
			return false;
		}
		patextra = pcre_study(pattern, 0, &errptr);
	}

	int ovector[30];
	int rc = pcre_exec(pattern, patextra, in, inlen, 0, 0, ovector, 30);
	return (rc > 0);
}

void SpliceTweetIDSet(tweetidset &set, tweetidset &out, uint64_t highlim_inc, uint64_t lowlim_inc, bool clearspliced) {
	tweetidset::iterator start = set.lower_bound(highlim_inc);
	tweetidset::iterator end = set.upper_bound(lowlim_inc);
	out.insert(start, end);
	if(clearspliced) set.erase(start, end);
}

// Note that this uses a (lazily) lower-cased screen name as the map key, so it sorts in roughly the right order
container::map<std::string, dm_conversation_map_item> GetDMConversationMap() {
	container::map<std::string, dm_conversation_map_item> output;

	for(auto &it : ad.user_dm_indexes) {
		uint64_t id = it.first;
		auto &udi = it.second;
		if(!udi.ids.empty()) {
			udc_ptr u = ad.GetUserContainerById(id);
			std::string lowercase_screenname;
			std::transform(u->GetUser().screen_name.begin(), u->GetUser().screen_name.end(), std::back_inserter(lowercase_screenname), [](char in) {
				return (in >= 'A' && in <= 'Z') ? in + 'a' - 'A' : in;
			});
			output[lowercase_screenname] = { u, &udi };
		}
	}

	return std::move(output);
}
