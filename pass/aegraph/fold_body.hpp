//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string_view>

#include "cprop.hpp"
#include "fold_engine.hpp"
#include "hhds/graph.hpp"
#include "pass.hpp"

// livehd::fold_body: THE per-body constant-folding entry for every caller that is not one of the compile
// schedule's two cprop slots. It dispatches on the engine:
//   classic          -> Cprop::do_trans (byte-for-byte what each site ran before),
//   aegraph          -> the e-graph engine (optimize_body: engine + the classic post steps),
//   aegraph+classic  -> the engine, then Cprop::do_trans on its result.
// The compile slots keep going through the pass.cprop/pass.aegraph methods (lhd_kernel_compile.cpp);
// both paths read the same `compile.cprop.engine` setting.
namespace livehd {

struct Fold_options {
  bool                low_lanes        = false;  // Cprop{low_lanes}: narrow over known low lanes (a bitwidth round follows)
  bool                bitwidth_follows = true;   // false: nothing re-stamps widths afterwards (LEC/semdiff preflight)
  Fold_cost           cost             = Fold_cost::synth;
  Cprop::Node_scope   guard;                     // the accumulation-guard scope handed to the classic Cprop (empty = nowhere)
  bool                guard_whole      = false;  // the engine's own guard rule: every cell is in scope (a lifted loop body)
  std::string_view    site;                      // counter/log name, e.g. "specialize.refold"
};

// Fold one body with an explicit engine.
void fold_body(const std::shared_ptr<hhds::Graph>& graph, Fold_engine engine, const Fold_options& options);
// Fold one body with the process-wide engine (set_fold_engine).
void fold_body(const std::shared_ptr<hhds::Graph>& graph, const Fold_options& options);

// The method-level counterpart for callers that hold an Eprp_var (the memory-module lowering): classic runs
// pass.cprop exactly as before, aegraph runs pass.aegraph, aegraph+classic runs both. `after_bitwidth`
// mirrors the compile schedule's second slot.
void fold_graphs(Eprp_var& var, Fold_engine engine, std::string_view site, Fold_cost cost = Fold_cost::synth,
                 bool after_bitwidth = false);
void fold_graphs(Eprp_var& var, std::string_view site, Fold_cost cost = Fold_cost::synth, bool after_bitwidth = false);

}  // namespace livehd
