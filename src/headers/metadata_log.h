#pragma once

#include <string>

#include "fs_core.h"

namespace metadata_log {

FsError flush_inode_record(FileSystemState& fs, const Inode& inode);

FsError flush_dirent_record(
    FileSystemState& fs,
    InodeId parent_inode,
    InodeId child_inode,
    const std::string& name
);

FsError flush_dirent_tombstone_record(
    FileSystemState& fs,
    InodeId parent_inode,
    InodeId child_inode
);

FsError write_checkpoint_record(FileSystemState& fs);

}
