#pragma once

#include <future>
#include <functional>
#include <utility>

#include "function_result.hpp"
#include "task.hpp"
#include "promise.hpp"
#include "worker_pool.hpp"

namespace dispatch_queue {

class dispatch_queue {
public:
	/**
	 * Create a synchronous dispatch queue.
	 * In synchronous mode, tasks are executed immediately when calling `dispatch`.
	 */
	dispatch_queue();

	/**
	 * Initializes dispatch queue with `thread_count` background threads and a no-op `worker_init`.
	 * @see dispatch_queue(int, Fn&&)
	 */
	dispatch_queue(int thread_count);

	/**
	 * Initializes dispatch queue with `thread_count` background threads and a worker initialization functor.
	 *
	 * @param thread_count  Number of background threads used to run tasks.
	 *                      If 0, the dispatch queue is created in synchronous mode.
	 *                      If 1, tasks will run serially in background, one at a time, without any concurrency.
	 *                      Otherwise, `thread_count` threads will be created and tasks may run concurrently.
	 *                      Pass a negative number to use the default value of `std::thread::hardware_concurrency()` threads.
	 * @param worker_init  Functor called inside worker threads for initialization, receiving as argument the worker index.
	 *                     May be used to set the thread name, for example.
	 */
	template<typename Fn>
	dispatch_queue(int thread_count, Fn&& worker_init) {
		if (thread_count < 0) {
			thread_count = std::thread::hardware_concurrency();
		}
		if (thread_count > 0) {
			worker_pool = std::make_unique<detail::worker_pool>(task_queue, thread_count, worker_init);
		}
	}

	dispatch_queue(const dispatch_queue&) = delete;
	dispatch_queue& operator=(const dispatch_queue&) = delete;

	/**
	 * Calls `shutdown`.
	 */
	~dispatch_queue();

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args`.
	 * If the dispatch queue is in synchronous mode, the task is processed immediately in the calling thread.
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch(F&& f, Args&&... args) {
		return dispatch_internal(false, std::forward<F>(f), std::forward<Args>(args)...);
	}

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args` in main loop.
	 * Tasks dispatched with `dispatch_main` will only be executed when calling `main_loop`.
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_main(F&& f, Args&&... args) {
		return dispatch_internal(true, std::forward<F>(f), std::forward<Args>(args)...);
	}

	/**
	 * Whether this dispatch queue uses threads for processing tasks.
	 */
	bool is_threaded() const;

	/**
	 * Number of threads used for processing tasks.
	 * This will be 0 on synchronous mode.
	 */
	int thread_count() const;

	/**
	 * Returns the number of queued tasks;
	 */
	size_t size() const;

	/**
	 * Returns whether queue is empty, that is, there are no tasks queued.
	 */
	bool empty() const;

	/**
	 * Cancel pending tasks, clearing the current queue.
	 * Tasks that are being processed will still run to completion.
	 */
	void clear();

	/**
	 * Invoke main loop tasks dispatched using `dispatch_main`.
	 * This should be called in you application main loop.
	 */
	void main_loop();

	/**
	 * Wait until all pending tasks finish processing.
	 * @see std::future<T>::wait
	 */
	void wait();

	/**
	 * Wait until all pending tasks finish processing.
	 * Blocks until specified `timeout_duration` has elapsed or the result becomes available, whichever comes first.
	 * The return value indicates why `wait_for` returned.
	 * @see std::future<T>::wait_for
	 */
	template<class Rep, class Period>
	std::future_status wait_for(const std::chrono::duration<Rep, Period>& timeout_duration) {
		if (worker_pool) {
			return worker_pool->wait_for(timeout_duration);
		}
		else {
			return std::future_status::ready;
		}
	}

	/**
	 * Wait until all pending tasks finish processing.
	 * Blocks until the specified `timeout_time` has been reached or the result becomes available, whichever comes first.
	 * The return value indicates why `wait_until` returned.
	 * @see std::future<T>::wait_until
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
	 * Cancel pending tasks, wait and release the used Threads.
	 * The queue now runs in synchronous mode.
	 * It is safe to call this more than once.
	 */
	void shutdown();

#ifdef __cpp_lib_coroutine
private:
	struct dispatch_awaiter {
		dispatch_queue& dispatch_queue;

		bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> cont) const {
            dispatch_queue.dispatch([cont]{
				cont();
				if (cont.done()) {
					cont.destroy();
				}
			});
        }
        void await_resume() {}
	};

	struct dispatch_main_awaiter {
		dispatch_queue& dispatch_queue;

		bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> cont) const {
            dispatch_queue.dispatch_main([cont]{
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
	 */
	dispatch_awaiter dispatch() {
		return dispatch_awaiter(*this);
	}
	/**
	 * Returns an awaiter that resumes a coroutine using `dispatch_main` when `co_await`ed.
	 */
	dispatch_main_awaiter dispatch_main() {
		return dispatch_main_awaiter(*this);
	}
#endif

private:
	std::unique_ptr<detail::worker_pool> worker_pool;
	detail::pending_task_queue task_queue;

	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_internal(bool run_on_main_loop, F&& f, Args&&... args) {
		auto work = std::bind(std::move(f), std::forward<Args>(args)...);
		if (worker_pool) {
			auto future = detail::task_future<Ret>::create(task_state::pending);
			worker_pool->enqueue_task({ future->wrap(work) }, run_on_main_loop);
			return task<Ret>(future);
		}
		else if (run_on_main_loop) {
			auto future = detail::task_future<Ret>::create(task_state::pending);
			task_queue.push({ future->wrap(work) }, run_on_main_loop);
			return task<Ret>(future);
		}
		else {
			auto future = detail::task_future<Ret>::create(work);
			return task<Ret>(future);
		}
	}
};

} // end namespace dispatch_queue
