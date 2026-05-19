#include "headers/fs_core.h"

#include "headers/allocator.h"
#include "headers/metadata_log.h"
#include "headers/recovery.h"
#include "headers/segment_io.h"
#include "headers/gc.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <filesystem>
#include <iostream>


#include <stdexcept>

FileSystemState::FileSystemState() = default;
FileSystemState::~FileSystemState() = default;

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

    FsError err = metadata_log::flush_dirent_record(fs, parent, child_id, name);
    if (err != FsError::Ok) {
        return err;
    }

    err = metadata_log::flush_inode_record(fs, *parent_inode);
    if (err != FsError::Ok) {
        return err;
    }

    return FsError::Ok;
}

uint64_t make_data_key(InodeId inode_id, uint32_t logical_block_index) {
    return (static_cast<uint64_t>(inode_id) << 32) |
           static_cast<uint64_t>(logical_block_index);
}

LogAddress flat_block_to_log_address(const Superblock& sb, uint32_t flat_block) {
    return LogAddress{
        flat_block / sb.blocks_per_segment,
        flat_block % sb.blocks_per_segment,
    };
}

uint32_t log_address_to_flat_block(const Superblock& sb, const LogAddress& addr) {
    return addr.segment_id * sb.blocks_per_segment + addr.block_index;
}

std::size_t block_count_for_size(std::size_t size, uint32_t block_size) {
    if (size == 0) {
        return 0;
    }
    return (size + block_size - 1) / block_size;
}

void reset_runtime_state(FileSystemState& fs) {
    fs.root_inode = 1;
    fs.next_inode_id = 2;
    fs.inode_table = InodeTable{};
    fs.directories.clear();
    fs.file_data.clear();
    fs.latest_records.clear();

    Inode root(fs.root_inode, InodeType::Directory, 0);
    fs.inode_table.insert(root);
    fs.directories[root.id()] = {};
}

bool is_env_false(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return false;
    }
    return std::string(value) == "0";
}


thread_local bool g_inside_auto_gc = false;

bool auto_gc_enabled()
{
    const char* value = std::getenv("MILFS_AUTO_GC");

    if (value == nullptr) {
        return true;
    }

    return std::string(value) != "0";
}

uint32_t auto_gc_min_free_segments()
{
    const char* value = std::getenv("MILFS_GC_MIN_FREE_SEGMENTS");

    if (value == nullptr) {
        return 2;
    }

    try {
        return static_cast<uint32_t>(std::stoul(value));
    } catch (...) {
        return 2;
    }
}


} // namespace

void fs_init(FileSystemState& fs) {
    const char* image_env = std::getenv("MILFS_IMAGE");
    const std::string image_path = image_env != nullptr ? image_env : "milfs.img";

    // MILFS_RESET=0 to save image.
    const char* reset_env = std::getenv("MILFS_RESET");
    const bool truncate_existing = (reset_env != nullptr && std::string(reset_env) == "0");
    fs_init_with_image(fs, image_path, truncate_existing);
}

void fs_init_with_image(FileSystemState& fs,
                        const std::string& image_path,
                        bool truncate_existing) {
    reset_runtime_state(fs);

    fs.superblock = Superblock{};
    fs.image_path = image_path;
    fs.disk_image.clear();

    fs.allocator = std::make_unique<Allocator>();
    fs.allocator->init(fs.superblock);

    fs.segment_io = std::make_unique<SegmentIO>();
    const FsError err =
        fs.segment_io->open_or_create(image_path, fs.superblock, truncate_existing);
    if (err != FsError::Ok) {
        fs.segment_io.reset();
        fs.allocator.reset();
        throw std::runtime_error("failed to open image file");
    }

    if (!truncate_existing) {
        const FsError recover_err = recovery::replay_log(fs);
        if (recover_err != FsError::Ok) {
            throw std::runtime_error("failed to replay log");
        }
    }
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
    FsError err = metadata_log::flush_inode_record(fs, node);
    if (err != FsError::Ok) {
        return err;
    }
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
    FsError err = metadata_log::flush_inode_record(fs, node);
    if (err != FsError::Ok) {
        return err;
    }
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

    const uint32_t block_sz = fs.superblock.block_size_bytes;
    const std::size_t total_blocks = block_count_for_size(data.size(), block_sz);
    // std::cout << block_sz << ' ' << data.size() << ' ' << total_blocks << std::endl;

    if (total_blocks > kNumDirectBlocks) {
        return FsError::NoSpace;
    }

    const std::byte* raw = reinterpret_cast<const std::byte*>(data.data());
    std::vector<LogAddress> written_blocks;
    written_blocks.reserve(total_blocks);

    for (std::size_t i = 0; i < total_blocks; ++i) {
        const std::size_t begin = i * block_sz;
        const std::size_t end = std::min(begin + block_sz, data.size());
        const std::size_t payload_size = end - begin;

        LogAddress addr{};
        FsError err =
            fs_append_record(fs,
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

        written_blocks.push_back(addr);
    }

    fs.file_data[*ino_opt] = data;
    inode->set_size_bytes(data.size());

    for (std::size_t i = 0; i < kNumDirectBlocks; ++i) {
        if (i < written_blocks.size()) {
            inode->set_block_pointer(
                i, log_address_to_flat_block(fs.superblock, written_blocks[i]));
        } else {
            inode->set_block_pointer(i, std::nullopt);
        }
    }

    inode->touch();
    FsError err = metadata_log::flush_inode_record(fs, *inode);
    if (err != FsError::Ok) {
        return err;
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

    out.clear();
    std::size_t remaining = static_cast<std::size_t>(inode->size_bytes());
    if (remaining == 0) {
        return FsError::Ok;
    }

    for (std::size_t i = 0; i < kNumDirectBlocks && remaining > 0; ++i) {
        const auto& block_ptr = inode->direct_blocks()[i];
        if (!block_ptr) {
            return FsError::Internal;
        }

        const LogAddress addr = flat_block_to_log_address(fs.superblock, *block_ptr);
        RecordHeader hdr{};
        std::vector<std::byte> payload;
        FsError err = fs_read_record(fs, addr, hdr, payload);
        if (err != FsError::Ok) {
            return err;
        }

        if (static_cast<RecordType>(hdr.type) != RecordType::Data ||
            hdr.owner_inode != *ino_opt ||
            hdr.logical_block_index != i) {
            return FsError::Internal;
        }

        const std::size_t take = std::min<std::size_t>(remaining, payload.size());
        out.append(reinterpret_cast<const char*>(payload.data()), take);
        remaining -= take;
    }

    return remaining == 0 ? FsError::Ok : FsError::Internal;
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


FsError fs_remove(FileSystemState& fs, const std::string& path)
{
    std::filesystem::path p(path);

    std::string dir_path = p.parent_path().string();
    std::string basename = p.filename().string();

    auto parent_ino = fs_lookup(fs, dir_path.empty() ? "/" : dir_path);
    if (!parent_ino) {
        return FsError::NotFound;
    }

    auto dir_it = fs.directories.find(*parent_ino);
    if (dir_it == fs.directories.end()) {
        return FsError::NotDirectory;
    }

    auto entry_it = dir_it->second.find(basename);
    if (entry_it == dir_it->second.end()) {
        return FsError::NotFound;
    }

    InodeId child_ino = entry_it->second;

    FsError err = metadata_log::flush_dirent_tombstone_record(
        fs,
        *parent_ino,
        child_ino
    );

    if (err != FsError::Ok) {
        return err;
    }

    dir_it->second.erase(entry_it);

    Inode* parent_inode = fs.inode_table.get(*parent_ino);
    if (parent_inode) {
        parent_inode->touch();
        err = metadata_log::flush_inode_record(fs, *parent_inode);
        if (err != FsError::Ok) {
            return err;
        }
    }

    std::cout << "[T10] rm " << path << " (ino=" << child_ino << ")\n";

    return FsError::Ok;
}


FsError fs_rename(FileSystemState& fs,
                  const std::string& from,
                  const std::string& to)
{
    if (from == to) {
        return FsError::Ok;
    }

    std::filesystem::path p_from(from);
    std::filesystem::path p_to(to);

    std::string dir_from = p_from.parent_path().string();
    std::string name_from = p_from.filename().string();

    std::string dir_to = p_to.parent_path().string();
    std::string name_to = p_to.filename().string();

    auto parent_from_ino = fs_lookup(fs, dir_from.empty() ? "/" : dir_from);
    if (!parent_from_ino) {
        return FsError::NotFound;
    }

    auto parent_to_ino = fs_lookup(fs, dir_to.empty() ? "/" : dir_to);
    if (!parent_to_ino) {
        return FsError::NotFound;
    }

    auto& entries_from = fs.directories[*parent_from_ino];
    auto& entries_to = fs.directories[*parent_to_ino];

    auto old_it = entries_from.find(name_from);
    if (old_it == entries_from.end()) {
        return FsError::NotFound;
    }

    if (entries_to.count(name_to)) {
        return FsError::AlreadyExists;
    }

    InodeId child_ino = old_it->second;

    FsError err = metadata_log::flush_dirent_tombstone_record(
        fs,
        *parent_from_ino,
        child_ino
    );

    if (err != FsError::Ok) {
        return err;
    }

    err = metadata_log::flush_dirent_record(
        fs,
        *parent_to_ino,
        child_ino,
        name_to
    );

    if (err != FsError::Ok) {
        return err;
    }

    entries_from.erase(old_it);
    entries_to[name_to] = child_ino;

    Inode* parent_from = fs.inode_table.get(*parent_from_ino);
    if (parent_from) {
        parent_from->touch();
        metadata_log::flush_inode_record(fs, *parent_from);
    }

    Inode* parent_to = fs.inode_table.get(*parent_to_ino);
    if (parent_to && *parent_to_ino != *parent_from_ino) {
        parent_to->touch();
        metadata_log::flush_inode_record(fs, *parent_to);
    }

    std::cout << "[T10] rename " << from << " -> " << to << "\n";

    return FsError::Ok;
}


FsError fs_truncate(FileSystemState& fs, const std::string& path, size_t new_size)
{
    std::string data;

    FsError err = fs_read(fs, path, data);
    if (err != FsError::Ok) {
        return err;
    }

    data.resize(new_size, '\0');

    return fs_write(fs, path, data);
}


FsError fs_append_record(FileSystemState& fs,
                         RecordType type,
                         uint64_t key,
                         uint32_t owner_inode,
                         uint32_t logical_block_index,
                         const std::byte* payload,
                         std::size_t payload_size,
                         LogAddress& out_addr)
{
    if (fs.segment_io == nullptr || fs.allocator == nullptr) {
        return FsError::Internal;
    }

    if (auto_gc_enabled() && !g_inside_auto_gc) {
        const uint32_t min_free_segments = auto_gc_min_free_segments();

        if (fs.allocator->has_low_free_space(min_free_segments)) {
            g_inside_auto_gc = true;

            GcStats stats{};
            FsError gc_err = fs_gc_once(fs, stats);

            g_inside_auto_gc = false;

            if (gc_err != FsError::Ok) {
                return gc_err;
            }

            if (stats.cleaned_segments > 0) {
                std::cout << "[AUTO-GC] cleaned_segments="
                          << stats.cleaned_segments
                          << ", moved_records="
                          << stats.moved_records
                          << ", moved_data_records="
                          << stats.moved_data_records
                          << ", skipped_records="
                          << stats.skipped_records
                          << "\n";
            }
        }
    }

    RecordHeader hdr{};

    FsError err = fs.segment_io->append_record(
        *fs.allocator,
        type,
        key,
        owner_inode,
        logical_block_index,
        payload,
        payload_size,
        out_addr,
        &hdr
    );

    if (err != FsError::Ok) {
        return err;
    }

    fs.latest_records[key] = out_addr;

    if (fs.sync_each_write) {
        err = fs.segment_io->flush();
        if (err != FsError::Ok) {
            return err;
        }
    }

    return FsError::Ok;
}


FsError fs_read_record(const FileSystemState& fs,
                       const LogAddress& addr,
                       RecordHeader& out_header,
                       std::vector<std::byte>& out_payload) {
    if (fs.segment_io == nullptr) {
        return FsError::Internal;
    }
    return fs.segment_io->read_record(addr, out_header, out_payload);
}

FsError fs_flush(FileSystemState& fs) {
    if (fs.segment_io == nullptr) {
        return FsError::Internal;
    }
    FsError err = metadata_log::write_checkpoint_record(fs);
    if (err != FsError::Ok) {
        return err;
    }
    err = fs.segment_io->flush();
    if (err != FsError::Ok) {
        return err;
    }
    return FsError::Ok;
}
