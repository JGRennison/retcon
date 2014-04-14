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

#ifndef HGUARD_SRC_HASH
#define HGUARD_SRC_HASH

#include "univdefs.h"
#include <memory>

struct sha1_hash_block {
	unsigned char hash_sha1[20];
};

//This is deliberately a pointer to immutable data, such that it can be freely passed around to other threads
typedef std::shared_ptr<const sha1_hash_block> shb_iptr;

shb_iptr hash_block(const void *data, size_t length);

#endif
