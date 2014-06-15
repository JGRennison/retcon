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

#ifndef HGUARD_SRC_OBSERVER_PTR
#define HGUARD_SRC_OBSERVER_PTR

//This is loosely based on the C++14 draft
//Haven't bothered with arithmetic, hashing, ordering, or arrays

#include "univdefs.h"
#include  <cstddef>
#include  <utility>
#include  <type_traits>
#include  <memory>

template <typename T>
class observer_ptr {

	using value_type      = T;
	using pointer         = typename std::add_pointer<T>::type;
	using const_pointer   = typename std::add_pointer<const T>::type;
	using reference       = typename std::add_lvalue_reference<T>::type;
	using const_reference = typename std::add_lvalue_reference<const T>::type;

	private:
	pointer ptr;

	public:
	constexpr observer_ptr() noexcept : ptr(nullptr) { }
	constexpr observer_ptr(std::nullptr_t) noexcept : ptr(nullptr) { }
	explicit constexpr observer_ptr(pointer p) noexcept : ptr(p) { }
	constexpr observer_ptr(const observer_ptr<T> &o) noexcept : ptr(o.ptr) { }
	observer_ptr &operator=(std::nullptr_t) noexcept { ptr = nullptr; return *this; }
	observer_ptr &operator=(pointer p) noexcept { ptr = p; return *this; }
	observer_ptr &operator=(const observer_ptr<T> &o) noexcept { ptr = o.ptr; return *this; }

	explicit constexpr observer_ptr(const std::unique_ptr<T> &p) noexcept : ptr(p.get()) { }
	explicit constexpr observer_ptr(const std::shared_ptr<T> &p) noexcept : ptr(p.get()) { }

	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	observer_ptr(C* p) noexcept : ptr(p) { }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	observer_ptr(observer_ptr<C> o) noexcept : ptr(o.ptr) { }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	observer_ptr &operator=(C* p) noexcept { ptr = p; return *this; }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	observer_ptr &operator=(observer_ptr<C> o) noexcept { ptr = o.ptr; return *this; }

	pointer get() const noexcept { return ptr; }
	reference operator*() const noexcept { return *ptr; }
	pointer operator->() const noexcept { return ptr; }
	explicit operator bool() const noexcept { return ptr; }
	pointer release() noexcept {
		pointer old = ptr;
		ptr = nullptr;
		return old;
	}
	void reset(pointer p = nullptr) noexcept { ptr = p; }
	void swap(observer_ptr<T> o) noexcept { std::swap(o.ptr, ptr); }
};

template <typename T> observer_ptr<T> make_observer(T *input) {
	return observer_ptr<T> {input};
}

template <typename T> observer_ptr<T> make_observer(const observer_ptr<T> &input) {
	return observer_ptr<T> {input};
}

template <typename T> observer_ptr<T> make_observer(const std::unique_ptr<T> &input) {
	return observer_ptr<T> {input};
}

template <typename T> observer_ptr<T> make_observer(const std::shared_ptr<T> &input) {
	return observer_ptr<T> {input};
}

template <typename A, typename B> bool operator==(observer_ptr<A> l, observer_ptr<B> r) noexcept { return l.get() == r.get(); }
template <typename A, typename B> bool operator!=(observer_ptr<A> l, observer_ptr<B> r) noexcept { return l.get() != r.get(); }
template <typename A> bool operator==(observer_ptr<A> l, std::nullptr_t r) noexcept { return l.get() == r; }
template <typename A> bool operator!=(observer_ptr<A> l, std::nullptr_t r) noexcept { return l.get() != r; }
template <typename A> bool operator==(std::nullptr_t l, observer_ptr<A> r) noexcept { return l == r.get(); }
template <typename A> bool operator!=(std::nullptr_t l, observer_ptr<A> r) noexcept { return l != r.get(); }

namespace std {
	template <typename T>
	inline void swap(observer_ptr<T> &l, observer_ptr<T> &r) noexcept {
		l.swap(r);
	}
};

//This is the same as observer_ptr, intended as a documentation hint that the value is optional
template <class T> using optional_observer_ptr = observer_ptr<T>;

#endif
