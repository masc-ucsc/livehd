#include <unistd.h>

#include "inou_ramgen.hpp"
#include "main_api.hpp"

class Inou_ramgen_api {
protected:
static void fromlg(Eprp_var &var) {
  const std::string odir   = var.get("odir");

  Inou_ramgen ramgen;

  ramgen.set("odir",odir);

  std::vector<const LGraph *> lgs;
  for(const auto &l:var.lgs) {
    lgs.push_back(l);
  }

  ramgen.fromlg(lgs);
}

public:
static void setup(Eprp &eprp) {
  Eprp_method m2("inou.ramgen.fromlg", "export SRAM information", &Inou_ramgen_api::fromlg);
  m2.add_label_optional("odir","ramgen output directory",".");

  eprp.register_method(m2);
}

};
