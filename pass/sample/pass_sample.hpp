//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_SAMPLE_H
#define PASS_SAMPLE_H

#include "pass.hpp"

class Pass_sample : public Pass {
protected:
  static void work(Eprp_var &var);

public:
  Pass_sample();

  void setup() final;

  void do_work(const LGraph *g);
};

#endif
