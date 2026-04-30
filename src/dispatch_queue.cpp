#include "../include/dispatch_queue.hpp"

namespace dispatch_queue {

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
		return 0;
	}
}

bool dispatch_queue::empty() const {
	return size() == 0;
}

void dispatch_queue::clear() {
	if (worker_pool) {
		worker_pool->clear();
	}
}

void dispatch_queue::main_loop() {
	std::deque<detail::pending_task> main_loop_tasks = worker_pool
		? worker_pool->pop_main_loop_tasks()
		: task_queue.pop_main_loop_tasks();
	for (auto&& it : main_loop_tasks) {
		it();
	}
}

void dispatch_queue::wait() {
	if (worker_pool) {
		worker_pool->wait();
	}
}

void dispatch_queue::shutdown() {
	clear();
	worker_pool.reset();
}

} // end namespace dispatch_queue
