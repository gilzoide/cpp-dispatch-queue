cmake_minimum_required(VERSION 3.21...4.1)

project(dispatch_queue LANGUAGES CXX)

option(DISPATCH_QUEUE_BUILD_TESTS "Whether to build automated tests" ${PROJECT_IS_TOP_LEVEL})
option(DISPATCH_QUEUE_UNITY_BUILD "Whether to make a unity build" OFF)
option(DISPATCH_QUEUE_STATIC_LIB "Whether to build dispatch_queue as static library" ON)


if(DISPATCH_QUEUE_STATIC_LIB)
  set(_DISPATCH_QUEUE_STATIC_OR_SHARED STATIC)
else()
  set(_DISPATCH_QUEUE_STATIC_OR_SHARED SHARED)
endif()

if(DISPATCH_QUEUE_UNITY_BUILD)
  set(_DISPATCH_QUEUE_SRC "src/dispatch_queue-one.cpp")
else()
  set(_DISPATCH_QUEUE_SRC
    "src/dispatch_queue.cpp"
    "src/pending_task_queue.cpp"
    "src/worker_pool.cpp"
  )
endif()
set(_DISPATCH_QUEUE_HEADERS
  "include/dispatch_queue.hpp"
  "include/function_result.hpp"
  "include/is_instance_of.hpp"
  "include/pending_task_queue.hpp"
  "include/promise.hpp"
  "include/task_future.hpp"
  "include/task.hpp"
  "include/worker_pool.hpp"
)

add_library(dispatch_queue ${_DISPATCH_QUEUE_STATIC_OR_SHARED}
  ${_DISPATCH_QUEUE_SRC}
  ${_DISPATCH_QUEUE_HEADERS}
)
target_compile_features(dispatch_queue PUBLIC cxx_std_11)
target_include_directories(dispatch_queue PUBLIC include)

if(DISPATCH_QUEUE_BUILD_TESTS)
  add_subdirectory(test)
endif()
