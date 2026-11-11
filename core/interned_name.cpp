// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "interned_name.hpp"

#include <array>
#include <deque>
#include <mutex>
#include <shared_mutex>

#include "absl/container/flat_hash_set.h"

namespace livehd {
namespace {
// The index holds pointers to the stored texts and is searched by content.
struct Text_hash {
  using is_transparent = void;
  size_t operator()(std::string_view text) const noexcept { return absl::Hash<std::string_view>{}(text); }
  size_t operator()(const std::string* text) const noexcept { return absl::Hash<std::string_view>{}(*text); }
};
struct Text_equal {
  using is_transparent = void;
  bool operator()(const std::string* a, const std::string* b) const noexcept { return a == b; }
  bool operator()(const std::string* a, std::string_view b) const noexcept { return *a == b; }
  bool operator()(std::string_view a, const std::string* b) const noexcept { return a == *b; }
};
// The table is split by hash so that threads that build names at the same time rarely meet. A std::deque
// never moves its elements, so a pointer to one stays valid while the table grows.
struct Shard {
  std::shared_mutex                                              mutex;
  absl::flat_hash_set<const std::string*, Text_hash, Text_equal> index;
  std::deque<std::string>                                        texts;
};
constexpr size_t shard_count = 64;

std::array<Shard, shard_count>& shards() {
  static auto* table = new std::array<Shard, shard_count>();  // never destroyed: names outlive every static
  return *table;
}
}  // namespace

const std::string& Name::empty_text() noexcept {
  static const auto* empty = new std::string();
  return *empty;
}

Name Pin_label::bare_marker() noexcept {
  static const auto* marker = new std::string();  // its address is the mark; it is in no table
  return Name(marker);
}

const std::string* Name::intern(std::string_view text) {
  if (text.empty()) {
    return &empty_text();
  }
  // The shard comes from a different hash than the one the index uses, so one shard's index keeps its spread.
  auto& shard = shards()[std::hash<std::string_view>{}(text) % shard_count];
  {
    std::shared_lock lock(shard.mutex);
    if (const auto it = shard.index.find(text); it != shard.index.end()) {
      return *it;
    }
  }
  std::unique_lock lock(shard.mutex);
  if (const auto it = shard.index.find(text); it != shard.index.end()) {
    return *it;
  }
  const std::string* stored = &shard.texts.emplace_back(text);
  shard.index.insert(stored);
  return stored;
}
}  // namespace livehd
