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

#ifndef HGUARD_SRC_DB
#define HGUARD_SRC_DB

#include "univdefs.h"
#include "tweetidset.h"
#include "flags.h"
#include "hash.h"
#include "media_id_type.h"
#include "ptr_types.h"
#include "set.h"
#include "observer_ptr.h"
#include "twit.h"
#include <string>
#include <memory>
#include <deque>
#include <queue>
#include <set>
#include <forward_list>
#include <wx/event.h>

struct media_entity;
struct tweet;
struct userdata;
struct userdatacontainer;
struct dbconn;
struct dbiothread;
enum class MEF : unsigned int;

enum class DBSM {
	QUIT = 1,
	INSERTTWEET,
	UPDATETWEET,
	SELTWEET,
	INSERTUSER,
	MSGLIST,
	INSERTACC,
	INSERTMEDIA,
	UPDATEMEDIAMSG,
	DELACC,
	UPDATETWEETSETFLAGS_GROUP,
	UPDATETWEETSETFLAGS_MULTI,
	FUNCTION,
	FUNCTION_CALLBACK,
	SELUSER,
	NOTIFYUSERSPURGED,
	INSERTEVENTLOGENTRY,
};

struct dbb_compressed { };
struct dbb_uncompressed { };

template<typename TAG>
struct db_bind_buffer_persistent;

struct deleter_free {
	void operator()(void* ptr) { free(ptr); }
};

template<typename TAG>
struct db_bind_buffer {
	std::unique_ptr<void, deleter_free> membuffer;
	const char *data = nullptr;
	size_t data_size = 0;

	char *mutable_data() {
		return const_cast<char *>(data);
	}

	void *release_membuffer() {
		return membuffer.release();
	}

	void make_persistent() {
		if(!membuffer && data_size) {
			membuffer.reset(malloc(data_size + 1));
			memcpy(membuffer.get(), data, data_size);
			static_cast<char *>(membuffer.get())[data_size] = 0;
			data = static_cast<const char *>(membuffer.get());
		}
	}

	void align() {
		if(!data_size)
			return;
		if(!membuffer) {
			make_persistent();
		}
		else if(data != membuffer.get()) {
			memmove(membuffer.get(), data, data_size);
			static_cast<char *>(membuffer.get())[data_size] = 0;
			data = static_cast<const char *>(membuffer.get());
		}
	}

	void allocate(size_t size) {
		membuffer.reset(malloc(size));
		data = static_cast<const char *>(membuffer.get());
		data_size = size;
	}

	void allocate_nt(size_t size) {
		allocate(size + 1);
		data_size = size;
		const_cast<char *>(data)[data_size] = 0;
	}
};

template<typename TAG>
struct db_bind_buffer_persistent : public db_bind_buffer<TAG> {
	db_bind_buffer_persistent() = default;
	db_bind_buffer_persistent &operator=(db_bind_buffer<TAG> &&src) noexcept {
		*this = db_bind_buffer_persistent(std::move(src));
		return *this;
	}
	db_bind_buffer_persistent(db_bind_buffer<TAG> &&src)
			: db_bind_buffer<TAG>(std::move(src)) {
		this->make_persistent();
	}
};

struct dbreplyevtstruct {
	std::deque<std::pair<wxEvtHandler *, std::unique_ptr<wxEvent> > > reply_list;
};

struct dbsendmsg {
	DBSM type;

	dbsendmsg(DBSM type_) : type(type_) { }
	virtual ~dbsendmsg() { }
};

struct dbsendmsg_list : public dbsendmsg {
	dbsendmsg_list() : dbsendmsg(DBSM::MSGLIST) { }

	std::vector<std::unique_ptr<dbsendmsg> > msglist;
};

struct dbsendmsg_callback : public dbsendmsg {
	dbsendmsg_callback(DBSM type_) : dbsendmsg(type_) { }
	dbsendmsg_callback(DBSM type_, wxEvtHandler *targ_, WXTYPE cmdevtype_, int winid_ = wxID_ANY ) :
		dbsendmsg(type_), targ(targ_), cmdevtype(cmdevtype_), winid(winid_) { }

	wxEvtHandler *targ;
	WXTYPE cmdevtype;
	int winid;

	void SendReply(std::unique_ptr<dbsendmsg> data, dbiothread *th);
};

struct dbinserttweetmsg : public dbsendmsg {
	dbinserttweetmsg() : dbsendmsg(DBSM::INSERTTWEET) { }

	std::string statjson;
	std::string dynjson;
	uint64_t id, user1, user2, rtid, timestamp;
	uint64_t flags;
};

struct dbupdatetweetmsg : public dbsendmsg {
	dbupdatetweetmsg() : dbsendmsg(DBSM::UPDATETWEET) { }

	std::string dynjson;
	uint64_t id;
	uint64_t flags;
};

struct dbrettweetdata {
	db_bind_buffer_persistent<dbb_uncompressed> statjson;
	db_bind_buffer_persistent<dbb_uncompressed> dynjson;
	uint64_t id, user1, user2, rtid, timestamp;
	uint64_t flags;
};

struct dbretuserdata {
	userdata ud;
	uint64_t id;
	uint64_t lastupdate;
	uint64_t lastupdate_wrotetodb;
	std::string cached_profile_img_url;
	shb_iptr cached_profile_img_sha1;
	tweetidset mention_set;
	uint64_t profile_img_last_used;
	uint64_t profile_img_last_used_db;

	dbretuserdata() = default;
	dbretuserdata(const dbretuserdata& that) = delete;
};

enum class DBSTMF {
	NO_ERR          = 1<<0,
	CLEARNOUPDF     = 1<<1,
};
template<> struct enum_traits<DBSTMF> { static constexpr bool flags = true; };

struct dbseltweetmsg : public dbsendmsg_callback {
	dbseltweetmsg() : dbsendmsg_callback(DBSM::SELTWEET) { }

	flagwrapper<DBSTMF> flags = 0;
	container::set<uint64_t> id_set;         // ids to select
	std::deque<dbrettweetdata> data;         // return data
	std::deque<dbretuserdata> user_data;     // return data
};

struct dbselusermsg : public dbsendmsg_callback {
	dbselusermsg() : dbsendmsg_callback(DBSM::SELUSER) { }

	container::set<uint64_t> id_set;         // ids to select
	std::deque<dbretuserdata> data;          // return data
};

struct dbinsertusermsg : public dbsendmsg {
	dbinsertusermsg() : dbsendmsg(DBSM::INSERTUSER) { }
	uint64_t id;
	std::string json;
	std::string cached_profile_img_url;
	time_t createtime;
	uint64_t lastupdate;
	shb_iptr cached_profile_img_hash;
	db_bind_buffer_persistent<dbb_compressed> mentionindex;
	uint64_t profile_img_last_used;
};

struct dbinsertaccmsg : public dbsendmsg_callback {
	dbinsertaccmsg() : dbsendmsg_callback(DBSM::INSERTACC) { }

	std::string name;            //account name
	std::string dispname;        //account name
	uint64_t userid;
	unsigned int dbindex;        //return data
};

struct dbdelaccmsg : public dbsendmsg {
	dbdelaccmsg() : dbsendmsg(DBSM::DELACC) { }

	unsigned int dbindex = 0;
};

struct dbinsertmediamsg : public dbsendmsg {
	dbinsertmediamsg() : dbsendmsg(DBSM::INSERTMEDIA) { }
	media_id_type media_id;
	std::string url;
	uint64_t lastused;
};

enum class DBUMMT {
	THUMBCHECKSUM = 1,
	FULLCHECKSUM,
	FLAGS,
	LASTUSED,
};

struct dbupdatemediamsg : public dbsendmsg {
	dbupdatemediamsg(DBUMMT type_) : dbsendmsg(DBSM::UPDATEMEDIAMSG), update_type(type_) { }
	media_id_type media_id;
	shb_iptr chksm;
	flagwrapper<MEF> flags;
	uint64_t lastused;
	DBUMMT update_type;
};

struct dbupdatetweetsetflagsmsg_group : public dbsendmsg {
	dbupdatetweetsetflagsmsg_group(tweetidset &&ids_, uint64_t setmask_, uint64_t unsetmask_)
			: dbsendmsg(DBSM::UPDATETWEETSETFLAGS_GROUP), ids(std::move(ids_)), setmask(setmask_), unsetmask(unsetmask_) { }

	tweetidset ids;
	uint64_t setmask;
	uint64_t unsetmask;
};

struct dbupdatetweetsetflagsmsg_multi : public dbsendmsg {
	dbupdatetweetsetflagsmsg_multi()
			: dbsendmsg(DBSM::UPDATETWEETSETFLAGS_MULTI) { }

	struct flag_action {
		uint64_t id;
		uint64_t setmask;
		uint64_t unsetmask;
	};
	std::vector<flag_action> flag_actions;
};

struct dbnotifyuserspurgedmsg : public dbsendmsg {
	dbnotifyuserspurgedmsg(useridset &&ids_) : dbsendmsg(DBSM::NOTIFYUSERSPURGED), ids(std::move(ids_)) { }

	useridset ids;
};

enum class DB_EVENTLOG_TYPE {
	FOLLOWED_ME,
	FOLLOWED_ME_PENDING,
	UNFOLLOWED_ME,
	I_FOLLOWED,
	I_FOLLOWED_PENDING,
	I_UNFOLLOWED,
};

// DB event log flags, not used yet
enum class DBELF {
};
template<> struct enum_traits<DBELF> { static constexpr bool flags = true; };

struct dbinserteventlogentrymsg : public dbsendmsg {
	dbinserteventlogentrymsg() : dbsendmsg(DBSM::INSERTEVENTLOGENTRY) { }

	int accid;
	DB_EVENTLOG_TYPE type;
	flagwrapper<DBELF> flags;
	uint64_t obj;
	time_t eventtime;
	std::string extrajson;
};


struct db_handle_msg_pending_guard {
	std::deque<tweet_ptr> tweets;
	std::deque<udc_ptr> users;

	~db_handle_msg_pending_guard();
};

bool DBC_Init(const std::string &filename);
void DBC_DeInit();
void DBC_AsyncWriteBackState();
void DBC_AsyncWriteBackStateMinimal();
void DBC_SendMessage(std::unique_ptr<dbsendmsg> msg);
void DBC_SendMessageOrAddToList(std::unique_ptr<dbsendmsg> msg, optional_observer_ptr<dbsendmsg_list> msglist);
void DBC_SendMessageBatched(std::unique_ptr<dbsendmsg> msg);
observer_ptr<dbsendmsg_list> DBC_GetMessageBatchQueue();
void DBC_SendBatchedTweetFlagUpdate(uint64_t id, uint64_t setmask, uint64_t unsetmask);
void DBC_SendAccDBUpdate(std::unique_ptr<dbinsertaccmsg> insmsg);
void DBC_InsertMedia(media_entity &me, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_UpdateMedia(media_entity &me, DBUMMT update_type, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_InsertNewEventLogEntry(optional_observer_ptr<dbsendmsg_list> msglist, optional_observer_ptr<taccount> acc, DB_EVENTLOG_TYPE type,
		flagwrapper<DBELF> flags, uint64_t obj, time_t eventtime = 0, std::string extrajson = "");
void DBC_InsertNewTweet(tweet_ptr_p tobj, std::string statjson, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_UpdateTweetDyn(tweet_ptr_p tobj, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_InsertUser(udc_ptr_p u, optional_observer_ptr<dbsendmsg_list> msglist = nullptr);
void DBC_HandleDBSelTweetMsg(dbseltweetmsg &msg, optional_observer_ptr<db_handle_msg_pending_guard> pending_guard = nullptr);
void DBC_SetDBSelTweetMsgHandler(dbseltweetmsg &msg, std::function<void(dbseltweetmsg &, dbconn *)> f);
void DBC_PrepareStdTweetLoadMsg(dbseltweetmsg &loadmsg);
void DBC_DBSelUserReturnDataHandler(std::deque<dbretuserdata> data, optional_observer_ptr<db_handle_msg_pending_guard> pending_guard = nullptr);
void DBC_SetDBSelUserMsgHandler(dbselusermsg &msg, std::function<void(dbselusermsg &, dbconn *)> f);
void DBC_PrepareStdUserLoadMsg(dbselusermsg &loadmsg);

#endif
