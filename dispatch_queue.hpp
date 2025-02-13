#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace dispatch_queue {

namespace detail {

#ifdef __cpp_lib_is_invocable
template<class F, class... ArgTypes>
using function_result = typename std::invoke_result<F, ArgTypes...>::type;
#else
template<class F, class... ArgTypes>
using function_result = typename std::result_of<F(ArgTypes...)>::type;
#endif

class worker_pool {
public:
	worker_pool(int thread_count, std::deque<std::function<void()>>& task_queue);
	~worker_pool();

	int thread_count() const;
	void enqueue_task(std::function<void()>&& task);
	void clear();
	void shutdown();

private:
	std::mutex mutex;
	std::condition_variable condition_variable;
	std::vector<std::thread> worker_threads;
	std::deque<std::function<void()>>& task_queue;
	bool is_shutting_down;

	static void thread_entrypoint(worker_pool *pool);
};

} // end namespace detail


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

	/**
	 * Calls `shutdown`.
	 */
	~dispatch_queue();

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args`.
	 * If the dispatch queue is in synchronous mode, the task is processed right immediately.
	 * @note If you don't need the returned future, prefer using `dispatch_forget` instead
	 *       to avoid the overhead of creating shared state.
	 * @returns Future for getting `f` result.
	 */
	template<typename F, typename... Args, typename Ret = detail::function_result<F, Args...>>
	std::future<Ret> dispatch(F&& f, Args&&... args) {
		if (worker_pool) {
			std::function<Ret()> task = std::bind(std::move(f), std::forward<Args>(args)...);
			auto packaged_task = std::make_shared<std::packaged_task<Ret()>>(task);
			worker_pool->enqueue_task([=]() {
				(*packaged_task)();
			});
			return packaged_task->get_future();
		}
		else {
			std::promise<Ret> promise;
			promise.set_value(f(std::forward<Args>(args)...));
			return promise.get_future();
		}
	}

	/**
	 * Dispatch a task that calls `f` with forwarded arguments `args`.
	 * If the dispatch queue is in synchronous mode, the task is processed right immediately.
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
	 * Cancel pending tasks, clearing the current queue.
	 * Tasks that are being processed will still run to completion.
	 */
	void clear();

	/**
	 * Cancel pending tasks, wait and release the used Threads.
	 * The queue now runs in synchronous mode, so that new tasks will run in the main thread.
	 * It is safe to call this more than once.
	 */
	void shutdown();

private:
	detail::worker_pool *worker_pool;
	std::deque<std::function<void()>> task_queue;
};

}
