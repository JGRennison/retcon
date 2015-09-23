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

#ifndef HGUARD_SRC_SAFE_OBSERVER_PTR
#define HGUARD_SRC_SAFE_OBSERVER_PTR

#include "univdefs.h"
#include "util.h"
#include "observer_ptr.h"
#include <vector>
#include <type_traits>
#include <boost/iterator/iterator_adaptor.hpp>

struct safe_observer_untyped_ptr;
template<typename T> struct safe_observer_ptr_container;

template<typename T> struct safe_observer_ptr_target_generic {
	friend T;

	private:
	std::vector<T*> list;

	void Mark(T* t) {
		list.push_back(t);
	}

	void Unmark(T* t) {
		container_unordered_remove(list, t);
	}

	public:
	void ResetAllPtrs() {
		for (auto &it : list) {
			it->PtrBeingDeleted(this);
		}
		list.clear();
	}

	virtual ~safe_observer_ptr_target_generic() {
		ResetAllPtrs();
	}
};

using safe_observer_ptr_target = safe_observer_ptr_target_generic<safe_observer_untyped_ptr>;

struct safe_observer_untyped_ptr {
	friend safe_observer_ptr_target;

	private:
	safe_observer_ptr_target *ptr;

	void PtrBeingDeleted(safe_observer_ptr_target *t) {
		if (ptr == t) {
			ptr = nullptr;
		}
	}

	public:
	safe_observer_untyped_ptr() : ptr(nullptr) { }

	~safe_observer_untyped_ptr() {
		if (ptr) {
			ptr->Unmark(this);
		}
	}

	void set(safe_observer_ptr_target *t) {
		if (ptr) {
			ptr->Unmark(this);
		}
		ptr = t;
		if (t) {
			t->Mark(this);
		}
	}

	safe_observer_ptr_target *get() const {
		return ptr;
	}

	safe_observer_ptr_target *operator->() const {
		return ptr;
	}

	safe_observer_ptr_target &operator*() const {
		return *ptr;
	}

	safe_observer_untyped_ptr(const safe_observer_untyped_ptr &p) {
		ptr = p.get();
		if (ptr) {
			ptr->Mark(this);
		}
	}

	safe_observer_untyped_ptr(safe_observer_ptr_target *t) {
		ptr = t;
		if (ptr) {
			ptr->Mark(this);
		}
	}

	safe_observer_untyped_ptr & operator=(const safe_observer_untyped_ptr &p) {
		set(p.ptr);
		return *this;
	}

	safe_observer_untyped_ptr & operator=(safe_observer_ptr_target *t) {
		set(t);
		return *this;
	}

	operator bool() const {
		return ptr;
	}
};

template <typename C> struct safe_observer_ptr {
	private:
	safe_observer_untyped_ptr ptr;

	public:
	safe_observer_ptr() : ptr() { }

	void set(C *t) {
		ptr.set(t);
	}

	C *get() const {
		return static_cast<C*>(ptr.get());
	}

	C *operator->() const {
		return get();
	}

	C &operator*() const {
		return *get();
	}

	safe_observer_ptr(const safe_observer_ptr<C> &p) : ptr(p.get()) { }

	safe_observer_ptr(C *t) : ptr(t) { }

	safe_observer_ptr & operator=(const safe_observer_ptr<C> &p) {
		set(p.get());
		return *this;
	}

	safe_observer_ptr & operator=(C *in) {
		set(in);
		return *this;
	}

	operator bool() const {
		return (bool) ptr;
	}
};

template<typename T> using safe_observer_ptr_contained = safe_observer_ptr_target_generic<safe_observer_ptr_container<T>>;

template<typename T> struct safe_observer_ptr_container {
	friend safe_observer_ptr_contained<T>;

	static_assert(std::is_base_of<safe_observer_ptr_contained<T>, T>::value, "T not derived from safe_observer_ptr_contained<T>");

	private:
	std::vector<safe_observer_ptr_contained<T> *> list;

	void PtrBeingDeleted(safe_observer_ptr_contained<T> *t) {
		container_unordered_remove(list, t);
	}

	public:
	void insert(T *t) {
		safe_observer_ptr_contained<T> *t_contained = t;
		t_contained->Mark(this);
		list.push_back(t);
	}

	void clear() {
		for (auto &it : list) {
			it->Unmark(this);
		}
		list.clear();
	}

	~safe_observer_ptr_container() {
		clear();
	}

	class const_iterator
			: public boost::iterator_adaptor<const_iterator,
				typename std::vector<safe_observer_ptr_contained<T> *>::iterator,
				const observer_ptr<T>,
				boost::use_default,
				const observer_ptr<T>
			> {

		public:
		const_iterator()
				: const_iterator::iterator_adaptor_() {}

		explicit const_iterator(const typename const_iterator::iterator_adaptor_::base_type& p)
				: const_iterator::iterator_adaptor_(p) {}

		private:
		friend class boost::iterator_core_access;
		const observer_ptr<T> dereference() const {
			return static_cast<T*>(*(this->base()));
		}
	};

	const_iterator begin() {
		return const_iterator(list.begin());
	}

	const_iterator end() {
		return const_iterator(list.end());
	}

	bool empty() const {
		return list.empty();
	}

	size_t size() const {
		return list.size();
	}
};

template <typename C, typename D> struct safe_paired_observer_ptr {
	friend safe_paired_observer_ptr<D, C>;
	private:
	C *other = nullptr;

	public:
	virtual void OnPairedPtrChange(C *targ, C *prevtarg, bool targdestructing) { }

	private:
	void half_set_ptr_pair(C* targ, bool updateprevtarg, bool targdestructing = false) {
		if (other && updateprevtarg) {
			static_cast<safe_paired_observer_ptr<D, C> *>(other)->half_set_ptr_pair(nullptr, false);
		}
		C* prev = other;
		other = targ;
		OnPairedPtrChange(targ, prev, targdestructing);
	}

	public:
	virtual ~safe_paired_observer_ptr() {
		if (other) {
			static_cast<safe_paired_observer_ptr<D, C> *>(other)->half_set_ptr_pair(nullptr, false, true);
		}
	}

	C *get_paired_ptr() const {
		return other;
	}

	void set_paired_ptr(C *targ, bool triggerlocalchange = false) {
		if (other != targ) {
			if (other) static_cast<safe_paired_observer_ptr<D, C> *>(other)->half_set_ptr_pair(nullptr, false);
			C * prev = other;
			other = targ;
			if (triggerlocalchange) {
				OnPairedPtrChange(other, prev, false);
			}
			if (other) {
				static_cast<safe_paired_observer_ptr<D, C> *>(other)->half_set_ptr_pair(static_cast<D*>(this), true);
			}
		}
	}
};

#endif
