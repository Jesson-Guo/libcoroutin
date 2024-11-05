//
// Created by Jesson on 2024/10/23.
//

#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>

template <typename T>
struct lock_free_node {
    std::atomic<lock_free_node*> next{ nullptr };
    T* data{ nullptr };

    lock_free_node() = default;
    explicit lock_free_node(T* data) : data(data) {}
};

template <typename T>
class lock_free_queue {
public:
    lock_free_queue() {
        auto dummy = new lock_free_node<T>();
        head.store(dummy, std::memory_order_relaxed);
        tail.store(dummy, std::memory_order_relaxed);
    }

    ~lock_free_queue() {
        lock_free_node<T>* node = head.load(std::memory_order_relaxed);
        while (node) {
            lock_free_node<T>* next = node->next.load(std::memory_order_relaxed);
            delete node;
            node = next;
        }
    }

    std::size_t approx_size() const noexcept {
        return size_counter.load(std::memory_order_relaxed);
    }

    bool peek() const noexcept {
        lock_free_node<T>* old_head = head.load(std::memory_order_acquire);
        lock_free_node<T>* next = old_head->next.load(std::memory_order_acquire);
        return next != nullptr;
    }

    void enqueue(T* data) {
        auto new_node = new lock_free_node<T>(data);
        lock_free_node<T>* old_tail = nullptr;

        while (true) {
            old_tail = tail.load(std::memory_order_acquire);
            lock_free_node<T>* next = old_tail->next.load(std::memory_order_acquire);

            if (old_tail == tail.load(std::memory_order_acquire)) {
                if (next == nullptr) {
                    if (old_tail->next.compare_exchange_weak(
                        next,
                        new_node,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        break;
                    }
                }
                else {
                    tail.compare_exchange_weak(
                        old_tail,
                        next,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                }
            }
        }
        tail.compare_exchange_strong(
            old_tail,
            new_node,
            std::memory_order_release,
            std::memory_order_relaxed);

        size_counter.fetch_add(1, std::memory_order_relaxed);
    }

    T* dequeue() {
        while (true) {
            lock_free_node<T>* old_head = head.load(std::memory_order_acquire);
            lock_free_node<T>* old_tail = tail.load(std::memory_order_acquire);
            lock_free_node<T>* next = old_head->next.load(std::memory_order_acquire);

            if (old_head == head.load(std::memory_order_acquire)) {
                if (old_head == old_tail) {
                    if (next == nullptr) {
                        return nullptr; // 队列为空
                    }
                    tail.compare_exchange_weak(
                        old_tail,
                        next,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                }
                else {
                    T* data = next->data;
                    if (head.compare_exchange_weak(
                        old_head,
                        next,
                        std::memory_order_release,
                        std::memory_order_relaxed)) {
                        delete old_head; // 删除旧的哑结点
                        if (data) {
                            size_counter.fetch_sub(1, std::memory_order_relaxed);
                        }
                        return data;
                    }
                }
            }
        }
    }

private:
    std::atomic<lock_free_node<T>*> head;
    std::atomic<lock_free_node<T>*> tail;
    std::atomic<std::size_t> size_counter;
};

#endif //LOCK_FREE_QUEUE_H
