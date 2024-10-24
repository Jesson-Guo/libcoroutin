//
// Created by Jesson on 2024/10/2.
//

#ifndef thread_pool_H
#define thread_pool_H

#include "auto_reset_event.h"
#include "spin_mutex.h"
#include "spin_wait.h"
// #include "detail/lock_free_queue.h"

#include <boost/lockfree/queue.hpp>

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
		explicit schedule_operation(thread_pool* tp) noexcept : m_thread_pool(tp) {}
		bool await_ready() noexcept { return false; }
		void await_suspend(std::coroutine_handle<> handle) noexcept;
		void await_resume() noexcept {}

	private:
		friend class thread_pool;
		thread_pool* m_thread_pool;
		std::coroutine_handle<> m_awaiting_handle;
	};

	std::uint32_t thread_count() const noexcept { return m_thread_count; }

	[[nodiscard]]
	schedule_operation schedule() noexcept { return schedule_operation{ this }; }

private:
	friend class schedule_operation;

    /**
     * 每个线程的主要工作函数，循环处理局部队列中的任务，或者从全局队列和其他线程窃取任务。
     *   -
     * 首先尝试从局部队列中获取任务，如果没有任务，则尝试从全局队列获取，最后尝试从其他线程窃取任务。
     *   - 如果找不到任务，线程会在短时间内自旋（等待任务），若仍没有任务，则进入睡眠等待任务的到来。
     *   - 唤醒逻辑则通过 ` notify_intent_to_sleep` 和 ` sleep_until_woken` 来实现。
     */
	void run_worker_thread(std::uint32_t thread_id) noexcept;

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
    auto approx_total_work() const noexcept -> std::uint32_t;

    auto is_shutdown() const noexcept -> bool;

    auto notify_intent_to_sleep(std::uint32_t thread_id) noexcept -> void;
    auto try_clear_intent_to_sleep(std::uint32_t thread_id) noexcept -> void;

    /**
     * 尝试从其他线程的局部队列中窃取任务，任务窃取优先从任务数量多的线程中窃取。
     */
	auto try_steal(std::uint32_t cur_thread_id) const noexcept -> schedule_operation*;

    /**
     * 根据当前任务数量和睡眠线程的数量，批量地唤醒线程。
     */
	auto wake_threads(std::uint32_t num_threads = 1) noexcept -> void;

	static thread_local thread_state* s_cur_state;
	static thread_local thread_pool* s_cur_thread_pool;

    static constexpr std::size_t global_queue_size = 1024 * 1024 / sizeof(void*);

	const std::uint32_t m_thread_count;
	const std::unique_ptr<thread_state[]> m_thread_states;

	std::vector<std::thread> m_threads;

	std::atomic<bool> m_stop;

	std::mutex m_global_queue_mutex;
    // std::unique_ptr<std::atomic<schedule_operation*>[]> m_global_queue;
    // std::size_t m_global_mask;
    // std::atomic<std::size_t> m_global_head;
    // std::atomic<std::size_t> m_global_tail;
    boost::lockfree::queue<schedule_operation*> m_global_queue;

	std::atomic<std::uint32_t> m_sleep_thread_count;
};

}

#endif //thread_pool_H
