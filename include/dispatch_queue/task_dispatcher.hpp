#pragma once

#include <functional>
#include <utility>

#include "detail/function_result.hpp"
#include "detail/pending_task_queue.hpp"
#include "detail/ranges.hpp"
#include "detail/worker_pool.hpp"
#include "task_tag.hpp"
#include "task.hpp"
#include "when_all.hpp"

#ifndef DISPATCH_QUEUE_DEFAULT_BATCH_SIZE
	#define DISPATCH_QUEUE_DEFAULT_BATCH_SIZE 64
#endif

namespace dispatch_queue {

class task_dispatcher {
public:
	/**
	 * Create an immediate dispatch queue.
	 * In immediate mode, tasks are executed immediately when calling `dispatch`.
	 */
	task_dispatcher();

	/**
	 * Initializes dispatch queue with `thread_count` background threads.
	 *
	 * @param thread_count  Number of background threads used to run tasks.
	 *                      If 0, the dispatch queue is created in immediate mode.
	 *                      If 1, tasks will run serially in background, one at a time, without any concurrency.
	 *                      Otherwise, `thread_count` threads will be created and tasks may run concurrently.
	 *                      Pass a negative number to use the default value of `std::thread::hardware_concurrency()` threads.
	 * @param name_prefix  Prefix for background thread names.
	 *                     Each thread will be named with the prefix plus a number from 0 until thread_count.
	 *
	 * @see dispatcher(int, Fn&&)
	 */
	task_dispatcher(int thread_count, const std::string& name_prefix = "dispatcher");

	/**
	 * Initializes dispatch queue with `thread_count` background threads and a worker initialization functor.
	 *
	 * @param thread_count  Number of background threads used to run tasks.
	 *                      If 0, the dispatch queue is created in immediate mode.
	 *                      If 1, tasks will run serially in background, one at a time, without any concurrency.
	 *                      Otherwise, `thread_count` threads will be created and tasks may run concurrently.
	 *                      Pass a negative number to use the default value of `std::thread::hardware_concurrency()` threads.
	 * @param worker_init  Functor called inside worker threads for initialization, receiving as argument the worker index.
	 *                     May be used to set the thread name or initialize thread local variables, for example.
	 */
	template<typename Fn>
	task_dispatcher(int thread_count, Fn&& worker_init) {
		if (thread_count < 0) {
			thread_count = std::thread::hardware_concurrency();
		}
		if (thread_count > 0) {
			worker_pool = std::make_unique<detail::worker_pool>(task_queue, thread_count, std::move(worker_init));
		}
	}

	// Not copyable
	task_dispatcher(const task_dispatcher&) = delete;
	task_dispatcher& operator=(const task_dispatcher&) = delete;

	// But moveable
	task_dispatcher(task_dispatcher&&) = default;
	task_dispatcher& operator=(task_dispatcher&&) = default;

	/**
	 * Calls `shutdown`.
	 */
	~task_dispatcher();

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args`.
	 * If the dispatch queue is in immediate mode, the task is processed immediately in the calling thread.
	 *
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch(F&& f, Args&&... args) {
		return dispatch_internal(detail::task_type::background, NULL_TAG, 0, std::forward<F>(f), std::forward<Args>(args)...);
	}

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args` in main loop.
	 * Tasks dispatched with `dispatch_main` will only be executed when calling `main_loop`.
	 *
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 * @see main_loop
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_main(F&& f, Args&&... args) {
		return dispatch_internal(detail::task_type::main, NULL_TAG, 0, std::forward<F>(f), std::forward<Args>(args)...);
	}

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args` in main loop.
	 * Tasks dispatched with `dispatch_main_after` will only be executed when calling `main_loop(float)` with a positive delta.
	 *
	 * @param delay Time to wait until task is executed in main loop
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 * @see main_loop
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_main_after(float delay, F&& f, Args&&... args) {
		return dispatch_internal(detail::task_type::main, NULL_TAG, delay, std::forward<F>(f), std::forward<Args>(args)...);
	}

	/**
	 * Dispatch a tagged task that calls `f` with forwarded arguments `args`.
	 * Tasks tagged with the same value never run in parallel: at most one task is processed for each tag at a time.
	 * Use this to serialize different task types without having to create separate dispatch queues.
	 * If the dispatch queue is in immediate mode, the task is processed immediately in the calling thread.
	 *
	 * @param tag The tag associated to the task
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_tagged(task_tag tag, F&& f, Args&&... args) {
		return dispatch_internal(detail::task_type::tagged, tag, 0, std::forward<F>(f), std::forward<Args>(args)...);
	}

	/**
	 * Iterate from `begin` until `end` applying `f` to each element in one or more dispatched tasks.
	 *
	 * The range is chunked in batches of size `batch_size`, so each dispatched task is applied to at most `batch_size` elements.
	 * The returned task finishes when all batches finish.
	 */
	template<typename F, typename It>
	task<void> parallel_for(F&& f, const It& begin, const It& end, size_t batch_size = DISPATCH_QUEUE_DEFAULT_BATCH_SIZE) {
		std::vector<task<void>> tasks;
		detail::apply_batches([&](auto&& batch_begin, auto&& batch_end) {
			tasks.emplace_back(dispatch([=] {
				for (auto it = batch_begin; it != batch_end; ++it) {
					f(*it);
				}
			}));
		}, begin, end, batch_size);
		return when_all(tasks);
	}

	/**
	 * Iterate over `range` applying `f` to each element in one or more dispatched tasks.
	 *
	 * The range is chunked in batches of size `batch_size`, so each dispatched task is applied to at most `batch_size` elements.
	 * The returned task finishes when all batches finish.
	 */
	template<typename F, typename R>
	task<void> parallel_for(F&& f, R&& range, size_t batch_size = DISPATCH_QUEUE_DEFAULT_BATCH_SIZE) {
		std::vector<task<void>> tasks;
		detail::apply_batches([&](auto&& batch_begin, auto&& batch_end) {
			tasks.emplace_back(dispatch([=] {
				for (auto it = batch_begin; it != batch_end; ++it) {
					f(*it);
				}
			}));
		}, range, batch_size);
		return when_all(tasks);
	}

	/**
	 * Whether this dispatch queue uses threads for processing tasks.
	 */
	bool is_threaded() const;

	/**
	 * Number of threads used for processing tasks.
	 * This will be 0 in immediate mode.
	 */
	int thread_count() const;

	/**
	 * Returns the number of queued background tasks.
	 */
	size_t size() const;
	/**
	 * Returns the number of queued main loop tasks.
	 */
	size_t main_size() const;

	/**
	 * Returns whether there are no background tasks queued.
	 */
	bool empty() const;
	/**
	 * Returns whether there are no main loop tasks queued.
	 */
	bool main_empty() const;

	/**
	 * Cancel pending background tasks, clearing the current queue.
	 * Tasks that are being processed will still run to completion.
	 */
	void clear();
	/**
	 * Cancel pending main loop tasks, clearing the current queue.
	 */
	void main_clear();

	/**
	 * Invoke main loop tasks dispatched using `dispatch_main`.
	 * This should be called in your application's main loop.
	 *
	 * @param delta Delta time between last main loop and this one.
	 *              Pass a positive value to advance time and execute delayed tasks dispatched using `dispatch_main_after`.
	 */
	void main_loop(float delta = 0);

	/**
	 * Wait until all pending background tasks finish processing.
	 */
	void wait();

	/**
	 * Wait until all pending background tasks finish processing.
	 * Blocks until specified `timeout_duration` has elapsed or all queued tasks complete, whichever comes first.
	 * @returns `true` if all tasks finished processing, otherwise `false`.
	 */
	template<class Rep, class Period>
	bool wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		if (worker_pool) {
			return worker_pool->wait_for(timeout_duration);
		}
		else {
			return true;
		}
	}

	/**
	 * Wait until all pending background tasks finish processing.
	 * Blocks until the specified `timeout_time` has been reached or all queued tasks complete, whichever comes first.
	 * @returns `true` if all tasks finished processing, otherwise `false`.
	 */
	template<class Clock, class Duration>
	bool wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		if (worker_pool) {
			return worker_pool->wait_until(timeout_time);
		}
		else {
			return true;
		}
	}

	/**
	 * Cancel pending background tasks, wait and release the used threads.
	 * The queue now runs in immediate mode.
	 * It is safe to call this more than once.
	 */
	void shutdown();

#ifdef __cpp_lib_coroutine
private:
	struct dispatch_awaiter {
		task_dispatcher& dispatcher;

		bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> cont) const {
            dispatcher.dispatch([cont]{
				cont();
				if (cont.done()) {
					cont.destroy();
				}
			});
        }
        void await_resume() {}
	};

	struct dispatch_main_awaiter {
		task_dispatcher& dispatcher;
		float delay;

		bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> cont) const {
            dispatcher.dispatch_main_after(delay, [cont]{
				cont();
				if (cont.done()) {
					cont.destroy();
				}
			});
        }
        void await_resume() {}
	};

	struct dispatch_tagged_awaiter {
		task_dispatcher& dispatcher;
		task_tag tag;

		bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> cont) const {
            dispatcher.dispatch_tagged(tag, [cont]{
				cont();
				if (cont.done()) {
					cont.destroy();
				}
			});
        }
        void await_resume() {}
	};
public:
	/**
	 * Returns an awaiter that resumes a coroutine using `dispatch` when `co_await`ed.
	 *
	 * @code
	 * dispatch_queue::task<void> my_coroutine() {
	 *     co_await dispatcher.dispatch();
	 *     do_something_in_background();
	 * }
	 * @endcode
	 */
	dispatch_awaiter dispatch() {
		return dispatch_awaiter(*this);
	}

	/**
	 * Returns an awaiter that resumes a coroutine using `dispatch_main` when `co_await`ed.
	 *
	 * @code
	 * dispatch_queue::task<void> my_coroutine() {
	 *     co_await dispatcher.dispatch_main();
	 *     do_something_in_main_loop();
	 * }
	 * @endcode
	 */
	dispatch_main_awaiter dispatch_main() {
		return dispatch_main_awaiter(*this, 0);
	}

	/**
	 * Returns an awaiter that resumes a coroutine using `dispatch_main_after` when `co_await`ed.
	 *
	 * @code
	 * dispatch_queue::task<void> my_coroutine() {
	 *     co_await dispatcher.dispatch_main_after(1);
	 *     do_something_in_main_loop();
	 * }
	 * @endcode
	 */
	dispatch_main_awaiter dispatch_main_after(float delay) {
		return dispatch_main_awaiter(*this, delay);
	}

	/**
	 * Returns an awaiter that resumes a coroutine using `dispatch_tagged` when `co_await`ed.
	 *
	 * @code
	 * dispatch_queue::task<void> my_coroutine() {
	 *     co_await dispatcher.dispatch_tagged(tag);
	 *     do_something_with_tag();
	 * }
	 * @endcode
	 */
	dispatch_tagged_awaiter dispatch_tagged(task_tag tag) {
		return dispatch_tagged_awaiter(*this, tag);
	}
#endif // __cpp_lib_coroutine

private:
	std::unique_ptr<detail::worker_pool> worker_pool;
	detail::pending_task_queue task_queue;

	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_internal(detail::task_type type, task_tag tag, float delay, F&& f, Args&&... args) {
		auto work = std::bind(std::move(f), std::forward<Args>(args)...);
		if (worker_pool) {
			auto future = detail::task_future<Ret>::create_pending();
			worker_pool->enqueue_task(type, { future->wrap(work) }, delay, tag);
			return task<Ret>(future);
		}
		else if (type == detail::task_type::main) {
			auto future = detail::task_future<Ret>::create_pending();
			task_queue.push(type, { future->wrap(work) }, delay);
			return task<Ret>(future);
		}
		else {
			auto future = detail::task_future<Ret>::create(work);
			return task<Ret>(future);
		}
	}
};

} // end namespace dispatch_queue
