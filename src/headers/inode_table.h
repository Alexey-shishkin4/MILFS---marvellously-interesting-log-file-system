#pragma once

#include <optional>
#include <unordered_map>

#include "inode.h"

class InodeTable {
 public:
  using Container = std::unordered_map<InodeId, Inode>;

  // Вставляет новый inode. Возвращает false, если идентификатор уже существует.
  bool insert(const Inode& inode);

  // Получает указатель на inode или nullptr, если он не найден.
  Inode* get(InodeId id);
  const Inode* get(InodeId id) const;

  Inode& operator[](InodeId id) { return table_[id]; }

  Container::const_iterator begin() const noexcept { return table_.begin(); }
  Container::const_iterator end() const noexcept { return table_.end(); }

 private:
  Container table_;
};
