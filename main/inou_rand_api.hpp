
#include "inou_rand.hpp"
#include "main_api.hpp"

class Inou_rand_api {
protected:
  static void tolg(Eprp_var &var) {
    Inou_rand rand;

    for(const auto &l:var.dict) {
      rand.set(l.first,l.second);
    }

    std::vector<LGraph *> lgs = rand.tolg();

    if (lgs.empty()) {
      Main_api::warn(fmt::format("inou.rand could not create a random {} lgraph in {} path", var.get("name"), var.get("path")));
    }else{
      assert(lgs.size()==1); // rand only generated one graph at a time
      var.add(lgs[0]);
    }
  }

public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("inou.rand", "generate a random lgraph", &Inou_rand_api::tolg);
    m1.add_label_optional("path","lgraph path");
    m1.add_label_required("name","lgraph name");

    m1.add_label_optional("seed","random seed");
    m1.add_label_optional("size","lgraph size");
    m1.add_label_optional("eratio","edge ratio for random");

    eprp.register_method(m1);
  }

};

