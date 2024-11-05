//
// Created by Jesson on 2024/10/17.
//

#include "../../include/coro/io/file_read_operation.h"

bool coro::file_read_operation_impl::try_start(detail::io_operation_base& operation, std::uint64_t offset) const noexcept {
    // 提交一个 PREAD 操作，通过 io_queue 进行管理
    const size_t bytes = m_byte_count <= std::numeric_limits<size_t>::max() ? m_byte_count : std::numeric_limits<size_t>::max();
    return operation.m_io_queue.transaction(&operation.m_message)
        .read(m_file_handle, m_buffer, bytes)
        .commit();
}

void coro::file_read_operation_impl::cancel(detail::io_operation_base& operation) noexcept {
    operation.m_io_queue.transaction(&operation.m_message).cancel().commit();
}
