//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgcpp_plugin.hpp"

static void lgcpp_test(LGraph *lg, const std::shared_ptr<Lgtuple> inp, std::shared_ptr<Lgtuple> out) {
  fmt::print("lgcpp_test called (compile time)\n");

  (void) inp;
  out = std::make_shared<Lgtuple>("lgcc_var");
  auto dpin = lg->create_node_const(33).get_driver_pin();
  out->add(dpin);
}

static Lgcpp_plugin sample("lgcpp_test", lgcpp_test);
