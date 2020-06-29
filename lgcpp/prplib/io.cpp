//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.


#include "lgcpp_plugin.hpp"

static void lgcpp_test(LGraph *lg, const std::shared_ptr<Lgtuple> inp, std::shared_ptr<Lgtuple> out) {
  fmt::print("lgcpp_test called (compile time)\n");

  out->add(lg, Lconst(33));
}

static Lgcpp_plugin sample("lgcpp_test", lgcpp_test);

