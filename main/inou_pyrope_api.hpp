#include <unistd.h>

#include "inou_pyrope.hpp"
#include "main_api.hpp"

class Inou_pyrope_api {
protected:
static void tolg(Eprp_var &var) {

  const std::string path    = var.get("path","lgdb");
  const std::string files   = var.get("files");

  Inou_pyrope pyrope;

  pyrope.set("path",path);

  for(const auto &f:Main_api::parse_files(files,"inou.pyrope.tolg")) {
    pyrope.set("input",f);

    var.add(pyrope.tolg());
  }
}

static void fromlg(Eprp_var &var) {
  const std::string output   = var.get("output");

  Inou_pyrope pyrope;

  pyrope.set("output",output);

  std::vector<const LGraph *> lgs;
  for(const auto &l:var.lgs) {
    lgs.push_back(l);
  }

  pyrope.fromlg(lgs);
}

public:
static void setup(Eprp &eprp) {
  Eprp_method m1("inou.pyrope.tolg", "import from pyrope to lgraph", &Inou_pyrope_api::tolg);
  m1.add_label_optional("path","lgraph path");

  m1.add_label_required("files","pyrope input file[s]");

  eprp.register_method(m1);

  Eprp_method m2("inou.pyrope.fromlg", "export from lgraph to pyrope", &Inou_pyrope_api::fromlg);
  m2.add_label_required("output","pyrope output file");

  eprp.register_method(m2);
}

};
