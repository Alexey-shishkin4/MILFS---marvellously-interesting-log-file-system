#include "headers/gc.h"

#include "headers/allocator.h"
#include "headers/metadata_log.h"
#include "headers/segment_io.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {

bool same_address(const LogAddress& a, const LogAddress& b)
{
    return a.segment_id == b.segment_id && a.block_index == b.block_index;
}

uint32_t log_address_to_flat_block(const Superblock& sb, const LogAddress& addr)
{
    return addr.segment_id * sb.blocks_per_segment + addr.block_index;
}

struct LiveRecord {
    RecordHeader header{};
    std::vector<std::byte> payload;
    LogAddress old_addr{};
};

bool is_live_record(const FileSystemState& fs,
                    const RecordHeader& hdr,
                    const LogAddress& addr)
{
    auto it = fs.latest_records.find(hdr.key);

    if (it == fs.latest_records.end()) {
        return false;
    }

    return same_address(it->second, addr);
}

FsError collect_live_records(FileSystemState& fs,
                             uint32_t segment_id,
                             std::vector<LiveRecord>& out,
                             GcStats& stats)
{
    out.clear();

    uint32_t block = fs.superblock.reserved_blocks_per_segment;

    while (block < fs.superblock.blocks_per_segment) {
        LogAddress addr{segment_id, block};

        RecordHeader hdr{};
        std::vector<std::byte> payload;

        FsError err = fs_read_record(fs, addr, hdr, payload);

        if (err == FsError::NotFound) {
            break;
        }

        if (err != FsError::Ok) {
            return err;
        }

        const uint32_t blocks_used = record_blocks(
            fs.superblock.block_size_bytes,
            hdr.payload_size_bytes
        );

        if (blocks_used == 0) {
            return FsError::Internal;
        }

        if (is_live_record(fs, hdr, addr)) {
            out.push_back(LiveRecord{hdr, std::move(payload), addr});
        } else {
            ++stats.skipped_records;
        }

        block += blocks_used;
    }

    return FsError::Ok;
}

FsError update_inode_pointer_after_data_move(FileSystemState& fs,
                                             const RecordHeader& hdr,
                                             const LogAddress& new_addr)
{
    if (static_cast<RecordType>(hdr.type) != RecordType::Data) {
        return FsError::Ok;
    }

    Inode* inode = fs.inode_table.get(hdr.owner_inode);

    if (inode == nullptr) {
        return FsError::Ok;
    }

    if (hdr.logical_block_index >= kNumDirectBlocks) {
        return FsError::Internal;
    }

    const uint32_t flat_block = log_address_to_flat_block(
        fs.superblock,
        new_addr
    );

    inode->set_block_pointer(hdr.logical_block_index, flat_block);
    inode->touch();

    return metadata_log::flush_inode_record(fs, *inode);
}

FsError move_live_record(FileSystemState& fs,
                         const LiveRecord& rec,
                         GcStats& stats)
{
    LogAddress new_addr{};

    FsError err = fs_append_record(
        fs,
        static_cast<RecordType>(rec.header.type),
        rec.header.key,
        rec.header.owner_inode,
        rec.header.logical_block_index,
        rec.payload.empty() ? nullptr : rec.payload.data(),
        rec.payload.size(),
        new_addr
    );

    if (err != FsError::Ok) {
        return err;
    }

    err = update_inode_pointer_after_data_move(fs, rec.header, new_addr);
    if (err != FsError::Ok) {
        return err;
    }

    ++stats.moved_records;

    if (static_cast<RecordType>(rec.header.type) == RecordType::Data) {
        ++stats.moved_data_records;
    }

    return FsError::Ok;
}

} // namespace

FsError fs_gc_once(FileSystemState& fs, GcStats& stats)
{
    if (fs.allocator == nullptr || fs.segment_io == nullptr) {
        return FsError::Internal;
    }

    FsError err = fs.segment_io->flush();
    if (err != FsError::Ok) {
        return err;
    }

    auto candidate = fs.allocator->choose_gc_candidate();

    if (!candidate.has_value()) {
        std::cout << "[GC] no sealed segment to clean\n";
        return FsError::Ok;
    }

    const uint32_t segment_id = *candidate;

    std::cout << "[GC] cleaning segment " << segment_id << "\n";

    std::vector<LiveRecord> live_records;

    err = collect_live_records(fs, segment_id, live_records, stats);
    if (err != FsError::Ok) {
        return err;
    }

    std::cout << "[GC] live records: " << live_records.size() << "\n";

    for (const auto& rec : live_records) {
        err = move_live_record(fs, rec, stats);
        if (err != FsError::Ok) {
            return err;
        }
    }

    err = fs_flush(fs);
    if (err != FsError::Ok) {
        return err;
    }

    err = fs.segment_io->zero_segment(segment_id);
    if (err != FsError::Ok) {
        return err;
    }

    err = fs.allocator->mark_segment_free(segment_id);
    if (err != FsError::Ok) {
        return err;
    }

    ++stats.cleaned_segments;

    std::cout << "[GC] cleaned segment " << segment_id
              << ", moved_records=" << stats.moved_records
              << ", moved_data_records=" << stats.moved_data_records
              << ", skipped_records=" << stats.skipped_records
              << "\n";

    return FsError::Ok;
}
