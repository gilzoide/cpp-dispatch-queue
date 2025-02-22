#ifndef __DISPATCH_QUEUE_HPP__
#define __DISPATCH_QUEUE_HPP__

#include <deque>
#include <future>
#include <functional>
#include <utility>

#include "function_result.hpp"
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
	 * If `thread_count` is 0, the dispatch queue is created in synchronouse mode.
	 * Otherwise, the dispatch queue is created in asynchronous mode with `thread_count` threads.
	 * Pass 1 to create a serial dispatch queue, where tasks are processed in a background thread,
	 * but only one at a time without any concurrency.
	 */
	dispatch_queue(int thread_count);

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
	std::future<Ret> dispatch(F&& f, Args&&... args) {
		std::function<Ret()> task = std::bind(std::move(f), std::forward<Args>(args)...);
		if (worker_pool) {
			auto packaged_task = std::make_shared<std::packaged_task<Ret()>>(task);
			worker_pool->enqueue_task([=]() {
				(*packaged_task)();
			});
			return packaged_task->get_future();
		}
		else {
			std::packaged_task<Ret()> packaged_task(task);
			packaged_task();
			return packaged_task.get_future();
		}
	}

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args`.
	 * If the dispatch queue is in synchronous mode, the task is processed immediately in the calling thread.
	 * Contrary to `dispatch`, there's no way to get the result of the call or know when the task is finished.
	 * Use this for "fire and forget" flows, benefiting of reduced overhead.
	 */
	template<typename F, typename... Args>
	void dispatch_forget(F&& f, Args&&... args) {
		if (worker_pool) {
			worker_pool->enqueue_task(std::bind(std::move(f), std::forward<Args>(args)...));
		}
		else {
			f(std::forward<Args>(args)...);
		}
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
		auto future = dispatch([](){});
		return future.wait_for(timeout_duration);
	}

	/**
	 * Wait until all pending tasks finish processing.
	 * Blocks until the specified `timeout_time` has been reached or the result becomes available, whichever comes first.
	 * The return value indicates why `wait_until` returned.
	 * @see std::future<T>::wait_until
	 */
	template<class Clock, class Duration>
	std::future_status wait_until(const std::chrono::time_point<Clock, Duration>& timeout_time) {
		auto future = dispatch([](){});
		return future.wait_until(timeout_time);
	}

	/**
	 * Cancel pending tasks, wait and release the used Threads.
	 * The queue now runs in synchronous mode, so that new tasks will run immediately.
	 * It is safe to call this more than once.
	 */
	void shutdown();

private:
	detail::worker_pool *worker_pool;
	std::deque<std::function<void()>> task_queue;
};

}

#endif  // __DISPATCH_QUEUE_HPP__
