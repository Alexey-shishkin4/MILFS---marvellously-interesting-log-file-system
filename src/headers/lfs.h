#pragma once

#include <cstdint>

struct LogAddress {
    uint32_t segment_id = 0;
    uint32_t block_index = 0;
};

enum class SegmentState : uint32_t {
  Free = 0,
  Open = 1,
  Sealed = 2,
  Dirty = 3,
  Bad = 4
};

enum class RecordType : uint16_t {
  Inode = 1,
  Data = 2,
  Dirent = 3,
  Checkpoint = 4,
  Tombstone = 5,
  Metadata = 6
};

enum RecordFlags : uint16_t {
  kNone = 0,
  kDeleted = 1u << 0,
  kHasPadding = 1u << 1,
  kCompressed = 1u << 2
};

#pragma pack(push, 1)

struct Superblock {
  uint32_t version;
  uint64_t disk_size_bytes;
  uint32_t block_size_bytes;
  uint32_t blocks_per_segment;
  uint32_t segment_count;
  uint32_t reserved_blocks_per_segment;
  uint64_t checkpoint_block;
  uint64_t features_compat;
  uint64_t features_incompat;
};

struct SegmentHeader {
  uint32_t segment_id;
  uint32_t state;
  uint64_t sequence_no;
  uint64_t timestamp;
  uint32_t data_start_block;
  uint32_t write_block_offset;
  uint32_t used_blocks;
  uint32_t free_blocks;
  uint32_t record_count;
};

struct RecordHeader {
  uint16_t type;
  uint16_t flags;
  uint64_t key;
  uint64_t seq_no;
  uint32_t payload_size_bytes;
  uint32_t total_size_bytes;
  uint32_t owner_inode;
  uint32_t logical_block_index;
};

#pragma pack(pop)

uint32_t align_up(uint32_t x, uint32_t a);

uint32_t record_total_size(uint32_t block_size,
                                uint32_t payload_size);

uint32_t record_blocks(uint32_t block_size,
                            uint32_t payload_size);
