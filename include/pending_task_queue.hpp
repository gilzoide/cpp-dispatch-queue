#pragma once

#include <limits>
#include <list>
#include <unordered_map>

namespace dispatch_queue {

static constexpr int NULL_TAG = std::numeric_limits<int>::min();

namespace detail {

enum class task_type {
	main,
	background,
	tagged,
};

using task_function = std::function<void()>;

struct pending_task {
	task_function implementation;
	int tag = NULL_TAG;

	void operator()() const {
		implementation();
	}
};

class pending_task_queue {
public:
	bool empty() const;
	size_t size() const;
	void clear();

	bool push(task_type type, task_function&& task, int tag = NULL_TAG);
	bool try_pop(pending_task& task);
	std::list<task_function> pop_main_loop_tasks();

private:
	std::unordered_map<int, std::list<pending_task>> tagged_tasks;
	std::list<pending_task> background_tasks;
	std::list<task_function> main_loop_tasks;
};

} // end namespace detail

} // end namespace dispatch_queue
