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

#ifndef HGUARD_SRC_UNIVDEFS
#define HGUARD_SRC_UNIVDEFS

#define wxUSE_UNICODE 1
#define _UNICODE 1
#define UNICODE 1

#include <cstddef>
#include <cstdint>

// This is based on <wx/defs.h> but without the wxT()s, and with legacy stuff removed
#if (defined(__VISUALC__) && defined(__WIN32__)) || defined(__MINGW32__)
    #define llFmtSpec "I64"
#elif (defined(SIZEOF_LONG_LONG) && SIZEOF_LONG_LONG >= 8)  || \
        defined(__GNUC__) || \
        defined(__CYGWIN__) || \
        defined(__WXMICROWIN__) || \
        (defined(__DJGPP__) && __DJGPP__ >= 2)
    #define llFmtSpec "ll"
#elif defined(SIZEOF_LONG) && (SIZEOF_LONG == 8)
    #define llFmtSpec "l"
#endif

#endif
