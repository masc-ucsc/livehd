//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Dotted-key string helpers shared by Bundle (upass/core) and the few
// lnast-side consumers (lnast_builder). Pure functions over the canonical
// key encoding — first segment is a bare name, a decimal index ("0", "1",
// …), or an attribute name; segments are separated by '.'. No storage
// access; Bundle's static methods of the same names delegate here (task
// 1b/A: Bundle moved to upass/core, lnast keeps only this header so the
// dependency arrow stays lnast <- upass).

#include <cctype>
#include <string>
#include <string_view>

#include "iassert.hpp"
#include "str_tools.hpp"

namespace bundle_key {

inline bool is_single_level(std::string_view key) { return key.find('.') == std::string::npos; }

inline std::string_view get_first_level(std::string_view key) {
  auto dot_pos = key.find('.');
  if (dot_pos == std::string::npos) {
    return key;
  }
  return key.substr(0, dot_pos);
}

inline std::string_view get_first_level_name(std::string_view key) {
  // Canonical keys: no `:N:` prefix to strip. First-level "name" is the
  // first segment (including decimal indices for unnamed entries — callers
  // that need to distinguish named-vs-unnamed use get_first_level_pos or
  // check the first character).
  return get_first_level(key);
}

inline std::string_view get_last_level(std::string_view key) {
  auto n = key.rfind('.');
  if (n == std::string::npos) {
    return key;
  }
  I(n != 0);  // name can not start with a .
  return key.substr(n + 1);
}

inline std::string_view get_all_but_last_level(std::string_view key) {
  auto n = key.rfind('.');
  if (n != std::string::npos) {
    return key.substr(0, n);
  }
  return std::string_view("");
}

inline std::string_view get_all_but_first_level(std::string_view key) {
  auto n = key.find('.');
  if (n != std::string::npos) {
    return key.substr(n + 1);
  }
  return std::string_view("");  // empty if no dot left
}

inline bool match(std::string_view a, std::string_view b) {
  // Canonical keys: equality reduces to plain string compare.
  return a == b;
}

inline size_t match_first_partial(std::string_view a, std::string_view b) {
  // a is a "partial prefix match" of b iff a == b (full match — return
  // a.size()) or b starts with a followed by a `.` segment separator
  // (prefix match — return a.size()+1 to skip the dot). Otherwise 0.
  if (a == b) {
    return a.size();
  }
  if (b.size() > a.size() && b[a.size()] == '.' && b.starts_with(a)) {
    return a.size() + 1;
  }
  return 0;
}

inline std::string normalize_key(std::string_view key) {
  // Post-PR3 (bundle_sorted plan §8): legacy `:N:name` / `:N:` producers
  // have all been migrated to canonical form. Any `:` at the start of a
  // key segment is an error — surface it via I() so a regression is loud.
#ifndef NDEBUG
  size_t scan_start = 0;
  while (scan_start <= key.size()) {
    auto end = key.find('.', scan_start);
    if (end == std::string_view::npos) {
      end = key.size();
    }
    auto seg = key.substr(scan_start, end - scan_start);
    I(seg.empty() || seg.front() != ':', "legacy :N: key encoding reached Bundle; producer not migrated");
    if (end == key.size()) {
      break;
    }
    scan_start = end + 1;
  }
#endif
  return std::string(key);
}

}  // namespace bundle_key
