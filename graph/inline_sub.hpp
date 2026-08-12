// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

#include "hhds/graph.hpp"

namespace livehd::graph_util {

// Structurally inline ONE Sub instance into the body that holds it, in place.
//
// The child's nodes are cloned into `parent` (node/wire names prefixed with the
// instance name, `alu.foo`), every edge crossing the boundary is rewired to what
// is on the other side of it, and the Sub node itself is deleted. The child def
// is untouched -- it stays in the library for its other instantiation sites, and
// the caller decides when (or whether) it becomes garbage.
//
// This is deliberately SINGLE-LEVEL. It does not recurse into the child's own
// sub-instances: those stay as Sub nodes in the cloned body. A caller wanting a
// deep inline walks the def hierarchy children-first, so that by the time a def
// is inlined anywhere its own children are already part of its body. That
// ordering is what keeps this a flat transform with no instantiation-context
// bookkeeping (contrast pass/partition/flatten.cpp, which inlines EVERYTHING at
// once and therefore has to carry a per-instance context tree).
//
// `inst` must be a Sub whose target def has a materialized body; a body-less
// black box (liberty cell, external IP, an fproperty/lgassert marker) has nothing
// to inline and is rejected. Returns false after emitting a diagnostic on a shape
// it cannot resolve (today: a combinational feed-through cycle through the
// boundary). On false the parent may hold a partially inlined body -- callers
// treat it as fatal, exactly like flatten.
// `def` overrides the body lookup for an instance whose def is not in the
// parent's own graph library — a `lec --lib` cell model lives in a SIDE library,
// so `Node_class::get_subnode_graph()` is null for it even though the instance's
// subnode gid resolves in that side map. nullptr = resolve the ordinary way.
// `name_state` names an UNNAMED spliced state node (a cell model's internal
// flop) after the INSTANCE, so a flop-cut correspondence key survives the
// inline. Without it the cut is keyed on a synthesized net name that has no
// counterpart on the other design.
[[nodiscard]] bool inline_sub_instance(hhds::Graph* parent, const hhds::Node_class& inst, std::string_view from_pass,
                                       hhds::Graph* def = nullptr, bool name_state = false);

}  // namespace livehd::graph_util
