
//#include "pass_opentimer.hpp"
#include "main_api.hpp"

class Pass_opentimer_api {
protected:
  static void tmap(Eprp_var &var) {
    /*Pass_opentimer pass;
    for(const auto &l:var.dict) {
      pass.set(l.first,l.second);
    }

    for(const auto &l:var.lgs) {
      pass.trans(l);
    }*/
  }

  static void optimize(Eprp_var &var) {
    /*
    Pass_opentimer pass;
    for(const auto &l:var.dict) {
      pass.set(l.first,l.second);
    }

    for(const auto &l:var.lgs) {
      LGraph *lg = pass.regen(l);
      var.add(lg);
    }*/
  }


public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("pass.opentimer", "optimize an lgraph with a opentimer, gen _mapped", &Pass_opentimer_api::optimize);

    m1.add_label_optional("verbose","verbose output true|false");
    m1.add_label_optional("liberty_file","liberty file for timing analysis");
    m1.add_label_optional("verilog_file","generate a verilog file for timing analysis");
    m1.add_label_optional("spef_file","spef file for timing analysis");
    m1.add_label_optional("sdc_file","sdc file for timing analysis");

    eprp.register_method(m1);

    Eprp_method m2("pass.opentimer.tmap", "tmap an lgraph with a opentimer", &Pass_opentimer_api::tmap);

    m2.add_label_optional("verbose","verbose output true|false");
    m2.add_label_optional("spef_file","spef file for timing analysis");
    m2.add_label_optional("spef_file","spef file for timing analysis");
    m2.add_label_optional("sdc_file","sdc file for timing analysis");

    eprp.register_method(m2);
  }

};

