// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// cgen-oriented coloring, specialized for inou.cgen.sim's per-output-cone split
// (the false-combinational-loop-through-an-instance fix).
//
// The `acyclic` algorithm is MFFC: it makes EVERY fan-out>1 node its own
// partition root, so its color count tracks internal shared nets, not outputs
// (e.g. a 1-output module with heavy fan-out gets dozens of colors). That is the
// wrong granularity for cgen: to break a false loop we only need to know, per
// PRIMARY OUTPUT, which inputs it combinationally depends on, so the parent can
// schedule one output's cone before an input the loop feeds back.
//
// `cgen` colors each combinational node by WHICH cone-sink(s) it forward-reaches,
// where a cone-sink is a primary output (each its own id) or the shared "state"
// bucket (all flop/mem next-state `din` logic). A node's color is that signature,
// so shared logic (feeding >1 output) gets its own color and is computed once;
// the number of colors tracks the number of outputs (a few), not fan-out. The
// backward walk stops at loop_break (flop/mem) boundaries — a registered path
// contributes no combinational input dependency.

#include "color_common.hpp"
#include "hhds/graph.hpp"

namespace livehd::color {

class Color_cgen {
public:
  explicit Color_cgen(Color_opts opts);
  void label(hhds::Graph* g);

private:
  Color_opts opts;
};

}  // namespace livehd::color
