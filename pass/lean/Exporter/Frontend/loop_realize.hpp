// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <vector>

#include "hhds/graph.hpp"

namespace lean_export {

// A ROLLED Pyrope loop is one compact instance (a loop Sub: `is_loop_subnode`)
// that stands for every iteration of the loop, and its carry feeds itself. The
// front end leaves a loop rolled when `compile.unroll` is off, and ALWAYS when
// the loop has a run-time `break` or `continue`. The scan needs every iteration
// as its own logic, so it refuses such a node.
//
// This realizes the rolled loops of `graph` in a PRIVATE copy, in two steps per
// loop, repeated until the top holds no loop Sub:
//   1. graph_util::materialize_occurrence turns the compact instance into
//      `count` ordinary instances of the loop body: replica r takes the index
//      constant `first + r*step`, the activation chain
//      `active[r] = active[r-1] && next_active[r-1]`, and the carry
//      `active[r-1] ? carry_out[r-1] : carry_in[r-1]`; external readers bind to
//      the last replica (graph/occurrence_materialize.hpp).
//   2. graph_util::inline_sub_instance splices each replica's body into the top,
//      so the scan sees flat logic. A body that holds a loop of its own (a
//      nested loop) brings a new loop Sub into the top with the splice; the
//      next round realizes it.
//
// The source graphs are never edited: the design's definitions are copied into
// `scratch` first, and only the copy of `graph` is rewritten. A graph without a
// loop Sub is returned unchanged (no copy). The caller keeps `scratch` alive for
// the returned graph's lifetime. Throws std::runtime_error when a loop cannot
// be realized; the materializer and the inliner have already reported why.
std::shared_ptr<hhds::Graph> realize_rolled_loops_copy(const std::shared_ptr<hhds::Graph>& graph, hhds::GraphLibrary& scratch);

// For each graph in `graphs`: true when it is the body of a loop Sub in some
// graph of `graphs` and no ordinary Sub in `graphs` instantiates it. Its
// iterations are realized inside the certificate of the graph that holds the
// loop, so it has no certificate of its own.
std::vector<bool> loop_body_only(const std::vector<std::shared_ptr<hhds::Graph>>& graphs);

}  // namespace lean_export
