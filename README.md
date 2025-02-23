# Dispatch Queue
Dispatch Queue / Thread Pool implementation for C++11.


## Features
- No external dependencies: uses only C++11 threading functionality
- `dispatch` method with API similar to [std::async](https://en.cppreference.com/w/cpp/thread/async): receives a functor and its arguments, returns a `std::future`
- `dispatch_forget` for a "fire and forget" style call, which avoids the overhead of creating the `std::future`
- Supports both synchronous (immediate) and asynchronous (threaded) execution.
  + Asynchronous dispatch queues are also known as Thread Pools
  + Synchronous dispatch queues are useful for multiplatform code that must work on platforms without thread support, for example WebAssembly on browsers that lack `SharedArrayBuffer` support
- Single implementation file [src/dispatch_queue.cpp](src/dispatch_queue.cpp), easy to integrate in any project


## Usage example
```cpp
#include <dispatch_queue.hpp>

///////////////////////////////////////////////////////////
// 1. Create a dispatch queue
///////////////////////////////////////////////////////////

// Default constructed dispatch queues are synchronous.
// They execute tasks immediately in the same thread that called `dispatch`.
dispatch_queue::dispatch_queue synchronous_dispatcher;
// Dispatch queues with 0 threads are also synchronous.
dispatch_queue::dispatch_queue synchronous_dispatcher2(0);

// A dispatch queue with 1 thread is called a serial queue:
// it only runs a single task at a time.
dispatch_queue::dispatch_queue serial_dispatcher(1);

// A dispatch queue with more than 1 thread runs tasks concurrently.
dispatch_queue::dispatch_queue concurrent_dispatcher(4);

// Pass a negative value to use the default thread count.
// Current default is `std::thread::hardware_concurrency`.
dispatch_queue::dispatch_queue concurrent_dispatcher2(-1);


///////////////////////////////////////////////////////////
// 2. Dispatch some tasks!
///////////////////////////////////////////////////////////

// Use the returned future to get results or wait for completion.
auto task = []() { return 42; };
std::future<int> async_result = dispatcher.dispatch(task);
assert(async_result.get() == 42);

// Pass arguments to forward to task
auto task2 = [](int value) { return value; };
std::future<int> async_result2 = dispatcher.dispatch(task2, 2);
assert(async_result2.get() == 2);

// For "fire and forget" calls, use `dispatch_forget`.
// This avoids the overhead of creating `std::future`.
dispatcher.dispatch_forget(task);
dispatcher.dispatch_forget(task2, 2);


///////////////////////////////////////////////////////////
// 3. Check some stats
///////////////////////////////////////////////////////////

int dispatcher_thread_count = dispatcher.thread_count();
bool dispatcher_is_threaded = dispatcher.is_threaded();
int pending_task_count = dispatcher.size();
bool has_no_pending_tasks = dispatcher.empty();


///////////////////////////////////////////////////////////
// 4. Other operations
///////////////////////////////////////////////////////////

// Cancel all pending tasks.
// Tasks already executing will still run to completion.
dispatcher.clear();

// Wait until pending tasks are completed
dispatcher.wait();
// Wait until pending tasks are completed, with timeout
dispatcher.wait_for(std::chrono::seconds(5));
dispatcher.wait_until(std::chrono::system_clock::now() + std::chrono::seconds(5));
```


## Using in CMake projects
Add this project using `add_subdirectory` and link your target to `dispatch_queue`:
```cmake
add_subdirectory(path/to/dispatch_queue)
target_link_libraries(my_target dispatch_queue)
```


## Setting thread names for debugging
You can pass a functor to the dispatch queue constructor that will run inside worker threads when they initialize.
There you can set thread names:
```cpp
dispatch_queue::dispatch_queue dispatcher(4, [](int worker_index) {
    std::string worker_name = std::format("worker{}", worker_index);
    // TODO: set thread name, platform-specific
});
```
