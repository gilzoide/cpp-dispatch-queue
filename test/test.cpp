#include <exception>
#include <stdexcept>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <dispatch_queue/dispatch_queue.hpp>

using namespace std::chrono_literals;

TEST_CASE("Synchronous") {
	dispatch_queue::task_dispatcher q(0);
	REQUIRE(!q.is_threaded());

	auto future = q.dispatch([]{ return 42; });
	REQUIRE(future.get() == 42);

	auto thread_id = std::this_thread::get_id();
	INFO("Test thread ID" << thread_id);
	q.dispatch([=]{
		REQUIRE(std::this_thread::get_id() == thread_id);
	});

	q.wait();
}

TEST_CASE("Serial") {
	dispatch_queue::task_dispatcher q(1);
	REQUIRE(q.is_threaded());

	auto future = q.dispatch([]{ return 42; });
	REQUIRE(future.get() == 42);

	auto thread_id = std::this_thread::get_id();
	INFO("Test thread ID" << thread_id);
	auto task = q.dispatch([=]{
		REQUIRE(std::this_thread::get_id() != thread_id);
	});

	task.wait();
}

TEST_CASE("Concurrent") {
	dispatch_queue::task_dispatcher q(-1);
	REQUIRE(q.is_threaded());

	auto future = q.dispatch([]{ return 42; });
	REQUIRE(future.get() == 42);

	auto thread_id = std::this_thread::get_id();
	INFO("Test thread ID" << thread_id);
	for (int i = 0; i < 10; i++) {
		q.dispatch([=]{
			REQUIRE(std::this_thread::get_id() != thread_id);
		});
	}

	q.wait();
}

TEST_CASE("Main loop") {
	dispatch_queue::task_dispatcher q(-1);

	auto thread_id = std::this_thread::get_id();
	auto task = q.dispatch_main([=]{
		REQUIRE(std::this_thread::get_id() == thread_id);
		return 42;
	});
	REQUIRE(task.get_state() == dispatch_queue::task_state::pending);
	q.main_loop();
	REQUIRE(task.get_state() == dispatch_queue::task_state::ready);
	REQUIRE(task.get() == 42);
}

TEST_CASE("Main loop dependency") {
	dispatch_queue::task_dispatcher q(-1);

	auto thread_id = std::this_thread::get_id();
	auto task = q.dispatch([=]{
		return 42;
	}).then([=, &q](auto t) {
		// This continuation may or may not run inside a background thread, it depends if `then` is called before or after task is pending execution
		return q.dispatch_main([=] {
			REQUIRE(std::this_thread::get_id() == thread_id);
			REQUIRE(t.get() == 42);
			return t.get() + 1;
		});
	}).then([=](auto t) {
		return t.get() + 2;
	});

	while (task.get_state() == dispatch_queue::task_state::pending) {
		q.main_loop();
	}
	REQUIRE(task.get_state() == dispatch_queue::task_state::ready);
	REQUIRE(task.get() == 42 + 1 + 2);
}

#ifdef __cpp_impl_coroutine
TEST_CASE("Dispatch awaiters") {
	dispatch_queue::task_dispatcher q(-1);

	auto thread_id = std::this_thread::get_id();
	auto coro = [&, thread_id]() -> dispatch_queue::task<int> {
		REQUIRE(std::this_thread::get_id() == thread_id);
		co_await q.dispatch();
		REQUIRE(std::this_thread::get_id() != thread_id);
		co_await q.dispatch_main();
		REQUIRE(std::this_thread::get_id() == thread_id);
		int value = co_await q.dispatch([]{ return 3; });
		REQUIRE(value == 3);
		co_return value;
	}();
	while (coro.get_state() != dispatch_queue::task_state::ready) {
		q.main_loop();
	}
	REQUIRE(coro.get() == 3);
}
#endif

TEST_CASE("when_all") {
	SECTION("all success") {
		auto task = dispatch_queue::when_all(
			dispatch_queue::task<void>::create_ready(),
			dispatch_queue::task<void>::create_ready(),
			dispatch_queue::task<void>::create_ready()
		);
		REQUIRE(task.get_state() == dispatch_queue::task_state::ready);
	}

	SECTION("all failed") {
		auto task = dispatch_queue::when_all(
			dispatch_queue::task<void>::create_failed(std::make_exception_ptr(std::runtime_error(""))),
			dispatch_queue::task<void>::create_failed(std::make_exception_ptr(std::runtime_error(""))),
			dispatch_queue::task<void>::create_failed(std::make_exception_ptr(std::runtime_error("")))
		);
		REQUIRE(task.get_state() == dispatch_queue::task_state::failed);
	}

	SECTION("one failed") {
		auto task = dispatch_queue::when_all(
			dispatch_queue::task<void>::create_ready(),
			dispatch_queue::task<void>::create_ready(),
			dispatch_queue::task<void>::create_failed(std::make_exception_ptr(std::runtime_error("")))
		);
		REQUIRE(task.get_state() == dispatch_queue::task_state::failed);
	}

	SECTION("one pending") {
		auto future = dispatch_queue::detail::task_future<void>::create_pending();
		auto task = dispatch_queue::when_all(
			dispatch_queue::task<void>::create_ready(),
			dispatch_queue::task<void>::create_ready(),
			dispatch_queue::task(future)
		);
		REQUIRE(task.get_state() == dispatch_queue::task_state::pending);
		future->set_value();
		REQUIRE(task.get_state() == dispatch_queue::task_state::ready);
	}
}

TEST_CASE("parallel_for") {
	bool finished[1024] = {};
	for (bool b : finished) {
		REQUIRE(!b);
	}

	dispatch_queue::task_dispatcher q(-1);
	auto task = q.parallel_for([&](auto& b) {
		b = true;
	}, finished).then([&](const auto& t) {
		for (bool b : finished) {
			REQUIRE(b);
		}
	});
	if (!task.wait_for(1s)) {
		FAIL("Task timed out!");
	}
}

TEST_CASE("task as promise") {
	SECTION("ready") {
		auto promise = dispatch_queue::task<void>::create_pending();
		REQUIRE(promise.get_state() == dispatch_queue::task_state::pending);
		promise.set_value();
		REQUIRE(promise.get_state() == dispatch_queue::task_state::ready);
		REQUIRE_THROWS(promise.set_value());
		REQUIRE_THROWS(promise.set_exception(std::make_exception_ptr(std::runtime_error(""))));
	}

	SECTION("ready T") {
		auto promise = dispatch_queue::task<int>::create_pending();
		REQUIRE(promise.get_state() == dispatch_queue::task_state::pending);
		promise.set_value(42);
		REQUIRE(promise.get_state() == dispatch_queue::task_state::ready);
		REQUIRE_THROWS(promise.set_value(42));
		REQUIRE_THROWS(promise.set_exception(std::make_exception_ptr(std::runtime_error(""))));
	}

	SECTION("create_ready") {
		auto promise = dispatch_queue::task<void>::create_ready();
		REQUIRE(promise.get_state() == dispatch_queue::task_state::ready);
		REQUIRE_THROWS(promise.set_value());
		REQUIRE_THROWS(promise.set_exception(std::make_exception_ptr(std::runtime_error(""))));
	}

	SECTION("create_ready T") {
		auto promise = dispatch_queue::task<int>::create_ready(42);
		REQUIRE(promise.get_state() == dispatch_queue::task_state::ready);
		REQUIRE_THROWS(promise.set_value(42));
		REQUIRE_THROWS(promise.set_exception(std::make_exception_ptr(std::runtime_error(""))));
	}

	SECTION("failed") {
		auto promise = dispatch_queue::task<void>::create_pending();
		REQUIRE(promise.get_state() == dispatch_queue::task_state::pending);
		promise.set_exception(std::make_exception_ptr(std::runtime_error("")));
		REQUIRE(promise.get_state() == dispatch_queue::task_state::failed);
		REQUIRE_THROWS(promise.set_value());
		REQUIRE_THROWS(promise.set_exception(std::make_exception_ptr(std::runtime_error(""))));
	}

	SECTION("create_failed") {
		auto promise = dispatch_queue::task<void>::create_failed(std::make_exception_ptr(std::runtime_error("")));
		REQUIRE(promise.get_state() == dispatch_queue::task_state::failed);
		REQUIRE_THROWS(promise.set_value());
		REQUIRE_THROWS(promise.set_exception(std::make_exception_ptr(std::runtime_error(""))));
	}
}

TEST_CASE("move") {
	dispatch_queue::task_dispatcher q;
	dispatch_queue::task_dispatcher q2 = std::move(q);
}
