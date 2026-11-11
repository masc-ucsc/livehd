// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

// Interconnect of the fast timing engine (todo 2q-fasttimer, step D): the Elmore solution of one RC tree,
// with the same moments OpenTimer's `Rct` computes, so the two can be compared on the same RC.
//
// A tree is nodes with a capacitance (fF, pin capacitances already included by the caller) and edges with a
// resistance (Ω). The root is the driver. For every node the solver gives the Elmore delay
//   delta(n) = sum over the edges e on the path root->n of R(e) * C_down(e)
// and the second moment beta(n) = sum of R(e) * sum_{k below e} C(k) * delta(k), whose impulse variance
// 2*beta - delta^2 is what a sink adds to the slew in quadrature (slew_out^2 = slew_in^2 + impulse).
// Units: Ω * fF = 1e-3 ps; delays come out in ps, impulses in ps^2.
//
// The solver owns scratch storage that is reused from tree to tree, so a pass over all nets allocates
// only while the largest net grows.
namespace livehd::timing::fast {

struct Wire_edge {
  uint32_t from = 0, to = 0;
  float    resistance_ohm = 0;
};

class Elmore {
public:
  // `edges` must form a tree over [0, cap_ff.size()) that is connected to `root`. Returns false (and leaves
  // the results zero) when it does not: a node unreachable from the root, or a cycle.
  bool solve(std::span<const float> cap_ff, std::span<const Wire_edge> edges, uint32_t root = 0);

  [[nodiscard]] float total_cap_ff() const { return load_.empty() ? 0.0f : load_[root_]; }
  [[nodiscard]] float load_ff(uint32_t node) const { return load_[node]; }
  [[nodiscard]] float delay_ps(uint32_t node) const { return delay_[node]; }
  [[nodiscard]] float impulse_ps2(uint32_t node) const { return impulse_[node]; }

private:
  uint32_t              root_ = 0;
  std::vector<uint32_t> start_, adjacent_edge_, order_, parent_, parent_edge_;
  std::vector<float>    load_, delay_, ldelay_, beta_, impulse_, parent_res_;
};

}  // namespace livehd::timing::fast
