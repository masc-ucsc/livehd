
#include "pass_abc.hpp"
#include "main_api.hpp"

class Pass_abc_api {
protected:
  static void tmap(Eprp_var &var) {
    Pass_abc pass;
    for(const auto &l:var.dict) {
      pass.set(l.first,l.second);
    }

    for(const auto &l:var.lgs) {
      pass.trans(l);
    }
  }

  static void optimize(Eprp_var &var) {
    Pass_abc pass;
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
    Eprp_method m1("pass.abc", "optimize an lgraph with a abc, gen _mapped", &Pass_abc_api::optimize);

    m1.add_label_optional("verbose","verbose output true|false");
    m1.add_label_optional("liberty_file","liberty file for synthesis");
    m1.add_label_optional("blif_file","generate a blif file for debugging");

    eprp.register_method(m1);

    Eprp_method m2("pass.abc.tmap", "tmap an lgraph with a abc", &Pass_abc_api::tmap);

    m2.add_label_optional("verbose","verbose output true|false");

    eprp.register_method(m2);
  }

};

