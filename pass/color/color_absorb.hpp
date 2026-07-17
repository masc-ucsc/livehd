// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// ABSORB: fold every def that is smaller than the size window's floor into the
// parents that instantiate it (todo/livehd/2c-color-size.html R3).
//
// Why this exists. The size window works inside one def, because a coloring does:
// colors live per def, and pass.partition emits one module per (def, color). So a
// def whose ENTIRE body is under min has no neighbour to merge with -- the window
// can only report it (Size_window_stats::left_under) and move on. XSCore has 1,146
// such defs, holding 1.2% of the design in 3,708 partitions. The only way to reach
// them is across the hierarchy boundary, and the only way across is to remove the
// boundary: a Sub is a BLACKBOX to ABC, so nothing short of inlining actually
// merges the logic with its neighbours.
//
// The cost is real and bounded: a def inlined at N sites is N copies of its logic
// (ABC maps each once per (def, region), so the dedup is lost). It is bounded by
// the threshold -- each copy is under min, ~1000 GE -- and the aggregate is what
// the acceptance run measures.

#include <cstdint>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::color {

struct Absorb_stats {
  uint64_t defs_absorbed = 0;   // distinct defs that were below min
  uint64_t sites_inlined = 0;   // instantiation sites folded (>= defs_absorbed)
  uint64_t ge_duplicated = 0;   // GE added by inlining a def at more than one site
};

// Inline every reachable bodied def whose TOTAL gate-equivalent weight (its own
// body plus everything it instantiates) is under `min_ge`, at every site under
// `top`. Children-first, so a def is only ever inlined once its own small
// children are already part of its body -- which is what lets the underlying
// transform stay single-level.
//
// `top` itself is never absorbed. Body-less defs (liberty cells, external IP,
// fproperty/lgassert markers) are skipped: there is nothing to inline, and they
// are black boxes to ABC by design.
//
// Absorbed defs are left in the library rather than deleted, for two reasons.
// One: hhds `delete_graph` invalidates the body immediately, and `Eprp_var::graphs`
// still holds a shared_ptr to it -- the pass would leave the kernel a live handle
// to a corpse to save. Two: a parent OUTSIDE `top`'s hierarchy could still hold a
// Sub bound to that gid, and nothing here can see it. Being unreachable is enough:
// pass.partition and pass.abc walk from `top` (resolve_order), so they never map
// an absorbed def. Only cgen, which emits every def in the library, still writes
// them out -- as dead, unreferenced modules.
//
// Returns false after a diagnostic if a def could not be inlined (the parent is
// then partially transformed -- treat as fatal, like flatten).
[[nodiscard]] bool absorb_small_defs(hhds::Graph* top, const absl::flat_hash_map<hhds::Gid, hhds::Graph*>& gid2graph,
                                     uint64_t min_ge, Absorb_stats* st);

}  // namespace livehd::color
