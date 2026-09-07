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
- Use `dispatch_queue.dispatch_tagged(tag, f, args...)` to dispatch tagged tasks
  + Tasks tagged with the same value never run in parallel: at most one task is processed for each tag at a time.
    Use this to serialize different task types without having to create separate dispatch queues.
- Returned `dispatch_queue::task<T>` from dispatch methods are similar to `std::shared_future`, with the following additions:
  + Use `task.get_state()` to get whether task is invalid, pending, ready or failed with exception
  + Use `task.then(f)` to add a continuation function that runs when task finishes
  + Use `task.get_exception()` to get stored `exception_ptr`
  + Use `task<T>::create_ready(T)` to create a finished task that in the ready state
  + Use `task<T>::create_failed(e)` to create a finished task that in the failed state
  + Use `task<T>::create_pending()` to create a pending task that can be finished using `task.set_value(T)` and `task.set_exception(e)`
- Use `dispatch_queue::when_all(tasks...)` to get a task that finishes when all the passed tasks finish
- Use `dispatch_queue::when_any(tasks...)` to get a task that finishes when any of the passed tasks finish
- Use `dispatch_queue::parallel_for(f, begin, end, batch_size)` or `dispatch_queue::parallel_for(f, range, batch_size)` to process ranges in parallel
- Built-in C++20 coroutine support
  + Use `dispatch_queue::task<T>` as the return value for your coroutines
  + `co_await` other tasks to resume the coroutine as the task's continuation
  + Use `co_await dispatch_queue.dispatch()` to continue coroutine in a dispatch queue's background loop
  + Use `co_await dispatch_queue.dispatch_main()` to continue coroutine in a dispatch queue's main loop
  + Use `co_await dispatch_queue.dispatch_tagged()` to continue coroutine in a dispatch queue's background loop using the provided tag
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

// Also pass name prefix to customize thread names.
// In this example, threads will be named "worker-N".
// Defaults to "dispatch_queueN".
dispatch_queue::dispatch_queue concurrent_dispatcher3(-1, "worker-");


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

// Queue tagged tasks
// Tasks tagged with the same value never run in parallel: at most one task is processed for each tag at a time.
enum TaskTags {
    SAVE_FILE_IO,
};
// The three following tasks will run one at a time, even if dispatch queue has more idle threads
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });


///////////////////////////////////////////////////////////
// 3. Aggregating tasks
///////////////////////////////////////////////////////////

// `all_task` will be finished only after all passed tasks are finished
auto all_task = dispatch_queue::when_all(
    dispatcher.dispatch([](){ /* ... */ }),
    dispatcher.dispatch([](){ /* ... */ }),
    dispatcher.dispatch([](){ /* ... */ }),
    dispatcher.dispatch([](){ /* ... */ })
);
all_task.wait();

// `any_task` will be finished after any of the passed tasks finish
auto any_task = dispatch_queue::when_any(
    dispatcher.dispatch([](){ /* ... */ }),
    dispatcher.dispatch([](){ /* ... */ }),
    dispatcher.dispatch([](){ /* ... */ }),
    dispatcher.dispatch([](){ /* ... */ })
);
any_task.wait();


///////////////////////////////////////////////////////////
// 4. Tasks as promise
///////////////////////////////////////////////////////////

// Tasks can be used as promises in async code even without dispatch queues
dispatch_queue::task<void> promise = dispatch_queue::task<void>::create_pending();
auto on_success = [=](){ promise.set_value(); };
auto on_failure = [=](){ promise.set_exception(std::make_exception_ptr(std::runtime_error("error"))); };
do_something_async_with_callback(on_success, on_failure);


///////////////////////////////////////////////////////////
// 5. Built-in C++20 coroutine support
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

    // co_await .dispatch_tagged(tag)
    // coroutine continues within dispatch queue using the provided tag
    co_await dispatcher.dispatch_tagged(SAVE_FILE_IO);
    do_something_in_main_loop();
}


///////////////////////////////////////////////////////////
// 6. Check some stats
///////////////////////////////////////////////////////////

int dispatcher_thread_count = dispatcher.thread_count();
bool dispatcher_is_threaded = dispatcher.is_threaded();
int pending_task_count = dispatcher.size();
bool has_no_pending_tasks = dispatcher.empty();


///////////////////////////////////////////////////////////
// 7. Other operations
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
