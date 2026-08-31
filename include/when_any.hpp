#pragma once

#include <atomic>
#include <memory>

#include "detail/ranges.hpp"
#include "task.hpp"

namespace dispatch_queue {

namespace detail {

struct when_any_helper {
	template<typename T>
	void operator()(const task<T>& task) const {
		if (!result_set->exchange(true, std::memory_order_acq_rel)) {
			if (auto exception = task.get_exception()) {
				future->set_exception(exception);
			}
			else {
				future->set_value();
			}
		}
	}

	std::shared_ptr<std::atomic<bool>> result_set = std::make_shared<std::atomic<bool>>(false);
	std::shared_ptr<detail::task_future<void>> future = detail::task_future<void>::create_pending();
};

template<typename TaskRange>
task<void> when_any_internal(const TaskRange& tasks) {
	auto tasks_size = detail::range_size(tasks);
	if (tasks_size == 0) {
		return detail::task_future<void>::create_ready();
	}
	else if (tasks_size == 1) {
		return *detail::range_begin(tasks);
	}

	when_any_helper helper;
	for (auto&& task : tasks) {
		task.then(helper);
	}
	return helper.future;
}

} // end namespace detail

/**
 * Create a task that finishes whenever any of the given tasks finish.
 *
 * If the first finishing task fails, this task fails with the same exception.
 * Otherwise, it succeeds.
 */
template<typename T>
task<void> when_any(std::initializer_list<task<T>> tasks) {
	return detail::when_any_internal(tasks);
}

/**
 * Create a task that finishes whenever any of the given tasks finish.
 *
 * If the first finishing task fails, this task fails with the same exception.
 * Otherwise, it succeeds.
 */
template<typename TaskRange>
task<void> when_any(const TaskRange& tasks) {
	return detail::when_any_internal(tasks);
}

#ifdef __cpp_fold_expressions
	/**
	 * Create a task that finishes whenever any of the given tasks finish.
	 *
	 * If the first finishing task fails, this task fails with the same exception.
	 * Otherwise, it succeeds.
	 */
	template<typename... Tasks>
	task<void> when_any(const Tasks&... tasks) {
		if constexpr (sizeof...(Tasks) == 0) {
			return detail::task_future<void>::create_ready();
		}
		else if constexpr (sizeof...(Tasks) == 1) {
			return std::get<0>(std::forward_as_tuple(std::forward<Tasks>(tasks)...));
		}
		else {
			detail::when_any_helper helper;
			(tasks.then(helper), ...);
			return helper.future;
		}
	}
#endif // __cpp_fold_expressions

} // end namespace dispatch_queue
