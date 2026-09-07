#include "../include/dispatch_queue/dispatch_queue.hpp"
#include "../include/dispatch_queue/thread_name.hpp"

namespace dispatch_queue {

task_dispatcher::task_dispatcher()
{
}

task_dispatcher::task_dispatcher(int thread_count, const std::string& name_prefix)
	: task_dispatcher(thread_count, [name_prefix](int i){
		set_thread_name(name_prefix + std::to_string(i));
	})
{
}

task_dispatcher::~task_dispatcher() {
	shutdown();
}

bool task_dispatcher::is_threaded() const {
	return worker_pool != nullptr;
}

int task_dispatcher::thread_count() const {
	if (worker_pool) {
		return worker_pool->thread_count();
	}
	else {
		return 0;
	}
}

size_t task_dispatcher::size() const {
	if (worker_pool) {
		return worker_pool->size();
	}
	else {
		return 0;
	}
}

bool task_dispatcher::empty() const {
	return size() == 0;
}

void task_dispatcher::clear() {
	if (worker_pool) {
		worker_pool->clear();
	}
}

void task_dispatcher::main_loop() {
	auto main_loop_tasks = worker_pool
		? worker_pool->pop_main_loop_tasks()
		: task_queue.pop_main_loop_tasks();
	for (auto&& it : main_loop_tasks) {
		it();
	}
}

void task_dispatcher::wait() {
	if (worker_pool) {
		worker_pool->wait();
	}
}

void task_dispatcher::shutdown() {
	clear();
	worker_pool.reset();
}

} // end namespace dispatch_queue
