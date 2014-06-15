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

#ifndef HGUARD_SRC_PRIMARYCLIPBOARD
#define HGUARD_SRC_PRIMARYCLIPBOARD

#include "univdefs.h"
#include <wx/defs.h>

// This is effectively a backport of http://trac.wxwidgets.org/changeset/70011#
#if (! wxCHECK_VERSION(2, 9, 3)) && (defined (__WXGTK__) || defined(__WXX11__) || defined(__WXMOTIF__))
#define HANDLE_PRIMARY_CLIPBOARD 1
#else
#define HANDLE_PRIMARY_CLIPBOARD 0
#endif

#endif
