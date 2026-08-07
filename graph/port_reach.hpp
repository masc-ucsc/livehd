//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Per-definition COMBINATIONAL port reachability: for each OUTPUT port of a
// definition, the set of INPUT ports with a purely combinational path to it.
//
// This is the summary FIRRTL's CheckCombLoops keeps per module ("DrivenBys"):
// computed once per DEFINITION, bottom-up over the instance DAG, and spliced
// in at every instance site — so a consumer can reason about through-instance
// dependencies in time linear in the sum of definition sizes, without ever
// flattening. The sim emitter's planned per-output-cone callee partitioning
// (todo_sim_pipeline.md step 5) consumes it twice over:
//
//   * caller side — a Sub instance's output `o` is schedulable as soon as the
//     inputs in `support(o)` are bound; an output with EMPTY support is a pure
//     function of the callee's state (the Moore / state-only-prebind cases are
//     the degenerate rows of this table);
//   * callee side — the distinct support masks are the seed of the
//     output-dependence COLORS the partition split groups by.
//
// Boundary rules (kept in lockstep with inou/cgen's simgen-7 classifiers):
//   Flop/Fflop/Latch   cut — q is last period's value.
//   Memory             JOINS via every sink cone: an async read's dout is comb
//                      in its address, and under write-forwarding orderings in
//                      the write cones too. Conservative for sync reads and
//                      read-first orderings (their douts are input-independent)
//                      — that direction only over-reports a dependence, which a
//                      scheduler answers with a later, still-correct order.
//   Sub                splices the CALLEE's summary at the boundary; a
//                      body-less blackbox conservatively depends on ALL of the
//                      instance's connected inputs.

#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "hhds/graph.hpp"

namespace livehd::port_reach {

// One SLICE of a packed output: the bit range a single concat leaf drives,
// with ITS OWN input support. Slices exist only when the output's driver is
// the disjoint `Or`-of-`SHL` concat idiom (or a pass-through of a sliced
// callee output) — the shape a packed struct port lowers to. They are what
// lets the partitioner refine to BIT level when a port-grain cycle demands it
// (2026-08-06 ruling: refine only under loop pressure — slices with identical
// supports merge back to the port grain downstream).
struct In_atom {
  uint32_t pid = 0;
  uint32_t lo  = 0;
  uint32_t len = 0;  // len == 0: the WHOLE input port
};
struct Out_slice {
  uint32_t             lo  = 0;
  uint32_t             len = 0;
  hhds::Pin_class      leaf;  // driver of this range: a concat leaf, or a sliced callee's output pin
  std::vector<In_atom> ins;   // sorted by (pid, lo, len), deduped
  // How `leaf` carries this range. false (a concat leaf): the value is
  // LSB-aligned, read it as-is. true (a pass-through of a sliced callee
  // output): `leaf` is the WHOLE bundle pin and the range must be shifted out
  // of it. Decided HERE, per slice, because only this pass knows which arm
  // produced the slice — a consumer cannot tell the two apart by inspecting
  // the leaves (a concat that replicates one instance output into two fields
  // looks identical to a pass-through).
  bool shifted = false;
};

struct Def_reach {
  // output port_id -> input port_ids with a comb path to it. An output with no
  // entry (or an empty set) has NO combinational input dependence: it is a
  // pure function of the definition's own state and constants.
  absl::flat_hash_map<uint32_t, absl::flat_hash_set<uint32_t>> out2ins;
  // output port_id -> its slice decomposition; absent = not decomposable
  // (consumers then use the port-level out2ins row).
  absl::flat_hash_map<uint32_t, std::vector<Out_slice>>        out_slices;

  [[nodiscard]] bool input_independent(uint32_t out_pid) const {
    auto it = out2ins.find(out_pid);
    return it == out2ins.end() || it->second.empty();
  }
  [[nodiscard]] bool all_input_independent() const {
    for (const auto& [o, ins] : out2ins) {
      if (!ins.empty()) {
        return false;
      }
    }
    return true;
  }
};

// Memoized per-definition summaries. One Cache per analysis run; summaries are
// computed on first request and reused for every instance of the same def.
// Not thread-safe (matches the emitter's single-threaded use).
class Cache {
public:
  // A null graph yields the empty summary, which reads as "no comb
  // dependence" — a caller holding a body-less BLACKBOX instance must apply
  // its own conservative rule (depend on everything) instead of asking here.
  const Def_reach& of(const std::shared_ptr<hhds::Graph>& g);

private:
  // node_hash_map: summaries are handed out by reference and must stay put
  // while later queries insert (a flat map moves values on rehash).
  absl::node_hash_map<const hhds::Graph*, Def_reach> memo_;
  absl::flat_hash_set<const hhds::Graph*>            busy_;  // recursion guard
};

}  // namespace livehd::port_reach
