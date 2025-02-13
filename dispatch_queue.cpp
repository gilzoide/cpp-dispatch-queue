#include "dispatch_queue.hpp"

namespace dispatch_queue {

namespace detail {

worker_pool::worker_pool(int thread_count, std::deque<std::function<void()>>& task_queue)
	: task_queue(task_queue)
	, worker_threads(thread_count)
{
	for (int i = 0; i < thread_count; i++) {
		worker_threads.emplace_back(thread_entrypoint, this);
	}
}

worker_pool::~worker_pool() {
	shutdown();
}

int worker_pool::thread_count() const {
	return worker_threads.size();
}

void worker_pool::enqueue_task(std::function<void()>&& task) {
	{
		std::lock_guard<std::mutex> lk(mutex);
		task_queue.push_back(std::move(task));
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

void worker_pool::thread_entrypoint(worker_pool *pool) {
	while (true) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lk(pool->mutex);
			pool->condition_variable.wait(lk, [&]() { return pool->is_shutting_down || !pool->task_queue.empty(); });
			if (pool->is_shutting_down) {
				return;
			}
			task = std::move(pool->task_queue.front());
			pool->task_queue.pop_front();
		}
		task();
	}
}

} // end namespace detail


dispatch_queue::dispatch_queue()
	: dispatch_queue(0)
{
}

dispatch_queue::dispatch_queue(int thread_count) {
	if (thread_count < 0) {
		thread_count = std::thread::hardware_concurrency();
	}
	if (thread_count > 0) {
		worker_pool = new detail::worker_pool(thread_count, task_queue);
	}
	else {
		worker_pool = nullptr;
	}
}

dispatch_queue::~dispatch_queue() {
	shutdown();
}

bool dispatch_queue::is_threaded() const {
	return worker_pool != nullptr;
}

int dispatch_queue::thread_count() const {
	if (worker_pool) {
		return worker_pool->thread_count();
	}
	else {
		return 0;
	}
}

void dispatch_queue::clear() {
	if (worker_pool) {
		worker_pool->clear();
	}
	else {
		task_queue.clear();
	}
}

void dispatch_queue::shutdown() {
	clear();
	if (worker_pool) {
		delete worker_pool;
		worker_pool = nullptr;
	}
}

}
