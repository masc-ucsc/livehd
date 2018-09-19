
#include "lgraph.hpp"
#include "main_api.hpp"

class Meta_api {
protected:
  static void find(Eprp_var &var) {

    const std::string path = var.get("path","lgdb");
    const std::string name = var.get("name");
    assert(!name.empty());

    LGraph *lg = LGraph::find_lgraph(path,name);

    if (lg==0) {
      Main_api::warn(fmt::format("lgraph.find could not find {} lgraph in {} path", name, path));
    }else{
      var.add(lg);
    }
  }

  static void open(Eprp_var &var) {

    const std::string path = var.get("path","lgdb");
    const std::string name = var.get("name");
    assert(!name.empty());

    LGraph *lg = LGraph::open_lgraph(path,name);

    if (lg==0) {
      Main_api::error(fmt::format("lgraph.open could not open {} lgraph in {} path", name, path));
      return;
    }

    var.add(lg);
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


  Meta_api() {
  }
public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("lgraph.find", "find an lgraph, do not create", &Meta_api::find);
    m1.add_label_optional("path","lgraph path");
    m1.add_label_required("name","lgraph name");

    eprp.register_method(m1);

    //---------------------
    Eprp_method m2("lgraph.open", "open an lgraph, create if not found", &Meta_api::open);
    m2.add_label_optional("path","lgraph path");
    m2.add_label_required("name","lgraph name");

    eprp.register_method(m2);

    //---------------------
    Eprp_method m3("lgraph.stats", "print the stats from the passed graphs", &Meta_api::stats);

    eprp.register_method(m3);

    //---------------------
    Eprp_method m4("dump", "dump labels and lgraphs passed", &Meta_api::dump);
    eprp.register_method(m4);
  }

};

