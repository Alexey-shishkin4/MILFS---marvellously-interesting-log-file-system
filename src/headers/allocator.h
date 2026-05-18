#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "lfs.h"
#include "fs_core.h"


struct Allocation {
  LogAddress addr;
  uint32_t block_count = 0;
};

class Allocator {
public:
  Allocator() = default;

  void init(const Superblock &sb);

  const Superblock &superblock() const noexcept;

  uint64_t next_record_seq_no() noexcept;

  FsError ensure_active_segment();

  FsError allocate_record(uint32_t payload_size_bytes, Allocation &out);

  const SegmentHeader *active_segment() const;

  const std::vector<SegmentHeader> &segments() const noexcept;

  void note_existing_record(const LogAddress& addr,
                            uint32_t block_count,
                            uint64_t record_seq_no);

  uint32_t free_segment_count() const noexcept;
  bool has_low_free_space(uint32_t min_free_segments = 2) const noexcept;
  std::optional<uint32_t> choose_gc_candidate() const;
  FsError mark_segment_free(uint32_t segment_id);

private:
  FsError open_next_free_segment();
  void seal_active_segment();

private:
  Superblock sb_{};
  std::vector<SegmentHeader> segments_;
  std::deque<uint32_t> free_segments_;
  std::optional<uint32_t> active_segment_id_;
  uint64_t global_seq_no_ = 1;
  uint64_t segment_seq_no_ = 1;
};
