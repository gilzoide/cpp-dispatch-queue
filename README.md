# Dispatch Queue
Dispatch Queue / Thread Pool implementation for C++11 with built-in C++20 coroutine support.


## Features
- No external dependencies: uses only the C++ STL
- Supports both immediate and threaded execution modes:
  + Threaded dispatch queues are also known as Thread Pools.
    In threaded mode it is safe to dispatch new tasks from any thread.
  + In immediate mode tasks are executed immediately. Useful for multiplatform code that must work on platforms without thread support, for example WebAssembly on browsers that lack `SharedArrayBuffer` support.
- Use `dispatch_queue.dispatch(f, args...)` to dispatch new tasks
- Use `dispatch_queue.dispatch_main(f, args...)` to dispatch "main loop" tasks
  + Users must call `dispatch_queue.main_loop()` manually where appropriate to run queued main loop tasks
  + Useful for synchronizing state calculated in background tasks with the application's main loop
- Returned `dispatch_queue::task<T>` from dispatch methods are similar to `std::shared_future`, with the following additions:
  + Use `task.get_state()` to get whether task is pending, ready or failed with exception
  + Use `task.then(f)` to add a continuation function that runs when task finishes
  + Use `task.get_exception()` to get stored exception_ptr
- Built-in C++20 coroutine support
  + Use `dispatch_queue::task<T>` as the return value for your coroutines
  + `co_await` other tasks to resume the coroutine as the task's continuation
  + Use `co_await dispatch_queue.dispatch()` to continue coroutine in a dispatch queue's background loop
  + Use `co_await dispatch_queue.dispatch_main()` to continue coroutine in a dispatch queue's main loop
- Supports compiling with `-fno-exceptions` and `-fno-rtti`
- Unified implementation file [src/dispatch_queue-one.cpp](src/dispatch_queue-one.cpp), easy to integrate in any project


## Usage example
```cpp
#include <dispatch_queue.hpp>

///////////////////////////////////////////////////////////
// 1. Create a dispatch queue
///////////////////////////////////////////////////////////

// Default constructed dispatch queues are immediate.
// They execute tasks immediately in the call to `dispatch`.
dispatch_queue::dispatch_queue immediate_dispatcher;
// Dispatch queues with 0 threads are also immediate.
dispatch_queue::dispatch_queue immediate_dispatcher2(0);

// A dispatch queue with 1 thread is a serial queue:
// it runs a single task at a time in its background thread.
dispatch_queue::dispatch_queue serial_dispatcher(1);

// A dispatch queue with more than 1 thread runs tasks concurrently.
dispatch_queue::dispatch_queue concurrent_dispatcher(4);

// Pass a negative value to use the default thread count.
// Current default is `std::thread::hardware_concurrency`.
dispatch_queue::dispatch_queue concurrent_dispatcher2(-1);


///////////////////////////////////////////////////////////
// 2. Dispatch some tasks!
///////////////////////////////////////////////////////////

// Use the returned task to get results or wait for completion.
auto work = []{ return 42; };
dispatch_queue::task<int> task = dispatcher.dispatch(work);
assert(task.get() == 42);

// Pass arguments to forward to task
auto work2 = [](int value) { return value; };
dispatch_queue::task<int> task2 = dispatcher.dispatch(work2, 2);
assert(task2.get() == 2);

// Use `then` for adding continuations
dispatch_queue::task<void> continued_task = dispatcher.dispatch(work)
    // continuations receive the finished task
    .then([](dispatch_queue::task<int> task) {
        if (std::exception_ptr exception = task.get_exception()) {
            // task failed with an exception...
            std::rethrow_exception(exception);
        }
        else {
            // task succeeded!
            int result = task.get();
            return (float) result;
        }
    })
    // .then() return a new task, so you can chain continuations
    .then([&](dispatch_queue::task<float> task) {
        return dispatcher.dispatch(work2);
    })
    // .then() unwraps task<task<T>> if C++20 concepts are available
    .then([](dispatch_queue::task<int> task) {
        return;
    });
continued_task.wait();

// Queue "main loop" tasks that will be executed by calling `main_loop()`
dispatcher.dispatch_main([]{
    std::cout << "This will run inside the call to `main_loop`" << std::endl;
});
while (!ApplicationShouldExit()) {
    // Inside your application's main loop...
    dispatcher.main_loop();
}


///////////////////////////////////////////////////////////
// 3. Built-in C++20 coroutine support
///////////////////////////////////////////////////////////

// Use dispatch_queue::task<T> as return value for coroutines
dispatch_queue::task<void> my_coro() {
    // co_await other tasks
    // coroutine becomes task's continuation via .then()
    co_await dispatcher.dispatch(some_work);
    do_something_after_some_work_finished();

    // co_await .dispatch()
    // coroutine continues within dispatch queue
    co_await dispatcher.dispatch();
    do_something_in_background();

    // co_await .dispatch_main()
    // coroutine continues within dispatch queue's main loop
    co_await dispatcher.dispatch_main();
    do_something_in_main_loop();
}


///////////////////////////////////////////////////////////
// 4. Check some stats
///////////////////////////////////////////////////////////

int dispatcher_thread_count = dispatcher.thread_count();
bool dispatcher_is_threaded = dispatcher.is_threaded();
int pending_task_count = dispatcher.size();
bool has_no_pending_tasks = dispatcher.empty();


///////////////////////////////////////////////////////////
// 5. Other operations
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
add_subdirectory("path/to/dispatch_queue")
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
