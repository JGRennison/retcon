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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_TWIT_COMMON
#define HGUARD_SRC_TWIT_COMMON

#include "univdefs.h"
#include <functional>
#include <set>

struct userdatacontainer;
struct tweet;
struct media_entity;

typedef std::set<uint64_t, std::greater<uint64_t> > tweetidset;		//std::set, sorted in opposite order

struct cached_id_sets {
	tweetidset unreadids;
	tweetidset highlightids;
	tweetidset hiddenids;
	tweetidset deletedids;

	inline static void IterateLists(std::function<void(const char *, tweetidset cached_id_sets::*)> f) {
		f("unreadids", &cached_id_sets::unreadids);
		f("highlightids", &cached_id_sets::highlightids);
		f("hiddenids", &cached_id_sets::hiddenids);
		f("deletedids", &cached_id_sets::deletedids);
	}
	inline void foreach(std::function<void(tweetidset &)> f) {
		IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr) {
			f(this->*ptr);
		});
	}
	inline void foreach(cached_id_sets &cid2, std::function<void(tweetidset &, tweetidset &)> f) {
		IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr) {
			f(this->*ptr, cid2.*ptr);
		});
	}
	void CheckTweet(tweet &tw);
};

struct media_id_type {
	uint64_t m_id;
	uint64_t t_id;
	media_id_type() : m_id(0), t_id(0) { }
	operator bool() const { return m_id || t_id; }
};

inline bool operator==(const media_id_type &m1, const media_id_type &m2) {
	return (m1.m_id==m2.m_id) && (m1.t_id==m2.t_id);
}

namespace std {
  template <> struct hash<media_id_type> : public unary_function<media_id_type, size_t>
  {
    inline size_t operator()(const media_id_type & x) const
    {
      return (hash<uint64_t>()(x.m_id)<<1) ^ hash<uint64_t>()(x.t_id);
    }
  };
}

typedef enum {
	CS_NULL          = 0,
	CS_ACCVERIFY     = 1,
	CS_TIMELINE,
	CS_STREAM,
	CS_USERLIST,
	CS_DMTIMELINE,
	CS_FRIENDLOOKUP,
	CS_USERLOOKUPWIN,
	CS_FRIENDACTION_FOLLOW,
	CS_FRIENDACTION_UNFOLLOW,
	CS_POSTTWEET,
	CS_SENDDM,
	CS_FAV,
	CS_UNFAV,
	CS_RT,
	CS_DELETETWEET,
	CS_DELETEDM,
	CS_USERTIMELINE,
	CS_USERFAVS,
	CS_USERFOLLOWING,
	CS_USERFOLLOWERS,
	CS_SINGLETWEET,
} CS_ENUMTYPE;

#endif
