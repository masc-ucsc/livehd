//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>
#include <time.h>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_opentimer.hpp"

//void Pass_opentimer_options::set(const std::string &key, const std::string &value) {
//  try {
//    if ( is_opt(key,"verbose") ) {
//      if (value == "true")
//        verbose = true;
//    } else if ( is_opt(key,"liberty_file") ) {
//        liberty_file = value;
//    } else if ( is_opt(key,"spef_file") ) {
//        spef_file = value;
//    } else if ( is_opt(key,"sdc_file") ) {
//        sdc_file = value;
//    } else{
//        set_val(key,value);
//    }
//  }
//  catch (const std::invalid_argument& ia) {
//    fmt::print("ERROR: key {} has an invalid argument {}\n",key);
//  }
//}

void setup_pass_opentimer() {
  Pass_opentimer p;
  p.setup();
}

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "timing analysis on lgraph", &Pass_opentimer::work);

  m1.add_label_required("src", "source module:net name to tap. E.g: a_module_name:a_instance_name.b_instance_name->a_wire_name");
  m1.add_label_required("dst", "destination module:net name to connect. E.g: a_module_name:c_instance_name.d_instance_name->b_wire_name");

  register_pass(m1);
}

Pass_opentimer::Pass_opentimer()
    : Pass("opentimer") {
}

Pass_opentimer::Pass_opentimer(std::string_view _src, std::string_view _dst)
    : Pass("opentimer") {
  bool ok_src = src_hierarchy.set_hierarchy(_src);
  bool ok_dst = dst_hierarchy.set_hierarchy(_dst);

  if (!ok_src || !ok_dst) {
    Pass::error("Looks like hierarchy syntax is wrong. See this: e.g: a_module_name:a_instance_name.b_instance_name->a_wire_name");
    return;
  }
}

void Pass_opentimer::work(Eprp_var &var) {
  fmt::print("Hello\n");
}
