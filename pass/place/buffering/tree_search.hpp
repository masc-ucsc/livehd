// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <optional>

#include "candidate.hpp"
#include "sites.hpp"

namespace livehd::placement::buffering {
struct Tree_option {
  Buffer    buffer;
  Site_span site;
  // Covers the explicit rectilinear vertex-to-input/output attachment paths.
  // A blocked path cannot be made legal by paying a resource price.
  bool      attachment_routing_allowed = true;
};
struct Tree_vertex {
  Point                    position;
  std::vector<size_t>      sinks;
  std::vector<Tree_option> options;
};
struct Tree_segment {
  size_t parent = 0, child = 0;
  // Wire-only pi segment, with half its capacitance at each end. No pin C.
  double resistance_ohm = 0, capacitance_ff = 0, length_um = 0;
  bool   routing_allowed = true;
};
struct Routing_tree {
  // Vertex zero is the existing driver's physical output. Every other vertex
  // has one parent; every original sink is attached exactly once.
  std::vector<Tree_vertex>  vertices;
  std::vector<Tree_segment> segments;
  // Known technology RC per micron for new rectilinear attachment wires:
  // horizontal R/C, vertical R/C. Zero is an explicitly ideal diagnostic model.
  std::array<double, 4>     attachment_rc{};
};

// choices[v] = 0 leaves v unbuffered; k+1 selects options[k]. Inserting a
// buffer splits electrical stages, preserves each segment's wire C exactly
// once, adds actual input/output attachment RC, and retains ALL original sinks.
// Pure producer: no timing, reservation, graph edit or physical acceptance.
Candidate expand_tree(const Context&, const Routing_tree&, const std::vector<size_t>& choices);

enum class Tree_selection { critical, outside_window };
struct Tree_search_settings {
  Tree_selection selection                     = Tree_selection::critical;
  bool           prefer_unbuffered_if_feasible = false;
  double         dbu_per_um = 1000, max_seconds = 1;
  size_t         max_assignments = 10000, max_frontier = 10000, max_waveform_fits = 100000;
  size_t         max_topology_vertices = 100000;
};
struct Tree_solution {
  std::vector<size_t>    choices;
  Candidate              candidate;
  Evaluation             timing;
  std::vector<Site_span> sites;
  std::optional<double>  worst_input_margin_ps;
};
enum class Tree_search_status { complete, unbuffered_retained, incomplete_models, budget_exhausted };
struct Tree_search_result {
  Tree_search_status           status                     = Tree_search_status::complete;
  bool                         assignment_space_exhausted = false;
  size_t                       assignments = 0, evaluated = 0, unsupported = 0, waveform_fits = 0;
  size_t                       site_rejected = 0, electrical_rejected = 0, resource_rejected = 0, required_rejected = 0;
  size_t                       peak_frontier = 0;
  std::vector<std::string>     unsupported_examples;
  std::vector<Tree_solution>   frontier;
  // Can contain the last evaluated feasible candidate even when frontier
  // storage is exhausted. Neither a partial frontier nor best proves optimum.
  std::optional<Tree_solution> best;
};

// Shared comparison of fully evaluated root solutions; never valid for
// partial subtrees whose actual upstream slew/load context is still unknown.
bool dominates_tree_solution(const Tree_solution&, const Tree_solution&, const Context&);
bool prefer_tree_solution(const Tree_solution&, const Tree_solution&, Tree_selection);

// Bounded exact expansion of the finite supplied site/cell assignments. Native
// NLDM/GaTe and per-sink waveforms use ACTUAL upstream slew at each full trial.
// No C/Q-only pruning of partial trees and no input-slew discretization occur.
// The root frontier compares physical required times, all original sink
// arrival/slew states, LR cost, area, wirelength, symbolic demand and exact site
// footprints. Distinct competing-site choices remain incomparable.
// Outside-window selection requires the caller's physical requirements to
// encode the window boundary, and rejects candidates which cross it.
// No reservation is committed. Whole-graph timing/rank/coverage must still be
// verified before materialization. This is not yet a scalable van Ginneken,
// Hanan/BEN search, routed calibration, or an unrestricted optimum.
Tree_search_result search_tree(const liberty::timing::Library&, const Context&, const Routing_tree&, const Site_reservations&,
                               const std::string& owner, const Tree_search_settings& = {});
}  // namespace livehd::placement::buffering
