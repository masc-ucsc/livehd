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
  uint32_t                              sharing_fanout  = 0;  // experimental; zero keeps unrestricted mapping
  double                                delay_ps        = 0;  // zero: no timing target
  // Physical-only mapping knobs, with pass.abc's meanings and defaults. They
  // buffer and size mapped cells against the delay budget and the partition
  // environment; none restructures the Boolean network the caller hands over.
  uint32_t                              max_fanout      = 16;       // `buffer -N` cap; 0 disables buffering
  bool                                  boundary        = true;     // static + exact boundary re-size
  bool                                  boundary_buffer = true;     // tree region inputs past max_fanout
  std::string                           boundary_drive;             // empty: smallest buffer; `none`: ideal driver
  float                                 io_load          = -1.0f;   // fF on primary outputs; <0: typical input pin
  int                                   boundary_rounds  = 1;       // exact re-size rounds, 1..64
  std::string                           reg_margin       = "auto";  // `auto` or non-negative ps
  uint32_t                              area_relax_pct   = 200;     // slack-to-area `&nf -R` remap cap; 0 disables
  int                                   memory_budget_mb = 16384;   // per-region growth; process ceiling is shared
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
  // Region `delay` rows are picoseconds only under a delay target (the SCL
  // timer of an NLDM Liberty; a scalar Liberty is reported by the provider).
  // Untimed mapping reports unit-delay logic levels, which are not ps.
  bool                         delay_ps = false;
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
