add_subdirectory(Catch2)

include(CTest)

add_executable(dispatch_queue_test test.cpp)
target_compile_features(dispatch_queue_test PRIVATE cxx_std_20)
target_link_libraries(dispatch_queue_test dispatch_queue Catch2::Catch2WithMain)

add_test(NAME dispatch_queue_test COMMAND dispatch_queue_test)
