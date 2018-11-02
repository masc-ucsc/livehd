#include <unistd.h>

#include "inou_graphviz.hpp"
#include "main_api.hpp"

class Inou_graphviz_api {
protected:
static void fromlg(Eprp_var &var) {
  const std::string odir   = var.get("odir");

  Inou_graphviz graphviz;

  graphviz.set("odir",odir);

  std::vector<const LGraph *> lgs;
  for(const auto &l:var.lgs) {
    lgs.push_back(l);
  }

  graphviz.fromlg(lgs);
}

public:
static void setup(Eprp &eprp) {
  Eprp_method m2("inou.graphviz", "export lgraph to graphviz dot format", &Inou_graphviz_api::fromlg);
  m2.add_label_optional("odir","graphviz output directory",".");

  eprp.register_method(m2);
}

};
