//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/file.h"
#include <filesystem>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/disk.h>
#include <sys/file.h>
#include <cerrno>

coro::file::~file() = default;

coro::file::file(safe_fd&& file_handle) noexcept
    : m_file_handle(std::move(file_handle))
    , m_io_service(nullptr) {}

std::uint64_t coro::file::size() const {
    struct stat st{};

    if (fstat(m_file_handle.fd(), &st) < 0) {
        throw std::system_error {
            errno, std::system_category(), "error getting file size: fstat"
        };
    }
    if (S_ISREG(st.st_mode)) {
        // regular file
        return static_cast<std::uint64_t>(st.st_size);
    }
    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        // 使用 ioctl 获取块设备的大小
        uint64_t block_count = 0;
        if (ioctl(m_file_handle.fd(), DKIOCGETBLOCKCOUNT, &block_count) == -1) {
            throw std::system_error{
                errno, std::system_category(), "error getting block count: ioctl(DKIOCGETBLOCKCOUNT)"};
        }

        uint32_t block_size = 0;
        if (ioctl(m_file_handle.fd(), DKIOCGETBLOCKSIZE, &block_size) == -1) {
            throw std::system_error{
                errno, std::system_category(), "error getting block size: ioctl(DKIOCGETBLOCKSIZE)"};
        }
        return block_count * block_size;
    }

    throw std::system_error {
        errno, std::system_category(), "error getting file size: unsupported file type"
    };
}

coro::file::safe_fd coro::file::open(
    int fill_access,
    io_service& io_svc,
    const std::filesystem::path& path,
    file_open_mode open_mode,
    file_share_mode share_mode,
    file_buffering_mode buffering_mode) {
    (void) io_svc;  // 暂未使用

    int flags = 0;

    // 设置文件打开模式
    switch (open_mode) {
        case file_open_mode::create_or_open:
            flags |= O_CREAT;
            break;
        case file_open_mode::create_always:
            flags |= O_CREAT | O_TRUNC;
            break;
        case file_open_mode::create_new:
            flags |= O_CREAT | O_EXCL;
            break;
        case file_open_mode::open_existing:
            // 不需要额外的 flag
            break;
        case file_open_mode::truncate_existing:
            flags |= O_TRUNC;
            break;
        default:
            throw std::invalid_argument("Invalid file_open_mode");
    }

    // 设置文件访问模式
    if ((fill_access & O_RDWR) == O_RDWR) {
        flags |= O_RDWR;
    }
    else if (fill_access & O_WRONLY) {
        flags |= O_WRONLY;
    }
    else if (fill_access & O_RDONLY) {
        flags |= O_RDONLY;
    }
    else {
        // 如果未指定访问模式，默认只读
        flags |= O_RDONLY;
    }

    // 设置文件缓冲模式
    if ((buffering_mode & file_buffering_mode::random_access) == file_buffering_mode::random_access) {
        // 使用 posix_fadvise 提示随机访问
        // 暂时记录，需要在打开文件后设置
    }
    if ((buffering_mode & file_buffering_mode::sequential) == file_buffering_mode::sequential) {
        // 使用 posix_fadvise 提示顺序访问
        // 暂时记录，需要在打开文件后设置
    }
    if ((buffering_mode & file_buffering_mode::write_through) == file_buffering_mode::write_through) {
        flags |= O_SYNC;
    }
    if ((buffering_mode & file_buffering_mode::unbuffered) == file_buffering_mode::unbuffered) {
        // 使用 fcntl 设置 F_NOCACHE
        // 暂时记录，需要在打开文件后设置
    }
    if ((buffering_mode & file_buffering_mode::temporary) == file_buffering_mode::temporary) {
        // macOS 没有直接的临时文件属性
        // 可以在关闭文件后删除文件
    }

    // 打开文件
    int fd;
    if (flags & O_CREAT) {
        fd = ::open(path.c_str(), flags, fill_access);
    }
    else {
        fd = ::open(path.c_str(), flags);
    }

    if (fd < 0) {
        throw std::system_error {
            errno, std::generic_category(), "error opening file"
        };
    }

    safe_fd file_handle(fd);

    // 设置文件共享模式
    // 使用 flock 设置文件锁
    if (share_mode == file_share_mode::none) {
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd);
            throw std::system_error {
                errno, std::system_category(), "error locking file exclusively"
            };
        }
    }
    else if (share_mode == file_share_mode::read) {
        if (flock(fd, LOCK_SH | LOCK_NB) != 0) {
            ::close(fd);
            throw std::system_error {
                errno, std::system_category(), "error locking file shared"
            };
        }
    }
    else if (share_mode == file_share_mode::write || share_mode == file_share_mode::read_write) {
        // 没有直接的方式实现，仅使用排他锁
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd);
            throw std::system_error {
                errno, std::system_category(), "error locking file exclusively"
            };
        }
    }
    else {
        // 不支持的共享模式
        ::close(fd);
        throw std::system_error {
            0, std::system_category(), "file::open unsupported share_mode"
        };
    }

    // 设置文件缓冲模式（打开后设置）
    if ((buffering_mode & file_buffering_mode::unbuffered) == file_buffering_mode::unbuffered) {
        int result = fcntl(fd, F_NOCACHE, 1);
        if (result == -1) {
            ::close(fd);
            throw std::system_error {
                errno, std::system_category(), "error setting F_NOCACHE"
            };
        }
    }

    if ((buffering_mode & file_buffering_mode::random_access) == file_buffering_mode::random_access) {
        // posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM);
        // 在macOS上可以选择手动优化随机访问
    }
    if ((buffering_mode & file_buffering_mode::sequential) == file_buffering_mode::sequential) {
        // posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
        // 在macOS上可以选择手动优化顺序访问
    }

    // 如果是临时文件，在关闭后删除
    if ((buffering_mode & file_buffering_mode::temporary) == file_buffering_mode::temporary) {
        // 在关闭文件时删除文件
        // 可以设置 close-on-exec，并在文件关闭后 unlink
        // 这里需要在文件对象的析构函数中处理
    }

    return std::move(file_handle);
}
