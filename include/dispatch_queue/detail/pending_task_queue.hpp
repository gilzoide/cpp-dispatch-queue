#pragma once

#include <list>
#include <unordered_map>

#include "../task_tag.hpp"

namespace dispatch_queue {

namespace detail {

enum class task_type {
	main,
	background,
	tagged,
};

using task_function = std::function<void()>;

struct pending_task {
	task_function implementation;
	task_tag tag = NULL_TAG;

	void operator()() const {
		implementation();
	}
};

struct delayed_task {
	task_function implementation;
	float delay;
};

class pending_task_queue {
public:
	bool empty() const;
	size_t size() const;
	void clear();

	bool push(task_type type, task_function&& task, float delay, task_tag tag = NULL_TAG);
	bool try_pop(pending_task& task);
	std::list<task_function> pop_main_loop_tasks(float delta);

private:
	std::unordered_map<task_tag, std::list<pending_task>> tagged_tasks;
	std::list<pending_task> background_tasks;
	std::list<task_function> main_loop_tasks;
	std::list<delayed_task> main_loop_delayed_tasks;
};

} // end namespace detail

} // end namespace dispatch_queue
