//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

#include "upass_core.hpp"

// uPass_semacheck — semantic-legality checks on the tree closest to user
// source. These are the user-facing checks formerly embedded in
// pass/lnastfmt, relocated so lnastfmt can be a pure compiler-internal
// structural validator (and so the LSP pipeline needs no lnastfmt stage):
//
//   * write to a read-only / derived attribute — `:[bits=…]`, `:[max=…]`,
//     `:[size/sign/key=…]` via attr_set. This is the ONLY enforcement site
//     in the compiler (bits/size/sign/key are derived views; max/min are
//     settable only through a type).
//   * declare-once — redeclaring a name in the same scope is an error.
//   * shadowing backstop — prp2lnast enforces no-shadowing first (with a
//     sharper span); this recheck only fires if a later stage synthesizes a
//     shadowing declare (i.e. a compiler bug surfaced as a user diagnostic).
//
// Runs ONCE per LNAST from begin_iteration — before the walk starts, so the
// source tree is exactly what prp2lnast produced (post decl-merge, pre any
// staging rewrite). Read-only; emits located core/diag records (the
// declare/attr_set loc-carry chain supplies the spans). Scope rules mirror
// Pyrope's closure rule: a func_def body is a fresh namespace.
struct uPass_semacheck : public upass::uPass {
public:
  using upass::uPass::uPass;

  void begin_iteration() override;

private:
  bool ran_ = false;

  void check_attr_writes(const Lnast* ln);
  void check_scope(const Lnast* ln, const Lnast_nid& scope_stmts, const std::unordered_set<std::string>& visible);

  // Located error: span from `nid`'s loc when the node carries one (declare /
  // attr_set via the loc-carry chain), else a null span (single-line render).
  void emit_sema_error(std::string_view code, std::string_view category, const std::string& msg, std::string_view hint,
                       const Lnast* ln, const Lnast_nid& nid);
};
