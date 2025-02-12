#include <condition_variable>
#include <deque>
#ifndef DISPATCH_QUEUE_NOEXCEPT
#include <exception>
#endif
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

	template<typename... Args>
	void emplace_task(Args&&... args) {
		std::function<void()> task(args...);
		queue_task(std::move(task));
	}

	int thread_count() const;
	void queue_task(std::function<void()>&& task);
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
	dispatch_queue();
	dispatch_queue(int thread_count);
	~dispatch_queue();

	template<typename F, typename... Args>
	std::future<detail::function_result<F, Args...>> dispatch(F&& f, Args&&... args) {
		using function_result = detail::function_result<F, Args...>;
		std::promise<function_result> promise;
		std::future<function_result> future = promise.get_future();
		if (worker_pool) {
			worker_pool->emplace_task(std::bind([](F&& f, std::promise<function_result>&& promise, Args&&... args) {
#ifndef DISPATCH_QUEUE_NOEXCEPT
				try {
					promise.set_value(f(std::forward<Args>(args)...));
				}
				catch (...) {
					try {
						promise.set_exception(std::current_exception());
					}
					catch (...) {}
				}
#else
				promise.set_value(f(std::forward<Args>(args)...));
#endif
			}, std::move(f), std::move(promise), std::forward<Args>(args)...));
		}
		else {
			promise.set_value(f(std::forward<Args>(args)...));
		}
		return future;
	}

	template<typename F, typename... Args>
	void dispatch_and_forget(F&& f, Args&&... args) {
		if (worker_pool) {
			worker_pool->emplace_task(std::bind(std::move(f), std::forward<Args>(args)...));
		}
		else {
			f(std::forward<Args>(args)...);
		}
	}

	bool is_threaded() const;
	int thread_count() const;
	void clear();
	void shutdown();

private:
	detail::worker_pool *worker_pool;
	std::deque<std::function<void()>> task_queue;
};

}
