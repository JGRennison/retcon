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
#include "safe_observer_ptr.h"
#include "flags.h"
#include "observer_ptr.h"
#include "util.h"
#include <memory>
#include <string>
#include <type_traits>

struct restbackfillstate;
struct taccount;
struct friendlookup;
struct streamconntimeout;
struct userlookup;
struct mainframe;
struct jsonparser;
struct twitcurlext_upload_media_state;

struct TwitterErrorMsg {
	unsigned int code;
	std::string message;
};

struct twitcurlext: public twitCurl, public mcurlconn {
	enum class TCF {
		ISSTREAM       = 1<<0,
		ALWAYSREPARSE  = 1<<1,
	};

	std::weak_ptr<taccount> tacc;
	bool inited = false;
	flagwrapper<TCF> tc_flags = 0;
	flagwrapper<PAF> post_action_flags = 0;
	observer_ptr<mainframe> ownermainframe;
	safe_observer_untyped_ptr mp;

	struct NotifyDoneSuccessState {
		bool do_post_actions = true;
		CURL *easy;
		CURLcode res;
		std::unique_ptr<mcurlconn> &&this_owner;

		NotifyDoneSuccessState(CURL *easy_, CURLcode res_, std::unique_ptr<mcurlconn> &&this_owner_)
				: easy(easy_), res(res_), this_owner(std::move(this_owner_)) { }
	};

	struct HandleFailureState {
		bool msgbox = false;
		bool retry = false;
		long httpcode;
		CURLcode res;
		std::unique_ptr<mcurlconn> &&this_owner;

		HandleFailureState(long httpcode_, CURLcode res_, std::unique_ptr<mcurlconn> &&this_owner_)
				: httpcode(httpcode_), res(res_), this_owner(std::move(this_owner_)) { }
	};

	virtual CURL *GenGetCurlHandle() override { return GetCurlHandle(); }
	void TwInit(std::shared_ptr<taccount> acc);
	virtual void NotifyDoneSuccess(CURL *easy, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) override;
	virtual void NotifyDoneSuccessHandler(const std::shared_ptr<taccount> &acc, NotifyDoneSuccessState &state) { }
	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) = 0;
	virtual void HandleFailure(long httpcode, CURLcode res, std::unique_ptr<mcurlconn> &&this_owner) override;
	virtual std::string GetFailureLogInfo() override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) { }
	virtual std::string GetConnTypeName() override;
	virtual std::string GetConnTypeNameBase() = 0;
	virtual bool IsQueueable(const std::shared_ptr<taccount> &acc);
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) = 0;
	void DoRetry(std::unique_ptr<mcurlconn> &&this_owner) override;

	protected:
	twitcurlext() { }
	void DoQueueAsyncExec(std::unique_ptr<mcurlconn> this_owner);

	public:
	template<typename T> static void QueueAsyncExec(std::unique_ptr<T> conn) {
		// This is to make sure we don't dereference conn to get the vtable/this,
		// after moving the unique_ptr into the argument
		static_assert(std::is_base_of<twitcurlext, T>::value, "T not derived from twitcurlext");
		twitcurlext *ptr = conn.get();
		ptr->DoQueueAsyncExec(static_pointer_cast<mcurlconn>(std::move(conn)));
	}

	template<typename T, typename F> static void IterateConnsByAcc(const std::shared_ptr<taccount> &acc, F func) {
		static_assert(std::is_base_of<twitcurlext, T>::value, "T not derived from twitcurlext");
		socketmanager::IterateConns([&](mcurlconn &c) {
			if (T *t = dynamic_cast<T *>(&c)) {
				if (t->tacc.lock() == acc) {
					return func(*t);
				}
			}
			return false;
		});
	}
};
template<> struct enum_traits<twitcurlext::TCF> { static constexpr bool flags = true; };

struct twitcurlext_stream: public twitcurlext {
	std::unique_ptr<streamconntimeout> scto;

	static std::unique_ptr<twitcurlext_stream> make_new(std::shared_ptr<taccount> acc);

	virtual void NotifyDoneSuccessHandler(const std::shared_ptr<taccount> &acc, NotifyDoneSuccessState &state) override;
	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;
	virtual void AddToRetryQueueNotify() override;
	virtual void RemoveFromRetryQueueNotify() override;

	~twitcurlext_stream();

	private:
	static void StreamCallback(std::string &data, twitCurl *pTwitCurlObj, void *userdata);
	static void StreamActivityCallback(twitCurl *pTwitCurlObj, void *userdata);
};

struct twitcurlext_rbfs: public twitcurlext {
	enum class CONNTYPE {
		NONE,
		TIMELINE,
		DMTIMELINE,
		USERTIMELINE,
		USERFAVS,
	};

	CONNTYPE conntype;
	observer_ptr<restbackfillstate> rbfs;

	static std::unique_ptr<twitcurlext_rbfs> make_new(std::shared_ptr<taccount> acc, observer_ptr<restbackfillstate> rbfs);

	virtual void NotifyDoneSuccessHandler(const std::shared_ptr<taccount> &acc, NotifyDoneSuccessState &state) override;
	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;

	protected:
	void DoExecRestGetTweetBackfill(std::unique_ptr<mcurlconn> this_owner);
};

struct twitcurlext_accverify: public twitcurlext {
	static std::unique_ptr<twitcurlext_accverify> make_new(std::shared_ptr<taccount> acc);

	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	bool TwSyncStartupAccVerify();
	virtual bool IsQueueable(const std::shared_ptr<taccount> &acc) override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;

	protected:
	void DoTwStartupAccVerify(std::unique_ptr<twitcurlext> this_owner);
};

struct twitcurlext_postcontent: public twitcurlext {
	enum class CONNTYPE {
		NONE,
		POSTTWEET,
		SENDDM,
	};

	struct upload_item {
		std::string filename;
		std::string upload_id;

		upload_item(std::string filename_)
				: filename(filename_) { }
	};

	struct upload_media_state {
		private:
		std::unique_ptr<twitcurlext_postcontent> content_conn;

		public:
		upload_media_state(std::unique_ptr<twitcurlext_postcontent> content_conn_);
		void UploadSuccess();
		void UploadFailure();
	};

	CONNTYPE conntype;
	uint64_t replyto_id = 0;
	uint64_t dmtarg_id = 0;
	bool has_been_enqueued = false;
	std::string text;
	std::vector<std::shared_ptr<upload_item>> image_uploads;

	static std::unique_ptr<twitcurlext_postcontent> make_new(std::shared_ptr<taccount> acc, CONNTYPE type);

	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;
	bool IsImageUploadingDone() const;
	void SetImageUploads(const std::vector<std::string> filenames);
	~twitcurlext_postcontent();
};

struct twitcurlext_uploadmedia: public twitcurlext {
	std::shared_ptr<twitcurlext_postcontent::upload_item> item;
	std::shared_ptr<twitcurlext_postcontent::upload_media_state> upload_state;
	bool has_been_enqueued = false;

	static std::unique_ptr<twitcurlext_uploadmedia> make_new(std::shared_ptr<taccount> acc,
			std::shared_ptr<twitcurlext_postcontent::upload_item> item_, std::shared_ptr<twitcurlext_postcontent::upload_media_state> upload_state_);

	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;
	~twitcurlext_uploadmedia();
};

struct twitcurlext_userlist: public twitcurlext {
	std::unique_ptr<userlookup> ul;

	static std::unique_ptr<twitcurlext_userlist> make_new(std::shared_ptr<taccount> acc, std::unique_ptr<userlookup> ul_);

	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;
};


struct twitcurlext_friendlookup: public twitcurlext {
	std::unique_ptr<friendlookup> fl;

	static std::unique_ptr<twitcurlext_friendlookup> make_new(std::shared_ptr<taccount> acc, std::unique_ptr<friendlookup> fl_);

	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;
};

struct twitcurlext_userlookupwin: public twitcurlext {
	enum class LOOKUPMODE {
		ID,
		SCREENNAME,
	};

	LOOKUPMODE mode;
	std::string search_string;

	static std::unique_ptr<twitcurlext_userlookupwin> make_new(std::shared_ptr<taccount> acc, LOOKUPMODE mode, std::string search_string);

	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;
};


struct twitcurlext_simple: public twitcurlext {
	enum class CONNTYPE {
		NONE,
		FRIENDACTION_FOLLOW,
		FRIENDACTION_UNFOLLOW,
		FAV,
		UNFAV,
		RT,
		DELETETWEET,
		DELETEDM,
		USERFOLLOWING,
		USERFOLLOWERS,
		SINGLETWEET,
		SINGLEDM,
		OWNFOLLOWERLISTING,
		OWNINCOMINGFOLLOWLISTING,
		OWNOUTGOINGFOLLOWLISTING,
		BLOCK,
		UNBLOCK,
		MUTE,
		UNMUTE,
	};

	CONNTYPE conntype;
	uint64_t extra_id = 0;

	static std::unique_ptr<twitcurlext_simple> make_new(std::shared_ptr<taccount> acc, CONNTYPE type);

	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual void HandleFailureHandler(const std::shared_ptr<taccount> &acc, HandleFailureState &state) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;
};

struct twitcurlext_block_list: public twitcurlext {
	BLOCKTYPE blocktype;
	useridset block_id_list;
	int64_t current_cursor = -1;

	static std::unique_ptr<twitcurlext_block_list> make_new(std::shared_ptr<taccount> acc, BLOCKTYPE type);

	virtual void NotifyDoneSuccessHandler(const std::shared_ptr<taccount> &acc, NotifyDoneSuccessState &state) override;
	virtual void ParseHandler(const std::shared_ptr<taccount> &acc, jsonparser &jp) override;
	virtual std::string GetConnTypeNameBase() override;
	virtual void HandleQueueAsyncExec(const std::shared_ptr<taccount> &acc, std::unique_ptr<mcurlconn> &&this_owner) override;
	bool IsCursored() const;
};

#endif
