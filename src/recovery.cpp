#include "headers/recovery.h"
#include "headers/allocator.h"

#include "headers/metadata_format.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

uint32_t log_address_to_flat_block(const Superblock& sb, const LogAddress& addr) {
    return addr.segment_id * sb.blocks_per_segment + addr.block_index;
}

FsError apply_inode_record(FileSystemState& fs, const std::vector<std::byte>& payload) {
    if (payload.size() != sizeof(InodeRecordPayload)) {
        return FsError::Internal;
    }

    InodeRecordPayload rec{};
    std::memcpy(&rec, payload.data(), sizeof(rec));

    const InodeType type = rec.inode_type == 1u ? InodeType::Directory : InodeType::File;
    Inode* inode = fs.inode_table.get(rec.inode_id);
    if (inode == nullptr) {
        Inode created(rec.inode_id, type, rec.parent_inode);
        if (!fs.inode_table.insert(created)) {
            return FsError::Internal;
        }
        inode = fs.inode_table.get(rec.inode_id);
        if (inode == nullptr) {
            return FsError::Internal;
        }
    }

    inode->set_size_bytes(rec.size_bytes);
    for (std::size_t i = 0; i < kNumDirectBlocks; ++i) {
        if (rec.direct_blocks[i] < 0) {
            inode->set_block_pointer(i, std::nullopt);
        } else {
            inode->set_block_pointer(i, static_cast<uint32_t>(rec.direct_blocks[i]));
        }
    }

    if (type == InodeType::Directory) {
        fs.directories.try_emplace(rec.inode_id);
    } else {
        fs.file_data.try_emplace(rec.inode_id, "");
    }

    fs.next_inode_id = std::max(fs.next_inode_id, rec.inode_id + 1);
    return FsError::Ok;
}

FsError apply_dirent_record(FileSystemState& fs, const std::vector<std::byte>& payload) {
    if (payload.size() < sizeof(DirentRecordHeader)) {
        return FsError::Internal;
    }

    DirentRecordHeader hdr{};
    std::memcpy(&hdr, payload.data(), sizeof(hdr));
    const std::size_t expected = sizeof(DirentRecordHeader) + hdr.name_size;
    if (payload.size() != expected) {
        return FsError::Internal;
    }

    std::string name;
    name.resize(hdr.name_size);
    if (hdr.name_size > 0) {
        std::memcpy(name.data(),
                    payload.data() + sizeof(DirentRecordHeader),
                    hdr.name_size);
    }

    fs.directories[hdr.parent_inode][name] = hdr.child_inode;
    fs.next_inode_id = std::max(fs.next_inode_id, hdr.child_inode + 1);
    return FsError::Ok;
}

FsError apply_checkpoint_record(FileSystemState& fs,
                                const LogAddress& addr,
                                const std::vector<std::byte>& payload) {
    if (payload.size() != sizeof(CheckpointPayload)) {
        return FsError::Internal;
    }

    CheckpointPayload cp{};
    std::memcpy(&cp, payload.data(), sizeof(cp));
    if (cp.version != kCheckpointVersion) {
        return FsError::Internal;
    }

    fs.root_inode = cp.root_inode;
    fs.next_inode_id = std::max(fs.next_inode_id, cp.next_inode_id);
    fs.superblock.checkpoint_block = log_address_to_flat_block(fs.superblock, addr);
    return FsError::Ok;
}

} // namespace

namespace recovery {

FsError replay_log(FileSystemState& fs) {
    if (fs.segment_io == nullptr) {
        return FsError::Internal;
    }

    std::map<InodeId, std::map<uint32_t, std::string>> data_blocks;

    for (uint32_t seg = 0; seg < fs.superblock.segment_count; ++seg) {
        uint32_t block = fs.superblock.reserved_blocks_per_segment;
        while (block < fs.superblock.blocks_per_segment) {
            const LogAddress addr{seg, block};
            RecordHeader hdr{};
            std::vector<std::byte> payload;
            FsError err = fs_read_record(fs, addr, hdr, payload);
            if (err == FsError::NotFound) {
                break;
            }
            if (err != FsError::Ok) {
                break;
            }

            const RecordType type = static_cast<RecordType>(hdr.type);
            if (type == RecordType::Inode) {
                err = apply_inode_record(fs, payload);
            } else if (type == RecordType::Dirent) {
                err = apply_dirent_record(fs, payload);
            } else if (type == RecordType::Checkpoint) {
                err = apply_checkpoint_record(fs, addr, payload);
            } else if (type == RecordType::Data) {
                data_blocks[hdr.owner_inode][hdr.logical_block_index] =
                    std::string(reinterpret_cast<const char*>(payload.data()), payload.size());
            } else {
                err = FsError::Ok;
            }

            if (err != FsError::Ok) {
                return err;
            }

            fs.latest_records[hdr.key] = addr;

            const uint32_t blocks_used =
                record_blocks(fs.superblock.block_size_bytes, hdr.payload_size_bytes);
            if (fs.allocator != nullptr) {
                fs.allocator->note_existing_record(addr, blocks_used, hdr.seq_no);
            }

            block += blocks_used;
        }
    }

    for (const auto& [inode_id, inode] : fs.inode_table) {
        if (inode.type() != InodeType::File) {
            continue;
        }

        std::string assembled;
        std::size_t remaining = static_cast<std::size_t>(inode.size_bytes());
        const auto blocks_it = data_blocks.find(inode_id);
        for (std::size_t i = 0; i < kNumDirectBlocks && remaining > 0; ++i) {
            if (blocks_it == data_blocks.end()) {
                break;
            }
            const auto it = blocks_it->second.find(static_cast<uint32_t>(i));
            if (it == blocks_it->second.end()) {
                break;
            }
            const std::size_t take = std::min<std::size_t>(remaining, it->second.size());
            assembled.append(it->second.data(), take);
            remaining -= take;
        }

        fs.file_data[inode_id] = assembled;
    }

    return FsError::Ok;
}

} // namespace recovery
