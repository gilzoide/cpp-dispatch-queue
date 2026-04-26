#pragma once

#include <functional>

namespace dispatch_queue {

struct pending_task {
	std::function<void()> work;
};

} // end namespace dispatch_queue
