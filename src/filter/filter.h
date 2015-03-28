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
#include "../map.h"
#include "../tweetidset.h"
#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <map>

struct filter_item;
struct filter_undo_action;
struct tweet;
struct taccount;
struct filter_db_lazy_state;

struct filter_set {
	std::vector<std::unique_ptr<filter_item> > filters;
	std::unique_ptr<filter_undo_action> filter_undo;
	std::string filter_text;

	void FilterTweet(tweet &tw, taccount *tac = nullptr);
	void FilterTweet(filter_db_lazy_state &state, uint64_t tweet_id);
	filter_set();
	~filter_set();
	filter_set & operator=(filter_set &&other);
	filter_set(filter_set &&other);

	void clear();
	void EnableUndo();
	std::unique_ptr<undo::action> GetUndoAction();

	// This takes full and exclusive ownership of fs
	static void DBFilterTweetIDs(filter_set fs, tweetidset ids, bool enable_undo, std::function<void(std::unique_ptr<undo::action>)> completion);
};

#endif
