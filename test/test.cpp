#include "task.hpp"
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <dispatch_queue.hpp>

TEST_CASE("Dispatch Queue") {
	SECTION("Synchronous") {
		dispatch_queue::dispatch_queue q(0);
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

	SECTION("Serial") {
		dispatch_queue::dispatch_queue q(1);
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

	SECTION("Concurrent") {
		dispatch_queue::dispatch_queue q(-1);
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

	SECTION("Dependency") {
		dispatch_queue::dispatch_queue q(-1);

		auto task = q.dispatch([=]{
			return 42;
		});
		auto dependant_task = q.dispatch(task, [task]{
			REQUIRE(task.get() == 42);
		});

		dependant_task.wait();
	}

	SECTION("Main loop") {
		dispatch_queue::dispatch_queue q(-1);

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

	SECTION("Main loop dependency") {
		dispatch_queue::dispatch_queue q(-1);

		auto thread_id = std::this_thread::get_id();
		auto task = q.dispatch([=]{
			return 42;
		});
		auto dependant_task = q.dispatch_main(task, [=]{
			REQUIRE(std::this_thread::get_id() == thread_id);
			REQUIRE(task.get() == 42);
		});
		task.wait();

		while(dependant_task.get_state() == dispatch_queue::task_state::pending) {
			q.main_loop();
		}
		REQUIRE(dependant_task.get_state() == dispatch_queue::task_state::ready);
	}

#ifdef __cpp_impl_coroutine
	SECTION("Dispatch awaiters") {
		dispatch_queue::dispatch_queue q(-1);

		auto thread_id = std::this_thread::get_id();
		auto coro = [&, thread_id]() -> dispatch_queue::task<int> {
			REQUIRE(std::this_thread::get_id() == thread_id);
			co_await q.dispatch();
			REQUIRE(std::this_thread::get_id() != thread_id);
			co_await q.dispatch_main();
			REQUIRE(std::this_thread::get_id() == thread_id);
			co_return 3;
		}();
		while (coro.get_state() != dispatch_queue::task_state::ready) {
			q.main_loop();
		}
		coro.get();
	}
#endif
}
