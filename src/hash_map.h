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
//  2017 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_HASH_MAP
#define HGUARD_SRC_HASH_MAP

#include "univdefs.h"

#ifdef RETCON_STD_STL
#include <unordered_map>
#else

#include <wx/version.h>
#include <wx/defs.h>

#if wxCHECK_GCC_VERSION(4, 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
#if wxCHECK_GCC_VERSION(8, 0)
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#endif

#include <sparsepp/spp.h>

#if wxCHECK_GCC_VERSION(4, 6)
#pragma GCC diagnostic pop
#endif
#endif

namespace container {

template<
	class Key,
	class T
> using hash_map =

#ifdef RETCON_STD_STL
std::unordered_map<Key, T>;
#else
spp::sparse_hash_map<Key, T>;
#endif

};

#endif
