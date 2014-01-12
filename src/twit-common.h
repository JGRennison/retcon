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
#include "flags.h"
#include <functional>
#include <set>
#include <bitset>

struct userdatacontainer;
struct tweet;
struct media_entity;

typedef std::set<uint64_t, std::greater<uint64_t> > tweetidset;		//std::set, sorted in opposite order

struct tweet_flags {
	protected:
	std::bitset<62> bits;

	public:
	tweet_flags() : bits() { }
	tweet_flags(unsigned long long val) : bits(val) { }
	tweet_flags(const tweet_flags &cpysrc) : bits(cpysrc.Save()) { }

	//Note that the below functions do minimal, if any, error checking

	static constexpr unsigned long long GetFlagValue(char in) { return ((uint64_t) 1) << GetFlagNum(in); }
	static constexpr ssize_t GetFlagNum(char in) {
		return (in >= '0' && in <= '9')
			? in-'0'
			: ((in >= 'a' && in <= 'z')
				? 10 + in - 'a'
				: ((in >= 'A' && in <= 'Z')
					? 10 + 26 + in - 'A'
					: -1
				  )
			  );
	}
	static constexpr char GetFlagChar(size_t in) {
		return (in<10)
			? in + '0'
			: ((in >= 10 && in < 36)
				? in + 'a' - 10
				: ((in >= 36 && in < 62)
					? in + 'A' - 36 : '?'
				  )
			  );
	}

	static unsigned long long GetFlagStringValue(const std::string &in) {
		unsigned long long out = 0;
		for(auto &it : in) {
			out |= GetFlagValue(it);
		}
		return out;
	}
	static std::string GetValueString(unsigned long long val);

	bool Get(char in) const {
		ssize_t num=GetFlagNum(in);
		if(num >= 0) return bits.test(num);
		else return 0;
	}

	void Set(char in, bool value = true) {
		ssize_t num=GetFlagNum(in);
		if(num >= 0) bits.set(num, value);
	}

	bool Toggle(char in) {
		ssize_t num=GetFlagNum(in);
		if(num >= 0) {
			bits.flip(num);
			return bits.test(num);
		}
		else return 0;
	}

	unsigned long long Save() const { return bits.to_ullong(); }
	std::string GetString() const {
		return GetValueString(Save());
	}
};

struct cached_id_sets {
	tweetidset unreadids;
	tweetidset highlightids;
	tweetidset hiddenids;
	tweetidset deletedids;

	inline static void IterateLists(std::function<void(const char *, tweetidset cached_id_sets::*, unsigned long long)> f) {
		f("unreadids", &cached_id_sets::unreadids, tweet_flags::GetFlagValue('u'));
		f("highlightids", &cached_id_sets::highlightids, tweet_flags::GetFlagValue('H'));
		f("hiddenids", &cached_id_sets::hiddenids, tweet_flags::GetFlagValue('h'));
		f("deletedids", &cached_id_sets::deletedids, tweet_flags::GetFlagValue('X'));
	}
	inline void foreach(std::function<void(tweetidset &)> f) {
		IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
			f(this->*ptr);
		});
	}
	inline void foreach(cached_id_sets &cid2, std::function<void(tweetidset &, tweetidset &)> f) {
		IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
			f(this->*ptr, cid2.*ptr);
		});
	}
	void CheckTweet(tweet &tw);
	void RemoveTweet(uint64_t id);
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

enum class UMPTF { //For UnmarkPendingTweet
	TPDB_NOUPDF        = 1<<0,
	RMV_LKPINPRGFLG    = 1<<1,  //Clear UDC::LOOKUP_IN_PROGRESS
};
template<> struct enum_traits<UMPTF> { static constexpr bool flags = true; };

#endif
