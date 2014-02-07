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
//  2014 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_MEDIA_ID_TYPE
#define HGUARD_SRC_MEDIA_ID_TYPE

#include "univdefs.h"
#include <functional>

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
	template <> struct hash<media_id_type> : public unary_function<media_id_type, size_t> {
		inline size_t operator()(const media_id_type & x) const {
			return (hash<uint64_t>()(x.m_id)<<1) ^ hash<uint64_t>()(x.t_id);
		}
	};
}

#endif
