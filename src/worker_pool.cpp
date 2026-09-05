#include "../include/detail/worker_pool.hpp"

#include <cassert>

namespace dispatch_queue {

namespace detail {

worker_pool::~worker_pool() {
	shutdown();
}

int worker_pool::thread_count() const {
	return worker_thread_count;
}

size_t worker_pool::size() const {
	std::lock_guard<std::mutex> lock(mutex);
	return task_queue.size();
}

void worker_pool::enqueue_task(task_type type, task_function&& task, task_tag tag) {
	bool should_wake_thread;
	{
		std::lock_guard<std::mutex> lock(mutex);
		bool has_new_background_task = task_queue.push(type, std::move(task), tag);
		should_wake_thread = has_new_background_task && idle_threads;
	}
	if (should_wake_thread) {
		task_condition_variable.notify_one();
	}
}

std::list<task_function> worker_pool::pop_main_loop_tasks() {
	std::lock_guard<std::mutex> lock(mutex);
	return task_queue.pop_main_loop_tasks();
}

void worker_pool::clear() {
	std::lock_guard<std::mutex> lock(mutex);
	task_queue.clear();
}

void worker_pool::shutdown() {
	if (worker_threads.empty()) {
		return;
	}

	{
		std::lock_guard<std::mutex> lock(mutex);
		is_shutting_down = true;
	}
	for (int i = 0; i < thread_count(); i++) {
		task_condition_variable.notify_one();
	}
	for (auto& thread : worker_threads) {
		if (thread.joinable()) {
			thread.join();
		}
	}
	worker_threads.clear();
	idle_threads = 0;
	is_shutting_down = false;
}

void worker_pool::wait() const {
	std::unique_lock<std::mutex> lock(mutex);
	all_done_condition_variable.wait(lock, wait_predicate());
}

void worker_pool::run_task_loop() {
	pending_task task;
	while (true) {
		// 1. Get a valid task
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (!task_queue.try_pop(task)) {
				++idle_threads;
				assert(idle_threads <= worker_thread_count);
				if (idle_threads == worker_thread_count) {
					all_done_condition_variable.notify_all();
				}
				task_condition_variable.wait(lock, [this, &task]{ return is_shutting_down || task_queue.try_pop(task); });
				--idle_threads;
				assert(idle_threads >= 0);
			}
			if (is_shutting_down) {
				return;
			}
		}

		// 2. Do some work
		task();
	}
}

} // end namespace detail

} // end namespace dispatch_queue
