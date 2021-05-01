//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "opt_lnast.hpp"
#include "lnast.hpp"

Opt_lnast::Opt_lnast(const Eprp_var &var) {
  (void)var;
  // could check for options
}

void Opt_lnast::opt(std::shared_ptr<Lnast> ln) {

  for (auto &it : ln->depth_postorder()) {
    auto &data = ln->get_data(it);
    fmt::print("lnast:{}\n", data.type.debug_name());
  }

}

