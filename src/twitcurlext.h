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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TWITCURLEXT
#define HGUARD_SRC_TWITCURLEXT

#include "univdefs.h"
#include "twitcurlext-common.h"
#include "twit-common.h"
#include "libtwitcurl/twitcurl.h"
#include "socket.h"
#include "magic_ptr.h"
#include "flags.h"
#include "observer_ptr.h"
#include <memory>
#include <string>

struct restbackfillstate;
struct taccount;
struct friendlookup;
struct streamconntimeout;
struct userlookup;
struct mainframe;

struct twitcurlext: public twitCurl, public mcurlconn {
	enum class TCF {
		ISSTREAM       = 1<<0,
	};

	std::weak_ptr<taccount> tacc;
	CS_ENUMTYPE connmode = CS_ENUMTYPE::CS_NULL;
	bool inited = false;
	flagwrapper<TCF> tc_flags = 0;
	flagwrapper<PAF> post_action_flags = 0;
	std::shared_ptr<streamconntimeout> scto;
	observer_ptr<restbackfillstate> rbfs;
	std::unique_ptr<userlookup> ul;
	std::string genurl;
	std::string extra1;
	std::vector<std::string> extra_array;
	uint64_t extra_id = 0;
	observer_ptr<mainframe> ownermainframe;
	magic_ptr mp;
	std::unique_ptr<friendlookup> fl;

	void NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) override;
	void TwInit(std::shared_ptr<taccount> acc);
	static void TwStartupAccVerify(std::unique_ptr<twitcurlext> conn);
	bool TwSyncStartupAccVerify();
	CURL *GenGetCurlHandle() { return GetCurlHandle(); }
	twitcurlext(std::shared_ptr<taccount> acc);
	twitcurlext();
	virtual ~twitcurlext();
	void DoRetry(std::unique_ptr<mcurlconn> &&this_owner) override;
	void HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) override;
	static void QueueAsyncExec(std::unique_ptr<twitcurlext> conn);
	static void ExecRestGetTweetBackfill(std::unique_ptr<twitcurlext> conn);
	virtual std::string GetConnTypeName();
	virtual void AddToRetryQueueNotify() override;
	virtual void RemoveFromRetryQueueNotify() override;

	template<typename F> static void IterateConnsByAcc(std::shared_ptr<taccount> acc, F func) {
		socketmanager::IterateConns([&](mcurlconn &c) {
			if(twitcurlext *t = dynamic_cast<twitcurlext *>(&c)) {
				if(t->tacc.lock() == acc) {
					return func(*t);
				}
			}
			return false;
		});
	}

	DECLARE_EVENT_TABLE()

	protected:
	void DoTwStartupAccVerify(std::unique_ptr<twitcurlext> this_owner);
	void DoQueueAsyncExec(std::unique_ptr<twitcurlext> this_owner);
	void DoExecRestGetTweetBackfill(std::unique_ptr<twitcurlext> this_owner);
};
template<> struct enum_traits<twitcurlext::TCF> { static constexpr bool flags = true; };

void StreamCallback(std::string &data, twitCurl* pTwitCurlObj, void *userdata);
void StreamActivityCallback(twitCurl* pTwitCurlObj, void *userdata);

#endif
