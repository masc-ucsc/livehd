//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgbench.hpp"

#include "pass_sample.hpp"

Pass_sample::Pass_sample() {
}

void Pass_sample::trans(LGraph *orig) {

  LGBench b("pass.sample");

  int cells = 0;
  for(const auto& idx : orig->forward()) {
    (void)idx;
    cells++;
  }

  fmt::print("Pass: cells {}\n",cells);

}
