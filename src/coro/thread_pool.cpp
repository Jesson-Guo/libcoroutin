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
		, m_mask(local_queue_size - 1)
		, m_head(0)
		, m_tail(0)
		, m_is_sleep(false) {}

	bool try_wakeup() {
		if (m_is_sleep.load(std::memory_order_seq_cst)) {
			if (m_is_sleep.exchange(false, std::memory_order_seq_cst)) {
				try {
					m_wakeup_event.set();
				}
				catch (...) {
					// TODO: What to do
				}
				return true;
			}
		}

		return false;
	}

	void notify_intent_to_sleep() noexcept {
		m_is_sleep.store(true, std::memory_order_relaxed);
	}

	void sleep_until_woken() noexcept {
		try {
			m_wakeup_event.wait();
		}
		catch (...) {
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(1ms);
		}
	}

	bool approx_has_queued_work() const noexcept {
		return difference(
			m_head.load(std::memory_order_relaxed),
			m_tail.load(std::memory_order_relaxed)) > 0;
	}

	bool has_queued_work() noexcept {
		std::scoped_lock lock{ m_remote_mutex };
		auto tail = m_tail.load(std::memory_order_relaxed);
		auto head = m_head.load(std::memory_order_seq_cst);
		return difference(head, tail) > 0;
	}

	bool try_local_enqueue(schedule_operation*& op) noexcept {
		// Head is only ever written-to by the current thread so we
		// are safe to use relaxed memory order when reading it.
		auto head = m_head.load(std::memory_order_relaxed);
		auto tail = m_tail.load(std::memory_order_relaxed);
		if (difference(head, tail) < static_cast<offset_t>(m_mask)) {
			// There is space left in the local buffer.
			m_local_queue[head & m_mask].store(op, std::memory_order_relaxed);
			m_head.store(head + 1, std::memory_order_seq_cst);
			return true;
		}

		if (m_mask == local_queue_size) {
			// No space in the buffer and we don't want to grow
			// it any further.
			return false;
		}

		// Allocate the new buffer before taking out the lock so that
		// we ensure we hold the lock for as short a time as possible.
		const size_t new_size = (m_mask + 1) * 2;

		std::unique_ptr<std::atomic<schedule_operation*>[]> new_local_queue{
			new (std::nothrow) std::atomic<schedule_operation*>[new_size]
		};
		if (!new_local_queue) {
			// Unable to allocate more memory.
			return false;
		}

		if (!m_remote_mutex.try_lock()) {
			// Don't wait to acquire the lock if we can't get it immediately.
			// Fail and let it be enqueued to the global queue.
			return false;
		}

		std::scoped_lock lock{ std::adopt_lock, m_remote_mutex };

		// We can now re-read tail, guaranteed that we are not seeing a stale version.
		tail = m_tail.load(std::memory_order_relaxed);

		// Copy the existing operations.
		const size_t new_mask = new_size - 1;
		for (size_t i = tail; i != head; ++i) {
			new_local_queue[i & new_mask].store(
				m_local_queue[i & m_mask].load(std::memory_order_relaxed),
				std::memory_order_relaxed);
		}

		// Finally, write the new operation to the queue.
		new_local_queue[head & new_mask].store(op, std::memory_order_relaxed);

		m_head.store(head + 1, std::memory_order_relaxed);
		m_local_queue = std::move(new_local_queue);
		m_mask = new_mask;
		return true;
	}

	schedule_operation* try_local_pop() noexcept {
		// Cheap, approximate, no memory-barrier check for emptiness
		auto head = m_head.load(std::memory_order_relaxed);
		auto tail = m_tail.load(std::memory_order_relaxed);
		if (difference(head, tail) <= 0) {
			// Empty
			return nullptr;
		}

		auto new_head = head - 1;
		m_head.store(new_head, std::memory_order_seq_cst);

		tail = m_tail.load(std::memory_order_seq_cst);

		if (difference(new_head, tail) < 0) {
			std::lock_guard lock{ m_remote_mutex };

			// Use relaxed since the lock guarantees visibility of the writes
			// that the remote steal thread performed.
			tail = m_tail.load(std::memory_order_relaxed);

			if (difference(new_head, tail) < 0) {
				// The other thread didn't see our write and stole the last item.
				// We need to restore the head back to it's old value.
				// We hold the mutex so can just use relaxed memory order for this.
				m_head.store(head, std::memory_order_relaxed);
				return nullptr;
			}
		}

		// We successfully acquired an item from the queue.
		return m_local_queue[new_head & m_mask].load(std::memory_order_relaxed);
	}

	schedule_operation* try_steal(bool* lock_unavailable = nullptr) noexcept {
		if (lock_unavailable == nullptr) {
			m_remote_mutex.lock();
		}
		else if (!m_remote_mutex.try_lock()) {
			*lock_unavailable = true;
			return nullptr;
		}

		std::scoped_lock lock{ std::adopt_lock, m_remote_mutex };

		auto tail = m_tail.load(std::memory_order_relaxed);
		auto head = m_head.load(std::memory_order_seq_cst);
		if (difference(head, tail) <= 0) {
			return nullptr;
		}

		m_tail.store(tail + 1, std::memory_order_seq_cst);
		head = m_head.load(std::memory_order_seq_cst);

		if (difference(head, tail) > 0) {
			return m_local_queue[tail & m_mask].load(std::memory_order_relaxed);
		}
		m_tail.store(tail, std::memory_order_seq_cst);
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

	static constexpr offset_t difference(size_t a, size_t b) {
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

void thread_pool::schedule_operation::await_suspend(std::coroutine_handle<> handle) noexcept {
	m_awaiting_handle = handle;
	m_thread_pool->schedule_impl(this);
}

thread_pool::thread_pool() : thread_pool(std::thread::hardware_concurrency()) {}

thread_pool::thread_pool(std::uint32_t thread_count)
	: m_thread_count(thread_count > 0 ? thread_count : 1)
	, m_thread_states(std::make_unique<thread_state[]>(m_thread_count))
    , m_stop(false)
    , m_global_queue(global_queue_size)
	, m_sleep_thread_count(0) {
	m_threads.reserve(thread_count);
	try {
		for (std::uint32_t i = 0; i < m_thread_count; ++i) {
			m_threads.emplace_back([this, i] { this->run_worker_thread(i); });
		}
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

void thread_pool::run_worker_thread(std::uint32_t thread_id) noexcept {
	auto& local_state = m_thread_states[thread_id];
	s_cur_state = &local_state;
	s_cur_thread_pool = this;

	auto try_get_remote = [&]() {
		auto* op = try_global_dequeue();
		if (op == nullptr) {
			op = try_steal(thread_id);
		}
		return op;
	};

	while (true) {
		schedule_operation* op;

		while (true) {
			op = local_state.try_local_pop();
			if (op == nullptr) {
				op = try_get_remote();
				if (op == nullptr) {
					break;
				}
			}

			op->m_awaiting_handle.resume();
		}

		spin_wait spinner;
		while (true) {
			for (int i = 0; i < 30; ++i) {
				if (is_shutdown()) {
					return;
				}

				spinner.spin_one();

				if (approx_has_queued_work(thread_id)) {
					op = try_get_remote();
					if (op != nullptr) {
						goto normal_processing;
					}
				}
			}

			notify_intent_to_sleep(thread_id);

			if (has_queued_work(thread_id)) {
				op = try_get_remote();
				if (op != nullptr) {
					try_clear_intent_to_sleep(thread_id);
					goto normal_processing;
				}
			}

			if (is_shutdown()) {
				return;
			}

			local_state.sleep_until_woken();
		}

	normal_processing:
		assert(op != nullptr);
		op->m_awaiting_handle.resume();
	}
}

auto thread_pool::shutdown() -> void {
	m_stop.store(true, std::memory_order_relaxed);

	for (std::uint32_t i = 0; i < m_threads.size(); ++i) {
		auto& thread_state = m_thread_states[i];
		assert(!thread_state.has_queued_work());
		thread_state.try_wakeup();
	}

	for (auto& t : m_threads) {
		t.join();
	}
}

auto thread_pool::schedule_impl(schedule_operation* op) noexcept -> void {
	if (s_cur_thread_pool != this || !s_cur_state->try_local_enqueue(op)) {
        // try_global_enqueue(op);
        while (!try_global_enqueue(op)) {
            // 如果队列满了，等待一段时间后重试
            std::this_thread::yield();
	    }
	}

    const auto num_wakeup_threads = std::min(approx_total_work(), m_sleep_thread_count.load(std::memory_order_relaxed));
    if (num_wakeup_threads > 0) {
        wake_threads(num_wakeup_threads);
    }
}

auto thread_pool::try_global_enqueue(schedule_operation* op) noexcept -> bool {
    if (m_global_queue.push(op)) {
        return true;
    }
    return false;

    // while (true) {
    //     auto head = m_global_head.load(std::memory_order_acquire);
    //     auto tail = m_global_tail.load(std::memory_order_acquire);
    //     // 检查队列是否已满
    //     if (diff(head, tail) >= static_cast<std::make_signed_t<std::size_t>>(m_global_mask)) {
    //         // 队列已满，无法入队
    //         return false;
    //     }
    //
    //     // 尝试在队列的当前位置插入任务
    //     schedule_operation* expected = nullptr;
    //     if (m_global_queue[head & m_global_mask].compare_exchange_strong(
    //         expected,
    //         op,
    //         std::memory_order_release,
    //         std::memory_order_relaxed)) {
    //         // 插入成功，尝试更新 m_global_head
    //         if (m_global_head.compare_exchange_strong(
    //             head,
    //             head + 1,
    //             std::memory_order_acq_rel,
    //             std::memory_order_relaxed)) {
    //             // 成功更新 m_global_head，入队完成
    //             return true;
    //         }
    //         // 更新 m_global_head 失败，需要撤销插入的任务
    //         m_global_queue[head & m_global_mask].store(nullptr, std::memory_order_relaxed);
    //         continue; // 重试入队操作
    //     }
    //     // 当前位置已被其他线程占用，重试
    //     std::this_thread::yield();
    // }
}

auto thread_pool::try_global_dequeue() noexcept -> schedule_operation* {
    if (schedule_operation* op = nullptr; m_global_queue.pop(op)) {
        return op;
    }
    return nullptr;

    // spin_wait spinner;
    //
    // while (true) {
    //     auto head = m_global_head.load(std::memory_order_acquire);
    //     auto tail = m_global_tail.load(std::memory_order_acquire);
    //     // 检查队列是否为空
    //     if (diff(head, tail) <= 0) {
    //         // 队列为空，回滚 tail
    //         return nullptr;
    //     }
    //
    //     // 计算取出的位置索引
    //     auto index = tail & m_global_mask;
    //
    //     // 尝试获取任务
    //     schedule_operation* op = m_global_queue[index].load(std::memory_order_acquire);
    //     if (op) {
    //         // 尝试将当前位置的任务指针置为 nullptr
    //         if (m_global_queue[index].compare_exchange_strong(
    //             op,
    //             nullptr,
    //             std::memory_order_acq_rel,
    //             std::memory_order_relaxed)) {
    //             // 成功获取任务，尝试更新 m_global_tail
    //             if (m_global_tail.compare_exchange_strong(
    //                 tail,
    //                 tail + 1,
    //                 std::memory_order_acq_rel,
    //                 std::memory_order_relaxed)) {
    //                 // 成功更新 m_global_tail，出队完成
    //                 return op;
    //             }
    //             // 更新 m_global_tail 失败，需要将任务指针恢复
    //             m_global_queue[index].store(op, std::memory_order_relaxed);
    //             continue; // 重试出队操作
    //         }
    //         // 当前位置的任务被其他线程修改，重试
    //         std::this_thread::yield();
    //         continue;
    //     }
    //     // 如果任务还没有写入完成，使用 spin_wait 进行等待
    //     spinner.spin_one(); // 调用 spin_wait 的 spin_one() 逐步退让
    //     if (spinner.next_spin_will_yield()) {
    //         // 如果自旋已经进行了足够长的时间，进入短暂休眠
    //         std::this_thread::yield();
    //     }
    // }
}

auto thread_pool::has_queued_work(std::uint32_t thread_id) noexcept -> bool {
    if (schedule_operation* op = nullptr; this->m_global_queue.pop(op)) {
        this->m_global_queue.push(op);
        return true;
    }

	for (std::uint32_t i = 0; i < m_thread_count; ++i) {
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
    if (!m_global_queue.empty()) {
        return true;
    }

    for (std::uint32_t i = 0; i < m_thread_count; ++i) {
        if (i == thread_id) {
            continue;
        }
        if (m_thread_states[i].has_queued_work()) {
            return true;
        }
    }

    return false;
}

auto thread_pool::approx_total_work() const noexcept -> std::uint32_t {
    std::uint32_t num_queued_work = 0;
    if (!m_global_queue.empty()) {
        ++num_queued_work;
    }

	for (std::uint32_t i = 0; i < m_thread_count; ++i) {
		if (m_thread_states[i].approx_has_queued_work()) {
		    ++num_queued_work;
		}
	}

	return num_queued_work;
}

auto thread_pool::is_shutdown() const noexcept -> bool {
    return m_stop.load(std::memory_order_relaxed);
}

auto thread_pool::notify_intent_to_sleep(std::uint32_t thread_id) noexcept -> void {
	// First mark the thread as asleep
	m_thread_states[thread_id].notify_intent_to_sleep();

	// Then publish the fact that a thread is asleep by incrementing the count
	// of threads that are asleep.
	m_sleep_thread_count.fetch_add(1, std::memory_order_seq_cst);
}

auto thread_pool::try_clear_intent_to_sleep(std::uint32_t thread_id) noexcept -> void {
	// First try to claim that we are waking up one of the threads.
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
		for (std::uint32_t i = 0; i < m_thread_count; ++i) {
			if (i == thread_id) {
			    continue;
			}
			if (m_thread_states[i].try_wakeup()) {
				return;
			}
		}
	}
}

auto thread_pool::try_steal(std::uint32_t cur_thread_id) const noexcept -> schedule_operation* {
	// Try first with non-blocking steal attempts.

	bool any_locks_unavailable = false;
	for (std::uint32_t i = 0; i < m_thread_count; ++i) {
		if (i == cur_thread_id) {
		    continue;
		}
		auto* op = m_thread_states[i].try_steal(&any_locks_unavailable);
		if (op != nullptr) {
			return op;
		}
	}

	if (any_locks_unavailable) {
		// We didn't check all of the other threads for work to steal yet.
		// Try again, this time waiting to acquire the locks.
		for (std::uint32_t i = 0; i < m_thread_count; ++i) {
			if (i == cur_thread_id) {
			    continue;
			}
			auto* op = m_thread_states[i].try_steal();
			if (op != nullptr) {
				return op;
			}
		}
	}

	return nullptr;
}

auto thread_pool::wake_threads(std::uint32_t num_threads) noexcept -> void {
    assert(num_threads > 0);

    std::uint32_t num_to_wake = num_threads;
    std::uint32_t old_sleep_count = m_sleep_thread_count.load(std::memory_order_seq_cst);

    do {
        if (old_sleep_count == 0) {
            // No sleeping threads to wake up
            return;
        }
        // Adjust the number of threads to wake if fewer are sleeping
        if (old_sleep_count < num_to_wake) {
            num_to_wake = old_sleep_count;
        }
    } while (!m_sleep_thread_count.compare_exchange_weak(
        old_sleep_count,
        old_sleep_count - num_to_wake,
        std::memory_order_acquire,
        std::memory_order_relaxed));

    // Wake up the calculated number of threads
    std::uint32_t woken = 0;
    while (true) {
        for (std::uint32_t i = 0; i < m_thread_count; ++i) {
            if (m_thread_states[i].try_wakeup()) {
                ++woken;
                if (woken >= num_to_wake) {
                    return;
                }
            }
        }
        if (is_shutdown() || m_sleep_thread_count.load(std::memory_order_relaxed) == 0) {
            return;
        }
    }
}

}
