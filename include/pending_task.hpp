#pragma once

#include <functional>

namespace dispatch_queue {

using task_id = size_t;

struct pending_task {
	task_id id;
	std::function<void()> work;
	std::vector<pending_task*> continuations;
};

} // end namespace dispatch_queue
