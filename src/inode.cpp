#include "headers/inode.h"

#include <optional>
#include <stdexcept>

Inode::Inode(InodeId id, InodeType type, InodeId parent)
    : id_(id),
      type_(type),
      size_bytes_(0),
      created_at_(std::time(nullptr)),
      modified_at_(created_at_),
      parent_(parent),
      children_() {  
  block_pointers_.fill(std::nullopt);
}

void Inode::touch() noexcept { modified_at_ = std::time(nullptr); }

void Inode::set_block_pointer(std::size_t index,
                              std::optional<std::uint32_t> block_id) {
  if (index >= kNumDirectBlocks) {
    throw std::out_of_range("block index out of range");
  }
  block_pointers_[index] = block_id;
  touch();
}
