
#if 0
#include "inou_lef_api.hpp"

#include "inou_lef.hpp"
#include "main_api.hpp"

void Inou_lef_api::tolg(Eprp_var &var) {
  Inou_lef inou;

  for (const auto &l : var.dict) {
    inou.set(l.first, l.second);
  }

  std::vector<Lgraph *> lgs = inou.tolg();

  if (lgs.empty()) {
    Main_api::warn(std::format("inou.lef.tolg could not create a {} lgraph in {} path", var.get("name"), var.get("path")));
  } else {
    assert(lgs.size() == 1);  // lef  only generated one graph at a time
    var.add(lgs[0]);
  }
}

void Inou_lef_api::setup(Eprp &eprp) {
  Eprp_method m1("inou.tolg", "generate a lgraph from an lef file", &Inou_lef_api::tolg);
  m1.add_label_optional("path", "lgraph path");
  m1.add_label_required("name", "lgraph name");

  m1.add_label_required("lef_file", "lef file");

  eprp.register_method(m1);
}
#endif
