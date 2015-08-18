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
//  2013 - Jonathan G Rennison <j.g.rennison@gmail.com>
//==========================================================================

#ifndef HGUARD_SRC_THREADUTIL
#define HGUARD_SRC_THREADUTIL

#include "univdefs.h"
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>

namespace ThreadPool {

	class Pool;
	class Worker;
	typedef std::function<void(Worker &)> Job;

	class Pool {
		friend class Worker;
		size_t max_threads;

		std::condition_variable queue_cv;

		std::mutex lifeguard;
		//Start: protected by lock
		std::deque<std::unique_ptr<Job> > job_queue;   //jobs are popped from the front of the queue
		std::deque<std::unique_ptr<Worker> > workers;
		size_t waitingcount = 0;
		//End: protected by lock
		size_t threadcount = 0;
		unsigned int next_thread_id = 0;

		public:
		Pool(size_t max_threads_) : max_threads(max_threads_) { }
		~Pool();
		void enqueue(std::unique_ptr<Job> j, bool queue_jump = false);

		void enqueue(Job &&j, bool queue_jump = false) {
			enqueue(std::unique_ptr<Job>(new Job(std::move(j))), queue_jump);
		}

		size_t GetThreadLimit() const { return max_threads; }

		//un-copyable, un-movable
		Pool(const Pool &) = delete;
		Pool& operator=(const Pool&) = delete;
		Pool(Pool &&) = delete;
		Pool& operator=(Pool &&) = delete;
	};

	//Pools must outlive Workers
	class Worker {
		friend class Pool;
		Pool * const parent; //*parent should be locked with its mutex before use
		const unsigned int id = 0;
		bool alive = true;  //this should only be touched by the spawned thread

		std::thread thread; //this should only be touched by the main thread

		Worker(Pool *parent_, unsigned int id_);

		private:
		//empty Worker
		Worker(Pool *parent_) : parent(parent_) { }

		//un-copyable, un-movable
		Worker(const Worker &) = delete;
		Worker& operator=(const Worker&) = delete;
		Worker(Worker &&) = delete;
		Worker& operator=(Worker &&) = delete;
	};

};

#endif
