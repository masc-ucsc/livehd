
#include "pass_dce.hpp"
#include "main_api.hpp"

class Pass_dce_api {
protected:

  static void optimize(Eprp_var &var) {
    Pass_dce pass;

    for(auto &l:var.lgs) {
      pass.trans(l);
    }
  }


public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("pass.dce", "optimize an lgraph with a dce, gen _mapped", &Pass_dce_api::optimize);
    eprp.register_method(m1);
  }

};

