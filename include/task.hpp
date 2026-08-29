#pragma once

#ifdef __cpp_lib_coroutine
#include <coroutine>
#endif
#include <exception>
#include <type_traits>

#include "detail/function_result.hpp"
#include "detail/is_instance_of.hpp"
#include "detail/task_future.hpp"
#include "task_error.hpp"
#include "task_state.hpp"

namespace dispatch_queue {

#ifdef __cpp_exceptions
	#define DISPATCH_QUEUE_THROW_INVALID_OR(body) throw task_error("task is invalid")
#else
	#define DISPATCH_QUEUE_THROW_INVALID_OR(body) body
#endif

/**
 * This template class represents asynchronous tasks that run in dispatch queues.
 *
 * Similar to `std::shared_future`, but with the addition of support for continuations (`then`),
 * checking for task state (`get_state`) and built-in C++20 coroutine support (`operator co_await`).
 *
 * All methods are thread-safe.
 */
template<typename T = void>
class task {
public:
	using value_type = T;

	task() = default;
	task(std::shared_ptr<detail::task_future<T>> future)
		: future(future)
	{
	}

	/**
	 * Checks if the task refers to a shared state.
	 */
	bool valid() const {
		return (bool)future;
	}

#ifdef __cpp_concepts
	/**
	 * Add a continuation `f` that is guaranteed to run after this task finishes.
	 *
	 * If the task is not finished yet, `f` will run right after the task finishes in the same thread where the task ran.
	 * Otherwise, `f` will run immediately in the calling thread.
	 *
	 * @throw task_error  Thrown when the task is invalid (`get_state() == task_state::invalid`)
	 */
	template<typename F>
	requires (detail::is_instance_of<T, task>::value)
	auto then(F&& f) const {
		if (future) {
			task value_this = *this;
			auto nested_future = detail::task_future<detail::function_result<F, T>>::create_pending();
			future->then([=] {
				value_this.get().then([=](const auto& t) {
					nested_future->do_work(f, t);
				});
			});
			return to_task(nested_future);
		}
		else {
			using future_t = detail::task_future<detail::function_result<F, T>>;
			DISPATCH_QUEUE_THROW_INVALID_OR({
				auto work = std::bind(f, T{});
				auto future = future_t::create(std::move(work));
				return to_task(future);
			});
		}
	}
#endif

	/**
	 * Add a continuation `f` that is guaranteed to run after this task finishes.
	 *
	 * If the task is not finished yet, `f` will run right after the task finishes in the same thread where the task ran.
	 * Otherwise, `f` will run immediately in the calling thread.
	 *
	 * @throw task_error  Thrown when the task is invalid (`get_state() == task_state::invalid`)
	 */
	template<typename F>
	auto then(F&& f) const {
		if (future) {
			task value_this = *this;
			return to_task(future->then([=] {
				return f(value_this);
			}));
		}
		else {
			using future_t = detail::task_future<detail::function_result<F, task>>;
			DISPATCH_QUEUE_THROW_INVALID_OR({
				auto work = std::bind(f, *this);
				auto future = future_t::create(std::move(work));
				return to_task(future);
			});
		}
	}

	/**
	 * Waits until the task's value is ready (by calling `wait`), then returns the stored value.
	 *
	 * @throw ...  If the task failed with an exception, rethrows the exception instead.
	 * @throw task_error  Thrown when the task is invalid (`get_state() == task_state::invalid`)
	 */
	T get() const {
		if (future) {
			return future->get();
		}
		else {
			DISPATCH_QUEUE_THROW_INVALID_OR(return T{});
		}
	}

	/**
	 * Returns the task state.
	 */
	task_state get_state() const {
		if (future) {
			return future->get_state();
		}
		else {
			return task_state::invalid;
		}
	}

	/**
	 * Returns the exception thrown while running task, if there's any.
	 */
	std::exception_ptr get_exception() const {
		if (future) {
			return future->get_exception();
		}
		else {
			return std::make_exception_ptr(task_error("task is invalid"));
		}
	}

	/**
	 * Waits until the task is either ready or failed with an exception.
	 *
	 * If the task is pending (`get_state() == task_state::pending`), blocks indefinitely until task finishes.
	 * Otherwise returns immediately without blocking.
	 *
	 * @throw task_error  Thrown when the task is invalid (`get_state() == task_state::invalid`)
	 */
	void wait() const {
		if (future) {
			future->wait();
		}
		else {
			DISPATCH_QUEUE_THROW_INVALID_OR({});
		}
	}

	/**
	 * Waits for at most `timeout_duration` until the task is either ready or failed with an exception.
	 *
	 * If the task is pending (`get_state() == task_state::pending`), blocks until task finishes or until the specified `timeout_duration` has elapsed.
	 * Otherwise returns immediately without blocking.
	 *
	 * @returns `true` if the task is finished, otherwise `false`.
	 *
	 * @throw task_error  Thrown when the task is invalid (`get_state() == task_state::invalid`)
	 */
	template<class Rep, class Period>
	bool wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) const {
		if (future) {
			return future->wait_for(timeout_duration);
		}
		else {
			DISPATCH_QUEUE_THROW_INVALID_OR(return false);
		}
	}

	/**
	 * Waits until `timeout_time` has been reached or until the task is either ready or failed with an exception, whichever comes first.
	 *
	 * If the task is pending (`get_state() == task_state::pending`), blocks until task finishes or until the specified `timeout_time` has been reached.
	 * Otherwise returns immediately without blocking.
	 *
	 * @returns `true` if the task is finished, otherwise `false`.
	 *
	 * @throw task_error  Thrown when the task is invalid (`get_state() == task_state::invalid`)
	 */
	template<class Clock, class Duration>
	bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) const {
		if (future) {
			return future->wait_until(timeout_time);
		}
		else {
			DISPATCH_QUEUE_THROW_INVALID_OR(return false);
		}
	}

	/**
	 * Convert valued task to void task.
	 */
	operator task<void>() const {
		switch (get_state()) {
			case task_state::pending: {
				auto void_future = detail::task_future<void>::create_pending();
				then([=](const task& t) {
					if (auto exception = t.get_exception()) {
						void_future->set_exception(exception);
					}
					else {
						void_future->set_value();
					}
				});
				return to_task(void_future);
			}

			case task_state::ready:
				return to_task(detail::task_future<void>::create_ready());

			case task_state::failed:
				return to_task(detail::task_future<void>::create_failed(get_exception()));

			default:
				return {};
		}
	}

	/**
	 * Convert to task of convertible type.
	 */
	template<typename U, typename = typename std::enable_if<std::is_convertible<T, U>::value>::type>
	explicit operator task<U>() const {
		switch (get_state()) {
			case task_state::pending: {
				auto u_future = detail::task_future<U>::create_pending();
				then([=](const task& t) {
					if (auto exception = t.get_exception()) {
						u_future->set_exception(exception);
					}
					else {
						U u_value = (U) t.get();
						u_future->set_value(std::move(u_value));
					}
				});
				return to_task(u_future);
			}

			case task_state::ready: {
				U u_value = (U) get();
				return to_task(detail::task_future<U>::create_ready(std::move(u_value)));
			}

			case task_state::failed:
				return to_task(detail::task_future<U>::create_failed(get_exception()));

			default:
				return {};
		}
	}

#ifdef __cpp_lib_coroutine
private:
	class task_awaiter {
	public:
		task_awaiter(const task<T>& t) : t(t) {}
		task_awaiter(task<T>&& t) : t(std::move(t)) {}

		bool await_ready() const noexcept {
			return t.get_state() != task_state::pending;
		}

		void await_suspend(std::coroutine_handle<> cont) const {
			t.then([cont](auto&&) {
				cont();
				if (cont.done()) {
					cont.destroy();
				}
			});
		}

		T await_resume() {
			return t.get();
		}

	private:
		task<T> t;
	};

public:
	/**
	 * Returns an awaiter that resumes coroutines on the task's continuation.
	 *
	 * @code
	 * dispatch_queue::task<void> my_coroutine() {
	 *     auto task = dispatch_queue.dispatch([]{ ... });
	 *     co_await task;
	 *     do_something_after_task_finished();
	 * }
	 * @endcode
	 */
	task_awaiter operator co_await() const {
		return task_awaiter(*this);
	}
#endif

private:
	std::shared_ptr<detail::task_future<T>> future;

	/// Helper function for creating a task of another type from within this class
	template<typename U>
	static task<U> to_task(std::shared_ptr<detail::task_future<U>> future) {
		return task<U>(future);
	}
};

} // end namespace dispatch_queue
