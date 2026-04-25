#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "directory.h"
#include "inode.h"
#include "inode_table.h"
#include "lfs.h"

class Allocator;

enum class FsError {
    Ok = 0,
    NotFound,
    AlreadyExists,
    NotDirectory,
    IsDirectory,
    InvalidPath,
    NoSpace,
    Internal,
};

struct FileSystemState {
    InodeId root_inode = 0;
    InodeId next_inode_id = 1;

    InodeTable inode_table;
    std::unordered_map<InodeId, DirectoryEntries> directories;
    std::unordered_map<InodeId, std::string> file_data;

    Superblock superblock{};
    std::vector<std::byte> disk_image;
    std::unordered_map<uint64_t, LogAddress> latest_records;
    Allocator* allocator = nullptr;
};

void fs_init(FileSystemState& fs);

std::pair<std::string, std::string> fs_split_parent(const std::string& path);

std::optional<InodeId> fs_lookup(const FileSystemState& fs,
                                 const std::string& path);

FsError fs_mkdir(FileSystemState& fs, const std::string& path);
FsError fs_create(FileSystemState& fs, const std::string& path);
FsError
fs_write(FileSystemState& fs, const std::string& path, const std::string& data);
FsError
fs_read(const FileSystemState& fs, const std::string& path, std::string& out);
FsError fs_listdir(const FileSystemState& fs,
                   const std::string& path,
                   std::vector<std::string>& entries);
FsError fs_remove(FileSystemState& fs, const std::string& path);
FsError fs_rename(FileSystemState& fs, const std::string& from, const std::string& to);
FsError fs_truncate(FileSystemState& fs, const std::string& path, size_t new_size);
