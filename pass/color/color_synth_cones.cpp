// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// CONE-SEEDED synthesis coloring -- `pass.color synth --set synth_alg=cones`
// (todo/livehd/2c-color-synthcones.html).
//
// The shape, in one paragraph. Seed one BACKWARD cone per register data input
// and one per register enable (plus one per memory port, per stateful instance,
// per graph output and per large arithmetic node). Walk each cone backward,
// claiming every still-unowned node first-wins; when a cone reaches a node an
// earlier cone already owns it keeps walking THROUGH it and records the
// predicted size of that shared sub-cone as an overlap between the two colors.
// Then merge the most-overlapping color pairs, largest overlap first, while the
// union stays under `max_gate`. What survives is a partition whose boundaries
// fall where two register cones genuinely share little logic.
//
// Why this and not the forward `synth` walk. `synth` propagates one id along
// combinational fan-out and cuts at state, so the region shape is decided by
// connectivity alone and then RESHAPED afterwards by the GE size window
// (color_size.cpp) -- split the monsters, merge the singletons. Cones decides
// size while it decides shape, in a unit that tracks what ABC actually builds.
// Both algorithms are kept; `synth` remains the default and a --set selects.
//
// THE UNIT. Everything here is counted in predict_abc_size (a predicted
// generic-AIG score), never in nodes and never in synthesis GE. Wiring --
// Concat, constant masks, constant shifts, Sext, Not -- scores ZERO and so
// consumes no budget: that is deliberate (a zero-score node cannot be made
// cheaper by cutting in front of it), and its cost is that a wiring-dominated
// shared cone may be re-walked once per root. Measured and accepted; see the
// task's Risks section.
//
// THE ONE PERFORMANCE INVARIANT. hhds `inp_edges()` is eager and scans a node's
// WHOLE edge slot list including its out-edges (graph.cpp inp_edges_local, the
// `vid & 2` filter). Calling it from inside the per-cone loop on a 100k-fanout
// reset/enable driver is the quadratic re-scan that cost `pass.opentimer`
// minutes per def. So the fan-in (and the fan-out derived from it) is
// materialized ONCE into dense CSR arrays up front, and no phase after
// prepare() touches the graph's edges at all.

#include <algorithm>
#include <cstdint>
#include <print>
#include <queue>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "color_region_graph.hpp"
#include "color_synth.hpp"
#include "node_util.hpp"
#include "predict_abc_size.hpp"

namespace livehd::color {

namespace {

using livehd::graph_util::predict_abc_size;
using livehd::graph_util::type_op_of;

// Dense per-node index. `Graph::ref_node` indexes node_table by `nid >> 2`, so
// this is the storage slot itself -- the tightest key available and the reason
// every array below can be a plain vector instead of a hash map. Builtins
// (INPUT_NODE / OUTPUT_NODE / CONST_NODE) occupy 0..3; user nodes start at 4.
[[nodiscard]] inline uint32_t idx_of(const hhds::Node_class& n) {
  return static_cast<uint32_t>(n.get_debug_nid() >> 2);
}

constexpr uint8_t kPresent      = 1;    // a live node of this def's body
constexpr uint8_t kPart         = 2;    // is_partitionable
constexpr uint8_t kSeeded       = 4;    // a source-seeded (block-attr) node: a wall
constexpr uint8_t kLoopBreak    = 8;    // flop / memory / latch / stateful sub
constexpr uint8_t kArithCut     = 16;   // Mult/Div, Sum wider than 8: its own color
constexpr uint8_t kConstMaskGet = 32;   // Get_mask with a CONSTANT mask
constexpr uint8_t kRuntimeSra   = 64;   // SRA with a runtime amount (barrel)

// The register / memory sink pids this file decodes, spelled ONCE. Not asked of
// Ntype::get_sink_pid: that lookup's fast path derives the pid from the leading
// char for 'a'..'f' and then ASSERTS the name round-trips, so
// `get_sink_pid(Fflop, "enable")` -- a pin an Fflop does not have -- trips a
// debug assert instead of returning invalid (the same reason
// graph/predict_abc_size.hpp's `ctrl_pids` spells them out). Keep the two in
// lockstep with graph/cell.cpp.
constexpr uint32_t kPidDin    = 3;  // Flop/Latch/Fflop din, and a Memory port's din
constexpr uint32_t kPidEnable = 4;  // Flop/Latch enable, and a Memory port's enable (Fflop has none)
constexpr uint32_t kPidAddr   = 0;  // a Memory port's addr
// Memory whole-array write: `update` / `update_enable`, offsets inside one
// Memory_port_stride block rather than a per-port pin.
constexpr uint32_t kPidUpdate    = 12;
constexpr uint32_t kPidUpdateEn  = 13;

[[nodiscard]] constexpr bool is_mem_port_off(uint32_t off) { return off == kPidAddr || off == kPidDin || off == kPidEnable; }
[[nodiscard]] constexpr bool is_mem_whole_array_off(uint32_t off) { return off == kPidUpdate || off == kPidUpdateEn; }

// Phase-2 forward merge (todo/livehd/2c-color-synthcones.html, forward option).
// `pair` ranks one (register color, ONE consumer color) candidate at a time;
// `all` ranks the register against the WHOLE qualifying Q fanout at once and
// fires only if every one of those colors fits. Which is better is an open
// question the lhdsuite A/B answers -- hence both.
enum class Forward_mode : uint8_t { off, pair, all };

struct Cone_stats {
  uint64_t roots = 0, r_din = 0, r_en = 0, r_mem = 0, r_sub = 0, r_cut = 0, r_out = 0, r_sweep = 0;
  uint64_t truncated = 0;  // roots whose walk hit the max_gate budget
  uint64_t merges = 0, refused = 0;
  uint64_t max_pred = 0, over_max = 0;
  uint64_t fwd_merges = 0, fwd_refused = 0, fwd_max_chain = 0;
};

// One def's cone state. Every array is indexed by idx_of(); every phase after
// prepare() reads only these arrays.
struct Cones {
  hhds::Graph* g        = nullptr;
  uint64_t     max_gate = 0;
  Forward_mode forward  = Forward_mode::off;

  std::vector<uint8_t>  flag;
  std::vector<uint8_t>  op;  // Ntype_op, so no phase after prepare() needs a node handle
  std::vector<uint64_t> pred;
  std::vector<uint32_t> owner;  // color id, 0 = unowned
  std::vector<uint32_t> epoch;  // per-root visited stamp

  // Fan-in CSR, materialized from ONE inp_edges() call per node.
  std::vector<uint32_t> fin_start, fin_cnt, fin_drv, fin_pid;
  // Fan-out CSR, derived from the fan-in by counting sort -- so the walk's
  // "does this node still have an uncolored consumer?" and the SRA group's
  // one forward hop never call out_edges() either.
  std::vector<uint32_t> fout_start, fout_cnt, fout;

  // Body nodes in body().nodes(forward) order. Every later phase iterates THIS
  // rather than the graph, so root ids, sweep order and the renumber are one
  // deterministic function of the graph and never of hash iteration.
  std::vector<uint32_t> forward_idx;

  // Roots, flattened: root `c` (1-based, == its color id) seeds from
  // seed_pool[root_start[c-1] .. +root_cnt[c-1]) and starts its budget at
  // root_init[c-1].
  std::vector<uint32_t> seed_pool, root_start, root_cnt;
  std::vector<uint64_t> root_init;

  // Register node -> the R_din color minted for it, so A4 can hand the register
  // that id when its din driver is not an ordinary owned node.
  absl::flat_hash_map<uint32_t, uint32_t> din_color;

  // (min<<32)|max color pair -> predicted size of the sub-cone they share. A
  // ZERO overlap must stay absent, not present-with-0: "no entry" is what makes
  // a zero-overlap cone its own ABC block.
  absl::flat_hash_map<uint64_t, uint64_t> pair_w;

  uint32_t              cur_epoch = 0;
  std::vector<uint32_t> work;  // walk worklist, reused across roots
  Cone_stats            st;

  [[nodiscard]] bool traversable(uint32_t i) const {
    return i < flag.size() && (flag[i] & (kPresent | kPart | kSeeded)) == (kPresent | kPart);
  }

  // ---- roots -------------------------------------------------------------
  uint32_t begin_root(uint64_t init) {
    root_start.emplace_back(static_cast<uint32_t>(seed_pool.size()));
    root_cnt.emplace_back(0);
    root_init.emplace_back(init);
    ++st.roots;
    return static_cast<uint32_t>(root_start.size());  // color ids are 1-based
  }
  void push_seed(uint32_t drv) {
    if (traversable(drv)) {
      seed_pool.emplace_back(drv);
      ++root_cnt.back();
    }
  }

  void record_pair(uint32_t a, uint32_t b, uint64_t w) {
    if (a == 0 || b == 0 || a == b || w == 0) {
      return;
    }
    const uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
    auto&          acc = pair_w[key];
    acc                = graph_util::sat_add(acc, w);
  }

  // ---- A3: one root's backward walk --------------------------------------
  void walk_root(uint32_t c) {
    uint64_t traversed = root_init[c - 1];
    ++cur_epoch;
    const uint32_t start = root_start[c - 1];
    work.assign(seed_pool.begin() + start, seed_pool.begin() + start + root_cnt[c - 1]);

    while (!work.empty()) {
      const uint32_t n = work.back();
      work.pop_back();
      // The epoch stamp also covers compact-loop self edges and comb cycles, so
      // no separate cycle guard is needed.
      if (!traversable(n) || epoch[n] == cur_epoch) {
        continue;
      }
      epoch[n] = cur_epoch;

      if ((flag[n] & kLoopBreak) != 0) {
        continue;  // crossing a register/memory/stateful sub is not sharing logic
      }
      if ((flag[n] & kArithCut) != 0) {
        // Pre-owned by its own root. Record the CONTACT overlap so the merge can
        // still fold the adder into a consumer cone when it fits, then stop:
        // an arithmetic cut is never claimed by another walk.
        record_pair(owner[n], c, pred[n]);
        continue;
      }

      if (owner[n] == 0) {
        owner[n] = c;
        claim_sra_group(n, c);
      } else {
        record_pair(owner[n], c, pred[n]);  // the shared sub-cone, in predicted AIG
      }

      traversed = graph_util::sat_add(traversed, pred[n]);
      if (max_gate != 0 && traversed > max_gate) {
        // The popped node stays claimed and counted; nothing already queued is
        // visited after the crossing. Shared logic deeper than max_gate could
        // never be merged in anyway, so walking it only costs time.
        ++st.truncated;
        work.clear();
        break;
      }

      for (uint32_t k = fin_start[n]; k < fin_start[n] + fin_cnt[n]; ++k) {
        work.emplace_back(fin_drv[k]);
      }
    }
  }

  // pass.abc builds a runtime right shift's barrel only to the width its
  // constant-mask consumers actually demand, and ONLY while the two share a
  // region (graph/synthesis_cost.hpp, ColorSize.WideSraUsesNarrowSliceDemand).
  // Keep them together where ownership permits -- best effort: a consumer an
  // earlier root already claimed stays with that root, first-wins is not broken
  // for it, and the resulting under-prediction is recorded rather than fixed by
  // silent reassignment.
  void claim_sra_group(uint32_t n, uint32_t c) {
    if ((flag[n] & kRuntimeSra) == 0) {
      return;
    }
    for (uint32_t k = fout_start[n]; k < fout_start[n] + fout_cnt[n]; ++k) {
      const uint32_t m = fout[k];
      if (traversable(m) && (flag[m] & kConstMaskGet) != 0 && owner[m] == 0) {
        owner[m] = c;
      }
    }
  }

  [[nodiscard]] bool is_register(uint32_t i) const {
    const auto o = i < op.size() ? static_cast<Ntype_op>(op[i]) : Ntype_op::Invalid;
    return o == Ntype_op::Flop || o == Ntype_op::Latch || o == Ntype_op::Fflop;
  }

  // Is `m` a consumer of `n` that the FORWARD merge may follow? "merge forward
  // between Q and Din (not enable)": into a register, ONLY its `din` (pid 3)
  // qualifies -- enable, clock, reset, async and initial are the control pins
  // ruling 2 keeps out of the data cone, and following Q into one of them would
  // re-weld the data and control cones the backward walk just separated. Every
  // other consumer (ordinary logic, Memory, Sub, a graph output) qualifies on
  // any pin.
  [[nodiscard]] bool forward_edge_ok(uint32_t n, uint32_t m) const {
    if (!is_register(m)) {
      return true;
    }
    for (uint32_t k = fin_start[m]; k < fin_start[m] + fin_cnt[m]; ++k) {
      if (fin_drv[k] == n && fin_pid[k] == kPidDin) {
        return true;  // reached through din
      }
    }
    return false;
  }


  [[nodiscard]] bool has_unowned_consumer(uint32_t n) const {
    for (uint32_t k = fout_start[n]; k < fout_start[n] + fout_cnt[n]; ++k) {
      const uint32_t m = fout[k];
      if (traversable(m) && owner[m] == 0) {
        return true;
      }
    }
    return false;
  }
};

// ---------------------------------------------------------------------------
// A2 -- roots, in body().nodes(forward) first-encounter order
// ---------------------------------------------------------------------------
//
// Collected in ONE pass and walked in a SECOND. That split is load-bearing:
// forward order emits cut nodes first, so a flop is reached BEFORE the cone that
// feeds its din exists, and a walk started during the iteration would see an
// empty graph of owners.
void collect_roots(Cones& cn) {
  const auto stride = static_cast<uint32_t>(Ntype::Memory_port_stride);

  for (uint32_t i : cn.forward_idx) {
    if (!cn.traversable(i)) {
      continue;
    }
    // An arithmetic cut owns its color OUTRIGHT and is pre-owned before any walk
    // runs, so every cone that reaches it records contact overlap instead of
    // claiming it. Its own cone is its operand logic, and its budget starts
    // already charged with its own (large) score.
    if ((cn.flag[i] & kArithCut) != 0) {
      const uint32_t c = cn.begin_root(cn.pred[i]);
      ++cn.st.r_cut;
      cn.owner[i] = c;
      for (uint32_t k = cn.fin_start[i]; k < cn.fin_start[i] + cn.fin_cnt[i]; ++k) {
        cn.push_seed(cn.fin_drv[k]);
      }
      continue;
    }
    if ((cn.flag[i] & kLoopBreak) == 0) {
      continue;
    }

    const auto op = static_cast<Ntype_op>(cn.op[i]);
    if (op == Ntype_op::Flop || op == Ntype_op::Latch || op == Ntype_op::Fflop) {
      // `din` seeds one color and `enable` a SEPARATE one: welding a stage's
      // data cone to the stall cone that gates it is exactly what made the
      // forward algorithm put 99.4% of a flat dino in one region. Clock, reset,
      // async and initial never seed and are never colored through the flop --
      // their driver logic is picked up by the leftover sweep.
      //
      // ONE din root for the whole node regardless of Q width: v1 is
      // node-granular, and an LGraph node cannot carry more than one color.
      const uint32_t cdin = cn.begin_root(0);
      ++cn.st.r_din;
      cn.din_color[i] = cdin;
      uint32_t en_drv = 0;
      bool     has_en = false;
      for (uint32_t k = cn.fin_start[i]; k < cn.fin_start[i] + cn.fin_cnt[i]; ++k) {
        if (cn.fin_pid[k] == kPidDin) {
          cn.push_seed(cn.fin_drv[k]);
        } else if (cn.fin_pid[k] == kPidEnable) {  // Fflop has no enable pin, so this never fires for one
          en_drv = cn.fin_drv[k];
          has_en = true;
        }
      }
      if (has_en) {
        cn.begin_root(0);
        ++cn.st.r_en;
        cn.push_seed(en_drv);
      }
      continue;
    }

    if (op == Ntype_op::Memory) {
      // The Memory node itself is ONE hard-stop node with ONE anchor color; the
      // per-port roots describe the logic AROUND it. Ports are decoded from the
      // Memory_port_stride blocks: port p's addr/din/enable sit at raw pids
      // p*stride + {0,3,4}. The comptime pins (bits/size/rdport/fwd/...) and the
      // clock never participate.
      const uint32_t anchor = cn.begin_root(0);
      ++cn.st.r_mem;
      cn.owner[i] = anchor;

      // Ascending raw port index, so ids are a function of the cell layout.
      std::vector<uint32_t> ports;
      bool                  whole_array = false;
      for (uint32_t k = cn.fin_start[i]; k < cn.fin_start[i] + cn.fin_cnt[i]; ++k) {
        const uint32_t off = cn.fin_pid[k] % stride;
        if (is_mem_whole_array_off(off)) {  // update / update_enable: the whole-array write
          whole_array = true;
          continue;
        }
        if (is_mem_port_off(off)) {  // addr / din / enable
          ports.emplace_back(cn.fin_pid[k] / stride);
        }
      }
      std::sort(ports.begin(), ports.end());
      ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
      for (uint32_t p : ports) {
        cn.begin_root(0);
        ++cn.st.r_mem;
        for (uint32_t k = cn.fin_start[i]; k < cn.fin_start[i] + cn.fin_cnt[i]; ++k) {
          const uint32_t off = cn.fin_pid[k] % stride;
          if (cn.fin_pid[k] / stride == p && is_mem_port_off(off)) {
            cn.push_seed(cn.fin_drv[k]);
          }
        }
      }
      if (whole_array) {
        cn.begin_root(0);
        ++cn.st.r_mem;
        for (uint32_t k = cn.fin_start[i]; k < cn.fin_start[i] + cn.fin_cnt[i]; ++k) {
          const uint32_t off = cn.fin_pid[k] % stride;
          if (is_mem_whole_array_off(off)) {
            cn.push_seed(cn.fin_drv[k]);
          }
        }
      }
      continue;
    }

    // A STATEFUL Sub (is_loop_break after the set_subnode re-stamp): one anchor
    // color for the instance node, one root over its inputs. A COMBINATIONAL Sub
    // is not a loop break at all -- it is an ordinary 0-score pass-through node
    // every walk continues through, since its logic is weighed inside its own
    // def's cones.
    //
    // Every input driver seeds, clock included: a Sub's sink names come from the
    // child's GraphIO and there is no reliable clock predicate over them, while
    // a real clock driver is a graph input (not traversable) or a Clock_cell
    // whose small cone belongs with the instance anyway.
    const uint32_t anchor = cn.begin_root(0);
    ++cn.st.r_sub;
    cn.owner[i] = anchor;
    cn.begin_root(0);
    ++cn.st.r_sub;
    for (uint32_t k = cn.fin_start[i]; k < cn.fin_start[i] + cn.fin_cnt[i]; ++k) {
      cn.push_seed(cn.fin_drv[k]);
    }
  }

  // Graph outputs, ascending port id. They overlap and merge exactly like
  // register cones; without them, output-only glue would reach the totality
  // fallback as a pile of one-node colors.
  std::vector<std::pair<uint32_t, uint32_t>> outs;  // (port id, driver idx)
  for (const auto& e : cn.g->get_output_node().inp_edges()) {
    outs.emplace_back(static_cast<uint32_t>(e.sink.get_port_id()), idx_of(e.driver.get_master_node()));
  }
  std::sort(outs.begin(), outs.end());
  for (const auto& [pid, drv] : outs) {
    (void)pid;
    cn.begin_root(0);
    ++cn.st.r_out;
    cn.push_seed(drv);
  }
}

// ---------------------------------------------------------------------------
// A4 -- root placement and totality
// ---------------------------------------------------------------------------
void place_state_and_sweep(Cones& cn) {
  // "The stage is the logic plus the register it writes": a register joins the
  // color of its din cone when that cone is an ordinary owned node. A register
  // fed by a primary input, by another register or by an arithmetic cut is a
  // one-node color of its own (ruling 4: no floor, nothing is added to it).
  for (uint32_t i : cn.forward_idx) {
    if (!cn.traversable(i) || cn.owner[i] != 0) {
      continue;
    }
    auto it = cn.din_color.find(i);
    if (it == cn.din_color.end()) {
      continue;  // not a register (memories and stateful subs are anchored already)
    }
    uint32_t placed = it->second;
    for (uint32_t k = cn.fin_start[i]; k < cn.fin_start[i] + cn.fin_cnt[i]; ++k) {
      if (cn.fin_pid[k] != kPidDin) {
        continue;
      }
      const uint32_t d = cn.fin_drv[k];
      // "Owned NON-CUT node" in both senses of cut. A register fed by another
      // register, a memory, a stateful instance or an arithmetic cut is a
      // one-node color of its own -- joining it would weld two pipeline stages
      // through the very boundary the algorithm exists to place, and forward
      // order means whether the upstream register happened to be placed first
      // would decide it.
      if (cn.traversable(d) && cn.owner[d] != 0 && (cn.flag[d] & (kArithCut | kLoopBreak)) == 0) {
        placed = cn.owner[d];
      }
      break;
    }
    cn.owner[i] = placed;
  }

  // Leftover sweep: root every still-unowned node that no longer has an
  // uncolored consumer -- a SINK of the uncolored subgraph (reset/clock glue,
  // output glue nobody reached, dead logic). Rooting at the sinks and walking
  // backward keeps those leftovers in cones rather than in singletons.
  for (uint32_t i : cn.forward_idx) {
    if (!cn.traversable(i) || cn.owner[i] != 0 || cn.has_unowned_consumer(i)) {
      continue;
    }
    const uint32_t c = cn.begin_root(0);
    ++cn.st.r_sweep;
    cn.push_seed(i);
    cn.walk_root(c);
  }

  // Totality fallback. An isolated combinational cycle has no sink -- every
  // member has an unowned consumer -- so it is invisible to the sweep above.
  // pass.partition warns and pass.analyze flags a node left at NO_COLOR, so
  // finish the job in forward order.
  for (uint32_t i : cn.forward_idx) {
    if (!cn.traversable(i) || cn.owner[i] != 0) {
      continue;
    }
    const uint32_t c = cn.begin_root(0);
    ++cn.st.r_sweep;
    cn.push_seed(i);
    cn.walk_root(c);
  }
}

// ---------------------------------------------------------------------------
// A5 -- merge, most-overlapping pair first
// ---------------------------------------------------------------------------
//
// Sound because ownership is DISJOINT: size(A u B) = size(A) + size(B), and
// overlaps fold additively too (overlap(A u B, C) = overlap(A,C) + overlap(B,C)).
// That additivity plus "weights only grow" is what makes a refused pair refused
// forever, and therefore what makes a single pass over a priority queue a
// fixpoint rather than an approximation.
struct Heap_entry {
  uint64_t ov;
  uint32_t a, b;  // region ids, a < b
  uint32_t ver;
};

struct Heap_cmp {
  bool operator()(const Heap_entry& x, const Heap_entry& y) const {
    if (x.ov != y.ov) {
      return x.ov < y.ov;  // largest overlap first
    }
    if (x.a != y.a) {
      return x.a > y.a;  // ties: smallest id first, so the order is total
    }
    return x.b > y.b;
  }
};

// ---------------------------------------------------------------------------
// Phase 2 -- FORWARD merge across the register (the `forward` option)
// ---------------------------------------------------------------------------
//
// Runs AFTER the backward overlap merge, never before. That order is the whole
// point: the backward cones are what ABC optimizes, so they get first claim on
// the budget, and forward merging spends only what is left. Reversed, a greedy
// forward pass would eat the budget and the shared sub-cones the backward phase
// exists to fuse would stay split.
//
// It also lands exactly where it is needed. The backward phase only merges
// pairs with a recorded OVERLAP, so a color that shares no sub-cone never grows
// there -- and that is precisely the singleton tail: a register fed by a primary
// input or by another register seeds a root whose walk claims nothing, so it has
// no pair-map entry at all and ends up a one-node color.
//
// There is no overlap to rank by here, so candidates are ordered by SMALLEST
// COMBINED SIZE -- the exact dual of the backward phase's largest-overlap-first
// heap. Merging keeps going as long as something still fits, so a chain of
// stages packs until `max_gate` stops it.
//
// CAVEAT worth measuring: a plain shift-register flop predicts ZERO AIG
// (predict_abc_size charges a register only for folded enable/reset muxes), so
// for such a chain the cap never binds and the whole chain fuses into one
// weightless color. `fwd_max_chain` reports it.
void merge_forward(Cones& cn, Region_graph& rg, Int_union_find& cuf) {
  struct Cand {
    uint64_t key;  // combined predicted size if this candidate fires
    uint32_t reg;  // the register node index
    int      tgt;  // pair mode: the one consumer color; all mode: -1
  };
  struct Cmp {
    bool operator()(const Cand& x, const Cand& y) const {
      if (x.key != y.key) {
        return x.key > y.key;  // SMALLEST combined size first
      }
      if (x.reg != y.reg) {
        return x.reg > y.reg;  // deterministic: forward-order register, then color
      }
      return x.tgt > y.tgt;
    }
  };

  // The qualifying Q-fanout colors of `reg`, resolved to live regions and with
  // the register's own color removed. Recomputed on every pop: merges move ids.
  const auto targets = [&](uint32_t reg, std::vector<int>& out) {
    out.clear();
    const int self = rg.find(static_cast<int>(cn.owner[reg]) - 1);
    for (uint32_t k = cn.fout_start[reg]; k < cn.fout_start[reg] + cn.fout_cnt[reg]; ++k) {
      const uint32_t m = cn.fout[k];
      if (!cn.traversable(m) || cn.owner[m] == 0 || !cn.forward_edge_ok(reg, m)) {
        continue;
      }
      const int c = rg.find(static_cast<int>(cn.owner[m]) - 1);
      if (c != self && std::find(out.begin(), out.end(), c) == out.end()) {
        out.emplace_back(c);
      }
    }
    std::sort(out.begin(), out.end());
  };

  const auto combined = [&](uint32_t reg, const std::vector<int>& tg) {
    uint64_t w = rg.weight(rg.find(static_cast<int>(cn.owner[reg]) - 1));
    for (int c : tg) {
      w += rg.weight(c);
    }
    return w;
  };

  std::priority_queue<Cand, std::vector<Cand>, Cmp> heap;
  std::vector<int>                                  tg;
  for (uint32_t i : cn.forward_idx) {
    if (!cn.traversable(i) || cn.owner[i] == 0 || !cn.is_register(i)) {
      continue;
    }
    targets(i, tg);
    if (tg.empty()) {
      continue;
    }
    if (cn.forward == Forward_mode::all) {
      heap.push({combined(i, tg), i, -1});
    } else {
      for (int c : tg) {
        heap.push({rg.weight(rg.find(static_cast<int>(cn.owner[i]) - 1)) + rg.weight(c), i, c});
      }
    }
  }

  absl::flat_hash_map<int, uint64_t> chain;  // forward merges folded into each live color
  while (!heap.empty()) {
    const Cand e = heap.top();
    heap.pop();
    if (!cn.traversable(e.reg) || cn.owner[e.reg] == 0) {
      continue;
    }
    const int self = rg.find(static_cast<int>(cn.owner[e.reg]) - 1);

    // Rebuild the candidate against the CURRENT colors. Lazy re-keying: if a
    // merge elsewhere changed what this one would cost, push it back with the
    // new key instead of acting on a stale order. Weights only grow and are
    // bounded by max_gate, so this terminates.
    std::vector<int> now;
    uint64_t         key = 0;
    if (e.tgt < 0) {
      targets(e.reg, now);
      if (now.empty()) {
        continue;
      }
      key = combined(e.reg, now);
    } else {
      const int t = rg.find(e.tgt);
      if (t == self || !rg.alive(t)) {
        continue;
      }
      targets(e.reg, now);
      if (std::find(now.begin(), now.end(), t) == now.end()) {
        continue;  // no longer a qualifying neighbour
      }
      now.assign(1, t);
      key = rg.weight(self) + rg.weight(t);
    }
    if (key != e.key) {
      heap.push({key, e.reg, e.tgt});
      continue;
    }
    if (cn.max_gate != 0 && key > cn.max_gate) {
      ++cn.st.fwd_refused;
      continue;  // sizes only grow: this candidate can never fit later
    }

    int survivor = self;
    for (int t : now) {
      const int tt = rg.find(t);
      if (tt == survivor) {
        continue;  // an earlier target in this same candidate already absorbed it
      }
      const int a = survivor;  // capture BEFORE merge: rg.merge picks the survivor
      // Two LOOKUPS, never two `operator[]`s inside one expression: `chain[a]`
      // and `chain[tt]` both INSERT, the operands of `+` are unsequenced, and a
      // rehash from the second call dangles the reference the first returned.
      const auto     ia     = chain.find(a);
      const auto     itt    = chain.find(tt);
      const uint64_t folded = (ia == chain.end() ? 0 : ia->second) + (itt == chain.end() ? 0 : itt->second) + 1;
      chain.erase(a);
      chain.erase(tt);
      survivor        = rg.merge(a, tt);
      chain[survivor] = folded;
      cuf.merge(a + 1, tt + 1);  // union the two ORIGINAL cone-id classes
      ++cn.st.fwd_merges;
      cn.st.fwd_max_chain = std::max(cn.st.fwd_max_chain, folded);
    }
    // The register's color grew, so every candidate touching it is now stale;
    // lazy re-keying above catches them when they surface.
    targets(e.reg, now);
    if (!now.empty()) {
      if (cn.forward == Forward_mode::all) {
        heap.push({combined(e.reg, now), e.reg, -1});
      } else {
        for (int c : now) {
          heap.push({rg.weight(rg.find(static_cast<int>(cn.owner[e.reg]) - 1)) + rg.weight(c), e.reg, c});
        }
      }
    }
  }
}

void merge_colors(Cones& cn, uint32_t n_colors, Int_union_find& cuf) {
  // 0 means RAW cones, not an unlimited merge cap: without a threshold there is
  // nothing to merge under.
  if (cn.max_gate == 0 || n_colors == 0) {
    return;
  }

  std::vector<uint64_t> weight(n_colors, 0);
  for (uint32_t i = 0; i < cn.owner.size(); ++i) {
    if (cn.owner[i] != 0) {
      auto& w = weight[cn.owner[i] - 1];
      w       = graph_util::sat_add(w, cn.pred[i]);
    }
  }
  std::vector<absl::flat_hash_map<int, uint64_t>> adj(n_colors);
  for (const auto& [key, ov] : cn.pair_w) {
    const int a = static_cast<int>(key >> 32) - 1;
    const int b = static_cast<int>(key & 0xffffffffU) - 1;
    adj[a][b]   = ov;
    adj[b][a]   = ov;
  }
  Region_graph rg(std::move(weight), {}, std::move(adj));

  std::priority_queue<Heap_entry, std::vector<Heap_entry>, Heap_cmp> heap;
  absl::flat_hash_map<uint64_t, uint32_t>                            ver;
  const auto pair_key = [](uint32_t a, uint32_t b) { return (static_cast<uint64_t>(a) << 32) | b; };
  for (const auto& [key, ov] : cn.pair_w) {
    const uint32_t a = static_cast<uint32_t>(key >> 32) - 1;
    const uint32_t b = static_cast<uint32_t>(key & 0xffffffffU) - 1;
    heap.push({ov, a, b, 0});
  }

  std::vector<int> touched;
  while (!heap.empty()) {
    const Heap_entry e = heap.top();
    heap.pop();
    // Lazy invalidation: an entry is valid iff BOTH ids are still live roots and
    // its stored version matches. A merge re-pushes only the pairs whose overlap
    // it changed; a survivor's untouched pairs stay valid even though its size
    // grew, because the size test happens here, at pop time.
    if (rg.find(static_cast<int>(e.a)) != static_cast<int>(e.a) || rg.find(static_cast<int>(e.b)) != static_cast<int>(e.b)) {
      continue;
    }
    if (auto it = ver.find(pair_key(e.a, e.b)); (it == ver.end() ? 0U : it->second) != e.ver) {
      continue;
    }
    const int a = static_cast<int>(e.a);
    const int b = static_cast<int>(e.b);
    if (rg.weight(a) + rg.weight(b) > cn.max_gate) {
      // Discard for good. Sizes only ever grow, so this pair can never fit
      // later; re-pushing it after an unrelated merge would only re-refuse it.
      ++cn.st.refused;
      continue;
    }

    // Snapshot exactly what the merge is about to rewrite -- the DISSOLVED
    // side's adjacency. Snapshotting both sides instead would cost the hub's
    // degree on every merge into a reset/flush cone.
    const int d = rg.dissolved_side(a, b);
    touched.clear();
    touched.reserve(rg.neighbours(d).size());
    for (const auto& [nb, w] : rg.neighbours(d)) {
      (void)w;
      touched.emplace_back(nb);
    }

    const int s = rg.merge(a, b);
    cuf.merge(a + 1, b + 1);  // union the two ORIGINAL cone-id classes
    ++cn.st.merges;

    for (int nb_raw : touched) {
      const int nb = rg.find(nb_raw);
      if (nb == s || !rg.alive(nb)) {
        continue;
      }
      auto it = rg.neighbours(s).find(nb);
      if (it == rg.neighbours(s).end()) {
        continue;
      }
      const uint32_t lo = static_cast<uint32_t>(std::min(s, nb));
      const uint32_t hi = static_cast<uint32_t>(std::max(s, nb));
      const uint32_t v  = ++ver[pair_key(lo, hi)];
      heap.push({it->second, lo, hi, v});
    }
  }

  // Phase 2: only now, on whatever budget the backward cones left behind.
  if (cn.forward != Forward_mode::off) {
    merge_forward(cn, rg, cuf);
  }
}

}  // namespace

// ---------------------------------------------------------------------------

void Color_synth::label_cones(hhds::Graph* g) {
  Cones cn;
  cn.g        = g;
  cn.max_gate = opts.max_gate;
  cn.forward  = opts.forward == "pair" ? Forward_mode::pair : (opts.forward == "all" ? Forward_mode::all : Forward_mode::off);

  // ---- A1: preparation, one pass over the body ----------------------------
  uint32_t nmax = 4;
  uint64_t body = 0;
  for (auto n : g->body().nodes()) {
    nmax = std::max(nmax, idx_of(n));
    ++body;
  }
  const size_t n = static_cast<size_t>(nmax) + 1;
  cn.flag.assign(n, 0);
  cn.op.assign(n, static_cast<uint8_t>(Ntype_op::Invalid));
  cn.pred.assign(n, 0);
  cn.owner.assign(n, 0);
  cn.epoch.assign(n, 0);
  cn.fin_start.assign(n, 0);
  cn.fin_cnt.assign(n, 0);
  cn.fout_start.assign(n, 0);
  cn.fout_cnt.assign(n, 0);
  cn.forward_idx.reserve(body);

  for (auto node : g->body().nodes(hhds::Node_order::forward)) {
    const uint32_t i = idx_of(node);
    cn.forward_idx.emplace_back(i);
    uint8_t f = kPresent;
    if (is_partitionable(node)) {
      f |= kPart;
      if (is_seeded(node)) {
        f |= kSeeded;
      }
      if (node.is_loop_break()) {
        f |= kLoopBreak;
      } else if (Color_synth::is_arith_cut(node)) {
        f |= kArithCut;
      }
      const auto op = type_op_of(node);
      cn.op[i]      = static_cast<uint8_t>(op);
      if (op == Ntype_op::Get_mask && graph_util::get_driver_of_sink_name(node, "mask").is_const()) {
        f |= kConstMaskGet;
      } else if (op == Ntype_op::SRA && graph_util::shift_mux_count(node) != 0) {
        f |= kRuntimeSra;
      }
      cn.pred[i] = predict_abc_size(node);
    }
    cn.flag[i] = f;

    // The ONE inp_edges() call per node (see the file header).
    cn.fin_start[i] = static_cast<uint32_t>(cn.fin_drv.size());
    for (const auto& e : node.inp_edges()) {
      cn.fin_drv.emplace_back(idx_of(e.driver.get_master_node()));
      cn.fin_pid.emplace_back(static_cast<uint32_t>(e.sink.get_port_id()));
    }
    cn.fin_cnt[i] = static_cast<uint32_t>(cn.fin_drv.size()) - cn.fin_start[i];
  }

  // Fan-out, by counting sort over the fan-in pool. Deriving it costs O(E) and
  // saves every later phase an out_edges() view walk per node.
  for (uint32_t d : cn.fin_drv) {
    if (d < n) {
      ++cn.fout_cnt[d];
    }
  }
  {
    uint32_t acc = 0;
    for (size_t i = 0; i < n; ++i) {
      cn.fout_start[i]  = acc;
      acc              += cn.fout_cnt[i];
    }
    cn.fout.assign(acc, 0);
    std::vector<uint32_t> fill(cn.fout_start);
    for (uint32_t i : cn.forward_idx) {
      for (uint32_t k = cn.fin_start[i]; k < cn.fin_start[i] + cn.fin_cnt[i]; ++k) {
        const uint32_t d = cn.fin_drv[k];
        if (d < n) {
          cn.fout[fill[d]++] = i;
        }
      }
    }
  }

  // ---- A2/A3/A4 -----------------------------------------------------------
  collect_roots(cn);
  const uint32_t n_primary = static_cast<uint32_t>(cn.root_start.size());
  for (uint32_t c = 1; c <= n_primary; ++c) {
    cn.walk_root(c);
  }
  place_state_and_sweep(cn);

  // ---- A5: merge ----------------------------------------------------------
  Int_union_find cuf;
  merge_colors(cn, static_cast<uint32_t>(cn.root_start.size()), cuf);

  // ---- A6: finalize -------------------------------------------------------
  // Renumber 1..k in forward first-encounter order: deterministic ids the caller
  // can pin, and the same rule apply_size_window's final renumber uses.
  //
  // The ORDER comes from the forward_idx that A1 already captured, NOT from a
  // second ordered walk. `body().nodes(forward)` is a Kahn schedule over the
  // whole def; `body().nodes()` is a flat node_table scan and is far cheaper.
  // Only the id MINTING is order-sensitive, and that is a pure array pass here
  // -- the walk after it exists solely to turn indices back into the Node_class
  // keys apply_coloring wants, so it takes the fast order. Verified equivalent
  // in-process on all 171 minion defs: same nodes, same ids.
  absl::flat_hash_map<int, int> class2color;
  std::vector<int>              final_color(cn.owner.size(), 0);
  for (uint32_t i : cn.forward_idx) {
    if (!cn.traversable(i) || cn.owner[i] == 0) {
      continue;
    }
    const int cls = cuf.find(static_cast<int>(cn.owner[i]));
    auto      it  = class2color.find(cls);
    if (it == class2color.end()) {
      it = class2color.emplace(cls, static_cast<int>(class2color.size()) + 1).first;
    }
    final_color[i] = it->second;
  }
  flat_node2id.clear();
  flat_node2id.reserve(class2color.empty() ? 0 : cn.forward_idx.size());
  for (auto node : g->body().nodes()) {
    if (const int c = final_color[idx_of(node)]; c != 0) {
      flat_node2id[node] = c;
    }
  }

  Color_opts o = opts;
  // The renumber above already minted one dense id per merged class, and a cones
  // color is deliberately NOT a connected component (see "packed" below), so a
  // continuous split on top would shred it.
  o.continuous = false;
  // The GE size window does not shape cones: min_ge/max_ge keep their meaning
  // for absorb and for the `synth` algorithm, and max_gate replaces them here.
  const int n_colors = apply_coloring(g, flat_node2id, o, o.sizes);

  // Predicted size per WRITTEN color, for the --stats threshold summary. Read
  // back off the graph so the seeded-base shift apply_coloring applies is
  // accounted for exactly once, and only for nodes this algorithm owns.
  if (o.sizes != nullptr) {
    for (auto node : g->body().nodes()) {  // a sum per color: order cannot matter
      auto it = flat_node2id.find(node);
      if (it == flat_node2id.end()) {
        continue;
      }
      o.sizes->color_pred[graph_util::node_color_of(node)] += cn.pred[idx_of(node)];
    }
  }

  if (opts.verbose) {
    absl::flat_hash_map<int, uint64_t> class_pred;
    for (uint32_t i = 0; i < cn.owner.size(); ++i) {
      if (cn.owner[i] != 0) {
        class_pred[cuf.find(static_cast<int>(cn.owner[i]))] += cn.pred[i];
      }
    }
    for (const auto& [cls, p] : class_pred) {
      (void)cls;
      cn.st.max_pred = std::max(cn.st.max_pred, p);
      if (cn.max_gate != 0 && p > cn.max_gate) {
        ++cn.st.over_max;
      }
    }
    std::print(stderr,
               "[color.cones] {} roots {} (din {}, en {}, mem-port {}, sub {}, cut {}, out {}, sweep {}), truncated {}, "
               "pairs {}, merges {}, refused {} over cap, fwd {} merges ({} refused, max chain {}), "
               "colors {}, max pred {} ({} over max_gate {})\n",
               g->get_name(),
               cn.st.roots,
               cn.st.r_din,
               cn.st.r_en,
               cn.st.r_mem,
               cn.st.r_sub,
               cn.st.r_cut,
               cn.st.r_out,
               cn.st.r_sweep,
               cn.st.truncated,
               cn.pair_w.size(),
               cn.st.merges,
               cn.st.refused,
               cn.st.fwd_merges,
               cn.st.fwd_refused,
               cn.st.fwd_max_chain,
               n_colors,
               cn.st.max_pred,
               cn.st.over_max,
               cn.max_gate);
  }
}

}  // namespace livehd::color
