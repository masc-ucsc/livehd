
#include "pass_dfg.hpp"
#include "main_api.hpp"

class Pass_dfg_api {
protected:
  static void generate(Eprp_var &var) {
    Pass_dfg pass_dfg;
    for(const auto &l:var.dict) {
      pass_dfg.set(l.first,l.second);
    }

    //std::vector<const LGraph *> lgs;
    //for(const auto &lg:var.lgs) {
    //  lgs.push_back(lg);
    //}
    std::vector<const LGraph *> lgs;
    for(const auto &l:var.lgs) {
      lgs.push_back(l);
    }

    if (lgs.empty()) {
      Main_api::warn(fmt::format("pass.dfg.generate needs an input cfg lgraph. Either name or |> from lgraph.open"));
      return;
    }

    pass_dfg.generate_dfg(lgs[0]);

  }

  static void optimize(Eprp_var &var) {
    Pass_dfg pass_dfg;
    /* for(const auto &l:var.dict) { */
    /*   pass_dfg.set(l.first,l.second); */
    /* } */

    //std::vector<const LGraph *> lgs;
    //for(const auto &lg:var.lgs) {
    //  lgs.push_back(lg);
    //}
		//for now, just make dfg setted by src name
    /* std::vector<LGraph *> lgs; */
    /* for(const auto &l:var.lgs) { */
    /*   lgs.push_back(l); */
    /* } */

    /* if (lgs.empty()) { */
    /*   Main_api::warn(fmt::format("pass.dfg.optimize needs an input dfg lgraph. Either name or |> from lgraph.open")); */
    /*   return; */
    /* } */
    pass_dfg.optimize(var.lgs[0]);
  }

public:
  static void setup(Eprp &eprp) {
    Eprp_method m1("pass.dfg.generate", "generate a dfg lgraph from a dfg lgraph", &Pass_dfg_api::generate);
    m1.add_label_optional("path","lgraph path");
    m1.add_label_required("name","lgraph name");
    /* m1.add_label_required("file","src cfg lgraph file"); */

    eprp.register_method(m1);

    Eprp_method m2("pass.dfg.optimize", "optimize a dfg lgraph", &Pass_dfg_api::optimize);
    m2.add_label_optional("path","lgraph path");
    m2.add_label_optional("name","lgraph name");
    /* m2.add_label_required("file","src dfg lgraph file"); */

    eprp.register_method(m2);
  }

};

