#include "dispatch_queue.hpp"

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

void worker_pool::run_task_loop() {
	while (true) {
		std::function<void()> task;
		{
			std::unique_lock<std::mutex> lk(mutex);
			condition_variable.wait(lk, [this]() { return is_shutting_down || !task_queue.empty(); });
			if (is_shutting_down) {
				return;
			}
			task = std::move(task_queue.front());
			task_queue.pop_front();
		}
		task();
	}
}

} // end namespace detail


dispatch_queue::dispatch_queue()
	: dispatch_queue(0)
{
}

dispatch_queue::dispatch_queue(int thread_count)
	: dispatch_queue(thread_count, [](int){})
{
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

size_t dispatch_queue::size() const {
	if (worker_pool) {
		return worker_pool->size();
	}
	else {
		return task_queue.size();
	}
}

bool dispatch_queue::empty() const {
	return size() == 0;
}

void dispatch_queue::clear() {
	if (worker_pool) {
		worker_pool->clear();
	}
	else {
		task_queue.clear();
	}
}

void dispatch_queue::wait() {
	auto future = dispatch([](){});
	future.wait();
}

void dispatch_queue::shutdown() {
	clear();
	if (worker_pool) {
		delete worker_pool;
		worker_pool = nullptr;
	}
}

}
