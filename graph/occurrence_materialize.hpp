// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Backend occurrence materialization — realize compact Subnode_groups into a
// private physical output graph.
//
// This is the correctness escape hatch for every consumer that is not
// occurrence-aware (LEC, ABC, timing, Verilog emission, structural inliners).
// Rather than teaching each of them to interpret a descriptor, they run on an
// expanded copy and see exactly the graph today's source unrolling produces.
//
// The expansion is deliberately shape-faithful to that unrolled form, down to
// the spelling: occurrence `r` is named `<compact name>__li<r>`, the same
// `__li<ordinal>` tag the runner's unroller stamps on the instances one source
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
// This adapter is only for backends whose private output representation is
// still an hhds::Graph. It mutates that scratch graph and must never receive a
// source/EPRP graph. Read-only consumers use occurrences() directly.

#include <memory>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Materializes one compact Subnode_group in private scratch `g`. Returns true
// when the selected node was expanded. This is for occurrence-aware consumers
// that retain the shapes they implement natively and expand only their
// documented fallback cases.
// When supplied, replicas receives the newly created ordinary instances.
bool materialize_occurrence(hhds::Graph* g, const hhds::Node_class& inst, std::string_view from_pass,
                            std::vector<hhds::Node_class>* replicas = nullptr);

// Materializes every compact Subnode_group in private scratch `g`. Returns the
// number of groups realized, or -1 if a binding could not be represented.
int materialize_occurrences(hhds::Graph* g, std::string_view from_pass);

// Same, over a whole design (an Eprp_var's graph list). Returns false when any
// graph failed to expand.
//
// Call this at the entry of any pass that is NOT occurrence-aware — one that
// walks a Sub as a single physical instance. Without it such a pass reads a
// compact node as ONE occurrence: a prover then compares count-1 replicas that
// are not there (a false PROVEN), and an area/timing pass under-counts by the
// same factor.
bool materialize_occurrences_all(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view from_pass);

// The UNROLLED spelling of a hierarchical name that crosses ROLLED loop
// wrappers. A rolled comptime loop is ONE replicated `__loop<n>` Sub named
// `u_loop_<n>` (upass_runner.cpp), whose k-th occurrence hhds spells
// `u_loop_<n>__li<k>`; the unrolled lowering of the same source stamps the k-th
// body copy's instances `<lhs>__li<k>` with no wrapper level, over the same
// ordinal space. Dropping each wrapper segment and carrying its tag onto the
// next segment (before that segment's own, inner, `__li` tags) maps one onto the
// other exactly:
//   u_loop_0__li1.lane_q.acc           -> lane_q__li1.acc
//   u_loop_0__li0.u_loop_0__li1.t.acc  -> t__li0__li1.acc    (nested)
//   u_loop_0__li0.t__li1.acc           -> t__li0__li1.acc    (inner loop already folded)
// A wrapper that is the LAST segment (the box instance itself) is left as-is.
// LEC canonicalizes state names with it (pass/lec/encode.cpp), and synthesis
// renames what it inlines from expanded loop bodies with it
// (pass/synth/loop_cleanup.cpp), so a rolled source's netlist names its
// instances and registers as the unrolled source's netlist does.
[[nodiscard]] std::string fold_rolled_loop_wrappers(std::string_view s);

}  // namespace livehd::graph_util
