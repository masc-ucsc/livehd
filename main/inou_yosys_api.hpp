//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#include "main_api.hpp"
#include "lgraph.hpp"

class Inou_yosys_api {
protected:
  static void tolg(Eprp_var &var) {

    const std::string path    = var.get("path","lgdb");
    const std::string script  = var.get("script");
    const std::string yosys   = var.get("yosys","yosys");
    const std::string techmap = var.get("techmap","alumac");

    const auto &main_path = Main_api::get_main_path();
    std::string liblg = main_path + "/lgraph.runfiles/__main__/inou/yosys/liblgraph_yosys.so";
    if(access(liblg.c_str(), X_OK) == -1) {
      // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
      const std::string liblg2 = main_path + "/../share/lgraph/inou/yosys/liblgraph_yosys.so";
      if(access(liblg2.c_str(), X_OK) == -1) {
        Main_api::error(fmt::format("could not find liblgraph_yosys.so, the {} is not executable", liblg));
        return;
      }
      liblg = liblg2;
    }

    std::string script_file;
    if (script.empty()) {
      script_file = main_path + "/lgraph.runfiles/__main__/main/inou_yosys_read.ys";
      if(access(script_file.c_str(), R_OK) == -1) {
        // Maybe it is installed in /usr/local/bin/lgraph and /usr/local/share/lgraph/inou/yosys/liblgrapth...
        const std::string script_file2 = main_path + "/../share/lgraph/main/inou_yosys_read.ys";
        if(access(script_file2.c_str(), X_OK) == -1) {
          Main_api::error(fmt::format("could not find the default script:{} file", script_file));
          return;
        }
        script_file = script_file2;
      }
    }else{
      if(access(script.c_str(), X_OK) == -1) {
        Main_api::error(fmt::format("could not find the provided script:{} file", script));
        return;
      }
      script_file = script;
    }

    std::string command = fmt::format("CMD: {} -m {} {}", yosys, liblg, script_file);

    std::cout << command << std::endl;

  }

  Inou_yosys_api() {
  }
public:

  static void setup(Eprp &eprp) {
    Eprp_method m1("inou.yosys.tolg", "read verilog using yosys to lgraph", &Inou_yosys_api::tolg);
    m1.add_label_optional("path","path to build the lgraph[s]");
    m1.add_label_optional("techmap","Either full or alumac techmap or none from yosys");
    m1.add_label_optional("script","alternative custom inou_yosys_read.ys command");
    m1.add_label_optional("yosys","path for yosys command");

    eprp.register_method(m1);
  }

};

