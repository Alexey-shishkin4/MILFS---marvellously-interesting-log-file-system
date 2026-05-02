#include "headers/inode_table.h"

bool InodeTable::insert(const Inode& inode) {
  bool inserted = table_.emplace(inode.id(), inode).second;
  return inserted;
}

Inode* InodeTable::get(InodeId id) {
  auto it = table_.find(id);
  if (it == table_.end()) {
    return nullptr;
  }
  return &it->second;
}

const Inode* InodeTable::get(InodeId id) const {
  auto it = table_.find(id);
  if (it == table_.end()) {
    return nullptr;
  }
  return &it->second;
}
