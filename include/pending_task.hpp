#pragma once

#include <functional>

#include "task.hpp"

namespace dispatch_queue {

struct pending_task {
	task_id id;
	std::function<void()> work;
	std::vector<pending_task*> continuations;
	bool run_on_main_loop;

	template<typename F>
	pending_task(task_id id, F&& work, bool run_on_main_loop)
		: id(id)
		, work(std::move(work))
		, run_on_main_loop(run_on_main_loop)
	{
	}
};

} // end namespace dispatch_queue
