#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "lfs.h"
#include "fs_core.h"


class LogBuffer {
    struct BufferedWrite {
        uint64_t offset;
        std::vector<std::byte> data;
    };

    std::size_t max_size_;
    std::size_t current_size_;
    std::vector<BufferedWrite> pending_writes_;

public:
    explicit LogBuffer(std::size_t max_size_bytes = Superblock::kDefaultBlockSizeBytes): max_size_(max_size_bytes), current_size_(0) {}

    FsError append(uint64_t offset, const void* data, std::size_t size, int fd);
    FsError flush_to_disk(int fd);

    FsError read_with_cache(uint64_t offset, void* data, std::size_t size, int fd) const;
};