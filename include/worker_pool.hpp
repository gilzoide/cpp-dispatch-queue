#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace dispatch_queue {

namespace detail {

class worker_pool {
public:
	worker_pool(int thread_count, std::deque<std::function<void()>>& task_queue);
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

	static void thread_entrypoint(worker_pool *pool);
};

} // end namespace detail

}
