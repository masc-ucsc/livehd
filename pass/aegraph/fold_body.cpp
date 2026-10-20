//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fold_body.hpp"

#include "pass_aegraph.hpp"

namespace livehd {

void fold_body(const std::shared_ptr<hhds::Graph>& graph, Fold_engine engine, const Fold_options& options) {
  if (!graph) {
    return;
  }
  fold_note(options.site, engine);
  Cprop cprop{options.low_lanes};
  cprop.set_guard_scope(options.guard);
  if (engine == Fold_engine::classic) {
    cprop.do_trans(graph);
    return;
  }
  aegraph::Options eopts;
  eopts.cost         = options.cost == Fold_cost::sim ? aegraph::Options::Cost::sim : aegraph::Options::Cost::synth;
  eopts.preflight    = !options.bitwidth_follows;  // no bitwidth after: every created cell must carry a stamp
  eopts.guard_scope  = options.guard_whole;
  eopts.unstamped_ok = options.low_lanes;
  (void)aegraph::optimize_body(cprop, graph, eopts);  // a refused graph is left to classic inside
  if (engine == Fold_engine::aegraph_classic) {
    cprop.do_trans(graph);
  }
}

void fold_body(const std::shared_ptr<hhds::Graph>& graph, const Fold_options& options) { fold_body(graph, fold_engine(), options); }

void fold_graphs(Eprp_var& var, Fold_engine engine, std::string_view site, Fold_cost cost, bool after_bitwidth) {
  fold_note(site, engine);
  if (engine != Fold_engine::classic) {
    Eprp_var::Eprp_dict labels{{"engine", "aegraph"}, {"cost", fold_cost_name(cost)}, {"after_bitwidth", after_bitwidth ? "true" : "false"}};
    Pass::eprp.run_method_now("pass.aegraph", var, labels);
  }
  if (engine != Fold_engine::aegraph) {
    Pass::eprp.run_method_now("pass.cprop", var, {{"engine", "classic"}});  // a var keeps its labels between steps
  }
}

void fold_graphs(Eprp_var& var, std::string_view site, Fold_cost cost, bool after_bitwidth) {
  fold_graphs(var, fold_engine(), site, cost, after_bitwidth);
}

}  // namespace livehd
