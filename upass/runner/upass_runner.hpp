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

#include "lconst.hpp"
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
  std::shared_ptr<Lnast> take_staging() { return std::move(staging); }

protected:
  struct Pass_entry {
    std::string                   name;
    std::shared_ptr<upass::uPass> pass;
  };

  std::vector<Pass_entry> upasses;
  bool                    configuration_error{false};
  std::string             configuration_error_msg;

  // Staging tree being built during traversal. See upass.md §2.1.
  std::shared_ptr<Lnast> staging;
  std::stack<Lnast_nid>  staging_parent_stack;
  Lnast_nid              staging_parent;

  std::vector<std::string> resolve_order(const std::vector<std::string>& requested_names, std::string* error_msg = nullptr) const;
  std::vector<std::string> changed_passes() const;

  // Staging emit helpers.
  void emit_push(const Lnast_node& node);
  void emit_pop();
  void emit_leaf(const Lnast_node& node);
  void emit_subtree_verbatim();

  // Returns the first non-nullopt result from any pass's fold_ref(name).
  std::optional<Lconst> try_fold_ref(std::string_view name);

  // Emits the current op-node and its children into staging. When fold_all is
  // false, the first child (LHS/dst) is copied verbatim and subsequent ref
  // children are fed through fold_ref. When true, every ref child is folded.
  void emit_op_with_fold(bool fold_all);

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

  // Verbatim path (category C): dispatch so passes see the node, then copy
  // the subtree without folding.
  void process_verbatim(Pass_method fn);
};
