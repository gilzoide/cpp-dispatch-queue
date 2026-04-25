#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "pending_task.hpp"
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
	void process_completed_task(pending_task* task);
	std::deque<pending_task*> pop_main_loop_tasks();
	void clear();
	void shutdown();

	void wait();

	template<class Rep, class Period>
	std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		return all_done_condition_variable.wait_for(timeout_duration);
	}

	template<class Clock, class Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		return all_done_condition_variable.wait_until(timeout_time);
	}

private:
	std::mutex mutex;
	std::condition_variable task_condition_variable;
	std::condition_variable all_done_condition_variable;
	std::vector<std::thread> worker_threads;
	pending_task_queue& task_queue;
	bool is_shutting_down;

	void run_task_loop();
};

} // end namespace detail

} // end namespace dispatch_queue
