#ifndef IO_SERVICE_H
#define IO_SERVICE_H

#include "../operation_cancelled.h"
#include "../on_scope_exit.h"
#include "../cancellation/cancellation_token.h"
#include "../cancellation/cancellation_registration.h"
#include "../detail/macos.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace coro {

class io_service {
public:
	class schedule_operation;
	class timed_schedule_operation;

    explicit io_service()
        : m_thread_state(0)
        , m_work_count(0)
        , m_schedule_operations(nullptr) {}

    ~io_service() {
        assert(m_schedule_operations.load(std::memory_order_relaxed) == nullptr);
        assert(m_thread_state.load(std::memory_order_relaxed) < active_thread_count_increment);
    }

	io_service(io_service&& other) = delete;
	io_service(const io_service& other) = delete;
	io_service& operator=(io_service&& other) = delete;
	io_service& operator=(const io_service& other) = delete;

    /// 返回一个操作，当被等待时，将挂起等待的协程，并将其重新调度到与此 io_service 关联的 I/O 线程上。
    [[nodiscard]] schedule_operation schedule() noexcept;

    /// 返回一个操作，当被等待时，将挂起等待的协程指定的延迟时间。
    /// 一旦延迟时间过去，协程将在与此 io_service 关联的 I/O 线程上恢复执行。
	template<typename REP, typename PERIOD>
    [[nodiscard]] timed_schedule_operation schedule_after(
        const std::chrono::duration<REP, PERIOD>& delay, cancellation_token ct = {}) noexcept;

    /// 处理事件直到 io_service 被停止。
    std::uint64_t process_events();

    /// 处理事件直到 io_service 被停止或队列中没有更多的待处理事件。
    std::uint64_t process_pending_events();

    /// 阻塞直到处理一个事件或 io_service 被停止。
    std::uint64_t process_one_event();

    /// 如果有任何事件待处理，则处理一个事件，否则如果没有待处理事件或 io_service 被停止，则立即返回。
    std::uint64_t process_one_pending_event();

    /// 关闭 io_service
    void stop() noexcept;

    /// 重置 io_service 以准备恢复事件处理。
    void reset() {
        const auto old_state = m_thread_state.fetch_and(~stop_requested_flag, std::memory_order_relaxed);
        // 检查是否没有活动线程正在运行事件循环。
        assert(old_state == stop_requested_flag);
    }

    /// 检测 io_service 是否被请求停止
    bool is_stop_requested() const noexcept {
        return (m_thread_state.load(std::memory_order_acquire) & stop_requested_flag) != 0;
    }

    /// 通知 io_service 新任务的开始，增加工作计数。
    void notify_work_started() noexcept {
        m_work_count.fetch_add(1, std::memory_order_relaxed);
    }

    /// 通知 io_service 某个工作任务已完成，减少工作计数。
    void notify_work_finished() noexcept;

	detail::macos::io_queue& io_queue() noexcept {
		return m_io_queue;
	}

private:
	friend class schedule_operation;

	void schedule_impl(schedule_operation* operation) noexcept;

	void try_reschedule_overflow_operations() noexcept;

    /// 尝试让线程进入事件循环，并对 io_service 中活动线程的计数进行管理。
    bool try_enter_event_loop() noexcept;

    void exit_event_loop() noexcept {
        m_thread_state.fetch_sub(active_thread_count_increment, std::memory_order_relaxed);
    }

    /// 处理单个事件，并根据传入的参数决定是否等待事件的到来。
    ///
    /// \param wait_for_event
    /// true:  表示调用 dequeue 时，如果队列中没有事件，应该阻塞等待直到有事件到达。
    /// false: 表示 dequeue 立即返回，如果没有事件可处理则返回 false。
	bool try_process_one_event(bool wait_for_event);

    static constexpr std::uint32_t stop_requested_flag = 1;
    static constexpr std::uint32_t active_thread_count_increment = 2;

    // 位 0: stop_requested_flag
    // 位 1-31: 当前正在运行事件循环的活动线程数
    std::atomic<std::uint32_t> m_thread_state;
    std::atomic<std::uint32_t> m_work_count;

    detail::macos::io_queue m_io_queue;

    // 准备运行但未能加入消息队列的调度操作的链表头
    std::atomic<schedule_operation*> m_schedule_operations;
};

class io_service::schedule_operation {
public:
    explicit schedule_operation(io_service& service) noexcept
        : m_service(service)
        , m_next(nullptr) {
        m_message = new detail::macos::io_message{};
    }

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> awaiter) noexcept;
    void await_resume() const noexcept;

private:
    friend class io_service;

    io_service& m_service;
    std::coroutine_handle<> m_awaiter;
    schedule_operation* m_next;
    detail::macos::io_message* m_message;
};

class io_work_scope {
public:
	explicit io_work_scope(io_service& service) noexcept : m_service(&service) {
		service.notify_work_started();
	}

	io_work_scope(const io_work_scope& other) noexcept : m_service(other.m_service) {
		if (m_service != nullptr) {
			m_service->notify_work_started();
		}
	}

	io_work_scope(io_work_scope&& other) noexcept : m_service(other.m_service) {
		other.m_service = nullptr;
	}

	~io_work_scope() {
		if (m_service != nullptr) {
			m_service->notify_work_finished();
		}
	}

	io_work_scope& operator=(io_work_scope other) noexcept {
	    std::swap(m_service, other.m_service);
		return *this;
	}

	io_service& service() noexcept {
		return *m_service;
	}

private:
	io_service* m_service;
};

}

#endif // IO_SERVICE_H
