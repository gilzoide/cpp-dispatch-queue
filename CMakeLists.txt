cmake_minimum_required(VERSION 3.0)

project(dispatch_queue LANGUAGES CXX)

option(DISPATCH_QUEUE_NOEXCEPT "Disable exception support. Use if compiling project with -fno-exceptions" OFF)

add_library(dispatch_queue
  "dispatch_queue.cpp"
  "dispatch_queue.hpp"
)
target_compile_features(dispatch_queue PUBLIC cxx_std_11)
target_include_directories(dispatch_queue PUBLIC .)

if(DISPATCH_QUEUE_BUILD_TESTS)
  add_subdirectory(test)
endif()
