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
	dispatch_queue();
	dispatch_queue(int thread_count);
	~dispatch_queue();

	template<typename F, typename... Args>
	std::future<detail::function_result<F, Args...>> dispatch(F&& f, Args&&... args) {
		using function_result = detail::function_result<F, Args...>;
		if (worker_pool) {
			std::function<function_result()> task = std::bind(std::move(f), std::forward<Args>(args)...);
			auto packaged_task = std::make_shared<std::packaged_task<function_result()>>(task);
			worker_pool->enqueue_task([=]() {
				(*packaged_task.get())();
			});
			return packaged_task->get_future();
		}
		else {
			std::promise<function_result> promise;
			promise.set_value(f(std::forward<Args>(args)...));
			return promise.get_future();
		}
	}

	template<typename F, typename... Args>
	void dispatch_and_forget(F&& f, Args&&... args) {
		if (worker_pool) {
			worker_pool->enqueue_task(std::bind(std::move(f), std::forward<Args>(args)...));
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
