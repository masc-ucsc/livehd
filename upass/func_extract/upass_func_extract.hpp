//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lnast.hpp"
#include "upass_core.hpp"

struct uPass_func_extract : public upass::uPass {
public:
  uPass_func_extract(std::shared_ptr<upass::Lnast_manager>&);
  uPass_func_extract()           = delete;
  ~uPass_func_extract() override = default;

  void process_assign() override;
  void process_func_def() override;
  void process_stmts() override;
  void process_stmts_post() override;

  upass::Emit_decision                classify_statement() override;
  bool                                overrides_classify_statement() const override { return true; }
  std::vector<std::shared_ptr<Lnast>> take_new_lnasts() override;

private:
  bool drop_current_func_def{false};

  std::vector<std::shared_ptr<Lnast>> extracted_lnasts;
  std::unordered_set<std::string>     extracted_names;

  // Live walk-time tracking: latest trivial-Const value assigned to each
  // outer name. `process_assign` updates this as it walks the outer LNAST
  // in DFS order. When `process_func_def` fires for a comb, this map holds
  // the surrounding scope's view of every previously-assigned scalar —
  // exactly the "constants visible at the definition site" set.
  //
  // We only capture trivial Const RHSes here. Non-trivial bindings (bundles,
  // refs, runtime values) deliberately don't flow into the function — this
  // is the "non-trivial lookups stop at the function boundary" rule.
  std::unordered_map<std::string, Const> latest_outer_value;

  // Names that, at some point during the outer walk, were written under
  // conditions that make them non-constant (a nested-scope assign, a
  // gated/when-style assign, a write whose RHS isn't a trivial Const, or
  // any second write at all). Once a name lands here it cannot be
  // recorded as a definition-site constant later — this enforces the
  // "non-constant bindings stop at the function scope" half of the rule
  // without needing to look at exact control-flow shapes.
  std::unordered_set<std::string> outer_non_const;

  // Stmts-block depth, incremented by process_stmts and decremented by
  // process_stmts_post. Depth == 1 is the outer/function-top stmts; any
  // deeper block is nested. Combined with the gated-if walk-time inspection
  // below to decide whether an assignment is "outer / unconditional".
  int stmts_depth{0};

  static std::string strip_io_prefix(std::string_view name);

  void copy_current_subtree(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent);
  void copy_current_children(const std::shared_ptr<Lnast>& dst, const Lnast_nid& parent);

  bool emit_io_tuple_from_decl(const std::shared_ptr<Lnast>& dst, const Lnast_nid& io_idx);

  // Walks an extracted-function body and records every ref-text it reads.
  // Used to filter latest_outer_value down to just the names the comb body
  // actually consumes, so the inlined-capture prelude stays minimal.
  void collect_body_refs(const std::shared_ptr<Lnast>& body, std::unordered_set<std::string>& refs) const;
};
