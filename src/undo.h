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

#ifndef HGUARD_SRC_UNDO
#define HGUARD_SRC_UNDO

#include "univdefs.h"
#include "observer_ptr.h"
#include "tweetidset.h"
#include <string>
#include <vector>
#include <list>
#include <memory>
#include <functional>

namespace undo {
	class undo_stack;

	struct action {
		virtual void execute() = 0;
	};

	struct generic_action : public action {
		std::function<void()> func;

		generic_action(std::function<void()> func_)
				: func(std::move(func_)) { }

		virtual void execute() override {
			func();
		}
	};

	class item {
		friend undo_stack;

		std::string name;
		std::vector<std::unique_ptr<action>> actions;

		item(std::string name_)
				: name(std::move(name_)) { }

		void execute();

		public:

		void AppendAction(std::unique_ptr<action> act) {
			if(act)
				actions.emplace_back(std::move(act));
		}

		const std::string &GetName() const {
			return name;
		}
	};

	class undo_stack {
		std::list<item> items;

		public:
		observer_ptr<item> NewItem(std::string name);
		optional_observer_ptr<const item> GetTopItem() const;
		void ExecuteTopItem();
	};

};

#endif
