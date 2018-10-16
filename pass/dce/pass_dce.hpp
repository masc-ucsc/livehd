//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef PASS_DCE_H
#define PASS_DCE_H

#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_dce_options_pack : public Options_base {
public:
  void set(const std::string &label, const std::string &value) {
    //nothing to do
  }
};

class Pass_dce : public Pass {
private:
protected:
  std::string dce_type;

  Pass_dce_options_pack opack;

public:
  Pass_dce();

  void trans(LGraph *orig) final;

  // regenerate, creates a new lgraph db
  LGraph *regen(const LGraph *orig) {
    //only inplace available
    assert(false);
  }

  // no options needed
  void set(const std::string &key, const std::string &value) { }
};

#endif

