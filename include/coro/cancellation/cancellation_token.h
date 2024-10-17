//
// Created by Jesson on 2024/10/3.
//

#ifndef CANCELLATION_TOKEN_H
#define CANCELLATION_TOKEN_H

namespace coro {

class cancellation_source;
class cancellation_registration;

namespace detail {

class cancellation_state;

}

class cancellation_token {
public:
    /// Construct to a cancellation token that can't be cancelled.
    cancellation_token() noexcept;
    cancellation_token(const cancellation_token& other) noexcept;
    cancellation_token(cancellation_token&& other) noexcept;
    ~cancellation_token();
    cancellation_token& operator=(const cancellation_token& other) noexcept;
    cancellation_token& operator=(cancellation_token&& other) noexcept;
    void swap(cancellation_token& other) noexcept;

    /// Query if it is possible that this operation will be cancelled
    /// or not.
    ///
    /// Cancellable operations may be able to take more efficient code-paths
    /// if they don't need to handle cancellation requests.
    [[nodiscard]] auto can_be_cancelled() const noexcept -> bool;

    /// Query if some thread has requested cancellation on an associated
    /// cancellation_source object.
    [[nodiscard]] auto is_cancellation_requested() const noexcept -> bool;

    /// Throws coro::operation_cancelled exception if cancellation
    /// has been requested for the associated operation.
    auto throw_if_cancellation_requested() const -> void;

private:
    friend class cancellation_source;
    friend class cancellation_registration;

    explicit cancellation_token(detail::cancellation_state* state) noexcept;

    detail::cancellation_state* m_state;
};

inline void swap(cancellation_token& a, cancellation_token& b) noexcept {
    a.swap(b);
}

}

#endif //CANCELLATION_TOKEN_H
