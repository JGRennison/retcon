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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_FILTER_FILTER
#define HGUARD_SRC_FILTER_FILTER

#include "../univdefs.h"
#include <functional>
#include <string>
#include <memory>
#include <list>

struct filter_item;
struct tweet;

struct filter_set {
	std::list<std::unique_ptr<filter_item> > filters;

	void FilterTweet(tweet &tw, taccount *tac = 0);
	filter_set();
	~filter_set();
	filter_set & operator =(filter_set &&other);
};

#endif
