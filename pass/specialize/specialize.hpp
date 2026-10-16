// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"

// pass.specialize -- cross-instance constant specialization, on a PRIVATE copy
// of the design (never the frozen compile output). An instance input tied to
// a constant gets a specialized callee (`<callee>__k<hash>`, named by content)
// with the constant folded in by cprop + bitwidth; a constant a callee returns
// is published to its parent. Native loop descriptors and state instance
// identity stay intact. Runs after the partitioning is decided: the simulator
// on its private library before Color_plan, synthesis on the mapper's private
// copy after pass.color (docs/passes.html).
namespace livehd::specialize {

struct Options {
  // Give the nodes the rewrite creates the color of a colored neighbor, so a
  // colored design (synthesis regions) gains no stray color-0 region.
  bool inherit_colors = false;
  // Only specialize a callee whose closure holds no state (no
  // Flop/Latch/Fflop/Memory, no blackbox): synthesis keys register names, DFF
  // picks, clock-gate mapping and LEC pairing on a stateful definition's
  // identity, so those keep their own module.
  bool state_free_only = false;
  // Refold (cprop + bitwidth) EVERY definition once, not only the ones this
  // pass rewrites. The simulator wants it; synthesis does not -- after
  // pass.color a refold would reshape regions nothing here touched.
  bool refold_all = false;
  // How many specialized versions one definition may have:
  //   0  no hierarchical traversal (every instance boundary as compiled);
  //   1  only the specialization ALL its instances share (the join of their
  //      contexts: a constant input where every instance agrees, the widest
  //      instance width): one copy `<name>__k<hash>`, the original untouched;
  //   N  one clone per distinct constant binding, at most N per definition.
  int max_versions = 1;
};

// Specialize `graphs` (appending the copies; an original stays, even when no
// instance uses it any more -- a testbench may drive any module directly). Information crosses an instance
// boundary both ways: a context into the child (constant inputs, narrower
// input widths) and a constant output, or a copy's narrower output, into the
// parent. Returns the number of specialized definitions created.
int specialize_constants(std::vector<std::shared_ptr<hhds::Graph>>& graphs, const Options& options = {});

// One constant-folding and width round over a private body a structural
// rewrite just changed (a small compact loop spliced flat exposes its ordinal
// arithmetic with constant inputs): the same pair specialize_constants runs
// after a callee publishes a constant. Kept here so cgen_sim.cpp does not
// include the pass headers (pass/common/pass.hpp's `Pass` shadows its locals).
void refold_private_body(const std::shared_ptr<hhds::Graph>& graph);

// In a colored graph (synthesis regions), give every node created since
// `existed` (node debug ids; nullptr = every node) the color of a colored
// neighbor -- drivers first, then readers, to a fixpoint -- so a rewrite of a
// colored body (a fold, an expanded loop's cleanup) gains no stray color-0
// region. A node pass.color left uncolored on purpose is in `existed` and keeps
// no color. A graph with no colored node is left alone.
void inherit_colors(hhds::Graph& g, const absl::flat_hash_set<uint64_t>* existed);

}  // namespace livehd::specialize
