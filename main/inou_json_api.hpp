
#include <unistd.h>

#include "inou_json.hpp"
#include "main_api.hpp"

class Inou_json_api {
protected:
  static void tolg(Eprp_var &var) {
    Inou_json json;

    for(const auto &l:var.dict) {
      json.set(l.first,l.second);
    }

    if(access(var.get("input").c_str(), R_OK) == -1) {
      Main_api::error(fmt::format("inou.json. could not open json file named {}",var.get("input")));
      return;
    }

    std::vector<LGraph *> lgs = json.tolg();

    if (lgs.empty()) {
      Main_api::warn(fmt::format("inou.json.tolg could not import from json to {} lgraph in {} path", var.get("name"), var.get("path")));
    }else{
      assert(lgs.size()==1);
      var.add(lgs[0]);
    }
  }

  static void fromlg(Eprp_var &var) {
    Inou_json json;

    for(const auto &l:var.dict) {
      json.set(l.first,l.second);
    }

    std::vector<const LGraph *> lgs;
    for(const auto &l:var.lgs) {
      lgs.push_back(l);
    }

    json.fromlg(lgs);
  }

public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("inou.json.tolg", "import from json to lgraph", &Inou_json_api::tolg);
    m1.add_label_optional("path","lgraph path");
    m1.add_label_required("name","lgraph name");

    m1.add_label_optional("input","json input file");

    eprp.register_method(m1);

    Eprp_method m2("inou.json.fromlg", "export from lgraph to json", &Inou_json_api::fromlg);
    m2.add_label_optional("path","lgraph path");
    m2.add_label_required("name","lgraph name");

    m2.add_label_optional("output","json output file");

    eprp.register_method(m2);
  }

};

