//  retcon
//
//  WEBSITE: http://retcon.sourceforge.net
//
//  NOTE: This software is licensed under the GPL. See: COPYING-GPL.txt
//
//  This program  is distributed in the  hope that it will  be useful, but
//  WITHOUT   ANY  WARRANTY;   without  even   the  implied   warranty  of
//  MERCHANTABILITY  or FITNESS  FOR A  PARTICULAR PURPOSE.   See  the GNU
//  General Public License for more details.
//
//  Jonathan Rennison (or anybody else) is in no way responsible, or liable
//  for this program or its use in relation to users, 3rd parties or to any
//  persons in any way whatsoever.
//
//  You  should have  received a  copy of  the GNU  General Public
//  License along  with this program; if  not, write to  the Free Software
//  Foundation, Inc.,  59 Temple Place,  Suite 330, Boston,  MA 02111-1307
//  USA
//
//  2012 - j.g.rennison@gmail.com
//==========================================================================

#include <set>

struct magic_ptr_base;
struct magic_ptr;

struct magic_ptr_base {
	friend struct magic_ptr;

	virtual ~magic_ptr_base();

	protected:

	std::set<magic_ptr*> list;

	void Mark(magic_ptr* t);
	void Unmark(magic_ptr* t);
};

struct magic_ptr {
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
	magic_ptr_base *get() {
		return ptr;
	}
	magic_ptr(magic_ptr &p) {
		set(p.ptr);
	}
	magic_ptr & operator=(const magic_ptr &p) {
		set(p.ptr);
		return *this;
	}
	magic_ptr & operator=(magic_ptr_base *t) {
		set(t);
		return *this;
	}
};

inline magic_ptr_base::~magic_ptr_base() {
	for(auto it=list.begin(); it!=list.end(); ++it) {
		(*it)->ptr=0;
	}
}

inline void magic_ptr_base::Mark(magic_ptr* t) {
	list.insert(t);
}

inline void magic_ptr_base::Unmark(magic_ptr* t) {
	list.erase(t);
}

template <typename C> C *MagicWindowCast(magic_ptr &in) {
	return dynamic_cast<C*>(in.get());
}