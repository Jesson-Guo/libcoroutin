cmake_minimum_required(VERSION 3.29)
project(libcoroutine)

set(CMAKE_CXX_STANDARD 20)

add_executable(libcoroutine main.cpp
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
        include/coro/awaitable/sequence_barrier.h
        include/coro/sequence_traits.h
        include/coro/detail/any.h
        include/coro/detail/is_awaiter.h
        include/coro/detail/get_awaiter.h
        include/coro/detail/manual_lifetime.h
        include/coro/awaitable_traits.h
        include/coro/io/thread_pool.h
        include/coro/io/thread_pool.h
        include/coro/io/thread_pool.h
        include/coro/spin_wait.h
        include/coro/spin_mutex.h
        include/coro/auto_reset_event.h
        include/coro/io/file.h
        include/coro/on_scope_exit.h
        include/coro/cancellation/operation_cancelled.h
        include/coro/cancellation/cancellation_token.h
        include/coro/cancellation/cancellation_state.h
        include/coro/cancellation/cancellation_source.h
        include/coro/cancellation/cancellation_registration.h
        include/coro/io/io_service.h
        include/coro/detail/linux.h
        include/coro/detail/macos.h
)
