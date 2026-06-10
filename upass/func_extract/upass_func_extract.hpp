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

  void        process_assign() override;
  upass::Vote process_store(std::string_view dst_name, Bundle& dst, upass::Src_span src) override {
    (void)dst_name;
    (void)dst;
    if (src.size() <= 1) {
      process_assign();
    }
    return upass::Vote::keep;
  }
  void process_func_call() override;
  void process_func_def() override;
  void process_stmts() override;
  void process_stmts_post() override;
  upass::Vote process_tuple_add(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_plus(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_minus(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_mult(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_div(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_bit_and(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_bit_or(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;
  upass::Vote process_bit_xor(std::string_view dst_name, Bundle& dst, upass::Src_span src) override;

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

  // Bundle-valued outer-scope captures: for each outer name that holds a
  // (statically-known) tuple, stores the flat field map (e.g.
  // {"gain": 2, "offset": 5} for `const CFG = (gain=2, offset=5)`). The
  // capture-prelude emitter materializes these as `assign <name>.<field>
  // <const>` statements so callee bodies that do `tuple_get(CFG, gain)`
  // resolve via the same flat-key path the inliner already uses.
  std::unordered_map<std::string, std::unordered_map<std::string, Const>> latest_outer_bundle;

  // Temporary (SSA ___N) value tracking for the outer walk. Captures the
  // values produced by const-folded arithmetic and by tuple_add nodes so
  // that a subsequent `assign <name> ___N` can recover the value. Without
  // this, derived comptime values (`const DERIVED = K + A`) and bundle
  // literals (`const CFG = (gain=2, offset=5)`) — both of which lower to
  // `assign <name> <temp_ref>` in LNAST — can't be captured as outer-scope
  // constants.
  std::unordered_map<std::string, Const>                                  temp_scalar_value;
  std::unordered_map<std::string, std::unordered_map<std::string, Const>> temp_bundle_value;

  // Task 1m — import-binding captures. `fcall(___N, 'import', '<str>')`
  // records ___N → raw import-string const text here; a subsequent
  // unconditional `store NAME ___N` promotes it to latest_outer_import.
  // The capture prelude then REPLICATES the import call at the head of any
  // extracted body that reads NAME (resolution happens during constprop —
  // after this pass — so the value can't be captured; the call can).
  std::unordered_map<std::string, std::string> temp_import_text;
  std::unordered_map<std::string, std::string> latest_outer_import;

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

  // Task 1p — stamp the deferred-template flag when the extracted signature is
  // not fully typed: an untyped non-`self` input, a `...args` var-arg param
  // (default-slot const "..."), or any generic. Walks the freshly-copied io
  // `inputs` tuple_add directly (io_meta is not populated until the SSA upass
  // runs, after this pass). A template emits no LGraph; the concrete form is
  // produced per call site (comb inlines, pipe/mod/fluid specialize).
  void stamp_template_if_untyped(const std::shared_ptr<Lnast>& dst);

  // Walks an extracted-function body and records every ref-text it reads.
  // Used to filter latest_outer_value down to just the names the comb body
  // actually consumes, so the inlined-capture prelude stays minimal.
  void collect_body_refs(const std::shared_ptr<Lnast>& body, std::unordered_set<std::string>& refs) const;
};
