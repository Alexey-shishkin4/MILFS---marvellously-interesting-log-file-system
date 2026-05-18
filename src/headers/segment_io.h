#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "allocator.h"
#include "lfs.h"
#include "log_buffer.h"


class SegmentIO {
public:
    SegmentIO() = default;
    ~SegmentIO();

    SegmentIO(const SegmentIO&) = delete;
    SegmentIO& operator=(const SegmentIO&) = delete;

    SegmentIO(SegmentIO&& other) noexcept;
    SegmentIO& operator=(SegmentIO&& other) noexcept;

    FsError open_or_create(const std::string& image_path,
                           const Superblock& superblock,
                           bool truncate_existing);

    void close() noexcept;
    bool is_open() const noexcept { return fd_ >= 0; }

    const std::string& image_path() const noexcept { return image_path_; }
    const Superblock& superblock() const noexcept { return sb_; }

    FsError append_record(Allocator& allocator,
                          RecordType type,
                          uint64_t key,
                          uint32_t owner_inode,
                          uint32_t logical_block_index,
                          const std::byte* payload,
                          std::size_t payload_size,
                          LogAddress& out_addr,
                          RecordHeader* out_header = nullptr);

    FsError read_record(const LogAddress& addr,
                        RecordHeader& out_header,
                        std::vector<std::byte>& out_payload) const;

    FsError flush();

    FsError zero_segment(uint32_t segment_id);

private:
    uint64_t segment_offset_bytes(uint32_t segment_id) const;
    uint64_t block_offset_bytes(uint32_t segment_id, uint32_t block_index) const;

    FsError write_all_at(const void* data, std::size_t size, uint64_t offset);
    FsError read_all_at(void* data, std::size_t size, uint64_t offset) const;
    FsError write_superblock();
    FsError read_superblock(Superblock& out) const;
    FsError write_segment_header(const SegmentHeader& header);

private:
    int fd_ = -1;
    Superblock sb_{};
    std::string image_path_;
    LogBuffer log_buffer_{};
};
