//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "lnast.hpp"

// Terminal LNAST -> LGraph lowering.
//
// Lowers ONE post-upass / post-SSA function-tree Lnast into an hhds::Graph
// ready for inou.cgen.verilog. The graph/module name is the LNAST tree name
// (`<file-stem>.<entity>`, e.g. `trivial_if.fun3`). I/O is read from
// lnast->io_meta(); per-name bit/sign ranges from lnast->bw_meta().
//
// Handles the combinational subset (graph I/O with widths, arithmetic/logic/
// compare ops, constants, get_mask/set_mask bit-slices, if -> Mux chains),
// the pipeline regs (declare(reg)+stages -> depth-parameterized
// Flop), and pipe/mod call sites lowered to Ntype_op::Sub
// instances (callee resolved through `registry`, the same var.lnasts list the
// runner's inliner uses; the callee's GraphIO must already exist, which is
// what the two-phase register_io()-then-run() protocol guarantees).
//
// run() returns nullptr when the lnast is not a lowerable module (no declared
// I/O in io_meta() — e.g. the empty file-root tree).
struct uPass_tolg {
  using Registry = std::vector<std::shared_ptr<Lnast>>;

  // Phase 1 — declare the module's GraphIO in the library (ports + bits +
  // sign + the implicit clock when the tree, or transitively any callee,
  // holds state). Idempotent. MUST run for every lnast before any run() so
  // Sub instances can bind callee GraphIOs regardless of build order
  // (mirrors the yosys two-pass build).
  static void register_io(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path, const Registry& registry);

  // Phase 0 — cross-unit `lg="name"` (2f-lg) collision check. Two units that
  // resolve to the same effective graph name (an lg override colliding with
  // another lg name or a default `file.entity`) would silently share one
  // GraphIO (find_io reuses it) and emit a broken double-driven module. Fatal
  // diagnostic on collision. MUST run before the register_io() loop. Idempotent
  // and cheap (linear scan); callable from any tolg orchestration site.
  static void detect_lg_collisions(const Registry& registry);

  // Phase 2 — build the body graph. `reset_style` is the elaboration
  // flag (`upass.reset_style=sync|async`, default sync — target-dependent,
  // FPGA-typical): it decides whether implicit-reset flops tie their `async`
  // pin. A per-reg `:[sync=…]` attr beats the flag.
  static std::shared_ptr<hhds::Graph> run(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path,
                                          const Registry& registry, std::string_view reset_style = "sync");
};
