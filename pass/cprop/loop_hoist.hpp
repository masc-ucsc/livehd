// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Loop-invariant code motion over COMPACT loops.
//
// A compact loop is one descriptor-bearing Sub whose body definition runs once
// per ordinal. Its inputs fall in two classes: the ordinal, the activation and
// every carry-in change between iterations; everything else arrives the same
// for every iteration (hhds calls that binding `invariant_external`). A body
// computation that depends only on the second class and on constants has the
// same value in every iteration, yet the rolled form computes it `count` times
// per evaluation -- in the simulator once per call, in synthesis once per
// instantiated copy.
//
// This moves such a computation OUT of the body: the body definition gains one
// input port per hoisted value (`__hoist<k>`), every loop instance computes the
// value once in its parent (a clone of the cone, fed from what the parent
// already drives into the instance's invariant inputs) and drives the new port,
// and the body reads the port where it used to compute. The compact form is
// preserved exactly: the descriptor, the carries, the trip count and the
// per-ordinal body are untouched; only the body/parent boundary moves.
//
// It is a library-wide rewrite (a body and ALL its parents change together), so
// it runs serially after the per-graph cprop. A body also instantiated as an
// ordinary (non-loop) Sub is left alone: for that call every input may vary.
// Hoisting never changes a simulated or synthesized value.

#include <cstddef>
#include <memory>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::cprop {

struct Loop_hoist_stats {
  size_t bodies         = 0;  // compact-loop body definitions that changed
  size_t hoisted_values = 0;  // new body inputs (one per hoisted frontier value)
  size_t removed_nodes  = 0;  // body nodes that became dead and were deleted
  size_t instances      = 0;  // loop instances that received cloned cones
};

// Hoist over every compact loop whose body AND parents are in `graphs`.
// Idempotent: a second call finds nothing to move.
Loop_hoist_stats hoist_loop_invariants(const std::vector<std::shared_ptr<hhds::Graph>>& graphs);

}  // namespace livehd::cprop
