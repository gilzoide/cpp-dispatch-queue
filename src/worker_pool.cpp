#include "../include/worker_pool.hpp"

namespace dispatch_queue {

namespace detail {

worker_pool::~worker_pool() {
	shutdown();
}

int worker_pool::thread_count() const {
	return worker_threads.size();
}

size_t worker_pool::size() {
	std::lock_guard<std::mutex> lk(mutex);
	return task_queue.size();
}

void worker_pool::enqueue_task(pending_task&& task, bool run_on_main_loop) {
	{
		std::lock_guard<std::mutex> lk(mutex);
		task_queue.push(std::move(task), run_on_main_loop);
	}
	task_condition_variable.notify_one();
}

std::deque<pending_task> worker_pool::pop_main_loop_tasks() {
	std::lock_guard<std::mutex> lk(mutex);
	return task_queue.pop_main_loop_tasks();
}

void worker_pool::clear() {
	std::lock_guard<std::mutex> lk(mutex);
	task_queue.clear();
}

void worker_pool::shutdown() {
	if (worker_threads.empty()) {
		return;
	}

	{
		std::lock_guard<std::mutex> lk(mutex);
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
	is_shutting_down = false;
}

void worker_pool::wait() {
	std::unique_lock<std::mutex> lk(mutex);
	all_done_condition_variable.wait(lk, [this]{ return is_shutting_down || task_queue.empty(); });
}

void worker_pool::run_task_loop() {
	while (true) {
		// 1. Get a valid task
		pending_task task;
		{
			std::unique_lock<std::mutex> lk(mutex);
			task_condition_variable.wait(lk, [this, &task]() { return is_shutting_down || task_queue.try_pop(task); });
			if (is_shutting_down) {
				return;
			}
		}

		// 2. Do some work
		task();

		// 3. If all is done, notify waiters
		bool all_done;
		{
			std::lock_guard<std::mutex> lk(mutex);
			all_done = task_queue.empty();
		}
		if (all_done) {
			all_done_condition_variable.notify_all();
		}
	}
}

} // end namespace detail

} // end namespace dispatch_queue
