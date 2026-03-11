//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <print>
#include <string_view>

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

inline void run_noop_shared(const IR_adapter& adapter, std::string_view tag) {
  std::print("{} - shared noop on {}\n", tag, adapter.kind());
}

inline Shared_scan_report run_scan_shared(const IR_adapter& adapter, std::string_view tag) {
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

inline Shared_decision_report compute_decide_shared(const IR_adapter& adapter) {
  return Shared_decision_report{
      .fold_candidate_count = adapter.fold_candidate_count(),
      .has_fold_candidates  = adapter.fold_candidate_count() > 0,
  };
}

inline Shared_decision_report run_decide_shared(const IR_adapter& adapter, std::string_view tag) {
  const auto report = compute_decide_shared(adapter);
  std::print("{} - shared decide on {} fold_candidates:{} has_fold_candidates:{}\n",
             tag,
             adapter.kind(),
             report.fold_candidate_count,
             report.has_fold_candidates ? "true" : "false");
  return report;
}

inline Shared_fold_sum_const_report run_fold_sum_const_shared(IR_adapter& adapter, std::string_view tag, bool dry_run = false) {
  Shared_fold_sum_const_report report;
  const auto                   nodes = adapter.list_nodes();
  for (const auto node : nodes) {
    if (adapter.op_name(node) != "sum") {
      continue;
    }

    const auto inps = adapter.inputs(node);
    if (inps.size() != 2) {
      continue;
    }

    const auto c0 = adapter.const_value(inps[0]);
    const auto c1 = adapter.const_value(inps[1]);
    if (!c0 || !c1) {
      continue;
    }

    const auto effect = adapter.estimate_replace_with_const(node);
    const auto folded = *c0 + *c1;
    if (!dry_run) {
      if (!adapter.replace_with_const(node, folded)) {
        continue;
      }
    }

    ++report.folded_nodes;
    report.rewired_edges += effect.rewired_edges;
    report.new_const_nodes += effect.new_const_nodes;
    report.deleted_nodes += effect.deleted_nodes;
  }

  std::print("{} - shared sum_const on {} folded:{} dry_run:{}\n",
             tag,
             adapter.kind(),
             report.folded_nodes,
             dry_run ? "true" : "false");

  return report;
}

}  // namespace upass
