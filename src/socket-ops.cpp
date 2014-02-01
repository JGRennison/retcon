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
////  You should have received a copy of the GNU General Public License
//  along with this program. If not, see <http://www.gnu.org/licenses/>.
//
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "socket.h"
#include "socket-ops.h"
#include "mediawin.h"
#include "alldata.h"
#include "mainui.h"
#include "userui.h"
#include "util.h"
#include "db.h"
#include "twit.h"
#include "twitcurlext.h"
#include "log.h"
#include "cfg.h"
#include <wx/file.h>
#include <wx/mstream.h>
#include <wx/dcmemory.h>
#include <openssl/sha.h>
#include <algorithm>

bool socketmanager::AddConn(twitcurlext &cs) {
	return AddConn(cs.GetCurlHandle(), &cs);
}

dlconn::dlconn() : curlHandle(0) {
}

dlconn::~dlconn() {
	if(curlHandle) curl_easy_cleanup(curlHandle);
	curlHandle=0;
}

void dlconn::Reset() {
	url.clear();
	data.clear();
}

void dlconn::Init(const std::string &url_) {
	url=url_;
	if(!curlHandle) curlHandle = curl_easy_init();
	else curl_easy_reset(curlHandle);
	#ifdef __WINDOWS__
	curl_easy_setopt(curlHandle, CURLOPT_CAINFO, "./cacert.pem");
	#endif
	curl_easy_setopt(curlHandle, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(curlHandle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, curlCallback );
	curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, this );
	curl_easy_setopt(curlHandle, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curlHandle, CURLOPT_MAXREDIRS, 5);
	sm.AddConn(curlHandle, this);
}

int dlconn::curlCallback(char* data, size_t size, size_t nmemb, dlconn *obj) {
	int writtenSize = 0;
	if( obj && data ) {
		writtenSize = size*nmemb;
		obj->data.append(data, writtenSize);
	}
	return writtenSize;
}

void profileimgdlconn::Init(const std::string &imgurl_, const std::shared_ptr<userdatacontainer> &user_) {
	user = user_;
	user->udc_flags |= UDC::IMAGE_DL_IN_PROGRESS;
	LogMsgFormat(LOGT::NETACT, wxT("Downloading profile image %s for user id %" wxLongLongFmtSpec "d (@%s), conn: %p"), wxstrstd(imgurl_).c_str(), user_->id, wxstrstd(user_->GetUser().screen_name).c_str(), this);
	dlconn::Init(imgurl_);
}

void profileimgdlconn::DoRetry() {
	if(url==user->GetUser().profile_img_url) Init(url, user);
	else cp.Standby(this);
}

void profileimgdlconn::HandleFailure(long httpcode, CURLcode res) {
	if(url == user->GetUser().profile_img_url) {
		if(!(user->udc_flags & UDC::PROFILE_BITMAP_SET)) {	//generate a placeholder image
			user->cached_profile_img.Create(48, 48, -1);
			wxMemoryDC dc(user->cached_profile_img);
			dc.SetBackground(wxBrush(wxColour(0, 0, 0, wxALPHA_TRANSPARENT)));
			dc.Clear();
			user->udc_flags|=UDC::PROFILE_BITMAP_SET;
		}
		user->udc_flags &= ~UDC::IMAGE_DL_IN_PROGRESS;
		user->udc_flags &= ~UDC::HALF_PROFILE_BITMAP_SET;
		user->udc_flags |= UDC::PROFILE_IMAGE_DL_FAILED;
		user->CheckPendingTweets();
	}
	cp.Standby(this);
}

void profileimgdlconn::Reset() {
	dlconn::Reset();
	user.reset();
}

profileimgdlconn *profileimgdlconn::GetConn(const std::string &imgurl_, const std::shared_ptr<userdatacontainer> &user_) {
	profileimgdlconn *res=cp.GetConn();
	res->Init(imgurl_, user_);
	return res;
}

void profileimgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res) {
	LogMsgFormat(LOGT::NETACT, wxT("Profile image downloaded: %s for user id %" wxLongLongFmtSpec "d (@%s), conn: %p"), wxstrstd(url).c_str(), user->id, wxstrstd(user->GetUser().screen_name).c_str(), this);

	user->udc_flags &= ~UDC::IMAGE_DL_IN_PROGRESS;
	user->udc_flags &= ~UDC::HALF_PROFILE_BITMAP_SET;

	if(url == user->GetUser().profile_img_url) {
		wxString filename;
		user->GetImageLocalFilename(filename);
		wxFile file(filename, wxFile::write);
		file.Write(data.data(), data.size());
		wxMemoryInputStream memstream(data.data(), data.size());

		wxImage img(memstream);
		if(!img.IsOk()) {
			LogMsgFormat(LOGT::OTHERERR, wxT("Profile image downloaded: %s for user id %" wxLongLongFmtSpec "d (@%s), is not OK, possible partial download?"), wxstrstd(url).c_str(), user->id, wxstrstd(user->GetUser().screen_name).c_str());
		}
		else {
			user->SetProfileBitmapFromwxImage(img);

			user->cached_profile_img_url=url;
			std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
			SHA1((const unsigned char *) data.data(), (unsigned long) data.size(), hash->hash_sha1);
			user->cached_profile_img_sha1 = std::move(hash);
			user->lastupdate_wrotetodb=0;		//force user to be written out to database
			dbc.InsertUser(user);
			data.clear();
			user->CheckPendingTweets();
			UpdateUsersTweet(user->id, true);
			if(user->udc_flags & UDC::WINDOWOPEN) user_window::CheckRefresh(user->id, true);
		}
	}
	else {
		LogMsgFormat(LOGT::OTHERERR, wxT("Profile image downloaded: %s for user id %" wxLongLongFmtSpec "d (@%s), does not match expected url of: %s. Maybe user updated profile during download?"), wxstrstd(url).c_str(), user->id, wxstrstd(user->GetUser().screen_name).c_str(), wxstrstd(user->GetUser().profile_img_url).c_str());
	}

	cp.Standby(this);
}

wxString profileimgdlconn::GetConnTypeName() {
	return wxT("Profile image download");
}

void mediaimgdlconn::Init(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_) {
	media_id = media_id_;
	flags = flags_;
	auto it = ad.media_list.find(media_id);
	if(it != ad.media_list.end()) {
		media_entity &me=it->second;
		if(flags & MIDC::FULLIMG) {
			me.flags |= MEF::FULL_NET_INPROGRESS;
		}
		if(flags & MIDC::THUMBIMG) {
			me.flags |= MEF::THUMB_NET_INPROGRESS;
		}
	}
	LogMsgFormat(LOGT::NETACT, wxT("Downloading media image %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d, flags: %X, conn: %p"), wxstrstd(imgurl_).c_str(), media_id_.m_id, media_id_.t_id, flags_, this);
	dlconn::Init(imgurl_);
}

void mediaimgdlconn::DoRetry() {
	Init(url, media_id, flags);
}

void mediaimgdlconn::HandleFailure(long httpcode, CURLcode res) {
	auto it = ad.media_list.find(media_id);
	if(it != ad.media_list.end()) {
		media_entity &me = it->second;
		if(flags & MIDC::FULLIMG) {
			me.flags |= MEF::FULL_FAILED;
			me.flags &= ~MEF::FULL_NET_INPROGRESS;
			if(me.win) me.win->UpdateImage();
		}
		if(flags & MIDC::THUMBIMG) {
			me.flags |= MEF::THUMB_FAILED;
			me.flags &= ~MEF::THUMB_NET_INPROGRESS;
		}
		if(flags & MIDC::REDRAW_TWEETS) {
			for(auto &it : me.tweet_list) {
				UpdateTweet(*it);
			}
		}
	}
	delete this;
}

void mediaimgdlconn::Reset() {
	dlconn::Reset();
}

void mediaimgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res) {

	LogMsgFormat(LOGT::NETACT, wxT("Media image downloaded: %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d, flags: %X, conn: %p"), wxstrstd(url).c_str(), media_id.m_id, media_id.t_id, flags, this);

	auto it=ad.media_list.find(media_id);
	if(it!=ad.media_list.end()) {
		media_entity &me=it->second;

		if(flags&MIDC::OPPORTUNIST_THUMB && !(flags&MIDC::THUMBIMG)) {
			flags|=MIDC::THUMBIMG;
			if(flags&MIDC::OPPORTUNIST_REDRAW_TWEETS) flags|=MIDC::REDRAW_TWEETS;
		}

		if(flags&MIDC::THUMBIMG) {
			wxMemoryInputStream memstream(data.data(), data.size());
			wxImage img(memstream);
			if(img.IsOk()) {
				const int maxdim=64;
				if(img.GetHeight()>maxdim || img.GetWidth()>maxdim) {
					double scalefactor=(double) maxdim / (double) std::max(img.GetHeight(), img.GetWidth());
					int newwidth = (double) img.GetWidth() * scalefactor;
					int newheight = (double) img.GetHeight() * scalefactor;
					me.thumbimg=img.Scale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH);
				}
				else me.thumbimg=img;
				me.flags |= MEF::HAVE_THUMB;
				if(gc.cachethumbs) {
					wxMemoryOutputStream memstr;
					me.thumbimg.SaveFile(memstr, wxBITMAP_TYPE_PNG);
					const unsigned char *data=(const unsigned char *) memstr.GetOutputStreamBuffer()->GetBufferStart();
					size_t size=memstr.GetSize();
					wxFile file(me.cached_thumb_filename(), wxFile::write);
					file.Write(data, size);

					std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
					SHA1(data, size, hash->hash_sha1);
					me.thumb_img_sha1 = std::move(hash);

					dbc.UpdateMediaChecksum(me, false);
				}
			}
			else {
				LogMsgFormat(LOGT::OTHERERR, wxT("Media image downloaded: %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d, flags: %X, conn: %p, is not OK, possible partial download?"), wxstrstd(url).c_str(), media_id.m_id, media_id.t_id, flags, this);
				me.flags |= MEF::THUMB_FAILED;
			}
			me.flags &= ~MEF::THUMB_NET_INPROGRESS;
		}

		if(flags&MIDC::FULLIMG) {
			me.fulldata=std::move(data);
			me.flags |= MEF::HAVE_FULL;
			me.flags &= ~MEF::FULL_NET_INPROGRESS;
			if(me.win) me.win->UpdateImage();
			if(gc.cachemedia) {
				wxFile file(me.cached_full_filename(), wxFile::write);
				file.Write(me.fulldata.data(), me.fulldata.size());

				std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
				SHA1((const unsigned char *) me.fulldata.data(), (unsigned long) me.fulldata.size(), hash->hash_sha1);
				me.full_img_sha1 = std::move(hash);

				dbc.UpdateMediaChecksum(me, true);
			}
		}

		if(flags & MIDC::REDRAW_TWEETS) {
			for(auto &it : me.tweet_list) {
				UpdateTweet(*it);
			}
		}
	}

	data.clear();

	delete this;
}

wxString mediaimgdlconn::GetConnTypeName() {
	return wxT("Media image download");
}

template <typename C> connpool<C>::~connpool() {
	ClearAllConns();
}
template connpool<twitcurlext>::~connpool();

template <typename C> void connpool<C>::ClearAllConns() {
	while(!idlestack.empty()) {
		delete idlestack.top();
		idlestack.pop();
	}
	for(auto it=activeset.begin(); it != activeset.end(); it++) {
		(*it)->KillConn();
		delete *it;
	}
	activeset.clear();
}
template void connpool<profileimgdlconn>::ClearAllConns();
template void connpool<twitcurlext>::ClearAllConns();

template <typename C> C *connpool<C>::GetConn() {
	C *res;
	if(idlestack.empty()) {
		res=new C();
	}
	else {
		res=idlestack.top();
		idlestack.pop();
	}
	activeset.insert(res);
	return res;
}
template twitcurlext *connpool<twitcurlext>::GetConn();

template <typename C> void connpool<C>::Standby(C *obj) {
	obj->Reset();
	obj->StandbyTidy();
	idlestack.push(obj);
	activeset.erase(obj);
}
template void connpool<twitcurlext>::Standby(twitcurlext *obj);

connpool<profileimgdlconn> profileimgdlconn::cp;
