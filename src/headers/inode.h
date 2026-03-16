#pragma once

#include <array>
#include <cstdint>
#include <ctime>
#include <optional>
#include <vector>

using InodeId = std::uint32_t;

inline constexpr std::size_t kNumDirectBlocks = 12;

enum class InodeType { File, Directory };

class Inode {
 public:
  Inode() = default;

  Inode(InodeId id, InodeType type, InodeId parent);

  InodeId id() const noexcept { return id_; }
  InodeType type() const noexcept { return type_; }
  InodeId parent() const { return parent_; }

  std::uint64_t size_bytes() const noexcept { return size_bytes_; }
  void set_size_bytes(std::uint64_t size) noexcept { size_bytes_ = size; }

  std::time_t created_at() const noexcept { return created_at_; }
  std::time_t modified_at() const noexcept { return modified_at_; }
  
  void touch() noexcept;

  const std::array<std::optional<std::uint32_t>, kNumDirectBlocks>&
  direct_blocks() const noexcept {
    return block_pointers_;
  }

  void add_child(InodeId child) { children_.push_back(child); }

  void set_block_pointer(std::size_t index,
                         std::optional<std::uint32_t> block_id);

 private:
  InodeId id_{0};
  InodeType type_{InodeType::File};
  std::uint64_t size_bytes_{0};
  std::time_t created_at_{0};
  std::time_t modified_at_{0};

  InodeId parent_;
  std::vector<InodeId> children_;

  std::array<std::optional<std::uint32_t>, kNumDirectBlocks> block_pointers_{};
};
