
#include <unistd.h>

#include "inou_json.hpp"
#include "main_api.hpp"

class Inou_json_api {
protected:
  static void tolg(Eprp_var &var) {
    const std::string files   = var.get("files");
    const std::string path    = var.get("path","lgdb");

    Inou_json json;

    json.set("name",var.get("name"));
    json.set("path",path);

    if (files.empty()) {
      Main_api::error(fmt::format("inou.json.tolg: no files provided"));
      return;
    }

    for(const auto &f:Main_api::parse_files(files,"inou.json.tolg")) {
      json.set("input",f);
      std::vector<LGraph *> lgs = json.tolg();

      if (lgs.empty()) {
        Main_api::warn(fmt::format("inou.json.tolg could not import from json to {} lgraph in {} path", var.get("name"), var.get("path")));
      }else{
        assert(lgs.size()==1);
        var.add(lgs[0]);
      }
    }
  }

  static void fromlg(Eprp_var &var) {
    const std::string output  = var.get("output");
    const std::string path    = var.get("path","lgdb");

    Inou_json json;

    json.set("path",path);
    json.set("output",output);

    std::vector<const LGraph *> lgs;
    for(const auto &l:var.lgs) {
      lgs.push_back(l);
    }

    if (lgs.empty()) {
      Main_api::warn(fmt::format("inou.json.fromlg needs an input lgraph. Either name or |> from lgraph.open"));
      return;
    }

    json.fromlg(lgs);
  }

public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("inou.json.tolg", "import from json to lgraph", &Inou_json_api::tolg);
    m1.add_label_optional("path","lgraph path");
    m1.add_label_required("files","json input file[s] to create lgraph[s]");
    m1.add_label_required("name","name for lgraph generated");

    eprp.register_method(m1);

    Eprp_method m2("inou.json.fromlg", "export from lgraph to json", &Inou_json_api::fromlg);
    m2.add_label_optional("path","lgraph path");
    m2.add_label_required("output","json output file from lgraphs");

    eprp.register_method(m2);
  }

};

