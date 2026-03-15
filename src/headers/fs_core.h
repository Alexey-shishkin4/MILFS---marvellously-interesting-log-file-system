#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

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

struct Inode {
    int         id;
    bool        is_dir;
    size_t      size = 0;
    int         parent;                    // inode id of parent dir (-1 for root)
    std::vector<int>  children;           // for directories: child inode ids
    std::vector<std::string> data_blocks; // for files: simple list of chunks
};

struct DirEntry {
    std::string name;
    int         inode_id;
};

struct FileSystemState {
    int root_inode = -1;
    int next_inode_id = 1;

    std::unordered_map<int, Inode> inodes;
    std::unordered_map<int, std::vector<DirEntry>> dir_entries;
};

// in-memory FS (создаёт root /)
void fs_init(FileSystemState& fs);

// "/a/b/c" -> ("/a/b", "c")
std::pair<std::string, std::string> fs_split_parent(const std::string& path);

// Возвращает id или FsError::NotFound.
std::optional<int> fs_lookup(const FileSystemState& fs, const std::string& path);

// Создать директорию по абсолютному пути (например "/dir" или "/a/b").
FsError fs_mkdir(FileSystemState& fs, const std::string& path);

// Создать пустой файл по абсолютному пути.
FsError fs_create(FileSystemState& fs, const std::string& path);

// Полностью перезаписать файл содержимым data.
FsError fs_write(FileSystemState& fs, const std::string& path,
                 const std::string& data);

// Прочитать содержимое файла в out.
FsError fs_read(const FileSystemState& fs, const std::string& path,
                std::string& out);

// Получить список имён в директории path (для команды ls).
FsError fs_listdir(const FileSystemState& fs, const std::string& path,
                   std::vector<std::string>& entries);

