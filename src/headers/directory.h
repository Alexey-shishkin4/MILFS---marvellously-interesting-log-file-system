#pragma once

#include <string>
#include <unordered_map>

#include "inode.h"

struct Dirent {
  std::string name;
  InodeId inode_id;
};

using DirectoryEntries = std::unordered_map<std::string, InodeId>;
