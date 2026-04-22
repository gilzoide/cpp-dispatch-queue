#pragma once

#include <condition_variable>
#include <exception>
#include <future>
#include <memory>
#include <mutex>


namespace dispatch_queue {

using task_id = size_t;

enum class task_state {
	pending,
	ready,
};

namespace detail {

template<typename T>
class task_future : public std::enable_shared_from_this<task_future<T>> {
	auto wait_predicate() {
		return [this]{ return is_ready(); };
	}
public:
	task_future(task_state state)
		: empty()
		, state(state)
	{
	}
	task_future(T&& value)
		: value(std::move(value))
		, state(task_state::ready)
	{
	}

	static std::shared_ptr<task_future> create(task_state state) {
		return std::make_shared<task_future>(state);
	}
	template<typename F>
	static std::shared_ptr<task_future> create(F&& work) {
		return std::make_shared<task_future>(std::move(work()));
	}

	T get() {
		wait();
		if (exception) {
			std::rethrow_exception(exception);
		}
		return value;
	}

	std::exception_ptr get_exception() const {
		return exception;
	}

	bool is_pending() const {
		return state == task_state::pending;
	}

	bool is_ready() const {
		return state == task_state::ready;
	}

	bool is_exception() const {
		return exception;
	}

	void wait() {
		if (is_ready()) {
			return;
		}
		std::unique_lock<std::mutex> lock(mutex);
		condition_variable.wait(lock, wait_predicate());
	}

	template<class Rep, class Period>
	std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		std::unique_lock<std::mutex> lock(mutex);
		return condition_variable.wait_for(lock, timeout_duration, wait_predicate());
	}

	template<class Clock, class Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		std::unique_lock<std::mutex> lock(mutex);
		return condition_variable.wait_for(lock, timeout_time, wait_predicate());
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

	void set_exception(std::exception_ptr exception) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			state = task_state::ready;
			this->exception = exception;
		}
		condition_variable.notify_all();
	}

private:
	std::mutex mutex;
	std::condition_variable condition_variable;
	std::exception_ptr exception;
	union {
		struct{} empty;
		T value;
	};
	task_state state;
};

template<>
class task_future<void> : public std::enable_shared_from_this<task_future<void>> {
	auto wait_predicate() {
		return [this]{ return is_ready(); };
	}
public:
	task_future(task_state state) : state(state)
	{
	}

	static std::shared_ptr<task_future> create(task_state state) {
		return std::make_shared<task_future>(state);
	}
	template<typename F>
	static std::shared_ptr<task_future> create(F&& work) {
		work();
		return std::make_shared<task_future>(task_state::ready);
	}

	void get() {
		wait();
		if (exception) {
			std::rethrow_exception(exception);
		}
	}

	std::exception_ptr get_exception() const {
		return exception;
	}

	bool is_pending() const {
		return state == task_state::pending;
	}

	bool is_ready() const {
		return state == task_state::ready;
	}

	bool is_exception() const {
		return (bool)exception;
	}

	void wait() {
		if (is_ready()) {
			return;
		}
		std::unique_lock<std::mutex> lock(mutex);
		condition_variable.wait(lock, wait_predicate());
	}

	template<class Rep, class Period>
	std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		std::unique_lock<std::mutex> lock(mutex);
		return condition_variable.wait_for(lock, timeout_duration, wait_predicate());
	}

	template<class Clock, class Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		std::unique_lock<std::mutex> lock(mutex);
		return condition_variable.wait_for(lock, timeout_time, wait_predicate());
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

	void set_exception(std::exception_ptr exception) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			state = task_state::ready;
			this->exception = exception;
		}
		condition_variable.notify_all();
	}

private:
	std::mutex mutex;
	std::condition_variable condition_variable;
	std::exception_ptr exception;
	task_state state;
};

} // end namespace detail

template<typename T>
class task {
public:
	task_id get_id() const {
		return id;
	}

	T get() {
		return future->get();
	}

	std::exception_ptr get_exception() const {
		return future->get_exception();
	}

	bool is_pending() const {
		return future->is_pending();
	}

	bool is_ready() const {
		return future->is_ready();
	}

	bool is_exception() const {
		return future->is_exception();
	}

	void wait() {
		future->wait();
	}

	template<class Rep, class Period>
	std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		return future->wait_for(timeout_duration);
	}

	template<class Clock, class Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		return future->wait_until(timeout_time);
	}

private:
	task_id id;
	std::shared_ptr<detail::task_future<T>> future;

	task(task_id id, std::shared_ptr<detail::task_future<T>> future) : id(id), future(future) {}

	friend class dispatch_queue;
};

} // end namespace dispatch_queue
