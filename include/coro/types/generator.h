//
// Created by Jesson on 2024/9/17.
//

#ifndef GENERATOR_H
#define GENERATOR_H

#include <coroutine>
#include <exception>
#include <future>

namespace coro {

template<typename T>
class generator;

namespace detail {

template<typename T>
class generator_promise {
public:
    using value_type = std::remove_reference_t<T>;
    using reference_type = std::conditional_t<std::is_reference_v<T>, T, T&>;
    using pointer_type = value_type*;

    generator_promise() = default;

    generator<T> get_return_object() noexcept;

    constexpr std::suspend_always initial_suspend() noexcept { return {}; }
    constexpr std::suspend_always final_suspend() noexcept { return {}; }

    template<
        typename U = T,
        std::enable_if_t<!std::is_rvalue_reference_v<U>, int> = 0>
    std::suspend_always yield_value(std::remove_reference_t<T>& value) {
        m_value = std::addressof(value);
        return {};
    }

    std::suspend_always yield_value(std::remove_reference_t<T>&& value) {
        m_value = std::addressof(value);
        return {};
    }

    auto unhandled_exception() noexcept {
        m_exception = std::current_exception();
    }

    auto return_void() noexcept {}

    reference_type value() const noexcept {
        return static_cast<reference_type>(*m_value);
    }

    template<typename U>
    std::suspend_never await_transform(U&& value) = delete;

    auto rethrow_if_exception() const -> void {
        if (m_exception) {
            std::rethrow_exception(m_exception);
        }
    }

private:
    pointer_type m_value;
    std::exception_ptr m_exception;
};

struct generator_sentinel;

template<typename T>
class generator_iterator {
    using coroutine_handle = std::coroutine_handle<generator_promise<T>>;
public:
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = typename generator_promise<T>::value_type;
    using pointer = typename generator_promise<T>::pointer_type;
    using reference = typename generator_promise<T>::reference_type;

    generator_iterator() = default;

    explicit generator_iterator(coroutine_handle handle) noexcept : m_handle(handle) {}

    friend bool operator==(const generator_iterator& it, generator_sentinel) noexcept {
        return !it.m_handle || it.m_handle.done();
    }

    friend bool operator!=(const generator_iterator& it, generator_sentinel s) noexcept {
        return !(it == s);
    }

    friend bool operator==(generator_sentinel s, const generator_iterator& it) noexcept {
        return it == s;
    }

    friend bool operator!=(generator_sentinel s, const generator_iterator& it) noexcept {
        return !(it == s);
    }

    generator_iterator& operator++() {
        if (m_handle) {
            m_handle.resume();
            if (m_handle.done()) {
                m_handle.promise().rethrow_if_exception();
            }
        }
        return *this;
    }

    void operator++(int) {
        (void)operator++();
    }

    reference operator*() const noexcept {
        return m_handle.promise().value();
    }

    pointer operator->() const noexcept {
        return std::addressof(operator*());
    }

private:
    coroutine_handle m_handle;
};
}

template<typename T>
class generator {
public:
    using promise_type = detail::generator_promise<T>;
    using iterator = detail::generator_iterator<T>;
    using sentinel = detail::generator_sentinel;

    generator() noexcept : m_handle(nullptr) {}

    generator(generator&& gen) noexcept : m_handle(gen.m_handle) {
        gen.m_handle = nullptr;
    }

    generator(const generator& gen) noexcept = delete;

    ~generator() noexcept {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    generator& operator=(generator& gen) noexcept = delete;

    generator& operator=(generator&& gen) noexcept {
        m_handle = gen.m_handle;
        gen.m_handle = nullptr;
        return *this;
    }

    auto begin() noexcept -> iterator {
        if (m_handle) {
            m_handle.resume();
            if (m_handle.done()) {
                m_handle.promise().rethrow_if_exception();
            }
        }
        return iterator{m_handle};
    }

    auto end() noexcept -> sentinel {
        return sentinel{};
    }

private:
    friend class detail::generator_promise<T>;
    explicit generator(std::coroutine_handle<promise_type> handle) noexcept : m_handle(handle) {}
    std::coroutine_handle<promise_type> m_handle;
};

namespace detail {
template<typename T>
auto generator_promise<T>::get_return_object() noexcept -> generator<T> {
    return generator<T>{std::coroutine_handle<generator_promise<T>>::from_promise(*this)};
}
}
}

#endif //GENERATOR_H
