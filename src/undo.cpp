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
//  2015 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "univdefs.h"
#include "undo.h"

namespace undo {

	const unsigned int undo_state_size = 10;

	void item::execute() {
		for (auto &it : actions) {
			it->execute();
		}
	}

	observer_ptr<item> undo_stack::NewItem(std::string name) {
		items.push_back(undo::item(std::move(name)));
		if (items.size() > undo_state_size) {
			items.pop_front();
		}
		return &(items.back());
	}

	optional_observer_ptr<const item> undo_stack::GetTopItem() const {
		if (items.empty()) {
			return nullptr;
		} else {
			return &(items.back());
		}
	}

	void undo_stack::ExecuteTopItem() {
		if (items.empty()) {
			return;
		}

		items.back().execute();
		items.pop_back();
	}
};
