#pragma once

#include <array>
#include <cstdint>

#include "inode.h"

#pragma pack(push, 1)
struct InodeRecordPayload {
    uint32_t inode_id;
    uint32_t inode_type;
    uint32_t parent_inode;
    uint64_t size_bytes;
    int64_t created_at;
    int64_t modified_at;
    std::array<int32_t, kNumDirectBlocks> direct_blocks;
};

struct DirentRecordHeader {
    uint32_t parent_inode;
    uint32_t child_inode;
    uint32_t name_size;
};
#pragma pack(pop)


