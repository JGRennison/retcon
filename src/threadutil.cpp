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

#include <deque>

#include "univdefs.h"
#include "threadutil.h"
#include "util.h"
#include "log.h"

#ifdef _GNU_SOURCE
#include <pthread.h>
#endif

namespace ThreadPool {
	void Pool::enqueue(std::unique_ptr<Job> j, bool queue_jump) {
		if(max_threads == 0) {
			//Thread-pool effectively disabled, execute stuff synchronously with a dummy Worker
			Worker w(this);
			(*j)(w);
			return;
		}

		std::unique_lock<std::mutex> lock(lifeguard);

		if(queue_jump) job_queue.emplace_front(std::move(j));
		else job_queue.emplace_back(std::move(j));

		if(waitingcount) {
			lock.unlock();
			queue_cv.notify_one();
		}
		else if(threadcount < max_threads) {
			unsigned int new_thread_id = next_thread_id;
			next_thread_id++;
			threadcount++;
			lock.unlock();
			workers.emplace_back(new Worker(this, new_thread_id));
			LogMsgFormat(LOGT::THREADTRACE, wxT("ThreadPool::Pool::enqueue Created thread pool worker: %d"), new_thread_id);
		}
		else lock.unlock();
	}

	Pool::~Pool() {
		std::unique_lock<std::mutex> lock(lifeguard);
		LogMsgFormat(LOGT::THREADTRACE, wxT("ThreadPool::Pool::~Pool: Waiting for %d threads to terminate"), threadcount);
		for(size_t i = 0; i < threadcount; i++) {
			job_queue.emplace_back(std::unique_ptr<Job>(new Job([](Worker &w) {
				w.alive = false;
			})));
		}
		lock.unlock();
		queue_cv.notify_all();

		for(auto &it : workers) {
			it->thread.join();
		}
		workers.clear();
		LogMsg(LOGT::THREADTRACE, wxT("ThreadPool::Pool::~Pool: Threads terminated"));
	}

	Worker::Worker(Pool *parent_, unsigned int id_) : parent(parent_), id(id_) {
		thread = std::thread([this] {

#if defined(_GNU_SOURCE)
#if __GLIBC_PREREQ(2, 12)
			pthread_setname_np(thread.native_handle(), string_format("retcon-pw-%d", id).c_str());
#endif
#endif
			std::unique_lock<std::mutex> lock(parent->lifeguard);

			while(alive) {
				parent->waitingcount++;
				parent->queue_cv.wait(lock, [this]() {
					return !parent->job_queue.empty();
				});
				parent->waitingcount--;
				//At this point the job queue is guaranteed to be non-empty

				auto job = std::move(parent->job_queue.front());
				parent->job_queue.pop_front();
				lock.unlock();
				(*job)(*this);
				lock.lock();
			}
		});
	}
}
