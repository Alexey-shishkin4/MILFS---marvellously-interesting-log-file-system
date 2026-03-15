#include "headers/fs_core.h"
#include <sstream>
#include <algorithm>

namespace {

// helper: split path by '/'
std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, '/')) {
        if (!item.empty()) parts.push_back(item);
    }
    return parts;
}

}

void fs_init(FileSystemState& fs) {
    fs.inodes.clear();
    fs.dir_entries.clear();
    fs.next_inode_id = 1;

    Inode root;
    root.id = fs.next_inode_id++;
    root.is_dir = true;
    root.size = 0;
    root.parent = -1;

    fs.root_inode = root.id;
    fs.inodes[root.id] = root;
    fs.dir_entries[root.id] = {};
}

std::pair<std::string, std::string> fs_split_parent(const std::string& path) {
    if (path == "/" || path.empty()) return {"/", ""};
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return {"/", path};   // "name" -> ("/","name")
    if (pos == 0) return {"/", path.substr(1)};         // "/name"
    return {path.substr(0, pos), path.substr(pos + 1)}; // "/a/b/c" -> "/a/b","c"
}

std::optional<int> fs_lookup(const FileSystemState& fs, const std::string& path) {
    if (path == "/" || path.empty()) return fs.root_inode;

    auto parts = split_path(path);
    int current = fs.root_inode;

    for (const auto& part : parts) {
        const auto it = fs.dir_entries.find(current);
        if (it == fs.dir_entries.end()) return std::nullopt;

        bool found = false;
        for (const auto& de : it->second) {
            if (de.name == part) {
                current = de.inode_id;
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return current;
}

static FsError add_child(FileSystemState& fs, int parent,
                         const std::string& name, int child_id) {
    auto it = fs.dir_entries.find(parent);
    if (it == fs.dir_entries.end()) return FsError::Internal;

    for (const auto& de : it->second) {
        if (de.name == name) return FsError::AlreadyExists;
    }

    it->second.push_back(DirEntry{name, child_id});
    fs.inodes[parent].children.push_back(child_id);
    return FsError::Ok;
}

FsError fs_mkdir(FileSystemState& fs, const std::string& path) {
    auto [parent_path, name] = fs_split_parent(path);
    if (name.empty()) return FsError::InvalidPath;

    auto parent_opt = fs_lookup(fs, parent_path);
    if (!parent_opt) return FsError::NotFound;
    int parent = *parent_opt;

    if (!fs.inodes.at(parent).is_dir) return FsError::NotDirectory;

    Inode node;
    node.id = fs.next_inode_id++;
    node.is_dir = true;
    node.size = 0;
    node.parent = parent;

    fs.inodes[node.id] = node;
    fs.dir_entries[node.id] = {};

    return add_child(fs, parent, name, node.id);
}

FsError fs_create(FileSystemState& fs, const std::string& path) {
    auto [parent_path, name] = fs_split_parent(path);
    if (name.empty()) return FsError::InvalidPath;

    auto parent_opt = fs_lookup(fs, parent_path);
    if (!parent_opt) return FsError::NotFound;
    int parent = *parent_opt;

    if (!fs.inodes.at(parent).is_dir) return FsError::NotDirectory;

    Inode node;
    node.id = fs.next_inode_id++;
    node.is_dir = false;
    node.size = 0;
    node.parent = parent;

    fs.inodes[node.id] = node;
    return add_child(fs, parent, name, node.id);
}

FsError fs_write(FileSystemState& fs, const std::string& path,
                 const std::string& data) {
    auto ino_opt = fs_lookup(fs, path);
    if (!ino_opt) return FsError::NotFound;
    Inode& node = fs.inodes[*ino_opt];

    if (node.is_dir) return FsError::IsDirectory;

    node.data_blocks.clear();
    node.data_blocks.push_back(data);
    node.size = data.size();
    return FsError::Ok;
}

FsError fs_read(const FileSystemState& fs, const std::string& path,
                std::string& out) {
    auto ino_opt = fs_lookup(fs, path);
    if (!ino_opt) return FsError::NotFound;
    const Inode& node = fs.inodes.at(*ino_opt);

    if (node.is_dir) return FsError::IsDirectory;

    out.clear();
    for (const auto& blk : node.data_blocks) out += blk;
    return FsError::Ok;
}

FsError fs_listdir(const FileSystemState& fs, const std::string& path,
                   std::vector<std::string>& entries) {
    auto ino_opt = fs_lookup(fs, path);
    if (!ino_opt) return FsError::NotFound;
    const Inode& node = fs.inodes.at(*ino_opt);

    if (!node.is_dir) return FsError::NotDirectory;

    entries.clear();
    const auto& dir = fs.dir_entries.at(*ino_opt);
    for (const auto& de : dir) entries.push_back(de.name);
    return FsError::Ok;
}

