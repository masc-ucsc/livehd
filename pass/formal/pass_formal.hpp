// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.formal — single-design formal property checks on the cvc5 prover
// (pass/formal/prove.hpp). Distinct from pass.lec (which compares TWO designs):
// pass.formal reasons about ONE design. Always-on in the compile recipe (after
// bitwidth, before cgen); opt out with --set pass.formal.enabled=false. The
// first built-in obligation is Hotmux selector one-hotness; user
// assert/assert_always/assume materialized in tolg are handled here too. The
// add_label_optional registry below IS the pass.formal.* option set. See
// todo/livehd/2f-formal.
class Pass_formal : public Pass {
public:
  explicit Pass_formal(const Eprp_var& var);

  static void setup();
  static void work(Eprp_var& var);
};
