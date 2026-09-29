// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The value sweep (todo/livehd/2s-satopt D, F). Word-level simulation
// (satopt_sim.hpp) nominates operation outputs that are constant, equal to a
// value computed earlier, or its complement; one word-level query proves each
// nomination and every proven one becomes an unconditional rewrite: the output's
// consumers read the constant, the earlier value, or a Not of it. A refuted
// nomination's counterexample becomes one more simulated pattern.
//
// Targets are operation cells only (F): Sum, Mult, Div, And, Or, Xor, Not, Ror,
// Rxor, Popcount, EQ, LT, GT, SHL, SRA and the two-arm Mux and Hotmux. Wiring
// (Get_mask, Set_mask, Concat, Sext), constants, graph inputs and state or
// instance outputs are never replaced, but any of them may be the earlier
// value another output is replaced with. The earlier value comes first in
// topological order, so no rewrite can close a loop, and the rewrites are
// proven on the unmodified graph but hold together because each keeps every
// value's function.
//
// The odc kind (G) replaces an output whose fanout window (one or two levels,
// every edge that leaves the window an exit, every Hotmux control in it too)
// never observes its value with a constant, or reads only its low bits when
// its top bits are never observed. Such a rewrite changes the output's
// function, so each is proven and applied alone.
//
// The resub kind (J, experimental) re-expresses a one-bit output whose private
// cone is larger than one gate as And/Or/Xor (or And with one side negated) of
// two one-bit values from its bounded fanin window outside that cone:
// nominated by exact simulated columns, proven equal, unconditional only.
#include <memory>
#include <string_view>
#include <vector>

#include "cell.hpp"
#include "hhds/graph.hpp"
#include "satopt_stages.hpp"

namespace livehd::satopt {

enum class Sweep : uint8_t {
  constants,   // an output that always carries one value
  equiv,       // an output always equal to an earlier value (same width and sign)
  complement,  // an output always the complement of an earlier value, when a Not is cheaper
  odc,         // an output whose value no window exit observes: a constant (or its top bits zero) instead (G)
  resub,       // a one-bit output equal to one gate over two earlier values, when that is cheaper (J)
};

// Is `op` a cell whose output the sweep may replace (F)?
[[nodiscard]] bool sweep_target(Ntype_op op);

// Runs one kind over `graphs` in place, adding its counts to `report`. The
// odc kind is contextual: each rewrite is proven on the graph the previous
// ones left and applied at once, never batched.

void sweep_values(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Sweep kind, Profile profile,
                  std::string_view cache_dir, Stage_report& report, Meter& meter);

}  // namespace livehd::satopt
