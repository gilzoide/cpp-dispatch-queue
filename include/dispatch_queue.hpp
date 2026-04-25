#pragma once

#include <future>
#include <functional>
#include <utility>

#include "function_result.hpp"
#include "pending_task.hpp"
#include "task.hpp"
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
	 * @note If you don't need the returned future, prefer using `dispatch_forget` instead
	 *       to avoid the overhead of creating shared state.
	 * @returns Future for getting `f` result.
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch(F&& f, Args&&... args) {
		return dispatch_internal(0, std::forward<F>(f), std::forward<Args>(args)...);
	}

	template<typename F, typename... Args, typename TaskRet, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_after(const task<TaskRet>& t, F&& f, Args&&... args) {
		return dispatch_internal(t.get_id(), std::forward<F>(f), std::forward<Args>(args)...);
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
			auto future = dispatch([](){});
			return future.wait_for(timeout_duration);
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
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		if (worker_pool) {
			auto future = dispatch([](){});
			return future.wait_until(timeout_time);
		}
		else {
			return std::future_status::ready;
		}
	}

	/**
	 * Cancel pending tasks, wait and release the used Threads.
	 * The queue now runs in synchronous mode, so that new tasks will run immediately.
	 * It is safe to call this more than once.
	 */
	void shutdown();

private:
	std::unique_ptr<detail::worker_pool> worker_pool;
	detail::pending_task_queue task_queue;

	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	task<Ret> dispatch_internal(task_id dependency, F&& f, Args&&... args) {
		auto work = std::bind(std::move(f), std::forward<Args>(args)...);
		if (worker_pool) {
			auto future = detail::task_future<Ret>::create(task_state::pending);
			worker_pool->enqueue_task({ future->get_id(), future->wrap(work) }, dependency);
			return task<Ret>(future);
		}
		else if (pending_task* dependency_task = task_queue.find(dependency)) {
			auto future = detail::task_future<Ret>::create(task_state::pending);
			task_queue.push({ future->get_id(), future->wrap(work) }, dependency);
			return task<Ret>(future);
		}
		else {
			auto future = detail::task_future<Ret>::create(work);
			return task<Ret>(future);
		}
	}
};

} // end namespace dispatch_queue
