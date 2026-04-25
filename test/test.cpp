#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <dispatch_queue.hpp>

TEST_CASE("Dispatch Queue") {
	SECTION("Synchronous") {
		dispatch_queue::dispatch_queue q(0);
		REQUIRE(!q.is_threaded());

		auto future = q.dispatch([]() { return 42; });
		REQUIRE(future.get() == 42);

		auto thread_id = std::this_thread::get_id();
		INFO("Test thread ID" << thread_id);
		q.dispatch([=]() {
			REQUIRE(std::this_thread::get_id() == thread_id);
		});

		q.wait();
	}

	SECTION("Serial") {
		dispatch_queue::dispatch_queue q(1);
		REQUIRE(q.is_threaded());

		// q.dispatch_and_forget([]() { return 1; });
		auto future = q.dispatch([]() { return 42; });
		REQUIRE(future.get() == 42);

		auto thread_id = std::this_thread::get_id();
		INFO("Test thread ID" << thread_id);
		auto task = q.dispatch([=]() {
			REQUIRE(std::this_thread::get_id() != thread_id);
		});

		task.wait();
	}

	SECTION("Concurrent") {
		dispatch_queue::dispatch_queue q(-1);
		REQUIRE(q.is_threaded());

		// q.dispatch_and_forget([]() { return 1; });
		auto future = q.dispatch([]() { return 42; });
		REQUIRE(future.get() == 42);

		auto thread_id = std::this_thread::get_id();
		INFO("Test thread ID" << thread_id);
		for (int i = 0; i < 10; i++) {
			q.dispatch([=]() {
				REQUIRE(std::this_thread::get_id() != thread_id);
			});
		}

		q.wait();
	}

	SECTION("Dependency") {
		dispatch_queue::dispatch_queue q(-1);

		auto task = q.dispatch([=]() {
			return 42;
		});
		auto dependant_task = q.dispatch(task, [task]{
			REQUIRE(task.get() == 42);
		});

		dependant_task.wait();
	}

	SECTION("Main loop") {
		dispatch_queue::dispatch_queue q(-1);

		auto task = q.dispatch_main([=]() {
			return 42;
		});
		REQUIRE(task.is_pending());
		q.main_loop();
		REQUIRE(task.is_ready());
		REQUIRE(task.get() == 42);
	}

	SECTION("Main loop dependency") {
		dispatch_queue::dispatch_queue q(-1);

		auto task = q.dispatch([=]() {
			return 42;
		});
		auto dependant_task = q.dispatch_main(task, [task]{
			REQUIRE(task.get() == 42);
		});
		task.wait();

		REQUIRE(dependant_task.is_pending());
		q.main_loop();
		REQUIRE(dependant_task.is_ready());
	}
}
