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

#ifndef HGUARD_SRC_RAPIDJSON_INC
#define HGUARD_SRC_RAPIDJSON_INC

#include "univdefs.h"
#include <wx/version.h>
#include <wx/defs.h>

#if wxCHECK_GCC_VERSION(4, 6)	//in old gccs, just leave the warnings turned off
#pragma GCC diagnostic push
#endif
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wuninitialized"
#if wxCHECK_GCC_VERSION(4, 7)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#if wxCHECK_GCC_VERSION(8, 0)
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
#include "rapidjson/document.h"
#if wxCHECK_GCC_VERSION(4, 6)
#pragma GCC diagnostic pop
#endif

#endif
