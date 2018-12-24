//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <regex>

#include "lgraph.hpp"
#include "main_api.hpp"

class Meta_api {
protected:
  static void open(Eprp_var &var) {

    const std::string path = var.get("path");
    const std::string name = var.get("name");
    assert(!name.empty());

    LGraph *lg = LGraph::open(path,name);

    if (lg==0) {
      Main_api::warn(fmt::format("lgraph.open could not find {} lgraph in {} path", name, path));
    }else{
      var.add(lg);
    }
  }

  static void create(Eprp_var &var) {

    const std::string path = var.get("path");
    const std::string name = var.get("name");
    assert(!name.empty());

    LGraph *lg = LGraph::create(path,name,"lgshell");

    if (lg==0) {
      Main_api::error(fmt::format("lgraph.create could not open {} lgraph in {} path", name, path));
      return;
    }

    var.add(lg);
  }

  static void rename(Eprp_var &var) {

    const std::string path = var.get("path");
    const std::string name = var.get("name");
    const std::string dest = var.get("dest");
    assert(!name.empty());

    LGraph::rename(path,name,dest);
  }

  static void match(Eprp_var &var) {

    const std::string path  = var.get("path");
    const std::string match = var.get("match");

    const auto *library = Graph_library::instance(path);
    if (library==0) {
      Main_api::warn(fmt::format("lgraph.match could not open {} path", path));
      return;
    }

    std::vector<LGraph *> lgs;

    try {
      library->each_type(match, [&lgs,path](Lg_type_id lgid, const std::string &name) {
          LGraph *lg = LGraph::open(path,name);
          if (lg) {
            lgs.push_back(lg);
          }
        });
    } catch (const std::regex_error& e) {
      Main_api::error(fmt::format("invalid match:{} regex. It is a FULL regex unlike bash. To test, try: `ls path | grep -E \"match\"`", match));
    }

    for(LGraph *lg:lgs) {
      var.add(lg);
    }
  }

  static void stats(Eprp_var &var) {

    for(const auto &lg:var.lgs) {
      assert(lg);
      lg->print_stats();
    }
  }

  static void dump(Eprp_var &var) {

    fmt::print("lgraph.dump labels:\n");
    for(const auto &l:var.dict) {
      fmt::print(fmt::format("  {}:{}\n",l.first,l.second));
    }
    fmt::print("lgraph.dump lgraphs:\n");
    for(const auto &l:var.lgs) {
      fmt::print(fmt::format("  {}/{}\n",l->get_path(), l->get_name()));
    }
  }

  static void lgdump(Eprp_var &var) {

    fmt::print("lgraph.dump lgraphs:\n");
    for(const auto &l:var.lgs) {
      fmt::print(fmt::format("  {}/{}\n",l->get_path(), l->get_name()));
      l->dump();
    }
  }


  Meta_api() {
  }
public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("lgraph.open", "open an lgraph if it exists", &Meta_api::open);
    m1.add_label_optional("path","lgraph path","lgdb");
    m1.add_label_required("name","lgraph name");

    eprp.register_method(m1);

    //---------------------
    Eprp_method m2("lgraph.create", "create a new lgraph", &Meta_api::create);
    m2.add_label_optional("path","lgraph path","lgdb");
    m2.add_label_required("name","lgraph name");

    eprp.register_method(m2);

    //---------------------
    Eprp_method m3("lgraph.stats", "print the stats from the passed graphs", &Meta_api::stats);

    eprp.register_method(m3);

    //---------------------
    Eprp_method m4("lgraph.dump", "verbose insides for lgraph ", &Meta_api::lgdump);

    eprp.register_method(m4);

    //---------------------
    Eprp_method m5("dump", "dump labels and lgraphs passed", &Meta_api::dump);
    eprp.register_method(m5);

    //---------------------
    Eprp_method m6("lgraph.rename", "rename a lgraph", &Meta_api::rename);
    m6.add_label_optional("path","lgraph path","lgdb");
    m6.add_label_required("name","lgraph name");
    m6.add_label_required("dest","lgraph destination name");

    eprp.register_method(m6);

    //---------------------
    Eprp_method m7("lgraph.match", "open many lgraphs (match regex)", &Meta_api::match);
    m7.add_label_optional("path","lgraph path","lgdb");
    m7.add_label_optional("match","quoted string of regex to match . E.g: match:\"\\.v$\" for verilog files.",".*");

    eprp.register_method(m7);
  }

};

