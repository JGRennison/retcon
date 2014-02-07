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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_SOCKET_OPS
#define HGUARD_SRC_SOCKET_OPS

#include "univdefs.h"
#include "socket.h"
#include "media_id_type.h"
#include "flags.h"

struct dlconn : public mcurlconn {
	CURL* curlHandle;
	std::string data;

	static int curlCallback(char* data, size_t size, size_t nmemb, dlconn *obj);
	dlconn();
	void Init(const std::string &url_);
	void Reset();
	~dlconn();
	CURL *GenGetCurlHandle() { return curlHandle; }
};

struct profileimgdlconn : public dlconn {
	std::shared_ptr<userdatacontainer> user;
	static connpool<profileimgdlconn> cp;

	void Init(const std::string &imgurl_, const std::shared_ptr<userdatacontainer> &user_);

	void NotifyDoneSuccess(CURL *easy, CURLcode res);
	void Reset();
	void DoRetry();
	void HandleFailure(long httpcode, CURLcode res);
	static profileimgdlconn *GetConn(const std::string &imgurl_, const std::shared_ptr<userdatacontainer> &user_);
	virtual wxString GetConnTypeName();
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

	void Init(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_ = 0);
	mediaimgdlconn(const std::string &imgurl_, media_id_type media_id_, flagwrapper<MIDC> flags_ = 0) { Init(imgurl_, media_id_, flags_); }

	void NotifyDoneSuccess(CURL *easy, CURLcode res);
	void Reset();
	void DoRetry();
	void HandleFailure(long httpcode, CURLcode res);
	virtual wxString GetConnTypeName();
};

#endif
