// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Replica expansion — turn a compact replicated `Sub` back into the `count`
// ordinary Sub occurrences it denotes.
//
// This is the correctness escape hatch for every consumer that is not
// occurrence-aware (LEC, ABC, timing, Verilog emission, structural inliners).
// Rather than teaching each of them to interpret a descriptor, they run on an
// expanded copy and see exactly the graph today's source unrolling produces.
//
// The expansion is deliberately shape-faithful to that unrolled form, down to
// the spelling: occurrence `r` is named `<compact name>_li<r>`, the same
// `_li<ordinal>` tag the runner's unroller stamps on the instances one source
// call site produces per iteration. So a rolled loop and the same source
// unrolled name their replicas identically, each occurrence carries the ordinal
// LEC's box correspondence needs to pair by name, and neither code generator's
// same-name de-collision (cgen_verilog's `_cgen<N>`, cgen_sim's `__i<N>`) is
// reached — those spellings restart at the bare name for ordinal 0 and renumber
// when an unrelated instance appears.
//
// Wiring produced for ordinal r in [0,count):
//   invariant input   -> the compact node's external driver (shared by all)
//   index input       -> a fresh constant `first + r*step`
//   activation input  -> r == 0: the external driver (or constant true)
//                        r >  0: `active[r-1] && next_active[r-1]` when the
//                                descriptor chains activation, else the same
//                                driver as replica 0
//   carry input i     -> r == 0: the external driver (the initial value)
//                        r >  0: replica r-1's mapped carry output
//   outputs           -> external readers bind to replica count-1
//   count == 0        -> carried outputs pass their external initial value
//                        straight through to the readers; the node vanishes
//
// It mutates the graph it is given, so callers pass a scratch copy when the
// library graph must survive (`lhd` gives each flow its own loaded library, so
// an emission-time expansion is already private to that run).

#include <memory>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Expands every replicated Sub in `g`. Returns the number of compact nodes
// expanded, or -1 if a descriptor could not be realized (a diagnostic is
// emitted against `from_pass`).
int expand_replicated_subs(hhds::Graph* g, std::string_view from_pass);

// Same, over a whole design (an Eprp_var's graph list). Returns false when any
// graph failed to expand.
//
// Call this at the entry of any pass that is NOT occurrence-aware — one that
// walks a Sub as a single physical instance. Without it such a pass reads a
// compact node as ONE occurrence: a prover then compares count-1 replicas that
// are not there (a false PROVEN), and an area/timing pass under-counts by the
// same factor.
bool expand_replicated_subs_all(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view from_pass);

}  // namespace livehd::graph_util
