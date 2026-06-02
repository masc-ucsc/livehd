//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string_view>

#include "hhds/graph.hpp"
#include "lnast.hpp"

// Terminal LNAST -> LGraph lowering (TODO task 1l, see
// docs/contracts/lnast2lgraph.md).
//
// Lowers ONE post-upass / post-SSA function-tree Lnast into an hhds::Graph
// ready for inou.cgen.verilog. The graph/module name is the LNAST tree name
// (`<file-stem>.<entity>`, e.g. `trivial_if.fun3`). I/O is read from
// lnast->io_meta(); per-name bit/sign ranges from lnast->bw_meta().
//
// The first milestone handles the combinational subset: graph I/O with widths,
// arithmetic/logic/compare ops, constants, get_mask/set_mask bit-slices, and
// if/elif/else -> binary Mux chains. Registers, memories, and submodule calls
// are later phases of task 1l.
//
// run() returns nullptr when the lnast is not a lowerable module (no declared
// I/O in io_meta() — e.g. the empty file-root tree).
struct uPass_tolg {
  static std::shared_ptr<hhds::Graph> run(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path);
};
