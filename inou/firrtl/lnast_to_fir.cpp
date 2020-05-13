//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_firrtl.hpp"

void Inou_firrtl::toFIRRTL(Eprp_var &var) {
  Inou_firrtl p(var);
  for (const auto &lnast : var.lnasts) {
    p.do_tofirrtl(lnast);
  }
}

void Inou_firrtl::do_tofirrtl(std::shared_ptr<Lnast> ln) {

}
