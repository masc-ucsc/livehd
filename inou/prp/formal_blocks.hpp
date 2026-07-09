// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// 2f-verify V2: extraction of `formal name.dotted { ... }` blocks from a .prp
// file. A formal block is a declarative verification overlay: its body is
// ordinary Pyrope statement syntax (alias bindings + assert/assume/
// assert_always calls whose expressions read design signals through dotted
// instance paths), every claim holds at EVERY cycle, and it never lowers to
// hardware — prp2lnast skips it; only `lhd formal` consumes it, through this
// extractor.
//
// The extractor does NOT evaluate anything. It rewrites each property
// statement's dotted signal paths (`acc.bar.xxx`) into generated input
// identifiers (`__p_bar_xxx`) and reports the referenced paths, so the driver
// can compile the statements into a tiny comb MONITOR module (real Pyrope
// semantics end to end — no hand-rolled expression evaluator to diverge) whose
// typed inputs the BMC engine binds per cycle to the design's encoded signals.
//
// V2 body subset (anything else = a per-block `error` naming the construct):
//   const X = import("file.mod")   // bind an alias to the verified design
//   mut   X = <alias-or-module>    // secondary alias (test-block style)
//   assert(expr[, "msg"]) / assume(...) / assert_always(...)
// where `expr` uses dotted paths rooted at an alias, literals, and operators —
// but no nested function calls (V2).

#include <string>
#include <vector>

namespace livehd::formal_blocks {

struct Input {
  std::string ident;  // generated monitor input identifier (__p_<sanitized path>)
  std::string path;   // dotted signal path INSIDE the design (alias stripped)
};

struct Stmt {
  std::string text;  // property statement with signal paths rewritten to idents
  int         line = 0;  // 1-based line in the ORIGINAL file (loc remap)
};

struct Block {
  std::string        name;    // dotted block name (the --formal filter handle)
  std::string        target;  // module the alias chain resolves to ("" = the verified top)
  std::vector<Input> inputs;  // referenced signals, deduped by ident
  std::vector<Stmt>  stmts;   // property statements, source order
  std::string        error;   // non-empty: first unsupported construct ("file:line: why")
  int                line = 0;  // block's own 1-based line (diagnostics)
};

// Parse `path` and return every formal block in it (empty vector when none).
// A file-level parse error is reported as ONE Block whose `error` is set (so
// the caller has a uniform reporting path).
std::vector<Block> extract(const std::string& path);

}  // namespace livehd::formal_blocks
