
#include "pass_topological.hpp"
#include "main_api.hpp"
#include "pass_topological_api.hpp"

void Pass_topological_api::trans(Eprp_var &var) {

  Pass_topo pass;

  for(const auto &l:var.lgs) {
    pass.trans(l);
  }
}

void Pass_topological_api::setup(Eprp &eprp) {
  Eprp_method m1("pass.topological", "counts number of nodes in an lgraph", &Pass_topological_api::trans);

  eprp.register_method(m1);
}

