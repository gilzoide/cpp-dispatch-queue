#pragma once

#ifdef __cpp_lib_coroutine

#include <coroutine>

#include "task.hpp"

namespace dispatch_queue {

namespace detail {

template<typename T>
class promise {
public:
	promise() : future(detail::task_future<T>::create()) {}

	task<T> get_return_object() { return task(future); }
	std::suspend_never initial_suspend() noexcept { return {}; }
	std::suspend_always final_suspend() noexcept { return {}; }
	void return_value(T&& value) {
		future->set_value(std::move(value));
	}
	void unhandled_exception() {
		future->set_exception(std::current_exception());
	}

private:
	std::shared_ptr<detail::task_future<T>> future;
};


template<>
class promise<void> {
public:
	promise() : future(detail::task_future<void>::create()) {}

	task<void> get_return_object() { return task(future); }
	std::suspend_never initial_suspend() noexcept { return {}; }
	std::suspend_always final_suspend() noexcept { return {}; }
	void return_void() {
		future->set_value();
	}
	void unhandled_exception() {
		future->set_exception(std::current_exception());
	}

private:
	std::shared_ptr<detail::task_future<void>> future;
};

} // end namespace detail

} // end namespace dispatch_queue

template<typename T, typename... Args>
struct std::coroutine_traits<dispatch_queue::task<T>, Args...> {
	using promise_type = dispatch_queue::detail::promise<T>;
};

#endif
