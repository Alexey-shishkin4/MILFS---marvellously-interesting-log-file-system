#include <cstring>
#include <unistd.h>

#include "headers/log_buffer.h"


FsError LogBuffer::append(uint64_t offset, const void* data, std::size_t size, int fd) {
    if (size == 0) {
        return FsError::Ok;
    }

    if (current_size_ + size > max_size_) {
        FsError err = flush_to_disk(fd);
        if (err != FsError::Ok) {
            return err;
        }
    }

    const std::byte* bytes = static_cast<const std::byte*>(data);

    if (!pending_writes_.empty()) {
        auto& last_op = pending_writes_.back();
        if (last_op.offset + last_op.data.size() == offset) {
            last_op.data.insert(last_op.data.end(), bytes, bytes + size);
            current_size_ += size;
            return FsError::Ok;
        }
    }

    pending_writes_.push_back( {offset, std::vector<std::byte>(bytes, bytes + size)} );
    current_size_ += size;
    return FsError::Ok;
}

FsError LogBuffer::flush_to_disk(int fd) {
    if (pending_writes_.empty() || fd < 0) { 
        return FsError::Ok;
    }

    for (const auto& op : pending_writes_) {
        std::size_t written = 0;
        while (written < op.data.size()) {
            const ssize_t rc = ::pwrite(fd, op.data.data() + written,
                                        op.data.size() - written,
                                        static_cast<off_t>(op.offset + written));
            if (rc < 0) {
                if (errno == EINTR) { 
                    continue;
                }
                return FsError::IoError;
            }

            if (rc == 0) {
                return FsError::IoError;
            }

            written += static_cast<std::size_t>(rc);
        }
    }

    pending_writes_.clear();
    current_size_ = 0;
    return FsError::Ok;
}

FsError LogBuffer::read_with_cache(uint64_t offset, void* data, std::size_t size, int fd) const {
    if (size == 0) { 
        return FsError::Ok; 
    }

    auto* bytes = static_cast<std::byte*>(data);

    std::size_t readen = 0;
    while (readen < size) {
        ssize_t rc = ::pread(fd, bytes + readen,
                             size - readen,
                             static_cast<off_t>(offset + readen));
        if (rc < 0) {
            if (errno == EINTR) { 
                continue;
            } 
            return FsError::IoError;
        }
        if (rc == 0) { 
            return FsError::IoError;
        } 
        readen += static_cast<std::size_t>(rc);
    }

    uint64_t r_start = offset;
    uint64_t r_end = r_start + size;

    for (const auto& op : pending_writes_) {
        uint64_t b_start = op.offset;
        uint64_t b_end = b_start + op.data.size();

        uint64_t o_start = std::max(r_start, b_start);
        uint64_t o_end = std::min(r_end, b_end);

        if (o_start < o_end) {
            std::size_t dest_offset = o_start - r_start;
            std::size_t src_offset = o_start - b_start;

            std::memcpy(bytes + dest_offset,
                        op.data.data() + src_offset,
                        o_end - o_start);
        }
    }

    return FsError::Ok;
}
