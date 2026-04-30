#pragma once

#ifdef __cpp_lib_coroutine
#include <coroutine>
#endif

#include "function_result.hpp"
#include "is_instance_of.hpp"
#include "task_future.hpp"

namespace dispatch_queue {

/**
 * This template class represents asynchronous tasks that run in dispatch queues.
 *
 * Similar to `std::shared_future`, but with the addition of support for continuations (`then`),
 * checking for task state (`get_state`) and built-in C++20 coroutine support (`operator co_await`).
 *
 * All methods are thread-safe.
 */
template<typename T>
class task {
public:
	using value_type = T;

	task(std::shared_ptr<detail::task_future<T>> future)
		: future(future)
	{
	}

#ifdef __cpp_concepts
	/**
	 * Add a continuation `f` that is guaranteed to run after this task finishes.
	 *
	 * If the task is not finished yet, `f` will run right after the task finishes in the same thread where the task ran.
	 * Otherwise, `f` will run immediately in the current thread.
	 */
	template<typename F>
	requires (detail::is_instance_of<T, task>::value)
	auto then(F&& f) const {
		auto nested_future = detail::task_future<detail::function_result<F, T>>::create();
		task value_this = *this;
		future->then([=]() {
			value_this.get().then([=](auto t) {
				nested_future->do_work(f, t);
			});
		});
		return to_task(nested_future);
	}
#endif

	/**
	 * Add a continuation `f` that is guaranteed to run after this task finishes.
	 *
	 * If the task is not finished yet, `f` will run right after the task finishes in the same thread where the task ran.
	 * Otherwise, `f` will run immediately in the current thread.
	 */
	template<typename F>
	auto then(F&& f) const {
		task value_this = *this;
		return to_task(future->then([=]() {
			return f(value_this);
		}));
	}

	/**
	 * Waits until the task's value is ready (by calling `wait`), then returns the stored value.
	 *
	 * If the task failed with an exception, rethrows the exception instead.
	 */
	T get() const {
		return future->get();
	}

	/**
	 * Returns the task state.
	 */
	task_state get_state() const {
		return future->get_state();
	}

	/**
	 * Returns the exception thrown while running task, if there's any.
	 */
	std::exception_ptr get_exception() const {
		return future->get_exception();
	}

	/**
	 * Waits until the task is either ready or failed with an exception.
	 *
	 * If the task is pending (`get_state() == task_state::pending`), blocks indefinitely until task finishes.
	 * Otherwise returns immediately without blocking.
	 */
	void wait() const {
		future->wait();
	}

	/**
	 * Waits for at most `timeout_duration` until the task is either ready or failed with an exception.
	 *
	 * If the task is pending (`get_state() == task_state::pending`), blocks until task finishes or until the specified `timeout_duration` has elapsed.
	 * Otherwise returns immediately without blocking.
	 *
	 * @returns `std::future_status::timeout` if the timeout has expired, otherwise `std::future_status::ready`.
	 */
	template<class Rep, class Period>
	std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) const {
		return future->wait_for(timeout_duration);
	}

	/**
	 * Waits until `timeout_time` has been reached or until the task is either ready or failed with an exception, whichever comes first.
	 *
	 * If the task is pending (`get_state() == task_state::pending`), blocks until task finishes or until the specified `timeout_time` has been reached.
	 * Otherwise returns immediately without blocking.
	 *
	 * @returns `std::future_status::timeout` if the timeout has expired, otherwise `std::future_status::ready`.
	 */
	template<class Clock, class Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) const {
		return future->wait_until(timeout_time);
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
			t.future->then(cont);
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
	 */
	task_awaiter operator co_await() const {
		return task_awaiter(*this);
	}
#endif

private:
	std::shared_ptr<detail::task_future<T>> future;

	template<typename U>
	static task<U> to_task(std::shared_ptr<detail::task_future<U>> future) {
		return task<U>(future);
	}
};

} // end namespace dispatch_queue
