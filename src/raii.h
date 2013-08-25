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
//  2013 - j.g.rennison@gmail.com
//==========================================================================

#include <functional>
#include <vector>

class raii {
	std::function<void()> f;

	public:
	raii(std::function<void()> func) : f(std::move(func)) { }
	void cancel() {
		f = nullptr;
	}
	void exec() {
		if(f) f();
		f = nullptr;
	}
	~raii() {
		exec();
	}
};

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
