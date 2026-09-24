// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Internals shared by the selector proofs (satopt.cpp) and the mux-fact
// sweep (satopt_mux.cpp): the proof cut, mux arms, the exact source key, the
// cache file helpers and the rewrite. The rejection filter is Word_sim
// (satopt_sim.hpp).
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "attr_carry.hpp"
#include "dlop.hpp"
#include "hhds/graph.hpp"
#include "mask_eval.hpp"
#include "node_util.hpp"
#include "prove.hpp"
#include "satopt.hpp"
#include "satopt_sim.hpp"

namespace livehd::satopt::detail {
// The simulation every stage filters with: the budget's pattern count and a
// fixed seed, so a run is reproducible across builds.
inline Word_sim::Options sim_options(const Budget& budget, bool descend = false, bool reject_unstamped = false) {
  return {.samples = budget.samples, .descend = descend, .reject_unstamped = reject_unstamped, .salt = 0x7361746f7074ULL};
}
// The cvc5 queries every stage asks: combinational (memory outputs are free
// words), no unknown constants, the budget's per-query limits and a model on
// refutation for the simulation. The rlimit scales with cone PINS, but a word
// query bit-blasts every bit of them: the floor lets a small arithmetic cone
// (a few adders) finish. Easy queries stop long before it.
inline formal::Prove_options prove_options(const Budget& budget, bool descend, bool reject_unstamped) {
  return {.budget_k                 = budget.budget_k,
          .min_rlimit               = static_cast<long long>(budget.budget_k) * 4096,
          .cone_max                 = budget.cone_max,
          .memory_as_symbols        = true,
          .reject_unknown_constants = true,
          .descend_subs             = descend,
          .reject_unstamped         = reject_unstamped,
          .produce_model            = true};
}
namespace gu = livehd::graph_util;
using Pin    = hhds::Pin_class;
using Node   = hhds::Node_class;
struct Unsupported {};

inline bool cut(const Pin& p) {
  if (gu::is_graph_input_pin(p)) {
    return true;
  }
  auto op = gu::type_op_of(p.get_master_node());
  return op == Ntype_op::Flop || op == Ntype_op::Memory || op == Ntype_op::Sub || op == Ntype_op::Fflop || op == Ntype_op::Latch;
}
inline int width(const Pin& p) {
  return p.is_const() ? std::max(1, static_cast<int>(gu::const_of(p).get_signed_bits())) : std::max(1, gu::bits_of(p));
}


struct Arms {
  std::vector<Pin> controls, values;
  bool             hot = false;
};
inline Arms arms_of(const Node& n) {
  Arms a;
  a.hot = gu::type_op_of(n) == Ntype_op::Hotmux;
  if (a.hot) {
    auto h = gu::hotmux_inputs(n);
    for (auto [c, v] : h.arms) {
      a.controls.push_back(c);
      a.values.push_back(v);
    }
    if (!h.fallback.is_invalid()) {
      a.controls.emplace_back();
      a.values.push_back(h.fallback);
    }
  } else {
    std::map<int, Pin> inputs;
    for (const auto& in_pin : n.inp_sorted_pins()) {
      const auto in_drv            = in_pin.get_driver_pin();
      inputs[in_pin.get_port_id()] = in_drv;
    }
    // Two-arm Mux uses a nonzero condition; larger indexed muxes are left to ABC.
    if (inputs.size() == 3 && inputs.contains(0) && inputs.contains(1) && inputs.contains(2)) {
      a.controls = {inputs.at(0), inputs.at(0)};
      a.values   = {inputs.at(1), inputs.at(2)};
    }
  }
  return a;
}

// The exact translation input, not a digest oracle. IDs are intentionally part
// of this descriptor: a harmless renumbering misses rather than attaching a
// theorem to a different node. State and opaque outputs remain independent
// cuts. `salt` is the code identity of the prover that uses the key.
std::string source_key(hhds::Graph* graph, bool colors, uint64_t salt);
// `<dir>/<hash of name><suffix>.json`, or empty without a directory.
std::string cache_path(std::string_view dir, std::string_view name, std::string_view suffix = {});
// Write through a private temporary file and rename it into place.
void        write_atomic(const std::string& path, const std::string& text);

// Builds a replacement value next to `mux` (any cell: its color and source
// location carry over) from slices, constant lanes, complements, a Concat of
// lanes and a signed read-back.
struct Arm_builder {
  hhds::Graph& g;
  Node         mux;
  Node         node(Ntype_op op) {
    auto n = gu::create_typed_node(g, op);
    if (gu::has_color(mux)) {
      gu::set_color(n, gu::color_of(mux));
    }
    gu::carry_srcid(mux, n);
    return n;
  }
  Pin output(Node n, int width) {
    auto p = n.create_driver_pin(0);
    gu::set_ubits(p, width);
    return p;
  }
  Pin ones(int width) { return gu::create_const(g, *Dlop::get_mask_value(width)); }
  Pin slice(const Pin& value, int lo, int hi) {
    auto n = gu::create_get_mask(g, value, lo, hi);
    if (gu::has_color(mux)) {
      gu::set_color(n, gu::color_of(mux));
    }
    gu::carry_srcid(mux, n);
    return output(n, hi - lo);
  }
  Pin complement(const Pin& value, int lo, int hi) {
    auto n = node(Ntype_op::Xor);
    slice(value, lo, hi).connect_sink(gu::setup_sink_pid(n, 0));  // a banked op: each call appends an operand
    ones(hi - lo).connect_sink(gu::setup_sink_pid(n, 0));
    return output(n, hi - lo);
  }
  // `value`'s low `width` bits read as a signed number.
  Pin sext(const Pin& value, int width) {
    auto n = node(Ntype_op::Sext);
    value.connect_sink(n.create_sink_pin(0));
    gu::create_const(g, *Dlop::create_integer(width)).connect_sink(n.create_sink_pin(1));
    auto p = n.create_driver_pin(0);
    gu::set_sbits(p, width);
    return p;
  }
  // lanes are least significant first: (value, width).
  Pin concat(const std::vector<std::pair<Pin, int>>& lanes, int width) {
    auto n = node(Ntype_op::Concat);
    for (size_t i = 0; i < lanes.size(); ++i) {
      const auto& [value, w] = lanes[lanes.size() - 1 - i];
      value.connect_sink(gu::setup_sink_pid(n, static_cast<hhds::Port_id>(2 * i)));
      gu::create_const(g, *Dlop::create_integer(w)).connect_sink(gu::setup_sink_pid(n, static_cast<hhds::Port_id>(2 * i + 1)));
    }
    return output(n, width);
  }
};

// Rewrites against a proven selector. Tying a pin to the constant it always
// carries is exact; zeroing an arm is exact because no input selects it.
// Every replaced driver is recorded so its cone can be swept afterwards.
struct Select_rewrite {
  hhds::Graph&     g;
  std::vector<Pin> released;
  // Shared profile: a Hotmux that loses its consumer is kept -- its controls
  // are the `unique if` exclusivity obligation, observable without its data.
  bool              keep_obligations = false;
  uint64_t          removed          = 0;  // nodes the sweeps deleted
  std::vector<Node> deleted_nodes = {};    // ... and which (a caller invalidating its caches reads them)
  std::vector<Pin>  deleted_pins  = {};    // their driver pins, read before the delete
  void             tie(const Node& n, hhds::Port_id pid, int64_t value) {
    const auto sink = n.create_sink_pin(pid);
    for (const auto& d : sink.get_driver_pins()) {
      if (d.is_const() && gu::const_of(d).is_known_eq(*Dlop::create_integer(value))) {
        return;
      }
      released.push_back(d);
    }
    gu::drop_drivers(sink);
    gu::create_const(g, *Dlop::create_integer(value)).connect_sink(sink);
  }
  // Rewire one sink to an equivalent replacement (an arm rebuilt from proven
  // per-bit facts). The replaced driver is released for the sweep.
  void replace(const Node& n, hhds::Port_id pid, const Pin& replacement) {
    const auto sink = n.create_sink_pin(pid);
    for (const auto& d : sink.get_driver_pins()) {
      if (d == replacement) {
        return;
      }
      released.push_back(d);
    }
    gu::drop_drivers(sink);
    replacement.connect_sink(sink);
  }
  // Only logic that lost its last consumer HERE is deleted: a cell that was
  // already dangling is not this pass's business.
  void sweep() {
    absl::flat_hash_set<Node> deleted;
    // Every released driver pin of a cell: once its edges are gone the cell
    // lists no driver pins, so these are the ones a cache may still hold.
    absl::flat_hash_map<Node, std::vector<Pin>> pins_of;
    while (!released.empty()) {
      const auto p = released.back();
      released.pop_back();
      if (p.is_invalid() || p.is_const() || gu::is_graph_input_pin(p) || gu::is_graph_output_pin(p)) {
        continue;
      }
      const auto n = p.get_master_node();
      if (deleted.contains(n) || n.is_invalid() || gu::is_builtin_node(n)) {
        continue;
      }
      pins_of[n].push_back(p);
      const auto op = gu::type_op_of(n);
      if (!Ntype::is_comb(op) || op == Ntype_op::Clock_cell || n.has_out_edges()
          || (keep_obligations && op == Ntype_op::Hotmux)) {
        continue;
      }
      for (const auto& in_pin : n.inp_sorted_pins()) {
        for (const auto& d : in_pin.get_driver_pins()) {
          released.push_back(d);
        }
      }
      deleted.insert(n);
      deleted_nodes.push_back(n);
      const auto& outs = pins_of[n];
      deleted_pins.insert(deleted_pins.end(), outs.begin(), outs.end());
      ++removed;
      n.del_node();
    }
  }
};
}  // namespace livehd::satopt::detail
