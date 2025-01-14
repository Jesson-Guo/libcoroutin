//
// Created by Jesson on 2024/9/22.
//

#ifndef SHARED_shared_task_H
#define SHARED_shared_task_H

#include "../awaitable_traits.h"
#include "../detail/remove_rvalue_reference.h"
#include "../broken_promise.h"

#include <coroutine>
#include <exception>
#include <atomic>
#include <cassert>

namespace coro {

template<typename T>
class shared_task;

namespace detail {

struct shared_task_waiter {
    std::coroutine_handle<> m_handle;
    shared_task_waiter* m_next = nullptr;
};

class shared_task_promise_base {
    friend struct final_awaitable;
    struct final_awaitable {
        auto await_ready() noexcept -> bool { return false; }

        template<typename promise_type>
        auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept -> void {
            shared_task_promise_base& promise = handle.promise();
            void* const ready = &promise;
            if (void* waiters = promise.m_waiters.exchange(ready, std::memory_order_acq_rel)) {
                auto* waiter = static_cast<shared_task_waiter*>(waiters);
                while (waiter->m_next) {
                    auto* next = waiter->m_next;
                    waiter->m_handle.resume();
                    waiter = next;
                }
                waiter->m_handle.resume();
            }
        }

        auto await_resume() noexcept -> void {}
    };
public:
    shared_task_promise_base() noexcept : m_ref_count(1), m_waiters(&this->m_waiters), m_exception(nullptr) {}

    std::suspend_always initial_suspend() noexcept { return {}; }

    final_awaitable final_suspend() noexcept { return {}; }

    void unhandled_exception() {
        m_exception = std::current_exception();
    }

    auto is_ready() const noexcept -> bool {
        const void* const ready = this;
        return m_waiters.load(std::memory_order_acquire) == ready;
    }

    auto add_ref() noexcept -> void {
        m_ref_count.fetch_add(1, std::memory_order_relaxed);
    }

    auto try_detach() noexcept -> bool {
        return m_ref_count.fetch_sub(1, std::memory_order_acq_rel) != 1;
    }

    auto try_await(shared_task_waiter* waiter, const std::coroutine_handle<> handle) noexcept -> bool {
        void* const ready = this;
        void* const not_started = &this->m_waiters;
        constexpr void* started_no_waiters = static_cast<shared_task_waiter*>(nullptr);

        void* old_waiters = m_waiters.load(std::memory_order_acquire);
        if (old_waiters == not_started &&
            m_waiters.compare_exchange_strong(
                old_waiters,
                started_no_waiters,
                std::memory_order_relaxed)) {
            handle.resume();
            old_waiters = m_waiters.load(std::memory_order_acquire);
        }

        do {
            if (old_waiters == ready) {
                return false;
            }
            waiter->m_next = static_cast<shared_task_waiter*>(old_waiters);
        } while (!m_waiters.compare_exchange_weak(
            old_waiters,
            waiter,
            std::memory_order_release,
            std::memory_order_acquire));
        return true;
    }

protected:
    auto completed_with_unhandled_exception() const noexcept -> bool {
        return m_exception != nullptr;
    }

    auto rethrow_if_unhandled_exception() const noexcept -> void {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
    }

private:
    std::atomic<uint32_t> m_ref_count;
    std::atomic<void*> m_waiters;
    std::exception_ptr m_exception;
};

template<typename T>
class shared_task_promise : public shared_task_promise_base {
public:
    shared_task_promise() noexcept = default;

    ~shared_task_promise() noexcept {
        if (this->is_ready() && !this->completed_with_unhandled_exception()) {
            reinterpret_cast<T*>(&m_value_storage)->~T();
        }
    }

    shared_task<T> get_return_object() noexcept;

    template<
        typename value_type,
        typename = std::enable_if_t<std::is_convertible_v<value_type&&, T>>>
    auto return_value(value_type&& value) noexcept(std::is_nothrow_constructible_v<T, value_type&&>) -> void {
        new (&m_value_storage) T(std::forward<value_type>(value));
    }

    auto result() -> T& {
        this->rethrow_if_unhandled_exception();
        return *reinterpret_cast<T*>(&m_value_storage);
    }

private:
    alignas(T) char m_value_storage[sizeof(T)];
};

template<>
class shared_task_promise<void> : public shared_task_promise_base {
public:
    shared_task_promise() noexcept = default;
    shared_task<void> get_return_object() noexcept;
    auto return_void() noexcept -> void {}
    auto result() const -> void {
        this->rethrow_if_unhandled_exception();
    }
};

template<typename T>
class shared_task_promise<T&> : public shared_task_promise_base {
public:
    shared_task_promise() noexcept = default;
    ~shared_task_promise() noexcept = default;
    shared_task<T&> get_return_object() noexcept;
    auto return_value(T& value) noexcept -> void {
        m_value = std::addressof(value);
    }
    auto result() -> T& {
        this->rethrow_if_unhandled_exception();
        return *m_value;
    }
private:
    T* m_value = nullptr;
};

}

template<typename T = void>
class shared_task {
public:
    using promise_type = detail::shared_task_promise<T>;
    using value_type = T;

private:
	struct awaitable_base {
		awaitable_base(std::coroutine_handle<promise_type> handle) noexcept : m_handle(handle) {}
		bool await_ready() const noexcept {
			return !m_handle || m_handle.promise().is_ready();
		}
		bool await_suspend(std::coroutine_handle<> awaiter) noexcept {
			m_waiter.m_handle = awaiter;
			return m_handle.promise().try_await(&m_waiter, m_handle);
		}
	    std::coroutine_handle<promise_type> m_handle;
	    detail::shared_task_waiter m_waiter;
	};

public:
    shared_task() noexcept : m_handle(nullptr) {}

    // 这里不用增加ref_count
    explicit shared_task(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}

    shared_task(shared_task&& t) noexcept : m_handle(t.m_handle) {
        t.m_handle = nullptr;
    }

    shared_task(const shared_task& t) noexcept : m_handle(t.m_handle) {
        if (m_handle) {
            m_handle.promise().add_ref();
        }
    }

    ~shared_task() noexcept {
        destroy();
    }

    shared_task& operator=(shared_task&& t) noexcept {
        if (&t != this) {
            destroy();
            m_handle = t.m_handle;
            t.m_handle = nullptr;
        }
        return *this;
    }

    shared_task& operator=(const shared_task& t) noexcept {
        if (m_handle != t.m_handle) {
            destroy();
            m_handle = t.m_handle;
            if (m_handle) {
                m_handle.promise().add_ref();
            }
        }
        return *this;
    }

	/// \brief
	/// Query if the task result is complete.
	///
	/// Awaiting a task that is ready will not block.
    auto is_ready() const noexcept -> bool {
        return !m_handle || m_handle.promise().is_ready();
    }

    auto operator co_await() const {
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

    /// \brief
    /// Returns an awaitable that will await completion of the task without
    /// attempting to retrieve the result.
    auto when_ready() const noexcept -> bool {
        struct awaitable : awaitable_base {
            using awaitable_base::awaitable_base;
            static auto await_resume() noexcept -> void {}
        };
        return awaitable{m_handle};
    }

private:
    template<typename U>
    friend bool operator==(const shared_task<U>&, const shared_task<U>&) noexcept;

    template<typename U>
    friend bool operator!=(const shared_task<U>&, const shared_task<U>&) noexcept;

    auto destroy() noexcept -> void {
        if (m_handle) {
            if (!m_handle.promise().try_detach()) {
                m_handle.destroy();
            }
        }
    }

	std::coroutine_handle<promise_type> m_handle;
};

template<typename T>
bool operator==(const shared_task<T>& t1, const shared_task<T>& t2) noexcept {
    return t1.m_handle == t2.m_handle;
}

template<typename T>
bool operator!=(const shared_task<T>& t1, const shared_task<T>& t2) noexcept {
    return !(t1 == t2);
}

namespace detail {
template<typename T>
shared_task<T> shared_task_promise<T>::get_return_object() noexcept {
    return shared_task<T>{ std::coroutine_handle<shared_task_promise>::from_promise(*this) };
}

inline shared_task<> shared_task_promise<void>::get_return_object() noexcept {
    return shared_task{ std::coroutine_handle<shared_task_promise>::from_promise(*this) };
}

template<typename T>
shared_task<T&> shared_task_promise<T&>::get_return_object() noexcept {
    return shared_task<T&>{ std::coroutine_handle<shared_task_promise>::from_promise(*this) };
}

}

template<typename awaitable_type>
auto make_shared_task(awaitable_type awaitable)
    -> shared_task<detail::remove_rvalue_reference_t<
        typename detail::awaitable_traits<awaitable_type>::await_result_t>> {
    co_return co_await static_cast<awaitable_type&&>(awaitable);
}

}

#endif //SHARED_shared_task_H
