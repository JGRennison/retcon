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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

//UTF-8 functions all assume perfectly valid UTF-8 for the most part

#ifndef HGUARD_SRC_UTF8
#define HGUARD_SRC_UTF8

#include "univdefs.h"

inline int getcharfromstr_utf8(const char *str) {
	char c1 = str[0];
	if (!(c1 & 0x80)) {
		return c1;
	} else if ((c1 & 0xE0) == 0xC0) {
		return ((c1 & 0x1F) << 6) + ((str[1] & 0x3F));
	} else if ((c1 & 0xF0) == 0xE0) {
		return ((c1 & 0x0F) << 12) + ((str[1] & 0x3F) << 6) + ((str[2] & 0x3F));
	} else if ((c1 & 0xF8) == 0xF0) {
		return ((c1 & 0x07) << 18) + ((str[1] & 0x3F) << 12) + ((str[2] & 0x3F) << 6) + ((str[3] & 0x3F));
	} else {
		return 0xFFFD;
	}
}

inline int getcharfromstr_utf8_ret(const char **str) {
	char c1 = (*str)[0];
	if (!(c1 & 0x80)) {
		(*str)++;
		return c1;
	} else if ((c1 & 0xE0) == 0xC0) {
		int r = ((c1 & 0x1F) << 6) + (((*str)[1] & 0x3F));
		(*str) += 2;
		return r;
	} else if ((c1 & 0xF0) == 0xE0) {
		int r = ((c1 & 0x0F) << 12) + (((*str)[1] & 0x3F) << 6) + (((*str)[2] & 0x3F));
		(*str) += 3;
		return r;
	} else if ((c1 & 0xF8) == 0xF0) {
		int r = ((c1 & 0x07) << 18) + (((*str)[1] & 0x3F) << 12) + (((*str)[2] & 0x3F) << 6) + (((*str)[3] & 0x3F));
		(*str) += 4;
		return r;
	} else {
		(*str)++;
		return 0xFFFD;
	}
}

inline int getcharfromstr_utf8_ret(char **str) {
	return getcharfromstr_utf8_ret(const_cast<const char **>(str));
}

inline int getcharfromstr_offset_utf8(const char *str, size_t offset, size_t bytelength) {
	for (size_t i = 0; i < bytelength; i++) {
		if ((str[i] & 0xC0) == 0x80) {
			continue;
		}
		if (!offset) {
			return getcharfromstr_utf8(&str[i]);    //this ignores the issue of a UTF-8 sequence running off the end of the string
		} else {
			offset--;
		}
	}
	return -1;    //offset is beyond end of string
}

inline const char *getstrfromstr_offset_utf8(const char *str, size_t offset, size_t bytelength) {
	for (size_t i = 0; i < bytelength; i++) {
		if ((str[i] & 0xC0) == 0x80) {
			continue;
		}
		if (!offset) {
			return str + i;          //this ignores the issue of a UTF-8 sequence running off the end of the string
		} else {
			offset--;
		}
	}
	if (offset == 0) {
		return str + bytelength;     //end of string
	}
	return nullptr;                  //offset is beyond end of string
}

inline char *getstrfromstr_offset_utf8(char *str, size_t offset, size_t bytelength) {
	return const_cast<char *>(getstrfromstr_offset_utf8(const_cast<const char *>(str), offset, bytelength));
}

inline size_t get_utf8_truncate_offset(const char *str, size_t max_chars, size_t bytelength) {
	const char *endptr = getstrfromstr_offset_utf8(str, max_chars, bytelength);
	if (endptr) {
		return endptr - str;
	} else {
		return bytelength;
	}
}

inline int utf8firsttonumbytes(char c1) {
	if (!(c1 & 0x80)) {
		return 1;
	} else if ((c1 & 0xE0) == 0xC0) {
		return 2;
	} else if ((c1 & 0xF0) == 0xE0) {
		return 3;
	} else if ((c1 & 0xF8) == 0xF0) {
		return 4;
	} else {
		return 0;
	}
}

inline int chartonumutf8bytes(int c) {
	if (c < 0) {
		return 0;
	} else if (c < 0x80) {
		return 1;
	} else if (c < 0x800) {
		return 2;
	} else if (c < 0x10000) {
		return 3;
	} else if (c < 0x200000) {
		return 4;
	} else {
		return 0;
	}
}

inline void setstrfromchar_utf8(int c, char *str) {
	if (c < 0) {
		// do nothing
	} else if (c < 0x80) {
		str[0] = c;
	} else if (c < 0x800) {
		str[0] = 0xC0 | (c >> 6);
		str[1] = 0x80 | (c & 0x3F);
	} else if (c < 0x10000) {
		str[0] = 0xE0 | (c >> 12);
		str[1] = 0x80 | ((c >> 6) & 0x3F);
		str[2] = 0x80 | (c & 0x3F);
	} else if (c < 0x200000) {
		str[0] = 0xF0 | (c >> 18);
		str[1] = 0x80 | ((c >> 12) & 0x3F);
		str[2] = 0x80 | ((c >> 6) & 0x3F);
		str[3] = 0x80 | (c & 0x3F);
	}
}

inline size_t strlen_utf8(const char *str) {
	size_t len = 0;
	while (*str) {
		int add = utf8firsttonumbytes(*str);
		len++;
		if (add) {
			str += add;
		} else {
			break;
		}
	}
	return len;
}

inline size_t strlen_utf8(const char *str, size_t bytes) {
	size_t len = 0;
	const char *end = str + bytes;
	while (str < end && *str) {
		int add = utf8firsttonumbytes(*str);
		len++;
		if (add) {
			str += add;
		} else {
			break;
		}
	}
	return len;
}

#endif
