// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Loop-index ports of a lifted loop body (upass/README.md "Rolled loops").
//
// An input port of a rolled loop's lifted definition that holds ONE value per
// loop occurrence: its own index, an enclosing loop's index, or an enclosing
// UNROLLED loop's index promoted to a port so the occurrences of one body share
// the definition. Each becomes a constant wherever a consumer expands the loop
// (synthesis, small-loop simulation), so upass.tolg lowers a bit position
// computed only from these into a form that folds to a constant-position slice
// once they are known. The marker rides the tree's bw_meta under a key no
// identifier can spell (`#`), so every path that persists bw_meta (compile
// cache, ln: manifest) keeps it with the body it describes; every bw_meta
// reader looks names up, so the key is invisible to them.

#include <string>
#include <string_view>

#include "absl/strings/str_cat.h"
#include "lnast.hpp"

namespace upass {

[[nodiscard]] inline std::string loop_index_key(std::string_view port) { return absl::StrCat("#loop_index:", port); }

inline void mark_loop_index_port(Lnast& ln, std::string_view port) {
  ln.bw_meta().ranges.insert_or_assign(loop_index_key(port), BitwidthEntry{.min = 0, .max = 0, .unbounded = false});
}

[[nodiscard]] inline bool is_loop_index_port(const Lnast& ln, std::string_view port) {
  return ln.bw_meta().ranges.contains(loop_index_key(port));
}

}  // namespace upass
