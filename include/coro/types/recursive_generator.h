//
// Created by Jesson on 2024/9/20.
//

#ifndef RECURSIVE_GENERATOR_H
#define RECURSIVE_GENERATOR_H

#include "generator.h"


#include <coroutine>
#include <exception>
#include <future>

namespace coro {

template<typename T>
class recursive_generator {
public:
    using value_type = std::remove_reference_t<T>;
    using reference_type = std::conditional_t<std::is_reference_v<T>, T, T&>;
    using pointer_type = value_type*;

    class promise_type final {
    public:
        promise_type() : m_value(nullptr), m_exception(nullptr), m_root(nullptr), m_node(nullptr) {}

        promise_type(const promise_type&) = delete;
        promise_type& operator=(const promise_type&) = delete;
        promise_type(promise_type&&) = default;
        promise_type& operator=(promise_type&&) = default;

        auto get_return_object() noexcept {
            return recursive_generator<T>{ *this };
        }

        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void unhandled_exception() noexcept {
            m_exception = std::current_exception();
        }

        void return_void() noexcept {}

        auto yield_value(T& value) noexcept -> std::suspend_always {
            m_value = std::addressof(value);
            return std::suspend_always{};
        }

        auto yield_value(T&& value) noexcept -> std::suspend_always {
            m_value = std::addressof(value);
            return std::suspend_always{};
        }

        auto yield_value(recursive_generator& generator) noexcept {
            struct awaitable {
                awaitable(promise_type* prom) : m_child_promise(prom) {}

                auto await_ready() noexcept -> bool {
                    return this->m_child_promise == nullptr;
                }

                auto await_suspend(std::coroutine_handle<promise_type>) noexcept -> void {}

                auto await_resume() noexcept -> void {
                    if (this->m_child_promise) {
                        this->m_child_promise->throw_if_exception();
                    }
                }
            private:
                promise_type* m_child_promise;
            };

            if (generator.m_promise) {
                m_root->m_node = generator.m_promise;
                generator.m_promise->m_root = m_root;
                generator.m_promise->m_node = this;
                generator.m_promise->resume();

                if (!generator.m_promise->is_complete()) {
                    return awaitable{generator.m_promise};
                }
                m_root->m_node = this;
            }
            return awaitable{ nullptr };
        }

        auto yield_value(recursive_generator&& generator) noexcept {
            yield_value(std::forward<recursive_generator>(generator));
        }

        template<typename U>
        auto await_transform(U&& value) = delete;

        void destroy() noexcept {
            std::coroutine_handle<promise_type>::from_promise(*this).destroy();
        }

        void throw_if_exception() const noexcept {
            if (m_exception) {
                std::rethrow_exception(std::move(m_exception));
            }
        }

        auto is_complete() const noexcept {
            return std::coroutine_handle<promise_type>::from_promise(*this).done();
        }

        T& value() noexcept {
            assert(this == m_root);
            assert(!is_complete());
            return *(m_node->m_value);
        }

        auto pull() noexcept {
            assert(this == m_root);
            assert(!is_complete());
            if (m_node) {
                m_node->resume();
                while (m_node != this && m_node->is_complete()) {
                    m_node = m_node->m_node;
                    m_node->resume();
                }
            }
        }

    private:
        auto resume() {
            std::coroutine_handle<promise_type>::from_promise(*this).resume();
        }

        pointer_type m_value;
        std::exception_ptr m_exception;
        promise_type* m_root;

        // If this is the promise of the root generator then this field
        // is a pointer to the leaf promise.
        // For non-root generators this is a pointer to the parent promise.
        promise_type* m_node;
    };

    recursive_generator() noexcept : m_promise(nullptr) {}
    explicit recursive_generator(promise_type& promise) noexcept : m_promise(&promise) {}
    recursive_generator(recursive_generator&& generator) noexcept : m_promise(generator.m_promise) {
        generator.m_promise = nullptr;
    }
    auto operator=(recursive_generator&& generator) noexcept -> recursive_generator& {
        if (this != &generator) {
            if (m_promise) {
                m_promise->destroy();
            }
            m_promise = std::move(generator.m_promise);
            generator.m_promise = nullptr;
        }
        return *this;
    }

    recursive_generator(const recursive_generator&) noexcept = delete;
    recursive_generator& operator=(const recursive_generator&) noexcept = delete;

    ~recursive_generator() noexcept {
        if (m_promise) {
            m_promise->destroy();
        }
    }

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = std::remove_reference_t<T>;
        using difference_type = std::ptrdiff_t;
        using pointer = std::add_pointer_t<T>;
        using reference = std::conditional_t<std::is_reference_v<T>, T, T&>;

        iterator() noexcept : m_promise(nullptr) {}
        explicit iterator(promise_type* promise) noexcept : m_promise(promise) {}

        auto operator==(const iterator& it) const noexcept -> bool {
            return m_promise == it.m_promise;
        }

        auto operator!=(const iterator& it) const noexcept -> bool {
            return m_promise != it.m_promise;
        }

        auto operator++() noexcept -> iterator& {
            assert(m_promise);
            assert(!m_promise->is_complete());

            m_promise->pull();
            if (m_promise->is_complete()) {
                auto* promise = m_promise;
                m_promise = nullptr;
                promise->throw_if_exception();
            }
            return *this;
        }

        auto operator++(int) noexcept -> void {
            (void)operator++();
        }

        auto operator*() const noexcept -> reference {
            assert(m_promise);
            return static_cast<reference>(m_promise->value());
        }

        auto operator->() const noexcept -> pointer {
            return std::addressof(operator*());
        }

    private:
        promise_type* m_promise;
    };

    auto begin() noexcept -> iterator {
        if (m_promise) {
            m_promise->pull();
            if (!m_promise->is_complete()) {
                return iterator(m_promise);
            }
            m_promise->throw_if_exception();
        }
        return iterator(nullptr);
    }

    auto end() noexcept -> iterator {
        return iterator(nullptr);
    }

private:
    friend class promise_type;
    promise_type* m_promise;
};

}

#endif //RECURSIVE_GENERATOR_H
