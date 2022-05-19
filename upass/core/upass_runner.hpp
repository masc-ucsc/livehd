//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "upass_core.hpp"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "lnast_writer.hpp"

namespace upass {

class uPass_runner : Lnast_traversal {
public:
  uPass_runner(const std::shared_ptr<Lnast>& ln, const std::vector<std::string> upass_names)
    : Lnast_traversal(ln), wr(std::cout, ln) {
    auto upass_registry = uPass_plugin::get_registry();
    // for (const auto& [key, value] : upass_registry) {
    //   fmt::print("uPass - {}\n", key);
    // }
    for (const auto& name : upass_names) {
      if (upass_registry.count(name)) {
        fmt::print("uPass - add {}\n", name);
        upasses.push_back(upass_registry[name](ln));
      } else {
        fmt::print("{} is not defined.\n", name);
      }
    }
    fmt::print("Lnast : {}\n", ln->get_top_module_name());
  }

  void run() {
    move_to_nid(Lnast_nid::root());
    process_lnast();
  }

protected:
  std::vector<std::shared_ptr<uPass>> upasses;
  Lnast_writer wr;

  void write_node();

  void process_lnast();
  void process_top();
  void process_stmts();

};

}
