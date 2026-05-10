#define FUSE_USE_VERSION 31
#include <fuse3/fuse.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "headers/fs_fuse.h"

static struct fuse_operations milfs_ops = {};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <mountpoint> [FUSE options]\n";
        return 1;
    }

    g_milfs_state = new MilfsFuseState{};
    fs_init(g_milfs_state->fs); 

    milfs_ops.getattr = milfs_getattr;
    milfs_ops.readdir = milfs_readdir;
    milfs_ops.read = milfs_read;
    milfs_ops.write = milfs_write;
    milfs_ops.mkdir = milfs_mkdir;
    milfs_ops.create = milfs_create;
    milfs_ops.unlink = milfs_unlink;
    milfs_ops.rename = milfs_rename;
    milfs_ops.truncate = milfs_truncate;
    milfs_ops.flush = milfs_flush;
    milfs_ops.fsync = milfs_fsync;
    milfs_ops.destroy = milfs_destroy;
    milfs_ops.utimens = milfs_utimens;

    return fuse_main(argc, argv, &milfs_ops, nullptr);
}
