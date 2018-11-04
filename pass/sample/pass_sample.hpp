//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_SAMPLE_H
#define PASS_SAMPLE_H

#include "pass.hpp"
#include "options.hpp"
#include "lgraph.hpp"

class Pass_sample_options_pack : public Options_base {
public:
  void set(const std::string &label, const std::string &value) {
    //nothing to do?
  }
};

class Pass_sample : public Pass {
protected:
  Pass_sample_options_pack opack;
public:
  Pass_sample();

  void trans(LGraph *orig) final;
};

#endif
