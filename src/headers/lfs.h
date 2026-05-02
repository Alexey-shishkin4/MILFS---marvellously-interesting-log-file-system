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
  Outdated = 3,
  Bad = 4
};

enum class RecordType : uint16_t {
  Inode = 1,
  Data = 2,
  Dirent = 3,
  Checkpoint = 4,
  Tombstone = 5 // we are close to gc so its mark for outdated (rm'd/renamed) objects
};

enum RecordFlags : uint16_t {
  kNone = 0,
  kDeleted = 1u << 0,
  kHasPadding = 1u << 1
};

#pragma pack(push, 1)

struct Superblock {
  static constexpr uint32_t kDefaultVersion = 1;
  static constexpr uint32_t kDefaultBlockSizeBytes = 4096;
  static constexpr uint32_t kDefaultBlocksPerSegment = 256;
  static constexpr uint32_t kDefaultSegmentCount = 64;
  static constexpr uint32_t kDefaultReservedBlocksPerSegment = 1;

  uint32_t version;
  uint64_t disk_size_bytes;
  uint32_t block_size_bytes;
  uint32_t blocks_per_segment;
  uint32_t segment_count;
  uint32_t reserved_blocks_per_segment;
  uint64_t checkpoint_block;

  Superblock()
      : version(kDefaultVersion),
        disk_size_bytes(static_cast<uint64_t>(kDefaultBlockSizeBytes) *
                        kDefaultBlocksPerSegment *
                        kDefaultSegmentCount),
        block_size_bytes(kDefaultBlockSizeBytes),
        blocks_per_segment(kDefaultBlocksPerSegment),
        segment_count(kDefaultSegmentCount),
        reserved_blocks_per_segment(kDefaultReservedBlocksPerSegment),
        checkpoint_block(0) {}
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
