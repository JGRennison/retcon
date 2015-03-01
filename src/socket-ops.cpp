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
#include "taccount.h"
#include <wx/file.h>
#include <wx/mstream.h>
#include <algorithm>

bool socketmanager::AddConn(std::unique_ptr<twitcurlext> cs) {
	CURL *ch = cs->GetCurlHandle(); // Do this before moving cs
	return AddConn(ch, std::move(cs));
}

dlconn::~dlconn() {
	if(curlHandle) curl_easy_cleanup(curlHandle);
	curlHandle = nullptr;
	if(extra_headers) curl_slist_free_all(extra_headers);
	extra_headers = nullptr;
}

// If this_owner is given, this will add it/itself to the socketmanager
void dlconn::Init(std::unique_ptr<mcurlconn> &&this_owner, const std::string &url_, std::unique_ptr<oAuth> auth_obj_) {
	url = url_;
	auth_obj = std::move(auth_obj_);

	if(!curlHandle) curlHandle = curl_easy_init();
	else curl_easy_reset(curlHandle);
	if(extra_headers) curl_slist_free_all(extra_headers);
	extra_headers = nullptr;
	data.clear();

	SetCurlHandleVerboseState(curlHandle, currentlogflags & LOGT::CURLVERB);
	SetCacerts(curlHandle);
	curl_easy_setopt(curlHandle, CURLOPT_HTTPGET, 1);
	curl_easy_setopt(curlHandle, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curlHandle, CURLOPT_WRITEFUNCTION, curlCallback );
	curl_easy_setopt(curlHandle, CURLOPT_WRITEDATA, this );
	curl_easy_setopt(curlHandle, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curlHandle, CURLOPT_MAXREDIRS, 5);
	if(auth_obj) {
		std::string oAuthHttpHeader;
		std::string dataStrDummy;
		auth_obj->getOAuthHeader(eOAuthHttpGet, url, dataStrDummy, oAuthHttpHeader);
		if(!oAuthHttpHeader.empty()) extra_headers = curl_slist_append(extra_headers, oAuthHttpHeader.c_str());
	}
	curl_easy_setopt(curlHandle, CURLOPT_HTTPHEADER, extra_headers);

	if(this_owner) {
		assert(this_owner.get() == this);
		sm.AddConn(curlHandle, std::move(this_owner));
	}
}

int dlconn::curlCallback(char* data, size_t size, size_t nmemb, dlconn *obj) {
	int writtenSize = 0;
	if( obj && data ) {
		writtenSize = size * nmemb;
		obj->data.append(data, writtenSize);
	}
	return writtenSize;
}

namespace profimglocal {
	void clear_dl_flags(udc_ptr_p user) {
		user->udc_flags &= ~UDC::IMAGE_DL_IN_PROGRESS;
		user->udc_flags &= ~UDC::HALF_PROFILE_BITMAP_SET;
	};

	void bad_url_handler(const std::string &url, udc_ptr_p user) {
		clear_dl_flags(user);
		TSLogMsgFormat(LOGT::OTHERERR, "Profile image downloaded: %s for user id %" llFmtSpec "d (@%s), does not match expected url of: %s. Maybe user updated profile during download?",
				cstr(url), user->id, cstr(user->GetUser().screen_name), cstr(user->GetUser().profile_img_url));

		//Try again:
		user->ImgIsReady(PENDING_REQ::PROFIMG_DOWNLOAD);
	}
};

void profileimgdlconn::Init(std::unique_ptr<mcurlconn> &&this_owner, const std::string &imgurl_, udc_ptr_p user_) {
	user = user_;
	user->udc_flags |= UDC::IMAGE_DL_IN_PROGRESS;
	LogMsgFormat(LOGT::NETACT, "Downloading profile image %s for user id %" llFmtSpec "d (@%s), conn ID: %d",
			cstr(imgurl_), user_->id, cstr(user_->GetUser().screen_name), id);
	dlconn::Init(std::move(this_owner), imgurl_);
}

void profileimgdlconn::DoRetry(std::unique_ptr<mcurlconn> &&this_owner) {
	if(url == user->GetUser().profile_img_url) Init(std::move(this_owner), url, user);
}

void profileimgdlconn::HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	if(url == user->GetUser().profile_img_url) {
		user->MakeProfileImageFailurePlaceholder();
		if(user->NeedsUpdating(PENDING_REQ::USEREXPIRE)) {
			// Might have failed because user obj is too old
			// Trigger an update
			LogMsgFormat(LOGT::PENDTRACE, "Downloading profile image for user id %" llFmtSpec "d (@%s) failed, triggering profile update attempt",
					user->id, cstr(user->GetUser().screen_name));
			std::shared_ptr<taccount> acc;
			user->GetUsableAccount(acc, true);
			if(acc) {
				acc->MarkUserPending(user);
				acc->StartRestQueryPendings();
			}
		}
	}
	else { //URL changed, try again
		profimglocal::bad_url_handler(url, user);
	}
}

void profileimgdlconn::NewConn(const std::string &imgurl_, udc_ptr_p user_) {
	std::unique_ptr<profileimgdlconn> res(new profileimgdlconn);
	profileimgdlconn *ptr = res.get();
	ptr->Init(std::move(res), imgurl_, user_);
}

void profileimgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	LogMsgFormat(LOGT::NETACT, "Profile image downloaded: %s for user id %" llFmtSpec "d (@%s), conn ID: %d", cstr(url), user->id, cstr(user->GetUser().screen_name), id);

	//URL changed, abort
	if(url != user->GetUser().profile_img_url) {
		profimglocal::bad_url_handler(url, user);
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
			TSLogMsgFormat(LOGT::OTHERERR, "Profile image downloaded: %s for user id %" llFmtSpec "d (@%s), is not OK, possible partial download?",
					cstr(job_data->url), user->id, cstr(user->GetUser().screen_name));
			job_data->ok = false;
		}
		else {
			job_data->img = userdatacontainer::ScaleImageToProfileSize(img);
			job_data->hash = hash_block(job_data->data.data(), job_data->data.size());
		}
	},
	[job_data]() {
		udc_ptr &user = job_data->user;
		profimglocal::clear_dl_flags(user);
		if(!job_data->ok) {
			user->MakeProfileImageFailurePlaceholder();
		}
		else if(job_data->url != user->GetUser().profile_img_url) {
			//Doesn't seem likely, but check again that URL hasn't changed
			profimglocal::bad_url_handler(job_data->url, user);
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

std::string profileimgdlconn::GetConnTypeName() {
	return string_format("Profile image download for user id %" llFmtSpec "d (@%s)", user->id, cstr(user->GetUser().screen_name));
}

void mediaimgdlconn::Init(std::unique_ptr<mcurlconn> &&this_owner, const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_, std::unique_ptr<oAuth> auth_obj_) {
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
	LogMsgFormat(LOGT::NETACT, "Downloading media image %s, id: %" llFmtSpec "d/%" llFmtSpec "d, flags: %X, conn ID: %d",
			cstr(imgurl_), media_id_.m_id, media_id_.t_id, flags_, id);
	dlconn::Init(std::move(this_owner), imgurl_, std::move(auth_obj_));
}

mediaimgdlconn *mediaimgdlconn::new_with_opt_acc_oauth(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_, const taccount *acc) {
	if(acc && acc->active) {
		// Set oAuth parameters for this media download
		std::unique_ptr<oAuth> auth(new oAuth());
		acc->setOAuthParameters(*auth);
		return new mediaimgdlconn(imgurl_, media_id_, flags_, std::move(auth));
	}
	else {
		return new mediaimgdlconn(imgurl_, media_id_, flags_);
	}
}

void mediaimgdlconn::NewConnWithOptAccOAuth(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_, const taccount *acc) {
	std::unique_ptr<mediaimgdlconn> conn(new_with_opt_acc_oauth(imgurl_, media_id_, flags_, acc));
	CURL *ch = conn->curlHandle;
	sm.AddConn(ch, std::move(conn));
}

void mediaimgdlconn::DoRetry(std::unique_ptr<mcurlconn> &&this_owner) {
	Init(std::move(this_owner), url, media_id, flags, std::move(auth_obj));
}

void mediaimgdlconn::HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
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
}

void mediaimgdlconn::NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) {
	LogMsgFormat(LOGT::NETACT, "Media image downloaded: %s, id: %" llFmtSpec "d/%" llFmtSpec "d, flags: %X, conn ID: %d",
			cstr(url), media_id.m_id, media_id.t_id, flags, id);

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

					job_data->thumb_hash = hash_block(data, size);
				}
			}
		}

		if(flags & MIDC::FULLIMG) {
			if(gc.cachemedia && !gc.readonlymode) {
				wxFile file(media_entity::cached_full_filename(job_data->media_id), wxFile::write);
				file.Write(job_data->fulldata.data(), job_data->fulldata.size());
				job_data->full_hash = hash_block(job_data->fulldata.data(), job_data->fulldata.size());
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
					LogMsgFormat(LOGT::OTHERERR, "Media image downloaded: %s, id: %" llFmtSpec "d/%" llFmtSpec "d, flags: %X, is not OK, possible partial download?",
							cstr(job_data->url), job_data->media_id.m_id, job_data->media_id.t_id, flags);
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
}

std::string mediaimgdlconn::GetConnTypeName() {
	return "Media image download";
}
