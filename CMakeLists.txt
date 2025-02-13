cmake_minimum_required(VERSION 3.0)

project(dispatch_queue LANGUAGES CXX)

option(DISPATCH_QUEUE_NOEXCEPT "Disable exception support. Use if compiling project with -fno-exceptions" OFF)

add_library(dispatch_queue
  "include/dispatch_queue.hpp"
  "src/dispatch_queue.cpp"
)
target_compile_features(dispatch_queue PUBLIC cxx_std_11)
target_include_directories(dispatch_queue PUBLIC include)

if(DISPATCH_QUEUE_BUILD_TESTS)
  add_subdirectory(test)
endif()
