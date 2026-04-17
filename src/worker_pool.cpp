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
	condition_variable.notify_one();
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
		condition_variable.notify_one();
	}
	for (auto& thread : worker_threads) {
		if (thread.joinable()) {
			thread.join();
		}
	}
	worker_threads.clear();
	is_shutting_down = false;
}

void worker_pool::run_task_loop() {
	while (true) {
		// 1. Get a valid task
		pending_task* task;
		{
			std::unique_lock<std::mutex> lk(mutex);
			condition_variable.wait(lk, [&, this]() { return is_shutting_down || !task_queue.empty(); });
			if (is_shutting_down) {
				return;
			}
			task = task_queue.pop();
		}

		// 2. Do some work
		task->work();

		// 3. Mark any pending tasks that dependend on `task` as valid
		int new_task_count;
		{
			std::unique_lock<std::mutex> lk(mutex);
			if (is_shutting_down) {
				return;
			}
			new_task_count = task_queue.process_completed_task(task);
		}
		for (int i = 0; i < new_task_count; ++i) {
			condition_variable.notify_one();
		}
	}
}

} // end namespace detail

} // end namespace dispatch_queue
