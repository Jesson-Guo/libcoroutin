//
// Created by Jesson on 2024/10/3.
//

#ifndef ON_SCOPE_EXIT_H
#define ON_SCOPE_EXIT_H

#include <type_traits>
#include <exception>

namespace coro {

template<typename F>
class scoped_lambda {
public:
    explicit scoped_lambda(F&& func)
        : m_func(std::forward<F>(func))
        , m_cancelled(false) {}

    scoped_lambda(const scoped_lambda& other) = delete;

    scoped_lambda(scoped_lambda&& other) noexcept
        : m_func(std::forward<F>(other.m_func))
        , m_cancelled(other.m_cancelled) {
        other.cancel();
    }

    ~scoped_lambda() {
        if (!m_cancelled) {
            m_func();
        }
    }

    auto cancel() {
        m_cancelled = true;
    }

    auto call_now() {
        m_cancelled = true;
        m_func();
    }

private:
    F m_func;
    bool m_cancelled;
};

template<typename F, bool CALL_ON_FAILURE>
class conditional_scoped_lambda {
public:
    explicit conditional_scoped_lambda(F&& func)
        : m_func(std::forward<F>(func))
        , m_uncaught_exception_count(std::uncaught_exceptions())
        , m_cancelled(false) {}

    conditional_scoped_lambda(const conditional_scoped_lambda& other) = delete;

    conditional_scoped_lambda(conditional_scoped_lambda&& other)
        noexcept(std::is_nothrow_move_constructible_v<F>)
        : m_func(std::forward<F>(other.m_func))
        , m_uncaught_exception_count(other.m_uncaught_exception_count)
        , m_cancelled(other.m_cancelled) {
        other.cancel();
    }

    ~conditional_scoped_lambda() noexcept(CALL_ON_FAILURE || noexcept(std::declval<F>()())) {
        if (!m_cancelled && is_unwinding_due_to_exception() == CALL_ON_FAILURE) {
            m_func();
        }
    }

    void cancel() noexcept {
        m_cancelled = true;
    }

private:
    auto is_unwinding_due_to_exception() const noexcept -> bool {
        return std::uncaught_exceptions() > m_uncaught_exception_count;
    }

    F m_func;
    int m_uncaught_exception_count;
    bool m_cancelled;
};

template<typename F>
auto on_scope_exit(F&& func) {
    return scoped_lambda<F>{ std::forward<F>(func) };
}

template<typename F>
auto on_scope_failure(F&& func) {
    return conditional_scoped_lambda<F, true>{ std::forward<F>(func) };
}

template<typename F>
auto on_scope_success(F&& func) {
    return conditional_scoped_lambda<F, false>{ std::forward<F>(func) };
}

}

#endif //ON_SCOPE_EXIT_H
