#include "headers/fs_core.h"

#include <algorithm>
#include <sstream>

namespace {

std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> parts;
    std::stringstream ss(path);
    std::string item;

    while (std::getline(ss, item, '/')) {
        if (!item.empty()) {
            parts.push_back(item);
        }
    }

    return parts;
}

FsError add_child(FileSystemState& fs,
                  InodeId parent,
                  const std::string& name,
                  InodeId child_id) {
    Inode* parent_inode = fs.inode_table.get(parent);
    if (!parent_inode) {
        return FsError::NotFound;
    }

    if (parent_inode->type() != InodeType::Directory) {
        return FsError::NotDirectory;
    }

    auto& entries = fs.directories[parent];
    if (entries.contains(name)) {
        return FsError::AlreadyExists;
    }

    entries[name] = child_id;
    parent_inode->add_child(child_id);
    parent_inode->touch();

    return FsError::Ok;
}

}  // namespace

void fs_init(FileSystemState& fs) {
    fs.root_inode = 1;
    fs.next_inode_id = 2;
    fs.inode_table = InodeTable{};
    fs.directories.clear();
    fs.file_data.clear();

    Inode root(fs.root_inode, InodeType::Directory, 0);
    fs.inode_table.insert(root);
    fs.directories[root.id()] = {};
}

std::pair<std::string, std::string> fs_split_parent(const std::string& path) {
    if (path.empty() || path == "/") {
        return {"/", ""};
    }

    auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return {"/", path};
    }

    if (pos == 0) {
        return {"/", path.substr(1)};
    }

    return {path.substr(0, pos), path.substr(pos + 1)};
}

std::optional<InodeId> fs_lookup(const FileSystemState& fs, const std::string& path) {
    if (path.empty() || path == "/") {
        return fs.root_inode;
    }

    auto parts = split_path(path);
    InodeId current = fs.root_inode;

    for (const auto& part : parts) {
        auto dir_it = fs.directories.find(current);
        if (dir_it == fs.directories.end()) {
            return std::nullopt;
        }

        auto entry_it = dir_it->second.find(part);
        if (entry_it == dir_it->second.end()) {
            return std::nullopt;
        }

        current = entry_it->second;
    }

    return current;
}

FsError fs_mkdir(FileSystemState& fs, const std::string& path) {
    auto [parent_path, name] = fs_split_parent(path);
    if (name.empty()) {
        return FsError::InvalidPath;
    }

    auto parent_opt = fs_lookup(fs, parent_path);
    if (!parent_opt) {
        return FsError::NotFound;
    }

    Inode* parent_inode = fs.inode_table.get(*parent_opt);
    if (!parent_inode) {
        return FsError::NotFound;
    }

    if (parent_inode->type() != InodeType::Directory) {
        return FsError::NotDirectory;
    }

    InodeId new_id = fs.next_inode_id++;
    Inode node(new_id, InodeType::Directory, *parent_opt);

    if (!fs.inode_table.insert(node)) {
        return FsError::Internal;
    }

    fs.directories[new_id] = {};
    return add_child(fs, *parent_opt, name, new_id);
}

FsError fs_create(FileSystemState& fs, const std::string& path) {
    auto [parent_path, name] = fs_split_parent(path);
    if (name.empty()) {
        return FsError::InvalidPath;
    }

    auto parent_opt = fs_lookup(fs, parent_path);
    if (!parent_opt) {
        return FsError::NotFound;
    }

    Inode* parent_inode = fs.inode_table.get(*parent_opt);
    if (!parent_inode) {
        return FsError::NotFound;
    }

    if (parent_inode->type() != InodeType::Directory) {
        return FsError::NotDirectory;
    }

    InodeId new_id = fs.next_inode_id++;
    Inode node(new_id, InodeType::File, *parent_opt);

    if (!fs.inode_table.insert(node)) {
        return FsError::Internal;
    }

    fs.file_data[new_id] = "";
    return add_child(fs, *parent_opt, name, new_id);
}

FsError fs_write(FileSystemState& fs, const std::string& path, const std::string& data) {
    auto ino_opt = fs_lookup(fs, path);
    if (!ino_opt) {
        return FsError::NotFound;
    }

    Inode* inode = fs.inode_table.get(*ino_opt);
    if (!inode) {
        return FsError::NotFound;
    }

    if (inode->type() != InodeType::File) {
        return FsError::IsDirectory;
    }

    fs.file_data[*ino_opt] = data;
    inode->set_size_bytes(data.size());
    inode->touch();

    return FsError::Ok;
}

FsError fs_read(const FileSystemState& fs, const std::string& path, std::string& out) {
    auto ino_opt = fs_lookup(fs, path);
    if (!ino_opt) {
        return FsError::NotFound;
    }

    const Inode* inode = fs.inode_table.get(*ino_opt);
    if (!inode) {
        return FsError::NotFound;
    }

    if (inode->type() != InodeType::File) {
        return FsError::IsDirectory;
    }

    auto it = fs.file_data.find(*ino_opt);
    if (it == fs.file_data.end()) {
        out.clear();
    } else {
        out = it->second;
    }

    return FsError::Ok;
}

FsError fs_listdir(const FileSystemState& fs,
                   const std::string& path,
                   std::vector<std::string>& entries) {
    auto ino_opt = fs_lookup(fs, path);
    if (!ino_opt) {
        return FsError::NotFound;
    }

    const Inode* inode = fs.inode_table.get(*ino_opt);
    if (!inode) {
        return FsError::NotFound;
    }

    if (inode->type() != InodeType::Directory) {
        return FsError::NotDirectory;
    }

    auto dir_it = fs.directories.find(*ino_opt);
    if (dir_it == fs.directories.end()) {
        return FsError::Internal;
    }

    entries.clear();
    for (const auto& [name, _] : dir_it->second) {
        entries.push_back(name);
    }

    std::sort(entries.begin(), entries.end());
    return FsError::Ok;
}

