//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_assert.hpp"
#include "lnast_ntype.hpp"

uPass_assert::uPass_assert(std::shared_ptr<upass::Lnast_manager> &_lm) : uPass(_lm) {}

void uPass_assert::process_func_call() {
  move_to_child();
  move_to_sibling(); // skip return value
  if (current_text() == "cassert") {
    move_to_sibling();
    // TODO: Check value
  }
  move_to_parent();
}
