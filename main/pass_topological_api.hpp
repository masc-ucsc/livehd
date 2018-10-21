
#include "pass_topological.hpp"
#include "main_api.hpp"

class Pass_topological_api {
protected:
  static void trans(Eprp_var &var) {

    Pass_topo pass;
    std::vector<const LGraph *> lgs;
    for(const auto &l:var.lgs) {
      //pass.set(l.first,l.second)
      pass.trans(l);
    }

  }

public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("pass.topological", "counts number of nodes in an lgraph", &Pass_topological_api::trans);

    eprp.register_method(m1);
  }

};
