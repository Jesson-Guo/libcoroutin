//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/thread_pool.h"

namespace coro {

thread_local thread_pool::thread_state* thread_pool::s_cur_state = nullptr;
thread_local thread_pool* thread_pool::s_cur_thread_pool = nullptr;

class thread_pool::thread_state {
public:
    thread_state()
        : m_local_queue(std::make_unique<std::atomic<schedule_operation*>[]>(local_queue_size))
        , m_mask(local_queue_size-1)
        , m_head(0)
        , m_tail(0)
        , m_is_sleep(false) {}

    auto try_wakeup() -> bool {
        if (m_is_sleep.load(std::memory_order_seq_cst)) {
            if (m_is_sleep.exchange(false, std::memory_order_seq_cst)) {
                m_wakeup_event.set();
                return true;
            }
        }
        return false;
    }

    auto notify_intent_to_sleep() noexcept -> void {
        m_is_sleep.store(true, std::memory_order_relaxed);
    }

    auto sleep_until_woken() noexcept -> void {
        try {
            m_wakeup_event.wait();
        }
        catch (...) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(1ms);
        }
    }

    auto has_queued_work() noexcept -> bool {
        std::scoped_lock lock(m_remote_mutex);
        const auto tail = m_tail.load(std::memory_order_relaxed);
        const auto head = m_head.load(std::memory_order_seq_cst);
        return diff(head, tail) > 0;
    }

    auto approx_has_queued_work() noexcept -> bool {
        const auto tail = m_tail.load(std::memory_order_relaxed);
        const auto head = m_head.load(std::memory_order_relaxed);
        return diff(head, tail) > 0;
    }

    auto queue_size() noexcept -> size_t {
        const auto tail = m_tail.load(std::memory_order_relaxed);
        const auto head = m_head.load(std::memory_order_relaxed);
        return static_cast<size_t>(diff(head, tail));
    }

    auto try_local_enqueue(schedule_operation* op) noexcept -> bool {
        auto head = m_head.load(std::memory_order_relaxed);
        auto tail = m_tail.load(std::memory_order_relaxed);
        if (diff(head, tail) < static_cast<offset_t>(m_mask)) {
            m_local_queue[head & m_mask].store(op, std::memory_order_relaxed);
            m_head.store(head + 1, std::memory_order_seq_cst);
            return true;
        }

        if (m_mask + 1 >= max_local_queue_size) {
            return false;
        }

        const size_t new_size = (m_mask + 1) * 2;
        std::unique_ptr<std::atomic<schedule_operation*>[]> new_local_queue{
            new std::atomic<schedule_operation*>[new_size]
        };
        if (!new_local_queue) {
            return false;
        }

        if (!m_remote_mutex.try_lock()) {
            return false;
        }

        std::scoped_lock lock{ std::adopt_lock, m_remote_mutex };
        tail = m_tail.load(std::memory_order_relaxed);

        const size_t new_mask = new_size - 1;
        for (auto i = tail; i != head; ++i) {
            new_local_queue[i & new_mask].store(
                m_local_queue[i & m_mask].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }

        new_local_queue[head & new_mask].store(op, std::memory_order_relaxed);
        m_head.store(head + 1, std::memory_order_relaxed);
        m_local_queue = std::move(new_local_queue);
        m_mask = new_mask;
        return true;
    }

    auto try_local_pop() noexcept -> schedule_operation* {
        auto head = m_head.load(std::memory_order_relaxed);
        auto tail = m_tail.load(std::memory_order_relaxed);
        if (diff(head, tail) <= 0) {
            return nullptr;
        }

        auto new_head = head - 1;
        m_head.store(new_head, std::memory_order_seq_cst);
        tail = m_tail.load(std::memory_order_seq_cst);
        // 如果操作过程中head和tail被修改则恢复原样。
        if (diff(new_head, tail) < 0) {
            std::lock_guard lock{ m_remote_mutex };
            tail = m_tail.load(std::memory_order_relaxed);
            if (diff(new_head, tail) < 0) {
                m_head.store(head, std::memory_order_relaxed);
                return nullptr;
            }
        }

        return m_local_queue[new_head & m_mask].load(std::memory_order_relaxed);
    }

    auto try_steal() noexcept -> schedule_operation* {
        if (!m_remote_mutex.try_lock()) {
            return nullptr;
        }

        std::scoped_lock lock{ std::adopt_lock, m_remote_mutex };

        auto head = m_head.load(std::memory_order_seq_cst);
        auto tail = m_tail.load(std::memory_order_seq_cst);
        if (diff(head, tail) <= 0) {
            return nullptr;
        }

        m_tail.store(tail + 1, std::memory_order_seq_cst);
        head = m_head.load(std::memory_order_seq_cst);
        if (diff(head, tail + 1) >= 0) {
            return m_local_queue[tail & m_mask].load(std::memory_order_relaxed);
        }
        m_tail.store(tail, std::memory_order_relaxed);
        return nullptr;
    }

private:
    using offset_t = std::make_signed_t<std::size_t>;

    // Keep each thread's local queue under 1MB
    static constexpr std::size_t max_local_queue_size = 1024 * 1024 / sizeof(void*);
    static constexpr std::size_t local_queue_size = 256;

    static constexpr offset_t diff(size_t a, size_t b) {
        return static_cast<offset_t>(a - b);
    }

    std::unique_ptr<std::atomic<schedule_operation*>[]> m_local_queue;

    std::size_t m_mask;

    std::atomic<std::size_t> m_head;
    std::atomic<std::size_t> m_tail;

    std::atomic<bool> m_is_sleep;

    spin_mutex m_remote_mutex;

    auto_reset_event m_wakeup_event;
};

thread_pool::thread_pool() : thread_pool(std::thread::hardware_concurrency()) {}

thread_pool::thread_pool(std::uint32_t thread_count)
    : m_thread_count(thread_count > 0 ? thread_count : 1)
    , m_thread_states(std::make_unique<thread_state[]>(m_thread_count))
    , m_stop(false)
    , m_global_queue(std::make_unique<std::atomic<schedule_operation*>[]>(global_queue_size))
    , m_global_mask(global_queue_size - 1)
    , m_global_head(0)
    , m_global_tail(0)
    , m_sleep_thread_count(0) {
    m_threads.reserve(m_thread_count);
    try {
        for (auto i = 0u; i < m_thread_count; ++i) {
            m_threads.emplace_back([this, i]() {
                this->run_worker_thread(i);
            });
        }
        // 启动守护线程
        // m_daemon_thread = std::thread([this]() {
        //     this->run_daemon_thread();
        // });
    }
    catch (...) {
        try {
            shutdown();
        }
        catch (...) {
            std::terminate();
        }
        throw;
    }
}

thread_pool::~thread_pool() {
    shutdown();
}

void thread_pool::schedule_operation::await_suspend(std::coroutine_handle<> handle) noexcept {
    m_awaiting_handle = handle;
    m_thread_pool->schedule_impl(this);
}

void thread_pool::schedule(std::function<void()> func) noexcept {
    auto op = std::make_unique<schedule_operation>(this, std::move(func));
    schedule_impl(std::move(op).get()); // 将所有权转移给 schedule_impl
}

void thread_pool::run_worker_thread(std::uint32_t thread_id) noexcept {
    auto& state = m_thread_states[thread_id];
    s_cur_state = &state;
    s_cur_thread_pool = this;

    auto try_get_remote = [&]() {
        // 首先尝试从全局队列获取任务
        auto* op = try_global_dequeue();
        if (!op) {
            // 如果全局队列为空，尝试从其他线程的局部队列窃取任务
            op = try_steal(thread_id);
        }
        return op;
    };

    while (!m_stop.load(std::memory_order_relaxed)) {// 处理本地队列中的任务
        schedule_operation* op;

        while (true) {
            op = state.try_local_pop();
            if (!op) {
                op = try_get_remote();
                if (!op) {
                    // 本地和全局队列均无任务，跳出循环
                    break;
                }
            }

            op->execute();
            if (op->m_func) {
                delete op;
            }
        }

        // 本地和远程队列均无任务，开始自旋等待新任务
        spin_wait spinner;
        bool found_op = false;
        schedule_operation* remote_op = nullptr;

        // 自旋30次，尝试获取新任务
        for (int i = 0; i < 30; ++i) {
            if (m_stop.load(std::memory_order_relaxed)) {
                return; // 接收到关闭信号，退出线程
            }

            spinner.spin_one(); // 执行一次自旋等待

            if (approx_has_queued_work(thread_id)) {
                remote_op = try_get_remote();
                if (remote_op) {
                    found_op = true;
                    break; // 找到任务，跳出自旋循环
                }
            }
        }

        if (found_op) {
            // 找到任务，恢复协程执行
            op = remote_op;
            assert(op != nullptr);

            op->execute();
            if (op->m_func) {
                delete op;
            }

            continue; // 继续主循环，处理更多任务
        }

        // 自旋结束后仍未找到任务，准备进入睡眠状态
        // 通知其他线程我们即将进入睡眠
        notify_intent_to_sleep(thread_id);

        // 重新检查是否有新任务到来，以避免进入睡眠时有任务到来却无法唤醒自己
        if (approx_has_queued_work(thread_id)) {
            remote_op = try_get_remote();
            if (remote_op) {
                // 清除睡眠意图，防止其他线程误唤醒
                try_clear_intent_to_sleep(thread_id);

                // 找到任务，恢复协程执行
                op = remote_op;
                assert(op != nullptr);

                op->execute();
                if (op->m_func) {
                    delete op;
                }

                continue; // 继续主循环，处理更多任务
            }
        }

        if (m_stop.load(std::memory_order_relaxed)) {
            return; // 接收到关闭信号，退出线程
        }

        // 进入睡眠，等待被唤醒
        state.sleep_until_woken();
    }
}
//
// void thread_pool::run_daemon_thread() noexcept {
//     while (!m_stop.load(std::memory_order_relaxed)) {
//         // 等待一段时间，避免频繁检查
//         std::this_thread::sleep_for(std::chrono::milliseconds(10));
//
//         // 检查是否有未处理的任务
//         auto work_count = approx_total_work();
//         auto sleep_threads_count = m_sleep_thread_count.load(std::memory_order_acquire);
//
//         if (work_count > 0 && sleep_threads_count > 0) {
//             // 需要唤醒线程来处理任务
//             auto num_threads = std::min(work_count, sleep_threads_count);
//             wake_threads(num_threads);
//         }
//     }
// }

auto thread_pool::shutdown() -> void {
    m_stop.store(true, std::memory_order_relaxed);
    wake_threads(m_thread_count);

    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    // if (m_daemon_thread.joinable()) {
    //     m_daemon_thread.join();
    // }
}

auto thread_pool::schedule_impl(schedule_operation* op) noexcept -> void {
    if (s_cur_state && s_cur_thread_pool == this) {
        if (s_cur_state->try_local_enqueue(op)) {
            return;
        }
    }
    try_global_enqueue(op);

    auto work_count = approx_total_work();
    auto sleep_threads_count = m_sleep_thread_count.load(std::memory_order_relaxed);
    auto num_threads = std::min(work_count, sleep_threads_count);
    wake_threads(num_threads);
}

auto thread_pool::try_global_enqueue(schedule_operation* op) noexcept -> bool {
    // Atomically increment the head and get the previous value
    auto head = m_global_head.fetch_add(1, std::memory_order_acq_rel);

    while (true) {
        auto tail = m_global_tail.load(std::memory_order_acquire);
        // Check if the queue is full
        if (diff(head, tail) >= static_cast<std::make_signed_t<std::size_t>>(m_global_mask)) {
            // Queue is full, cannot enqueue
            // Roll back the head increment
            m_global_head.fetch_sub(1, std::memory_order_acq_rel);
            return false;
        }

        // Use compare_exchange to ensure only one thread writes to this position
        schedule_operation* expected = nullptr;
        if (m_global_queue[head & m_global_mask].compare_exchange_strong(
            expected,
            op,
            std::memory_order_release,
            std::memory_order_relaxed)) {
            // Successfully enqueued
            return true;
        }
        // Another thread has written to this position, need to try again
        // Roll back the head increment
        m_global_head.fetch_sub(1, std::memory_order_acq_rel);
        // Yield and try again
        std::this_thread::yield();
        head = m_global_head.fetch_add(1, std::memory_order_acq_rel);
    }
}

auto thread_pool::try_global_dequeue() noexcept -> schedule_operation* {
    auto tail = m_global_tail.fetch_add(1, std::memory_order_acq_rel);

    spin_wait spinner;

    while (true) {
        auto head = m_global_head.load(std::memory_order_acquire);
        // 检查队列是否为空
        if (diff(head, tail) <= 0) {
            // 队列为空，回滚 tail
            m_global_tail.fetch_sub(1, std::memory_order_acq_rel);
            return nullptr;
        }

        // 获取队列中的任务
        auto index = tail & m_global_mask;
        schedule_operation* op = m_global_queue[index].load(std::memory_order_acquire);

        if (op) {
            // 清理槽位
            m_global_queue[index].store(nullptr, std::memory_order_release);
            return op;
        }
        // 如果任务还没有写入完成，使用 spin_wait 进行等待
        spinner.spin_one(); // 调用 spin_wait 的 spin_one() 逐步退让
        if (spinner.next_spin_will_yield()) {
            // 如果自旋已经进行了足够长的时间，进入短暂休眠
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        // 如果仍未获取到任务，放弃本次尝试，回滚 tail
        if (spinner.next_spin_will_yield()) {
            m_global_tail.fetch_sub(1, std::memory_order_acq_rel);
            return nullptr;
        }
    }
}

auto thread_pool::has_queued_work(std::uint32_t thread_id) noexcept -> bool {
    std::scoped_lock lock(m_global_queue_mutex);
    const auto head = m_global_head.load(std::memory_order_seq_cst);
    const auto tail = m_global_tail.load(std::memory_order_seq_cst);
    if (diff(head, tail) > 0) {
        return true;
    }

    for (auto i = 0u; i < m_thread_count; ++i) {
        if (i == thread_id) {
            continue;
        }
        if (m_thread_states[i].has_queued_work()) {
            return true;
        }
    }
    return false;
}

auto thread_pool::approx_has_queued_work(std::uint32_t thread_id) const noexcept -> bool {
    const auto head = m_global_head.load(std::memory_order_relaxed);
    const auto tail = m_global_tail.load(std::memory_order_relaxed);
    if (diff(head, tail) > 0) {
        return true;
    }

    for (auto i = 0u; i < m_thread_count; ++i) {
        if (i == thread_id) {
            continue;
        }
        if (m_thread_states[i].approx_has_queued_work()) {
            return true;
        }
    }
    return false;
}

auto thread_pool::approx_total_work() const noexcept -> std::uint32_t {
    const auto head = m_global_head.load(std::memory_order_relaxed);
    const auto tail = m_global_tail.load(std::memory_order_relaxed);
    const auto queued_work_count = diff(head, tail);
    return queued_work_count > 0 ? static_cast<std::uint32_t>(queued_work_count) : 0;
}

auto thread_pool::notify_intent_to_sleep(std::uint32_t thread_id) noexcept -> void {
    m_thread_states[thread_id].notify_intent_to_sleep();
    // Then publish the fact that a thread is asleep by incrementing the count
    // of threads that are asleep.
    m_sleep_thread_count.fetch_add(1, std::memory_order_seq_cst);
}

auto thread_pool::try_clear_intent_to_sleep(std::uint32_t thread_id) noexcept -> void {
    std::uint32_t old_sleeping_count = m_sleep_thread_count.load(std::memory_order_relaxed);
    do {
        if (old_sleeping_count == 0) {
            // No more sleeping threads.
            // Someone must have woken us up.
            return;
        }
    } while (!m_sleep_thread_count.compare_exchange_weak(
        old_sleeping_count,
        old_sleeping_count - 1,
        std::memory_order_acquire,
        std::memory_order_relaxed));

    // Then preferentially try to wake up our thread.
    // If some other thread has already requested that this thread wake up
    // then we will wake up another thread - the one that should have been woken
    // up by the thread that woke this thread up.
    if (!m_thread_states[thread_id].try_wakeup()) {
        for (auto i = 0u; i < m_thread_count; ++i) {
            if (i == thread_id) {
                continue;
            }
            if (m_thread_states[i].try_wakeup()) {
                return;
            }
        }
    }
}

auto thread_pool::try_steal(std::uint32_t cur_thread_id) noexcept -> schedule_operation* {
    for (auto i = 0u; i < m_thread_count; ++i) {
        if (i == cur_thread_id) {
            continue;
        }
        auto& other_thread_state = m_thread_states[i];
        auto* op = other_thread_state.try_steal();
        if (op != nullptr) {
            return op;
        }
    }
    return nullptr;
}

auto thread_pool::wake_threads(std::uint32_t num_threads) noexcept -> void {
    // 尽可能地唤醒指定数量的线程
    while (num_threads > 0 && m_sleep_thread_count.load(std::memory_order_acquire) > 0) {
        std::uint32_t old_sleep_count = m_sleep_thread_count.load(std::memory_order_acquire);
        if (old_sleep_count == 0) {
            // 没有更多的线程可以唤醒
            break;
        }
        // 尝试减少睡眠线程计数
        if (m_sleep_thread_count.compare_exchange_weak(
            old_sleep_count,
            old_sleep_count - 1,
            std::memory_order_release,
            std::memory_order_relaxed)) {
            // 成功减少睡眠线程计数，唤醒一个线程
            for (std::uint32_t i = 0; i < m_thread_count; ++i) {
                if (m_thread_states[i].try_wakeup()) {
                    break;
                }
            }
            --num_threads;
        }
    }
}

}
