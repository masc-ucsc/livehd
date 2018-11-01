#include "eprp_utils.hpp"
#include "pass_dfg.hpp"
#include "main_api.hpp"
#include "pass_dfg_api.hpp"

void Pass_dfg_api::generate(Eprp_var &var) {
  Pass_dfg pass_dfg;
  for(const auto &l:var.dict) {
    pass_dfg.set(l.first,l.second);
  }

  std::vector<LGraph *> lgs;
  for(auto &g:var.lgs) {
    if (Eprp_utils::ends_with(g->get_name(),std::string("_cfg"))) {
      g = pass_dfg.regen(g);
    }
    lgs.push_back(g);
  }

  if (lgs.empty()) {
    Main_api::warn(fmt::format("pass.dfg.generate needs an input cfg lgraph. Either name or |> from lgraph.open"));
    return;
  }

}

void Pass_dfg_api::optimize(Eprp_var &var) {
  Pass_dfg pass_dfg;

  pass_dfg.optimize(var.lgs[0]);
}

void Pass_dfg_api::pseudo_bitwidth(Eprp_var &var) {
  Pass_dfg pass_dfg;
  pass_dfg.pseudo_bitwidth(var.lgs[0]);
}

void Pass_dfg_api::setup(Eprp &eprp) {
  Eprp_method m1("pass.dfg.generate", "generate a dfg lgraph from a cfg lgraph", &Pass_dfg_api::generate);
  m1.add_label_optional("path","lgraph path");
  m1.add_label_required("name","lgraph name");

  eprp.register_method(m1);

  Eprp_method m2("pass.dfg.optimize", "optimize a dfg lgraph", &Pass_dfg_api::optimize);
  m2.add_label_optional("path","lgraph path");
  m2.add_label_optional("name","lgraph name");

  eprp.register_method(m2);

  Eprp_method m3("pass.dfg.pseudo_bitwidth", "patch fake bitwidth for a dfg lgraph", &Pass_dfg_api::pseudo_bitwidth);
  m3.add_label_optional("path","lgraph path");
  m3.add_label_optional("name","lgraph name");

  eprp.register_method(m3);
}

