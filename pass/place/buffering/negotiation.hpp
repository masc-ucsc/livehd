// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <set>

#include "tree_search.hpp"

namespace livehd::placement::buffering {
enum class Net_class { critical, window, outside_window };
struct Allocation_candidate {
  Candidate                 candidate;
  std::vector<Site_span>    sites;
  // Explicit caller-characterized crossing/other shared integer demand.
  // Missing capacity is unsupported, zero capacity is a hard rejection.
  std::vector<Resource_use> resources;
};
struct Allocation_net {
  std::string                       owner;
  Net_class                         classification = Net_class::critical;
  // Larger dimensionless severity gets priority within a class. A net entering
  // the window must be submitted again with its current class/context.
  double                            severity       = 0;
  std::optional<Context>            context;
  std::vector<Allocation_candidate> candidates;
  bool                              input_search_complete = true;
};
struct Gap_key {
  std::string row;
  uint64_t    first = 0, count = 0;
  auto        operator<=>(const Gap_key&) const = default;
};
struct Congestion_history {
  uint64_t                      geometry_epoch = 0;
  std::map<Gap_key, double>     gaps;
  std::map<std::string, double> resources;
};
struct Allocation_state {
  Site_reservations                                      sites;
  // Includes protected unmanaged owners; all non-site counts are explicit.
  std::map<std::string, std::map<std::string, uint64_t>> resources;
  Congestion_history                                     history;
  explicit Allocation_state(Site_reservations inventory) : sites(std::move(inventory)) {}
};
struct Allocation_settings {
  uint64_t geometry_epoch = 0;
  size_t   max_rounds = 8, max_candidates = 10000, max_waveform_fits = 100000;
  double   max_seconds = 1, dbu_per_um = 1000;
  double   present_price = 1, history_increment = 1, maximum_price = 1000000;
  // Outside-window objective: normalized physical area + this dimensionless
  // congestion charge; physical requirements remain a separate hard gate.
  double   area_normalization_um2 = 1, outside_congestion_weight = 1;
  double   maximum_pull_fraction = .25;
};
struct Gap_entry_pull {
  std::string owner;
  size_t      driver_stage = 0, buffer = 0;
  Point       target;
  double      fraction = 0;
  Net_class   priority = Net_class::critical;
};
struct Net_allocation {
  std::string               owner;
  std::optional<size_t>     candidate;
  std::optional<Evaluation> timing;
  double                    congestion_charge = 0;
  // No admitted unbuffered choice in the supplied set; not a proof about
  // omitted alternatives or whole-net electrical infeasibility.
  bool                      mandatory         = false;
  std::string               reason;
};
enum class Allocation_status { settled, incomplete_models, budget_exhausted };
struct Allocation_round {
  size_t   round = 0, allocated = 0, unmet = 0;
  uint64_t requested_overlap_sites = 0, requested_resource_excess = 0;
};
struct Allocation_result {
  Allocation_status             status = Allocation_status::budget_exhausted;
  Allocation_state              proposal;
  std::vector<Net_allocation>   nets;
  std::vector<Allocation_round> rounds;
  std::vector<Gap_entry_pull>   pulls;
  size_t                        evaluated = 0, waveform_fits = 0, unsupported = 0, rejected = 0;
  size_t                        missing_gap_entries   = 0;
  bool                          input_search_complete = true, history_rebased = false;
  double                        seconds = 0;
  explicit Allocation_result(const Allocation_state& original) : proposal(original) {}
};
// Pure bounded finite-candidate negotiation. Every alternative is freshly
// evaluated in its supplied native source context before admission. Sites and
// explicit shared capacities stay hard even as prices/history grow. Critical/
// window nets precede others; mandatory/unmet nets get priority within a class.
// Critical choices maximize physical input margin before price. Outside-window
// choices minimize normalized priced area, then wire, only after physical
// requirements pass. LR/congestion charges never offset a required time.
//
// Geometry/capacity history is reset on a caller-declared geometry epoch change.
// Only explicitly retired owners or successfully replanned owners are released;
// missing models/options preserve prior reservations and report unmet demand.
// Protected owners are never borrowed. Returned state is a proposal: no input
// state/netlist is mutated. A settled result is stable/feasible over the supplied
// finite alternatives, not an optimum or full-graph timing/routing acceptance.
// Entry pulls use actual rectilinear RC paths; unresolved geometry is explicit.
Allocation_result negotiate(const liberty::timing::Library&, const Allocation_state&, const std::vector<Allocation_net>&,
                            const std::map<std::string, uint64_t>& resource_capacities,
                            const std::set<std::string>&           retired_owners = {}, const Allocation_settings& = {});
}  // namespace livehd::placement::buffering
