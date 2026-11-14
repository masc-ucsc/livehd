//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "fold_body.hpp"

#include "pass_aegraph.hpp"

namespace livehd {

void fold_body(const std::shared_ptr<hhds::Graph>& graph, const Fold_options& options) {
  if (!graph) {
    return;
  }
  fold_note(options.site);
  Cprop cprop{options.low_lanes};
  cprop.set_guard_scope(options.guard);
  aegraph::Options eopts;
  eopts.cost         = options.cost == Fold_cost::sim ? aegraph::Options::Cost::sim : aegraph::Options::Cost::synth;
  eopts.preflight    = !options.bitwidth_follows;  // no bitwidth after: every created cell must carry a stamp
  eopts.guard_scope  = options.guard_whole;
  eopts.unstamped_ok = options.low_lanes;
  eopts.rules_disable = fold_rules_disable();
  (void)aegraph::optimize_body(cprop, graph, eopts);
}

void fold_graphs(Eprp_var& var, std::string_view site, Fold_cost cost, bool after_bitwidth) {
  fold_note(site);
  Eprp_var::Eprp_dict labels{{"cost", fold_cost_name(cost)}, {"after_bitwidth", after_bitwidth ? "true" : "false"}};
  Pass::eprp.run_method_now("pass.aegraph", var, labels);
}

}  // namespace livehd
