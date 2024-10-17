#cmake_minimum_required(VERSION 3.29)
#project(libcoroutine LANGUAGES CXX)
#
#set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)
#
## 启用导出编译命令。这通常用于生成 compile_commands.json 文件，便于使用 clangd、ccls 等工具进行代码分析。
#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#
## 设置头文件目录
#include_directories(${PROJECT_SOURCE_DIR}/include)
#
## 可选地添加一个选项以启用或禁用测试
#option(BUILD_TESTS "Build tests" ON)
#
#if(BUILD_TESTS)
#    enable_testing()
#    add_subdirectory(test)
#endif()
#cmake_minimum_required(VERSION 3.29)
#project(libcoroutine LANGUAGES CXX)
#
#set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)
#set(CMAKE_CXX_EXTENSIONS OFF)
#
## 启用导出编译命令。这通常用于生成 compile_commands.json 文件，便于使用 clangd、ccls 等工具进行代码分析。
#set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
#
## 设置头文件目录
#include_directories(${PROJECT_SOURCE_DIR}/include)
#
## 可选地添加一个选项以启用或禁用测试
#option(BUILD_TESTS "Build tests" ON)
#
#if(BUILD_TESTS)
#    enable_testing()
#    add_subdirectory(test)
#endif()


cmake_minimum_required(VERSION 3.29)
project(libcoroutine)

set(CMAKE_CXX_STANDARD 20)

add_executable(libcoroutine src/coro/thread_pool.cpp
        include/coro/types/task.h
        include/coro/types/generator.h
        include/coro/types/recursive_generator.h
        include/coro/types/recursive_generator.h
        include/coro/types/shared_task.h
        include/coro/types/async_generator.h
        include/coro/awaitable/single_consumer_event.h
        include/coro/awaitable/single_producer_sequencer.h
        include/coro/awaitable/async_mutex.h
        include/coro/awaitable/async_manual_reset_event.h
        include/coro/awaitable/async_auto_reset_event.h
        include/coro/awaitable/single_consumer_async_auto_reset_event.h
        include/coro/awaitable/async_latch.h
        include/coro/sequence_traits.h
        include/coro/detail/any.h
        include/coro/detail/is_awaiter.h
        include/coro/detail/get_awaiter.h
        include/coro/detail/manual_lifetime.h
        include/coro/awaitable_traits.h
        include/coro/thread_pool.h
        include/coro/thread_pool.h
        include/coro/thread_pool.h
        include/coro/spin_wait.h
        include/coro/spin_mutex.h
        include/coro/auto_reset_event.h
        include/coro/io/file.h
        include/coro/on_scope_exit.h
        include/coro/cancellation/cancellation_token.h
        include/coro/cancellation/cancellation_state.h
        include/coro/cancellation/cancellation_source.h
        include/coro/cancellation/cancellation_registration.h
        include/coro/io/io_service.h
        include/coro/detail/macos.h
        include/coro/io/file_open_mode.h
        include/coro/io/file_share_mode.h
        include/coro/io/file_buffering_mode.h
        include/coro/io/file_read_operation.h
        include/coro/net/ip_address.h
        include/coro/net/ipv4_address.h
        include/coro/net/ipv6_address.h
        include/coro/net/ipv4_endpoint.h
        include/coro/net/ipv6_endpoint.h
        include/coro/net/ip_endpoint.h
        include/coro/net/socket_helpers.h
        include/coro/net/socket.h
        include/coro/net/socket_accept_operation.h
        include/coro/detail/macos_io_operation.h
        include/coro/operation_cancelled.h
        include/coro/net/socket_connect_operation.h
        include/coro/net/socket_disconnect_operation.h
        include/coro/net/socket_recv_from_operation.h
        include/coro/net/socket_recv_operation.h
        include/coro/net/socket_send_operation.h
        include/coro/net/socket_send_to_operation.h
        include/coro/io/file_write_operation.h
        include/coro/io/read_only_file.h
        include/coro/io/readable_file.h
        include/coro/io/writable_file.h
        include/coro/io/write_only_file.h
        include/coro/io/read_write_file.h
        include/coro/sequence_range.h
        include/coro/sequence_barrier.h
        include/coro/awaitable/multi_producer_sequencer.h
        src/coro/io_service.cpp
        src/coro/socket.cpp
        src/coro/socket_accept_operation.cpp
        src/coro/socket_connect_operation.cpp
        src/coro/socket_disconnect_operation.cpp
        src/coro/socket_recv_from_operation.cpp
        src/coro/file_read_operation.cpp
        src/coro/file_write_operation.cpp
        src/coro/readable_file.cpp
        src/coro/writable_file.cpp
        src/coro/read_only_file.cpp
        src/coro/write_only_file.cpp
        src/coro/read_write_file.cpp
        src/coro/file.cpp
        src/coro/cancellation_state.cpp
        src/coro/cancellation_source.cpp
        src/coro/cancellation_registration.cpp
        src/coro/cancellation_token.cpp
        src/coro/async_auto_reset_event.cpp
        src/coro/async_manual_reset_event.cpp
        src/coro/async_mutex.cpp
        src/coro/auto_reset_event.cpp
        src/coro/socket_helpers.cpp
        src/coro/spin_mutex.cpp
        src/coro/spin_wait.cpp
        include/coro/detail/lightweight_manual_reset_event.h
        src/coro/lightweight_manual_reset_event.cpp
        include/coro/detail/sync_wait.h
        include/coro/detail/sync_wait_task.h
        include/coro/detail/remove_rvalue_reference.h
)