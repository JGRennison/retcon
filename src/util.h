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

class wxImage;

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
std::string hexify(const std::string &in);
wxString hexify_wx(const std::string &in);

bool LoadImageFromFileAndCheckHash(const wxString &filename, shb_iptr hash, wxImage &img);
bool LoadFromFileAndCheckHash(const wxString &filename, shb_iptr hash, char *&data, size_t &size);

//fix for MinGW, from http://pastebin.com/7rhvv92A
#ifdef __MINGW32__

#include <string>
#include <sstream>

namespace std
{
    template <typename T>
    string to_string(const T & value)
    {
        stringstream stream;
        stream << value;
        return stream.str();
    }
}

#endif

#ifdef __WINDOWS__
#define strncasecmp _strnicmp
#endif

template <typename C, typename D> inline void ownstrtonum(C &val, D *str, ssize_t len) {
	val = 0;
	for(ssize_t i = 0; len < 0 || i < len; i++) {
		if(str[i] >= '0' && str[i] <= '9') {
			val *= 10;
			val += str[i] - '0';
		}
		else break;
	}
}

wxString rc_wx_strftime(const wxString &format, const struct tm *tm, time_t timestamp = 0, bool localtime = true);

std::string string_format(const std::string &fmt, ...);

inline void UnShare(wxString &str) {
	str = wxString(str.c_str(), str.size());
}

template <typename I> I swap_single_bits(I in, I bit1, I bit2) {
	bool rev = !(in & bit1) != !(in & bit2);
	if(rev) return in ^ (bit1 | bit2);
	else return in;
}

template <typename I> I SetOrClearBits(I in, I bits, bool set) {
	if(set) return in | bits;
	else in &= ~bits;
	return in;
}

template <typename I> void SetOrClearBitsRef(I &in, I bits, bool set) {
	in = SetOrClearBits(in, bits, set);
}

#endif
