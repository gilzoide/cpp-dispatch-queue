#pragma once

#include <functional>

#include "task.hpp"

namespace dispatch_queue {

struct pending_task {
	task_id id;
	std::function<void()> work;
	std::vector<pending_task*> continuations;
};

} // end namespace dispatch_queue
