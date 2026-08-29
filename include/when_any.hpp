#pragma once

#include <atomic>
#include <iterator>
#include <memory>
#ifdef __cpp_lib_ranges
#include <ranges>
#endif

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
#ifdef __cpp_lib_ranges
	auto is_empty = std::ranges::empty(tasks);
#else
	auto is_empty = std::empty(tasks);
#endif
	if (is_empty) {
		return detail::task_future<void>::create_ready();
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
	task<void> when_any(Tasks&&... tasks) {
		if (sizeof...(Tasks) == 0) {
			return detail::task_future<void>::create_ready();
		}

		detail::when_any_helper helper;
		(tasks.then(helper), ...);
		return helper.future;
	}
#endif // __cpp_fold_expressions

} // end namespace dispatch_queue
