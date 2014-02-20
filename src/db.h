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
#include <string>
#include <memory>
#include <deque>
#include <queue>
#include <set>
#include <forward_list>
#include <wx/event.h>

struct media_entity;
struct tweet;
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
	UPDATETWEETSETFLAGS,
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

	std::queue<dbsendmsg *> msglist;
};

struct dbsendmsg_callback : public dbsendmsg {
	dbsendmsg_callback(DBSM type_) : dbsendmsg(type_) { }
	dbsendmsg_callback(DBSM type_, wxEvtHandler *targ_, WXTYPE cmdevtype_, int winid_ = wxID_ANY ) :
		dbsendmsg(type_), targ(targ_), cmdevtype(cmdevtype_), winid(winid_) { }

	wxEvtHandler *targ;
	WXTYPE cmdevtype;
	int winid;

	void SendReply(void *data, dbiothread *th);
};

struct dbinserttweetmsg : public dbsendmsg {
	dbinserttweetmsg() : dbsendmsg(DBSM::INSERTTWEET) { }

	std::string statjson;
	std::string dynjson;
	uint64_t id, user1, user2, rtid, timestamp;
	uint64_t flags;
	unsigned char *mediaindex;			//already packed and compressed, must be malloced
	size_t mediaindex_size;
};

struct dbupdatetweetmsg : public dbsendmsg {
	dbupdatetweetmsg() : dbsendmsg(DBSM::UPDATETWEET) { }

	std::string dynjson;
	uint64_t id;
	uint64_t flags;
};

struct dbrettweetdata {
	char *statjson;	//free when done
	char *dynjson;	//free when done
	uint64_t id, user1, user2, rtid, timestamp;
	uint64_t flags;

	dbrettweetdata() : statjson(0), dynjson(0) { }
	~dbrettweetdata() {
		if(statjson) free(statjson);
		if(dynjson) free(dynjson);
	}
	dbrettweetdata(const dbrettweetdata& that) = delete;
};

struct dbretmediadata {
	media_id_type media_id;
	std::string url;
	shb_iptr full_img_sha1;
	shb_iptr thumb_img_sha1;
	flagwrapper<MEF> flags;

	dbretmediadata() : url(0) { }
	~dbretmediadata() { }
	dbretmediadata(const dbretmediadata& that) = delete;
};

enum class DBSTMF {
	PULLMEDIA       = 1<<0,
	NO_ERR          = 1<<1,
	NET_FALLBACK    = 1<<2,
	CLEARNOUPDF     = 1<<3,
};
template<> struct enum_traits<DBSTMF> { static constexpr bool flags = true; };

struct dbseltweetmsg : public dbsendmsg_callback {
	dbseltweetmsg() : dbsendmsg_callback(DBSM::SELTWEET), flags(0) { }

	flagwrapper<DBSTMF> flags;
	std::set<uint64_t> id_set;                      //ids to select
	std::forward_list<dbrettweetdata> data;         //return data
	std::forward_list<dbretmediadata> media_data;   //return data
};

struct dbseltweetmsg_netfallback : public dbseltweetmsg {
	dbseltweetmsg_netfallback() : dbseltweetmsg(), dbindex(0) {
		flags |= DBSTMF::NET_FALLBACK;
	}

	unsigned int dbindex;				//for the use of the main thread only
};

struct dbinsertusermsg : public dbsendmsg {
	dbinsertusermsg() : dbsendmsg(DBSM::INSERTUSER) { }
	uint64_t id;
	std::string json;
	std::string cached_profile_img_url;
	time_t createtime;
	uint64_t lastupdate;
	shb_iptr cached_profile_img_hash;
	unsigned char *mentionindex;			//already packed and compressed, must be malloced
	size_t mentionindex_size;
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

	unsigned int dbindex;
};

struct dbinsertmediamsg : public dbsendmsg {
	dbinsertmediamsg() : dbsendmsg(DBSM::INSERTMEDIA) { }
	media_id_type media_id;
	std::string url;
};

enum class DBUMMT {
	THUMBCHECKSUM = 1,
	FULLCHECKSUM,
	FLAGS,
};

struct dbupdatemediamsg : public dbsendmsg {
	dbupdatemediamsg(DBUMMT type) : dbsendmsg(DBSM::UPDATEMEDIAMSG), update_type(type) { }
	media_id_type media_id;
	shb_iptr chksm;
	flagwrapper<MEF> flags;
	DBUMMT update_type;
};

struct dbupdatetweetsetflagsmsg : public dbsendmsg {
	dbupdatetweetsetflagsmsg(tweetidset &&ids_, uint64_t setmask_, uint64_t unsetmask_) : dbsendmsg(DBSM::UPDATETWEETSETFLAGS), ids(ids_), setmask(setmask_), unsetmask(unsetmask_) { }

	tweetidset ids;
	uint64_t setmask;
	uint64_t unsetmask;
};

enum class HDBSF {
	NOPENDINGS         = 1<<0,
};
template<> struct enum_traits<HDBSF> { static constexpr bool flags = true; };

bool DBC_Init(const std::string &filename);
void DBC_DeInit();
void DBC_SendMessage(dbsendmsg *msg);
void DBC_SendMessageOrAddToList(dbsendmsg *msg, dbsendmsg_list *msglist);
void DBC_SendMessageBatched(dbsendmsg *msg);
void DBC_SendAccDBUpdate(dbinsertaccmsg *insmsg);
void DBC_InsertMedia(media_entity &me, dbsendmsg_list *msglist = 0);
void DBC_UpdateMedia(media_entity &me, DBUMMT update_type, dbsendmsg_list *msglist = 0);
void DBC_InsertNewTweet(tweet_ptr_p tobj, std::string statjson, dbsendmsg_list *msglist = 0);
void DBC_UpdateTweetDyn(tweet_ptr_p tobj, dbsendmsg_list *msglist = 0);
void DBC_InsertUser(udc_ptr_p u, dbsendmsg_list *msglist = 0);
void DBC_HandleDBSelTweetMsg(dbseltweetmsg *msg, flagwrapper<HDBSF> flags);
void DBC_SetDBSelTweetMsgHandler(dbseltweetmsg *msg, std::function<void(dbseltweetmsg *, dbconn *)> f);
bool DBC_AllMediaEntitiesLoaded();
void DBC_PrepareStdTweetLoadMsg(dbseltweetmsg *loadmsg);

#endif
