#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <dispatch_queue.hpp>

std::uint64_t fibonacci(std::uint64_t number) {
    return number < 2 ? 1 : fibonacci(number - 1) + fibonacci(number - 2);
}

auto some_work() {
	return fibonacci(10);
}

TEST_CASE("Dispatch Queue") {
	for (int thread_count = 0; thread_count <= 4; ++thread_count) {
		SECTION(std::format("{} threads", thread_count)) {
			BENCHMARK_ADVANCED("dispatch")(auto meter) {
				dispatch_queue::dispatch_queue q(thread_count);
				meter.measure([&]{
					q.dispatch(some_work);
				});
			};

			BENCHMARK_ADVANCED("dispatch 100")(auto meter) {
				dispatch_queue::dispatch_queue q(thread_count);
				meter.measure([&]{
					for (int i = 0; i < 100; ++i) {
						q.dispatch(some_work);
					}
				});
			};

			BENCHMARK_ADVANCED("dispatch 100+wait")(auto meter) {
				dispatch_queue::dispatch_queue q(thread_count);
				meter.measure([&]{
					for (int i = 0; i < 100; ++i) {
						q.dispatch(some_work);
					}
					q.wait();
				});
			};
		}
	}
}
