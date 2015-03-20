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

#ifndef HGUARD_SRC_TWIT_COMMON
#define HGUARD_SRC_TWIT_COMMON

#include "univdefs.h"
#include "flags.h"
#include "tweetidset.h"
#include "ptr_types.h"
#include <bitset>

struct userdatacontainer;
struct tweet;
struct media_entity;

struct tweet_flags {
	protected:
	std::bitset<62> bits;

	public:
	tweet_flags() : bits() { }
	tweet_flags(unsigned long long val) : bits(val) { }
	tweet_flags(const tweet_flags &cpysrc) : bits(cpysrc.ToULLong()) { }

	//Note that the below functions do minimal, if any, error checking

	static constexpr unsigned long long GetFlagValue(char in) {
		return ((uint64_t) 1) << GetFlagNum(in);
	}

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
		ssize_t num = GetFlagNum(in);
		if(num >= 0) return bits.test(num);
		else return 0;
	}

	void Set(char in, bool value = true) {
		ssize_t num = GetFlagNum(in);
		if(num >= 0) bits.set(num, value);
	}

	bool Toggle(char in) {
		ssize_t num = GetFlagNum(in);
		if(num >= 0) {
			bits.flip(num);
			return bits.test(num);
		}
		else return 0;
	}

	unsigned long long ToULLong() const {
		return bits.to_ullong();
	}

	std::string GetString() const {
		return GetValueString(ToULLong());
	}

	tweet_flags & operator &=(const tweet_flags &other) {
		bits &= other.bits;
		return *this;
	}
	tweet_flags & operator |=(const tweet_flags &other) {
		bits |= other.bits;
		return *this;
	}
	tweet_flags & operator ^=(const tweet_flags &other) {
		bits ^= other.bits;
		return *this;
	}
	tweet_flags operator ~() const {
		return tweet_flags(~ToULLong());
	}
};

inline tweet_flags operator &(const tweet_flags &l, const tweet_flags &r) {
	tweet_flags result = l;
	result &= r;
	return std::move(result);
}
inline tweet_flags operator |(const tweet_flags &l, const tweet_flags &r) {
	tweet_flags result = l;
	result |= r;
	return std::move(result);
}
inline tweet_flags operator ^(const tweet_flags &l, const tweet_flags &r) {
	tweet_flags result = l;
	result ^= r;
	return std::move(result);
}

struct cached_id_sets {
	tweetidset unreadids;
	tweetidset highlightids;
	tweetidset hiddenids;
	tweetidset deletedids;

	//! Functor should have signature of void(const char *, tweetidset cached_id_sets::*, unsigned long long)
	template <typename F>
	inline static void IterateLists(F f) {
		f("unreadids", &cached_id_sets::unreadids, tweet_flags::GetFlagValue('u'));
		f("highlightids", &cached_id_sets::highlightids, tweet_flags::GetFlagValue('H'));
		f("hiddenids", &cached_id_sets::hiddenids, tweet_flags::GetFlagValue('h'));
		f("deletedids", &cached_id_sets::deletedids, tweet_flags::GetFlagValue('X'));
	}

	//! Functor should have signature of void(tweetidset &)
	template <typename F>
	inline void foreach(F f) {
		IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
			f(this->*ptr);
		});
	}

	//! Functor should have signature of void(tweetidset &, tweetidset &)
	template <typename F>
	inline void foreach(cached_id_sets &cid2, F f) {
		IterateLists([&](const char *name, tweetidset cached_id_sets::*ptr, unsigned long long tweetflag) {
			f(this->*ptr, cid2.*ptr);
		});
	}
	void CheckTweet(tweet &tw);
	void CheckTweetID(uint64_t id);
	void RemoveTweet(uint64_t id);
	std::string DumpInfo();
};

enum class UMPTF { //For UnmarkPendingTweet
	TPDB_NOUPDF        = 1<<0,
	RMV_LKPINPRGFLG    = 1<<1,  //Clear UDC::LOOKUP_IN_PROGRESS
};
template<> struct enum_traits<UMPTF> { static constexpr bool flags = true; };

enum class ARRIVAL {
	NEW               = 1<<0,
	RECV              = 1<<1,
};
template<> struct enum_traits<ARRIVAL> { static constexpr bool flags = true; };

#endif
