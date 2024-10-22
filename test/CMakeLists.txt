set(TEST_FILES
        async_auto_reset_event_tests.cpp
        async_generator_tests.cpp
        async_latch_tests.cpp
        async_manual_reset_event_tests.cpp
        async_mutex_tests.cpp
        cancellation_tests.cpp
        file_tests.cpp
        generator_tests.cpp
        io_service_tests.cpp
        multi_producer_sequencer_tests.cpp
        recursive_generator_tests.cpp
        sequence_barrier_tests.cpp
        shared_task_tests.cpp
        single_consumer_async_auto_reset_event_tests.cpp
        single_producer_sequencer_tests.cpp
        socket_tests.cpp
        sync_wait_tests.cpp
        task_tests.cpp
        thread_pool_tests.cpp
        when_all_ready_tests.cpp
        when_all_tests.cpp
)

add_library(doctest::doctest INTERFACE IMPORTED)
target_include_directories(doctest::doctest INTERFACE doctest)

include(${CMAKE_CURRENT_LIST_DIR}/doctest/doctest.cmake)

find_package(Threads REQUIRED)

option(CORO_TESTS_LIMITED_RESOURCES "Use limited-resources testing parameters" OFF)
if(CORO_TESTS_LIMITED_RESOURCES)
    add_definitions(-DCORO_TESTS_LIMITED_RESOURCES=1)
endif()

add_library(tests-main STATIC test.cpp counted.cpp)
target_link_libraries(tests-main PUBLIC coro doctest::doctest Threads::Threads)

# 遍历所有测试文件，创建可执行文件并链接库
foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} PRIVATE tests-main)
    string(REPLACE "_" " " test_prefix ${test_name})
    if (NOT DEFINED ${test_name}_TIMEOUT)
        set(${test_name}_TIMEOUT 30)
    endif()
    doctest_discover_tests(${test_name} TEST_PREFIX ${test_prefix}- PROPERTIES TIMEOUT ${${test_name}_TIMEOUT})
endforeach()
