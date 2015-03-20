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

#ifndef HGUARD_SRC_FILTER_FILTER
#define HGUARD_SRC_FILTER_FILTER

#include "../univdefs.h"
#include "../undo.h"
#include <functional>
#include <string>
#include <memory>
#include <vector>

struct filter_item;
struct filter_undo_action;
struct tweet;
struct taccount;

struct filter_set {
	std::vector<std::unique_ptr<filter_item> > filters;
	std::unique_ptr<filter_undo_action> filter_undo;

	void FilterTweet(tweet &tw, taccount *tac = nullptr);
	filter_set();
	~filter_set();
	filter_set & operator =(filter_set &&other);
	void clear();
	void EnableUndo();
	std::unique_ptr<undo::action> GetUndoAction();
};

#endif
