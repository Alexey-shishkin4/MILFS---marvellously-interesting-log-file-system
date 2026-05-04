#include "headers/allocator.h"
#include <algorithm>
#include <ctime>

void Allocator::init(const Superblock &sb) {
  sb_ = sb;
  segments_.clear();
  free_segments_.clear();
  global_seq_no_ = 1;
  segment_seq_no_ = 1;
  active_segment_id_.reset();

  segments_.resize(sb.segment_count);

  for (uint32_t i = 0; i < sb.segment_count; ++i) {
    auto &seg = segments_[i];
    seg.segment_id = i;
    seg.state = static_cast<uint32_t>(SegmentState::Free);
    seg.sequence_no = 0;
    seg.data_start_block = sb.reserved_blocks_per_segment;
    seg.write_block_offset = sb.reserved_blocks_per_segment;
    seg.used_blocks = 0;
    seg.free_blocks = sb.blocks_per_segment - sb.reserved_blocks_per_segment;
    seg.record_count = 0;
    free_segments_.push_back(i);
  }
}

const Superblock &Allocator::superblock() const noexcept { return sb_; }

uint64_t Allocator::next_record_seq_no() noexcept {
  return global_seq_no_++;
}


void Allocator::note_existing_record(const LogAddress& addr,
                                     uint32_t block_count,
                                     uint64_t record_seq_no) {
  if (addr.segment_id >= segments_.size() || block_count == 0) {
    return;
  }

  auto &seg = segments_[addr.segment_id];

  if (static_cast<SegmentState>(seg.state) == SegmentState::Free) {
    seg.state = static_cast<uint32_t>(SegmentState::Open);
    seg.sequence_no = std::max<uint64_t>(seg.sequence_no, segment_seq_no_++);
    free_segments_.erase(
        std::remove(free_segments_.begin(), free_segments_.end(), addr.segment_id),
        free_segments_.end());
  }

  const uint32_t end_block = addr.block_index + block_count;
  if (end_block > seg.write_block_offset) {
    seg.write_block_offset = end_block;
  }

  seg.used_blocks = seg.write_block_offset - seg.data_start_block;
  seg.free_blocks = sb_.blocks_per_segment - seg.write_block_offset;
  seg.record_count += 1;
  global_seq_no_ = std::max(global_seq_no_, record_seq_no + 1);

  active_segment_id_ = addr.segment_id;
}


FsError Allocator::ensure_active_segment() {
  if (active_segment_id_.has_value()) {
    const auto &seg = segments_[*active_segment_id_];
    if (static_cast<SegmentState>(seg.state) == SegmentState::Open) {
      return FsError::Ok;
    }
  }
  return open_next_free_segment();
}

FsError Allocator::allocate_record(uint32_t payload_size_bytes,
                                        Allocation &out) {
  const uint32_t need_blocks =
      record_blocks(sb_.block_size_bytes, payload_size_bytes);

  if (need_blocks == 0) {
    return FsError::Internal;
  }

  if (need_blocks >
      (sb_.blocks_per_segment - sb_.reserved_blocks_per_segment)) {
    return FsError::NoSpace;
  }

  FsError err = ensure_active_segment();
  if (err != FsError::Ok) {
    return err;
  }

  auto &active = segments_[*active_segment_id_];
  if (active.write_block_offset + need_blocks > sb_.blocks_per_segment) {
    seal_active_segment();

    err = open_next_free_segment();
    if (err != FsError::Ok) {
      return err;
    }
  }

  auto &seg = segments_[*active_segment_id_];
  out.addr.segment_id = *active_segment_id_;
  out.addr.block_index = seg.write_block_offset;
  out.block_count = need_blocks;

  seg.write_block_offset += need_blocks;
  seg.used_blocks += need_blocks;
  seg.free_blocks -= need_blocks;
  seg.record_count += 1;

  return FsError::Ok;
}

const SegmentHeader *Allocator::active_segment() const {
  if (!active_segment_id_) {
    return nullptr;
  }
  return &segments_[*active_segment_id_];
}

const std::vector<SegmentHeader> &Allocator::segments() const noexcept {
  return segments_;
}

FsError Allocator::open_next_free_segment() {
  if (free_segments_.empty()) {
    return FsError::NoSpace;
  }

  const uint32_t seg_id = free_segments_.front();
  free_segments_.pop_front();

  auto &seg = segments_[seg_id];
  seg.state = static_cast<uint32_t>(SegmentState::Open);
  seg.sequence_no = segment_seq_no_++;
  seg.write_block_offset = seg.data_start_block;
  seg.used_blocks = 0;
  seg.free_blocks = sb_.blocks_per_segment - seg.data_start_block;
  seg.record_count = 0;

  active_segment_id_ = seg_id;
  return FsError::Ok;
}

void Allocator::seal_active_segment() {
  if (!active_segment_id_) {
    return;
  }

  auto &seg = segments_[*active_segment_id_];
  if (static_cast<SegmentState>(seg.state) == SegmentState::Open) {
    seg.state = static_cast<uint32_t>(SegmentState::Sealed);
  }

  active_segment_id_.reset();
}
