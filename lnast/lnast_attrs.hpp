//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The per-node source attributes that used to live here — `lnast.loc`
// (pos1/pos2/line/tok struct) and `lnast.fname` (an uninterned per-node
// std::string path) — are gone. Source provenance is one uint64
// SourceId per def-bearing node (hhds::attrs::srcid), resolved through the
// Lnast's hhds::Source_locator (hhds::Source_locator).
//
// Node names ride on a per-Lnast interned int32 id (lnast_attrs::lnast_name)
// resolved through the Lnast's Lnast_name_pool — NOT hhds::attrs::name, which
// stays the std::string LGraph instance name. The id is a SIGNED index into
// the pool's string deque: 0 = empty, positive = an ordinary (interned,
// deduped) name, negative = a `%`-prefixed SSA temp (so Lnast::is_tmp is a
// sign test, not a prefix scan). Both signs index the same pooled string
// (resolve = pool[abs(id)]); the id is in-memory only — export_into
// materializes hhds::attrs::name strings on the clone and adopt re-interns
// them, so the on-disk `ln:` format is unchanged.

#include <cstdint>
#include <string>

#include "hhds/attr.hpp"
#include "hhds/attrs/srcid.hpp"

namespace lnast_attrs {

struct lnast_name_t {
  using value_type = int32_t;
  using storage    = hhds::flat_storage;
};

inline constexpr lnast_name_t lnast_name{};

}  // namespace lnast_attrs

namespace hhds {

template <>
[[nodiscard]] inline std::string attr_tag_name<lnast_attrs::lnast_name_t>() {
  return "lnast_attrs::lnast_name";
}

}  // namespace hhds
