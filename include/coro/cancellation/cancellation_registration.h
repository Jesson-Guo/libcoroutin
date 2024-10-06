//
// Created by Jesson on 2024/10/3.
//

#ifndef CANCELLATION_REGISTRATION_H
#define CANCELLATION_REGISTRATION_H

#include "cancellation_token.h"
#include "cancellation_state.h"

#include <cstdint>
#include <functional>
#include <thread>

namespace coro {

namespace detail {

class cancellation_state;
struct cancellation_registration_list_chunk;
struct cancellation_registration_list;
struct cancellation_registration_result;
struct cancellation_registration_state;

}

class cancellation_registration {
public:
	/// Registers the callback to be executed when cancellation is requested
	/// on the cancellation_token.
	///
	/// The callback will be executed if cancellation is requested for the
	/// specified cancellation token. If cancellation has already been requested
	/// then the callback will be executed immediately, before the constructor
	/// returns. If cancellation has not yet been requested then the callback
	/// will be executed on the first thread to request cancellation inside
	/// the call to cancellation_source::request_cancellation().
	///
	/// \param token
	/// The cancellation token to register the callback with.
	///
	/// \param callback
	/// The callback to be executed when cancellation is requested on the
	/// cancellation_token. Note that callback must not throw an exception
	/// if called when cancellation is requested otherwise std::terminate()
	/// will be called.
	///
	/// \throw std::bad_alloc
	/// If registration failed due to insufficient memory available.
	template<
		typename FUNC,
		typename = std::enable_if_t<std::is_constructible_v<std::function<void()>, FUNC&&>>>
	cancellation_registration(cancellation_token token, FUNC&& callback)
		: m_callback(std::forward<FUNC>(callback)) {
		register_callback(std::move(token));
	}

	cancellation_registration(const cancellation_registration& other) = delete;
	cancellation_registration& operator=(const cancellation_registration& other) = delete;

	/// Deregisters the callback.
	///
	/// After the destructor returns it is guaranteed that the callback
	/// will not be subsequently called during a call to request_cancellation()
	/// on the cancellation_source.
	///
	/// This may block if cancellation has been requested on another thread
	/// is it will need to wait until this callback has finished executing
	/// before the callback can be destroyed.
    ~cancellation_registration();

private:
	friend class detail::cancellation_state;
	friend struct detail::cancellation_registration_state;

    void register_callback(cancellation_token&& token);

	detail::cancellation_state* m_state;
	std::function<void()> m_callback;
	detail::cancellation_registration_list_chunk* m_chunk;
	std::uint32_t m_entry_id;
};

}

coro::cancellation_registration::~cancellation_registration() {
    if (m_state) {
        m_state->deregister_callback(this);
        m_state->release_token_ref();
    }
}

void coro::cancellation_registration::register_callback(cancellation_token&& token) {
    auto* state = token.m_state;
    if (state && state->can_be_cancelled()) {
        m_state = state;
        if (state->try_register_callback(this)) {
            // 设置 token.m_state 为 nullptr：这表示 token 的状态已经转移给了 cancellation_registration，
            // 以确保 token 的状态不会与 cancellation_registration 再次发生交互。
            token.m_state = nullptr;
        }
        else {
            m_state = nullptr;
            m_callback();
        }
    }
    else {
        m_state = nullptr;
    }
}

struct coro::detail::cancellation_registration_list_chunk {
    static auto allocate(std::uint32_t entry_count) -> cancellation_registration_list_chunk*;
    static void free(cancellation_registration_list_chunk* chunk) noexcept {
        std::free(chunk);
    }

    std::atomic<cancellation_registration_list_chunk*> m_next_chunk;
    cancellation_registration_list_chunk* m_prev_chunk;
    std::atomic<std::int32_t> m_approx_free_count;
    std::uint32_t m_entry_count;
    std::atomic<cancellation_registration*> m_entries[1];
};

struct coro::detail::cancellation_registration_list {
    static auto allocate() -> cancellation_registration_list*;
    static void free(cancellation_registration_list* list) noexcept {
        std::free(list);
    }

    std::atomic<cancellation_registration_list_chunk*> m_approx_tail;
    cancellation_registration_list_chunk m_head_chunk;
};

struct coro::detail::cancellation_registration_result {
    cancellation_registration_result(cancellation_registration_list_chunk* chunk, std::uint32_t entry_id)
        : m_chunk(chunk)
        , m_entry_id(entry_id) {}

    cancellation_registration_list_chunk* m_chunk;
    std::uint32_t m_entry_id;
};

struct coro::detail::cancellation_registration_state {
    static auto allocate() -> cancellation_registration_state*;
    static void free(cancellation_registration_state* state) noexcept {
        std::free(state);
    }

    auto add_registration(cancellation_registration* registration) -> cancellation_registration_result;

    std::thread::id m_notification_thread_id;

    // 存储 N 个单独的列表并将线程随机分配到给定的列表中，以减少争用的机会。
    std::uint32_t m_list_count;
    std::atomic<cancellation_registration_list*> m_lists[1];
};

#endif //CANCELLATION_REGISTRATION_H
