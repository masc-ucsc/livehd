
#include "inou_cfg.hpp"
#include "main_api.hpp"

class Inou_cfg_api {
protected:
  static void generate(Eprp_var &var) {
    Inou_cfg inou;

    for(const auto &l:var.dict) {
      inou.set(l.first,l.second);
    }

    std::vector<LGraph *> lgs = inou.generate();

    if (lgs.empty()) {
      Main_api::warn(fmt::format("inou.inou.generate could not create a {} lgraph in {} path", var.get("name"), var.get("path")));
    }else{
      for(const auto &l:lgs) {
        var.add(l);
      }
    }
  }

public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("inou.cfg", "generate a lgraph from a cfg (pyrope)", &Inou_cfg_api::generate);
    m1.add_label_optional("path","lgraph path");
    m1.add_label_required("name","lgraph name");

    m1.add_label_required("src","src cfg file");

    eprp.register_method(m1);
  }

};

