// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

// pass.liberty — companion to pass.abc (task 2a-abc). The `gensim` subcommand
// reads a Liberty file and emits one LGraph behavioral model per combinational
// cell, so the blackbox cell Subs in an ABC-mapped netlist resolve for LEC with
// no PDK-Verilog dependency. Names + pins match the netlist Subs exactly (same
// read_lib-derived Mio library).
class Pass_liberty : public Pass {
public:
  explicit Pass_liberty(const Eprp_var& var);

  static void setup();
  static void gensim(Eprp_var& var);
};
