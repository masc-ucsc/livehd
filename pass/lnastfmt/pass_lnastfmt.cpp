//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lnastfmt.hpp"

Pass_lnastfmt::Pass_lnastfmt(const Eprp_var &var) : Pass("Pass.lnastfmt", var) {}

void Pass_lnastfmt::setup() {
  Eprp_method m1("Pass.lnastfmt", "Formats LNAST: remove SSA and recreate tuples", &Pass_lnastfmt::fmt_begin);
  m1.add_label_optional("odir", "path to put the LNAST", ".");
  register_pass(m1);
}


