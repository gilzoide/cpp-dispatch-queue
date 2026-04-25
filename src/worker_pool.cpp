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

void worker_pool::enqueue_task(pending_task&& task, task_id dependency) {
	{
		std::lock_guard<std::mutex> lk(mutex);
		task_queue.push(std::move(task), dependency);
	}
	task_condition_variable.notify_one();
}

void worker_pool::process_completed_task(pending_task* task) {
	int new_task_count = 0;
	bool processed_last = false;
	{
		std::unique_lock<std::mutex> lk(mutex);
		new_task_count = task_queue.process_completed_task(task);
		processed_last = task_queue.empty();
	}
	for (int i = 0; i < new_task_count; ++i) {
		task_condition_variable.notify_one();
	}
	if (processed_last) {
		all_done_condition_variable.notify_all();
	}
}

std::deque<pending_task*> worker_pool::pop_main_loop_tasks() {
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
		pending_task* task;
		{
			std::unique_lock<std::mutex> lk(mutex);
			task_condition_variable.wait(lk, [this]() { return is_shutting_down || !task_queue.empty(); });
			if (is_shutting_down) {
				return;
			}
			task = task_queue.pop();
		}

		// 2. Do some work
		task->work();

		// 3. Mark any pending tasks that dependend on `task` as valid
		process_completed_task(task);
	}
}

} // end namespace detail

} // end namespace dispatch_queue
