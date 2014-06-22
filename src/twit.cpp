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

#ifdef __WINDOWS__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "strptime.cpp"
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
	return wxString::Format(wxT("%s%s%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), wxstrstd(wxGetApp().datadir).c_str(), wxT("/media_"), media_id.m_id, media_id.t_id);
}
wxString media_entity::cached_thumb_filename(media_id_type media_id) {
	return wxString::Format(wxT("%s%s%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), wxstrstd(wxGetApp().datadir).c_str(), wxT("/mediathumb_"), media_id.m_id, media_id.t_id);
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
	me->lastused = time(0);
	return me;
}


void media_entity::UpdateLastUsed(observer_ptr<dbsendmsg_list> msglist) {
	uint64_t now = time(0);
	if((now - lastused) >= (60 * 60)) {
		//don't bother sending updates for small timestamp changes
		lastused = now;
		DBC_UpdateMedia(*this, DBUMMT::LASTUSED, msglist ? msglist : DBC_GetMessageBatchQueue());
	}
}

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
	idlist.clear();
	if(users_queried.empty()) return;
	auto it = users_queried.cbegin();
	while(true) {
		idlist += std::to_string((*it)->id);
		it++;
		if(it == users_queried.cend()) break;
		idlist += ",";
	}
}

std::string friendlookup::GetTwitterURL() const {
	auto it = ids.begin();
	std::string idlist = "api.twitter.com/1.1/friendships/lookup.json?user_id=";
	while(true) {
		idlist += std::to_string(*it);
		it++;
		if(it == ids.end()) break;
		idlist += ",";
	}
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
		LogMsgFormat(LOGT::SOCKERR, "Clock jump detected: Last activity %ds ago, expected ~%ds. Forcibly re-trying stream connection: %s, conn: %p",
				(int) delta, (int) expected, acc ? cstr(acc->dispname) : "", tw);
		tw->KillConn();
		tw->DoRetry();
		return;
	}

	if(triggercount < 1) {
		//45s in
		triggercount++;
		ArmTimer();
	}
	else {
		//90s in, timed-out
		LogMsgFormat(LOGT::SOCKERR, "Stream connection timed out: %s, conn: %p",
				acc ? cstr(acc->dispname) : "", tw);
		tw->KillConn();
		tw->HandleError(tw->GetCurlHandle(),0,CURLE_OPERATION_TIMEDOUT);
	}
}

bool userdatacontainer::NeedsUpdating(flagwrapper<PENDING_REQ> preq, time_t timevalue) const {
	if(!lastupdate) return true;
	if(!GetUser().screen_name.size()) return true;
	if(!timevalue) timevalue = time(0);
	if(preq & PENDING_REQ::USEREXPIRE) {
		if((uint64_t) timevalue > (lastupdate + gc.userexpiretime)) return true;
		else return false;
	}
	else return false;
}

bool userdatacontainer::ImgIsReady(flagwrapper<PENDING_REQ> preq) {
	if(udc_flags & UDC::IMAGE_DL_IN_PROGRESS) return false;
	if(!(preq & PENDING_REQ::PROFIMG_NEED)) return false;
	if(user.profile_img_url.size()) {
		if(cached_profile_img_url != user.profile_img_url) {
			if(udc_flags & UDC::PROFILE_IMAGE_DL_FAILED) return true;
			if(preq & PENDING_REQ::PROFIMG_DOWNLOAD_FLAG) {
				profileimgdlconn::GetConn(user.profile_img_url, this);

				// New image, bump last used timestamp to prevent it being evicted prior to display
				profile_img_last_used = time(0);
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
						profileimgdlconn::GetConn(u->GetUser().profile_img_url, u);
					}
				}
			});
			return false;
		}
		else {
			return true;
		}
	}
	else return false;
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
		if(ImgIsReady(preq) && !((udc_flags & UDC::IMAGE_DL_IN_PROGRESS) && (preq & PENDING_REQ::USEREXPIRE))) {
			result |= PENDING_RESULT::PROFIMG_READY;
		}
		else {
			result |= PENDING_RESULT::PROFIMG_NOT_READY;
		}
	}
	if(!NeedsUpdating(preq, timevalue) && !((udc_flags & UDC::LOOKUP_IN_PROGRESS) && (preq & PENDING_REQ::USEREXPIRE))) {
		result |= PENDING_RESULT::CONTENT_READY;
	}
	else {
		result |= PENDING_RESULT::CONTENT_NOT_READY;
	}
	return result;
}

void userdatacontainer::CheckPendingTweets(flagwrapper<UMPTF> umpt_flags) {
	FreezeAll();
	std::vector<std::pair<flagwrapper<PENDING_BITS>, tweet_ptr> > stillpending;
	stillpending.reserve(pendingtweets.size());
	for(auto &it : pendingtweets) {
		flagwrapper<PENDING_BITS> res = TryUnmarkPendingTweet(it, umpt_flags);
		if(res) {
			stillpending.push_back(std::make_pair(res, it));
		}
	}
	pendingtweets.clear();

	for(auto &it : stillpending) {
		GenericMarkPending(it.second, it.first, "userdatacontainer::CheckPendingTweets");
	}

	if(udc_flags & UDC::WINDOWOPEN) {
		user_window *uw = user_window::GetWin(id);
		if(uw) uw->Refresh();
	}
	if(udc_flags & UDC::CHECK_USERLISTWIN) {
		udc_flags &= ~UDC::CHECK_USERLISTWIN;
		tpanelparentwin_user::CheckPendingUser(this);
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

void handlenew_pending_op::MarkUnpending(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) {
	HandleNewTweet(t, tac.lock(), arr);
}

std::string handlenew_pending_op::dump() {
	std::shared_ptr<taccount> acc = tac.lock();
	return string_format("Handle arrived on account: %s, 0x%X", acc ? cstr(acc->dispname) : "N/A", arr.get());
}

flagwrapper<PENDING_BITS> TryUnmarkPendingTweet(tweet_ptr_p t, flagwrapper<UMPTF> umpt_flags) {
	LogMsgFormat(LOGT::PENDTRACE, "Try Unmark Pending: %s", cstr(tweet_log_line(t.get())));
	flagwrapper<PENDING_BITS> result;
	std::vector<std::unique_ptr<pending_op> > still_pending;
	for(auto &it : t->pending_ops) {
		tweet_pending tp = t->IsPending(it->preq);
		if(tp.IsReady(it->presult_required)) it->MarkUnpending(t, umpt_flags);
		else {
			still_pending.emplace_back(std::move(it));
			result |= tp.bits;
		}
	}
	t->pending_ops = std::move(still_pending);
	if(t->pending_ops.empty()) {
		t->lflags &= ~TLF::BEINGLOADEDFROMDB;
		t->lflags &= ~TLF::ISPENDING;
	}
	return result;
}

std::shared_ptr<taccount> userdatacontainer::GetAccountOfUser() const {
	for(auto &it : alist) {
		if(it->usercont.get() == this) return it;
	}
	return std::shared_ptr<taccount>();
}

void userdatacontainer::GetImageLocalFilename(wxString &filename) const {
	filename.Printf(wxT("/img_%" wxLongLongFmtSpec "d"), id);
	filename.Prepend(wxstrstd(wxGetApp().datadir));
}

void userdatacontainer::MarkUpdated() {
	lastupdate = time(0);
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
	if(cached_profile_img_url!=user.profile_img_url) {	//don't bother writing it if it's the same as the cached image url
		jw.String("profile_img_url");
		jw.String(user.profile_img_url);
	}
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
	if(!tac) {
		for(auto &it : alist) {	//look for users who we follow, or who follow us
			taccount &acc = *it;
			if(!enabledonly || acc.enabled) {
				auto rel = acc.user_relations.find(id);
				if(rel != acc.user_relations.end()) {
					if(rel->second.ur_flags&(URF::FOLLOWSME_TRUE | URF::IFOLLOW_TRUE)) {
						tac = it;
						break;
					}
				}
			}
		}
	}
	if(!tac) {					//otherwise find the first enabled account
		for(auto &it : alist) {
			if(it->enabled) {
				tac = it;
				break;
			}
		}
	}
	if(!tac) {					//otherwise find the first account
		if(!alist.empty()) tac = alist.front();
	}
	if(!tac) return false;
	if(!enabledonly || tac->enabled) return true;
	else return false;
}

const tweetidset &userdatacontainer::GetMentionSet() {
	if(!msd) msd.reset(new mention_set_data);
	if(msd->added_offset < mention_index.size()) {
		msd->mention_set.insert(mention_index.begin() + msd->added_offset, mention_index.end());
		msd->added_offset = mention_index.size();
	}
	return msd->mention_set;
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
	if(flags.Get('u')) {
		flags.Set('r', true);
		flags.Set('u', false);
	}
}

void cached_id_sets::CheckTweet(tweet &tw) {
	IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
		if(tw.flags.Save() & flagvalue) (this->*mptr).insert(tw.id);
		else (this->*mptr).erase(tw.id);
	});
}

void cached_id_sets::RemoveTweet(uint64_t id) {
	IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
		(this->*mptr).erase(id);
	});
}

void MarkTweetIDSetCIDS(const tweetidset &ids, const tpanel *exclude, tweetidset cached_id_sets::* idsetptr, bool remove, std::function<void(tweet_ptr_p )> existingtweetfunc) {
	tweetidset &globset = ad.cids.*idsetptr;

	if(remove) {
		for(auto &tweet_id : ids) globset.erase(tweet_id);
	}
	else globset.insert(ids.begin(), ids.end());

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
	unsigned long long setmask = mask & tw.flags.Save();
	unsigned long long unsetmask = mask & (~tw.flags.Save());
	std::unique_ptr<dbupdatetweetsetflagsmsg>msg(new dbupdatetweetsetflagsmsg(std::move(ids), setmask, unsetmask));
	DBC_SendMessageBatched(std::move(msg));
}

//the following set of procedures should be kept in sync

//returns true is ready, false is pending
bool taccount::CheckMarkPending(tweet_ptr_p t, flagwrapper<PENDING_REQ> preq, flagwrapper<PENDING_RESULT> presult) {
	tweet_pending tp = t->IsPending(preq);
	if(tp.IsReady(presult)) {
		return true;
	}
	else {
		FastMarkPending(t, tp.bits);
		return false;
	}
}

//returns true is ready, false is pending
bool CheckMarkPending_GetAcc(tweet_ptr_p t, flagwrapper<PENDING_REQ> preq, flagwrapper<PENDING_RESULT> presult) {
	tweet_pending tp = t->IsPending(preq);
	if(tp.IsReady(presult)) {
		return true;
	}
	else {
		GenericMarkPending(t, tp.bits, "CheckMarkPending_GetAcc");
		return false;
	}
}

void GenericMarkPending(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark, const std::string &logprefix, flagwrapper<tweet::GUAF> guaflags) {
	if(mark & PENDING_BITS::ACCMASK) {
		std::shared_ptr<taccount> curacc;
		if(t->GetUsableAccount(curacc, guaflags)) {
			curacc->FastMarkPending(t, mark);
		}
		else {
			FastMarkPendingNoAccFallback(t, mark, logprefix);
		}
	}
	else FastMarkPendingNonAcc(t, mark);
}

//mark *must* be exactly right
void FastMarkPendingNonAcc(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark) {
	t->lflags |= TLF::ISPENDING;
	if(mark & PENDING_BITS::T_U) t->user->MarkTweetPending(t);
	if(mark & PENDING_BITS::T_UR) t->user_recipient->MarkTweetPending(t);
	if(mark & PENDING_BITS::RT_RTU) t->rtsrc->user->MarkTweetPending(t->rtsrc);
	if(mark & PENDING_BITS::RT_MISSING) {
		t->rtsrc->lflags |= TLF::ISPENDING;
		bool insertnewrtpo=true;
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

//mark *must* be exactly right
void taccount::FastMarkPending(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark) {
	FastMarkPendingNonAcc(t, mark);

	if(mark & PENDING_BITS::U) MarkUserPending(t->user);
	if(mark & PENDING_BITS::UR) MarkUserPending(t->user_recipient);
	if(mark & PENDING_BITS::RTU) MarkUserPending(t->rtsrc->user);
}

//return true if successfully marked pending
//mark *must* be exactly right
bool FastMarkPendingNoAccFallback(tweet_ptr_p t, flagwrapper<PENDING_BITS> mark, const std::string &logprefix) {
	FastMarkPendingNonAcc(t, mark);

	if(mark & PENDING_BITS::ACCMASK) {
		if(mark & PENDING_BITS::U) ad.noacc_pending_userconts[t->user->id] = t->user;
		if(mark & PENDING_BITS::UR) ad.noacc_pending_userconts[t->user_recipient->id] = t->user_recipient;
		if(mark & PENDING_BITS::RTU) ad.noacc_pending_userconts[t->rtsrc->user->id] = t->rtsrc->user;

		LogMsgFormat(LOGT::PENDTRACE, "%s: Cannot mark pending as there is no usable account, %s", cstr(logprefix), cstr(tweet_log_line(t.get())));
		return false;
	}
	else return true;
}

//ends

void RemoveUserFromAccPendingLists(uint64_t userid) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		(*it)->pendingusers.erase(userid);
	}
}

bool MarkPending_TPanelMap(tweet_ptr_p tobj, tpanelparentwin_nt* win_, PUSHFLAGS pushflags, std::shared_ptr<tpanel> *pushtpanel_) {
	tpanel *tp = 0;
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
	if(!found) {
		tobj->lflags |= TLF::ISPENDING;
		tobj->AddNewPendingOp(new tpanelload_pending_op(win_, pushflags, pushtpanel_));
	}
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
		tweet_pending tp = tobj->IsPending(preq);
		if(tp.IsReady(presult)) {
			isready = true;
		}
		else {
			if(tobj->GetUsableAccount(acc_hint, GUAF::CHECKEXISTING | GUAF::NOERR) ||
					tobj->GetUsableAccount(acc_hint, GUAF::CHECKEXISTING | GUAF::NOERR | GUAF::USERENABLED)) {
				acc_hint->FastMarkPending(tobj, tp.bits);
			}
			else {
				FastMarkPendingNoAccFallback(tobj, tp.bits, "CheckFetchPendingSingleTweet");
			}
		}
	}
	else {	//tweet not loaded at all
		if(!(tobj->lflags & TLF::BEINGLOADEDFROMDB) && !(tobj->lflags & TLF::BEINGLOADEDOVERNET)) {
			if(ad.unloaded_db_tweet_ids.find(tobj->id) == ad.unloaded_db_tweet_ids.end()) {
				//tweet is not listed as stored in DB, don't bother querying it first
				std::shared_ptr<taccount> curacc = acc_hint;
				CheckLoadSingleTweet(tobj, curacc);
			}
			else {
				std::unique_ptr<dbseltweetmsg_netfallback> own_loadmsg;
				dbseltweetmsg *loadmsg;
				dbseltweetmsg_netfallback *net_loadmsg = nullptr;
				if(existing_dbsel && *existing_dbsel) {
					loadmsg = existing_dbsel->get();
				}
				else if(existing_dbsel) {
					loadmsg = net_loadmsg = new dbseltweetmsg_netfallback;
					existing_dbsel->reset(loadmsg);
				}
				else {
					loadmsg = net_loadmsg = new dbseltweetmsg_netfallback;
					own_loadmsg.reset(net_loadmsg);
				}

				tobj->lflags |= TLF::BEINGLOADEDFROMDB;

				loadmsg->id_set.insert(tobj->id);
				DBC_PrepareStdTweetLoadMsg(*loadmsg);
				loadmsg->flags |= DBSTMF::NO_ERR | DBSTMF::CLEARNOUPDF;
				if(acc_hint) {
					if(!net_loadmsg) net_loadmsg = dynamic_cast<dbseltweetmsg_netfallback*>(loadmsg);
					if(net_loadmsg) net_loadmsg->dbindex = acc_hint->dbindex;
				}
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
		twitcurlext *twit = acc_hint->GetTwitCurlExt();
		twit->connmode = CS_SINGLETWEET;
		twit->extra_id = t->id;
		twit->QueueAsyncExec();
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
		if(user->NeedsUpdating(preq, createtime)) result.bits |= PENDING_BITS::U;
	}
	else presult.Combine(PENDING_RESULT::CONTENT_NOT_READY);

	if(flags.Get('D')) {
		if(!user_recipient) presult.Combine(PENDING_RESULT::CONTENT_NOT_READY);
		else {
			flagwrapper<PENDING_RESULT> user_result = user_recipient->GetPending(preq, createtime);
			presult.Combine(user_result);
			if(user_result & PENDING_RESULT::NOT_READY) result.bits |= PENDING_BITS::T_UR;
			if(user_recipient->NeedsUpdating(preq, createtime)) result.bits |= PENDING_BITS::UR;
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
	bool isready = CheckMarkPending(t);
	if(arr) {
		if(isready) HandleNewTweet(t, shared_from_this(), arr);
		else t->AddNewPendingOp(new handlenew_pending_op(shared_from_this(), arr));
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
	unsigned long long changemask = flags_at_prev_update.Save() ^ flags.Save();
	if(!changemask) return;

	flags_at_prev_update = flags;

	cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*mptr, unsigned long long flagvalue) {
		if(changemask & flagvalue) {
			if(flags.Save() & flagvalue) {
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
struct tm *gmtime_r (const time_t *timer, struct tm *result) {
	struct tm *local_result;
	local_result = gmtime(timer);

	if(!local_result || !result) return 0;

	memcpy (result, local_result, sizeof (struct tm));
	return result;
}
#endif
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
	*createtm_t = _mkgmtime(createtm);
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

unsigned int TwitterCharCount(const char *in, size_t inlen) {
	static pcre *pattern = 0;
	static pcre_extra *patextra = 0;
	static pcre *invprotpattern = 0;
	static pcre *tcopattern = 0;

	if(!inlen) return 0;

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

	char *comp = 0;
	unsigned int outsize = 0;
	ssize_t len = utf8proc_map((const uint8_t *) in, inlen, (uint8_t **) &comp, UTF8PROC_STABLE | UTF8PROC_COMPOSE);
	if(len > 0) {
		outsize = strlen_utf8(comp);
	}
	if(outsize) {
		int startoffset = 0;
		do {
			bool https = false;
			int ovector[30];
			int rc = pcre_exec(pattern, patextra, comp, len, startoffset, 0, ovector, 30);
			if(rc <= 0) break;
			startoffset = ovector[1];
			if(rc < 4 || ovector[6] == -1 ) {
				int inv_ovector[30];
				int rc_inv = pcre_exec(invprotpattern, 0, comp+ovector[2], ovector[3] - ovector[2], 0, 0, inv_ovector, 30);
				if(rc_inv > 0) continue;
			}
			else {
				if(strncasecmp(comp+ovector[6], "https://", ovector[7] - ovector[6]) == 0) https=true;
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

	pcre *pattern = 0;
	pcre **pattern_store = 0;
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

void SpliceTweetIDSet(tweetidset &set, tweetidset &out, uint64_t highlim_inc, uint64_t lowlim_inc, bool clearspliced) {
		tweetidset::iterator start = set.lower_bound(highlim_inc);
		tweetidset::iterator end = set.upper_bound(lowlim_inc);
		out.insert(start, end);
		if(clearspliced) set.erase(start, end);
}
