#pragma once

#include <functional>
#include <utility>

#include "detail/function_result.hpp"
#include "detail/pending_task_queue.hpp"
#include "detail/promise.hpp"
#include "detail/worker_pool.hpp"
#include "task.hpp"

namespace dispatch_queue {

class dispatch_queue {
public:
	/**
	 * Create an immediate dispatch queue.
	 * In immediate mode, tasks are executed immediately when calling `dispatch`.
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
	 *                      If 0, the dispatch queue is created in immediate mode.
	 *                      If 1, tasks will run serially in background, one at a time, without any concurrency.
	 *                      Otherwise, `thread_count` threads will be created and tasks may run concurrently.
	 *                      Pass a negative number to use the default value of `std::thread::hardware_concurrency()` threads.
	 * @param worker_init  Functor called inside worker threads for initialization, receiving as argument the worker index.
	 *                     May be used to set the thread name or initialize thread local variables, for example.
	 */
	template<typename Fn>
	dispatch_queue(int thread_count, Fn&& worker_init) {
		if (thread_count < 0) {
			thread_count = std::thread::hardware_concurrency();
		}
		if (thread_count > 0) {
			worker_pool = std::make_unique<detail::worker_pool>(task_queue, thread_count, std::move(worker_init));
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
	 * If the dispatch queue is in immediate mode, the task is processed immediately in the calling thread.
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch(F&& f, Args&&... args) {
		return dispatch_internal(detail::task_type::background, detail::NULL_TAG, std::forward<F>(f), std::forward<Args>(args)...);
	}

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args` in main loop.
	 * Tasks dispatched with `dispatch_main` will only be executed when calling `main_loop`.
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 * @see main_loop
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_main(F&& f, Args&&... args) {
		return dispatch_internal(detail::task_type::main, detail::NULL_TAG, std::forward<F>(f), std::forward<Args>(args)...);
	}

	/**
	 * Dispatch a tagged task that calls `f` with forwarded arguments `args`.
	 * Tasks tagged with the same value never run in parallel: at most one task is processed for each tag at a time.
	 * Use this to serialize different task types without having to create separate dispatch queues.
	 * If the dispatch queue is in immediate mode, the task is processed immediately in the calling thread.
	 * @param tag The tag associated to the task
	 * @param f Functor to be executed
	 * @param args Arguments forwarded to `f`
	 * @returns Future for getting `f` result.
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_tagged(int tag, F&& f, Args&&... args) {
		return dispatch_internal(detail::task_type::tagged, tag, std::forward<F>(f), std::forward<Args>(args)...);
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
	 * Returns the number of queued tasks.
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
	 * This should be called in your application's main loop.
	 */
	void main_loop();

	/**
	 * Wait until all pending tasks finish processing.
	 */
	void wait();

	/**
	 * Wait until all pending tasks finish processing.
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
	 * Wait until all pending tasks finish processing.
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
	 * Cancel pending tasks, wait and release the used threads.
	 * The queue now runs in immediate mode.
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

	struct dispatch_tagged_awaiter {
		dispatch_queue& dispatch_queue;
		int tag;

		bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> cont) const {
            dispatch_queue.dispatch_tagged(tag, [cont]{
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
	 *     co_await dispatch_queue.dispatch();
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
	 *     co_await dispatch_queue.dispatch_main();
	 *     do_something_in_main_loop();
	 * }
	 * @endcode
	 */
	dispatch_main_awaiter dispatch_main() {
		return dispatch_main_awaiter(*this);
	}

	/**
	 * Returns an awaiter that resumes a coroutine using `dispatch_tagged` when `co_await`ed.
	 *
	 * @code
	 * dispatch_queue::task<void> my_coroutine() {
	 *     co_await dispatch_queue.dispatch_tagged(tag);
	 *     do_something_with_tag();
	 * }
	 * @endcode
	 */
	dispatch_tagged_awaiter dispatch_tagged(int tag) {
		return dispatch_tagged_awaiter(*this, tag);
	}
#endif

private:
	std::unique_ptr<detail::worker_pool> worker_pool;
	detail::pending_task_queue task_queue;

	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_internal(detail::task_type type, int tag, F&& f, Args&&... args) {
		auto work = std::bind(std::move(f), std::forward<Args>(args)...);
		if (worker_pool) {
			auto future = detail::task_future<Ret>::create_pending();
			worker_pool->enqueue_task(type, { future->wrap(work) }, tag);
			return task<Ret>(future);
		}
		else if (type == detail::task_type::main) {
			auto future = detail::task_future<Ret>::create_pending();
			task_queue.push(type, { future->wrap(work) });
			return task<Ret>(future);
		}
		else {
			auto future = detail::task_future<Ret>::create(work);
			return task<Ret>(future);
		}
	}
};

} // end namespace dispatch_queue
