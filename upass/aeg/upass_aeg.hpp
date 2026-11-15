//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The tree aegraph (todo 2a-treeaeg): the aegraph rules on the staged LNAST of
// one unit, after the uPass walk and before the post-walk DCE, so the algebra
// simplifies before lnast.tolg builds the LGraph and less reaches the graph
// engine. Only a body tolg alone reads (a concrete mod or pipe; a comb's body
// is inlined and cached as walked, see uPass_runner::run). Three linear sweeps
// over the staging tree:
//
//   prescan     reads and definitions per name, the value kind (bool, integer)
//               of each name, the names upass.tolg keys a side channel on
//               (memory reads, call results, ...), and the PIN closure: every
//               statement in the cone of a statement whose lowering reads an
//               operand's structural width (red_and, a runtime slice, ...), or
//               that writes an output with no declared width, is left exactly
//               as it is. In a unit whose cycle checks run (they judge the
//               post-step body by its dependencies), so is every statement
//               that computes or reads a value off cycle 0 (a stage, a call
//               result, ...), every `@[N]` cone and each output's chain of
//               copies and slices. The temps the DCE sweeps right after are
//               ignored.
//   walk        one forward pass in tolg's name model: each value statement is
//               imported into an e-graph (Rewriter: fold, hash-cons, the
//               allowlisted simplify rules of aeg_rules.cpp) under a scope model
//               of if arms (an undo log), barriers (unique_if, rolled_for, tick,
//               the default-input prologue) and region colors.
//   write-back  in place only: a reader operand is renamed to an older name of
//               the same value, or replaced by the constant it holds, and a
//               statement whose value a rule rewrote is retyped to the result
//               when that is one statement over named operands. No statement is
//               created or deleted; the definitions left unread are swept by the
//               runner's DCE right after.

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "aeg_map.hpp"
#include "lnast.hpp"
#include "staging_rewrite.hpp"

namespace upass::aeg {

// The step uPass_runner runs (injected by pass.upass).
[[nodiscard]] Staging_rewrite_stats rewrite_staging(const Staging_rewrite_ctx& ctx);

// pass.upass's summary of every unit of one invocation: one always-printed
// `[upass.aeg]` step-log line and, when stats are on, one `treeaeg-rule` record
// per rule that ran (the shape of pass.aegraph's `aegraph-rule` census).
void report(const Staging_rewrite_stats& total);

// What the prescan derived (upass_aeg_test; names resolved to text).
struct Prescan_report {
  struct Name {
    uint32_t reads   = 0;
    uint32_t defs    = 0;
    Kind     kind    = Kind::unknown;
    bool     tmp     = false;
    bool     state   = false;
    bool     special = false;
    bool     clock   = false;
  };
  struct Stmt {
    std::string dst;  // the defined name, "" for none
    std::string op;   // the LNAST node type
    bool        pinned      = false;
    bool        w_seed      = false;
    bool        chain_inner = false;  // its one reader is a same-op, same-region statement
    bool        dead        = false;  // the runner's DCE sweeps it right after the step
  };
  absl::flat_hash_map<std::string, Name> names;
  std::vector<Stmt>                      stmts;  // preorder
  int64_t                                prologue_last = -1;
};
[[nodiscard]] Prescan_report prescan_report(const Lnast& ln, const Lnast_tree_io& io);

// The e-node the import table builds for the unit's first value statement,
// rules off, as `Op(bank:operand,...)`: operands sorted, a leaf spelled as the
// name it reads, a constant as its integer value. upass_aeg_conformance_test
// compares it with the cells lnast.tolg builds for the same statement.
[[nodiscard]] std::string import_view(Lnast& ln, const Lnast_tree_io& io);

}  // namespace upass::aeg
