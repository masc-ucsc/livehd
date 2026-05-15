//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnast_to_lgraph.hpp"

#include <string>

#include "graph_library.hpp"

static Pass_plugin sample("pass_lnast_to_lgraph", Pass_lnast_to_lgraph::setup);

void Pass_lnast_to_lgraph::setup() {
  Eprp_method m1("pass.lnast_to_lgraph", "lower LNAST to LGraph", &Pass_lnast_to_lgraph::work);
  m1.add_label_optional("path", "lgraph database directory", "lgdb");
  register_pass(m1);
}

Pass_lnast_to_lgraph::Pass_lnast_to_lgraph(const Eprp_var& var) : Pass("pass.lnast_to_lgraph", var) {}

void Pass_lnast_to_lgraph::work(Eprp_var& var) {
  Pass_lnast_to_lgraph p(var);

  if (var.lnasts.empty()) {
    Pass::warn("pass.lnast_to_lgraph: no LNASTs in pipeline — nothing to lower");
    return;
  }

  auto db_path = std::string(var.get("path", "lgdb"));
  if (db_path.empty()) {
    db_path = "lgdb";
  }

  auto* lib = Graph_library::instance(db_path);

  for (const auto& ln : var.lnasts) {
    auto  module_name = std::string(ln->get_top_module_name());
    auto* lg          = lib->create_lgraph(module_name, db_path);
    if (!lg) {
      Pass::error("pass.lnast_to_lgraph: could not create LGraph for '{}'", module_name);
      return;
    }
    Lnast_to_lgraph lowerer(lg, ln);
    lowerer.lower();
    var.add(lg);
  }
}
