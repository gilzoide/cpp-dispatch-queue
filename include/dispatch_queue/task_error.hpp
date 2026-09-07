#pragma once

#include <stdexcept>

namespace dispatch_queue {

/**
 * Runtime error thrown when calling methods in invalid tasks.
 */
class task_error : public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};

}
