//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_assert.hpp"

#include "lnast_ntype.hpp"

// Registered once here (not in the header) to avoid duplicate-registration
// errors when multiple TUs include upass_assert.hpp.
static upass::uPass_plugin plugin_assert("assert", upass::uPass_wrapper<uPass_assert>::get_upass, {"constprop"});

uPass_assert::uPass_assert(std::shared_ptr<upass::Lnast_manager> &_lm) : uPass(_lm) {
  st.function_scope(_lm->get_top_module_name());
}

void uPass_assert::process_func_call() {
  move_to_child();
  move_to_sibling();  // skip return value
  if (current_text() == "cassert") {
    move_to_sibling();  // advance to the assertion argument
    const auto val = current_prim_value();
    // Only flag if the value is concretely known to be false.
    // An invalid/unknown Lconst (val.is_invalid()) means the argument is not
    // constpropagated yet — we cannot statically decide, so do not throw.
    if (!val.is_invalid() && val.is_known_false()) {
      upass::error("assert: cassert condition is statically false at '{}'\n",
                   current_text());
    }
    // If the value is not known-false (unknown or provably true), silently pass.
  }
  move_to_parent();
}
