#pragma once

namespace dispatch_queue {

/**
 * Current state of a task.
 * @see task
 */
enum class task_state {
	/// Task was created without a future and is invalid
	invalid,
	/// Task is either queued for execution or still running
	pending,
	/// Task finished successfully and the result value is readily available
	ready,
	/// Task failed with an exception
	failed,
};

} // end namespace dispatch_queue
