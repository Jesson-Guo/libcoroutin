//
// Created by Jesson on 2024/10/16.
//

#ifndef READABLE_FILE_H
#define READABLE_FILE_H

#include "file.h"
#include "file_read_operation.h"
#include "../cancellation/cancellation_token.h"

namespace coro {

class readable_file : virtual public file {
public:
    [[nodiscard]] file_read_operation read(std::uint64_t offset, void* buffer, std::size_t byte_count) const noexcept {
        return file_read_operation{*m_io_service, m_file_handle.fd(), offset, buffer, byte_count};
    }

    /// Read some data from the file.
    ///
    /// Reads \a byteCount bytes from the file starting at \a offset
    /// into the specified \a buffer.
    ///
    /// \param offset
    /// The offset within the file to start reading from.
    /// If the file has been opened using file_buffering_mode::unbuffered
    /// then the offset must be a multiple of the file-system's sector size.
    ///
    /// \param buffer
    /// The buffer to read the file contents into.
    /// If the file has been opened using file_buffering_mode::unbuffered
    /// then the address of the start of the buffer must be a multiple of
    /// the file-system's sector size.
    ///
    /// \param byte_count
    /// The number of bytes to read from the file.
    /// If the file has been opeend using file_buffering_mode::unbuffered
    /// then the byteCount must be a multiple of the file-system's sector size.
    ///
    /// \param ct
    /// An optional cancellation_token that can be used to cancel the
    /// read operation before it completes.
    ///
    /// \return
    /// An object that represents the read-operation.
    /// This object must be co_await'ed to start the read operation.
    [[nodiscard]] file_read_operation_cancellable read(
        std::uint64_t offset, void* buffer, std::size_t byte_count, cancellation_token ct) const noexcept {
        return file_read_operation_cancellable{
            *m_io_service, m_file_handle.fd(), offset, buffer, byte_count, std::move(ct)
        };
    }

protected:
    using file::file;
};

}

#endif //READABLE_FILE_H
