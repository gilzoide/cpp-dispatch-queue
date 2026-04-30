#pragma once

#include <deque>

namespace dispatch_queue {

namespace detail {

using pending_task = std::function<void()>;

class pending_task_queue {
public:
	bool empty() const;
	size_t size() const;
	void clear();

	void push(pending_task&& task, bool run_on_main_loop);
	bool try_pop(pending_task& task);
	std::deque<pending_task> pop_main_loop_tasks();

private:
	std::deque<pending_task> background_tasks;
	std::deque<pending_task> main_loop_tasks;
};

} // end namespace detail

} // end namespace dispatch_queue
