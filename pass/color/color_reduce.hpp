// This file is distributed under the BSD 3-Clause License. See LICENSE for
// details.
#pragma once

// pass.color `reduce`: find small combinational subgraphs that repeat across
// the design and EXTRACT each repeated pattern as one shared def instantiated
// at every occurrence -- fewer total nodes in the library, and one Verilog
// module where there used to be N copies of the same logic.
//
// Shape of the transform (per run):
//   1. MINE   every def once: decompose the eligible combinational nodes into
//              disjoint fanout-free cones (every node whose fanout all lands on
//              one eligible sink joins that sink's cone; everything else roots
//              its own). Cones are single-output by construction, which is what
//              makes the splice cycle-free: a path from the extracted output
//              back into a cone input would have been a cycle in the source.
//   2. BUCKET  cones by an alpha-blind structural digest (no names, no nids --
//              two independent 64-bit lanes, the abc_incr discipline). A
//              pattern is a bucket with >= min_count occurrences, across defs.
//   3. VERIFY  each occurrence against the bucket's representative with an
//              exact iterative isomorphism walk. The walk also DERIVES the
//              per-occurrence boundary correspondence, so any accepted stitch
//              is a witnessed isomorphism -- symmetric operands may be paired
//              arbitrarily because a wrong guess fails the walk and drops the
//              occurrence (refuse-not-guess). The digest only proposes.
//   4. SPLICE  build the pattern defs once, all of them, from the pristine
//              graphs (named pat_<digest32hex>, ports in canonical token-rank
//              order: i0..iK / o0..oM), then replace every occurrence in place
//              with an anonymous, uncolored Sub instance (color ids belong to
//              pass.partition's regions; pattern identity is the pat_* target
//              name). The enclosing def keeps its own name and every node
//              outside the cones -- there is no wrapper and no re-region.
//
// Combinational PRIMITIVES only: cone members exclude state, Sub instances
// (even comb-bodied ones -- the digest/walk do not fold the target module),
// and assert/assume-marked nodes, so no state name ever moves into a shared
// body (LEC pairs flops by name; comb name aliasing across shared sites is the
// accepted cost of dedup). Pattern defs themselves are never re-mined.

#include <cstdint>
#include <span>

#include "hhds/graph.hpp"

namespace livehd::color {

struct Reduce_opts {
  uint64_t min_count = 3;  // occurrences a pattern needs before extraction
  // 3, not 4: the canonical unrolled-loop residue is a compare/select/mask
  // TRIPLE (Eq -> Mux -> And); the text-profit guard rejects any small cone
  // whose instance would out-line its statements.
  uint64_t min_nodes = 3;  // smallest cone (in nodes) worth considering
  // Required PER-SITE Verilog line win (estimated statement+decl lines saved
  // minus the ports+2 lines an instance costs). 0 = guard off. Inert here by
  // the Color_opts convention: the shipped policy (1) lives on the pass.color
  // label, so a direct caller or unit test gets the raw algorithm.
  uint64_t min_win   = 0;
  bool     verbose   = false;
};

struct Reduce_stats {
  uint64_t defs_scanned        = 0;
  uint64_t defs_skipped_seeded = 0;  // block-attr seeded defs are left alone
  uint64_t cones               = 0;  // candidate cones meeting the floor
  uint64_t patterns            = 0;  // pattern defs extracted (or reused)
  uint64_t occurrences         = 0;  // sites replaced by a pattern instance
  uint64_t nodes_deleted       = 0;  // cone members + consts they orphaned
  uint64_t nodes_created       = 0;  // pattern-body nodes + instance Subs
  uint64_t verify_dropped      = 0;  // digest-equal cones the exact walk rejected
  uint64_t port_heavy_skipped  = 0;  // buckets skipped: interface dwarfs body
  uint64_t dup_edge_skipped    = 0;  // cones refused: parallel duplicate edge
  uint64_t reuse_refused       = 0;  // rerun buckets refused: fragile port binding
  uint64_t promoted_consts     = 0;  // const slots promoted to input ports
};

// Mine + extract over `defs` (each def visited once; buckets are global, so a
// pattern repeating across defs shares one body). Rewrites the graphs and
// their owning library IN PLACE -- copy the lgdb first if you need the
// original. Returns false only after a fatal diag (already emitted).
[[nodiscard]] bool color_reduce(std::span<hhds::Graph* const> defs, const Reduce_opts& opts, Reduce_stats* st);

}  // namespace livehd::color
