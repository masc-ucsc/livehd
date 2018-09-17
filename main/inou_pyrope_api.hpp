#include <unistd.h>

#include "inou_pyrope.hpp"
#include "main_api.hpp"

class Inou_pyrope_api {
protected:
  static void tolg(Eprp_var &var) {
    Inou_pyrope pyrope;

    for(const auto &l:var.dict) {
      pyrope.set(l.first,l.second);
    }

    if(access(var.get("input").c_str(), R_OK) == -1) {
      Main_api::error(fmt::format("inou.pyrope could not open pyrope file named {}",var.get("input")));
      return;
    }

    std::vector<LGraph *> lgs = pyrope.tolg();

    if (lgs.empty()) {
      Main_api::warn(fmt::format("inou.pyrope.tolg could not import from pyrope to {} lgraph in {} path", var.get("name"), var.get("path")));
    }else{
      assert(lgs.size()==1);
      var.add(lgs[0]);
    }
  }

  static void fromlg(Eprp_var &var) {
    Inou_pyrope pyrope;

    for(const auto &l:var.dict) {
      pyrope.set(l.first,l.second);
    }

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
    m1.add_label_required("name","lgraph name");

    m1.add_label_optional("input","pyrope input file");

    eprp.register_method(m1);

    Eprp_method m2("inou.pyrope.fromlg", "export from lgraph to pyrope", &Inou_pyrope_api::fromlg);
    m2.add_label_optional("path","lgraph path");
    m2.add_label_required("name","lgraph name");

    m2.add_label_optional("output","pyrope output file");

    eprp.register_method(m2);
  }

};
