#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "directory.h"
#include "inode.h"
#include "inode_table.h"
#include "lfs.h"

class Allocator;
class SegmentIO;

enum class FsError {
    Ok = 0,
    NotFound,
    AlreadyExists,
    NotDirectory,
    IsDirectory,
    InvalidPath,
    NoSpace,
    IoError,
    Internal,
};

struct FileSystemState {
    FileSystemState();
    ~FileSystemState();

    InodeId root_inode = 0;
    InodeId next_inode_id = 1;

    InodeTable inode_table;
    std::unordered_map<InodeId, DirectoryEntries> directories;
    std::unordered_map<InodeId, std::string> file_data;

    Superblock superblock{};
    std::vector<std::byte> disk_image;
    std::string image_path = "milfs.img";
    bool sync_each_write = false;

    std::unordered_map<uint64_t, LogAddress> latest_records;
    std::unique_ptr<Allocator> allocator;
    std::unique_ptr<SegmentIO> segment_io;
};

void fs_init(FileSystemState& fs);
void fs_init_with_image(FileSystemState& fs,
                        const std::string& image_path,
                        bool truncate_existing = true);

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

FsError fs_append_record(FileSystemState& fs,
                         RecordType type,
                         uint64_t key,
                         uint32_t owner_inode,
                         uint32_t logical_block_index,
                         const std::byte* payload,
                         std::size_t payload_size,
                         LogAddress& out_addr);

FsError fs_read_record(const FileSystemState& fs,
                       const LogAddress& addr,
                       RecordHeader& out_header,
                       std::vector<std::byte>& out_payload);

FsError fs_flush(FileSystemState& fs);
