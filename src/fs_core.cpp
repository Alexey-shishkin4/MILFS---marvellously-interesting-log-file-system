#include "headers/fs_core.h"

#include "headers/allocator.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <sstream>
#include <filesystem>
#include <iostream>

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

Allocator& allocator(FileSystemState& fs) { return *fs.allocator; }

uint64_t make_data_key(InodeId inode_id, uint32_t logical_block_index) {
    return (static_cast<uint64_t>(inode_id) << 32) |
           static_cast<uint64_t>(logical_block_index);
}

std::size_t block_offset_bytes(const Superblock& sb,
                               uint32_t segment_id,
                               uint32_t block_index) {
    const std::size_t flat_block =
        static_cast<std::size_t>(segment_id) * sb.blocks_per_segment +
        block_index;
    return flat_block * sb.block_size_bytes;
}

FsError append_record(FileSystemState& fs,
                      RecordType type,
                      uint64_t key,
                      uint32_t owner_inode,
                      uint32_t logical_block_index,
                      const std::byte* payload,
                      std::size_t payload_size,
                      LogAddress& out_addr) {
    Allocation alloc{};
    FsError err =
        allocator(fs).allocate_record(static_cast<uint32_t>(payload_size),
                                      alloc);
    if (err != FsError::Ok) {
        return err;
    }

    RecordHeader hdr{};
    hdr.type = static_cast<uint16_t>(type);
    hdr.flags = RecordFlags::kNone;
    hdr.key = key;
    hdr.seq_no = allocator(fs).next_record_seq_no();
    hdr.payload_size_bytes = static_cast<uint32_t>(payload_size);
    hdr.total_size_bytes = record_total_size(fs.superblock.block_size_bytes,
                                             hdr.payload_size_bytes);
    hdr.owner_inode = owner_inode;
    hdr.logical_block_index = logical_block_index;

    const std::size_t write_off = block_offset_bytes(fs.superblock,
                                                     alloc.addr.segment_id,
                                                     alloc.addr.block_index);

    if (write_off + hdr.total_size_bytes > fs.disk_image.size()) {
        return FsError::NoSpace;
    }

    std::memcpy(fs.disk_image.data() + write_off, &hdr, sizeof(hdr));

    if (payload_size > 0) {
        std::memcpy(fs.disk_image.data() + write_off + sizeof(hdr),
                    payload,
                    payload_size);
    }

    fs.latest_records[key] = alloc.addr;
    out_addr = alloc.addr;
    return FsError::Ok;
}

} // namespace

void fs_init(FileSystemState& fs) {
    fs.root_inode = 1;
    fs.next_inode_id = 2;
    fs.inode_table = InodeTable{};
    fs.directories.clear();
    fs.file_data.clear();
    fs.latest_records.clear();

    Inode root(fs.root_inode, InodeType::Directory, 0);
    fs.inode_table.insert(root);
    fs.directories[root.id()] = {};

    fs.superblock.version = 1;

    fs.superblock.block_size_bytes = 4096;
    fs.superblock.blocks_per_segment = 256;
    fs.superblock.segment_count = 64;
    fs.superblock.reserved_blocks_per_segment = 1;

    fs.superblock.disk_size_bytes =
        static_cast<uint64_t>(fs.superblock.block_size_bytes) *
        fs.superblock.blocks_per_segment * fs.superblock.segment_count;

    fs.superblock.checkpoint_block = 0;
    fs.superblock.features_compat = 0;
    fs.superblock.features_incompat = 0;

    fs.disk_image.clear();
    fs.disk_image.resize(
        static_cast<std::size_t>(fs.superblock.disk_size_bytes));

    static std::unique_ptr<Allocator> g_allocator;
    g_allocator = std::make_unique<Allocator>();
    g_allocator->init(fs.superblock);
    fs.allocator = g_allocator.get();
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

std::optional<InodeId> fs_lookup(const FileSystemState& fs,
                                 const std::string& path) {
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

FsError fs_write(FileSystemState& fs,
                 const std::string& path,
                 const std::string& data) {
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

    const uint32_t block_sz = fs.superblock.block_size_bytes;
    const std::size_t total_blocks = (data.size() + block_sz - 1) / block_sz;

    if (total_blocks > kNumDirectBlocks) {
        return FsError::NoSpace;
    }

    const std::byte* raw = reinterpret_cast<const std::byte*>(data.data());

    for (std::size_t i = 0; i < total_blocks; ++i) {
        const std::size_t begin = i * block_sz;
        const std::size_t end = std::min(begin + block_sz, data.size());
        const std::size_t payload_size = end - begin;

        LogAddress addr{};
        FsError err =
            append_record(fs,
                          RecordType::Data,
                          make_data_key(*ino_opt, static_cast<uint32_t>(i)),
                          *ino_opt,
                          static_cast<uint32_t>(i),
                          raw + begin,
                          payload_size,
                          addr);
        if (err != FsError::Ok) {
            return err;
        }

        const uint32_t flat_block_id =
            addr.segment_id * fs.superblock.blocks_per_segment +
            addr.block_index;

        inode->set_block_pointer(i, flat_block_id);
    }

    return FsError::Ok;
}

FsError
fs_read(const FileSystemState& fs, const std::string& path, std::string& out) {
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


FsError fs_remove(FileSystemState& fs, const std::string& path) {
    std::filesystem::path p(path);
    std::string dir_path = p.parent_path().string();
    std::string basename = p.filename().string();
    
    auto parent_ino = fs_lookup(fs, dir_path.empty() ? "/" : dir_path);
    if (!parent_ino) return FsError::NotFound;
    
    auto& entries = fs.directories[*parent_ino];
    if (entries.find(basename) == entries.end()) return FsError::NotFound;
    
    InodeId child_ino = entries[basename];
    fs.directories[*parent_ino].erase(basename);
    
    std::cout << "[T10] rm " << path << " (ino=" << child_ino << ")\n";
    return FsError::Ok;
}

FsError fs_rename(FileSystemState& fs, const std::string& from, const std::string& to) {
    std::filesystem::path p_from(from), p_to(to);
    std::string dir_from = p_from.parent_path().string();
    std::string name_from = p_from.filename().string();
    std::string dir_to = p_to.parent_path().string();
    std::string name_to = p_to.filename().string();
    
    auto parent_from_ino = fs_lookup(fs, dir_from.empty() ? "/" : dir_from);
    if (!parent_from_ino) return FsError::NotFound;
    
    auto& entries_from = fs.directories[*parent_from_ino];
    if (entries_from.find(name_from) == entries_from.end()) return FsError::NotFound;
    
    InodeId child_ino = entries_from[name_from];
    
    auto parent_to_ino = fs_lookup(fs, dir_to.empty() ? "/" : dir_to);
    if (!parent_to_ino) return FsError::NotFound;
    auto& entries_to = fs.directories[*parent_to_ino];
    if (entries_to.count(name_to)) return FsError::AlreadyExists;
    
    entries_from.erase(name_from);
    entries_to[name_to] = child_ino;
    
    std::cout << "[T10] rename " << from << " -> " << to << "\n";
    return FsError::Ok;
}

FsError fs_truncate(FileSystemState& fs, const std::string& path, size_t new_size) {
    auto ino_opt = fs_lookup(fs, path);
    if (!ino_opt) return FsError::NotFound;
    
    Inode* inode = fs.inode_table.get(*ino_opt);
    if (!inode || inode->type() != InodeType::File) return FsError::NotDirectory;
    
    inode->set_size_bytes(new_size);
    auto& data = fs.file_data[*ino_opt];
    if (new_size < data.size()) {
        data.resize(new_size);
    }
    
    std::cout << "[T10] truncate " << path << " to " << new_size << " bytes\n";
    return FsError::Ok;
}
