#pragma once

#include "task_future.hpp"

namespace dispatch_queue {

template<typename T>
class task {
public:
	task(std::shared_ptr<detail::task_future<T>> future)
		: future(future)
	{
	}

	template<typename F>
	auto then(F&& f) {
		return to_task(future->then([f, this]() {
			return f(*this);
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

private:
	std::shared_ptr<detail::task_future<T>> future;

	template<typename U>
	static task<U> to_task(std::shared_ptr<detail::task_future<U>> future) {
		return task<U>(future);
	}
};

} // end namespace dispatch_queue
