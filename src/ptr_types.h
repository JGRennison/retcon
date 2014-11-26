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

#ifndef HGUARD_SRC_PTR_TYPES
#define HGUARD_SRC_PTR_TYPES

#include "univdefs.h"
#include "observer_ptr.h"
#include "intrusive_ptr.h"

struct userdatacontainer;
struct tweet;

//This is to hopefully make it as painless as possible to change the pointer types later
//The _p variant is for wherever a shared_ptr might have been passed using const shared_ptr &, ie. parameters

typedef intrusive_ptr<userdatacontainer> udc_ptr;
typedef cref_intrusive_ptr<userdatacontainer> udc_ptr_p;

typedef intrusive_ptr<tweet> tweet_ptr;
typedef cref_intrusive_ptr<tweet> tweet_ptr_p;

#endif
