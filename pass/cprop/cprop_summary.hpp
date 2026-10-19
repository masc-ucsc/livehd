// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// The `cprop-summary` diagnostic: one record per graph body from EACH constant
// propagation engine (pass.cprop and pass.aegraph), so the two are compared with
// the same fields. It is emitted only when `lhd.stats` is on (the kernel passes
// the INTERNAL `stats` label to the step), because collecting it walks the body
// twice.
//
//   graph        the def name
//   nodes_in     body nodes before the engine ran
//   nodes_out    body nodes after the engine (and, for aegraph, its post steps)
//   by_op        "Op=count,..." over the nodes after, sorted by op name
//   ms           wall time of the per-body transform
//   rules_fired  committed aegraph rewrites (rule results applied; 0 for classic, which
//                has no rule counters)
//   creates      body nodes whose class index did not exist before the engine ran
//                (a lower bound: a recycled handle counts as an old node)
//   aeg_bytes    peak capacity of the e-graph tables (0 for classic)
//   lg_bytes     nodes_out * 32 + driven sink edges * 16 (an estimate of the body's footprint)

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace livehd::cprop_summary {

struct Record {
  std::string graph;
  size_t      nodes_in    = 0;
  size_t      nodes_out   = 0;
  std::string by_op;
  double      ms          = 0;
  size_t      rules_fired = 0;
  size_t      creates     = 0;
  size_t      aeg_bytes   = 0;
  size_t      lg_bytes    = 0;
};

// Measures one body transform: construct before the engine runs, call finish()
// after. A disabled probe does nothing and costs nothing.
class Probe {
  bool                                  enabled_;
  absl::flat_hash_set<uint64_t>         before_;
  size_t                                nodes_in_ = 0;
  std::chrono::steady_clock::time_point start_;

public:
  Probe(hhds::Graph& g, bool enabled) : enabled_(enabled) {
    if (!enabled_) {
      return;
    }
    for (auto node : g.body().nodes()) {
      before_.insert(static_cast<uint64_t>(node.get_class_index().value));
      ++nodes_in_;
    }
    start_ = std::chrono::steady_clock::now();
  }

  [[nodiscard]] bool enabled() const { return enabled_; }

  Record finish(hhds::Graph& g, std::string_view name, size_t rules_fired = 0, size_t aeg_bytes = 0) const {
    Record r;
    if (!enabled_) {
      return r;
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
    r.graph       = std::string{name};
    r.nodes_in    = nodes_in_;
    r.rules_fired = rules_fired;
    r.aeg_bytes   = aeg_bytes;
    std::map<std::string, size_t> ops;
    size_t                        edges = 0;
    for (auto node : g.body().nodes()) {
      ++r.nodes_out;
      if (!before_.contains(static_cast<uint64_t>(node.get_class_index().value))) {
        ++r.creates;
      }
      ++ops[std::string{Ntype::get_name(livehd::graph_util::type_op_of(node))}];
      for (auto sink : node.inp_sorted_pins()) {
        for ([[maybe_unused]] auto driver : sink.get_driver_pins()) {
          ++edges;
        }
      }
    }
    for (const auto& [op, n] : ops) {
      r.by_op += (r.by_op.empty() ? "" : ",") + op + "=" + std::to_string(n);
    }
    r.lg_bytes = r.nodes_out * 32 + edges * 16;
    return r;
  }
};

inline void emit(std::string_view pass, const Record& r) {
  livehd::diag::info(pass, "cprop-summary", "progress")
      .msg("`{}`: {} -> {} nodes, {:.3f} ms, {} creates", r.graph, r.nodes_in, r.nodes_out, r.ms, r.creates)
      .attr("graph", r.graph)
      .attr("nodes_in", std::to_string(r.nodes_in))
      .attr("nodes_out", std::to_string(r.nodes_out))
      .attr("by_op", r.by_op)
      .attr("ms", std::format("{:.3f}", r.ms))
      .attr("rules_fired", std::to_string(r.rules_fired))
      .attr("creates", std::to_string(r.creates))
      .attr("aeg_bytes", std::to_string(r.aeg_bytes))
      .attr("lg_bytes", std::to_string(r.lg_bytes))
      .emit();
}

}  // namespace livehd::cprop_summary
