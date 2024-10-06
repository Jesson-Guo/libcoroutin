//
// Created by Jesson on 2024/10/5.
//

#ifndef MACOS_H
#define MACOS_H

#include <fcntl.h>
#include <queue>
#include <sys/event.h>   // 用于kqueue
#include <sys/types.h>
#include <unistd.h>

namespace coro::detail::macos {

using fd_t = int;

enum message_type {
    CALLBACK_TYPE,
    RESUME_TYPE
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

struct message {
    message_type m_type;
    void* m_ptr;
};

struct io_state : message {
    using callback_type = void(io_state* state);
    callback_type* m_callback;
};

class message_queue {
    int                     m_pipe_fds[2];   // 管道文件描述符，[0]为读端，[1]为写端
    safe_fd                 m_kqueue_fd;
    size_t                  m_queue_length;    // 队列最大长度
    std::queue<message>     m_message_queue;   // 实际的消息队列
    std::mutex              m_mutex;           // 保护队列的互斥锁
    std::condition_variable m_cond_var;        // 条件变量

public:
    explicit message_queue(const size_t queue_length)
        : m_pipe_fds{-1, -1}
        , m_queue_length(queue_length) {
        // 创建kqueue
        m_kqueue_fd = safe_fd{kqueue()};
        if (m_kqueue_fd.fd() == -1) {
            throw std::system_error{errno, std::system_category(), "创建kqueue时出错"};
        }

        // 创建管道
        if (pipe(m_pipe_fds) == -1) {
            throw std::system_error{errno, std::system_category(), "创建管道时出错"};
        }

        // 将管道的读端设置为非阻塞
        const int flags = fcntl(m_pipe_fds[0], F_GETFL, 0);
        if (flags == -1) {
            throw std::system_error{errno, std::system_category(), "获取管道标志时出错"};
        }
        if (fcntl(m_pipe_fds[0], F_SETFL, flags | O_NONBLOCK) == -1) {
            throw std::system_error{errno, std::system_category(), "设置管道标志时出错"};
        }

        // 将管道的读端注册到kqueue
        struct kevent kev {};
        EV_SET(&kev, m_pipe_fds[0], EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, NULL);
        if (kevent(m_kqueue_fd.fd(), &kev, 1, nullptr, 0, nullptr) == -1) {
            throw std::system_error{errno, std::system_category(), "将管道注册到kqueue时出错"};
        }
    }

    ~message_queue() {
        close(m_pipe_fds[0]);
        close(m_pipe_fds[1]);
        // m_kqueue_fd将在safe_fd析构函数中关闭
    }

    bool enqueue_message(void* msg, message_type type) {
        std::unique_lock lock(m_mutex);
        if (m_message_queue.size() >= m_queue_length) {
            // 队列已满，无法插入新消息
            return false;
        }

        m_message_queue.push({type, msg});

        // 向管道写入一个字节，通知有新消息
        constexpr char notification = 0;
        if (const ssize_t status = write(m_pipe_fds[1], &notification, 1); status != 1) {
            throw std::system_error{errno, std::system_category(), "写入管道通知时出错"};
        }

        return true;
    }

    bool dequeue_message(void*& msg, message_type& type, bool wait) {
        // 使用kevent等待管道的读取事件
        struct kevent kev {};
        timespec      timeout{};
        timespec*     p_timeout = nullptr;
        if (!wait) {
            timeout.tv_sec  = 0;
            timeout.tv_nsec = 0;
            p_timeout       = &timeout;
        }
        const int n_events = kevent(m_kqueue_fd.fd(), nullptr, 0, &kev, 1, p_timeout);
        if (n_events == -1) {
            throw std::system_error{errno, std::system_category(), "kevent调用时出错"};
        }
        if (n_events == 0) {
            return false;
        }

        std::unique_lock lock(m_mutex);
        if (m_message_queue.empty()) {
            return false;
        }

        auto [m_type, m_ptr] = m_message_queue.front();
        m_message_queue.pop();
        msg = m_ptr;
        type = m_type;

        // 从管道读取一个字节，清除通知
        char buffer;
        if (const ssize_t status = read(m_pipe_fds[0], &buffer, 1); status != 1) {
            throw std::system_error{errno, std::system_category(), "读取管道通知时出错"};
        }

        return true;
    }
};

inline safe_fd create_kqueue_fd() {
    const int fd = kqueue();
    if (fd == -1) {
        throw std::system_error{errno, std::system_category(), "Error creating kqueue fd"};
    }
    return safe_fd{fd};
}

}

#endif //MACOS_H
