#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "pending_task_queue.hpp"


namespace dispatch_queue {

namespace detail {

class worker_pool {
public:
	template<typename Fn>
	worker_pool(pending_task_queue& task_queue, int thread_count, Fn&& worker_init)
		: task_queue(task_queue)
	{
		worker_threads.reserve(thread_count);
		for (int i = 0; i < thread_count; i++) {
			worker_threads.emplace_back([&, this, i]() {
				worker_init(i);
				run_task_loop();
			});
		}
	}
	~worker_pool();

	worker_pool(const worker_pool&) = delete;
	worker_pool& operator=(const worker_pool&) = delete;

	int thread_count() const;
	size_t size();

	void enqueue_task(pending_task&& task, task_id dependency = 0);
	void clear();
	void shutdown();

private:
	std::mutex mutex;
	std::condition_variable condition_variable;
	std::vector<std::thread> worker_threads;
	pending_task_queue& task_queue;
	bool is_shutting_down;

	void run_task_loop();
};

} // end namespace detail

} // end namespace dispatch_queue
