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

#ifndef HGUARD_SRC_RAII
#define HGUARD_SRC_RAII

#include "univdefs.h"
#include <functional>
#include <vector>

template <typename T>
class scope_exit_obj {
	T f;
	bool shouldexec;


	public:

	scope_exit_obj(T &&func)
		: f(std::move(func)), shouldexec(true) { }

	scope_exit_obj(const scope_exit_obj &copysrc) = delete;
	scope_exit_obj(scope_exit_obj &&movesrc)
		: f(std::move(movesrc.f)), shouldexec(movesrc.shouldexec) {
		movesrc.shouldexec = false;
	}

	~scope_exit_obj() {
		exec();
	}

	void exec() {
		if(shouldexec) {
			f();
			shouldexec = false;
		}
	}

	void cancel() {
		shouldexec = false;
	}
};

template <typename T>
scope_exit_obj<typename std::decay<T>::type> scope_guard(T &&func) {
	return scope_exit_obj<typename std::decay<T>::type>(std::forward<T>(func));
}

class raii_set {
	std::vector<std::function<void()> > f_set;

	public:
	raii_set() { }
	void add(std::function<void()> func) {
		f_set.emplace_back(std::move(func));
	}
	void cancel() {
		f_set.clear();
	}
	void exec() {
		for(auto f = f_set.rbegin(); f != f_set.rend(); ++f) {
			if(*f) (*f)();
		}
		f_set.clear();
	}
	~raii_set() {
		exec();
	}
};

#endif
