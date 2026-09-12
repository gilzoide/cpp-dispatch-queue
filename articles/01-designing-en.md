# Designing Dispatch Queue
> 2026-09-12 | `#C++ #Multithreading #ThreadPool` | [*Versão em Português*](01-designing-pt.md)

## Context
I've been working with games for quite some time now and in the last couple of years I got to work in some native games that use C++ for their engines.
All of them have a thread pool / task dispatch queue implementation of some sort, although some projects use multithreading more (and wiser) than others.
Multithreading is hard, but it can certainly be simpler with some nice abstractions.

I've also been using Unity for quite some time and I really like C#'s [async/await](https://learn.microsoft.com/en-us/dotnet/csharp/asynchronous-programming/task-asynchronous-programming-model) and the [Task Parallel Library](https://learn.microsoft.com/en-us/dotnet/standard/parallel-programming/task-parallel-library-tpl).
[Unity's Job System](https://docs.unity3d.com/Manual/job-system-overview.html) is also nice, although a bit harder to work with because of the hard requirement on using only unmanaged types.

So the idea behind this project was to try implementing a robust multithreaded task dispatcher with a really nice API that would:

1. Depend only in C++ STL, using C++11 threading primitives and avoid platform-dependent code as much as possible.
2. Support nice C++ features such as [lambdas](https://en.cppreference.com/cpp/language/lambda) and C++20 [coroutines](https://en.cppreference.com/cpp/language/coroutines).
3. Allow for some of the async code style that I use in Unity / C# and really like, for example having functionality similar to what [`Task.WhenAll`](https://learn.microsoft.com/en-us/dotnet/api/system.threading.tasks.task.whenall) and [`TaskCompletionSource`](https://learn.microsoft.com/en-us/dotnet/api/system.threading.tasks.taskcompletionsource) provide.
4. Be app engine agnostic: no matter if using Cocos2d, Raylib, SDL or something else entirely, Dispatch Queue is still useful.
5. Be compilable and usable in [Emscripten](https://emscripten.org/) web builds.

During the development, I ended up adding some more design choices that I'll discuss below.


## Task interface
One implementation I found in a project required you to make a subclass for each different task you wanted to use.
Unity Job System and other implementations also use that approach.

I feel the "C++ way" would be to use [functors](https://en.cppreference.com/cpp/utility/functional) and/or [`std::function`](https://en.cppreference.com/cpp/utility/functional/function), though.
Requiring users to subclass a task interface just for a `void Execute()` method seems like too much boilerplate to me, and I wanted an API that is as simple as possible while still very effective.
Supporting lambdas is a big win in terms of usability in my opinion.

Look at C++ [`std::async`](https://en.cppreference.com/cpp/thread/async) for example, you pass a functor and optional arguments and it returns a [`std::future`](https://en.cppreference.com/cpp/thread/future) that you can use to wait for and get the result afterwards.
Simple, powerful, awesome!

Here's how dispatching a task looks like in Dispatch Queue:
```cpp
task_dispatcher dispatcher;

dispatcher.dispatch([]() {
    std::cout << "Hello world!" << std::endl;
});
```


## Future
Some things really bother me in `std::future`, though:

1. The only way to check if the future value is ready is to use [`wait_for`](https://en.cppreference.com/cpp/thread/future/wait_for) with a zero timeout.
   Something like a `future.is_ready()` method would be better in terms of API in my opinion.
2. The only way to check for exceptions is to call [`get`](https://en.cppreference.com/cpp/thread/future/get) inside a try/catch block, there's no check like `future.is_failed()` or `future.get_exception()`.
3. It lacks a way to run code right after the future is ready, something akin to C#'s [`Task.ContinueWith`](https://learn.microsoft.com/en-us/dotnet/api/system.threading.tasks.task.continuewith).

I started the implementation using `std::future` and `std::packaged_task`, but ended up implementing my own [`task_future<T>`](../include/dispatch_queue/detail/task_future.hpp) that fix the points above.


```cpp
task<void> t1 = dispatcher.dispatch([]() {
    std::cout << "Hello world!" << std::endl;
});

// Check state with `task.get_state()`
if (t1.get_state() == task_state::pending) {
   std::cout << "Hasn't finished yet" << std::endl;
}

// Add continuation with `task.then()`
t1.then([](const task<void>& t) {
    if (t.get_state() == task_state::ready) {
        std::cout << "Success =D" << std::endl;
    }
    else {  // task_state::failed
        std::cout << "Failure =(" << std::endl;
    }
})
```


## Promise
I mentioned C#'s `TaskCompletionSource` above, let me explain further: it's a way for users to complete tasks "manually".
It's really useful to convert methods receiving callbacks into async/await-ready code.

In Dispatch Queue, I decided adding this feature into the existing `task<T>` class instead of creating a separate `task_source<T>` class, mainly for simplicity.
```cpp
// Using tasks as promise
task<int> promise = task<int>::create_pending();
do_something_async_with_callback([promise](int result) {
    promise.set_value(result);
});

int result = promise.get();
```


## Aggregate tasks
It's really useful being able to wait for the completion of several tasks at once.
That's what `when_all` is for:
```cpp
// Initialize several game systems concurrently
when_all(
    dispatcher.dispatch(initialize_graphics),
    dispatcher.dispatch(initialize_audio),
    dispatcher.dispatch(initialize_input),
    dispatcher.dispatch(initialize_analytics_service),
    dispatcher.dispatch(load_saved_game)
).then([](const task<void>& t) {
    // After all of them are done, start the game
    start_game();
})
```


## Synchronizing with the main loop
Another really common pattern in interactive applications / games is dispatching a task in some background thread and, after it's finished, using its result in the main thread.

In Unity, as well as Cocos2d and other engines, some APIs are not thread-safe and can only be used in the main thread, such as updating graphics and adding objects to the scene tree.

Reading and writing save files, waiting for HTTP requests and parsing big JSON or XML configuration files are some examples of potentially long tasks that you'll want backgrounded to avoid freezing frames.

Additional design choice:
- Be able to dispatch tasks to the app's main thread

```cpp
dispatcher.dispatch_main([]() {
    std::cout << "In main loop" << std::endl;
});

void app_main_loop() {
    // Call in your app's main loop...
    dispatcher.main_loop();
}
```


## Running tasks after delay
Another really common pattern is executing tasks after some time.
I saw this in some implementations and thought "why not?", even though I haven't personally used this one yet in sample projects 😅

Additional optional design choice:

- Be able to dispatch tasks to the app's main thread with a delay

```cpp
float delay_secs = 1;
dispatcher.dispatch_main_after(delay_secs, []() {
    std::cout << "In main loop after 1s" << std::endl;
});

void app_main_loop(float dt) {
    // Call in your app's main loop...
    // Requires delta time to advance inner timer
    dispatcher.main_loop(dt);
}
```


## Task pinning
This one is not that common but quite interesting.
So another pattern that exists is allowing some tasks to be pinned to specific threads.
This is useful to serialize tasks, for example saving files in a way that they are never accessed concurrently, avoiding corrupting them.
This is also useful if the task needs some thread-local state to be preserved between executions.

Running some tasks always in the same thread can be quite easily done by creating separate task dispatchers with a single thread specifically to run them.

So I chose a slightly different option: instead of pinning tasks to specific threads, just ensure that tasks using the same tag (an integer value) are guaranteed to never run concurrently.
This allows for tagged tasks to be run by any of the pooled threads.
Users that need tasks run in specific threads can create separate serial dispatchers to run them instead.

```cpp
enum TaskTags {
    SAVE_FILE_IO,
};

// The three following tasks will run one at a time,
// even if dispatcher has more idle threads
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });
dispatcher.dispatch_tagged(SAVE_FILE_IO, [](){ /* ... */ });
```


## C++20 coroutines
So, C++20 introduces coroutines, which is an awesome mechanism for suspending and resuming functions asynchronously, similar to C#'s (and Python's and JavaScript's) async/await.

In C++, though, there's no default runtime for this suspension and resuming of functions, we need to provide some implementation for it to work.

So I used Dispatch Queue's [`task_dispatcher`](../include/dispatch_queue/task_dispatcher.hpp) and [`task`](../include/dispatch_queue/task.hpp) for the coroutine runtime, allowing code such as the following:
```cpp
task_dispatcher dispatcher;

// If your function returns task<T> you can use `co_await` and `co_return`,
// turning it into a coroutine
task<int> my_coroutine() {
    // Using `co_await` suspends the coroutine
    // It will be automatically resumed as if using `task.then()`
    co_await dispatcher.dispatch([]() { /* ... */ });

    // Valued tasks also work
    int value = co_await dispatcher.dispatch([]() { return 42; });

    // Resume the coroutine in main thread
    co_await dispatcher.dispatch_main();
    do_something_in_main_thread();

    // Resume the coroutine in a background thread
    co_await dispatcher.dispatch();
    do_something_in_background();

    co_return 0;
}

// the coroutine runs immediately, but suspends in the first `co_await`
task<int> t1 = my_coroutine();
```


## Next steps
One of the few features missing that I saw in some implementations is support for canceling dispatched tasks.
We could use tags for that, so that tagged tasks could be canceled.
Adding support for tags in main loop / delayed tasks would also be useful in this case.

Aside from that, making some benchmarks comparing with other implementations (such as [enkiTS](https://github.com/dougbinks/enkiTS) or [BS::thread_pool](https://github.com/bshoshany/thread-pool)) would be a nice addition.


## Conclusion
Well, I guess that's it, see you next time!
