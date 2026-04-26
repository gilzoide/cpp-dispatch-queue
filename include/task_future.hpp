#pragma once

#include <condition_variable>
#include <exception>
#include <future>
#include <memory>
#include <mutex>

namespace dispatch_queue {

enum class task_state {
	pending,
	ready,
	exception,
};

namespace detail {

class task_future_base {
	auto wait_predicate() {
		return [this]{ return state == task_state::ready; };
	}
public:
	task_state get_state() {
		std::lock_guard<std::mutex> lock(mutex);
		return state;
	}

	std::exception_ptr get_exception() const {
		return exception;
	}

	void set_exception(std::exception_ptr exception) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			state = task_state::exception;
			this->exception = exception;
		}
		condition_variable.notify_all();
	}

	void wait() {
		if (state == task_state::ready || state == task_state::exception) {
			return;
		}
		std::unique_lock<std::mutex> lock(mutex);
		condition_variable.wait(lock, wait_predicate());
	}

	template<class Rep, class Period>
	std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		if (state == task_state::ready || state == task_state::exception) {
			return std::future_status::ready;
		}
		std::unique_lock<std::mutex> lock(mutex);
		return condition_variable.wait_for(lock, timeout_duration, wait_predicate());
	}

	template<class Clock, class Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		if (state == task_state::ready || state == task_state::exception) {
			return std::future_status::ready;
		}
		std::unique_lock<std::mutex> lock(mutex);
		return condition_variable.wait_for(lock, timeout_time, wait_predicate());
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
};

template<typename T>
class task_future : public task_future_base, public std::enable_shared_from_this<task_future<T>> {
public:
	task_future(private_construct, task_state state)
		: task_future_base(private_construct{}, state)
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

	static std::shared_ptr<task_future> create(task_state state = task_state::pending) {
		return std::make_shared<task_future>(private_construct{}, state);
	}
	template<typename F>
	static std::shared_ptr<task_future> create(F&& work) {
		return std::make_shared<task_future>(private_construct{}, std::move(work()));
	}

	T get() {
		wait();
		if (exception) {
			std::rethrow_exception(exception);
		}
		return value;
	}

	template<typename F>
	void do_work(F&& work) {
#ifdef __cpp_exceptions
		try {
#endif
			auto value = work();
			set_value(std::move(value));
#ifdef __cpp_exceptions
		}
		catch (...) {
			set_exception(std::current_exception());
		}
#endif
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
	union {
		struct{} empty;
		T value;
	};
};

template<>
class task_future<void> : public task_future_base, public std::enable_shared_from_this<task_future<void>> {
public:
	task_future(private_construct, task_state state)
		: task_future_base(private_construct{}, state)
	{
	}

	static std::shared_ptr<task_future> create(task_state state = task_state::pending) {
		return std::make_shared<task_future>(private_construct{}, state);
	}
	template<typename F>
	static std::shared_ptr<task_future> create(F&& work) {
		work();
		return std::make_shared<task_future>(private_construct{}, task_state::ready);
	}

	void get() {
		wait();
		if (exception) {
			std::rethrow_exception(exception);
		}
	}

	template<typename F>
	void do_work(F&& work) {
#ifdef __cpp_exceptions
		try {
#endif
			work();
			set_value();
#ifdef __cpp_exceptions
		}
		catch (...) {
			set_exception(std::current_exception());
		}
#endif
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
};

} // end namespace detail

} // end namespace dispatch_queue
