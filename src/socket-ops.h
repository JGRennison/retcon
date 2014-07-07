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

#ifndef HGUARD_SRC_SOCKET_OPS
#define HGUARD_SRC_SOCKET_OPS

#include "univdefs.h"
#include "socket.h"
#include "media_id_type.h"
#include "flags.h"
#include "ptr_types.h"

class oAuth;
struct taccount;

struct dlconn : public mcurlconn {
	CURL* curlHandle = nullptr;
	std::string data;
	struct curl_slist *extra_headers = nullptr;

	static int curlCallback(char* data, size_t size, size_t nmemb, dlconn *obj);
	void Init(std::unique_ptr<mcurlconn> &&this_owner, const std::string &url_, oAuth *auth_obj = nullptr);
	~dlconn();
	CURL *GenGetCurlHandle() { return curlHandle; }

	protected:
	dlconn() { }
};

struct profileimgdlconn : public dlconn {
	udc_ptr user;

	void Init(std::unique_ptr<mcurlconn> &&this_owner, const std::string &imgurl_, udc_ptr_p user_);

	void NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) override;
	void DoRetry(std::unique_ptr<mcurlconn> &&this_owner) override;
	void HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) override;
	static void NewConn(const std::string &imgurl_, udc_ptr_p user_);
	virtual std::string GetConnTypeName() override;

	protected:
	profileimgdlconn() { }
};

enum class MIDC {
	FULLIMG                      = 1<<0,
	THUMBIMG                     = 1<<1,
	REDRAW_TWEETS                = 1<<2,
	OPPORTUNIST_THUMB            = 1<<3,
	OPPORTUNIST_REDRAW_TWEETS    = 1<<4,
};
template<> struct enum_traits<MIDC> { static constexpr bool flags = true; };

struct mediaimgdlconn : public dlconn {
	media_id_type media_id;
	flagwrapper<MIDC> flags;

	void Init(std::unique_ptr<mcurlconn> &&this_owner, const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_ = 0, oAuth *auth_obj = nullptr);

	void NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) override;
	void DoRetry(std::unique_ptr<mcurlconn> &&this_owner);
	void HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) override;
	virtual std::string GetConnTypeName() override;

	static void NewConnWithOptAccOAuth(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_ = 0, const taccount *acc = nullptr);

	protected:
	mediaimgdlconn(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_ = 0, oAuth *auth_obj = nullptr) { Init(nullptr, imgurl_, media_id_, flags_, auth_obj); }
	static mediaimgdlconn *new_with_opt_acc_oauth(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_ = 0, const taccount *acc = nullptr);
};

#endif
