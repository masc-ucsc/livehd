//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string_view>

#include "cprop.hpp"
#include "fold_engine.hpp"
#include "hhds/graph.hpp"
#include "pass.hpp"

// livehd::fold_body: THE per-body constant-folding entry for every caller that is not one of the compile
// schedule's two fold slots: the e-graph engine (optimize_body: the engine, then the shared post steps).
// The compile slots run the pass.aegraph method (lhd_kernel_compile.cpp).
namespace livehd {

struct Fold_options {
  bool                low_lanes        = false;  // Cprop{low_lanes}: narrow over known low lanes (a bitwidth round follows)
  bool                bitwidth_follows = true;   // false: nothing re-stamps widths afterwards (LEC/semdiff preflight)
  Fold_cost           cost             = Fold_cost::synth;
  Cprop::Node_scope   guard;                     // the accumulation-guard scope of the shared scalar/post steps (empty = nowhere)
  bool                guard_whole      = false;  // the engine's own guard rule: every cell is in scope (a lifted loop body)
  std::string_view    site;                      // counter/log name, e.g. "specialize.refold"
};

// Fold one body.
void fold_body(const std::shared_ptr<hhds::Graph>& graph, const Fold_options& options);

// The method-level counterpart for callers that hold an Eprp_var (the memory-module lowering): runs
// pass.aegraph. `after_bitwidth` mirrors the compile schedule's second slot.
void fold_graphs(Eprp_var& var, std::string_view site, Fold_cost cost = Fold_cost::synth, bool after_bitwidth = false);

}  // namespace livehd
