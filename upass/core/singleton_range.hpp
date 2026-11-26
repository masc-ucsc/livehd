// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "lnast.hpp"

namespace upass {

// Unlike the last value range published for a name, this fact bounds every
// read of a declaration or a captured loop invariant. Keep it under a non-identifier key so persisted
// bitwidth metadata retains the proof independently of flow-sensitive ranges.
inline std::string singleton_range_key(std::string_view name) { return absl::StrCat("#decl_singleton:", name); }

inline void set_singleton_range(Lnast& ln, std::string_view name, int64_t value) {
  ln.bw_meta().ranges[singleton_range_key(name)] = {.min = value, .max = value, .unbounded = false};
}

inline std::optional<int64_t> singleton_range(const Lnast& ln, std::string_view name) {
  const auto& ranges = ln.bw_meta().ranges;
  const auto  it     = ranges.find(singleton_range_key(name));
  return it == ranges.end() ? std::nullopt : std::optional<int64_t>{it->second.min};
}

}  // namespace upass
