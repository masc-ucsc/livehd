
#include "pass_dce.hpp"
#include "main_api.hpp"

class Pass_dce_api {
protected:
  static void tmap(Eprp_var &var) {
    Pass_dce pass;
    for(const auto &l:var.dict) {
      pass.set(l.first,l.second);
    }

    for(const auto &l:var.lgs) {
      pass.trans(l);
    }
  }

  static void optimize(Eprp_var &var) {
    Pass_dce pass;
    for(const auto &l:var.dict) {
      pass.set(l.first,l.second);
    }

    for(const auto &l:var.lgs) {
      LGraph *lg = pass.regen(l);
      var.add(lg);
    }
  }


public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("pass.dce", "optimize an lgraph with a dce, gen _mapped", &Pass_dce_api::optimize);

    eprp.register_method(m1);

    Eprp_method m2("pass.dce.tmap", "tmap an lgraph with a dce", &Pass_dce_api::tmap);

    eprp.register_method(m2);
  }

};

