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
#include "retcon.h"
#include "raii.h"
#include "hash.h"
#include <wx/file.h>
#include <wx/mstream.h>
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

void profileimgdlconn::Init(const std::string &imgurl_, udc_ptr_p user_) {
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
		user->MakeProfileImageFailurePlaceholder();
	}
	cp.Standby(this);
}

void profileimgdlconn::Reset() {
	dlconn::Reset();
	user.reset();
}

profileimgdlconn *profileimgdlconn::GetConn(const std::string &imgurl_, udc_ptr_p user_) {
	profileimgdlconn *res=cp.GetConn();
	res->Init(imgurl_, user_);
	return res;
}

void profileimgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res) {
	LogMsgFormat(LOGT::NETACT, wxT("Profile image downloaded: %s for user id %" wxLongLongFmtSpec "d (@%s), conn: %p"), wxstrstd(url).c_str(), user->id, wxstrstd(user->GetUser().screen_name).c_str(), this);

	struct local {
		static void clear_dl_flags(udc_ptr_p user) {
			user->udc_flags &= ~UDC::IMAGE_DL_IN_PROGRESS;
			user->udc_flags &= ~UDC::HALF_PROFILE_BITMAP_SET;
		};
		static void bad_url_handler(const std::string &url, udc_ptr_p user) {
			TSLogMsgFormat(LOGT::OTHERERR, wxT("Profile image downloaded: %s for user id %" wxLongLongFmtSpec "d (@%s), does not match expected url of: %s. Maybe user updated profile during download?"),
					wxstrstd(url).c_str(), user->id, wxstrstd(user->GetUser().screen_name).c_str(), wxstrstd(user->GetUser().profile_img_url).c_str());

			//Try again:
			user->ImgIsReady(UPDCF::DOWNLOADIMG);
		}
	};

	//tidy up *this when function returns
	raii tidy_up_this([&]() {
		cp.Standby(this);
	});

	//URL changed, abort
	if(url != user->GetUser().profile_img_url) {
		local::clear_dl_flags(user);
		local::bad_url_handler(url, user);
		return;
	}

	struct profimg_job_data_struct {
		std::string data;
		wxString filename;
		wxImage img;
		bool ok = true;
		udc_ptr user;
		std::string url;
		shb_iptr hash;
	};
	auto job_data = std::make_shared<profimg_job_data_struct>();
	job_data->data = std::move(data);
	user->GetImageLocalFilename(job_data->filename);
	job_data->user = user;
	job_data->url = std::move(url);

	wxGetApp().EnqueueThreadJob([job_data]() {
		udc_ptr user = job_data->user;
		if(!gc.readonlymode) {
			wxFile file(job_data->filename, wxFile::write);
			file.Write(job_data->data.data(), job_data->data.size());
		}
		wxMemoryInputStream memstream(job_data->data.data(), job_data->data.size());

		wxImage img(memstream);
		if(!img.IsOk()) {
			TSLogMsgFormat(LOGT::OTHERERR, wxT("Profile image downloaded: %s for user id %" wxLongLongFmtSpec "d (@%s), is not OK, possible partial download?"),
					wxstrstd(job_data->url).c_str(), user->id, wxstrstd(user->GetUser().screen_name).c_str());
			job_data->ok = false;
		}
		else {
			job_data->img = userdatacontainer::ScaleImageToProfileSize(img);
			std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
			SHA1((const unsigned char *) job_data->data.data(), (unsigned long) job_data->data.size(), hash->hash_sha1);
			job_data->hash = std::move(hash);
		}
	},
	[job_data]() {
		udc_ptr &user = job_data->user;
		local::clear_dl_flags(user);
		if(!job_data->ok) {
			user->MakeProfileImageFailurePlaceholder();
		}
		else if(job_data->url != user->GetUser().profile_img_url) {
			//Doesn't seem likely, but check again that URL hasn't changed
			local::bad_url_handler(job_data->url, user);
		}
		else {
			//Must do the bitmap stuff in the main thread, wxBitmaps are not thread safe at all
			user->SetProfileBitmap(wxBitmap(job_data->img));

			user->cached_profile_img_url = job_data->url;
			user->cached_profile_img_sha1 = std::move(job_data->hash);
			user->lastupdate_wrotetodb = 0;    //force user to be written out to database

			DBC_InsertUser(user);
			user->NotifyProfileImageChange();
		}
	});
}

wxString profileimgdlconn::GetConnTypeName() {
	return wxT("Profile image download");
}

void mediaimgdlconn::Init(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_) {
	media_id = media_id_;
	flags = flags_;
	auto it = ad.media_list.find(media_id);
	if(it != ad.media_list.end()) {
		media_entity &me = *(it->second);
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
		media_entity &me = *(it->second);
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
			for(auto &jt : me.tweet_list) {
				UpdateTweet(*jt);
			}
		}
	}
	delete this;
}

void mediaimgdlconn::Reset() {
	dlconn::Reset();
}

void mediaimgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res) {
	LogMsgFormat(LOGT::NETACT, wxT("Media image downloaded: %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d, flags: %X, conn: %p"),
			wxstrstd(url).c_str(), media_id.m_id, media_id.t_id, flags, this);

	struct midc_job_data {
		wxImage thumb;
		std::string fulldata;
		media_id_type media_id;
		shb_iptr full_hash;
		shb_iptr thumb_hash;
		MIDC flags;
		std::string url;
		bool thumbok = false;
	};
	auto job_data = std::make_shared<midc_job_data>();
	job_data->fulldata = std::move(data);
	job_data->media_id = media_id;
	job_data->flags = flags;
	job_data->url = std::move(url);

	wxGetApp().EnqueueThreadJob([job_data]() {
		MIDC &flags = job_data->flags;

		if(flags & MIDC::OPPORTUNIST_THUMB && !(flags & MIDC::THUMBIMG)) {
			flags |= MIDC::THUMBIMG;
			if(flags & MIDC::OPPORTUNIST_REDRAW_TWEETS) flags |= MIDC::REDRAW_TWEETS;
		}

		if(flags & MIDC::THUMBIMG) {
			wxMemoryInputStream memstream(job_data->fulldata.data(), job_data->fulldata.size());
			wxImage img(memstream);
			job_data->thumbok = img.IsOk();
			if(job_data->thumbok) {
				const int maxdim = 64;
				if(img.GetHeight() > maxdim || img.GetWidth() > maxdim) {
					double scalefactor=(double) maxdim / (double) std::max(img.GetHeight(), img.GetWidth());
					int newwidth = (double) img.GetWidth() * scalefactor;
					int newheight = (double) img.GetHeight() * scalefactor;
					job_data->thumb = img.Scale(std::lround(newwidth), std::lround(newheight), wxIMAGE_QUALITY_HIGH);
				}
				else job_data->thumb = img;
				if(gc.cachethumbs && !gc.readonlymode) {
					wxMemoryOutputStream memstr;
					job_data->thumb.SaveFile(memstr, wxBITMAP_TYPE_PNG);
					const unsigned char *data = (const unsigned char *) memstr.GetOutputStreamBuffer()->GetBufferStart();
					size_t size = memstr.GetSize();
					wxFile file(media_entity::cached_thumb_filename(job_data->media_id), wxFile::write);
					file.Write(data, size);

					std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
					SHA1(data, size, hash->hash_sha1);
					job_data->thumb_hash = std::move(hash);
				}
			}
		}

		if(flags & MIDC::FULLIMG) {
			if(gc.cachemedia && !gc.readonlymode) {
				wxFile file(media_entity::cached_full_filename(job_data->media_id), wxFile::write);
				file.Write(job_data->fulldata.data(), job_data->fulldata.size());
				std::shared_ptr<sha1_hash_block> hash = std::make_shared<sha1_hash_block>();
				SHA1((const unsigned char *) job_data->fulldata.data(), (unsigned long) job_data->fulldata.size(), hash->hash_sha1);
				job_data->full_hash = std::move(hash);
			}
		}
	},
	[job_data]() {
		MIDC &flags = job_data->flags;
		auto it = ad.media_list.find(job_data->media_id);
		if(it != ad.media_list.end()) {
			media_entity &me = *(it->second);

			me.ClearPurgeFlag();

			if(flags & MIDC::THUMBIMG) {
				if(job_data->thumbok) {
					me.thumbimg = job_data->thumb;
					me.flags |= MEF::HAVE_THUMB;
					if(job_data->thumb_hash) {
						me.thumb_img_sha1 = job_data->thumb_hash;
						DBC_UpdateMedia(me, DBUMMT::THUMBCHECKSUM);
					}
				}
				else {
					LogMsgFormat(LOGT::OTHERERR, wxT("Media image downloaded: %s, id: %" wxLongLongFmtSpec "d/%" wxLongLongFmtSpec "d, flags: %X, is not OK, possible partial download?"),
							wxstrstd(job_data->url).c_str(), job_data->media_id.m_id, job_data->media_id.t_id, flags);
					me.flags |= MEF::THUMB_FAILED;
				}
				me.flags &= ~MEF::THUMB_NET_INPROGRESS;
			}

			if(flags & MIDC::FULLIMG) {
				me.flags |= MEF::HAVE_FULL;
				me.flags &= ~MEF::FULL_NET_INPROGRESS;
				me.fulldata = std::move(job_data->fulldata);
				if(job_data->full_hash) {
					me.full_img_sha1 = job_data->full_hash;
					DBC_UpdateMedia(me, DBUMMT::FULLCHECKSUM);
				}
				if(me.win) me.win->UpdateImage();
			}

			if(flags & MIDC::REDRAW_TWEETS) {
				for(auto &jt : me.tweet_list) {
					UpdateTweet(*jt);
				}
			}
		}
	});

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
