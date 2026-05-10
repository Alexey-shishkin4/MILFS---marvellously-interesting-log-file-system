#pragma once

#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>
#include <sys/stat.h>
#include "fs_core.h"

struct MilfsFuseState {
    FileSystemState fs;
};

extern MilfsFuseState* g_milfs_state;

int milfs_getattr(const char* path, struct stat* stbuf, struct fuse_file_info* fi);
int milfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t offset,
                  struct fuse_file_info* fi, enum fuse_readdir_flags flags);
int milfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
int milfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi);
int milfs_mkdir(const char* path, mode_t mode);
int milfs_create(const char* path, mode_t mode, struct fuse_file_info* fi);
int milfs_unlink(const char* path);
int milfs_rename(const char* from, const char* to, unsigned int flags);
int milfs_truncate(const char* path, off_t size, struct fuse_file_info* fi);
void milfs_destroy(void* private_data);
int milfs_flush(const char* path, struct fuse_file_info* fi);
int milfs_fsync(const char* path, int datasync, struct fuse_file_info* fi);
int milfs_utimens(const char* path,
                  const struct timespec tv[2],
                  struct fuse_file_info* fi);
