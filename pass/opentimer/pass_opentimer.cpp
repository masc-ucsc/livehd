//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <time.h>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_opentimer.hpp"

void Pass_opentimer_options::set(const std::string &key, const std::string &value) {
  try {
    if ( is_opt(key,"verbose") ) {
      if (value == "true")
        verbose = true;
    } else if ( is_opt(key,"liberty_file") ) {
        liberty_file = value;
    } else if ( is_opt(key,"spef_file") ) {
        spef_file = value;
    } else if ( is_opt(key,"sdc_file") ) {
        sdc_file = value;
    } else{
        set_val(key,value);
    }
  }
  catch (const std::invalid_argument& ia) {
    fmt::print("ERROR: key {} has an invalid argument {}\n",key);
  }
}

void setup_pass_opentimer() {
  Pass_opentimer p;
  p.setup();
}

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "optimize an lgraph with a opentimer, gen _mapped", &Pass_opentimer::optimize);

  register_pass(m1);
}

Pass_opentimer::Pass_opentimer()
    : Pass("opentimer") {
}

void Pass_opentimer::optimize(Eprp_var &var) {
  Pass_opentimer pass;

  for(auto &l : var.lgs) {
    pass.print(l);
  }
}

void Pass_opentimer::print(LGraph *g) {

  fmt::print("Hello\n");
}
