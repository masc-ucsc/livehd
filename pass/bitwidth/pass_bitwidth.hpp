//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_BITWIDTH_H
#define PASS_BITWIDTH_H

#include "pass.hpp"
#include "options.hpp"
#include "lgraph.hpp"

#include <string>

class Pass_bitwidth_options_pack : public Options_base {
public:
  void set(const std::string &label, const std::string &value) {
    //nothing to do?
  }
};

class Pass_bitwidth : public Pass {
protected:
  Pass_bitwidth_options_pack opack;
public:
  Pass_bitwidth();

  void trans(LGraph *orig) final;

  LGraph *regen(const LGraph *orig) {
    assert(false);
  }

  //no options needed?
  void set(const std::string &key, const std::string &value) { }
};

#endif
