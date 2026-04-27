#pragma once

#ifdef __cpp_lib_coroutine
#include <coroutine>
#endif

#include "function_result.hpp"
#include "is_instance_of.hpp"
#include "task_future.hpp"

namespace dispatch_queue {

template<typename T>
class task {
public:
	using value_type = T;

	task(std::shared_ptr<detail::task_future<T>> future)
		: future(future)
	{
	}

#ifdef __cpp_concepts
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

	template<typename F>
	auto then(F&& f) const {
		task value_this = *this;
		return to_task(future->then([=]() {
			return f(value_this);
		}));
	}

	T get() const {
		return future->get();
	}

	task_state get_state() const {
		return future->get_state();
	}

	std::exception_ptr get_exception() const {
		return future->get_exception();
	}

	void wait() const {
		future->wait();
	}

	template<class Rep, class Period>
	std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) const {
		return future->wait_for(timeout_duration);
	}

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
