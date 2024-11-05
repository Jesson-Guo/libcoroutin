//
// Created by Jesson on 2024/9/16.
//

#ifndef TASK_H
#define TASK_H

#include "../awaitable_traits.h"
#include "../detail/remove_rvalue_reference.h"
#include "../broken_promise.h"

#include <coroutine>
#include <exception>
#include <cassert>

namespace coro {

template<typename T>
class task;

namespace detail {
class task_promise_base {
    friend struct final_awaitable;
    struct final_awaitable {
        auto await_ready() noexcept -> bool { return false; }

        template<typename promise_type>
        auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> std::coroutine_handle<> {
            auto& promise = handle.promise();
            if (promise.m_handle) {
                return promise.m_handle;
            }
            return std::noop_coroutine();
        }

        auto await_resume() noexcept -> void {}
    };
public:
    task_promise_base() noexcept = default;
    ~task_promise_base() noexcept = default;
    auto initial_suspend() const noexcept -> std::suspend_always { return std::suspend_always{}; }
    auto final_suspend() const noexcept -> final_awaitable { return final_awaitable{}; }
    auto set_continuation(const std::coroutine_handle<> handle) noexcept -> void {
        m_handle = handle;
    }
private:
    std::coroutine_handle<> m_handle;
};

template<typename T>
class task_promise final : public task_promise_base {
public:
    task_promise() noexcept {}

    ~task_promise() noexcept {
        switch (m_result) {
        case result_type::value:
            m_value.~T();
            break;
        case result_type::exception:
            m_exception.~exception_ptr();
            break;
        default:
            break;
        }
    }

    task<T> get_return_object() noexcept;

    auto unhandled_exception() noexcept -> void {
        ::new (static_cast<void*>(std::addressof(m_exception))) std::exception_ptr(
            std::current_exception());
        m_result = result_type::exception;
    }

    template<
        typename value_type,
        typename = std::enable_if_t<std::is_convertible_v<value_type&&, T>>>
    auto return_value(value_type&& value)
        noexcept(std::is_nothrow_constructible_v<T, value_type&&>) -> void {
        ::new (static_cast<void*>(std::addressof(m_value))) T(std::forward<value_type>(value));
        m_result = result_type::value;
    }

    auto result() & -> T& {
        if (m_result == result_type::exception) {
            std::rethrow_exception(m_exception);
        }
        assert(m_result == result_type::value);
        return m_value;
    }

    using rvalue_type = std::conditional_t<
        std::is_arithmetic_v<T> || std::is_pointer_v<T>,
        T,
        T&&>;
    auto result() && -> rvalue_type {
        if (m_result == result_type::exception) {
            std::rethrow_exception(m_exception);
        }
        assert(m_result == result_type::value);
        return std::move(m_value);
    }

private:
    enum class result_type { empty, value, exception };
    result_type m_result = result_type::empty;
    union {
        T m_value;
        std::exception_ptr m_exception;
    };
};

template<>
class task_promise<void> : public task_promise_base {
public:
    task_promise() noexcept = default;
    ~task_promise() noexcept = default;

    task<void> get_return_object() noexcept;

    auto return_void() noexcept -> void {}

    auto unhandled_exception() noexcept -> void {
        m_exception = std::current_exception();
    }

    auto result() const -> void {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
    }
private:
    std::exception_ptr m_exception;
};

template<typename T>
class task_promise<T&> : public task_promise_base {
public:
    task_promise() noexcept = default;
    ~task_promise() noexcept = default;

    task<T&> get_return_object() noexcept;

    auto unhandled_exception() noexcept -> void {
        m_exception = std::current_exception();
    }

    auto return_value(T& value) noexcept -> void {
        m_value = std::addressof(value);
    }

    auto result() -> T& {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
        return *m_value;
    }
private:
    T* m_value = nullptr;
    std::exception_ptr m_exception;
};
}

template<typename T = void>
class task {
public:
    using promise_type = detail::task_promise<T>;
    using value_type = T;
private:
    struct awaitable_base {
        awaitable_base(std::coroutine_handle<promise_type> handle) noexcept : m_handle(handle) {}

        auto await_ready() const noexcept -> bool {
            return !m_handle || m_handle.done();
        }

        auto await_suspend(std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<> {
            m_handle.promise().set_continuation(handle);
            return m_handle;
        }

        std::coroutine_handle<promise_type> m_handle = nullptr;
    };
public:
    task() noexcept : m_handle(nullptr) {}
    explicit task(std::coroutine_handle<promise_type> handle) noexcept : m_handle(handle) {}
    task(task&& t) noexcept : m_handle(t.m_handle) {
        t.m_handle = nullptr;
    }
    task& operator=(task&& t) noexcept {
        if (std::addressof(t) != this) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle = t.m_handle;
            t.m_handle = nullptr;
        }
        return *this;
    }
    task(const task&) = delete;
    task& operator=(const task&) = delete;
    ~task() noexcept {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    auto is_ready() const noexcept -> bool {
        return !m_handle || m_handle.done();
    }

    auto operator co_await() const & {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;

            decltype(auto) await_resume() {
                if (!this->m_handle) {
                    throw broken_promise{};
                }
                return this->m_handle.promise().result();
            }
        };
        return awaitable{m_handle};
    }

    auto operator co_await() const && {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;
            decltype(auto) await_resume() {
                if (!this->m_handle) {
                    throw broken_promise{};
                }
                return std::move(this->m_handle.promise()).result();
            }
        };
        return awaitable{m_handle};
    }

    auto when_ready() const noexcept {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;
            static auto await_resume() noexcept -> void {}
        };
        return awaitable{m_handle};
    }
private:
    std::coroutine_handle<promise_type> m_handle;
};

namespace detail {
template<typename T>
task<T> task_promise<T>::get_return_object() noexcept {
    return task<T>{ std::coroutine_handle<task_promise>::from_promise(*this) };
}

inline task<void> task_promise<void>::get_return_object() noexcept {
    return task<void>{ std::coroutine_handle<task_promise>::from_promise(*this) };
}

template<typename T>
task<T&> task_promise<T&>::get_return_object() noexcept {
    return task<T&>{ std::coroutine_handle<task_promise>::from_promise(*this) };
}

}

template<typename awaitable_type>
auto make_task(awaitable_type awaitable) -> task<detail::remove_rvalue_reference_t<typename detail::awaitable_traits<awaitable_type>::await_result_t>> {
    co_return co_await static_cast<awaitable_type&&>(awaitable);
}

}

#endif //TASK_H
