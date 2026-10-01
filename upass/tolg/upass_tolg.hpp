//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
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
  // (mirrors the yosys two-pass build). The GraphIO ends up declaring exactly
  // this module's ports, as in a fresh library: a port an earlier compile left
  // in a reused lg: library, or an absorbed lg: module the source redefines,
  // is dropped; the gid and an unchanged leading run of ports are kept.
  static void register_io(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path, const Registry& registry);

  // Phase 0 — cross-unit `lg="name"` (2f-lg) collision check. Two units that
  // resolve to the same effective graph name (an lg override colliding with
  // another lg name or a default `file.entity`) would silently share one
  // GraphIO (find_io reuses it) and emit a broken double-driven module. Fatal
  // diagnostic on collision. MUST run before the register_io() loop. Idempotent
  // and cheap (linear scan); callable from any tolg orchestration site.
  //
  // It also STARTS the lowering pass: it records every unit of `registry` that
  // run() will lower (all but restored ones) as pending, and run() retires each
  // one once its body is final. run()'s combinational-loop checks read a
  // child's comb reach (the record run() leaves on every body it finishes, or
  // the body itself) only when the child is not pending, so a driver that calls
  // run() without this call first inherits the previous pass's pending set.
  static void detect_lg_collisions(const Registry& registry);

  // register_io refuses (`stale-instance`) an interface change that would
  // silently rewire a module it does not rebuild: one the library already
  // holds, that no unit of `registry` owns (`unit` or `unit.<x>`), and whose
  // Sub binds a connected port id that now names another port.
  //
  // `units` = this compile scope's PREVIOUS generation (the compile cache
  // manifest). A module only such a unit owns is a leftover of a unit the edit
  // dropped from the import closure, which the kernel prunes right after
  // lowering unless something live still instantiates it. Its stale instance
  // is therefore deferred: check_leftover_instances, run after the prune,
  // refuses it only when the module survived. Call after detect_lg_collisions
  // (which clears the set and the deferred refusals) and before register_io.
  static void set_prior_units(const std::vector<std::string>& units);
  static void check_leftover_instances();

  // Where the library's modules came from, for the `stale-instance` hint:
  // `input_dirs` are the lg: INPUT dirs absorbed into the working library, and
  // `absorbed` maps a module whose body one of them supplied to that dir; any
  // other module was already in the working library, which the user knows as
  // `working_lib` ("" when it is internal scratch). Optional -- without it the
  // hint names both kinds of dir. Call after detect_lg_collisions (which
  // clears it), like set_prior_units.
  static void set_library_origins(std::string working_lib, std::vector<std::string> input_dirs,
                                  std::vector<std::pair<std::string, std::string>> absorbed);

  // `registry` reordered callee-first (a DFS post-order over the call graph;
  // units sharing a graph name stay together, in registry order). Lowering in
  // this order gives every caller the finished bodies of its children, which
  // run()'s combinational-loop checks read through (a caller ring through a
  // child's register is sequential, a ring through a Mealy output is a loop).
  // A child lowered later (another order, an instantiation cycle) is still
  // checked soundly, as an instance whose every input feeds every output.
  // `follow` (optional) restricts the edges to the callees it accepts; the
  // rest keep their registry order.
  static Registry lowering_order(const Registry& registry, const std::function<bool(const Lnast&)>& follow = {});

  // Phase 3: refresh Sub loop-break classification bottom-up and gate
  // conditionally activated instance clocks after every phase-2 body exists.
  // Both classifications are structural and recursive, so they cannot be
  // performed safely while calls are lowered in arbitrary definition order.
  static void gate_activation_clocks(const std::vector<std::shared_ptr<hhds::Graph>>& graphs);

  // Phase 2 — build the body graph. `reset_style` is the elaboration
  // flag (`upass.reset_style=sync|async`, default sync — target-dependent,
  // FPGA-typical): it decides whether implicit-reset flops tie their `async`
  // pin. A per-reg `:[sync=…]` attr beats the flag.
  static std::shared_ptr<hhds::Graph> run(const std::shared_ptr<Lnast>& lnast, std::string_view lib_path, const Registry& registry,
                                          std::string_view reset_style = "sync");
};
