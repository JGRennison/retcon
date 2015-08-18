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

#ifndef HGUARD_SRC_UTIL
#define HGUARD_SRC_UTIL

#include "univdefs.h"
#include "hash.h"
#include <wx/string.h>
#include <string>
#include <memory>

inline wxString wxstrstd(const std::string &st) {
	return wxString::FromUTF8(st.c_str());
}
inline wxString wxstrstd(const char *ch) {
	return wxString::FromUTF8(ch);
}
inline wxString wxstrstd(const char *ch, size_t len) {
	return wxString::FromUTF8(ch, len);
}
inline std::string stdstrwx(const wxString &st) {
	return std::string(st.ToUTF8());
}

void hexify_char(std::string &out, unsigned char c);
std::string hexify(const char *in, size_t len);
wxString hexify_wx(const char *in, size_t len);
inline std::string hexify(const std::string &in) {
	return hexify(in.data(), in.size());
}
inline wxString hexify_wx(const std::string &in) {
	return hexify_wx(in.data(), in.size());
}

// This is intended to be used instead of std::string().c_str()
// as it is too easy to accidentally do a wxString().c_str() and get silently broken results
#define cstr(str) static_cast<const char *>(cstr_wrap(str))
inline const char *cstr_wrap(const char *in) {
	return in;
}
inline const char *cstr_wrap(const std::string &in) {
	return in.c_str();
}
inline decltype(wxString().ToUTF8()) cstr_wrap(const wxString &in) {
	return in.ToUTF8();
}

//fix for MinGW, from http://pastebin.com/7rhvv92A
#ifdef __MINGW32__

#include <string>
#include <sstream>

namespace std
{
	template <typename T> string to_string(const T & value) {
		stringstream stream;
		stream << value;
		return stream.str();
	}
}

#endif

#ifdef __WINDOWS__
#define strncasecmp _strnicmp
#endif

// len can be negative to signal string is null-terminated
// returns true if whole input string is valid
template <typename C, typename D> inline bool ownstrtonum(C &val, D *str, ssize_t len) {
	if (len == 0) {
		return false;
	}
	if (str[0] == 0) {
		return false;
	}
	val = 0;
	for (ssize_t i = 0; len < 0 || i < len; i++) {
		if (str[i] >= '0' && str[i] <= '9') {
			val *= 10;
			val += str[i] - '0';
		} else if (str[i] == 0) {
			return len < 0;
		} else {
			return false;
		}
	}
	return true;
}

std::string rc_strftime(const std::string &format, const struct tm *tm, time_t timestamp = 0, bool localtime = true);
inline wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp = 0, bool localtime = true) {
	return wxstrstd(rc_strftime(stdstrwx(format), tm, timestamp, localtime));
}

std::string string_format(const std::string &fmt, ...);

inline void UnShare(wxString &str) {
	str = wxString(str.c_str(), str.size());
}

template <typename I> I swap_single_bits(I in, I bit1, I bit2) {
	bool rev = !(in & bit1) != !(in & bit2);
	if (rev) {
		return in ^ (bit1 | bit2);
	} else {
		return in;
	}
}

template <typename I> I SetOrClearBits(I in, I bits, bool set) {
	if (set) {
		return in | bits;
	} else {
		in &= ~bits;
		return in;
	}
}

template <typename I> void SetOrClearBitsRef(I &in, I bits, bool set) {
	in = SetOrClearBits(in, bits, set);
}

template <typename C, typename UP> unsigned int container_unordered_remove_if (C &container, UP predicate) {
	unsigned int removecount = 0;
	for (auto it = container.begin(); it != container.end();) {
		if (predicate(*it)) {
			removecount++;
			if (std::next(it) != container.end()) {
				*it = std::move(container.back());
				container.pop_back();
			} else {
				container.pop_back();
				break;
			}
		} else {
			++it;
		}
	}
	return removecount;
}

template <typename C, typename V> unsigned int container_unordered_remove(C &container, const V &value) {
	return container_unordered_remove_if (container, [&](const typename C::value_type &v) {
		return v == value;
	});
}

template <typename Tout, typename Tin> std::unique_ptr<Tout> static_pointer_cast(std::unique_ptr<Tin> in) {
	return std::unique_ptr<Tout>(static_cast<Tout *>(in.release()));
}

template <typename C, typename F> std::string string_join(const C &container, std::string delimiter, F appender) {
	std::string out = "";
	auto it = container.begin();
	if (it == container.end()) {
		return out;
	}
	while (true) {
		appender(out, *it);
		it++;
		if (it == container.end()) {
			break;
		}
		out += delimiter;
	}
	return out;
}

template <typename C> std::string string_join(const C &container, std::string delimiter) {
	return string_join(container, delimiter, [](std::string &out, const std::string &in) {
		out += in;
	});
}

// These are from http://stackoverflow.com/a/217605
// Author: Evan Teran

// trim from start
static inline std::string &ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
	return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
	return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
	return ltrim(rtrim(s));
}

#endif
