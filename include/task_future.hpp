#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "function_result.hpp"

namespace dispatch_queue {

enum class task_state {
	/// Task is either queued for execution or still running
	pending,
	/// Task finished successfully and the result value is readily available
	ready,
	/// Task failed with an exception
	failed,
};

namespace detail {

#ifdef __cpp_exceptions
	#define DISPATCH_QUEUE_TRY try
	#define DISPATCH_QUEUE_CATCH(...) catch(__VA_ARGS__)
#else
	#define DISPATCH_QUEUE_TRY
	#define DISPATCH_QUEUE_CATCH(...) if (0)
#endif

class task_future_base {
	auto wait_predicate() {
		return [this]{ return state != task_state::pending; };
	}
public:
	task_state get_state() {
		std::lock_guard<std::mutex> lock(mutex);
		return state;
	}

	std::exception_ptr get_exception() {
		std::lock_guard<std::mutex> lock(mutex);
		return exception;
	}

	void set_exception(std::exception_ptr exception) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			state = task_state::failed;
			this->exception = exception;
		}
		condition_variable.notify_all();
	}

	void wait() {
		std::unique_lock<std::mutex> lock(mutex);
		condition_variable.wait(lock, wait_predicate());
	}

	template<class Rep, class Period>
	bool wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		std::unique_lock<std::mutex> lock(mutex);
		return condition_variable.wait_for(lock, timeout_duration, wait_predicate());
	}

	template<class Clock, class Duration>
	bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		std::unique_lock<std::mutex> lock(mutex);
		return condition_variable.wait_until(lock, timeout_time, wait_predicate());
	}

protected:
	std::mutex mutex;
	std::condition_variable condition_variable;
	std::exception_ptr exception;
	task_state state;

	struct private_construct {};

	task_future_base(private_construct, task_state state)
		: state(state)
	{
	}
	task_future_base(private_construct, std::exception_ptr exception)
		: state(task_state::failed)
		, exception(exception)
	{
	}

	task_future_base(const task_future_base&) = delete;
	task_future_base& operator=(const task_future_base&) = delete;
};


template<typename T>
class task_future : public task_future_base, public std::enable_shared_from_this<task_future<T>> {
public:
	using value_type = T;

	template<typename... Args>
	task_future(private_construct, Args&&... args)
		: task_future_base(private_construct{}, std::forward<Args>(args)...)
		, empty()
	{
	}
	task_future(private_construct, T&& value)
		: task_future_base(private_construct{}, task_state::ready)
		, value(std::move(value))
	{
	}

	~task_future() {
		if (state == task_state::ready) {
			value.~T();
		}
	}

	static std::shared_ptr<task_future> create_pending() {
		return std::make_shared<task_future>(private_construct{}, task_state::pending);
	}
	static std::shared_ptr<task_future> create_ready(T&& value) {
		return std::make_shared<task_future>(private_construct{}, std::move(value));
	}
	static std::shared_ptr<task_future> create_failed(std::exception_ptr exception) {
		return std::make_shared<task_future>(private_construct{}, exception);
	}
	template<typename F>
	static std::shared_ptr<task_future> create(F&& work) {
		DISPATCH_QUEUE_TRY {
			auto value = work();
			return create_ready(std::move(value));
		}
		DISPATCH_QUEUE_CATCH(...) {
			return create_failed(std::current_exception());
		}
	}

	template<typename F>
	auto then(F&& f) {
		auto continuation_future = task_future<function_result<F>>::create_pending();
		std::unique_lock<std::mutex> lock(mutex);
		if (state == task_state::pending) {
			continuations.push_back([=]() {
				continuation_future->do_work(f);
			});
		}
		else {
			lock.unlock();
			continuation_future->do_work(f);
		}
		return continuation_future;
	}

	T get() {
		wait();
		if (exception) {
			std::rethrow_exception(exception);
		}
		return value;
	}

	template<typename F, typename... Args>
	void do_work(F&& work, Args&&... args) {
		DISPATCH_QUEUE_TRY {
			auto value = work(std::forward<Args>(args)...);
			set_value(std::move(value));
		}
		DISPATCH_QUEUE_CATCH(...) {
			set_exception(std::current_exception());
		}

		auto continuations = std::move(this->continuations);
		for (auto&& continuation : continuations) {
			continuation();
		}
	}

	template<typename F>
	auto wrap(F&& work) {
		auto shared_this = this->shared_from_this();
		return [shared_this, work]{
			shared_this->do_work(work);
		};
	}

	void set_value(T&& value) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			state = task_state::ready;
			this->value = std::move(value);
		}
		condition_variable.notify_all();
	}

private:
	std::vector<std::function<void()>> continuations;
	union {
		struct{} empty;
		T value;
	};
};


template<>
class task_future<void> : public task_future_base, public std::enable_shared_from_this<task_future<void>> {
public:
	using value_type = void;

	template<typename... Args>
	task_future(private_construct, Args&&... args)
		: task_future_base(private_construct{}, std::forward<Args>(args)...)
	{
	}

	static std::shared_ptr<task_future> create_pending() {
		return std::make_shared<task_future>(private_construct{}, task_state::pending);
	}
	static std::shared_ptr<task_future> create_ready() {
		return std::make_shared<task_future>(private_construct{}, task_state::ready);
	}
	static std::shared_ptr<task_future> create_failed(std::exception_ptr exception) {
		return std::make_shared<task_future>(private_construct{}, exception);
	}
	template<typename F>
	static std::shared_ptr<task_future> create(F&& work) {
		DISPATCH_QUEUE_TRY {
			work();
			return create_ready();
		}
		DISPATCH_QUEUE_CATCH(...) {
			return create_failed(std::current_exception());
		}
	}

	template<typename F>
	auto then(F&& f) {
		auto continuation_future = task_future<function_result<F>>::create_pending();
		std::unique_lock<std::mutex> lock(mutex);
		if (state == task_state::pending) {
			continuations.push_back([=]() {
				continuation_future->do_work(f);
			});
		}
		else {
			lock.unlock();
			continuation_future->do_work(f);
		}
		return continuation_future;
	}

	void get() {
		wait();
		if (exception) {
			std::rethrow_exception(exception);
		}
	}

	template<typename F, typename... Args>
	void do_work(F&& work, Args&&... args) {
		DISPATCH_QUEUE_TRY {
			work(std::forward<Args>(args)...);
			set_value();
		}
		DISPATCH_QUEUE_CATCH(...) {
			set_exception(std::current_exception());
		}

		auto continuations = std::move(this->continuations);
		for (auto&& continuation : continuations) {
			continuation();
		}
	}

	template<typename F>
	auto wrap(F&& work) {
		auto shared_this = this->shared_from_this();
		return [shared_this, work]{
			shared_this->do_work(work);
		};
	}

	void set_value() {
		{
			std::lock_guard<std::mutex> lock(mutex);
			state = task_state::ready;
		}
		condition_variable.notify_all();
	}

private:
	std::vector<std::function<void()>> continuations;
};

} // end namespace detail

} // end namespace dispatch_queue
