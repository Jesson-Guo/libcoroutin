//
// Created by Jesson on 2024/10/16.
//

#ifndef WRITABLE_FILE_H
#define WRITABLE_FILE_H

#include "file.h"
#include "file_write_operation.h"
#include "../cancellation/cancellation_token.h"

namespace coro {

class writable_file : virtual public file {
public:
    /// Set the size of the file.
    ///
    /// \param file_size
    /// The new size of the file in bytes.
    void set_size(off_t file_size) {
        if (ftruncate(m_file_handle.fd(), file_size) < 0) {
            throw std::system_error{
                errno, std::system_category(), "error setting file size: ftruncate"
            };
        }
    }

    [[nodiscard]] file_write_operation write(std::uint64_t offset, void* buffer, std::size_t byte_count) noexcept {
        return file_write_operation{
            *m_io_service, m_file_handle.fd(), offset, buffer, byte_count
        };
    }

    /// Write some data to the file.
    ///
    /// Writes \a byteCount bytes from the file starting at \a offset
    /// into the specified \a buffer.
    ///
    /// \param offset
    /// The offset within the file to start writing from.
    /// If the file has been opened using file_buffering_mode::unbuffered
    /// then the offset must be a multiple of the file-system's sector size.
    ///
    /// \param buffer
    /// The buffer containing the data to be written to the file.
    /// If the file has been opened using file_buffering_mode::unbuffered
    /// then the address of the start of the buffer must be a multiple of
    /// the file-system's sector size.
    ///
    /// \param byte_count
    /// The number of bytes to write to the file.
    /// If the file has been opeend using file_buffering_mode::unbuffered
    /// then the byteCount must be a multiple of the file-system's sector size.
    ///
    /// \param ct
    /// An optional cancellation_token that can be used to cancel the
    /// write operation before it completes.
    ///
    /// \return
    /// An object that represents the write operation.
    /// This object must be co_await'ed to start the write operation.
    [[nodiscard]] file_write_operation_cancellable write(
        std::uint64_t offset, void* buffer, std::size_t byte_count, cancellation_token ct) noexcept {
        return file_write_operation_cancellable{
            *m_io_service, m_file_handle.fd(), offset, buffer, byte_count, std::move(ct)
        };
    }

protected:
    using file::file;
};

}

#endif //WRITABLE_FILE_H
