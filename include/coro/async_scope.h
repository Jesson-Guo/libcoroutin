//
// Created by Jesson on 2024/10/18.
//

#ifndef ASYNC_SCOPE_H
#define ASYNC_SCOPE_H

#include "on_scope_exit.h"

#include <atomic>
#include <coroutine>
#include <type_traits>
#include <cassert>

namespace coro {

class async_scope {
public:
    async_scope() noexcept : m_count(1u) {}

    ~async_scope() {
        // scope must be co_awaited before it destructs.
        assert(m_handle);
    }

    template<typename AWAITABLE>
    void spawn(AWAITABLE&& awaitable) {
        [](async_scope* scope, std::decay_t<AWAITABLE> awaitable) -> oneway_task {
            scope->on_work_started();
            auto decrement_on_completion = on_scope_exit([scope] { scope->on_work_finished(); });
            co_await std::move(awaitable);
        }(this, std::forward<AWAITABLE>(awaitable));
    }

    [[nodiscard]] auto join() noexcept {
        class awaiter {
            async_scope* m_scope;
        public:
            explicit awaiter(async_scope* scope) noexcept : m_scope(scope) {}
            bool await_ready() noexcept {
                return m_scope->m_count.load(std::memory_order_acquire) == 0;
            }
            bool await_suspend(std::coroutine_handle<> handle) noexcept {
                m_scope->m_handle = handle;
                return m_scope->m_count.fetch_sub(1u, std::memory_order_acq_rel) > 1u;
            }
            void await_resume() noexcept {}
        };
        return awaiter{ this };
    }

private:
    void on_work_finished() noexcept {
        if (m_count.fetch_sub(1u, std::memory_order_acq_rel) == 1) {
            m_handle.resume();
        }
    }

    void on_work_started() noexcept {
        assert(m_count.load(std::memory_order_relaxed) != 0);
        m_count.fetch_add(1, std::memory_order_relaxed);
    }

    struct oneway_task {
        struct promise_type {
            std::suspend_never initial_suspend() { return {}; }
            std::suspend_never final_suspend() { return {}; }
            void unhandled_exception() { std::terminate(); }
            oneway_task get_return_object() { return {}; }
            void return_void() {}
        };
    };

    std::atomic<size_t> m_count;
    std::coroutine_handle<> m_handle;
};

}

#endif //ASYNC_SCOPE_H
