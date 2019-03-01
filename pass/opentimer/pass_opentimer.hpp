//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#ifndef PASS_OPENTIMER_H
#define PASS_OPENTIMER_H

#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_opentimer_options : public Options_base {
public:
  Pass_opentimer_options() {
    verbose = false;
  };

  std::string liberty_file;
  std::string spef_file;
  std::string sdc_file;
  bool verbose;

  void set(const std::string &key, const std::string &value);
};

class Pass_opentimer : public Pass {
private:
protected:
  static void optimize(Eprp_var &var);

  void print(LGraph *orig);

public:
  Pass_opentimer();

  void setup() final;
};

#endif

