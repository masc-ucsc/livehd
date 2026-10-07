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
//   Memory             port-accurate (Memory_deps below): an async read's dout
//                      is comb in its address/enable, and under
//                      write-forwarding orderings in the write cones too; a
//                      sync read or read-first ordering is input-independent.
//                      A port it cannot classify joins every sink cone (only
//                      over-reports, which a scheduler answers with a later,
//                      still-correct order).
//   Sub                splices the CALLEE's summary at the boundary; a
//                      body-less blackbox conservatively depends on ALL of the
//                      instance's connected inputs.

#include <functional>
#include <memory>
#include <optional>

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
// (refine only under loop pressure — slices with identical
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

// A callee summary supplied by the client instead of a walk of the callee
// body: a stored one (see stamped() below), or a conservative one for a body
// the client cannot trust. nullopt walks the callee body as usual.
using Callee_reach = std::function<std::optional<Def_reach>(const std::shared_ptr<hhds::Graph>&)>;

// One memory cell's ports, decoded once: which drivers each OUTPUT pin
// depends on COMBINATIONALLY. An async read's dout reaches that read port's
// address/enable (plus the write cones under same-cycle forwarding or on an
// unclocked array); a sync read is a register; read_all reaches the write
// cones only when unclocked. Shared by the summaries below and pass.legalize's
// loop check, so both use one memory model.
class Memory_deps {
public:
  explicit Memory_deps(const hhds::Node_class& mem);
  // Calls `enqueue` with each comb driver of output `out_pid`; false when the
  // pin is not a decoded port (callers then join every sink). `writes` false
  // drops the write cones a forwarding or unclocked memory adds to a read
  // (and the readall arm): an over-approximation a scheduler can afford, but
  // every consumer sequences those writes after the read, so a loop check
  // must not report them -- only an async read's own address/enable is.
  bool deps(hhds::Port_id out_pid, const std::function<void(const hhds::Pin_class&)>& enqueue, bool writes = true) const;

private:
  struct Port {
    hhds::Pin_class addr, en, din;
    bool            rd = false;
  };
  std::vector<Port>            pv;
  std::vector<hhds::Pin_class> wr_cones;
  hhds::Pin_class              update;
  bool                         has_clock   = false;
  bool                         fwd_nonzero = false;
  int                          mtype       = 2;
  int                          n_wr        = 0;
};

// Memoized per-definition summaries. One Cache per analysis run; summaries are
// computed on first request and reused for every instance of the same def.
// Not thread-safe (matches the emitter's single-threaded use).
class Cache {
public:
  Cache() = default;
  // `callee` answers first at every instance splice inside a walked body (and
  // in callee_of), never for the graph handed to of() itself. `slices` false
  // computes the out2ins rows only (a wide packed output otherwise costs one
  // walk per slice).
  explicit Cache(Callee_reach callee, bool slices = true) : callee_(std::move(callee)), slices_(slices) {}

  // A null graph yields the empty summary, which reads as "no comb
  // dependence" — a caller holding a body-less BLACKBOX instance must apply
  // its own conservative rule (depend on everything) instead of asking here.
  const Def_reach& of(const std::shared_ptr<hhds::Graph>& g);

  // The summary an INSTANCE of `g` splices: the Callee_reach answer when it
  // gives one, else of(g).
  const Def_reach& callee_of(const std::shared_ptr<hhds::Graph>& g);

private:
  Callee_reach callee_;
  bool         slices_ = true;

  // node_hash_map: summaries are handed out by reference and must stay put
  // while later queries insert (a flat map moves values on rehash).
  absl::node_hash_map<const hhds::Graph*, Def_reach> memo_;
  absl::node_hash_map<const hhds::Graph*, Def_reach> supplied_;  // Callee_reach answers
  absl::flat_hash_set<const hhds::Graph*>            busy_;      // recursion guard
};

// The out2ins rows of `r`, recorded on g's input node (attrs::comb_reach), and
// read back: nullopt when `g` carries none. Slices are not recorded.
void                     stamp(const hhds::Graph& g, const Def_reach& r);
std::optional<Def_reach> stamped(const hhds::Graph& g);

// Every output of `g` depending on every input: the summary of a body that
// cannot be read.
Def_reach crossbar(const hhds::Graph& g);

}  // namespace livehd::port_reach
