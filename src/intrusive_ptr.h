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

#ifndef HGUARD_SRC_INTRUSIVE_PTR
#define HGUARD_SRC_INTRUSIVE_PTR

//The interface is loosely based on the C++14 draft of observer_ptr
//Haven't bothered with arithmetic, hashing, ordering, or arrays

//Note that it is not quite the same as boost::intrusive_ptr

#include "univdefs.h"
#include  <cstddef>
#include  <utility>
#include  <type_traits>
#include  <memory>

template <typename T>
class intrusive_ptr_common {
	using pointer         = typename std::add_pointer<T>::type;
	using reference       = typename std::add_lvalue_reference<T>::type;

	protected:
	pointer ptr;

	constexpr intrusive_ptr_common() noexcept {}
	constexpr intrusive_ptr_common(std::nullptr_t) noexcept : ptr(nullptr) { }

	public:
	pointer get() const noexcept { return ptr; }
	reference operator*() const noexcept { return *ptr; }
	pointer operator->() const noexcept { return ptr; }
	explicit operator bool() const noexcept { return ptr; }
};

template <typename T>
class intrusive_ptr : public intrusive_ptr_common<T> {
	public:
	using value_type      = T;
	using pointer         = typename std::add_pointer<T>::type;
	using const_pointer   = typename std::add_pointer<const T>::type;
	using reference       = typename std::add_lvalue_reference<T>::type;
	using const_reference = typename std::add_lvalue_reference<const T>::type;

	private:
	void initptr(pointer newptr) {
		if(newptr) newptr->intrusive_ptr_increment();
		this->ptr = newptr;
	}

	void unsetptr() {
		if(this->ptr) this->ptr->intrusive_ptr_decrement();
		this->ptr = nullptr;
	}

	public:
	constexpr intrusive_ptr() noexcept : intrusive_ptr_common<T>(nullptr) { }
	constexpr intrusive_ptr(std::nullptr_t) noexcept : intrusive_ptr_common<T>(nullptr) { }
	explicit intrusive_ptr(pointer p) : intrusive_ptr_common<T>() { initptr(p); }
	intrusive_ptr(const intrusive_ptr_common<T> &o) : intrusive_ptr_common<T>() { initptr(o.get()); }
	intrusive_ptr(const intrusive_ptr<T> &o) : intrusive_ptr_common<T>() { initptr(o.get()); }
	intrusive_ptr(intrusive_ptr<T> &&o) noexcept : intrusive_ptr_common<T>() { this->ptr = o.ptr; o.ptr = nullptr; }

	~intrusive_ptr() { unsetptr(); }

	intrusive_ptr &operator=(std::nullptr_t) { unsetptr(); return *this; }
	intrusive_ptr &operator=(pointer p) { unsetptr(); initptr(p); return *this; }
	intrusive_ptr &operator=(const intrusive_ptr_common<T> &o) { unsetptr(); initptr(o.get()); ; return *this; }
	intrusive_ptr &operator=(const intrusive_ptr<T> &o) { unsetptr(); initptr(o.get()); ; return *this; }
	intrusive_ptr &operator=(intrusive_ptr<T> &&o) noexcept { unsetptr(); this->ptr = o.ptr; o.ptr = nullptr; return *this; }

	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	intrusive_ptr(C* p) { initptr(p); }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	intrusive_ptr(const intrusive_ptr_common<C> &o) { initptr(o.get()); }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	intrusive_ptr(const intrusive_ptr<C> &o) { initptr(o.get()); }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	intrusive_ptr(intrusive_ptr<C> &&o) noexcept { this->ptr = o.ptr; o.ptr = nullptr; }

	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	intrusive_ptr &operator=(C* p) { unsetptr(); initptr(p); return *this; }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	intrusive_ptr &operator=(const intrusive_ptr_common<C> &o) { unsetptr(); initptr(o.get()); return *this; }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	intrusive_ptr &operator=(const intrusive_ptr<C> &o) { unsetptr(); initptr(o.get()); return *this; }
	template<typename C, typename = typename std::enable_if<std::is_convertible<typename std::add_pointer<C>::type, pointer>::value>::type>
	intrusive_ptr &operator=(intrusive_ptr<C> &&o) { unsetptr(); this->ptr = o.ptr; o.ptr = nullptr; return *this; }

	pointer release() {
		pointer old = this->ptr;
		unsetptr();
		return old;
	}
	void reset(pointer p = nullptr) noexcept { unsetptr(); initptr(p); }
	void swap(intrusive_ptr<T> &o) noexcept { std::swap(o.ptr, this->ptr); }
};

// This is for use wherever one might otherwise want to use const intrusive_ptr<> &
template <typename T>
class cref_intrusive_ptr : public intrusive_ptr_common<T> {
	public:
	using value_type      = T;
	using pointer         = typename std::add_pointer<T>::type;
	using const_pointer   = typename std::add_pointer<const T>::type;
	using reference       = typename std::add_lvalue_reference<T>::type;
	using const_reference = typename std::add_lvalue_reference<const T>::type;

	constexpr cref_intrusive_ptr() noexcept : intrusive_ptr_common<T>(nullptr) { }
	constexpr cref_intrusive_ptr(std::nullptr_t) noexcept : intrusive_ptr_common<T>(nullptr) { }
	explicit cref_intrusive_ptr(pointer p) : intrusive_ptr_common<T>() { this->ptr = p; }
	cref_intrusive_ptr(const intrusive_ptr_common<T> &o) noexcept : intrusive_ptr_common<T>() { this->ptr = o.get(); }

	operator intrusive_ptr<T>() const { return intrusive_ptr<T>(this->ptr); };
};

template <typename A, typename B> bool operator==(intrusive_ptr_common<A> l, intrusive_ptr_common<B> r) noexcept { return l.get() == r.get(); }
template <typename A, typename B> bool operator!=(intrusive_ptr_common<A> l, intrusive_ptr_common<B> r) noexcept { return l.get() != r.get(); }
template <typename A> bool operator==(intrusive_ptr_common<A> l, std::nullptr_t r) noexcept { return l.get() == r; }
template <typename A> bool operator!=(intrusive_ptr_common<A> l, std::nullptr_t r) noexcept { return l.get() != r; }
template <typename A> bool operator==(std::nullptr_t l, intrusive_ptr_common<A> r) noexcept { return l == r.get(); }
template <typename A> bool operator!=(std::nullptr_t l, intrusive_ptr_common<A> r) noexcept { return l != r.get(); }

namespace std {
	template <typename T>
	inline void swap(intrusive_ptr<T> &l, intrusive_ptr<T> &r) noexcept {
		l.swap(r);
	}
};

//This is the same as intrusive_ptr, intended as a documentation hint that the value is optional
template <class T> using optional_intrusive_ptr = intrusive_ptr<T>;

#endif
