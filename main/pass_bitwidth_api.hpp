
#include "pass_bitwidth.hpp"
#include "main_api.hpp"

class Pass_bitwidth_api {
protected:
  static void trans(Eprp_var &var) {

    Pass_bitwidth pass;
    std::vector<const LGraph *> lgs;
    for(const auto &l:var.lgs) {
      pass.trans(l);
    }

  }

public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("pass.bitwidth", "MIT algorithm... FIXME", &Pass_bitwidth_api::trans);

    eprp.register_method(m1);
  }
};
