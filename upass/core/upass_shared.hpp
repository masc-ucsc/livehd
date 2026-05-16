//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <print>
#include <string_view>
#include <vector>

#include "const.hpp"
#include "ir_adapter.hpp"

namespace upass {

struct Shared_scan_report {
  std::size_t node_count{0};
  std::size_t const_count{0};
  std::size_t arithmetic_count{0};
};

struct Shared_decision_report {
  std::size_t fold_candidate_count{0};
  bool        has_fold_candidates{false};
};

struct Shared_fold_sum_const_report {
  std::size_t folded_nodes{0};
  std::size_t rewired_edges{0};
  std::size_t new_const_nodes{0};
  std::size_t deleted_nodes{0};
};

// Shared passes are templated over the concrete IR manager (Lgraph_manager or
// Lnast_manager). Each manager exposes:
//   • kind()                                — IR identity, for logging
//   • node_count() / const_count() / …      — read-only summaries
//   • using Node, Input                     — handle types (hhds::Node_class
//                                             + hhds::Pin_class for Lgraph,
//                                             Lnast_nid for both on Lnast)
//   • for_each_node(fn) / for_each_input    — streaming iteration with native
//                                             iterators (no Node_id encoding)
//   • is_sum_op / is_const / const_value    — typed predicates over Node/Input
//   • estimate_replace_with_const / replace_with_const
// Concrete handles flow through these passes — there is no IR_adapter virtual
// base anymore, and no opaque Node_id integers.

template <typename Adapter>
inline void run_noop_shared(const Adapter& adapter, std::string_view tag) {
  std::print("{} - shared noop on {}\n", tag, adapter.kind());
}

template <typename Adapter>
inline Shared_scan_report run_scan_shared(const Adapter& adapter, std::string_view tag) {
  Shared_scan_report report{
      .node_count       = adapter.node_count(),
      .const_count      = adapter.const_count(),
      .arithmetic_count = adapter.arithmetic_count(),
  };
  std::print("{} - shared scan on {} nodes:{} const:{} arith:{}\n",
             tag,
             adapter.kind(),
             report.node_count,
             report.const_count,
             report.arithmetic_count);
  return report;
}

template <typename Adapter>
inline Shared_decision_report compute_decide_shared(const Adapter& adapter) {
  return Shared_decision_report{
      .fold_candidate_count = adapter.fold_candidate_count(),
      .has_fold_candidates  = adapter.fold_candidate_count() > 0,
  };
}

template <typename Adapter>
inline Shared_decision_report run_decide_shared(const Adapter& adapter, std::string_view tag) {
  const auto report = compute_decide_shared(adapter);
  std::print("{} - shared decide on {} fold_candidates:{} has_fold_candidates:{}\n",
             tag,
             adapter.kind(),
             report.fold_candidate_count,
             report.has_fold_candidates ? "true" : "false");
  return report;
}

template <typename Adapter>
inline Shared_fold_sum_const_report run_fold_sum_const_shared(Adapter& adapter, std::string_view tag, bool dry_run = false) {
  Shared_fold_sum_const_report report;

  // Single streaming pass — no intermediate vector. The adapter walks its
  // native iterator (lg->fast() / lnast->depth_preorder()) and invokes the
  // lambda inline. replace_with_const may delete the current node, but
  // lg->fast()'s fast_next is robust to that (it scans forward to the next
  // live master_root), and lnast's depth_preorder only rewrites data in
  // place, so direct mutation during traversal is safe.
  adapter.for_each_node([&](const typename Adapter::Node& node) {
    if (!adapter.is_sum_op(node)) {
      return;
    }

    bool               empty     = true;
    bool               all_const = true;
    std::vector<Const> const_vals;
    adapter.for_each_input(node, [&](const typename Adapter::Input& inp) {
      empty = false;
      if (!all_const) {
        return;
      }
      auto cv = adapter.const_value(inp);
      if (!cv) {
        all_const = false;
        return;
      }
      const_vals.emplace_back(std::move(*cv));
    });
    // N-ary fold: handle any number of inputs ≥ 1; all must be compile-time constants.
    if (empty || !all_const) {
      return;
    }

    // Reduce-fold: accumulate sum across all constant inputs.
    Const folded = const_vals[0];
    for (std::size_t i = 1; i < const_vals.size(); ++i) {
      folded = folded.add_op(const_vals[i]);
    }

    const auto effect = adapter.estimate_replace_with_const(node);
    if (!dry_run) {
      if (!adapter.replace_with_const(node, folded)) {
        return;
      }
    }

    ++report.folded_nodes;
    report.rewired_edges += effect.rewired_edges;
    report.new_const_nodes += effect.new_const_nodes;
    report.deleted_nodes += effect.deleted_nodes;
  });

  std::print("{} - shared sum_const on {} folded:{} dry_run:{}\n",
             tag,
             adapter.kind(),
             report.folded_nodes,
             dry_run ? "true" : "false");

  return report;
}

}  // namespace upass
