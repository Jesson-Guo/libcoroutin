# 包含头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 定义头文件列表
set(INC_FILES
        ${PROJECT_SOURCE_DIR}/include/coro/async_scope.h
        ${PROJECT_SOURCE_DIR}/include/coro/auto_reset_event.h
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable_traits.h
        ${PROJECT_SOURCE_DIR}/include/coro/broken_promise.h
        ${PROJECT_SOURCE_DIR}/include/coro/fmap.h
        ${PROJECT_SOURCE_DIR}/include/coro/is_awaitable.h
        ${PROJECT_SOURCE_DIR}/include/coro/on_scope_exit.h
        ${PROJECT_SOURCE_DIR}/include/coro/operation_cancelled.h
        ${PROJECT_SOURCE_DIR}/include/coro/sequence_barrier.h
        ${PROJECT_SOURCE_DIR}/include/coro/sequence_range.h
        ${PROJECT_SOURCE_DIR}/include/coro/sequence_traits.h
        ${PROJECT_SOURCE_DIR}/include/coro/spin_mutex.h
        ${PROJECT_SOURCE_DIR}/include/coro/spin_wait.h
        ${PROJECT_SOURCE_DIR}/include/coro/thread_pool.h
        ${PROJECT_SOURCE_DIR}/include/coro/when_all.h
        ${PROJECT_SOURCE_DIR}/include/coro/when_all_ready.h
)

set(AWAITABLE_INC_FILES
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable/async_auto_reset_event.h
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable/async_latch.h
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable/async_manual_reset_event.h
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable/async_mutex.h
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable/multi_producer_sequencer.h
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable/single_producer_sequencer.h
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable/single_consumer_async_auto_reset_event.h
        ${PROJECT_SOURCE_DIR}/include/coro/awaitable/single_consumer_event.h
)

set(CANCELLATION_INC_FILES
        ${PROJECT_SOURCE_DIR}/include/coro/cancellation/cancellation_registration.h
        ${PROJECT_SOURCE_DIR}/include/coro/cancellation/cancellation_source.h
        ${PROJECT_SOURCE_DIR}/include/coro/cancellation/cancellation_state.h
        ${PROJECT_SOURCE_DIR}/include/coro/cancellation/cancellation_token.h
)

set(DETAIL_INC_FILES
        ${PROJECT_SOURCE_DIR}/include/coro/detail/any.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/get_awaiter.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/is_awaiter.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/lightweight_manual_reset_event.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/macos.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/macos_io_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/manual_lifetime.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/remove_rvalue_reference.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/sync_wait.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/sync_wait_task.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/unwrap_reference.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/void_value.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/when_all_counter.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/when_all_ready_awaitable.h
        ${PROJECT_SOURCE_DIR}/include/coro/detail/when_all_task.h
)

set(IO_INC_FILES
        ${PROJECT_SOURCE_DIR}/include/coro/io/file.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/file_share_mode.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/file_open_mode.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/file_buffering_mode.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/file_read_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/file_write_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/readable_file.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/read_only_file.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/writable_file.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/write_only_file.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/read_write_file.h
        ${PROJECT_SOURCE_DIR}/include/coro/io/io_service.h
)

set(NET_INC_FILES
        ${PROJECT_SOURCE_DIR}/include/coro/net/ip_address.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/ip_endpoint.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/ipv4_address.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/ipv4_endpoint.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/ipv6_address.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/ipv6_endpoint.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/socket.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/socket_accept_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/socket_connect_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/socket_disconnect_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/socket_recv_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/socket_recv_from_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/socket_send_operation.h
        ${PROJECT_SOURCE_DIR}/include/coro/net/socket_send_to_operation.h
)

set(TYPES_INC_FILES
        ${PROJECT_SOURCE_DIR}/include/coro/types/async_generator.h
        ${PROJECT_SOURCE_DIR}/include/coro/types/generator.h
        ${PROJECT_SOURCE_DIR}/include/coro/types/recursive_generator.h
        ${PROJECT_SOURCE_DIR}/include/coro/types/shared_task.h
        ${PROJECT_SOURCE_DIR}/include/coro/types/task.h
)

# 合并所有头文件
set(HEADER_FILES
        ${INC_FILES}
        ${AWAITABLE_INC_FILES}
        ${CANCELLATION_INC_FILES}
        ${DETAIL_INC_FILES}
        ${IO_INC_FILES}
        ${NET_INC_FILES}
        ${TYPES_INC_FILES}
)

# 定义源文件列表
set(SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/async_auto_reset_event.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/async_manual_reset_event.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/async_mutex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/auto_reset_event.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/cancellation_registration.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/cancellation_source.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/cancellation_token.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/cancellation_state.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/file_read_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/file_write_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/io_service.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/lightweight_manual_reset_event.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/read_only_file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/read_write_file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/readable_file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket_helpers.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket_accept_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket_connect_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket_disconnect_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket_recv_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket_recv_from_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket_send_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/socket_send_to_operation.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/spin_mutex.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/spin_wait.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/thread_pool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/writable_file.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/coro/write_only_file.cpp
)

# 创建静态库
add_library(coro STATIC ${SRC_FILES} ${HEADER_FILES})

# 添加头文件目录
target_include_directories(coro PUBLIC ${PROJECT_SOURCE_DIR}/include)

# 链接线程库
target_link_libraries(coro PUBLIC Threads::Threads)

# 设置目标的C++标准
set_target_properties(coro PROPERTIES CXX_STANDARD 20)
