// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "hhds/graph.hpp"
#include "region_qor.hpp"

namespace livehd::synth {

struct Tmap_options {
  std::string                           library;
  double                                delay_ps         = 0;      // zero: no timing target
  int                                   memory_budget_mb = 16384;  // per-region growth; process ceiling is shared
  uint64_t                              time_budget_ms   = 0;
  std::string                           cache_directory;  // internal; empty disables mapped reuse
  std::function<bool(std::string_view)> admission;        // optional cancellation/resource admission
};

struct Mapped_design {
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> top;
  std::vector<Region_qor>      regions;
  bool                         cache_enabled = false;
  uint64_t                     cache_invalid = 0, cache_store_failed = 0;
};

enum class Tmap_status { mapped, unavailable, invalid, refused };
struct Tmap_result {
  Tmap_status                    status = Tmap_status::invalid;
  std::unique_ptr<Mapped_design> design;
  std::string                    reason;
};

// The graph is the complete behavioral expansion, with original state and
// declared ordered ports. Proposed DOMINO boundaries are not mapping barriers.
// The provider must keep source graphs immutable, preserve state/port semantics,
// and return an owned complete result, or no result on failure. Logical cell
// formulas/source correspondence stay in the caller's authoritative artifact.
// No generic synthesis, SATOPT, sequential optimization or ABC fallback is part
// of this interface. Libraries/providers are optional; no provider is linked by
// this target. Graph access and synthesis calls must be serialized by callers.
using Tmap_provider = std::function<Tmap_result(const std::shared_ptr<hhds::Graph>&, const Tmap_options&)>;
// Registration is once per provider name; duplicates are rejected, not replaced.
bool        register_tmap_provider(std::string name, Tmap_provider provider);
bool        has_tmap_provider(std::string_view name);
Tmap_result technology_map(std::string_view provider, const std::shared_ptr<hhds::Graph>& top, const Tmap_options& options);

}  // namespace livehd::synth
