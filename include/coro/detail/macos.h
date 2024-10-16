//
// Created by Jesson on 2024/10/5.
//

#ifndef MACOS_H
#define MACOS_H

#include "../thread_pool.h"

#include <coroutine>
#include <sys/event.h>   // 用于kqueue
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace coro::detail::macos {

using fd_t = int;

enum message_type {
    RESUME_TYPE
};

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
    int result;
    int fd;
    void* buffer;
    size_t size;
    off_t offset;
    msghdr* msg;
    int flags;
    io_operation_type operation;
    message_type type;
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

class kqueue_queue;

class io_transaction final {
public:
    io_transaction(kqueue_queue& queue, io_message& message) noexcept;

    bool commit() noexcept;

    [[nodiscard]] io_transaction& read(int fd, void* buffer, size_t size) noexcept;
    [[nodiscard]] io_transaction& write(int fd, const void* buffer, size_t size) noexcept;

    [[nodiscard]] io_transaction& pread(int fd, void* buffer, size_t size, off_t offset) noexcept;
    [[nodiscard]] io_transaction& pwrite(int fd, const void* buffer, size_t size, off_t offset) noexcept;

    [[nodiscard]] io_transaction& recv(int fd, void* buffer, size_t size = 0) noexcept;
    [[nodiscard]] io_transaction& send(int fd, const void* buffer, size_t size = 0) noexcept;

    [[nodiscard]] io_transaction& recvmsg(int fd, msghdr *msg = nullptr, int flags = 0) noexcept;
    [[nodiscard]] io_transaction& sendmsg(int fd, msghdr *msg = nullptr, int flags = 0) noexcept;

    [[nodiscard]] io_transaction& connect(int fd, const void* addr, socklen_t addrlen) noexcept;
    [[nodiscard]] io_transaction& accept(int fd, const void* addr, socklen_t* addrlen) noexcept;
    [[nodiscard]] io_transaction& close(int fd) noexcept;
    [[nodiscard]] io_transaction& timeout(timespec* ts) noexcept;
    [[nodiscard]] io_transaction& timeout_remove(int flags = 0) noexcept;
    [[nodiscard]] io_transaction& nop() noexcept;
    [[nodiscard]] io_transaction& cancel() noexcept;

private:
    kqueue_queue& m_queue;
    io_message& m_message;
    struct kevent m_event;
};

class kqueue_queue {
public:
    explicit kqueue_queue()
        : m_event_index(0)
        , m_thread_pool(std::thread::hardware_concurrency()) {
        // 创建 kqueue
        m_kqueue_fd = kqueue();
        if (m_kqueue_fd == -1) {
            throw std::system_error{errno, std::system_category(), "Error initializing kqueue"};
        }

        // 初始化事件缓冲区，预留一定空间
        m_event_buffer.reserve(64); // 可以根据需要调整初始容量

        // 注册 EVFILT_USER 事件，用于唤醒
        struct kevent event{};
        EV_SET(&event, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
        if (kevent(m_kqueue_fd, &event, 1, nullptr, 0, nullptr) == -1) {
            throw std::system_error{errno, std::system_category(), "Failed to register EVFILT_USER"};
        }
    }

    ~kqueue_queue() noexcept {
        if (m_kqueue_fd != -1) {
            close(m_kqueue_fd);
        }
    }

    kqueue_queue(kqueue_queue&&) = delete;
    kqueue_queue& operator=(kqueue_queue&&) = delete;
    kqueue_queue(const kqueue_queue&) = delete;
    kqueue_queue& operator=(const kqueue_queue&) = delete;

    int get_kqueue_fd() const {
        return m_kqueue_fd;
    }

    io_transaction transaction(io_message& message) noexcept {
        return io_transaction{*this, message};
    }

    bool dequeue(io_message*& message, bool wait) {
        std::unique_lock lock(m_event_mutex);

        // 检查缓冲区中是否还有未处理的事件
        if (m_event_index < m_event_buffer.size()) {
            struct kevent& event = m_event_buffer[m_event_index++];
            // 处理事件
            message = static_cast<io_message*>(event.udata);
            if (message != nullptr) {
                // 根据事件类型处理
                process_event(message);
                return true; // 成功处理了一个事件
            }
        }

        // 缓冲区已空，需要从 kqueue 获取新的事件
        m_event_buffer.clear();
        m_event_index = 0;

        timespec timeout{};
        timespec* p_timeout = nullptr;

        if (!wait) {
            // 如果不需要等待事件，将超时时间设置为 0
            timeout.tv_sec = 0;
            timeout.tv_nsec = 0;
            p_timeout = &timeout;
        }

        const int max_events = 64;   // 每次最多获取的事件数量
        m_event_buffer.resize(max_events);

        const int nevents = kevent(m_kqueue_fd, nullptr, 0, m_event_buffer.data(), max_events, p_timeout);

        if (nevents == -1) {
            if (errno == EINTR) {
                // 系统调用被中断
                return false;
            }
            // 其他错误，抛出异常
            throw std::system_error{errno, std::system_category(), "kevent failed"};
        }

        if (nevents == 0) {
            // 没有事件
            m_event_buffer.clear();
            return false;
        }

        // 调整缓冲区大小为实际获取的事件数量
        m_event_buffer.resize(nevents);

        // 处理获取到的第一个事件
        struct kevent& event = m_event_buffer[m_event_index++];
        message = static_cast<io_message*>(event.udata);
        if (message != nullptr) {
            // 根据事件类型处理
            process_event(message);
            return true; // 成功处理了一个事件
        }

        // 如果第一个事件无效，继续在缓冲区中查找有效事件
        while (m_event_index < m_event_buffer.size()) {
            struct kevent& evt = m_event_buffer[m_event_index++];
            message = static_cast<io_message*>(evt.udata);
            if (message != nullptr) {
                // 根据事件类型处理
                process_event(message);
                return true; // 成功处理了一个事件
            }
        }

        // 没有处理到有效的事件
        return false;
    }

private:
    friend class io_transaction;

    void process_event(io_message* message) {
        // 根据 message->operation 处理不同的操作类型
        ssize_t res = 0;
        switch (message->operation) {
        case io_operation_type::READ:
            res = read(message->fd, message->buffer, message->size);
            if (res == -1) {
                message->result = -errno;
            }
            else {
                message->result = res;
            }
            break;
        case io_operation_type::WRITE:
            res = write(message->fd, message->buffer, message->size);
            if (res == -1) {
                message->result = -errno;
            }
            else {
                message->result = res;
            }
            break;
        case io_operation_type::PREAD:
            // 提交到线程池执行
            m_thread_pool.schedule([message]() {
                ssize_t res = pread(message->fd, message->buffer, message->size, message->offset);
                if (res == -1) {
                    message->result = -errno;
                }
                else {
                    message->result = res;
                }
            });
            break;
        case io_operation_type::PWRITE:
            // 提交到线程池执行
            m_thread_pool.schedule([message]() {
                ssize_t res = ::pwrite(message->fd, message->buffer, message->size, message->offset);
                if (res == -1) {
                    message->result = -errno;
                }
                else {
                    message->result = res;
                }
            });
            break;
        case io_operation_type::RECVMSG:
            res = recvmsg(message->fd, message->msg, message->flags);
            if (res == -1) {
                message->result = -errno;
            }
            else {
                message->result = res;
            }
            break;
        case io_operation_type::SENDMSG:
            res = sendmsg(message->fd, message->msg, message->flags);
            if (res == -1) {
                message->result = -errno;
            }
            else {
                message->result = res;
            }
            break;
        case io_operation_type::TIMEOUT:
            // TODO ???
            message->result = -ETIMEDOUT;
            break;
        case io_operation_type::NOP:
            // TODO ???
            message->result = 0;
            break;
            // 处理其他操作...
        default:
            message->result = -ENOSYS; // 功能未实现
            break;
        }
    }

    int m_kqueue_fd;
    size_t m_event_index; // 当前处理的事件索引
    std::vector<struct kevent> m_event_buffer; // 事件缓冲区
    std::mutex m_event_mutex;
    thread_pool m_thread_pool;
};

using io_queue = kqueue_queue;

}

coro::detail::macos::io_transaction::io_transaction(kqueue_queue& queue, io_message& message) noexcept
    : m_queue{queue}
    , m_message{message}
    , m_event{} {
    // Initialize the kevent structure
    EV_SET(&m_event, 0, EVFILT_USER, 0, 0, 0, nullptr);
}

bool coro::detail::macos::io_transaction::commit() noexcept {
    std::scoped_lock lock(m_queue.m_event_mutex);
    if (m_event.filter != 0) {
        // Set the message pointer as the udata for identification
        m_event.udata = &m_message;

        // Register the event with kqueue
        if (kevent(m_queue.m_kqueue_fd, &m_event, 1, nullptr, 0, nullptr) == -1) {
            m_message.result = -errno;
            return false;
        }
        return true;
    }
    // No operation specified
    m_message.result = -ENOSYS;   // Function not implemented
    return false;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::read(int fd, void* buffer, size_t size) noexcept {
    // For simplicity, we ignore the offset as macOS does not support it directly in kevent
    if (fd >= 0 && buffer != nullptr && size > 0) {
        EV_SET(&m_event, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
        m_message.fd = fd;
        m_message.buffer = buffer;
        m_message.size = size;
        m_message.operation = io_operation_type::READ;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::write(int fd, const void* buffer, size_t size) noexcept {
    // For simplicity, we ignore the offset as macOS does not support it directly in kevent
    if (fd >= 0 && buffer != nullptr && size > 0) {
        EV_SET(&m_event, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
        m_message.fd = fd;
        m_message.buffer = const_cast<void*>(buffer);
        m_message.size = size;
        m_message.operation = io_operation_type::WRITE;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::pread(int fd, void* buffer, size_t size, off_t offset) noexcept {
    if (fd >= 0 && buffer != nullptr && size > 0) {
        m_message.fd = fd;
        m_message.buffer = buffer;
        m_message.size = size;
        m_message.offset = offset;
        m_message.operation = io_operation_type::PREAD;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::pwrite(int fd, const void* buffer, size_t size, off_t offset) noexcept {
    if (fd >= 0 && buffer != nullptr && size > 0) {
        m_message.fd = fd;
        m_message.buffer = const_cast<void*>(buffer);
        m_message.size = size;
        m_message.offset = offset;
        m_message.operation = io_operation_type::PWRITE;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::recv(int fd, void* buffer, size_t size) noexcept {
    // Similar to read
    if (fd >= 0 && buffer != nullptr && size > 0) {
        EV_SET(&m_event, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
        m_message.fd = fd;
        m_message.buffer = buffer;
        m_message.size = size;
        m_message.operation = io_operation_type::RECV;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::send(int fd, const void* buffer, size_t size) noexcept {
    // Similar to write
    if (fd >= 0 && buffer != nullptr && size > 0) {
        EV_SET(&m_event, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
        m_message.fd = fd;
        m_message.buffer = const_cast<void*>(buffer);
        m_message.size = size;
        m_message.operation = io_operation_type::SEND;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::recvmsg(int fd, msghdr* msg, int flags) noexcept {
    if (fd >= 0 && msg != nullptr) {
        EV_SET(&m_event, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
        m_message.fd = fd;
        m_message.msg = msg;
        m_message.flags = flags;
        m_message.operation = io_operation_type::RECVMSG;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::sendmsg(int fd, msghdr* msg, int flags) noexcept {
    if (fd >= 0 && msg != nullptr) {
        EV_SET(&m_event, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
        m_message.fd = fd;
        m_message.msg = msg;
        m_message.flags = flags;
        m_message.operation = io_operation_type::SENDMSG;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::connect(int fd, const void* addr, socklen_t addrlen) noexcept {
    if (fd >= 0 && addr != nullptr) {
        int res = ::connect(fd, addr, addrlen);
        if (res == -1 && errno != EINPROGRESS) {
            m_message.result = -errno;
        }
        else {
            EV_SET(&m_event, fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
            m_message.fd = fd;
            m_message.operation = io_operation_type::CONNECT;
            m_message.type = RESUME_TYPE;
        }
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::accept(int fd, const void* addr, socklen_t* addrlen) noexcept {
    if (fd >= 0) {
        EV_SET(&m_event, fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, nullptr);
        m_message.fd = fd;
        m_message.buffer = addr;
        m_message.size = addrlen ? *addrlen : 0;
        m_message.operation = io_operation_type::ACCEPT;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::close(int fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
        m_message.result = 0;
        m_message.operation = io_operation_type::CLOSE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::timeout(timespec* ts) noexcept {
    if (ts != nullptr) {
        // 将 timespec 转换为毫秒
        int timeout_ms = static_cast<int>(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
        EV_SET(&m_event, reinterpret_cast<uintptr_t>(&m_message), EVFILT_TIMER, EV_ADD | EV_ONESHOT, 0, timeout_ms, &m_message);
        m_message.operation = io_operation_type::TIMEOUT;
        m_message.type = RESUME_TYPE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::timeout_remove(int flags) noexcept {
    // 使用 kevent 删除定时器事件
    struct kevent kev{};
    EV_SET(&kev, reinterpret_cast<uintptr_t>(&m_message), EVFILT_TIMER, EV_DELETE, flags, 0, nullptr);
    // 提交删除定时器的 kevent
    if (kevent(m_queue.m_kqueue_fd, &kev, 1, nullptr, 0, nullptr) == -1) {
        m_message.result = -errno;  // 设置错误码
        m_message.operation = io_operation_type::TIMEOUT_REMOVE;
    }
    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::nop() noexcept {
    // 使用 kqueue 的 EVFILT_USER 事件模拟 no-op 操作
    EV_SET(&m_event, 0, EVFILT_USER, EV_ADD | EV_ONESHOT, 0, 0, &m_message);

    // 提交 no-op 事件到 kqueue
    if (kevent(m_queue.m_kqueue_fd, &m_event, 1, nullptr, 0, nullptr) == -1) {
        m_message.result = -errno;
        m_message.operation = io_operation_type::NOP;
    }

    return *this;
}

coro::detail::macos::io_transaction& coro::detail::macos::io_transaction::cancel() noexcept {
    // 使用 EV_DELETE 删除事件
    // 删除事件并不能真正取消系统调用，如果系统调用已经阻塞，可能无法立即返回。
    EV_SET(&m_event, m_message.fd, m_event.filter, EV_DELETE, 0, 0, nullptr);
    if (kevent(m_queue.m_kqueue_fd, &m_event, 1, nullptr, 0, nullptr) == -1) {
        m_message.result = -errno;
    }
    else {
        m_message.result = -ECANCELED;
    }
    m_message.operation = io_operation_type::CANCEL;
    return *this;
}

#endif //MACOS_H
