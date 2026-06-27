//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "bundle_key.hpp"
#include "lnast.hpp"

// bundle_path — turn a dotted LNAST name (one int32 name-id) into the per-segment
// id path the nested Bundle navigates by, with NO per-call string work after the
// first split of a given name.
//
// A Bundle field is addressed by a Seg (segment) id:
//   Seg > 0  named field   — the LNAST interner id of the segment substring
//                            (so `a.b.c` -> [id(a), id(b), id(c)]).
//   Seg < 0  unnamed field — a positional index p encoded as -(p+1)
//                            (so "0" -> -1, "1" -> -2, …; the bare-scalar leaf
//                            is the unnamed pos-0 slot, Seg == -1).
//   Seg == 0 invalid / empty path.
//
// The split respects backtick-quoted identifiers (a '.' inside `` `ar.x` `` is
// part of the name, not a separator) via bundle_key, and reuses the thread-local
// LNAST name pool (Lnast::active_name_pool) to intern named segments — so a
// bundle field id IS an LNAST name id (one shared id space). The split of a given
// name-id is stable (the name never changes), so it is cached forever per thread.
namespace bundle_path {

using Seg = int32_t;

[[nodiscard]] inline bool is_named(Seg s) noexcept { return s > 0; }
[[nodiscard]] inline bool is_unnamed(Seg s) noexcept { return s < 0; }
[[nodiscard]] inline int  unnamed_pos(Seg s) noexcept { return -s - 1; }     // s<0 -> position
[[nodiscard]] inline Seg  make_unnamed(int pos) noexcept { return -(pos + 1); }  // position -> s<0

// True when the segment substring is an unnamed decimal index ("0", "1", …).
[[nodiscard]] inline bool is_decimal_segment(std::string_view seg) noexcept {
  if (seg.empty()) {
    return false;
  }
  for (const char c : seg) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  return true;
}

namespace detail {
// One cache per thread = one per single-threaded compile (mirrors the name pool
// it draws ids from). Never cleared: a name-id's segmentation is immutable. The
// span returned points into the cached vector's heap buffer, which survives map
// rehash (vector move preserves the buffer pointer).
inline absl::flat_hash_map<int32_t, std::vector<Seg>>& seg_cache() {
  static thread_local absl::flat_hash_map<int32_t, std::vector<Seg>> cache;
  return cache;
}
}  // namespace detail

// Segment-id path for a whole dotted name-id. Empty span for name_id 0 / empty.
[[nodiscard]] inline std::span<const Seg> of_name(int32_t name_id) {
  if (name_id == 0) {
    return {};
  }
  auto& cache = detail::seg_cache();
  if (const auto it = cache.find(name_id); it != cache.end()) {
    return it->second;
  }
  const auto&      pool = Lnast::active_name_pool();
  std::string_view full = pool->resolve(name_id);
  std::vector<Seg> segs;
  std::string_view rest = full;
  while (!rest.empty()) {
    const std::string_view seg = bundle_key::get_first_level(rest);
    if (is_decimal_segment(seg)) {
      int pos = 0;
      for (const char c : seg) {
        pos = pos * 10 + (c - '0');
      }
      segs.push_back(make_unnamed(pos));
    } else {
      segs.push_back(pool->intern(seg));  // named: the segment's own pool id (>0)
    }
    rest = bundle_key::get_all_but_first_level(rest);  // "" when no more dots → loop ends
  }
  auto [ins, ok] = cache.emplace(name_id, std::move(segs));
  return ins->second;
}

// Segment-id path for a dotted string built at runtime (dynamic keys / the
// string-API adapters). Interns the whole string first, so it shares of_name's
// cache and id space.
[[nodiscard]] inline std::span<const Seg> of_string(std::string_view dotted) {
  if (dotted.empty()) {
    return {};
  }
  return of_name(Lnast::active_name_pool()->intern(dotted));
}

}  // namespace bundle_path
