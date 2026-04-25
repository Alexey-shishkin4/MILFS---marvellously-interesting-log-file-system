#define FUSE_USE_VERSION 31
#include "headers/fs_fuse.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <unistd.h>

MilfsFuseState* g_milfs_state = nullptr;

static int to_errno(FsError err) {
    switch (err) {
        case FsError::Ok: return 0;
        case FsError::NotFound: return -ENOENT;
        case FsError::AlreadyExists: return -EEXIST;
        case FsError::NotDirectory: return -ENOTDIR;
        case FsError::IsDirectory: return -EISDIR;
        default: return -EIO;
    }
}

int milfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi) {
    (void)fi;
    std::memset(stbuf, 0, sizeof(struct stat));

    auto ino_opt = fs_lookup(g_milfs_state->fs, path);
    if (!ino_opt) return -ENOENT;

    Inode* inode = g_milfs_state->fs.inode_table.get(*ino_opt);
    if (!inode) return -ENOENT;

    stbuf->st_ino = static_cast<ino_t>(*ino_opt);
    stbuf->st_uid = getuid();
    stbuf->st_gid = getgid();
    stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime = std::time(nullptr);

    if (inode->type() == InodeType::Directory) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        stbuf->st_size = 0;
    } else {
        stbuf->st_mode = S_IFREG | 0644;
        stbuf->st_nlink = 1;
        stbuf->st_size = static_cast<off_t>(inode->size_bytes());
    }

    return 0;
}

int milfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
                  struct fuse_file_info* fi, enum fuse_readdir_flags flags) {
    (void)offset;
    (void)fi;
    (void)flags;

    auto ino_opt = fs_lookup(g_milfs_state->fs, path);
    if (!ino_opt) return -ENOENT;

    Inode* inode = g_milfs_state->fs.inode_table.get(*ino_opt);
    if (!inode) return -ENOENT;
    if (inode->type() != InodeType::Directory) return -ENOTDIR;

    filler(buf, ".", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));

    auto it = g_milfs_state->fs.directories.find(*ino_opt);
    if (it == g_milfs_state->fs.directories.end()) return 0;

    for (const auto& [name, child_ino] : it->second) {
        (void)child_ino;
        filler(buf, name.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
    }

    return 0;
}

int milfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void)fi;

    auto ino_opt = fs_lookup(g_milfs_state->fs, path);
    if (!ino_opt) return -ENOENT;

    Inode* inode = g_milfs_state->fs.inode_table.get(*ino_opt);
    if (!inode) return -ENOENT;
    if (inode->type() == InodeType::Directory) return -EISDIR;

    auto data_it = g_milfs_state->fs.file_data.find(*ino_opt);
    if (data_it == g_milfs_state->fs.file_data.end()) return 0;

    const std::string& data = data_it->second;

    if (offset < 0) return -EINVAL;
    if (static_cast<size_t>(offset) >= data.size()) return 0;

    size_t to_copy = std::min(size, data.size() - static_cast<size_t>(offset));
    std::memcpy(buf, data.data() + offset, to_copy);
    return static_cast<int>(to_copy);
}

int milfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
    (void)fi;

    auto ino_opt = fs_lookup(g_milfs_state->fs, path);
    if (!ino_opt) return -ENOENT;

    Inode* inode = g_milfs_state->fs.inode_table.get(*ino_opt);
    if (!inode) return -ENOENT;
    if (inode->type() == InodeType::Directory) return -EISDIR;
    if (offset < 0) return -EINVAL;

    std::string& data = g_milfs_state->fs.file_data[*ino_opt];
    size_t end_pos = static_cast<size_t>(offset) + size;
    if (data.size() < end_pos) {
        data.resize(end_pos, '\0');
    }

    std::memcpy(data.data() + offset, buf, size);
    inode->set_size_bytes(data.size());
    return static_cast<int>(size);
}

int milfs_mkdir(const char* path, mode_t mode) {
    (void)mode;
    return to_errno(fs_mkdir(g_milfs_state->fs, path));
}

int milfs_create(const char* path, mode_t mode, struct fuse_file_info* fi) {
    (void)mode;
    (void)fi;
    return to_errno(fs_create(g_milfs_state->fs, path));
}

int milfs_unlink(const char* path) {
    return to_errno(fs_remove(g_milfs_state->fs, path));
}

int milfs_rename(const char* from, const char* to, unsigned int flags) {
    if (flags != 0) return -EINVAL;
    return to_errno(fs_rename(g_milfs_state->fs, from, to));
}

int milfs_truncate(const char* path, off_t size, struct fuse_file_info* fi) {
    (void)fi;
    if (size < 0) return -EINVAL;
    return to_errno(fs_truncate(g_milfs_state->fs, path, static_cast<size_t>(size)));
}
