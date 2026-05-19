#include "headers/metadata_log.h"

#include "headers/metadata_format.h"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

constexpr uint64_t kMetaKeyPrefix = 0x8000000000000000ULL;
constexpr uint64_t kMetaKeyInode = 0x1000000000000000ULL;
constexpr uint64_t kMetaKeyDirent = 0x2000000000000000ULL;
constexpr uint64_t kCheckpointKey = 0x7000000000000001ULL;

uint64_t make_inode_key(InodeId inode_id) {
    return kMetaKeyPrefix | kMetaKeyInode | static_cast<uint64_t>(inode_id);
}

uint64_t make_dirent_key(InodeId parent_inode, InodeId child_inode) {
    return kMetaKeyPrefix | kMetaKeyDirent |
           ((static_cast<uint64_t>(parent_inode) & 0xffffffffULL) << 32) |
           (static_cast<uint64_t>(child_inode) & 0xffffffffULL);
}

} // namespace

namespace metadata_log {

FsError flush_inode_record(FileSystemState& fs, const Inode& inode) {
    InodeRecordPayload payload{};
    payload.inode_id = inode.id();
    payload.inode_type = inode.type() == InodeType::Directory ? 1u : 0u;
    payload.parent_inode = inode.parent();
    payload.size_bytes = inode.size_bytes();
    payload.created_at = static_cast<int64_t>(inode.created_at());
    payload.modified_at = static_cast<int64_t>(inode.modified_at());

    for (std::size_t i = 0; i < kNumDirectBlocks; ++i) {
        const auto& ptr = inode.direct_blocks()[i];
        payload.direct_blocks[i] = ptr ? static_cast<int32_t>(*ptr) : -1;
    }

    LogAddress addr{};
    return fs_append_record(fs,
                            RecordType::Inode,
                            make_inode_key(inode.id()),
                            inode.id(),
                            0,
                            reinterpret_cast<const std::byte*>(&payload),
                            sizeof(payload),
                            addr);
}

FsError flush_dirent_record(FileSystemState& fs,
                            InodeId parent_inode,
                            InodeId child_inode,
                            const std::string& name) {
    if (name.size() > std::numeric_limits<uint32_t>::max()) {
        return FsError::Internal;
    }

    DirentRecordHeader hdr{};
    hdr.parent_inode = parent_inode;
    hdr.child_inode = child_inode;
    hdr.name_size = static_cast<uint32_t>(name.size());

    std::vector<std::byte> payload(sizeof(DirentRecordHeader) + name.size());
    std::memcpy(payload.data(), &hdr, sizeof(hdr));
    if (!name.empty()) {
        std::memcpy(payload.data() + sizeof(hdr), name.data(), name.size());
    }

    LogAddress addr{};
    return fs_append_record(fs,
                            RecordType::Dirent,
                            make_dirent_key(parent_inode, child_inode),
                            parent_inode,
                            0,
                            payload.data(),
                            payload.size(),
                            addr);
}


FsError flush_dirent_tombstone_record(FileSystemState& fs,
                                      InodeId parent_inode,
                                      InodeId child_inode) {
    DirentRecordHeader hdr{};
    hdr.parent_inode = parent_inode;
    hdr.child_inode = child_inode;
    hdr.name_size = 0;

    LogAddress addr{};

    return fs_append_record(
        fs,
        RecordType::Tombstone,
        make_dirent_key(parent_inode, child_inode),
        parent_inode,
        0,
        reinterpret_cast<const std::byte*>(&hdr),
        sizeof(hdr),
        addr
    );
}


FsError write_checkpoint_record(FileSystemState& fs) {
    CheckpointPayload payload{};
    payload.version = kCheckpointVersion;
    payload.root_inode = fs.root_inode;
    payload.next_inode_id = fs.next_inode_id;

    LogAddress addr{};
    return fs_append_record(fs,
                            RecordType::Checkpoint,
                            kCheckpointKey,
                            fs.root_inode,
                            0,
                            reinterpret_cast<const std::byte*>(&payload),
                            sizeof(payload),
                            addr);
}

} // namespace metadata_log
