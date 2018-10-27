
#include "pass_sample.hpp"
#include "main_api.hpp"
#include "pass_sample_api.hpp"

void Pass_sample_api::trans(Eprp_var &var) {

  Pass_sample pass;

  for(const auto &l:var.lgs) {
    pass.trans(l);
  }
}

void Pass_sample_api::setup(Eprp &eprp) {
  Eprp_method m1("pass.sample", "counts number of nodes in an lgraph", &Pass_sample_api::trans);

  eprp.register_method(m1);
}

