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
#include "dispscr.h"
#include "util.h"
#include "userui.h"
#include "alldata.h"
#include "twitcurlext.h"
#include "mainui.h"

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
#include <algorithm>

void HandleNewTweet(const std::shared_ptr<tweet> &t) {
	//do some filtering, etc

	for(auto it=ad.tpanels.begin(); it!=ad.tpanels.end(); ++it) {
		tpanel &tp=*(it->second);
		for(auto &tpa : tp.tpautos) {
			if((tpa.autoflags & TPAF_DM && t->flags.Get('D')) || (tpa.autoflags & TPAF_TW && t->flags.Get('T')) || (tpa.autoflags & TPAF_MN && t->flags.Get('M'))) {
				if(tpa.autoflags & TPAF_ALLACCS) tp.PushTweet(t);
				else {
					bool stop = false;
					t->IterateTP([&](const tweet_perspective &twp) {
						if(stop) return;
						if(twp.acc.get()==tpa.acc.get() && twp.IsArrivedHere()) {
							tp.PushTweet(t);
							stop = true;
						}
					});
				}
			}
		}
	}
}

void EnumAllDisplayedTweets(std::function<bool (tweetdispscr *)> func, bool setnoupdateonpush) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		(*it)->EnumDisplayedTweets(func, setnoupdateonpush);
	}
}

void UpdateAllTweets(bool redrawimg, bool resethighlight) {
	EnumAllDisplayedTweets([&](tweetdispscr *tds) {
		if(resethighlight) tds->tds_flags &= ~TDSF_HIGHLIGHT;
		tds->DisplayTweet(redrawimg);
		return true;
	}, true);
}

void UpdateUsersTweet(uint64_t userid, bool redrawimg) {
	EnumAllDisplayedTweets([&](tweetdispscr *tds) {
		bool found=false;
		if((tds->td->user && tds->td->user->id==userid)
		|| (tds->td->user_recipient && tds->td->user_recipient->id==userid)) found=true;
		if(tds->td->rtsrc) {
			if((tds->td->rtsrc->user && tds->td->rtsrc->user->id==userid)
			|| (tds->td->rtsrc->user_recipient && tds->td->rtsrc->user_recipient->id==userid)) found=true;
		}
		if(found) {
			LogMsgFormat(LFT_TPANEL, wxT("UpdateUsersTweet: Found Entry %" wxLongLongFmtSpec "d."), tds->td->id);
			tds->DisplayTweet(redrawimg);
		}
		return true;
	}, true);
}

void UpdateTweet(const tweet &t, bool redrawimg) {
	for(auto it=tpanelparentwinlist.begin(); it!=tpanelparentwinlist.end(); ++it) {
		(*it)->UpdateOwnTweet(t, redrawimg);
	}
}

wxString media_entity::cached_full_filename() const {
	return wxString::Format(wxT("%s%s%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), wxstrstd(wxGetApp().datadir).c_str(), wxT("/media_"), media_id.m_id, media_id.t_id);
}
wxString media_entity::cached_thumb_filename() const {
	return wxString::Format(wxT("%s%s%" wxLongLongFmtSpec "d_%" wxLongLongFmtSpec "d"), wxstrstd(wxGetApp().datadir).c_str(), wxT("/mediathumb_"), media_id.m_id, media_id.t_id);
}

userlookup::~userlookup() {
	UnMarkAll();
}

void userlookup::UnMarkAll() {
	while(!users_queried.empty()) {
		users_queried.front()->udc_flags&=~UDC_LOOKUP_IN_PROGRESS;
		users_queried.pop_front();
	}
}

void userlookup::Mark(std::shared_ptr<userdatacontainer> udc) {
	udc->udc_flags|=UDC_LOOKUP_IN_PROGRESS;
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
	LogMsgFormat(LFT_SOCKERR, wxT("Stream connection timed out: %s (%p)"), acc?acc->dispname.c_str():wxT(""), tw->GetCurlHandle());
	tw->KillConn();
	tw->HandleError(tw->GetCurlHandle(),0,CURLE_OPERATION_TIMEDOUT);
}

bool userdatacontainer::NeedsUpdating(unsigned int updcf_flags, time_t timevalue) const {
	if(!lastupdate) return true;
	if(!timevalue) timevalue = time(0);
	if(!(updcf_flags&UPDCF_USEREXPIRE) && GetUser().screen_name.size()) return false;
	else {
		if((uint64_t) timevalue > (lastupdate + gc.userexpiretime)) return true;
		else return false;
	}
}

bool userdatacontainer::ImgIsReady(unsigned int updcf_flags) {
	if(udc_flags & UDC_IMAGE_DL_IN_PROGRESS) return false;
	if(user.profile_img_url.size()) {
		if(cached_profile_img_url!=user.profile_img_url) {
			if(udc_flags&UDC_PROFILE_IMAGE_DL_FAILED) return true;
			if(updcf_flags&UPDCF_DOWNLOADIMG) profileimgdlconn::GetConn(user.profile_img_url, shared_from_this());
			return false;
		}
		else if(cached_profile_img_url.size() && !(udc_flags&UDC_PROFILE_BITMAP_SET))  {
			wxImage img;
			wxString filename;
			GetImageLocalFilename(filename);
			bool success=LoadImageFromFileAndCheckHash(filename, cached_profile_img_sha1, img);
			if(success) {
				SetProfileBitmapFromwxImage(img);
				return true;
			}
			else {
				LogMsgFormat(LFT_OTHERERR, wxT("userdatacontainer::ImgIsReady, cached profile image file for user id: %" wxLongLongFmtSpec "d (%s), file: %s, url: %s, missing, invalid or failed hash check"),
					id, wxstrstd(GetUser().screen_name).c_str(), filename.c_str(), wxstrstd(cached_profile_img_url).c_str());
				cached_profile_img_url.clear();
				if(updcf_flags&UPDCF_DOWNLOADIMG) {					//the saved image is not loadable, clear cache and re-download
					profileimgdlconn::GetConn(user.profile_img_url, shared_from_this());
				}
				return false;
			}
		}
		else {
			return true;
		}
	}
	else return false;
}

bool userdatacontainer::ImgHalfIsReady(unsigned int updcf_flags) {
	bool res=ImgIsReady(updcf_flags);
	if(res && !(udc_flags&UDC_HALF_PROFILE_BITMAP_SET)) {
		wxImage img=cached_profile_img.ConvertToImage();
		cached_profile_img_half=MkProfileBitmapFromwxImage(img, 0.5);
		udc_flags|=UDC_HALF_PROFILE_BITMAP_SET;
	}
	return res;
}

bool userdatacontainer::IsReady(unsigned int updcf_flags, time_t timevalue) {
	if(!ImgIsReady(updcf_flags)) return false;
	if(NeedsUpdating(updcf_flags, timevalue)) return false;
	else if( !(updcf_flags&UPDCF_USEREXPIRE) ) return true;
	else if( udc_flags & (UDC_LOOKUP_IN_PROGRESS|UDC_IMAGE_DL_IN_PROGRESS)) return false;
	else return true;
}

void userdatacontainer::CheckPendingTweets(unsigned int umpt_flags) {
	FreezeAll();
	std::forward_list<std::pair<int, std::shared_ptr<tweet> > > stillpending;
	for(auto it=pendingtweets.begin(); it!=pendingtweets.end(); ++it) {
		int res=CheckTweetPendings(*it);
		if(res==0) {
			UnmarkPendingTweet(*it, umpt_flags);
		}
		else {
			stillpending.push_front(std::make_pair(res, *it));
		}
	}
	pendingtweets.clear();

	for(auto it=stillpending.begin(); it!=stillpending.end(); ++it) {
		std::shared_ptr<taccount> curacc;
		if(it->second->GetUsableAccount(curacc, true)) {
			curacc->FastMarkPending(it->second, it->first, true);
		}
	}

	if(udc_flags&UDC_WINDOWOPEN) {
		user_window *uw=user_window::GetWin(id);
		if(uw) uw->Refresh();
	}
	if(udc_flags&UDC_CHECK_USERLISTWIN) {
		auto pit=tpanelparentwin_user::pendingmap.equal_range(id);
		for(auto it=pit.first; it!=pit.second; ++it) {
			it->second->PushBackUser(shared_from_this());
		}
	}
	ThawAll();
}

void userdatacontainer::MarkTweetPending(const std::shared_ptr<tweet> &t, bool checkfirst) {
	if(checkfirst) {
		if(std::find_if(pendingtweets.begin(), pendingtweets.end(), [&](const std::shared_ptr<tweet> &tw) {
			return (t->id==tw->id);
		})!=pendingtweets.end()) {
			return;
		}
	}
	pendingtweets.push_front(t);
	LogMsgFormat(LFT_PENDTRACE, wxT("Mark Pending: User: %" wxLongLongFmtSpec "d (@%s) --> Tweet: %" wxLongLongFmtSpec "d (%.15s...)"), id, wxstrstd(GetUser().screen_name).c_str(), t->id, wxstrstd(t->text).c_str());
}

void rt_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags) {
	if(target_retweet->IsReady()) UnmarkPendingTweet(target_retweet, umpt_flags);
}

wxString rt_pending_op::dump() {
	return wxString::Format(wxT("Retweet depends on this: %" wxLongLongFmtSpec "d (%.20s...)"), target_retweet->id, wxstrstd(target_retweet->text).c_str());
}

tpanelload_pending_op::tpanelload_pending_op(tpanelparentwin_nt* win_, unsigned int pushflags_, std::shared_ptr<tpanel> *pushtpanel_)
		: win(win_), pushflags(pushflags_) {
	if(pushtpanel_) pushtpanel=*pushtpanel_;
}

void tpanelload_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags) {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	if(tp) tp->PushTweet(t);
	tpanelparentwin_nt *window=win.get();
	if(window) {
		if(umpt_flags&UMPTF_TPDB_NOUPDF) window->tppw_flags|=TPPWF_NOUPDATEONPUSH;
		window->PushTweet(t, pushflags);
	}
}

wxString tpanelload_pending_op::dump() {
	std::shared_ptr<tpanel> tp=pushtpanel.lock();
	tpanelparentwin_nt *window=win.get();
	return wxString::Format(wxT("Push tweet to tpanel: %s, window: %p, pushflags: 0x%X"), (tp)?wxstrstd(tp->dispname).c_str():wxT("N/A"), window, pushflags);
}

tpanel_subtweet_pending_op::tpanel_subtweet_pending_op(wxSizer *v, tpanelparentwin_nt *s, tweetdispscr *t)
		: vbox(v), win(s), parent_td(t) { }

void tpanel_subtweet_pending_op::MarkUnpending(const std::shared_ptr<tweet> &t, unsigned int umpt_flags) {
	tweetdispscr *tds=parent_td.get();
	tpanelparentwin_nt *window=win.get();
	if(!tds || !window) return;

	tppw_scrollfreeze sf;
	window->StartScrollFreeze(sf);
	window->scrollwin->Freeze();

	if(umpt_flags&UMPTF_TPDB_NOUPDF) window->tppw_flags|=TPPWF_NOUPDATEONPUSH;

	wxBoxSizer *subhbox = new wxBoxSizer(wxHORIZONTAL);
	vbox->Add(subhbox, 0, wxALL | wxEXPAND, 1);

	tweetdispscr *subtd=new tweetdispscr(t, window->scrollwin, window, subhbox);
	subtd->tds_flags |= TDSF_SUBTWEET;

	tds->subtweets.emplace_front(subtd);

	if(t->rtsrc && gc.rtdisp) {
		t->rtsrc->user->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
		subtd->bm = new profimg_staticbitmap(window->scrollwin, t->rtsrc->user->cached_profile_img_half, t->rtsrc->user->id, t->id, window->GetMainframe(), true);
	}
	else {
		t->user->ImgHalfIsReady(UPDCF_DOWNLOADIMG);
		subtd->bm = new profimg_staticbitmap(window->scrollwin, t->user->cached_profile_img_half, t->user->id, t->id, window->GetMainframe(), true);
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

	if(!(window->tppw_flags&TPPWF_NOUPDATEONPUSH)) {
		subtd->ForceRefresh();
	}
	window->EndScrollFreeze(sf);
	window->scrollwin->Thaw();
}

wxString tpanel_subtweet_pending_op::dump() {
	return wxString::Format(wxT("Push inline tweet reply to tpanel: %p, %p, %p"), vbox, win.get(), parent_td.get());
}

void UnmarkPendingTweet(const std::shared_ptr<tweet> &t, unsigned int umpt_flags) {
	LogMsgFormat(LFT_PENDTRACE, wxT("Unmark Pending: Tweet: %" wxLongLongFmtSpec "d (%.15s...), lflags: %X, updcf_flags: %X"), t->id, wxstrstd(t->text).c_str(), t->lflags, t->updcf_flags);
	t->lflags&=~TLF_BEINGLOADEDFROMDB;
	if(t->lflags&TLF_PENDINGHANDLENEW) {
		t->lflags&=~TLF_PENDINGHANDLENEW;
		HandleNewTweet(t);
	}
	for(auto it=t->pending_ops.begin(); it!=t->pending_ops.end(); ++it) {
		(*it)->MarkUnpending(t, umpt_flags);
	}
	t->pending_ops.clear();
	t->updcf_flags&=~UPDCF_USEREXPIRE;
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
	ImgIsReady(UPDCF_DOWNLOADIMG);
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
	jw.Bool(user.u_flags&UF_ISPROTECTED);
	jw.String("verified");
	jw.Bool(user.u_flags&UF_ISVERIFIED);
	jw.String("followers_count");
	jw.Uint(user.followers_count);
	jw.String("statuses_count");
	jw.Uint(user.statuses_count);
	jw.String("friends_count");
	jw.Uint(user.friends_count);
	jw.EndObject();
	return json;
}

wxBitmap userdatacontainer::MkProfileBitmapFromwxImage(const wxImage &img, double limitscalefactor) {
	int maxdim=(gc.maxpanelprofimgsize*limitscalefactor);
	if(img.GetHeight()>maxdim || img.GetWidth()>maxdim) {
		double scalefactor=(double) maxdim / (double) std::max(img.GetHeight(), img.GetWidth());
		int newwidth = (double) img.GetWidth() * scalefactor;
		int newheight = (double) img.GetHeight() * scalefactor;
		return wxBitmap(img.Scale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH));
	}
	else return wxBitmap(img);
}

void userdatacontainer::SetProfileBitmapFromwxImage(const wxImage &img) {
	cached_profile_img=MkProfileBitmapFromwxImage(img, 1.0);
	udc_flags|=UDC_PROFILE_BITMAP_SET;
}

bool userdatacontainer::GetUsableAccount(std::shared_ptr<taccount> &tac, bool enabledonly) const {
	if(!tac) {
		for(auto it=alist.begin(); it!=alist.end(); ++it) {	//look for users who we follow, or who follow us
			taccount &acc=**it;
			if(!enabledonly || acc.enabled) {
				auto rel=acc.user_relations.find(id);
				if(rel!=acc.user_relations.end()) {
					if(rel->second.ur_flags&(URF_FOLLOWSME_TRUE|URF_IFOLLOW_TRUE)) {
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

bool tweet::GetUsableAccount(std::shared_ptr<taccount> &tac, unsigned int guaflags) const {
	if(guaflags&GUAF_CHECKEXISTING) {
		if(tac && tac->enabled) return true;
	}
	bool retval = false;
	IterateTP([&](const tweet_perspective &tp) {
		if(tp.IsArrivedHere()) {
			if(tp.acc->enabled || (guaflags&GUAF_USERENABLED && tp.acc->userenabled)) {
				tac=tp.acc;
				retval = true;
			}
		}
	});
	if(retval) return true;

	//try again, but use any associated account
	IterateTP([&](const tweet_perspective &tp) {
		if(tp.acc->enabled || (guaflags&GUAF_USERENABLED && tp.acc->userenabled)) {
			tac=tp.acc;
			retval = true;
		}
	});
	if(retval) return true;

	//use the first account which is actually enabled
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		if((*it)->enabled || (guaflags&GUAF_USERENABLED && (*it)->userenabled)) {
			tac=*it;
			return true;
		}
	}
	if(!(guaflags&GUAF_NOERR)) {
		LogMsgFormat(LFT_OTHERERR, wxT("Tweet: %" wxLongLongFmtSpec "d (%.15s...), has no usable enabled account, cannot perform network actions on tweet"), id, wxstrstd(text).c_str());
	}
	return false;
}

tweet_perspective *tweet::AddTPToTweet(const std::shared_ptr<taccount> &tac, bool *isnew) {
	if(! (lflags & TLF_HAVEFIRSTTP)) {
		first_tp.Reset(tac);
		if(isnew) *isnew=true;
		lflags |= TLF_HAVEFIRSTTP;
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
	if(lflags & TLF_HAVEFIRSTTP && first_tp.acc.get() == tac.get()) {
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
	if(tw.flags.Get('u')) unreadids.insert(tw.id);
	else unreadids.erase(tw.id);
	if(tw.flags.Get('H')) highlightids.insert(tw.id);
	else highlightids.erase(tw.id);
	if(tw.flags.Get('h')) hiddenids.insert(tw.id);
	else hiddenids.erase(tw.id);
}

void MarkTweetIDSetCIDS(const tweetidset &ids, const tpanel *exclude, std::function<tweetidset &(cached_id_sets &)> idsetselector, bool remove, std::function<void(const std::shared_ptr<tweet> &)> existingtweetfunc) {
	tweetidset &globset = idsetselector(ad.cids);

	for(auto &tweet_id : ids) {
		for(auto &tpiter : ad.tpanels) {
			tpanel *tp = tpiter.second.get();
			if(tp == exclude) continue;

			auto updatetp = [&]() {
				tp->TPPWFlagMaskAllTWins(TPPWF_CLABELUPDATEPENDING|TPPWF_NOUPDATEONPUSH, 0);
			};

			tweetidset &tpset = idsetselector(tp->cids);

			if(remove) {
				auto item_it = tpset.find(tweet_id);
				if(item_it != tpset.end()) {
					tpset.erase(item_it);
					updatetp();
				}
			}
			else if(tp->tweetlist.find(tweet_id) != tp->tweetlist.end()) {
				auto res = tpset.insert(tweet_id);
				if(res.second) updatetp();
			}
		}
		if(existingtweetfunc) {
			auto twshpp = ad.GetExistingTweetById(tweet_id);
			if(twshpp) {
				existingtweetfunc(*twshpp);
			}
		}
		if(remove) globset.erase(tweet_id);
		else globset.insert(tweet_id);
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

	if(mask & tweet_flags::GetFlagValue('H')) {
		MarkTweetIDSetCIDS(ids, 0, [&](cached_id_sets &cids) -> tweetidset & { return cids.highlightids; }, ! tw->flags.Get('H'));
	}
	if(mask & (tweet_flags::GetFlagValue('u') | tweet_flags::GetFlagValue('r'))) {
		MarkTweetIDSetCIDS(ids, 0, [&](cached_id_sets &cids) -> tweetidset & { return cids.unreadids; }, ! tw->flags.Get('u'));
	}
	if(mask & tweet_flags::GetFlagValue('h')) {
		MarkTweetIDSetCIDS(ids, 0, [&](cached_id_sets &cids) -> tweetidset & { return cids.hiddenids; }, ! tw->flags.Get('h'));
	}

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
	dbc.SendMessage(msg);
}

//the following set of procedures should be kept in sync

//returns true is ready, false is pending
bool taccount::CheckMarkPending(const std::shared_ptr<tweet> &t, bool checkfirst) {
	unsigned int res=CheckTweetPendings(t);
	if(!res) return true;
	else {
		FastMarkPending(t, res, checkfirst);
		return false;
	}
}

//mark *must* be exactly right
void taccount::FastMarkPending(const std::shared_ptr<tweet> &t, unsigned int mark, bool checkfirst) {
	if(mark&1) t->user->MarkTweetPending(t, checkfirst);
	if(mark&2) t->user_recipient->MarkTweetPending(t, checkfirst);
	if(mark&4) {
		bool insertnewrtpo=true;
		for(auto it=t->rtsrc->pending_ops.begin(); it!=t->rtsrc->pending_ops.end(); ++it) {
			rt_pending_op *rtpo = dynamic_cast<rt_pending_op*>((*it).get());
			if(rtpo && rtpo->target_retweet==t) {
				insertnewrtpo=false;
				break;
			}
		}
		if(insertnewrtpo) t->rtsrc->pending_ops.emplace_front(new rt_pending_op(t));
		t->rtsrc->user->MarkTweetPending(t->rtsrc, checkfirst);
	}

	if(mark&8) MarkUserPending(t->user);
	if(mark&16) MarkUserPending(t->user_recipient);
	if(mark&32) MarkUserPending(t->rtsrc->user);
}

//returns non-zero if pending
unsigned int CheckTweetPendings(const std::shared_ptr<tweet> &t) {
	unsigned int retval=0;
	if(t->user && !t->user->IsReady(t->updcf_flags, t->createtime)) {
		if(t->user->NeedsUpdating(t->updcf_flags, t->createtime)) retval|=8;
		retval|=1;
	}
	if(t->flags.Get('D') && t->user_recipient && !(t->user_recipient->IsReady(t->updcf_flags, t->createtime))) {
		if(t->user_recipient->NeedsUpdating(t->updcf_flags, t->createtime)) retval|=16;
		retval|=2;
	}
	if(t->rtsrc && t->rtsrc->user && !t->rtsrc->user->IsReady(t->rtsrc->updcf_flags, t->rtsrc->createtime)) {
		if(t->rtsrc->user->NeedsUpdating(t->rtsrc->updcf_flags, t->rtsrc->createtime)) retval|=32;
		retval|=4;
	}
	return retval;
}

void RemoveUserFromAccPendingLists(uint64_t userid) {
	for(auto it=alist.begin(); it!=alist.end(); ++it) {
		(*it)->pendingusers.erase(userid);
	}
}

//returns true is ready, false is pending
bool CheckMarkPending_GetAcc(const std::shared_ptr<tweet> &t, bool checkfirst) {
	unsigned int res=CheckTweetPendings(t);
	if(!res) return true;
	else {
		std::shared_ptr<taccount> curacc;
		if(t->GetUsableAccount(curacc, true)) {
			curacc->FastMarkPending(t, res, checkfirst);
			return false;
		}
		else return true;
	}
}

bool MarkPending_TPanelMap(const std::shared_ptr<tweet> &tobj, tpanelparentwin_nt* win_, unsigned int pushflags, std::shared_ptr<tpanel> *pushtpanel_) {
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
	if(!found) tobj->pending_ops.emplace_front(new tpanelload_pending_op(win_, pushflags, pushtpanel_));
	return found;
}

//return true if ready now
bool CheckFetchPendingSingleTweet(const std::shared_ptr<tweet> &tobj, std::shared_ptr<taccount> acc_hint) {
	if(tobj->text.size()) {
		unsigned int res=CheckTweetPendings(tobj);
		if(!res) return true;
		else {
			if(tobj->GetUsableAccount(acc_hint, GUAF_CHECKEXISTING|GUAF_NOERR) ||
					tobj->GetUsableAccount(acc_hint, GUAF_CHECKEXISTING|GUAF_NOERR|GUAF_USERENABLED)) {
				acc_hint->FastMarkPending(tobj, res, true);
				return false;
			}
			else return true;
		}
	}
	else {	//tweet not loaded at all
		if(!(tobj->lflags&TLF_BEINGLOADEDFROMDB) && !(tobj->lflags&TLF_BEINGLOADEDOVERNET)) {
			dbseltweetmsg_netfallback *loadmsg=new dbseltweetmsg_netfallback;
			loadmsg->id_set.insert(tobj->id);
			loadmsg->targ=&dbc;
			loadmsg->cmdevtype=wxextDBCONN_NOTIFY;
			loadmsg->winid=wxDBCONNEVT_ID_TPANELTWEETLOAD;
			loadmsg->flags|=DBSTMF_NO_ERR;
			if(acc_hint) loadmsg->dbindex=acc_hint->dbindex;
			if(!gc.persistentmediacache) loadmsg->flags|=DBSTMF_PULLMEDIA;
			dbc.SendMessage(loadmsg);
		}
		return false;
	}
}

//returns true is ready, false is pending
bool tweet::IsReady() {
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

void taccount::MarkPendingOrHandle(const std::shared_ptr<tweet> &t) {
	bool isready=CheckMarkPending(t);
	if(isready) HandleNewTweet(t);
	else t->lflags|=TLF_PENDINGHANDLENEW;
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

std::string userdatacontainer::GetPermalink(bool ssl) const {
	if(!GetUser().screen_name.size()) return "";
	return "http" + std::string(ssl?"s":"") + "://twitter.com/" + GetUser().screen_name;
}

bool tweet::IsFavouritable() const {
	return flags.Get('T');
}

bool tweet::IsRetweetable() const {
	return (flags.Get('T') && (rtsrc || !(user->GetUser().u_flags&UF_ISPROTECTED)));
}

bool tweet::IsArrivedHereAnyPerspective() const {
	bool res = false;
	IterateTP([&](const tweet_perspective &tp) {
		if(tp.IsArrivedHere()) res = true;
	});
	return res;
}

#ifdef __WINDOWS__
struct tm *gmtime_r (const time_t *timer, struct tm *result) {
	struct tm *local_result;
	local_result = gmtime(timer);

	if(!local_result || !result) return 0;

	memcpy (result, local_result, sizeof (struct tm));
	return result;
}
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
			LogMsgFormat(LFT_OTHERERR, wxT("TwitterCharCount: pcre_compile failed: %s (%d)\n%s"), wxstrstd(errptr).c_str(), erroffset, wxstrstd(pat).c_str());
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
			outsize+=(https)?21:20;
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
		LogMsgFormat(LFT_OTHERERR, wxT("IsUserMentioned: pcre_compile failed: %s (%d)\n%s"), wxstrstd(errptr).c_str(), erroffset, wxstrstd(pat).c_str());
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
