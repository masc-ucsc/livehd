//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <stack>
#include <string>
#include <vector>

#include "const.hpp"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_ntype.hpp"
#include "upass_core.hpp"

struct uPass_runner : public upass::uPass_struct {
public:
  uPass_runner(std::shared_ptr<upass::Lnast_manager>& _lm, const std::vector<std::string>& upass_names,
               upass::Options_map options = {});

  void               run(std::size_t max_iters = 1);
  bool               has_configuration_error() const { return configuration_error; }
  const std::string& get_configuration_error() const { return configuration_error_msg; }

  // Hands the freshly-built staging LNAST to the caller; after this the
  // runner no longer owns it. Call once per run(), after run() returns.
  std::shared_ptr<Lnast>              take_staging() { return std::move(staging); }
  std::vector<std::shared_ptr<Lnast>> take_new_lnasts() override { return std::move(new_lnasts); }

protected:
  struct Pass_entry {
    std::string                   name;
    std::shared_ptr<upass::uPass> pass;
  };

  std::vector<Pass_entry> upasses;
  // Subset of `upasses` whose `overrides_fold_ref()` returned true at
  // construction. try_fold_ref walks this list instead of `upasses` so it
  // skips the virtual no-op fold_ref on every other pass — measurable on
  // bulk-arithmetic workloads where try_fold_ref runs per ref operand.
  std::vector<upass::uPass*> fold_ref_passes;
  // Same idea for classify_statement — only passes that actually return
  // drop on some inputs are polled; the default-no-op passes are skipped.
  std::vector<upass::uPass*> classify_statement_passes;

  // Pre-resolved operand summary for the current source op node. Filled by
  // fill_op_summary() before every drop-candidate / verbatim dispatch and
  // exposed to passes via runner_op_summary_fn. See docs/upass_redesign.md §6.
  upass::Op_summary current_op_summary;
  bool                    configuration_error{false};
  std::string             configuration_error_msg;

  // Staging tree being built during traversal. See upass.md §2.1.
  std::shared_ptr<Lnast>              staging;
  std::stack<Lnast_nid>               staging_parent_stack;
  Lnast_nid                           staging_parent;
  std::vector<std::shared_ptr<Lnast>> new_lnasts;

  std::vector<std::string> resolve_order(const std::vector<std::string>& requested_names, std::string* error_msg = nullptr) const;
  std::vector<std::string> changed_passes() const;

  // Staging emit helpers.
  void emit_push(Lnast_ntype::Lnast_ntype_int type);
  void emit_pop();
  void emit_leaf(Lnast_ntype::Lnast_ntype_int type);
  void emit_leaf(const Lnast_node& node);
  void emit_subtree_verbatim();

  // Returns the first non-nullopt result from any pass's fold_ref(name).
  std::optional<Const> try_fold_ref(std::string_view name);

  // Emits either the folded value of `name` (when any pass returns a valid
  // Const) or the original ref node otherwise. Used by both emit_op_with_fold
  // and the statement-scope ref leaf case.
  void emit_ref_or_folded(std::string_view name);

  // Emits the current op-node and its children into staging. When fold_all is
  // false, the first child (LHS/dst) is copied verbatim and subsequent ref
  // children are fed through fold_ref. When true, every ref child is folded.
  void emit_op_with_fold(bool fold_all);

  // Replays emit_op_with_fold for the op-node at `src` at the current staging
  // position. Saves/restores the read cursor so passes can call this from
  // inside process_* without disturbing the in-progress traversal. Exposed to
  // passes via the Emit_at_fn callback wired in the constructor.
  void emit_op_with_fold_at(const Lnast_nid& src);

  void process_top() override;
  void process_stmts() override;
  void process_if() override;
  void process_lnast();

private:
  using Pass_method = void (upass::uPass::*)();

  // Dispatches `fn` across every registered pass so they can update their
  // internal state (symbol tables, etc.) from the current read cursor. The
  // cursor must be at an op-node and each pass is expected to restore it.
  void dispatch_to_passes(Pass_method fn);

  // Drop-candidate path (category A / B in upass.md §3 Slice 1): dispatch,
  // classify via every pass's classify_statement, emit if no pass drops.
  void process_drop_candidate(Pass_method fn, bool fold_all);
  void process_drop_candidate_verbatim(Pass_method fn);
  bool any_pass_drops() const;

  // Verbatim path (category C): dispatch so passes see the node, then copy
  // the subtree without folding.
  void process_verbatim(Pass_method fn);

  // Walks the current source op node's children once, populating
  // current_op_summary so passes can skip their own scan_op. Cursor returns
  // to the op node before the call returns. Cheap when called on every
  // drop-candidate; passes opt in via runner_op_summary_fn.
  void fill_op_summary();
};
