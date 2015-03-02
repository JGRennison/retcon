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
//  2012 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_RETCON
#define HGUARD_SRC_RETCON

#include "univdefs.h"
#include "magic_ptr.h"
#include "fileutil.h"
#include <wx/app.h>
#include <wx/event.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <map>

DECLARE_EVENT_TYPE(wxextRetcon_Evt, -1)
enum {
	ID_ExecPendings      = 1,
	ID_ThreadPoolExec,
};

namespace ThreadPool {
	class Pool;
}

class retcon: public wxApp {
	virtual bool OnInit();
	virtual int OnExit();
	int FilterEvent(wxEvent &event);
	void OnQuitMsg(wxCommandEvent &event);
	void OnExecPendingsMsg(wxCommandEvent &event);
	void OnExecThreadPoolMsg(wxCommandEvent &event);

	std::vector<std::function<void()> > pendings;

	std::unique_ptr<ThreadPool::Pool> pool;
	std::map<long, std::function<void()> > pool_post_jobs;
	long next_pool_job = 0;

	public:
	std::string datadir;
	std::string tmpdir;
	unsigned int popuprecursion;
	magic_ptr_container<temp_file_holder> temp_file_set;

	void EnqueuePending(std::function<void()> &&f);
	void EnqueueThreadJob(std::function<void()> &&worker_thread_job, std::function<void()> &&main_thread_post_job);
	void EnqueueThreadJob(std::function<void()> &&worker_thread_job);

	DECLARE_EVENT_TABLE()

public:
    unsigned int terms_requested = 0;
};

DECLARE_APP(retcon)

#endif
