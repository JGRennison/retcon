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
//  2013 - Jonathan Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef INC_FLAGS_ALREADY
#define INC_FLAGS_ALREADY

#include <cstddef>
#include <type_traits>

template< typename enum_type >
struct enum_traits {
	static constexpr bool flags = false;
};

template <typename C> class flagwrapper {
	C flags;

	public:
	flagwrapper() : flags(static_cast<C>(0)) { }
	flagwrapper(C f) : flags(f) { }
	flagwrapper(std::nullptr_t n) : flags(static_cast<C>(0)) { }
	operator bool() { return flags != static_cast<C>(0); }
	operator bool() const { return flags != static_cast<C>(0); }
	operator C&() { return flags; }
	operator C() const { return flags; }
	template <typename D> operator D() = delete;
	flagwrapper &operator&=(C r) { flags &= r; return *this; }
	flagwrapper &operator^=(C r) { flags ^= r; return *this; }
	flagwrapper &operator|=(C r) { flags |= r; return *this; }
	flagwrapper &operator&=(const flagwrapper<C> &r) { flags &= r; return *this; }
	flagwrapper &operator^=(const flagwrapper<C> &r) { flags ^= r; return *this; }
	flagwrapper &operator|=(const flagwrapper<C> &r) { flags |= r; return *this; }
	flagwrapper &operator=(C r) { flags = r; return *this; }
	flagwrapper &operator=(std::nullptr_t n) { flags = static_cast<C>(0); return *this; }
	bool operator!=(C r) const { return flags != r; }
	bool operator==(C r) const { return flags == r; }
	C &getref() { return flags; }
	C get() const { return flags; }
};
template <typename C> flagwrapper<C> operator|(const flagwrapper<C> &l, C r) { return flagwrapper<C>(l.get() | r); }
template <typename C> flagwrapper<C> operator&(const flagwrapper<C> &l, C r) { return flagwrapper<C>(l.get() & r); }
template <typename C> flagwrapper<C> operator^(const flagwrapper<C> &l, C r) { return flagwrapper<C>(l.get() ^ r); }
template <typename C> flagwrapper<C> operator|(const flagwrapper<C> &l, const flagwrapper<C> &r) { return flagwrapper<C>(l.get() | r.get()); }
template <typename C> flagwrapper<C> operator&(const flagwrapper<C> &l, const flagwrapper<C> &r) { return flagwrapper<C>(l.get() & r.get()); }
template <typename C> flagwrapper<C> operator^(const flagwrapper<C> &l, const flagwrapper<C> &r) { return flagwrapper<C>(l.get() ^ r.get()); }
template <typename C> flagwrapper<C> operator~(const flagwrapper<C> &l) { return flagwrapper<C>(~(l.get())); }

template <typename C> typename std::enable_if<enum_traits<C>::flags, flagwrapper<C>>::type operator|(C l, C r) { return flagwrapper<C>(static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) | static_cast<typename std::underlying_type<C>::type >(r))); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, flagwrapper<C>>::type operator&(C l, C r) { return flagwrapper<C>(static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) & static_cast<typename std::underlying_type<C>::type >(r))); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, flagwrapper<C>>::type operator^(C l, C r) { return flagwrapper<C>(static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) ^ static_cast<typename std::underlying_type<C>::type >(r))); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, flagwrapper<C>>::type operator|(C l, const flagwrapper<C> &r) { return flagwrapper<C>(static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) | static_cast<typename std::underlying_type<C>::type >(r.get()))); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, flagwrapper<C>>::type operator&(C l, const flagwrapper<C> &r) { return flagwrapper<C>(static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) & static_cast<typename std::underlying_type<C>::type >(r.get()))); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, flagwrapper<C>>::type operator^(C l, const flagwrapper<C> &r) { return flagwrapper<C>(static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) ^ static_cast<typename std::underlying_type<C>::type >(r.get()))); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, C&>::type operator|=(C &l, C r) { l = static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) | static_cast<typename std::underlying_type<C>::type >(r)); return l; }
template <typename C> typename std::enable_if<enum_traits<C>::flags, C&>::type operator&=(C &l, C r) { l = static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) & static_cast<typename std::underlying_type<C>::type >(r)); return l; }
template <typename C> typename std::enable_if<enum_traits<C>::flags, C&>::type operator^=(C &l, C r) { l = static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) ^ static_cast<typename std::underlying_type<C>::type >(r)); return l; }
template <typename C> typename std::enable_if<enum_traits<C>::flags, C&>::type operator|=(C &l, const flagwrapper<C> &r) { l = static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) | static_cast<typename std::underlying_type<C>::type >(r.get())); return l; }
template <typename C> typename std::enable_if<enum_traits<C>::flags, C&>::type operator&=(C &l, const flagwrapper<C> &r) { l = static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) & static_cast<typename std::underlying_type<C>::type >(r.get())); return l; }
template <typename C> typename std::enable_if<enum_traits<C>::flags, C&>::type operator^=(C &l, const flagwrapper<C> &r) { l = static_cast<C>(static_cast<typename std::underlying_type<C>::type >(l) ^ static_cast<typename std::underlying_type<C>::type >(r.get())); return l; }
template <typename C> typename std::enable_if<enum_traits<C>::flags, flagwrapper<C>>::type operator~(C l) { return flagwrapper<C>(static_cast<C>(~static_cast<typename std::underlying_type<C>::type >(l))); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, bool>::type operator!(C l) { return !static_cast<typename std::underlying_type<C>::type >(l); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, bool>::type operator||(C l, C r) { return static_cast<typename std::underlying_type<C>::type >(l) || static_cast<typename std::underlying_type<C>::type >(r); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, bool>::type operator&&(C l, C r) { return static_cast<typename std::underlying_type<C>::type >(l) && static_cast<typename std::underlying_type<C>::type >(r); }

template <typename C> typename std::enable_if<enum_traits<C>::flags, flagwrapper<C>>::type flag_wrap(typename std::underlying_type<C>::type in) { return static_cast<C>(in); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, typename std::underlying_type<C>::type>::type flag_unwrap(C in) { return static_cast<typename std::underlying_type<C>::type>(in); }
template <typename C> typename std::enable_if<enum_traits<C>::flags, typename std::underlying_type<C>::type>::type flag_unwrap(flagwrapper<C> in) { return static_cast<typename std::underlying_type<C>::type>(in.get()); }

#endif

