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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include <vector>

struct magic_ptr_base;
struct magic_ptr;

struct magic_ptr_base {
	friend struct magic_ptr;

	virtual ~magic_ptr_base();

	protected:
	//std::set<magic_ptr*> list;
	std::vector<magic_ptr*> list;

	void Mark(magic_ptr* t);
	void Unmark(magic_ptr* t);

	public:
	unsigned int ResetAllMagicPtrs();
};

struct magic_ptr /*final*/ {
	friend struct magic_ptr_base;

	protected:
	magic_ptr_base *ptr;

	public:
	magic_ptr() : ptr(0) { }
	~magic_ptr() {
		if(ptr) ptr->Unmark(this);
	}
	void set(magic_ptr_base *t) {
		if(ptr) ptr->Unmark(this);
		ptr=t;
		if(t) t->Mark(this);
	}
	magic_ptr_base *get() const {
		return ptr;
	}
	magic_ptr_base *operator->() const {
		return ptr;
	}
	magic_ptr_base &operator*() const {
		return *ptr;
	}
	magic_ptr(const magic_ptr &p) {
		ptr=p.get();
		if(ptr) ptr->Mark(this);
	}
	magic_ptr(magic_ptr_base *t) {
		ptr=t;
		if(ptr) ptr->Mark(this);
	}
	magic_ptr & operator=(const magic_ptr &p) {
		set(p.ptr);
		return *this;
	}
	magic_ptr & operator=(magic_ptr_base *t) {
		set(t);
		return *this;
	}
	operator bool() const {
		return ptr;
	}
};

inline unsigned int magic_ptr_base::ResetAllMagicPtrs() {
	for(auto &it : list) {
		it->ptr=0;
	}
	unsigned int count = list.size();
	list.clear();
	return count;
}

inline magic_ptr_base::~magic_ptr_base() {
	ResetAllMagicPtrs();
}

inline void magic_ptr_base::Mark(magic_ptr* t) {
	//list.insert(t);
	for(size_t i=0; i<list.size(); i++) {
		if(list[i]==t) return;
	}
	list.push_back(t);
}

inline void magic_ptr_base::Unmark(magic_ptr* t) {
	//list.erase(t);
	for(size_t i=0; i<list.size(); i++) {
		if(list[i]==t) {
			if(i+1<list.size()) {
				list[i]=list[list.size()-1];
			}
			list.pop_back();
			break;
		}
	}
}

template <typename C> C *MagicWindowCast(magic_ptr &in) {
	return dynamic_cast<C*>(in.get());
}

template <typename C> struct magic_ptr_ts {
	private:
	magic_ptr ptr;

	public:
	magic_ptr_ts() : ptr() { }
	void set(C *t) {
		ptr.set(t);
	}
	C *get() const {
		return (C*) ptr.get();
	}
	C *operator->() const {
		return get();
	}
	C &operator*() const {
		return *get();
	}
	magic_ptr_ts(const magic_ptr_ts<C> &p) : ptr(p.get()) { }
	magic_ptr_ts(C *t) : ptr(t) { }
	magic_ptr_ts & operator=(const magic_ptr_ts<C> &p) {
		set(p.get());
		return *this;
	}
	magic_ptr_ts & operator=(C *in) {
		set(in);
		return *this;
	}
	operator bool() const {
		return (bool) ptr;
	}
};

template <typename C, typename D> struct magic_paired_ptr_ts {
	friend magic_paired_ptr_ts<D, C>;
	private:
	C *other = 0;

	public:
	virtual void OnMagicPairedPtrChange(C *targ, C *prevtarg, bool targdestructing) { }

	private:
	void halfset(C* targ, bool updateprevtarg, bool targdestructing = false) {
		if(other && updateprevtarg) static_cast<magic_paired_ptr_ts<D, C> *>(other)->halfset(0, false);
		C* prev = other;
		other = targ;
		OnMagicPairedPtrChange(targ, prev, targdestructing);
	}

	public:
	virtual ~magic_paired_ptr_ts() {
		if(other) static_cast<magic_paired_ptr_ts<D, C> *>(other)->halfset(0, false, true);
	}
	C *get() const {
		return other;
	}
	void set(C *targ, bool triggerlocalchange = false) {
		if(other != targ) {
			if(other) static_cast<magic_paired_ptr_ts<D, C> *>(other)->halfset(0, false);
			C * prev = other;
			other = targ;
			if(triggerlocalchange) OnMagicPairedPtrChange(other, prev, false);
			if(other) static_cast<magic_paired_ptr_ts<D, C> *>(other)->halfset(static_cast<D*>(this), true);
		}
	}
	C *operator->() const {
		return get();
	}
	C &operator*() const {
		return *get();
	}
	operator bool() const {
		return (bool) other;
	}
};

