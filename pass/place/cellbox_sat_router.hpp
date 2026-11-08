// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "signal_router.hpp"

namespace livehd::placement {

// Membership is fixed; the solver selects complete legal placements and routes
// jointly. Component indices refer to the small, original-coordinate frame.
struct Cellbox_sat_cell {
  size_t                 component = 0;
  std::vector<Ann_place> poses;
};

// One actual connected immutable incoming conductor component. IDs are only
// provenance: they never electrically join disconnected shapes. Terminal
// shapes must be original source metal, not invented free access points.
struct Cellbox_sat_incoming {
  std::string                       id;
  std::vector<physical::Route_path> paths;
  std::vector<physical::Shape>      terminals;
};

struct Cellbox_sat_net {
  size_t                                     source_net = 0;  // Original complete-source net index, preserved in output.
  // Original instance/pin identities present in the small frame; every local
  // physical pin is required. Zero pins are allowed for an incoming join.
  physical::Net                              local_net;
  bool                                       external = false;
  // Every supplied geometric component is a required root. An external net
  // with no incoming component is invalid, never silently an internal net.
  std::vector<Cellbox_sat_incoming>          incoming;
  std::shared_ptr<const Signal_route_domain> domain;
};

struct Cellbox_sat_problem {
  physical::Rect                bounds;
  std::vector<Cellbox_sat_cell> cells;
  std::vector<Cellbox_sat_net>  nets;
  Signal_route_profile          profile;
};

struct Cellbox_sat_limits {
  std::chrono::steady_clock::time_point deadline;
  size_t   layouts = 32, candidates_per_net_layout = 8, route_trials_per_net_layout = 24, sat_rounds = 256;
  size_t   access_candidates = 2048, route_patterns = 8192;
  size_t   max_variables = 200000, max_clauses = 2000000;
  unsigned sat_conflicts = 100000;
};

enum class Cellbox_sat_status { complete, finite_pool_unsat, unknown, invalid };

struct Cellbox_sat_metrics {
  // Exclusive phases; SAT includes actual native solver checks, not candidate
  // generation or geometry validation. Total includes unsuccessful work.
  double setup_seconds = 0, layout_seconds = 0, candidate_seconds = 0, sat_seconds = 0, verification_seconds = 0;
  double setup_cpu_seconds = 0, layout_cpu_seconds = 0, candidate_cpu_seconds = 0, sat_cpu_seconds = 0,
         verification_cpu_seconds = 0;
  double total_seconds = 0, total_cpu_seconds = 0;
  size_t layouts = 0, route_trials = 0, candidates = 0, sat_calls = 0, variables = 0, clauses = 0;
  size_t layout_sat_calls = 0, route_sat_calls = 0;
  size_t native_rejections = 0, incompatible_pairs = 0, rejected_models = 0;
  bool   layout_limit = false, candidate_limit = false, construction_limit = false, deadline_hit = false;
  bool   generation_time_limit = false;
};

struct Cellbox_sat_net_route {
  size_t                source_net = 0;
  // New metal only, including owned finite incoming extensions and native
  // stack/access vias. Immutable incoming geometry is never copied as output.
  Signal_route_geometry geometry;
};

struct Cellbox_sat_result {
  Cellbox_sat_status                 status = Cellbox_sat_status::unknown;
  std::string                        reason;
  // Populated only for independently validated complete selections. Pose
  // order matches problem.cells; route IDs match the complete problem.nets.
  std::vector<Ann_place>             poses;
  std::vector<Cellbox_sat_net_route> routes;
  Cellbox_sat_metrics                metrics;
  // Even generated-pool UNSAT is not a physical infeasibility certificate.
  bool                               pose_domain_exhausted = false;
};

// The small frame retains original global coordinates/die, native rows/tracks
// and source identities. Background contains all immutable PG/fixed/foreign
// geometry, excluding member cells' old pins/OBS. The solver inserts their
// actual transformed geometry separately for each candidate layout. Incoming
// source geometry and domain ownership are validated, not trusted by ID.
//
// SAT selects a complete layout and compatible complete whole-net alternatives
// bound to that layout. No fixed-placement sequential fallback is used. One
// shared deadline covers setup, generation, selection and final verification.
// On any non-complete status, poses/routes are empty and inputs unchanged.
// Caller still performs whole-design connectivity/geometry checks before an
// atomic placement+metal commit; a local result is not a global certificate.
Cellbox_sat_result solve_cellbox_sat(const physical::Technology&, const physical::Floorplan& local_frame,
                                     const physical::Routing_occupancy& immutable_background, const Cellbox_sat_problem&,
                                     const Cellbox_sat_limits&);

}  // namespace livehd::placement
