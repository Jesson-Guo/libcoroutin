//
// Created by Jesson on 2024/10/2.
//

#ifndef STATIC_THREAD_POOL_H
#define STATIC_THREAD_POOL_H

#include "auto_reset_event.h"
#include "spin_mutex.h"
#include "spin_wait.h"

#include <atomic>
#include <cassert>
#include <coroutine>
#include <vector>

namespace coro {

class thread_pool {
public:
    class schedule_operation;

private:
    class thread_state;

public:
    thread_pool();

    explicit thread_pool(std::uint32_t thread_count);

    ~thread_pool();

    class schedule_operation {
    public:
        explicit schedule_operation(thread_pool* pool, std::function<void()> func = nullptr) noexcept
            : m_thread_pool(pool)
            , m_func(std::move(func)) {}

        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        void await_resume() noexcept {}

        void execute() {
            if (m_func) {
                // 执行函数任务
                m_func();
            }
            else {
                // 恢复协程
                m_awaiting_handle.resume();
            }
        }

    private:
        friend class thread_pool;

        thread_pool* m_thread_pool;
        std::coroutine_handle<> m_awaiting_handle;
        std::function<void()> m_func;
    };

    std::uint32_t thread_count() const noexcept { return m_thread_count; }

    schedule_operation schedule() noexcept { return schedule_operation{ this }; }

    void schedule(std::function<void()> func) noexcept;

private:
    static thread_local thread_state* s_cur_state;
    static thread_local thread_pool* s_cur_thread_pool;

    static constexpr std::size_t global_queue_size = 1024 * 1024 / sizeof(void*);
    static constexpr std::make_signed_t<std::size_t> diff(size_t a, size_t b) {
        return static_cast<std::make_signed_t<std::size_t>>(a - b);
    }

    const std::uint32_t m_thread_count;
    std::unique_ptr<thread_state[]> m_thread_states;

    std::thread m_daemon_thread;
    std::vector<std::thread> m_threads;

    std::atomic<bool> m_stop;

    std::mutex m_global_queue_mutex;

    std::unique_ptr<std::atomic<schedule_operation*>[]> m_global_queue;

    std::size_t m_global_mask;

    std::atomic<std::size_t> m_global_head;
    std::atomic<std::size_t> m_global_tail;

    std::atomic<std::uint32_t> m_sleep_thread_count;

    /**
     * 每个线程的主要工作函数，循环处理局部队列中的任务，或者从全局队列和其他线程窃取任务。
     *   -
     * 首先尝试从局部队列中获取任务，如果没有任务，则尝试从全局队列获取，最后尝试从其他线程窃取任务。
     *   - 如果找不到任务，线程会在短时间内自旋（等待任务），若仍没有任务，则进入睡眠等待任务的到来。
     *   - 唤醒逻辑则通过 ` notify_intent_to_sleep` 和 ` sleep_until_woken` 来实现。
     */
    void run_worker_thread(std::uint32_t thread_id) noexcept;
    //
    // void run_daemon_thread() noexcept;

    /**
     * 将 `m_stop` 设置为 `true`，并尝试唤醒所有线程，等待线程完成后再退出。
     */
    auto shutdown() -> void;

    /**
     * 将任务添加到当前线程的局部队列中，如果失败则放入全局队列，并唤醒能够唤醒的最大数量线程处理任务。
     */
    auto schedule_impl(schedule_operation* op) noexcept -> void;

    /**
     * 将任务放入全局队列，如果队列满了不会扩容，返回添加失败。需要处理多线程的竞争条件。
     */
    auto try_global_enqueue(schedule_operation* op) noexcept -> bool;

    /**
     * 从全局队列取出一个任务。需要处理多线程的竞争条件。
     */
    auto try_global_dequeue() noexcept -> schedule_operation*;

    auto has_queued_work(std::uint32_t thread_id) noexcept -> bool;

    auto approx_has_queued_work(std::uint32_t thread_id) const noexcept -> bool;

    /**
     * 估计当前待处理的任务总数，包括全局队列中的任务数量和各线程本地队列中的任务数量
     */
    auto approx_total_work() const noexcept -> std::uint32_t;

    /**
     * 通知系统当前线程准备进入睡眠，增加睡眠线程的计数。
     */
    auto notify_intent_to_sleep(std::uint32_t thread_id) noexcept -> void;

    auto try_clear_intent_to_sleep(std::uint32_t thread_id) noexcept -> void;

    /**
     * 尝试从其他线程的局部队列中窃取任务，任务窃取优先从任务数量多的线程中窃取。
     */
    auto try_steal(std::uint32_t cur_thread_id) noexcept -> schedule_operation*;

    /**
     * 根据当前任务数量和睡眠线程的数量，批量地唤醒线程。
     */
    auto wake_threads(std::uint32_t num_threads) noexcept -> void;

};

}

#endif //STATIC_THREAD_POOL_H
