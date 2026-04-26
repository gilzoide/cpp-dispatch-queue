#include "../include/pending_task_queue.hpp"

namespace dispatch_queue {

namespace detail {

bool pending_task_queue::empty() const {
	return background_tasks.empty();
}

size_t pending_task_queue::size() const {
	return background_tasks.size();
}

void pending_task_queue::clear() {
	background_tasks.clear();
}

void pending_task_queue::push(pending_task&& task, bool run_on_main_loop) {
	if (run_on_main_loop) {
		main_loop_tasks.push_back(std::move(task));
	}
	else {
		background_tasks.push_back(std::move(task));
	}
}

bool pending_task_queue::try_pop(pending_task& task) {
	if (!background_tasks.empty()) {
		task = std::move(background_tasks.front());
		background_tasks.pop_front();
		return true;
	}
	else {
		task = {};
		return false;
	}
}

std::deque<pending_task> pending_task_queue::pop_main_loop_tasks() {
	std::deque<pending_task> result;
	main_loop_tasks.swap(result);
	return result;
}

} // end namespace detail

} // end namespace dispatch_queue
