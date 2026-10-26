// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "geometry.hpp"

namespace livehd::placement::buffering {
struct Graph_search_settings {
  Tree_search_settings tree;
  size_t               max_fanout = 4, max_vertices = 100000, max_edges = 200000;
  size_t               max_states = 10000, max_state_vertex_entries = 1000000;
  size_t               max_transitions = 1000000, max_topologies = 10000;
  // One total bound across graph validation, topology DP and all native fits.
  double               max_seconds = 1;
};
struct Graph_solution {
  // Original visibility-edge IDs; choices index the matching rooted tree.
  std::vector<size_t> edges;
  Routing_tree        tree;
  Tree_solution       solution;
};
struct Graph_trial {
  std::vector<size_t>   edges;
  Tree_search_status    status      = Tree_search_status::complete;
  size_t                assignments = 0, evaluated = 0, unsupported = 0;
  bool                  assignment_space_exhausted = false;
  size_t                electrical_rejected = 0, site_rejected = 0, resource_rejected = 0;
  std::optional<double> best_input_margin_ps, best_wire_length_um;
};
enum class Graph_search_status { complete, unbuffered_retained, incomplete_models, budget_exhausted };
struct Graph_search_result {
  Graph_search_status           status                   = Graph_search_status::complete;
  bool                          topology_space_exhausted = false, geometry_disconnected = false;
  size_t                        states = 0, state_vertex_entries = 0, transitions = 0, duplicate_states = 0;
  size_t                        cycle_rejected = 0, duplicate_sink_rejected = 0, waveform_fits = 0;
  size_t                        assignments = 0, evaluated = 0, unsupported = 0, peak_frontier = 0;
  double                        seconds = 0;
  std::vector<std::string>      unsupported_examples;
  std::vector<Graph_trial>      trials;
  std::vector<Graph_solution>   frontier;
  std::optional<Graph_solution> best;
};

// Sink-subset/reachable-set topology DP: extend a rooted partial tree across
// one graph edge or merge branches intersecting ONLY at their common root.
// Every original sink at a visited vertex is retained, including coincidences.
// The complete finite graph search covers all terminal-spanning trees without
// unloaded nonterminal leaves, and all supplied discrete site/cell choices.
// A shortest-path seed supplies early feasible evidence under later bounds.
//
// Cap/Q-only dominance and sampled-slew pruning are NOT applied to partial
// trees: complete trees use native actual-input-slew NLDM/waveforms and the
// shared full electrical/resource frontier. Thus work may be exponential; a
// bound is explicit unfinished search, never an optimum/infeasibility proof.
// This is an exact bounded topology reference using CPB-style extension/merge
// recurrences, not yet scalable CPB/VG/BEN or continuous buffer placement.
// Hard layer/crossing capacity and whole-design timing/routing checks remain.
// No graph edit or shared reservation is committed.
Graph_search_result search_graph(const liberty::timing::Library&, const Context&, const Visibility_graph&,
                                 const std::vector<std::vector<Tree_option>>& vertex_options,
                                 const std::array<double, 4>& directional_rc, const Site_reservations&, const std::string& owner,
                                 const Graph_search_settings& = {});
}  // namespace livehd::placement::buffering
