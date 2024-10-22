//
// Created by Jesson on 2024/9/25.
//

#ifndef ASYNC_GENERATOR_H
#define ASYNC_GENERATOR_H

#include <coroutine>
#include <exception>
#include <future>

namespace coro
{
template<typename T>
class async_generator;

namespace detail
{
template<typename T>
class async_generator_iterator;
class async_generator_yield_operation;
class async_generator_advance_operation;

class async_generator_promise_base {
public:
    async_generator_promise_base() noexcept : m_exception(nullptr) {}
    async_generator_promise_base(const async_generator_promise_base& other) = delete;
    async_generator_promise_base& operator=(const async_generator_promise_base& other) = delete;

    auto initial_suspend() const noexcept -> std::suspend_always {return {};}
    auto final_suspend() noexcept -> async_generator_yield_operation;

    auto unhandled_exception() noexcept -> void {
        m_exception = std::current_exception();
    }

    auto return_void() noexcept -> void {}

    // use after begin() or ++ operation
    auto is_finished() const noexcept -> bool {
        return m_current_value == nullptr;
    }

    auto rethrow_if_unhandled_exception() const -> void {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
    }

protected:
    async_generator_yield_operation internal_yield_value() noexcept;
    void* m_current_value;

private:
    friend class async_generator_yield_operation;
    friend class async_generator_advance_operation;

    std::exception_ptr m_exception;
    std::coroutine_handle<> m_consumer_handle;
};

class async_generator_yield_operation final {
public:
    explicit async_generator_yield_operation(const std::coroutine_handle<> consumer) noexcept : m_consumer(consumer) {}

    auto await_ready() noexcept -> bool {return false;}

    auto await_suspend(std::coroutine_handle<> producer) noexcept -> std::coroutine_handle<> {
        return m_consumer;
    }

    auto await_resume() noexcept -> void {}

private:
    std::coroutine_handle<> m_consumer;
};

inline auto async_generator_promise_base::internal_yield_value() noexcept -> async_generator_yield_operation {
    return async_generator_yield_operation{m_consumer_handle};
}

inline auto async_generator_promise_base::final_suspend() noexcept -> async_generator_yield_operation {
    m_current_value = nullptr;
    return internal_yield_value();
}

class async_generator_advance_operation {
protected:
    explicit async_generator_advance_operation(std::nullptr_t) noexcept
        : m_promise(nullptr)
        , m_producer_handle(nullptr) {}

    async_generator_advance_operation(async_generator_promise_base& promise, const std::coroutine_handle<> producer) noexcept
        : m_promise(std::addressof(promise))
        , m_producer_handle(producer) {}

public:
    auto await_ready() const noexcept -> bool {return false;}

    auto await_suspend(std::coroutine_handle<> consumer) noexcept -> std::coroutine_handle<> {
        m_promise->m_consumer_handle = consumer;
        return m_producer_handle;
    }

protected:
    async_generator_promise_base* m_promise;
    std::coroutine_handle<> m_producer_handle;
};

template<typename T>
class async_generator_promise final : public async_generator_promise_base {
    using value_type = std::remove_reference_t<T>;
public:
    async_generator_promise() noexcept = default;
    async_generator<T> get_return_object() noexcept;

    auto yield_value(value_type& value) noexcept -> async_generator_yield_operation {
        m_current_value = std::addressof(value);
        return internal_yield_value();
    }

    auto yield_value(value_type&& value) noexcept -> async_generator_yield_operation {
        return yield_value(value);
    }

    auto value() noexcept -> T& {
        return *static_cast<T*>(m_current_value);
    }
};

template<typename T>
class async_generator_promise<T&&> final : public async_generator_promise_base {
public:
    async_generator_promise() noexcept = default;
    async_generator<T> get_return_object() noexcept;

    auto yield_value(T&& value) noexcept -> async_generator_yield_operation {
        m_current_value = std::addressof(value);
        return internal_yield_value();
    }

    auto value() noexcept -> T& {
        return std::move(*static_cast<T*>(m_current_value));
    }
};

template<typename T>
class async_generator_increment_operation final : public async_generator_advance_operation {
public:
    explicit async_generator_increment_operation(async_generator_iterator<T>& it) noexcept
        : async_generator_advance_operation(it.m_handle.promise(), it.m_handle)
        , m_iterator(it) {}

    auto await_resume() -> async_generator_iterator<T>&;

private:
    async_generator_iterator<T>& m_iterator;
};

template<typename T>
class async_generator_iterator final {
    using promise_type = async_generator_promise<T>;
public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = std::remove_reference_t<T>;
    using reference = std::add_lvalue_reference_t<T>;
    using pointer = std::add_pointer_t<value_type>;

    async_generator_iterator(std::nullptr_t) noexcept : m_handle(nullptr) {}
    async_generator_iterator(std::coroutine_handle<promise_type> handle) noexcept : m_handle(handle) {}

    async_generator_increment_operation<T> operator++() noexcept {
        return async_generator_increment_operation<T>{ *this };
    }

    reference operator*() const noexcept {
        return m_handle.promise().value();
    }

    bool operator==(const async_generator_iterator& other) const noexcept {
        return m_handle == other.m_handle;
    }

    bool operator!=(const async_generator_iterator& other) const noexcept {
        return !(*this == other);
    }

private:
    friend class async_generator_increment_operation<T>;
    std::coroutine_handle<promise_type> m_handle;
};

template<typename T>
auto async_generator_increment_operation<T>::await_resume() -> async_generator_iterator<T>& {
    if (m_promise->is_finished()) {
        m_iterator = async_generator_iterator<T>{nullptr};
        m_promise->rethrow_if_unhandled_exception();
    }
    return m_iterator;
}

template<typename T>
class async_generator_begin_operation final : public async_generator_advance_operation {
	using promise_type = async_generator_promise<T>;
	using handle_type = std::coroutine_handle<promise_type>;
public:
    explicit async_generator_begin_operation(std::nullptr_t) : async_generator_advance_operation(nullptr) {}
    explicit async_generator_begin_operation(std::coroutine_handle<promise_type> producer_handle) noexcept
        : async_generator_advance_operation(producer_handle.promise(), producer_handle) {}

    auto await_ready() noexcept -> bool {
        return m_promise == nullptr || async_generator_advance_operation::await_ready();
    }

    auto await_resume() -> async_generator_iterator<T> {
        if (m_promise == nullptr) {
            return async_generator_iterator<T>{nullptr};
        }
        if (m_promise->is_finished()) {
            m_promise->rethrow_if_unhandled_exception();
            return async_generator_iterator<T>{nullptr};
        }
        return async_generator_iterator<T>{
            std::coroutine_handle<promise_type>::from_promise(*static_cast<promise_type*>(m_promise))
        };
    }
};

}

template<typename T>
class async_generator {
public:
    using promise_type = detail::async_generator_promise<T>;
    using iterator = detail::async_generator_iterator<T>;

    async_generator() noexcept : m_handle(nullptr) {}

    explicit async_generator(std::coroutine_handle<promise_type> handle) noexcept : m_handle(handle) {}

    explicit async_generator(promise_type& promise) noexcept
        : m_handle(std::coroutine_handle<promise_type>::from_promise(promise)) {}

    async_generator(async_generator&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    async_generator& operator=(async_generator&& other) noexcept {
        async_generator g(std::move(other));
        std::swap(m_handle, g.m_handle);
        return *this;
    }

    async_generator(const async_generator&) = delete;
    async_generator& operator=(const async_generator&) = delete;

    ~async_generator() noexcept {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    auto begin() noexcept {
        if (!m_handle) {
            return detail::async_generator_begin_operation<T>{nullptr};
        }
        return detail::async_generator_begin_operation<T>{m_handle};
    }

    auto end() noexcept {
        return iterator{nullptr};
    }

private:
    std::coroutine_handle<promise_type> m_handle;
};

namespace detail {

template<typename T>
async_generator<T> async_generator_promise<T>::get_return_object() noexcept {
    return async_generator<T>{*this};
}

template<typename T>
async_generator<T> async_generator_promise<T&&>::get_return_object() noexcept {
    return async_generator<T>{std::move(*this)};
}

}

template<typename FUNC, typename T>
async_generator<std::invoke_result_t<FUNC&, decltype(*std::declval<typename async_generator<T>::iterator&>())>>
fmap(FUNC func, async_generator<T> source) {
    static_assert(
        !std::is_reference_v<FUNC>,
        "Passing by reference to async_generator<T> coroutine is unsafe. "
        "Use std::ref or std::cref to explicitly pass by reference.");

    // Explicitly hand-coding the loop here rather than using range-based
    // for loop since it's difficult to std::forward<???> the value of a
    // range-based for-loop, preserving the value category of operator*
    // return-value.
    auto it = co_await source.begin();
    const auto itEnd = source.end();
    while (it != itEnd) {
        co_yield std::invoke(func, *it);
        (void)co_await ++it;
    }
}

}

#endif //ASYNC_GENERATOR_H
