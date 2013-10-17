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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#include "retcon.h"

#ifdef __UNIX_LIKE__
	#include <signal.h>

	static void termsighandler(int signum, siginfo_t *info, void *ucontext) {
		retcon &rt = wxGetApp();
		if(rt.term_requested) {
			//termination already requested, be more forceful this time
			rt.ExitMainLoop();
		}
		rt.term_requested = true;
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
