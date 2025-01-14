//
// Created by Jesson on 2024/10/5.
//

#ifndef MACOS_H
#define MACOS_H

#include "../thread_pool.h"
#include "../types/task.h"

#include "boost/lockfree/queue.hpp"

#include <coroutine>
#include <sys/event.h>   // 用于kqueue
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <iostream>

namespace coro::detail::macos {

using fd_t = int;

enum class io_operation_type {
    NONE,
    READ,
    WRITE,
    PREAD,
    PWRITE,
    RECV,
    SEND,
    RECVMSG,
    SENDMSG,
    CONNECT,
    ACCEPT,
    CLOSE,
    TIMEOUT,
    TIMEOUT_REMOVE,
    NOP,
    CANCEL
};

struct io_message {
    std::coroutine_handle<> handle;
    ssize_t result = 0;
    int fd = -1;
    void* buffer = nullptr;
    size_t size = 0;
    off_t offset = 0;
    msghdr* msg = nullptr;
    int flags = 0;
    timespec timeout{};
    std::atomic<bool> canceled = false;
    std::atomic<bool> completed = false;
    io_operation_type operation = io_operation_type::NONE;
    std::error_code ec;
};

class safe_fd {
public:
    safe_fd() : m_fd(-1) {}
    explicit safe_fd(const fd_t fd) : m_fd(fd) {}
    safe_fd(const safe_fd& other) = delete;
    safe_fd(safe_fd&& other) noexcept : m_fd(other.m_fd) { other.m_fd = -1; }
    ~safe_fd() { close(); }

    safe_fd& operator=(safe_fd fd) noexcept {
        swap(fd);
        return *this;
    }

    constexpr fd_t fd() const { return m_fd; }

    /// Calls close() and sets the fd to -1.
    void close() noexcept {
        if (m_fd != -1) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

    void swap(safe_fd& other) noexcept { std::swap(m_fd, other.m_fd); }
    bool operator==(const safe_fd& other) const { return m_fd == other.m_fd; }
    bool operator!=(const safe_fd& other) const { return m_fd != other.m_fd; }
    bool operator==(const fd_t fd) const { return m_fd == fd; }
    bool operator!=(const fd_t fd) const { return m_fd != fd; }

private:
    fd_t m_fd;
};

class message_queue;

class io_transaction final {
public:
    io_transaction(message_queue& queue, io_message* message) noexcept
        : m_queue(queue)
        , m_message{message} {}

    bool commit() const noexcept;

    [[nodiscard]] io_transaction& read(int fd, void* buffer, size_t size) noexcept;
    [[nodiscard]] io_transaction& write(int fd, const void* buffer, size_t size) noexcept;

    [[nodiscard]] io_transaction& pread(int fd, void* buffer, size_t size, off_t offset) noexcept;
    [[nodiscard]] io_transaction& pwrite(int fd, const void* buffer, size_t size, off_t offset) noexcept;

    [[nodiscard]] io_transaction& recv(int fd, void* buffer, size_t size = 0, int flags = 0) noexcept;
    [[nodiscard]] io_transaction& send(int fd, const void* buffer, size_t size = 0, int flags = 0) noexcept;

    [[nodiscard]] io_transaction& recvmsg(int fd, msghdr* msg = nullptr, int flags = 0) noexcept;
    [[nodiscard]] io_transaction& sendmsg(int fd, msghdr* msg = nullptr, int flags = 0) noexcept;

    [[nodiscard]] io_transaction& connect(int fd, sockaddr* addr, socklen_t addrlen) noexcept;
    [[nodiscard]] io_transaction& accept(int fd, sockaddr* addr, socklen_t addrlen) noexcept;
    [[nodiscard]] io_transaction& close(int fd) noexcept;
    [[nodiscard]] io_transaction& timeout(const timespec& ts, bool absolute = false) noexcept;
    [[nodiscard]] io_transaction& timeout_remove() noexcept;
    [[nodiscard]] io_transaction& nop() noexcept;
    [[nodiscard]] io_transaction& cancel() noexcept;

private:
    message_queue& m_queue;
    io_message* m_message;
};

class message_queue {
public:
    explicit message_queue(size_t queue_length = 32);
    ~message_queue() noexcept;

    message_queue(message_queue&&) = delete;
    message_queue& operator=(message_queue&&) = delete;
    message_queue(message_queue const&) = delete;
    message_queue& operator=(message_queue const&) = delete;

    int get_kqueue_fd() const;
    void stop();

    io_transaction transaction(io_message* message) noexcept;
    bool try_submit(io_message* message) noexcept;
    bool dequeue(io_message*& message, bool wait);

private:
    int io_wait_cq(io_message*& msg);
    int io_peek_cq(io_message*& msg);

    void enqueue(io_message* msg);

    int m_kqueue_fd;
    std::mutex m_kq_mutex;

    boost::lockfree::queue<io_message*> m_submission_queue;
    boost::lockfree::queue<io_message*> m_completion_queue;

    std::mutex m_cqe_mutex;
    std::condition_variable m_cqe_cv;
    std::atomic<bool> m_stop;
    std::vector<std::thread> m_worker_threads;

    void worker_thread_func();
    void event_set(io_message* msg);
    void process_one(struct kevent& event);
};
using io_queue = message_queue;

// io_transaction
inline bool io_transaction::commit() const noexcept {
    return m_queue.try_submit(m_message);
}

inline io_transaction& io_transaction::read(int fd, void* buffer, size_t size) noexcept {
    m_message->fd = fd;
    m_message->buffer = buffer;
    m_message->size = size;
    m_message->operation = io_operation_type::READ;
    return *this;
}

inline io_transaction& io_transaction::write(int fd, const void* buffer, size_t size) noexcept {
    m_message->fd = fd;
    m_message->buffer = const_cast<void*>(buffer);
    m_message->size = size;
    m_message->operation = io_operation_type::WRITE;
    return *this;
}

inline io_transaction& io_transaction::pread(int fd, void* buffer, size_t size, off_t offset) noexcept {
    m_message->fd = fd;
    m_message->buffer = buffer;
    m_message->size = size;
    m_message->offset = offset;
    m_message->operation = io_operation_type::PREAD;
    return *this;
}

inline io_transaction& io_transaction::pwrite(int fd, const void* buffer, size_t size, off_t offset) noexcept {
    m_message->fd = fd;
    m_message->buffer = const_cast<void*>(buffer);
    m_message->size = size;
    m_message->offset = offset;
    m_message->operation = io_operation_type::PWRITE;
    return *this;
}

inline io_transaction& io_transaction::recv(int fd, void* buffer, size_t size, int flags) noexcept {
    m_message->fd = fd;
    m_message->buffer = buffer;
    m_message->size = size;
    m_message->flags = flags;
    m_message->operation = io_operation_type::RECV;
    return *this;
}

inline io_transaction& io_transaction::send(int fd, const void* buffer, size_t size, int flags) noexcept {
    m_message->fd = fd;
    m_message->buffer = const_cast<void*>(buffer);
    m_message->size = size;
    m_message->flags = flags;
    m_message->operation = io_operation_type::SEND;
    return *this;
}

inline io_transaction& io_transaction::recvmsg(int fd, msghdr* msg, int flags) noexcept {
    m_message->fd = fd;
    m_message->msg = msg;
    m_message->flags = flags;
    m_message->operation = io_operation_type::RECVMSG;
    return *this;
}

inline io_transaction& io_transaction::sendmsg(int fd, msghdr* msg, int flags) noexcept {
    m_message->fd = fd;
    m_message->msg = msg;
    m_message->flags = flags;
    m_message->operation = io_operation_type::SENDMSG;
    return *this;
}

inline io_transaction& io_transaction::connect(int fd, sockaddr* addr, socklen_t addrlen) noexcept {
    m_message->fd = fd;
    m_message->buffer = addr;
    m_message->size = addrlen;
    m_message->operation = io_operation_type::CONNECT;
    return *this;
}

inline io_transaction& io_transaction::accept(int fd, sockaddr* addr, socklen_t addrlen) noexcept {
    m_message->fd = fd;
    m_message->buffer = addr;
    m_message->size = addrlen;
    m_message->operation = io_operation_type::ACCEPT;
    return *this;
}

inline io_transaction& io_transaction::close(int fd) noexcept {
    m_message->fd = fd;
    m_message->operation = io_operation_type::CLOSE;
    return *this;
}

inline io_transaction& io_transaction::timeout(const timespec& ts, bool absolute) noexcept {
    m_message->timeout = ts;
    m_message->operation = io_operation_type::TIMEOUT;
    m_message->flags = absolute ? NOTE_ABSOLUTE : 0;
    return *this;
}

inline io_transaction& io_transaction::timeout_remove() noexcept {
    if (m_message->operation == io_operation_type::TIMEOUT) {
        m_message->operation = io_operation_type::TIMEOUT_REMOVE;
        m_message->canceled.store(true);
    }
    return *this;
}

inline io_transaction& io_transaction::nop() noexcept {
    m_message->operation = io_operation_type::NOP;
    return *this;
}

inline io_transaction& io_transaction::cancel() noexcept {
    m_message->operation = io_operation_type::CANCEL;
    return *this;
}

// message_queue
inline message_queue::message_queue(size_t queue_length)
    : m_submission_queue(queue_length)
    , m_completion_queue(queue_length)
    , m_stop(false) {
    m_kqueue_fd = kqueue();
    if (m_kqueue_fd == -1) {
        throw std::system_error{errno, std::system_category(), "Error initializing kqueue"};
    }

    struct kevent ev{};
    EV_SET(&ev, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr) == -1) {
        throw std::system_error{errno, std::system_category(), "Error registering EVFILT_USER"};
    }

    // Start worker threads
    size_t num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
        // Default to 2 threads if undetermined
        num_threads = 2;
    }
    for (size_t i = 0; i < num_threads; ++i) {
        m_worker_threads.emplace_back(&message_queue::worker_thread_func, this);
    }
}

inline message_queue::~message_queue() noexcept {
    stop();

    // Send wake-up event to ensure worker threads unblock
    if (m_kqueue_fd != -1) {
        struct kevent ev{};
        EV_SET(&ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        kevent(m_kqueue_fd, &ev, 1, nullptr, 0, nullptr);
    }

    for (auto& thread : m_worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    if (m_kqueue_fd != -1) {
        close(m_kqueue_fd);
        m_kqueue_fd = -1;
    }
}

inline int message_queue::get_kqueue_fd() const {
    return m_kqueue_fd;
}

inline void message_queue::stop() {
    m_stop.store(true, std::memory_order_seq_cst);
    m_cqe_cv.notify_all();
}

inline io_transaction message_queue::transaction(io_message* message) noexcept {
    return io_transaction{*this, message};
}

inline bool message_queue::try_submit(io_message* message) noexcept {
    return m_submission_queue.push(message);
}

inline bool message_queue::dequeue(io_message*& message, bool wait) {
    int ret;
    if (wait) {
        ret = io_wait_cq(message);
    }
    else {
        ret = io_peek_cq(message);
    }

    if (ret == -EAGAIN || ret == -ECANCELED) {
        return false;
    }
    if (ret < 0) {
        throw std::system_error{-ret, std::system_category(), "Error in dequeue"};
    }
    return true;
}

inline int message_queue::io_wait_cq(io_message*& msg) {
    std::unique_lock lock(m_cqe_mutex);
    m_cqe_cv.wait(lock, [this]{ return !m_completion_queue.empty() || m_stop; });
    if (m_stop) {
        return -ECANCELED;
    }
    if (m_completion_queue.pop(msg)) {
        return 0;
    }
    return -EAGAIN;
}

inline int message_queue::io_peek_cq(io_message*& msg) {
    if (m_completion_queue.pop(msg)) {
        return 0;
    }
    return -EAGAIN;
}

inline void message_queue::enqueue(io_message* msg) {
    while (!m_completion_queue.push(msg)) {
        std::this_thread::yield();
    }
    {
        std::lock_guard lock(m_cqe_mutex);
        m_cqe_cv.notify_one();
    }
}

inline void message_queue::worker_thread_func() {
    while (!m_stop) {
        io_message* submit_msg = nullptr;
        while (m_submission_queue.pop(submit_msg)) {
            event_set(submit_msg);
        }

        // Wait for events
        struct kevent events[64];
        timespec timeout = {0, 1000000}; // Wait for 1 millisecond
        int nev;
        {
            std::lock_guard lock(m_kq_mutex);
            nev = kevent(m_kqueue_fd, nullptr, 0, events, 64, &timeout);
        }
        if (nev > 0) {
            for (int i = 0; i < nev; ++i) {
                struct kevent& event = events[i];
                if (event.filter == EVFILT_USER) {
                    // wakeup event
                    continue;
                }
                process_one(event);
            }
        }
        else if (nev == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EBADF) {
                // kqueue fd was closed, exit the loop
                break;
            }
            // Log the error and exit
            std::cerr << "kevent error: " << strerror(errno) << std::endl;
            break;
        }
    }

    // Process remaining submissions before exiting
    io_message* submit_msg = nullptr;
    while (m_submission_queue.pop(submit_msg)) {
        event_set(submit_msg);
    }

    // Process remaining events before exiting
    while (true) {
        struct kevent events[64];
        timespec timeout = {0, 0};  // Non-blocking timeout
        int nev;
        {
            std::lock_guard lock(m_kq_mutex);
            nev = kevent(m_kqueue_fd, nullptr, 0, events, 64, &timeout);
        }
        if (nev <= 0) {
            break;
        }
        for (int i = 0; i < nev; ++i) {
            struct kevent& event = events[i];
            process_one(event);
        }
    }
}

inline void message_queue::event_set(io_message* msg) {
    bool is_register = true;

    struct kevent kev{};
    switch (msg->operation) {
    case io_operation_type::READ:
    case io_operation_type::PREAD:
    case io_operation_type::RECV:
    case io_operation_type::RECVMSG:
    case io_operation_type::ACCEPT:
        EV_SET(&kev, msg->fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, msg);
        break;
    case io_operation_type::WRITE:
    case io_operation_type::PWRITE:
    case io_operation_type::SEND:
    case io_operation_type::SENDMSG:
        EV_SET(&kev, msg->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, msg);
        break;
    case io_operation_type::CONNECT: {
        // Attempt to initiate connect
        int ret = ::connect(msg->fd, static_cast<sockaddr*>(msg->buffer), static_cast<socklen_t>(msg->size));
        if (ret == 0) {
            // Connect succeeded immediately
            msg->result = 0;
            is_register = false;
            enqueue(msg);
        }
        else {
            if (errno == EINPROGRESS) {
                // Connection in progress, register for write event
                EV_SET(&kev, msg->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, msg);
            }
            else {
                // Connect failed
                int err = errno;
                msg->result = -1;
                msg->ec = std::error_code(errno, std::generic_category());
                is_register = false;
                enqueue(msg);
            }
        }
        break;
    }
    case io_operation_type::CLOSE: {
        int ret = ::close(msg->fd);
        if (ret == -1) {
            msg->result = -1;
            msg->ec = std::error_code(errno, std::generic_category());
        }
        else {
            msg->result = 0;
        }
        is_register = false;
        enqueue(msg);
        break;
    }
    case io_operation_type::TIMEOUT: {
        uint16_t fflags = msg->flags;
        EV_SET(&kev, reinterpret_cast<uintptr_t>(msg), EVFILT_TIMER, EV_ADD | EV_ONESHOT, fflags,
               msg->timeout.tv_sec * 1000 + msg->timeout.tv_nsec / 1000000, msg);
        break;
    }
    case io_operation_type::TIMEOUT_REMOVE: {
        EV_SET(&kev, reinterpret_cast<uintptr_t>(msg), EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
        std::lock_guard lock(m_kq_mutex);
        int res = kevent(m_kqueue_fd, &kev, 1, nullptr, 0, nullptr);
        if (res == -1 && errno != ENOENT) {
            msg->result = -1;
            msg->ec = std::error_code(errno, std::generic_category());
        }
        else {
            bool expected = false;
            if (msg->completed.compare_exchange_strong(expected, true)) {
                msg->result = -ECANCELED;
                msg->ec = std::error_code(ECANCELED, std::generic_category());
                enqueue(msg);
            }
        }
        is_register = false; // No need to register event
        break;
    }
    case io_operation_type::NOP:
        msg->result = 0;
        is_register = false;
        enqueue(msg);
        break;
    case io_operation_type::CANCEL: {
        std::lock_guard lock(m_kq_mutex);

        // Attempt to delete EVFILT_READ event
        EV_SET(&kev, msg->fd, EVFILT_READ, EV_DELETE, 0, 0, msg);
        kevent(m_kqueue_fd, &kev, 1, nullptr, 0, nullptr);
        // No need to check for errors unless you want to log them

        // Attempt to delete EVFILT_WRITE event
        EV_SET(&kev, msg->fd, EVFILT_WRITE, EV_DELETE, 0, 0, msg);
        kevent(m_kqueue_fd, &kev, 1, nullptr, 0, nullptr);
        // Again, errors can be ignored for ENOENT

        // Mark the target message as canceled
        msg->result = -ECANCELED;
        is_register = false;
        enqueue(msg);
        break;
    }
    default:
        // Unsupported operation
        msg->result = -1;
        msg->ec = std::make_error_code(std::errc::function_not_supported);
        is_register = false;
        enqueue(msg);
    }

    // Register the event
    if (is_register) {
        std::lock_guard lock(m_kq_mutex);
        if (kevent(m_kqueue_fd, &kev, 1, nullptr, 0, nullptr) == -1) {
            msg->result = -1;
            msg->ec = std::error_code(errno, std::generic_category());
            enqueue(msg);
        }
    }
}

inline void message_queue::process_one(struct kevent& event) {
    auto* msg = static_cast<io_message*>(event.udata);

    if (msg == nullptr || msg->canceled) {
        return;
    }

    // Handle the event
    if (event.flags & EV_ERROR) {
        // Error occurred
        msg->result = -1;
        msg->ec = std::error_code(event.data, std::generic_category());
        return;
    }

    switch (msg->operation) {
    case io_operation_type::READ:
        // Perform read operation
        msg->result = ::read(msg->fd, msg->buffer, msg->size);
        if (msg->result == -1) {
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    case io_operation_type::WRITE:
        // Perform write operation
        msg->result = ::write(msg->fd, msg->buffer, msg->size);
        if (msg->result == -1) {
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    case io_operation_type::PREAD:
        // Perform pread operation
        msg->result = ::pread(msg->fd, msg->buffer, msg->size, msg->offset);
        if (msg->result == -1) {
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    case io_operation_type::PWRITE:
        // Perform pwrite operation
        msg->result = ::pwrite(msg->fd, msg->buffer, msg->size, msg->offset);
        if (msg->result == -1) {
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    case io_operation_type::RECV:
        // Perform recv operation
        msg->result = ::recv(msg->fd, msg->buffer, msg->size, msg->flags);
        if (msg->result == -1) {
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    case io_operation_type::SEND:
        // Perform send operation
        msg->result = ::send(msg->fd, msg->buffer, msg->size, msg->flags);
        if (msg->result == -1) {
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    case io_operation_type::RECVMSG:
        // Perform recvmsg operation
        msg->result = ::recvmsg(msg->fd, msg->msg, msg->flags);
        if (msg->result == -1) {
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    case io_operation_type::SENDMSG:
        // Perform sendmsg operation
        msg->result = ::sendmsg(msg->fd, msg->msg, msg->flags);
        if (msg->result == -1) {
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    case io_operation_type::CONNECT: {
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(msg->fd, SOL_SOCKET, SO_ERROR, &error, &len) == -1) {
            // getsockopt failed
            msg->result = -1;
            msg->ec = std::error_code(errno, std::generic_category());
        }
        else if (error != 0) {
            // connect failed
            msg->result = -1;
            msg->ec = std::error_code(error, std::generic_category());
        }
        else {
            // connect succeeded
            msg->result = 0;
        }
        break;
    }
    case io_operation_type::ACCEPT: {
        // Perform accept operation
        socklen_t addrlen = msg->size;
        int fd = ::accept(msg->fd, static_cast<sockaddr*>(msg->buffer), &addrlen);
        if (fd >= 0) {
            msg->result = fd;
        }
        else {
            msg->result = -1;
            msg->ec = std::error_code(errno, std::generic_category());
        }
        break;
    }
    case io_operation_type::TIMEOUT: {
        if (msg->canceled.load()) {
            msg->result = -ECANCELED;
            msg->ec = std::error_code(ECANCELED, std::generic_category());
        }
        else {
            msg->result = 0;
        }
        break;
    }
    default:
        msg->result = -EINVAL;
        msg->ec = std::error_code(EINVAL, std::generic_category());
        break;
    }
    enqueue(msg);
}

}

#endif //MACOS_H
