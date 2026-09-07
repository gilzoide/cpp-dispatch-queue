#pragma once

#include <atomic>
#include <exception>
#include <memory>

#include "detail/ranges.hpp"
#include "task.hpp"

namespace dispatch_queue {

namespace detail {

struct when_all_helper {
	when_all_helper(size_t count)
		: result_count(std::make_shared<std::atomic<size_t>>(count))
	{
	}

	template<typename T>
	void operator()(const task<T>& task) const {
		if (task.get_state() == task_state::failed) {
			any_failed->store(true, std::memory_order_release);
		}
		if (result_count->fetch_sub(1, std::memory_order_acq_rel) == 1) {
			if (any_failed->load(std::memory_order_acquire)) {
				future->set_exception(std::make_exception_ptr(task_error("subtask failed")));
			}
			else {
				future->set_value();
			}
		}
	}

	std::shared_ptr<std::atomic<size_t>> result_count;
	std::shared_ptr<std::atomic<bool>> any_failed = std::make_shared<std::atomic<bool>>(false);
	std::shared_ptr<detail::task_future<void>> future = detail::task_future<void>::create_pending();
};

template<typename TaskRange>
task<void> when_all_internal(const TaskRange& tasks) {
	auto tasks_size = detail::range_size(tasks);
	if (tasks_size == 0) {
		return detail::task_future<void>::create_ready();
	}
	else if (tasks_size == 1) {
		return *detail::range_begin(tasks);
	}

	when_all_helper helper(tasks_size);
	for (auto&& task : tasks) {
		task.then(helper);
	}
	return helper.future;
}

} // end namespace detail

/**
 * Create a task that finishes whenever all given tasks finish.
 *
 * If any subtask fails, this task fail with a `task_error`.
 * Otherwise, it succeeds.
 */
template<typename T>
task<void> when_all(std::initializer_list<task<T>> tasks) {
	return detail::when_all_internal(tasks);
}

/**
 * Create a task that finishes whenever all given tasks finish.
 *
 * If any subtask fails, this task fail with a `task_error`.
 * Otherwise, it succeeds.
 */
template<typename TaskRange>
task<void> when_all(const TaskRange& tasks) {
	return detail::when_all_internal(tasks);
}

#ifdef __cpp_fold_expressions
	/**
	 * Create a task that finishes whenever all given tasks finish.
	 *
	 * If any subtask fails, this task fail with a `task_error`.
	 * Otherwise, it succeeds.
	 */
	template<typename... Tasks>
	task<void> when_all(const Tasks&... tasks) {
		if constexpr (sizeof...(Tasks) == 0) {
			return detail::task_future<void>::create_ready();
		}
		else if constexpr (sizeof...(Tasks) == 1) {
			return std::get<0>(std::forward_as_tuple(std::forward<Tasks>(tasks)...));
		}
		else {
			detail::when_all_helper helper(sizeof...(Tasks));
			(tasks.then(helper), ...);
			return helper.future;
		}
	}
#endif // __cpp_fold_expressions

} // end namespace dispatch_queue
