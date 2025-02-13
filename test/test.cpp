#include <catch2/catch_test_macros.hpp>
#include <dispatch_queue.hpp>
#include <thread>

TEST_CASE("Dispatch Queue") {
	SECTION("Synchronous") {
		dispatch_queue::dispatch_queue q(0);
		REQUIRE(!q.is_threaded());

		auto future = q.dispatch([]() { return 42; });
		REQUIRE(future.get() == 42);

		auto thread_id = std::this_thread::get_id();
		INFO("Test thread ID" << thread_id);
		q.dispatch_and_forget([=]() {
			REQUIRE(std::this_thread::get_id() == thread_id);
		});
	}

	SECTION("Serial") {
		dispatch_queue::dispatch_queue q(1);
		REQUIRE(q.is_threaded());

		// q.dispatch_and_forget([]() { return 1; });
		auto future = q.dispatch([]() { return 42; });
		REQUIRE(future.get() == 42);

		auto thread_id = std::this_thread::get_id();
		INFO("Test thread ID" << thread_id);
		q.dispatch_and_forget([=]() {
			REQUIRE(std::this_thread::get_id() != thread_id);
		});
	}

	SECTION("Concurrent") {
		dispatch_queue::dispatch_queue q(5);
		REQUIRE(q.is_threaded());

		// q.dispatch_and_forget([]() { return 1; });
		auto future = q.dispatch([]() { return 42; });
		REQUIRE(future.get() == 42);

		auto thread_id = std::this_thread::get_id();
		INFO("Test thread ID" << thread_id);
		q.dispatch_and_forget([=]() {
			REQUIRE(std::this_thread::get_id() != thread_id);
		});
	}
}
