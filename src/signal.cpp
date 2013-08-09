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

#include "retcon.h"

#ifdef __UNIX_LIKE__
	#include <signal.h>

	static void termsighandler(int signum, siginfo_t *info, void *ucontext) {
		wxGetApp().term_requested = true;
		for(auto &it : mainframelist) {
			wxCommandEvent evt(wxEVT_COMMAND_MENU_SELECTED, ID_Quit);
			it->AddPendingEvent(evt);
		}
	}

	void SetTermSigHandler() {
		auto setsig = [&](int signum) {
			struct sigaction sa;
			memset(&sa, 0, sizeof(sa));
			sa.sa_sigaction=&termsighandler;
			sa.sa_flags=SA_SIGINFO|SA_RESTART;
			sigfillset(&sa.sa_mask);
			sigaction(signum, &sa, 0);
		};
		setsig(SIGHUP);
		setsig(SIGINT);
		setsig(SIGTERM);
	}

#else
	void SetTermSigHandler() { }
#endif
