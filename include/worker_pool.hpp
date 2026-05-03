#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "pending_task_queue.hpp"


namespace dispatch_queue {

namespace detail {

class worker_pool {
	auto wait_predicate() const {
		return [this]{ return is_shutting_down || task_queue.empty(); };
	}
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

	void enqueue_task(pending_task&& task, bool run_on_main_loop);
	std::deque<pending_task> pop_main_loop_tasks();
	void clear();
	void shutdown();

	void wait();

	template<class Rep, class Period>
	bool wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		std::unique_lock<std::mutex> lock(mutex);
		return all_done_condition_variable.wait_for(lock, timeout_duration, wait_predicate());
	}

	template<class Clock, class Duration>
	bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		std::unique_lock<std::mutex> lock(mutex);
		return all_done_condition_variable.wait_until(lock, timeout_time, wait_predicate());
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
