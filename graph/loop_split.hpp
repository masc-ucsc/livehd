// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Compact-loop carry classification -- the shared analysis behind "treat the
// parallel part and the induction part differently".
//
// A compact `Subnode_loop` exposes no per-lane output: `occurrence_materialize`
// binds external readers to replica `count-1`. So a loop that is SEMANTICALLY
// parallel still threads its per-lane results through a CARRY -- typically
// `acc = set_mask(acc, lane(index), f(index, invariants))`, each lane writing a
// disjoint slice and reading nothing it wrote. A naive "has a self-edge =>
// induction" test therefore classifies essentially every loop as induction and
// buys nothing. Separating those carries from genuine recurrences is the whole
// point of this file.
//
// WHAT EACH CONSUMER DOES WITH IT (why the distinction is worth computing):
//   LEC    parallel  -> prove the body once with a SYMBOLIC index; it lifts to
//                       every count, and a count mismatch compares lane sets.
//          reduction -> log-depth obligation; an IDEMPOTENT reduction is
//                       count-independent, which is what makes two loops with
//                       different counts legitimately equal.
//          induction -> induct over the ordinal; virtual unroll as fallback.
//   SYNTH  parallel  -> KEEP the boundary: map once, instantiate N. Lanes are
//                       independent, so no cross-lane optimization is lost, and
//                       changing the count re-maps nothing.
//          induction -> REMOVE the boundary: unroll into the AIG so ABC can turn
//                       a ripple chain into a prefix/carry-lookahead structure.
//   SIM    needs no distinction for correctness (it is a schedule); the split
//          only enables vectorizing the parallel lanes.
//
// THIS IS AN ANALYSIS. It writes nothing -- no attribute, no node -- so it runs
// on a frozen graph and cannot drift from what it describes.
//
// CONSERVATIVE BY CONSTRUCTION. `induction` is always a correct answer (it is
// just the slow one), so every shape this cannot prove falls back to it. The one
// place that is subtle is disjointness: whether two lanes' slices overlap is a
// VALUE question, not a structural one, so this file reports the SHAPE and
// leaves the side condition to the consumer -- see Carry_class::needs_disjoint_proof.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "hhds/graph.hpp"

namespace livehd::graph_util {

enum class Carry_kind : uint8_t {
  disjoint_slice,   // per-lane write into its own slice: PARALLEL despite the self-edge
  assoc_reduction,  // carry_out = op(carry_in, X), op associative, X carry-independent
  induction,        // a genuine recurrence -- the conservative default
};

struct Carry_class {
  hhds::Port_id in_port  = 0;
  hhds::Port_id out_port = 0;
  Carry_kind    kind     = Carry_kind::induction;

  // assoc_reduction only.
  Ntype_op reduce_op  = Ntype_op::Invalid;
  bool     idempotent = false;  // Or/And/min/max: applying it twice changes nothing,
                                // so the loop is COUNT-INDEPENDENT once saturated

  // disjoint_slice only. The shape held (a Set_mask of the carry at a position
  // that reaches the loop INDEX and no carry -- its own or a sibling's), but
  // whether two lanes' windows actually overlap depends on the mask VALUES,
  // which no structural walk can decide for a symbolic index. The consumer must
  // discharge it -- for LEC that is a one-line query (mask(i) & mask(j) == 0
  // for i != j); a consumer that cannot discharge it must treat the carry as
  // `induction`.
  bool needs_disjoint_proof = false;

  [[nodiscard]] bool is_parallel() const { return kind == Carry_kind::disjoint_slice; }
};

struct Loop_split {
  bool                     valid = false;  // false: not a compact loop, or no body
  std::vector<Carry_class> carries;

  // Callee-body nodes on a carry_in -> carry_out path for a carry classified
  // `induction` (a genuine recurrence). A reduction's cone is NOT in here: it
  // is still a recurrence, but one a consumer can re-associate, so it is
  // reported through `carries` rather than as an induction node set.
  absl::flat_hash_set<hhds::Node_class> induction_nodes;
  std::size_t                           body_nodes = 0;

  [[nodiscard]] std::size_t parallel_nodes() const {
    return body_nodes >= induction_nodes.size() ? body_nodes - induction_nodes.size() : 0;
  }
  // What fraction of the body is a genuine recurrence. The design premise is
  // that this is SMALL; report it so that premise can be measured rather than
  // assumed.
  [[nodiscard]] double induction_ratio() const {
    return body_nodes == 0 ? 0.0 : static_cast<double>(induction_nodes.size()) / static_cast<double>(body_nodes);
  }
  // Every carry is a per-lane slice write. Derived from the carries themselves
  // rather than from `induction_nodes`: a reduction adds no induction node, and
  // an undriven carry has no cone, yet neither is parallel.
  [[nodiscard]] bool fully_parallel() const {
    if (!valid) {
      return false;
    }
    for (const auto& c : carries) {
      if (!c.is_parallel()) {
        return false;
      }
    }
    return true;
  }
};

// Classify the carries of one compact-loop Sub. Returns `valid == false` when
// `loop_sub` is not a loop-bearing Sub or its callee has no body.
[[nodiscard]] Loop_split classify_loop(const hhds::Node_class& loop_sub);

// True for an op whose reduction re-associates (a+b)+c == a+(b+c).
[[nodiscard]] bool is_associative_op(Ntype_op op);
// True for an op where op(x, x) == x, so extra iterations cannot change a
// saturated value -- the reason two loops with DIFFERENT counts can be equal.
[[nodiscard]] bool is_idempotent_op(Ntype_op op);

}  // namespace livehd::graph_util
