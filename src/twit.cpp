//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
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
#include "log-impl.h"
#include "mediawin.h"

#ifdef __WINDOWS__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "strptime.cpp"
#pragma GCC diagnostic pop
#endif
#include <openssl/sha.h>
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

//Do not assume that *acc is non-null
void HandleNewTweet(const std::shared_ptr<tweet> &t, const std::shared_ptr<taccount> &acc, flagwrapper<ARRIVAL> arr) {
	if(arr & ARRIVAL::RECV) ad.alltweet_filter.FilterTweet(*t, acc.get());
	if(arr & ARRIVAL::NEW) ad.incoming_filter.FilterTweet(*t, acc.get());
	ad.cids.CheckTweet(*t);

	if(arr & ARRIVAL::NEW) {
		for(auto &it : ad.tpanels) {
			tpanel &tp = *(it.second);
			if(tp.TweetMatches(t, acc)) {
				tp.PushTweet(t);
			}
		}
	}
}

wxString media_entity::cached_full_filename(media_id_type media_id) {
	return wxString::Format(wxT("%s%s%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), wxstrstd(wxGetApp().datadir).c_str(), wxT("/media_"), media_id.m_id, media_id.t_id);
}
wxString media_entity::cached_thumb_filename(media_id_type media_id) {
	return wxString::Format(wxT("%s%s%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), wxstrstd(wxGetApp().datadir).c_str(), wxT("/mediathumb_"), media_id.m_id, media_id.t_id);
}

void media_entity::PurgeCache(dbsendmsg_list *msglist) {
	if(win) win->Close(true);
	flags &= ~(MEF::HAVE_THUMB | MEF::HAVE_FULL | MEF::LOAD_THUMB | MEF::LOAD_FULL);
	flags |= MEF::MANUALLY_PURGED;
	full_img_sha1.reset();
	thumb_img_sha1.reset();
	fulldata.clear();

	::wxRemoveFile(cached_full_filename());
	::wxRemoveFile(cached_thumb_filename());

	dbsendmsg_list *batch = msglist;
	if(!msglist) batch = new dbsendmsg_list();
	DBC_UpdateMedia(*this, DBUMMT::THUMBCHECKSUM, batch);
	DBC_UpdateMedia(*this, DBUMMT::FULLCHECKSUM, batch);
	DBC_UpdateMedia(*this, DBUMMT::FLAGS, batch);
	if(!msglist) DBC_SendMessage(batch);
}

void media_entity::ClearPurgeFlag(dbsendmsg_list *msglist) {
	if(flags & MEF::MANUALLY_PURGED) {
		flags &= ~MEF::MANUALLY_PURGED;
		DBC_UpdateMedia(*this, DBUMMT::FLAGS, msglist);
	}
}

userlookup::~userlookup() {
	UnMarkAll();
}

void userlookup::UnMarkAll() {
	while(!users_queried.empty()) {
		users_queried.front()->udc_flags&=~UDC::LOOKUP_IN_PROGRESS;
		users_queried.pop_front();
	}
}

void userlookup::Mark(std::shared_ptr<userdatacontainer> udc) {
	udc->udc_flags|=UDC::LOOKUP_IN_PROGRESS;
	users_queried.push_front(udc);
}

void userlookup::GetIdList(std::string &idlist) const {
	idlist.clear();
	if(users_queried.empty()) return;
	auto it=users_queried.cbegin();
	while(true) {
		idlist+=std::to_string((*it)->id);
		it++;
		if(it==users_queried.cend()) break;
		idlist+=",";
	}
}

std::string friendlookup::GetTwitterURL() const {
	auto it=ids.begin();
	std::string idlist="api.twitter.com/1.1/friendships/lookup.json?user_id=";
	while(true) {
		idlist+=std::to_string((*it));
		it++;
		if(it==ids.end()) break;
		idlist+=",";
	}
	return idlist;
}



void streamconntimeout::Arm() {
	Start(90000, wxTIMER_ONE_SHOT);
}

void streamconntimeout::Notify() {
	auto acc = tw->tacc.lock();
	LogMsgFormat(LOGT::SOCKERR, wxT("Stream connection timed out: %s (%p)"), acc?acc->dispname.c_str():wxT(""), tw->GetCurlHandle());
	tw->KillConn();
	tw->HandleError(tw->GetCurlHandle(),0,CURLE_OPERATION_TIMEDOUT);
}

bool userdatacontainer::NeedsUpdating(flagwrapper<UPDCF> updcf_flags, time_t timevalue) const {
	if(!lastupdate) return true;
	if(!timevalue) timevalue = time(0);
	if(!(updcf_flags&UPDCF::USEREXPIRE) && GetUser().screen_name.size()) return false;
	else {
		if((uint64_t) timevalue > (lastupdate + gc.userexpiretime)) return true;
		else return false;
	}
}

bool userdatacontainer::ImgIsReady(flagwrapper<UPDCF> updcf_flags) {
	if(udc_flags & UDC::IMAGE_DL_IN_PROGRESS) return false;
	if(user.profile_img_url.size()) {
		if(cached_profile_img_url!=user.profile_img_url) {
			if(udc_flags & UDC::PROFILE_IMAGE_DL_FAILED) return true;
			if(updcf_flags&UPDCF::DOWNLOADIMG) profileimgdlconn::GetConn(user.profile_img_url, shared_from_this());
			return false;
		}
		else if(cached_profile_img_url.size() && !(udc_flags & UDC::PROFILE_BITMAP_SET))  {
			struct job_data {
				wxImage img;
				wxString filename;
				std::string url;
				std::shared_ptr<userdatacontainer> u;
				shb_iptr hash;
				bool success;
			};
			auto data = std::make_shared<job_data>();
			GetImageLocalFilename(data->filename);
			data->url = cached_profile_img_url;
			data->u = this->shared_from_this();
			data->hash = cached_profile_img_sha1;

			LogMsgFormat(LOGT::FILEIOTRACE, wxT("userdatacontainer::ImgIsReady, about to load cached profile image for user id: %" wxLongLongFmtSpec "d (%s), file: %s, url: %s"),
					id, wxstrstd(GetUser().screen_name).c_str(), data->filename.c_str(), wxstrstd(cached_profile_img_url).c_str());

			udc_flags |= UDC::IMAGE_DL_IN_PROGRESS;
			wxGetApp().EnqueueThreadJob([this, data]() {
				wxImage img;
				data->success = LoadImageFromFileAndCheckHash(data->filename, data->hash, img);
				if(data->success) data->img = userdatacontainer::ScaleImageToProfileSize(img);
			},
			[data, updcf_flags]() {
				std::shared_ptr<userdatacontainer> &u = data->u;

				u->udc_flags &= ~UDC::IMAGE_DL_IN_PROGRESS;

				if(data->url != u->cached_profile_img_url) {
					LogMsgFormat(LOGT::OTHERERR, wxT("userdatacontainer::ImgIsReady, cached profile image read from file, which did correspond to url: %s for user id %" wxLongLongFmtSpec "d (@%s), does not match current url of: %s. Maybe user updated profile during read?"),
							wxstrstd(data->url).c_str(), u->id, wxstrstd(u->GetUser().screen_name).c_str(), wxstrstd(u->GetUser().profile_img_url).c_str());
					//Try again:
					u->ImgIsReady(updcf_flags);
					return;
				}

				if(data->success) {
					u->SetProfileBitmap(wxBitmap(data->img));
					u->NotifyProfileImageChange();
				}
				else {
					LogMsgFormat(LOGT::FILEIOERR, wxT("userdatacontainer::ImgIsReady, cached profile image file for user id: %" wxLongLongFmtSpec "d (%s), file: %s, url: %s, missing, invalid or failed hash check"),
						u->id, wxstrstd(u->GetUser().screen_name).c_str(), data->filename.c_str(), wxstrstd(u->cached_profile_img_url).c_str());
					u->cached_profile_img_url.clear();
					if(updcf_flags & UPDCF::DOWNLOADIMG) {    //the saved image is not loadable, clear cache and re-download
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

bool userdatacontainer::ImgHalfIsReady(flagwrapper<UPDCF> updcf_flags) {
	bool res=ImgIsReady(updcf_flags);
	if(res && !(udc_flags & UDC::HALF_PROFILE_BITMAP_SET)) {
		wxImage img = cached_profile_img.ConvertToImage();
		cached_profile_img_half = wxBitmap(ScaleImageToProfileSize(img, 0.5));
		udc_flags|=UDC::HALF_PROFILE_BITMAP_SET;
	}
	return res;
}

bool userdatacontainer::IsReady(flagwrapper<UPDCF> updcf_flags, time_t timevalue) {
	if(!ImgIsReady(updcf_flags)) return false;
	if(NeedsUpdating(updcf_flags, timevalue)) return false;
	else if( !(updcf_flags&UPDCF::USEREXPIRE) ) return true;
	else if( udc_flags & (UDC::LOOKUP_IN_PROGRESS|UDC::IMAGE_DL_IN_PROGRESS)) return false;
	else return true;
}

void userdatacontainer::CheckPendingTweets(flagwrapper<UMPTF> umpt_flags) {
	FreezeAll();
	std::forward_list<std::pair<flagwrapper<PENDING>, std::shared_ptr<tweet> > > stillpending;
	for(auto it=pendingtweets.begin(); it!=pendingtweets.end(); ++it) {
		flagwrapper<PENDING> res = CheckTweetPendings(*it);
		if(!res) {
			UnmarkPendingTweet(*it, umpt_flags);
		}
		else {
			stillpending.push_front(std::make_pair(res, *it));
		}
	}
	pendingtweets.clear();

	for(auto &it : stillpending) {
		GenericMarkPending(it.second, it.first, wxT("userdatacontainer::CheckPendingTweets"));
	}

	if(udc_flags & UDC::WINDOWOPEN) {
		user_window *uw=user_window::GetWin(id);
		if(uw) uw->Refresh();
	}
	if(udc_flags & UDC::CHECK_USERLISTWIN) {
		auto pit=tpanelparentwin_user::pendingmap.equal_range(id);
		for(auto it=pit.first; it!=pit.second; ++it) {
			it->second->PushBackUser(shared_from_this());
		}
	}
	ThawAll();
}

void userdatacontainer::MarkTweetPending(const std::shared_ptr<tweet> &t) {
	if(std::find(pendingtweets.begin(), pendingtweets.end(), t) != pendingtweets.end()) {
		return;
	}
	pendingtweets.push_back(t);
	LogMsgFormat(LOGT::PENDTRACE, wxT("Mark Pending: User: %" wxLongLongFmtSpec "d (@%s) --> %s"), id, wxstrstd(GetUser().screen_name).c_str(), tweet_log_line(t.get()).c_str());
}

void rt_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, flagwrapper<UMPTF> umpt_flags) {
	if(target_retweet->IsReady()) UnmarkPendingTweet(target_retweet, umpt_flags);
}

wxString rt_pending_op::dump() {
	return wxString::Format(wxT("Retweet depends on this: %s"), tweet_log_line(target_retweet.get()).c_str());
}

tpanelload_pending_op::tpanelload_pending_op(tpanelparentwin_nt* win_, flagwrapper<PUSHFLAGS> pushflags_, std::shared_ptr<tpanel> *pushtpanel_)
		: win(win_), pushflags(pushflags_) {
	if(pushtpanel_) pushtpanel = *pushtpanel_;
}

void tpanelload_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, flagwrapper<UMPTF> umpt_flags) {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	if(tp) tp->PushTweet(t);
	tpanelparentwin_nt *window=win.get();
	if(window) {
		if(umpt_flags&UMPTF::TPDB_NOUPDF) window->SetNoUpdateFlag();
		window->PushTweet(t, pushflags);
	}
}

wxString tpanelload_pending_op::dump() {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	tpanelparentwin_nt *window=win.get();
	return wxString::Format(wxT("Push tweet to tpanel: %s, window: %p, pushflags: 0x%X"), (tp)?wxstrstd(tp->dispname).c_str():wxT("N/A"), window, pushflags);
}

void tpanel_subtweet_pending_op::CheckLoadTweetReply(const std::shared_ptr<tweet> &t, wxSizer *v, tpanelparentwin_nt *s,
		tweetdispscr *tds, unsigned int load_count, const std::shared_ptr<tweet> &top_tweet, tweetdispscr *top_tds) {
	using GUAF = tweet::GUAF;

	if(t->in_reply_to_status_id) {
		std::function<void(unsigned int)> loadmorefunc = [=](unsigned int load_count) {
			std::shared_ptr<tweet> subt = ad.GetTweetById(t->in_reply_to_status_id);

			if(top_tweet->IsArrivedHereAnyPerspective()) {	//save
				subt->lflags |= TLF::SHOULDSAVEINDB;
			}

			std::shared_ptr<taccount> pacc;
			t->GetUsableAccount(pacc, GUAF::NOERR) || t->GetUsableAccount(pacc, GUAF::NOERR | GUAF::USERENABLED);
			subt->pending_ops.emplace_front(new tpanel_subtweet_pending_op(v, s, top_tds, load_count, top_tweet));
			subt->lflags |= TLF::ISPENDING;
			if(CheckFetchPendingSingleTweet(subt, pacc)) UnmarkPendingTweet(subt, 0);
		};

		if(load_count == 0) {
			tds->tds_flags |= TDSF::CANLOADMOREREPLIES;
			tds->loadmorereplies = [=]() {
				loadmorefunc(gc.inlinereplyloadmorecount);
			};
			return;
		}
		else loadmorefunc(load_count);
	}
}

tpanel_subtweet_pending_op::tpanel_subtweet_pending_op(wxSizer *v, tpanelparentwin_nt *s, tweetdispscr *top_tds_,
		unsigned int load_count_, std::shared_ptr<tweet> top_tweet_) {
	action_data = std::make_shared<tspo_action_data>();
	action_data->vbox = v;
	action_data->win = s;
	action_data->top_tds = top_tds_;
	action_data->load_count = load_count_;
	action_data->top_tweet = std::move(top_tweet_);
}

void tpanel_subtweet_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, flagwrapper<UMPTF> umpt_flags) {
	std::shared_ptr<tspo_action_data> data = this->action_data;

	tweetdispscr *tp_tds = data->top_tds.get();
	tpanelparentwin_nt *tp_window = data->win.get();
	if(!tp_tds || !tp_window) return;

	if(umpt_flags & UMPTF::TPDB_NOUPDF) tp_window->SetNoUpdateFlag();

	tp_window->GenericAction([data, t](tpanelparentwin_nt *window) {
		tweetdispscr *tds = data->top_tds.get();
		if(!tds) return;

		wxBoxSizer *subhbox = new wxBoxSizer(wxHORIZONTAL);
		data->vbox->Add(subhbox, 0, wxALL | wxEXPAND, 1);

		tweetdispscr *subtd = new tweetdispscr(t, window->scrollwin, window, subhbox);
		subtd->tds_flags |= TDSF::SUBTWEET;

		tds->subtweets.emplace_front(subtd);
		subtd->parent_tweet.set(tds);

		if(t->rtsrc && gc.rtdisp) {
			t->rtsrc->user->ImgHalfIsReady(UPDCF::DOWNLOADIMG);
			subtd->bm = new profimg_staticbitmap(window->scrollwin, t->rtsrc->user->cached_profile_img_half, t->rtsrc->user->id, t->id, window->GetMainframe(), profimg_staticbitmap::PISBF::HALF);
		}
		else {
		t->user->ImgHalfIsReady(UPDCF::DOWNLOADIMG);
		subtd->bm = new profimg_staticbitmap(window->scrollwin, t->user->cached_profile_img_half, t->user->id, t->id, window->GetMainframe(), profimg_staticbitmap::PISBF::HALF);
		}
		subhbox->Add(subtd->bm, 0, wxALL, 1);
		subhbox->Add(subtd, 1, wxLEFT | wxRIGHT | wxEXPAND, 2);

		wxFont newfont;
		wxTextAttrEx tae(subtd->GetDefaultStyleEx());
		if(tae.HasFont()) {
			newfont = tae.GetFont();
		}
		else {
			newfont = subtd->GetFont();
		}
		int newsize = 0;
		if(newfont.IsOk()) newsize = ((newfont.GetPointSize() * 3) + 2) / 4;
		if(!newsize) newsize = 7;

		newfont.SetPointSize(newsize);
		tae.SetFont(newfont);
		subtd->SetFont(newfont);
		subtd->SetDefaultStyle(tae);

		subtd->DisplayTweet();

		if(!(window->tppw_flags&TPPWF::NOUPDATEONPUSH)) {
			subtd->ForceRefresh();
		}
		else subtd->gdb_flags |= tweetdispscr::GDB_F::NEEDSREFRESH;

		CheckLoadTweetReply(t, data->vbox, window, subtd, data->load_count - 1, data->top_tweet, tds);
	});
}

wxString tpanel_subtweet_pending_op::dump() {
	return wxString::Format(wxT("Push inline tweet reply to tpanel: %p, %p, %p"), action_data->vbox, action_data->win.get(), action_data->top_tds.get());
}

void handlenew_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, flagwrapper<UMPTF> umpt_flags) {
	HandleNewTweet(t, tac.lock(), arr);
}

wxString handlenew_pending_op::dump() {
	std::shared_ptr<taccount> acc = tac.lock();
	return wxString::Format(wxT("Handle arrived on account: %s, 0x%X"), acc ? acc->dispname.c_str() : wxT("N/A"), arr.get());
}

void UnmarkPendingTweet(const std::shared_ptr<tweet> &t, flagwrapper<UMPTF> umpt_flags) {
	LogMsgFormat(LOGT::PENDTRACE, wxT("Unmark Pending: %s"), tweet_log_line(t.get()).c_str());
	t->lflags &= ~TLF::BEINGLOADEDFROMDB;
	t->lflags &= ~TLF::ISPENDING;
	for(auto &it : t->pending_ops) {
		it->MarkUnpending(t, umpt_flags);
	}
	t->pending_ops.clear();
	t->updcf_flags &= ~UPDCF::USEREXPIRE;
}

std::shared_ptr<taccount> userdatacontainer::GetAccountOfUser() const {
	for(auto it=alist.begin() ; it != alist.end(); it++ ) if( (*it)->usercont.get()==this ) return *it;
	return std::shared_ptr<taccount>();
}

void userdatacontainer::GetImageLocalFilename(wxString &filename) const {
	filename.Printf(wxT("/img_%" wxLongLongFmtSpec "d"), id);
	filename.Prepend(wxstrstd(wxGetApp().datadir));
}

void userdatacontainer::MarkUpdated() {
	lastupdate=time(0);
	ImgIsReady(UPDCF::DOWNLOADIMG);
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
	int maxdim=(gc.maxpanelprofimgsize*limitscalefactor);
	if(img.GetHeight()>maxdim || img.GetWidth()>maxdim) {
		double scalefactor=(double) maxdim / (double) std::max(img.GetHeight(), img.GetWidth());
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
		for(auto it=alist.begin(); it!=alist.end(); ++it) {	//look for users who we follow, or who follow us
			taccount &acc=**it;
			if(!enabledonly || acc.enabled) {
				auto rel=acc.user_relations.find(id);
				if(rel!=acc.user_relations.end()) {
					if(rel->second.ur_flags&(URF::FOLLOWSME_TRUE | URF::IFOLLOW_TRUE)) {
						tac=*it;
						break;
					}
				}
			}
		}
	}
	if(!tac) {					//otherwise find the first enabled account
		for(auto it=alist.begin(); it!=alist.end(); ++it) {
			if((*it)->enabled) {
				tac=*it;
				break;
			}
		}
	}
	if(!tac) {					//otherwise find the first account
		if(!alist.empty()) tac=alist.front();
	}
	if(!tac) return false;
	if(!enabledonly || tac->enabled) return true;
	else return false;
}

std::string tweet_flags::GetValueString(unsigned long long bitint) {
	std::string out;
	while(bitint) {
		int offset=__builtin_ctzll(bitint);
		bitint&=~((uint64_t) 1<<offset);
		out+=GetFlagChar(offset);
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
				tac=tp.acc;
				retval = true;
			}
		}
	});
	if(retval) return true;

	//try again, but use any associated account
	IterateTP([&](const tweet_perspective &tp) {
		if(tp.acc->enabled || (guaflags & GUAF::USERENABLED && tp.acc->userenabled)) {
			tac=tp.acc;
			retval = true;
		}
	});
	if(retval) return true;

	//use the first account which is actually enabled
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		if((*it)->enabled || (guaflags & GUAF::USERENABLED && (*it)->userenabled)) {
			tac=*it;
			return true;
		}
	}
	if(!(guaflags & GUAF::NOERR)) {
		LogMsgFormat(LOGT::OTHERERR, wxT("Tweet has no usable enabled account, cannot perform network actions on tweet: %s"), tweet_log_line(this).c_str());
	}
	return false;
}

tweet_perspective *tweet::AddTPToTweet(const std::shared_ptr<taccount> &tac, bool *isnew) {
	if(! (lflags & TLF::HAVEFIRSTTP)) {
		first_tp.Reset(tac);
		if(isnew) *isnew=true;
		lflags |= TLF::HAVEFIRSTTP;
		return &first_tp;
	}
	else if(first_tp.acc.get()==tac.get()) {
		if(isnew) *isnew=false;
		return &first_tp;
	}

	for(auto it=tp_extra_list.begin(); it!=tp_extra_list.end(); it++) {
		if(it->acc.get()==tac.get()) {
			if(isnew) *isnew=false;
			return &(*it);
		}
	}
	tp_extra_list.emplace_back(tac);
	if(isnew) *isnew=true;
	return &tp_extra_list.back();
}

tweet_perspective *tweet::GetTweetTP(const std::shared_ptr<taccount> &tac) {
	if(lflags & TLF::HAVEFIRSTTP && first_tp.acc.get() == tac.get()) {
		return &first_tp;
	}
	for(auto it=tp_extra_list.begin(); it!=tp_extra_list.end(); it++) {
		if(it->acc.get()==tac.get()) {
			return &(*it);
		}
	}
	return 0;
}

void tweet::UpdateMarkedAsRead(const tpanel *exclude) {
	if(flags.Get('u')) {
		flags.Set('r', true);
		flags.Set('u', false);
		UpdateTweet(*this, false);
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

void MarkTweetIDSetCIDS(const tweetidset &ids, const tpanel *exclude, std::function<tweetidset &(cached_id_sets &)> idsetselector, bool remove, std::function<void(const std::shared_ptr<tweet> &)> existingtweetfunc) {
	tweetidset &globset = idsetselector(ad.cids);

	if(remove) {
		for(auto &tweet_id : ids) globset.erase(tweet_id);
	}
	else globset.insert(ids.begin(), ids.end());

	for(auto &tpiter : ad.tpanels) {
		tpanel *tp = tpiter.second.get();
		tp->NotifyCIDSChange();
		if(tp == exclude) continue;

		tweetidset &tpset = idsetselector(tp->cids);
		bool updatetp = false;

		for(auto &tweet_id : ids) {
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
		if(updatetp) tp->TPPWFlagMaskAllTWins(TPPWF::CLABELUPDATEPENDING | TPPWF::NOUPDATEONPUSH, 0);
	}
	if(existingtweetfunc) {
		for(auto &tweet_id : ids) {
			auto twshpp = ad.GetExistingTweetById(tweet_id);
			if(twshpp) {
				existingtweetfunc(*twshpp);
			}
		}
	}
}

void UpdateSingleTweetUnreadState(const std::shared_ptr<tweet> &tw) {
	UpdateSingleTweetFlagState(tw, tweet_flags::GetFlagValue('u') | tweet_flags::GetFlagValue('r'));
}
void UpdateSingleTweetHighlightState(const std::shared_ptr<tweet> &tw) {
	UpdateSingleTweetFlagState(tw, tweet_flags::GetFlagValue('H'));
}

void UpdateSingleTweetFlagState(const std::shared_ptr<tweet> &tw, unsigned long long mask) {
	tweetidset ids;
	ids.insert(tw->id);

	cached_id_sets::IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
		if(mask & tweetflag) {
			MarkTweetIDSetCIDS(ids, 0, [&](cached_id_sets &cids) -> tweetidset & { return cids.*ptr; }, !(tw->flags.Save() & tweetflag));
		}
	});

	SendTweetFlagUpdate(tw, mask);
	UpdateTweet(*tw, false);
	CheckClearNoUpdateFlag_All();
}

void SendTweetFlagUpdate(const std::shared_ptr<tweet> &tw, unsigned long long mask) {
	tweetidset ids;
	ids.insert(tw->id);
	unsigned long long setmask = mask & tw->flags.Save();
	unsigned long long unsetmask = mask & (~tw->flags.Save());
	dbupdatetweetsetflagsmsg *msg=new dbupdatetweetsetflagsmsg(std::move(ids), setmask, unsetmask);
	DBC_SendMessageBatched(msg);
}

//the following set of procedures should be kept in sync

//returns true is ready, false is pending
bool taccount::CheckMarkPending(const std::shared_ptr<tweet> &t) {
	flagwrapper<PENDING> res = CheckTweetPendings(t);
	if(!res) return true;
	else {
		FastMarkPending(t, res);
		return false;
	}
}

//mark *must* be exactly right
void FastMarkPendingNonAcc(const std::shared_ptr<tweet> &t, flagwrapper<PENDING> mark) {
	t->lflags |= TLF::ISPENDING;
	if(mark & PENDING::T_U) t->user->MarkTweetPending(t);
	if(mark & PENDING::T_UR) t->user_recipient->MarkTweetPending(t);
	if(mark & PENDING::RT_RTU) t->rtsrc->user->MarkTweetPending(t->rtsrc);
	if(mark & PENDING::RT_MISSING) {
		t->rtsrc->lflags |= TLF::ISPENDING;
		bool insertnewrtpo=true;
		for(auto it=t->rtsrc->pending_ops.begin(); it!=t->rtsrc->pending_ops.end(); ++it) {
			rt_pending_op *rtpo = dynamic_cast<rt_pending_op*>((*it).get());
			if(rtpo && rtpo->target_retweet==t) {
				insertnewrtpo=false;
				break;
			}
		}
		if(insertnewrtpo) t->rtsrc->pending_ops.emplace_front(new rt_pending_op(t));
	}
}

//mark *must* be exactly right
void taccount::FastMarkPending(const std::shared_ptr<tweet> &t, flagwrapper<PENDING> mark) {
	FastMarkPendingNonAcc(t, mark);

	if(mark & PENDING::U) MarkUserPending(t->user);
	if(mark & PENDING::UR) MarkUserPending(t->user_recipient);
	if(mark & PENDING::RTU) MarkUserPending(t->rtsrc->user);
}

//return true if successfully marked pending
//mark *must* be exactly right
bool FastMarkPendingNoAccFallback(const std::shared_ptr<tweet> &t, flagwrapper<PENDING> mark, const wxString &logprefix) {
	FastMarkPendingNonAcc(t, mark);

	if(mark & PENDING::ACCMASK) {
		if(mark & PENDING::U) ad.noacc_pending_userconts[t->user->id] = t->user;
		if(mark & PENDING::UR) ad.noacc_pending_userconts[t->user_recipient->id] = t->user_recipient;
		if(mark & PENDING::RTU) ad.noacc_pending_userconts[t->rtsrc->user->id] = t->rtsrc->user;

		LogMsgFormat(LOGT::PENDTRACE, wxT("%s: Cannot mark pending as there is no usable account, %s"), logprefix.c_str(), tweet_log_line(t.get()).c_str());
		return false;
	}
	else return true;
}

//returns non-zero if pending
flagwrapper<PENDING> CheckTweetPendings(const tweet &t) {
	flagwrapper<PENDING> retval = 0;
	if(t.user && !t.user->IsReady(t.updcf_flags, t.createtime)) {
		if(t.user->NeedsUpdating(t.updcf_flags, t.createtime)) retval |= PENDING::U;
		retval |= PENDING::T_U;
	}
	if(t.flags.Get('D') && t.user_recipient && !(t.user_recipient->IsReady(t.updcf_flags, t.createtime))) {
		if(t.user_recipient->NeedsUpdating(t.updcf_flags, t.createtime)) retval |= PENDING::UR;
		retval |= PENDING::T_UR;
	}
	if(t.rtsrc) {
		if(t.rtsrc->createtime == 0 || !t.rtsrc->user) {
			//Retweet source is not inited at all
			retval |= PENDING::RT_MISSING;
		}
		else if(t.rtsrc->user && !t.rtsrc->user->IsReady(t.rtsrc->updcf_flags, t.rtsrc->createtime)) {
			if(t.rtsrc->user->NeedsUpdating(t.rtsrc->updcf_flags, t.rtsrc->createtime)) retval |= PENDING::RTU;
			retval |= PENDING::RT_RTU | PENDING::RT_MISSING;
		}
	}
	return retval;
}

//returns true is ready, false is pending
bool CheckMarkPending_GetAcc(const std::shared_ptr<tweet> &t) {
	flagwrapper<PENDING> res = CheckTweetPendings(t);
	if(!res) return true;
	else {
		GenericMarkPending(t, res, wxT("CheckMarkPending_GetAcc"));
		return false;
	}
}

void GenericMarkPending(const std::shared_ptr<tweet> &t, flagwrapper<PENDING> mark, const wxString &logprefix, flagwrapper<tweet::GUAF> guaflags) {
	if(mark & PENDING::ACCMASK) {
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

//ends

void RemoveUserFromAccPendingLists(uint64_t userid) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		(*it)->pendingusers.erase(userid);
	}
}

bool MarkPending_TPanelMap(const std::shared_ptr<tweet> &tobj, tpanelparentwin_nt* win_, PUSHFLAGS pushflags, std::shared_ptr<tpanel> *pushtpanel_) {
	tpanel *tp=0;
	if(pushtpanel_) tp=(*pushtpanel_).get();
	bool found=false;
	for(auto it=tobj->pending_ops.begin(); it!=tobj->pending_ops.end(); ++it) {
		tpanelload_pending_op *op=dynamic_cast<tpanelload_pending_op *>((*it).get());
		if(!op) continue;
		if(win_ && op->win.get()!=win_) continue;
		if(tp) {
			std::shared_ptr<tpanel> test_tp=op->pushtpanel.lock();
			if(test_tp.get()!=tp) continue;
		}
		found=true;
		break;
	}
	if(!found) {
		tobj->lflags |= TLF::ISPENDING;
		tobj->pending_ops.emplace_front(new tpanelload_pending_op(win_, pushflags, pushtpanel_));
	}
	return found;
}

//return true if ready now
//If existing_dbsel is given, any DB lookup message is stored in/added to it
//Otherwise any individual DB lookup is executed batched
bool CheckFetchPendingSingleTweet(const std::shared_ptr<tweet> &tobj, std::shared_ptr<taccount> acc_hint, dbseltweetmsg **existing_dbsel) {
	using GUAF = tweet::GUAF;

	if(tobj->text.size()) {
		flagwrapper<PENDING> res = CheckTweetPendings(tobj);
		if(!res) return true;
		else {
			if(tobj->GetUsableAccount(acc_hint, GUAF::CHECKEXISTING | GUAF::NOERR) ||
					tobj->GetUsableAccount(acc_hint, GUAF::CHECKEXISTING | GUAF::NOERR | GUAF::USERENABLED)) {
				acc_hint->FastMarkPending(tobj, res);
				return false;
			}
			else {
				return !FastMarkPendingNoAccFallback(tobj, res, wxT("CheckFetchPendingSingleTweet"));
			}
		}
	}
	else {	//tweet not loaded at all
		if(!(tobj->lflags&TLF::BEINGLOADEDFROMDB) && !(tobj->lflags&TLF::BEINGLOADEDOVERNET)) {
			if(ad.unloaded_db_tweet_ids.find(tobj->id) == ad.unloaded_db_tweet_ids.end()) {
				//tweet is not listed as stored in DB, don't bother querying it first
				std::shared_ptr<taccount> curacc = acc_hint;
				CheckLoadSingleTweet(tobj, curacc);
			}
			else {
				dbseltweetmsg *loadmsg;
				dbseltweetmsg_netfallback *net_loadmsg = 0;
				if(existing_dbsel && *existing_dbsel) loadmsg = *existing_dbsel;
				else if(existing_dbsel) *existing_dbsel = loadmsg = net_loadmsg = new dbseltweetmsg_netfallback;
				else loadmsg = net_loadmsg = new dbseltweetmsg_netfallback;

				tobj->lflags |= TLF::BEINGLOADEDFROMDB;

				loadmsg->id_set.insert(tobj->id);
				DBC_PrepareStdTweetLoadMsg(loadmsg);
				loadmsg->flags |= DBSTMF::NO_ERR | DBSTMF::CLEARNOUPDF;
				if(acc_hint) {
					if(!net_loadmsg) net_loadmsg = dynamic_cast<dbseltweetmsg_netfallback*>(loadmsg);
					if(net_loadmsg) net_loadmsg->dbindex = acc_hint->dbindex;
				}
				if(!DBC_AllMediaEntitiesLoaded()) loadmsg->flags |= DBSTMF::PULLMEDIA;
				if(!existing_dbsel) DBC_SendMessageBatched(loadmsg);
			}
		}
		return false;
	}
}

//returns true on success, otherwise add tweet to pending list
//modifies acc_hint to account actually used
bool CheckLoadSingleTweet(const std::shared_ptr<tweet> &t, std::shared_ptr<taccount> &acc_hint) {
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
		LogMsgFormat(LOGT::OTHERERR, wxT("Cannot lookup tweet: id:%" wxLongLongFmtSpec "d, no usable account, marking pending."), t->id);
		return false;
	}
}

//returns true is ready, false is pending
bool tweet::IsReady(flagwrapper<UPDCF> updcf_flags) {
	bool isready=true;

	if(rtsrc) {
		bool rtsrcisready=rtsrc->IsReady();
		if(!rtsrcisready) isready=false;
	}
	if(!user) isready=false;
	else if(!user->IsReady(updcf_flags, createtime)) isready=false;
	if(flags.Get('D')) {
		if(!user_recipient) isready=false;
		else if(!(user_recipient->IsReady(updcf_flags, createtime))) isready=false;
	}
	return isready;
}

bool taccount::MarkPendingOrHandle(const std::shared_ptr<tweet> &t, flagwrapper<ARRIVAL> arr) {
	bool isready = CheckMarkPending(t);
	if(arr) {
		if(isready) HandleNewTweet(t, shared_from_this(), arr);
		else t->pending_ops.emplace_front(new handlenew_pending_op(shared_from_this(), arr));
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
	return "http" + std::string(flags.Get('s')?"s":"") + "://twitter.com/" + user->GetUser().screen_name + "/status/" + std::to_string(id);
}

void tweet::GetMediaEntities(std::vector<media_entity *> &out, flagwrapper<MEF> mask) const {
	for(const entity &et  : entlist) {
		if((et.type == ENT_MEDIA || et.type == ENT_URL_IMG) && et.media_id) {
			media_entity &me = *(ad.media_list[et.media_id]);
			if(!mask || me.flags & mask) out.push_back(&me);
		}
	}
}

std::string userdatacontainer::GetPermalink(bool ssl) const {
	if(!GetUser().screen_name.size()) return "";
	return "http" + std::string(ssl?"s":"") + "://twitter.com/" + GetUser().screen_name;
}

void userdatacontainer::NotifyProfileImageChange() {
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
	if(!createtm) createtm=&tmp_tm;
	if(!createtm_t) createtm_t=&tmp_time;

	memset(createtm, 0, sizeof(struct tm));
	*createtm_t=0;
	strptime(created_at.c_str(), "%a %b %d %T +0000 %Y", createtm);
	#ifdef __WINDOWS__
	*createtm_t=_mkgmtime(createtm);
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
      "(?:(?:aero|asia|biz|cat|com|coop|edu|gov|info|int|jobs|mil|mobi|museum|name|net|org|pro|tel|travel|xxx)(?!\\p{Xan}))"
#define  URL_VALID_CCTLD \
      "(?:(?:ac|ad|ae|af|ag|ai|al|am|an|ao|aq|ar|as|at|au|aw|ax|az|ba|bb|bd|be|bf|bg|bh|bi|bj|bm|bn|bo|br|bs|bt|" \
      "bv|bw|by|bz|ca|cc|cd|cf|cg|ch|ci|ck|cl|cm|cn|co|cr|cs|cu|cv|cx|cy|cz|dd|de|dj|dk|dm|do|dz|ec|ee|eg|eh|" \
      "er|es|et|eu|fi|fj|fk|fm|fo|fr|ga|gb|gd|ge|gf|gg|gh|gi|gl|gm|gn|gp|gq|gr|gs|gt|gu|gw|gy|hk|hm|hn|hr|ht|" \
      "hu|id|ie|il|im|in|io|iq|ir|is|it|je|jm|jo|jp|ke|kg|kh|ki|km|kn|kp|kr|kw|ky|kz|la|lb|lc|li|lk|lr|ls|lt|" \
      "lu|lv|ly|ma|mc|md|me|mg|mh|mk|ml|mm|mn|mo|mp|mq|mr|ms|mt|mu|mv|mw|mx|my|mz|na|nc|ne|nf|ng|ni|nl|no|np|" \
      "nr|nu|nz|om|pa|pe|pf|pg|ph|pk|pl|pm|pn|pr|ps|pt|pw|py|qa|re|ro|rs|ru|rw|sa|sb|sc|sd|se|sg|sh|si|sj|sk|" \
      "sl|sm|sn|so|sr|ss|st|su|sv|sy|sz|tc|td|tf|tg|th|tj|tk|tl|tm|tn|to|tp|tr|tt|tv|tw|tz|ua|ug|uk|us|uy|uz|" \
      "va|vc|ve|vg|vi|vn|vu|wf|ws|ye|yt|za|zm|zw)(?!\\p{Xan}))"
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
#define URL_VALID_GENERAL_PATH_CHARS "[a-z0-9!\\*';:=\\+,.\\$/%#\\x{5B}\\x{5D}\\-_~\\|&" LATIN_ACCENTS_CHARS "]"
#define URL_BALANCED_PARENS "\\(" URL_VALID_GENERAL_PATH_CHARS "+\\)"
#define URL_VALID_PATH_ENDING_CHARS "[a-z0-9=_#/\\-\\+" LATIN_ACCENTS_CHARS "]|(?:" URL_BALANCED_PARENS ")"
#define URL_VALID_PATH "(?:" \
    "(?:" \
      URL_VALID_GENERAL_PATH_CHARS "*" \
      "(?:" URL_BALANCED_PARENS URL_VALID_GENERAL_PATH_CHARS "*)*" \
      URL_VALID_PATH_ENDING_CHARS \
    ")|(?:@" URL_VALID_GENERAL_PATH_CHARS "+/)" \
  ")"
#define URL_VALID_URL_QUERY_CHARS "[a-z0-9!?\\*'\\(\\);:&=\\+\\$/%#\\x{5B}\\x{5D}\\-_\\.,~\\|]"
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
	static pcre *pattern=0;
	static pcre_extra *patextra=0;
	static pcre *invprotpattern=0;
	static pcre *tcopattern=0;

	if(!inlen) return 0;

	if(!pattern) {
		const char *errptr;
		int erroffset;
		const char *pat=VALID_URL_PATTERN_STRING;
		pattern=pcre_compile(pat, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!pattern) {
			LogMsgFormat(LOGT::OTHERERR, wxT("TwitterCharCount: pcre_compile failed: %s (%d)\n%s"), wxstrstd(errptr).c_str(), erroffset, wxstrstd(pat).c_str());
			return 0;
		}
		patextra=pcre_study(pattern, 0, &errptr);
		invprotpattern=pcre_compile(INVALID_URL_WITHOUT_PROTOCOL_MATCH_BEGIN, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		tcopattern=pcre_compile(VALID_TCO_URL, PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
		if(!invprotpattern || !tcopattern) return 0;
	}

	char *comp=0;
	unsigned int outsize=0;
	ssize_t len=utf8proc_map((const uint8_t *) in, inlen, (uint8_t **) &comp, UTF8PROC_STABLE | UTF8PROC_COMPOSE);
	if(len>0) {
		outsize=strlen_utf8(comp);
	}
	if(outsize) {
		int startoffset=0;
		int rc;
		do {
			bool https=false;
			int ovector[30];
			rc=pcre_exec(pattern, patextra, comp, len, startoffset, 0, ovector, 30);
			if(rc<=0) break;
			startoffset=ovector[1];
			if(rc<4 || ovector[6]==-1 ) {
				int inv_ovector[30];
				int rc_inv=pcre_exec(invprotpattern, 0, comp+ovector[2], ovector[3]-ovector[2], 0, 0, inv_ovector, 30);
				if(rc_inv>0) continue;
			}
			else {
				if(strncasecmp(comp+ovector[6], "https://", ovector[7]-ovector[6])==0) https=true;
			}
			int tc_ovector[30];
			int tc_inv=pcre_exec(tcopattern, 0, comp+ovector[4], ovector[5]-ovector[4], 0, 0, tc_ovector, 30);
			const char *start;
			size_t bytes;
			if(tc_inv>0) {
				start=comp+ovector[4]+tc_ovector[0];
				bytes=tc_ovector[1]-tc_ovector[0];
			}
			else {
				start=comp+ovector[0];
				bytes=ovector[5]-ovector[4];
			}
			size_t urllen=strlen_utf8(start, bytes);
			outsize-=urllen;
			outsize+=(https)?TCO_LINK_LENGTH_HTTPS:TCO_LINK_LENGTH;
		} while(true);
	}
	free(comp);
	return outsize;
}

bool IsUserMentioned(const char *in, size_t inlen, const std::shared_ptr<userdatacontainer> &u) {
	const char *errptr;
	int erroffset;
	std::string pat=IS_USER_MENTIONED_1ST + u->GetUser().screen_name + IS_USER_MENTIONED_2ND;
	pcre *pattern=pcre_compile(pat.c_str(), PCRE_UCP | PCRE_NO_UTF8_CHECK | PCRE_CASELESS | PCRE_UTF8, &errptr, &erroffset, 0);
	if(!pattern) {
		LogMsgFormat(LOGT::OTHERERR, wxT("IsUserMentioned: pcre_compile failed: %s (%d)\n%s"), wxstrstd(errptr).c_str(), erroffset, wxstrstd(pat).c_str());
		return 0;
	}
	int ovector[30];
	int rc=pcre_exec(pattern, 0, in, inlen, 0, 0, ovector, 30);
	return (rc>0);
}

void SpliceTweetIDSet(tweetidset &set, tweetidset &out, uint64_t highlim_inc, uint64_t lowlim_inc, bool clearspliced) {
		tweetidset::iterator start = set.lower_bound(highlim_inc);
		tweetidset::iterator end = set.upper_bound(lowlim_inc);
		out.insert(start, end);
		if(clearspliced) set.erase(start, end);
}
