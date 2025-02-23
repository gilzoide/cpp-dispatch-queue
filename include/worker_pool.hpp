#ifndef __DISPATCH_QUEUE_WORKER_POOL_HPP__
#define __DISPATCH_QUEUE_WORKER_POOL_HPP__

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace dispatch_queue {

namespace detail {

class worker_pool {
public:
	template<typename Fn>
	worker_pool(int thread_count, std::deque<std::function<void()>>& task_queue, Fn&& worker_init)
		: task_queue(task_queue)
		, worker_threads()
	{
		worker_threads.reserve(thread_count);
		for (int i = 0; i < thread_count; i++) {
			worker_threads.emplace_back([&, this, i]() {
				worker_init(i);
				run_task_loop();
			});
		}
	}
	~worker_pool();

	worker_pool(const worker_pool&) = delete;
	worker_pool& operator=(const worker_pool&) = delete;

	int thread_count() const;
	size_t size();

	void enqueue_task(std::function<void()>&& task);
	void clear();
	void shutdown();

private:
	std::mutex mutex;
	std::condition_variable condition_variable;
	std::vector<std::thread> worker_threads;
	std::deque<std::function<void()>>& task_queue;
	bool is_shutting_down;

	void run_task_loop();
};

} // end namespace detail

}

#endif  // __DISPATCH_QUEUE_WORKER_POOL_HPP__
