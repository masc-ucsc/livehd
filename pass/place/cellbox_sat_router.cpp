// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cellbox_sat_router.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>
#include <ctime>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

#include "inou/lefdef/cut_rules.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/signal_via.hpp"
#include "sat_solver.hpp"

namespace livehd::placement {
namespace ph = physical;
namespace {
using Clock = std::chrono::steady_clock;

double thread_cpu() {
  timespec t{};
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) {
    throw std::runtime_error("cellbox solver cannot read thread CPU time");
  }
  return double(t.tv_sec) + double(t.tv_nsec) * 1e-9;
}

struct Phase_time {
  double&           wall;
  double&           cpu;
  const double*     nested_wall       = nullptr;
  const double*     nested_cpu        = nullptr;
  double            nested_wall_start = nested_wall ? *nested_wall : 0;
  double            nested_cpu_start  = nested_cpu ? *nested_cpu : 0;
  Clock::time_point start             = Clock::now();
  double            cpu_start         = thread_cpu();
  ~Phase_time() {
    wall += std::max(
        0.0,
        std::chrono::duration<double>(Clock::now() - start).count() - (nested_wall ? *nested_wall - nested_wall_start : 0));
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0) {
      cpu += std::max(0.0,
                      double(t.tv_sec) + double(t.tv_nsec) * 1e-9 - cpu_start - (nested_cpu ? *nested_cpu - nested_cpu_start : 0));
    }
  }
};

struct Search_limit : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// This model selects a whole layout and complete owner routes. Native checks
// supply incompatibilities only after both complete geometries are fixed.
class Choice_model {
  bool                      layout_model_;
  const Cellbox_sat_limits& limits_;
  Cellbox_sat_metrics&      metrics_;

public:
  Sat_solver sat{Sat_backend::cadical};
  Choice_model(const Cellbox_sat_limits& limits, Cellbox_sat_metrics& metrics, bool layout_model = false)
      : layout_model_(layout_model), limits_(limits), metrics_(metrics) {}
  int variable() {
    if (metrics_.variables >= limits_.max_variables || metrics_.variables >= size_t(INT_MAX)) {
      metrics_.construction_limit = true;
      throw Search_limit("SAT variable construction limit");
    }
    ++metrics_.variables;
    return sat.new_variable();
  }
  void clause(std::span<const int> literals) {
    if (metrics_.clauses >= limits_.max_clauses) {
      metrics_.construction_limit = true;
      throw Search_limit("SAT clause construction limit");
    }
    ++metrics_.clauses;
    sat.clause(literals);
  }
  void clause(std::initializer_list<int> literals) { clause(std::span<const int>(literals.begin(), literals.size())); }
  void at_most_one(const std::vector<int>& literals) {
    if (literals.size() < 2) {
      return;
    }
    int previous = variable();
    clause({-literals.front(), previous});
    for (size_t i = 1; i + 1 < literals.size(); ++i) {
      const int current = variable();
      clause({-literals[i], current});
      clause({-previous, current});
      clause({-literals[i], -previous});
      previous = current;
    }
    clause({-literals.back(), -previous});
  }
  Sat_status solve(std::span<const int> assumptions = {}, Clock::time_point phase_deadline = Clock::time_point::max()) {
    const auto now  = Clock::now();
    const auto stop = std::min(limits_.deadline, phase_deadline);
    if (now >= stop) {
      metrics_.deadline_hit |= now >= limits_.deadline;
      return Sat_status::unknown;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(stop - now).count();
    if (remaining <= 0) {
      metrics_.deadline_hit |= now >= limits_.deadline;
      return Sat_status::unknown;
    }
    const auto milliseconds = unsigned(std::min<int64_t>(remaining, std::numeric_limits<unsigned>::max()));
    Phase_time timer{metrics_.sat_seconds, metrics_.sat_cpu_seconds};
    ++metrics_.sat_calls;
    ++(layout_model_ ? metrics_.layout_sat_calls : metrics_.route_sat_calls);
    const auto status = sat.solve(assumptions, limits_.sat_conflicts, milliseconds);
    if (Clock::now() >= limits_.deadline) {
      metrics_.deadline_hit = true;
      return Sat_status::unknown;
    }
    return status;
  }
};

bool valid_rect(const ph::Rect& r) { return ph::integral_dbu(r) && !r.empty(); }

// Exact rectangular set coverage. A hull or an incoming component label does
// not establish ownership of missing metal between actual source pieces.
bool covered_by(const ph::Shape& seed, std::span<const ph::Shape> metal) {
  if (!valid_rect(seed.rect)) {
    return false;
  }
  std::vector<double> xs{seed.rect.xl, seed.rect.xh};
  for (const auto& shape : metal) {
    if (shape.layer == seed.layer && shape.rect.overlaps(seed.rect)) {
      xs.push_back(std::max(seed.rect.xl, shape.rect.xl));
      xs.push_back(std::min(seed.rect.xh, shape.rect.xh));
    }
  }
  std::ranges::sort(xs);
  xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  for (size_t i = 1; i < xs.size(); ++i) {
    std::vector<std::pair<double, double>> intervals;
    for (const auto& shape : metal) {
      if (shape.layer == seed.layer && shape.rect.xl <= xs[i - 1] && shape.rect.xh >= xs[i] && shape.rect.yh > seed.rect.yl
          && shape.rect.yl < seed.rect.yh) {
        intervals.emplace_back(std::max(seed.rect.yl, shape.rect.yl), std::min(seed.rect.yh, shape.rect.yh));
      }
    }
    std::ranges::sort(intervals);
    double reached = seed.rect.yl;
    for (const auto& [lo, hi] : intervals) {
      if (lo > reached) {
        return false;
      }
      reached = std::max(reached, hi);
    }
    if (reached < seed.rect.yh) {
      return false;
    }
  }
  return true;
}

ph::Rect body(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Component& cell) {
  const auto& macro = tech.macros.at(cell.macro);
  return ph::placed_rect(macro,
                         cell.place,
                         frame.dbu,
                         {-macro.origin_x, -macro.origin_y, macro.width - macro.origin_x, macro.height - macro.origin_y});
}

std::vector<Signal_route_via> vias_of(std::span<const ph::Route_path> paths) {
  std::vector<Signal_route_via>                                        vias;
  std::set<std::tuple<std::string, ph::Orientation, int64_t, int64_t>> seen;
  for (const auto& path : paths) {
    std::optional<ph::Route_point> point;
    for (const auto& step : path.steps) {
      if (const auto* p = std::get_if<ph::Route_point>(&step)) {
        point = *p;
      } else if (const auto* v = std::get_if<ph::Route_via>(&step)) {
        if (!point) {
          throw std::invalid_argument("via without native route point");
        }
        if (seen.emplace(v->name, v->orientation, point->x, point->y).second) {
          vias.push_back({*v, point->x, point->y});
        }
      }
    }
  }
  return vias;
}

std::vector<ph::Shape> path_shapes(const ph::Technology& tech, const ph::Floorplan& frame, const std::string& owner,
                                   const std::vector<ph::Route_path>& paths, const ph::Route_width_rules* rules = nullptr) {
  ph::Net net{owner, {}, {{"ROUTED", paths}}, "SIGNAL"};
  return ph::route_geometry(tech, frame, net, false, rules);
}

std::vector<std::vector<ph::Shape>> pin_unions(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Net& net) {
  std::vector<std::vector<ph::Shape>> out;
  for (const auto& connection : net.connections) {
    const auto cell = std::ranges::find(frame.components, connection.instance, &ph::Component::name);
    if (cell == frame.components.end()) {
      throw std::invalid_argument("unknown local physical endpoint");
    }
    const auto& macro  = tech.macros.at(cell->macro);
    const auto& pin    = macro.pins.at(connection.pin);
    auto&       shapes = out.emplace_back();
    for (const auto& shape : pin.shapes) {
      shapes.push_back({shape.layer, ph::placed_rect(macro, cell->place, frame.dbu, shape.rect)});
    }
    if (shapes.empty()) {
      throw std::invalid_argument("local physical pin has no native shapes");
    }
  }
  return out;
}

struct Candidate {
  size_t                 layout = 0, net = 0;
  Signal_route_geometry  geometry;
  std::vector<ph::Shape> owner_shapes;
};

struct Layout {
  std::vector<bool>                                                 empty_tried;
  std::vector<size_t>                                               calls;
  std::vector<size_t>                                               next_trial, span_count;
  std::vector<bool>                                                 span_exhausted;
  std::vector<std::set<std::tuple<size_t, size_t, size_t, size_t>>> tried;
  std::vector<Ann_place>                                            poses;
  std::shared_ptr<ph::Floorplan>                                    frame;
  std::shared_ptr<const ph::Routing_occupancy>                      occupancy;
  std::vector<std::vector<ph::Shape>>                               source_groups;
  std::map<size_t, size_t>                                          occupancy_groups;
  std::vector<std::vector<size_t>>                                  candidates;
};

// Mirror only native source enumeration, not ownership or obstacle admission.
// Check every resulting rectangle against routing_occupancy below, so a future
// native projection change cannot silently lose a source shape or group it with
// a different physical pin. Unowned OBS are grouped per member and layer.
std::vector<size_t> member_source_groups(const ph::Technology& tech, const ph::Floorplan& frame,
                                         const ph::Routing_occupancy& dynamic, std::vector<std::vector<ph::Shape>>& groups) {
  std::vector<ph::Shape> projected;
  std::vector<size_t>    ids;
  for (const auto& cell : frame.components) {
    const auto&                   macro = tech.macros.at(cell.macro);
    std::map<std::string, size_t> obstruction_groups;
    const auto                    new_group = [&] {
      groups.emplace_back();
      return groups.size() - 1;
    };
    const auto obs_group = [&](const std::string& layer) {
      const auto found = obstruction_groups.find(layer);
      if (found != obstruction_groups.end()) {
        return found->second;
      }
      const auto id = new_group();
      obstruction_groups.emplace(layer, id);
      return id;
    };
    const auto append = [&](const ph::Shape& source, size_t id) {
      if (!tech.layers.contains(source.layer) && !tech.cut_layers.contains(source.layer)) {
        return;
      }
      const auto r = ph::placed_rect(macro, cell.place, frame.dbu, source.rect);
      ph::Shape  shape{
          source.layer,
          {std::floor(r.xl + 1e-9), std::floor(r.yl + 1e-9), std::ceil(r.xh - 1e-9), std::ceil(r.yh - 1e-9)}
      };
      groups.at(id).push_back(shape);
      projected.push_back(std::move(shape));
      ids.push_back(id);
    };
    const auto body_shape = [&](const std::string& layer, size_t id) {
      append(
          {
              layer,
              {-macro.origin_x, -macro.origin_y, macro.width - macro.origin_x, macro.height - macro.origin_y}
      },
          id);
    };
    if (macro.virtual_proxy) {
      for (const auto& [layer, rule] : tech.layers) {
        body_shape(layer, obs_group(layer));
      }
      for (const auto& [layer, rule] : tech.cut_layers) {
        body_shape(layer, obs_group(layer));
      }
      continue;
    }
    for (const auto& [name, pin] : macro.pins) {
      const auto id = new_group();
      for (const auto& shape : pin.shapes) {
        if (shape.rect.empty()) {
          body_shape(shape.layer, id);
        } else {
          append(shape, id);
        }
      }
    }
    for (const auto& shape : macro.obstructions) {
      append(shape, obs_group(shape.layer));
    }
    for (const auto& layer : macro.conservative_obstructions) {
      body_shape(layer, obs_group(layer));
    }
    for (const auto& layer : macro.blocked_layers) {
      if (!macro.conservative_obstructions.contains(layer)
          && std::ranges::none_of(macro.obstructions, [&](const auto& shape) { return shape.layer == layer; })) {
        body_shape(layer, obs_group(layer));
      }
    }
  }
  if (projected.size() != dynamic.size()) {
    throw std::invalid_argument("native source grouping inventory mismatch");
  }
  for (size_t i = 0; i < projected.size(); ++i) {
    if (projected[i] != dynamic.at(i).shape) {
      throw std::invalid_argument("native source grouping geometry mismatch");
    }
  }
  return ids;
}

std::span<const ph::Shape> source_context(const Layout& layout, size_t occupancy_id) {
  const auto found = layout.occupancy_groups.find(occupancy_id);
  return found == layout.occupancy_groups.end() ? std::span<const ph::Shape>{} : layout.source_groups.at(found->second);
}

class Search {
  size_t                                   selection_rounds_ = 0;
  Clock::time_point                        started_          = Clock::now();
  const ph::Technology&                    tech_;
  const ph::Floorplan&                     original_;
  const ph::Routing_occupancy&             background_;
  const Cellbox_sat_problem&               problem_;
  const Cellbox_sat_limits&                limits_;
  Cellbox_sat_result                       result_;
  ph::Signal_metal_rules                   rules_;
  std::set<std::string>                    supply_nets_, supply_pin_layers_;
  std::vector<Layout>                      layouts_;
  std::vector<Candidate>                   candidates_;
  std::vector<std::vector<ph::Route_path>> incoming_paths_;
  std::vector<std::vector<ph::Shape>>      incoming_metal_;
  std::set<size_t>                         members_;

  void checkpoint() {
    if (Clock::now() >= limits_.deadline) {
      result_.metrics.deadline_hit = true;
      throw Search_limit("cellbox deadline");
    }
  }

  // These are subdivisions of the caller's unchanged absolute deadline, not
  // extra effort. Leave the final quarter for SAT and native verification;
  // reserve the final tenth specifically for validating a selected model.
  Clock::time_point generation_stop() const { return limits_.deadline - (limits_.deadline - started_) / 4; }
  Clock::time_point solve_stop() const { return limits_.deadline - (limits_.deadline - started_) / 10; }
  Clock::time_point layout_stop() const { return started_ + (limits_.deadline - started_) / 5; }
  Clock::time_point proposal_stop(size_t remaining_nets) const {
    const auto now       = Clock::now();
    const auto remaining = generation_stop() - now;
    const auto slice     = std::min((limits_.deadline - started_) / 8, remaining / int64_t(std::max(size_t{2}, remaining_nets)));
    return std::min(generation_stop(), now + slice);
  }

  bool legal_pose(size_t component, const Ann_place& pose) const {
    const auto& cell  = original_.components.at(component);
    const auto& macro = tech_.macros.at(cell.macro);
    if (!macro.klass.starts_with("CORE") || macro.virtual_proxy || !ph::orientation_allowed(macro, pose.orientation)
        || pose.status != ph::Status::PLACED || pose.width != ph::exact_units(macro.width, original_.dbu)
        || pose.height != ph::exact_units(macro.height, original_.dbu)) {
      return false;
    }
    auto placed  = cell;
    placed.place = pose;
    const auto b = body(tech_, original_, placed);
    if (!valid_rect(b) || !problem_.bounds.contains(b) || !original_.die.contains(b)) {
      return false;
    }
    bool row_ok = false;
    for (const auto& row : original_.rows) {
      const auto site = tech_.sites.find(row.site);
      if (site == tech_.sites.end() || row.site != macro.site || row.count <= 0 || row.step <= 0 || row.y != pose.y) {
        continue;
      }
      const auto height = ph::exact_units(site->second.height, original_.dbu);
      const auto width  = ph::exact_units(site->second.width, original_.dbu);
      if (row.orientation != ph::Orientation::N && row.orientation != ph::Orientation::FN && row.orientation != ph::Orientation::FS
          && row.orientation != ph::Orientation::S) {
        continue;
      }
      const bool lower  = row.orientation == ph::Orientation::N || row.orientation == ph::Orientation::FN;
      const bool orient = lower ? (pose.orientation == ph::Orientation::N || pose.orientation == ph::Orientation::FN)
                                : (pose.orientation == ph::Orientation::FS || pose.orientation == ph::Orientation::S);
      if (height != pose.height || width <= 0 || row.step != width || !orient || pose.x < row.x || (pose.x - row.x) % row.step
          || row.count > INT32_MAX / row.step) {
        continue;
      }
      row_ok |= b.xh <= double(row.x) + double(row.count) * row.step;
    }
    if (!row_ok) {
      return false;
    }
    for (size_t i = 0; i < original_.components.size(); ++i) {
      if (!members_.contains(i) && b.overlaps(body(tech_, original_, original_.components[i]))) {
        return false;
      }
    }
    for (const auto& blockage : original_.blockages) {
      if (!blockage.soft && std::ranges::any_of(blockage.rectangles, [&](const auto& r) { return b.overlaps(r); })) {
        return false;
      }
    }
    return true;
  }

  void input() {
    checkpoint();
    if (original_.dbu <= 0 || !valid_rect(problem_.bounds) || !original_.die.contains(problem_.bounds) || !limits_.layouts
        || !limits_.candidates_per_net_layout || !limits_.route_trials_per_net_layout || !limits_.sat_rounds
        || !limits_.access_candidates || !limits_.route_patterns || !limits_.sat_conflicts || !limits_.max_variables
        || limits_.max_variables > size_t(INT_MAX) || !limits_.max_clauses) {
      throw std::invalid_argument("invalid cellbox bounds or limits");
    }
    for (const auto& cell : problem_.cells) {
      checkpoint();
      if (cell.component >= original_.components.size() || !members_.insert(cell.component).second || cell.poses.empty()) {
        throw std::invalid_argument("missing or duplicate cell pose domain");
      }
      const auto status = original_.components[cell.component].place.status;
      if (status == ph::Status::FIXED || status == ph::Status::LOCKED) {
        throw std::invalid_argument("fixed component included in movable box membership");
      }
    }
    std::set<std::string>                         names;
    std::set<size_t>                              source_ids;
    std::set<std::pair<std::string, std::string>> owned_pins;
    for (const auto& net : problem_.nets) {
      checkpoint();
      if (!source_ids.insert(net.source_net).second || net.local_net.name.empty() || !names.insert(net.local_net.name).second
          || !net.local_net.wires.empty() || !net.domain || net.domain->owner != net.local_net.name
          || (net.external && net.incoming.empty()) || (!net.external && !net.incoming.empty())
          || (net.local_net.connections.empty() && net.incoming.empty())) {
        throw std::invalid_argument("invalid net identity, source scope or incoming obligation");
      }
      const auto source = std::ranges::find(original_.nets, net.local_net.name, &ph::Net::name);
      if (source == original_.nets.end()) {
        throw std::invalid_argument("local net absent from source frame");
      }
      std::set<std::pair<std::string, std::string>> required, supplied;
      for (const auto& connection : source->connections) {
        const auto cell = std::ranges::find(original_.components, connection.instance, &ph::Component::name);
        if (cell != original_.components.end() && members_.contains(size_t(cell - original_.components.begin()))) {
          required.emplace(connection.instance, connection.pin);
        }
      }
      for (const auto& connection : net.local_net.connections) {
        if (connection.instance == "PIN" || !supplied.emplace(connection.instance, connection.pin).second
            || !owned_pins.emplace(connection.instance, connection.pin).second) {
          throw std::invalid_argument("invented, duplicated or multiply owned local pin");
        }
      }
      if (required != supplied) {
        throw std::invalid_argument("local physical-pin obligations differ from source membership");
      }
      std::set<std::string>                         component_ids;
      auto&                                         paths = incoming_paths_.emplace_back();
      auto&                                         metal = incoming_metal_.emplace_back();
      std::map<std::string, std::vector<ph::Shape>> admitted_components;
      for (const auto& incoming : net.incoming) {
        checkpoint();
        if (incoming.id.empty() || !component_ids.insert(incoming.id).second) {
          throw std::invalid_argument("duplicate incoming provenance component");
        }
        const auto connected = ph::check_conductor_connectivity(tech_, original_, incoming.paths, incoming.terminals, {});
        if (!connected.connected) {
          throw std::invalid_argument("incoming component is not physically connected: " + connected.reason);
        }
        const auto decoded = path_shapes(tech_, original_, net.local_net.name, incoming.paths);
        paths.insert(paths.end(), incoming.paths.begin(), incoming.paths.end());
        auto& admitted = admitted_components[incoming.id];
        admitted.insert(admitted.end(), decoded.begin(), decoded.end());
        admitted.insert(admitted.end(), incoming.terminals.begin(), incoming.terminals.end());
        metal.insert(metal.end(), decoded.begin(), decoded.end());
        metal.insert(metal.end(), incoming.terminals.begin(), incoming.terminals.end());
      }
      for (const auto& carrier : net.domain->carriers) {
        checkpoint();
        const auto group = admitted_components.find(carrier.component);
        if (carrier.owner != net.local_net.name || group == admitted_components.end() || !covered_by(carrier.seed, group->second)) {
          throw std::invalid_argument("carrier seed is not actual metal of its named incoming component");
        }
      }
      if (auto why = validate_signal_route_domain(tech_, original_, net.local_net, {}, *net.domain); !why.empty()) {
        throw std::invalid_argument("invalid incoming/reserved domain: " + why);
      }
    }
  }

  bool prepare(Layout& layout) {
    checkpoint();
    auto frame = std::make_shared<ph::Floorplan>(original_);
    for (size_t i = 0; i < problem_.cells.size(); ++i) {
      frame->components[problem_.cells[i].component].place = layout.poses[i];
    }
    auto member_frame = *frame;
    member_frame.components.clear();
    for (const auto& member : problem_.cells) {
      member_frame.components.push_back(frame->components[member.component]);
    }
    for (auto& net : member_frame.nets) {
      net.wires.clear();
    }
    for (auto& net : member_frame.special_nets) {
      net.wires.clear();
    }
    member_frame.terminals.clear();
    member_frame.blockages.clear();
    member_frame.snapshot_blockages.clear();
    const auto dynamic = ph::routing_occupancy(tech_, member_frame, 256, [&] { checkpoint(); }, nullptr, ph::Signal_wires::exclude);
    const auto group_ids = member_source_groups(tech_, member_frame, dynamic, layout.source_groups);
    auto       base      = std::shared_ptr<const ph::Routing_occupancy>(&background_, [](const auto*) {});
    auto       occupancy = std::make_shared<ph::Routing_occupancy>(std::move(base));
    // Immutable incoming roots also obstruct moved foreign member geometry.
    for (size_t n = 0; n < problem_.nets.size(); ++n) {
      for (const auto& shape : incoming_metal_[n]) {
        occupancy->insert({shape, problem_.nets[n].local_net.name, true});
      }
    }
    for (size_t i = 0; i < dynamic.size(); ++i) {
      checkpoint();
      const auto item = dynamic.at(i);
      if (!frame->die.contains(item.shape.rect)) {
        return false;
      }
      const auto group      = group_ids[i];
      const auto same_group = [&](size_t id) {
        const auto found = layout.occupancy_groups.find(id);
        return found != layout.occupancy_groups.end() && found->second == group;
      };
      if (tech_.layers.contains(item.shape.layer)) {
        const auto clearance = rules_.clearance(item.shape.layer);
        if (!clearance) {
          return false;
        }
        for (auto other : occupancy->query(item.shape.layer, item.shape.rect, *clearance, item.net)) {
          const auto& existing = occupancy->at(other);
          // Standard-cell geometry on legal rows/sites is clean by library construction: on the layers of the cells'
          // own power pins (M1 in asap7) a member's pin/OBS shapes are not checked against other cells' pin/OBS shapes
          // (abutment) nor against the supply rails (each cell's rail continues there). Routed signal metal, and every
          // other layer, still is.
          if (supply_pin_layers_.contains(item.shape.layer) && (!existing.routed || supply_nets_.contains(existing.net))) {
            continue;
          }
          if (!same_group(other)
              && !rules_
                      .spacing_with_union(item.shape,
                                          existing.shape,
                                          layout.source_groups[group],
                                          source_context(layout, other))
                      .legal) {
            return false;
          }
        }
      } else if (const auto cut = tech_.cut_layers.find(item.shape.layer); cut != tech_.cut_layers.end()) {
        const auto clearance = double(ph::signal_cut_spacing(cut->second, original_.dbu));
        // A member obstruction blocks cuts of every owner. It is not an
        // admitted same-net via that can be reused by logical net identity.
        for (auto other : occupancy->query(item.shape.layer, item.shape.rect, clearance, {})) {
          if (!same_group(other) && ph::violates_cut_spacing(item.shape.rect, occupancy->at(other).shape.rect, clearance)) {
            return false;
          }
        }
      }
      layout.occupancy_groups.emplace(occupancy->insert(item), group);
    }
    layout.frame     = std::move(frame);
    layout.occupancy = std::move(occupancy);
    layout.candidates.resize(problem_.nets.size());
    layout.next_trial.resize(problem_.nets.size());
    layout.span_count.resize(problem_.nets.size());
    layout.span_exhausted.resize(problem_.nets.size());
    layout.tried.resize(problem_.nets.size());
    layout.calls.resize(problem_.nets.size());
    layout.empty_tried.resize(problem_.nets.size());
    return true;
  }

  void layouts() {
    std::vector<std::vector<Ann_place>> poses;
    for (const auto& member : problem_.cells) {
      auto& allowed = poses.emplace_back();
      for (const auto& pose : member.poses) {
        checkpoint();
        if (legal_pose(member.component, pose) && std::ranges::find(allowed, pose) == allowed.end()) {
          allowed.push_back(pose);
        }
      }
      if (allowed.empty()) {
        throw Search_limit("no admitted pose in supplied native domain");
      }
      const auto& instance = original_.components[member.component];
      const auto& macro    = tech_.macros.at(instance.macro);
      const auto  score    = [&](const Ann_place& pose) {
        double alignment = 0;
        for (const auto& net : problem_.nets) {
          for (const auto& connection : net.local_net.connections) {
            if (connection.instance != instance.name) {
              continue;
            }
            double pin_distance = std::numeric_limits<double>::infinity();
            for (const auto& shape : macro.pins.at(connection.pin).shapes) {
              const auto rectangle = ph::placed_rect(macro, pose, original_.dbu, shape.rect);
              for (const auto& region : net.domain->regions) {
                if (const auto hs = region.tracks.find(problem_.profile.access.escape); hs != region.tracks.end()) {
                  for (auto y : hs->second) {
                    pin_distance = std::min(pin_distance, std::max({rectangle.yl - y, y - rectangle.yh, 0.0}));
                  }
                }
              }
              for (const auto& carrier : net.domain->carriers) {
                if (carrier.seed.layer == problem_.profile.access.escape) {
                  const double y = (carrier.seed.rect.yl + carrier.seed.rect.yh) / 2;
                  pin_distance   = std::min(pin_distance, std::max({rectangle.yl - y, y - rectangle.yh, 0.0}));
                }
              }
            }
            alignment += std::isfinite(pin_distance) ? pin_distance : 1e9;
          }
        }
        return std::tuple{alignment,
                          std::abs(double(pose.x) - instance.place.x) + std::abs(double(pose.y) - instance.place.y),
                          pose.y,
                          pose.x,
                          pose.orientation};
      };
      std::ranges::stable_sort(allowed, [&](const auto& a, const auto& b) { return score(a) < score(b); });
    }
    Choice_model                       placement(limits_, result_.metrics, true);
    std::vector<std::vector<int>>      choices(poses.size());
    std::vector<std::vector<ph::Rect>> bodies(poses.size());
    for (size_t cell = 0; cell < poses.size(); ++cell) {
      for (const auto& pose : poses[cell]) {
        checkpoint();
        choices[cell].push_back(placement.variable());
        auto instance  = original_.components[problem_.cells[cell].component];
        instance.place = pose;
        bodies[cell].push_back(body(tech_, original_, instance));
      }
      placement.clause(choices[cell]);
      placement.at_most_one(choices[cell]);
      placement.sat.prefer(choices[cell].front());
    }
    for (size_t a = 0; a < poses.size(); ++a) {
      for (size_t b = a + 1; b < poses.size(); ++b) {
        for (size_t i = 0; i < poses[a].size(); ++i) {
          for (size_t j = 0; j < poses[b].size(); ++j) {
            checkpoint();
            if (bodies[a][i].overlaps(bodies[b][j])) {
              placement.clause({-choices[a][i], -choices[b][j]});
            }
          }
        }
      }
    }
    for (size_t attempt = 0; attempt < limits_.layouts; ++attempt) {
      std::vector<int> assumptions;
      if (attempt && !poses.empty()) {
        const size_t                           cell = (attempt - 1) % poses.size();
        // Rotate the forced member and the row before revisiting shifts in the
        // same row; 32 models must not merely wiggle the last DFS variable.
        std::vector<size_t>                    diverse;
        std::map<int64_t, std::vector<size_t>> by_row;
        for (size_t p = 0; p < poses[cell].size(); ++p) {
          by_row[poses[cell][p].y].push_back(p);
        }
        for (size_t depth = 0; diverse.size() < poses[cell].size(); ++depth) {
          for (const auto& [row, alternatives] : by_row) {
            if (depth < alternatives.size()) {
              diverse.push_back(alternatives[depth]);
            }
          }
        }
        const size_t rank = (1 + (attempt - 1) / poses.size()) % diverse.size();
        assumptions.push_back(choices[cell][diverse[rank]]);
      }
      auto status = placement.solve(assumptions, layout_stop());
      if (status == Sat_status::unsatisfiable && !assumptions.empty()) {
        status = placement.solve({}, layout_stop());
      }
      if (status == Sat_status::unknown) {
        checkpoint();
        break;  // Preserve previously admitted layouts, without claiming exhaustion.
      }
      if (status == Sat_status::unsatisfiable) {
        result_.pose_domain_exhausted = true;
        break;
      }
      Layout           layout;
      std::vector<int> no_good;
      for (size_t cell = 0; cell < poses.size(); ++cell) {
        std::optional<size_t> selected;
        for (size_t p = 0; p < poses[cell].size(); ++p) {
          if (placement.sat.value(choices[cell][p])) {
            if (selected) {
              throw std::runtime_error("layout SAT selected two poses");
            }
            selected = p;
          }
        }
        if (!selected) {
          throw std::runtime_error("layout SAT omitted member including zero-signal cell");
        }
        layout.poses.push_back(poses[cell][*selected]);
        no_good.push_back(-choices[cell][*selected]);
      }
      placement.clause(no_good);
      if (prepare(layout)) {
        layouts_.push_back(std::move(layout));
      } else {
        ++result_.metrics.native_rejections;
      }
    }
    result_.metrics.layout_limit = !result_.pose_domain_exhausted;
    result_.metrics.layouts      = layouts_.size();
  }

  std::vector<ph::Shape> fixed_terminals(size_t n) const {
    std::vector<ph::Shape> out;
    for (const auto& component : problem_.nets[n].incoming) {
      out.insert(out.end(), component.terminals.begin(), component.terminals.end());
    }
    return out;
  }

  bool native_candidate(size_t lid, size_t n, const Signal_route_geometry& geometry, std::vector<ph::Shape>& owner_shapes,
                        std::string& reason) {
    checkpoint();
    const auto& layout = layouts_[lid];
    const auto& frame  = *layout.frame;
    const auto& input  = problem_.nets[n];
    for (const auto& [name, widths] : geometry.nondefault_rules) {
      if (name.empty() || widths.empty()) {
        reason = "empty native width rule";
        return false;
      }
      if (const auto existing = frame.nondefault_rules.find(name);
          existing != frame.nondefault_rules.end() && existing->second != widths) {
        reason = "conflicting immutable width rule";
        return false;
      }
      for (const auto& [layer, width] : widths) {
        const auto legal = rules_.candidate_widths(layer);
        if (width <= 0 || std::ranges::find(legal, width) == legal.end()) {
          reason = "width rule not derived from native policy";
          return false;
        }
      }
    }
    if (auto why = validate_signal_route_domain(tech_,
                                                frame,
                                                input.local_net,
                                                geometry.paths,
                                                *input.domain,
                                                &geometry.nondefault_rules,
                                                nullptr,
                                                incoming_paths_[n]);
        !why.empty()) {
      reason = "route domain: " + why;
      return false;
    }
    auto paths = incoming_paths_[n];
    paths.insert(paths.end(), geometry.paths.begin(), geometry.paths.end());
    const auto pins  = pin_unions(tech_, frame, input.local_net);
    const auto fixed = fixed_terminals(n);
    if (const auto check = ph::check_conductor_connectivity(tech_, frame, paths, fixed, pins, &geometry.nondefault_rules);
        !check.connected) {
      reason = "incomplete physical obligations: " + check.reason;
      return false;
    }
    auto all = path_shapes(tech_, frame, input.local_net.name, paths, &geometry.nondefault_rules);
    all.insert(all.end(), fixed.begin(), fixed.end());
    for (const auto& pin : pins) {
      all.insert(all.end(), pin.begin(), pin.end());
    }
    owner_shapes              = ph::coalesce_route_metal(tech_, all);
    const auto complete_union = owner_shapes;
    std::erase_if(owner_shapes, [&](const auto& shape) {
      return tech_.layers.contains(shape.layer) && std::ranges::any_of(complete_union, [&](const auto& containing) {
               return containing.layer == shape.layer && containing.rect != shape.rect && containing.rect.contains(shape.rect);
             });
    });  // Exact set preservation: a contained plate creates no extra EOL.
    const auto added = path_shapes(tech_, frame, input.local_net.name, geometry.paths, &geometry.nondefault_rules);
    for (const auto& shape : owner_shapes) {
      checkpoint();
      if (!tech_.layers.contains(shape.layer)
          || !std::ranges::any_of(added, [&](const auto& s) { return s.layer == shape.layer && shape.rect.overlaps(s.rect); })) {
        continue;
      }
      const bool native_pin = std::ranges::any_of(pins, [&](const auto& pin) {
        return std::ranges::any_of(pin, [&](const auto& p) { return p.layer == shape.layer && p.rect.contains(shape.rect); });
      });
      if ((!native_pin && !rules_.rectangle(shape).legal) || !frame.die.contains(shape.rect)) {
        reason = "native completed-owner rectangle";
        return false;
      }
      const auto clearance = rules_.clearance(shape.layer);
      if (!clearance) {
        reason = "unsupported native owner clearance";
        return false;
      }
      for (auto id : layout.occupancy->query(shape.layer, shape.rect, *clearance, input.local_net.name)) {
        if (!rules_.spacing_with_union(shape, layout.occupancy->at(id).shape, owner_shapes, source_context(layout, id)).legal) {
          reason = "native completed-owner spacing";
          return false;
        }
      }
    }
    ph::Signal_cut_set cuts(tech_, frame);
    for (const auto& via : vias_of(paths)) {
      checkpoint();
      if (!cuts.add(via.via, via.x, via.y)) {
        reason = "native same-owner cut spacing";
        return false;
      }
    }
    const auto retained_vias = vias_of(incoming_paths_[n]);
    for (const auto& via : vias_of(geometry.paths)) {
      if (std::ranges::find(retained_vias, via) != retained_vias.end()) {
        continue;
      }
      std::array<ph::Shape, 2> landings;
      size_t                   count = 0;
      for (auto plate : ph::via_geometry(tech_, frame, via.via)) {
        if (!tech_.layers.contains(plate.layer)) {
          continue;
        }
        if (count >= landings.size()) {
          reason = "unsupported native via stack";
          return false;
        }
        plate.rect               = plate.rect.translated(double(via.x), double(via.y));
        const ph::Shape* landing = nullptr;
        for (const auto& shape : owner_shapes) {
          if (shape.layer == plate.layer && shape.rect.contains(plate.rect)
              && (!landing
                  || (shape.rect.xh - shape.rect.xl) * (shape.rect.yh - shape.rect.yl)
                         < (landing->rect.xh - landing->rect.xl) * (landing->rect.yh - landing->rect.yl))) {
            landing = &shape;
          }
        }
        if (!landing) {
          reason = "native via lacks actual owner landing";
          return false;
        }
        landings[count++] = *landing;
      }
      if (count != landings.size()) {
        reason = "native via does not connect two layers";
        return false;
      }
      const auto check = ph::check_signal_via(tech_,
                                              frame,
                                              rules_,
                                              *layout.occupancy,
                                              via.via,
                                              via.x,
                                              via.y,
                                              landings,
                                              input.local_net.name,
                                              owner_shapes);
      if (!check.legal) {
        reason = "native via: " + check.reason;
        return false;
      }
    }
    checkpoint();
    return true;
  }

  bool admit(size_t lid, size_t n, Signal_route_geometry geometry) {
    auto& menu = layouts_[lid].candidates[n];
    if (menu.size() >= limits_.candidates_per_net_layout) {
      result_.metrics.candidate_limit = true;
      return false;
    }
    if (std::ranges::any_of(menu, [&](size_t i) {
          return candidates_[i].geometry.paths == geometry.paths
                 && candidates_[i].geometry.nondefault_rules == geometry.nondefault_rules;
        })) {
      return false;
    }
    if (candidates_.size() >= limits_.max_variables) {
      result_.metrics.construction_limit = true;
      throw Search_limit("route candidate construction limit");
    }
    Candidate candidate;
    candidate.layout = lid;
    candidate.net    = n;
    std::string reason;
    if (!native_candidate(lid, n, geometry, candidate.owner_shapes, reason)) {
      ++result_.metrics.native_rejections;
      return false;
    }
    candidate.geometry = std::move(geometry);
    menu.push_back(candidates_.size());
    candidates_.push_back(std::move(candidate));
    ++result_.metrics.candidates;
    return true;
  }

  static bool append(Signal_route_geometry& into, const Signal_route_geometry& addition) {
    for (const auto& [name, widths] : addition.nondefault_rules) {
      if (const auto old = into.nondefault_rules.find(name); old != into.nondefault_rules.end() && old->second != widths) {
        return false;
      }
    }
    into.paths.insert(into.paths.end(), addition.paths.begin(), addition.paths.end());
    into.endpoints.insert(into.endpoints.end(), addition.endpoints.begin(), addition.endpoints.end());
    into.nondefault_rules.insert(addition.nondefault_rules.begin(), addition.nondefault_rules.end());
    into.vias           += addition.vias;
    into.wire_length_um += addition.wire_length_um;
    for (const auto& [layer, length] : addition.layer_wire_length_um) {
      into.layer_wire_length_um[layer] += length;
    }
    return true;
  }

  std::optional<Signal_route_geometry> external_route(size_t lid, size_t n, size_t trial, Clock::time_point deadline) {
    const auto&            layout = layouts_[lid];
    const auto&            input  = problem_.nets[n];
    Signal_route_geometry  combined;
    std::vector<ph::Shape> anchors;
    const auto             order = [&](size_t size) {
      std::vector<size_t> out(size);
      std::iota(out.begin(), out.end(), size_t{0});
      if (size) {
        std::rotate(out.begin(), out.begin() + trial % size, out.end());
        if (trial & 1u) {
          std::reverse(out.begin(), out.end());
        }
      }
      return out;
    };
    const auto component_metal = [&](size_t k) {
      auto metal = path_shapes(tech_, *layout.frame, input.local_net.name, input.incoming[k].paths);
      metal.insert(metal.end(), input.incoming[k].terminals.begin(), input.incoming[k].terminals.end());
      std::erase_if(metal, [&](const auto& shape) { return !tech_.layers.contains(shape.layer); });
      return metal;
    };
    const auto piece = [&](const ph::Net& net, const std::vector<ph::Shape>* sources) {
      checkpoint();
      if (Clock::now() >= deadline) {
        result_.metrics.generation_time_limit = true;
        return false;
      }
      auto retained = incoming_paths_[n];
      retained.insert(retained.end(), combined.paths.begin(), combined.paths.end());
      Signal_router                 router(tech_,
                                           *layout.frame,
                                           *layout.occupancy,
                                           rules_,
                                           problem_.profile,
                                           nullptr,
                                           input.domain,
                                           retained,
                                           &combined.nondefault_rules);
      const Signal_route_limits     limits{limits_.access_candidates, limits_.route_patterns, deadline};
      std::map<std::string, double> costs;
      costs[problem_.profile.access.escape]  = 1 + double(trial % 3);
      const auto prior                       = vias_of(retained);
      auto       proposal                    = sources ? router.propose_join(net, limits, *sources, anchors, costs, prior)
                                                       : router.propose_tap(net, limits, anchors, costs, prior);
      result_.metrics.generation_time_limit |= proposal.timed_out;
      if (!proposal.geometry || !append(combined, *proposal.geometry)) {
        auto why = std::string(sources ? "join: " : "tap: ")
                   + (!proposal.geometry ? (proposal.timed_out ? std::string("timed out") : proposal.reason) : "overlaps the partial route");
        if (why.size() > 160) {
          why.resize(160);
        }
        ++external_failures_[why];
        return false;
      }
      auto added = path_shapes(tech_, *layout.frame, input.local_net.name, proposal.geometry->paths, &combined.nondefault_rules);
      std::erase_if(added, [&](const auto& shape) { return !tech_.layers.contains(shape.layer); });
      anchors.insert(anchors.end(), added.begin(), added.end());
      return true;
    };
    const auto roots = order(input.incoming.size());
    anchors          = component_metal(roots.front());
    for (size_t i = 1; i < roots.size(); ++i) {
      const auto    sources = component_metal(roots[i]);
      const ph::Net join{input.local_net.name, {}, {}, "SIGNAL"};
      if (!piece(join, &sources)) {
        return std::nullopt;
      }
      anchors.insert(anchors.end(), sources.begin(), sources.end());
    }
    for (auto pin : order(input.local_net.connections.size())) {
      const ph::Net tap{input.local_net.name, {input.local_net.connections[pin]}, {}, "SIGNAL"};
      if (!piece(tap, nullptr)) {
        return std::nullopt;
      }
    }
    return combined;
  }

  bool generate(size_t lid, size_t goal) {
    auto& layout   = layouts_[lid];
    bool  progress = false;
    for (size_t n = 0; n < problem_.nets.size(); ++n) {
      checkpoint();
      if (layout.candidates[n].size() >= goal) {
        continue;
      }
      if (Clock::now() >= generation_stop()) {
        result_.metrics.generation_time_limit = true;
        break;
      }
      const auto& input = problem_.nets[n];
      if (!layout.empty_tried[n]) {
        layout.empty_tried[n] = true;
        // This is a SAT route alternative only if the unchanged native
        // validator connects every distinct physical pin and incoming root.
        if (admit(lid, n, {})) {
          progress = true;
          continue;
        }
      }
      if (!input.external && input.local_net.connections.size() == 1) {
        continue;
      }
      if (layout.calls[n] >= limits_.route_trials_per_net_layout) {
        continue;
      }
      const auto                deadline = proposal_stop(problem_.nets.size() - n);
      Signal_router             router(tech_, *layout.frame, *layout.occupancy, rules_, problem_.profile, nullptr, input.domain);
      const Signal_route_limits native_limits{limits_.access_candidates, limits_.route_patterns, deadline};
      // One whole-net proposal per net/pass: other obligations get a menu
      // before this net spends its budget generating extra alternatives.
      if (!input.external && !layout.span_exhausted[n] && layout.span_count[n] < goal) {
        layout.span_count[n] = goal;
        ++layout.calls[n];
        ++result_.metrics.route_trials;
        progress                               = true;
        auto spans                             = router.propose_spans(input.local_net, native_limits, goal);
        result_.metrics.generation_time_limit |= spans.timed_out;
        layout.span_exhausted[n]               = !spans.timed_out && spans.candidates.size() < goal;
        for (auto& candidate : spans.candidates) {
          admit(lid, n, std::move(candidate));
        }
        continue;
      }
      std::optional<size_t> next;
      while (layout.next_trial[n] < limits_.route_trials_per_net_layout) {
        checkpoint();
        const size_t trial = layout.next_trial[n]++;
        const auto   key   = std::tuple{trial % std::max(size_t{1}, input.local_net.connections.size()),
                                        trial & size_t{1},
                                        input.external ? trial % input.incoming.size() : 0,
                                        input.external ? trial % 3 : 0};
        if (layout.tried[n].insert(key).second) {
          next = trial;
          break;
        }
      }
      if (!next) {
        continue;
      }
      const size_t trial = *next;
      ++layout.calls[n];
      ++result_.metrics.route_trials;
      progress = true;
      if (input.external) {
        if (auto candidate = external_route(lid, n, trial, deadline)) {
          admit(lid, n, std::move(*candidate));
        }
      } else {
        auto net = input.local_net;
        std::rotate(net.connections.begin(), net.connections.begin() + trial % net.connections.size(), net.connections.end());
        if (trial & 1u) {
          std::reverse(net.connections.begin(), net.connections.end());
        }
        auto candidate = trial % 2 ? router.propose_maze(net, native_limits) : router.propose_pattern(net, native_limits);
        result_.metrics.generation_time_limit |= candidate.timed_out;
        if (candidate.geometry) {
          admit(lid, n, std::move(*candidate.geometry));
        }
      }
    }
    return progress;
  }

  bool compatible(const Candidate& a, const Candidate& b) {
    for (const auto& x : a.owner_shapes) {
      checkpoint();
      for (const auto& y : b.owner_shapes) {
        if (x.layer != y.layer) {
          continue;
        }
        if (tech_.layers.contains(x.layer)) {
          if (!rules_.spacing_with_union(x, y, a.owner_shapes, b.owner_shapes).legal) {
            return false;
          }
        } else if (const auto cut = tech_.cut_layers.find(x.layer); cut != tech_.cut_layers.end()) {
          if (ph::violates_cut_spacing(x.rect, y.rect, double(ph::signal_cut_spacing(cut->second, original_.dbu)))) {
            return false;
          }
        }
      }
    }
    return true;
  }

  bool select(size_t generated) {
    Choice_model     model(limits_, result_.metrics);
    std::vector<int> layout_literals;
    std::vector<int> route_literals(candidates_.size());
    for (size_t lid = 0; lid < generated; ++lid) {
      checkpoint();
      const int layout_literal = model.variable();
      layout_literals.push_back(layout_literal);
      for (size_t n = 0; n < problem_.nets.size(); ++n) {
        std::vector<int> choices;
        for (auto id : layouts_[lid].candidates[n]) {
          const int choice   = model.variable();
          route_literals[id] = choice;
          choices.push_back(choice);
          model.clause({-choice, layout_literal});
        }
        model.at_most_one(choices);
        if (!choices.empty()) {
          model.sat.prefer(choices.front());
        }
        choices.push_back(-layout_literal);
        model.clause(choices);
      }
    }
    model.clause(layout_literals);
    model.at_most_one(layout_literals);
    if (!layout_literals.empty()) {
      model.sat.prefer(layout_literals.front());
    }
    std::set<std::pair<size_t, size_t>> rejected;
    while (selection_rounds_ < limits_.sat_rounds) {
      ++selection_rounds_;
      const auto status = model.solve({}, solve_stop());
      if (status == Sat_status::unknown) {
        throw Search_limit("native SAT returned UNKNOWN");
      }
      if (status == Sat_status::unsatisfiable) {
        return false;
      }
      Phase_time verify{result_.metrics.verification_seconds, result_.metrics.verification_cpu_seconds};
      checkpoint();
      std::optional<size_t> selected_layout;
      for (size_t lid = 0; lid < generated; ++lid) {
        if (model.sat.value(layout_literals[lid])) {
          if (selected_layout) {
            throw std::runtime_error("SAT model selected multiple layouts");
          }
          selected_layout = lid;
        }
      }
      if (!selected_layout) {
        throw std::runtime_error("SAT model omitted layout");
      }
      std::vector<size_t> selected;
      std::vector<int>    no_good{-layout_literals[*selected_layout]};
      for (size_t n = 0; n < problem_.nets.size(); ++n) {
        std::optional<size_t> choice;
        for (auto id : layouts_[*selected_layout].candidates[n]) {
          if (model.sat.value(route_literals[id])) {
            if (choice) {
              throw std::runtime_error("SAT model selected multiple routes for a net");
            }
            choice = id;
          }
        }
        if (!choice || candidates_[*choice].layout != *selected_layout || candidates_[*choice].net != n) {
          throw std::runtime_error("SAT route has wrong layout or missing whole-net obligation");
        }
        selected.push_back(*choice);
        no_good.push_back(-route_literals[*choice]);
      }
      bool conflict = false;
      for (size_t a = 0; a < selected.size(); ++a) {
        for (size_t b = a + 1; b < selected.size(); ++b) {
          const auto pair = std::minmax(selected[a], selected[b]);
          if (!compatible(candidates_[selected[a]], candidates_[selected[b]])) {
            if (rejected.emplace(pair.first, pair.second).second) {
              model.clause({-route_literals[pair.first], -route_literals[pair.second]});
              ++result_.metrics.incompatible_pairs;
            }
            conflict = true;
          }
        }
      }
      if (conflict) {
        ++result_.metrics.rejected_models;
        continue;
      }
      ph::Route_width_rules selected_rules = layouts_[*selected_layout].frame->nondefault_rules;
      for (auto id : selected) {
        std::string            reason;
        std::vector<ph::Shape> rebuilt;
        if (!native_candidate(*selected_layout, candidates_[id].net, candidates_[id].geometry, rebuilt, reason)) {
          conflict = true;
          break;
        }
        for (const auto& [name, widths] : candidates_[id].geometry.nondefault_rules) {
          const auto [it, inserted] = selected_rules.emplace(name, widths);
          if (!inserted && it->second != widths) {
            conflict = true;
          }
        }
      }
      checkpoint();
      if (conflict) {
        model.clause(no_good);  // Exact complete-model rejection, never a fragment impossibility claim.
        ++result_.metrics.rejected_models;
        continue;
      }
      result_.poses = layouts_[*selected_layout].poses;
      for (auto id : selected) {
        result_.routes.push_back({problem_.nets[candidates_[id].net].source_net, candidates_[id].geometry});
      }
      result_.status = Cellbox_sat_status::complete;
      result_.reason = "native-verified joint layout and complete whole-net SAT selection";
      return true;
    }
    throw Search_limit("SAT model-validation round limit");
  }

public:
  Search(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Routing_occupancy& background,
         const Cellbox_sat_problem& problem, const Cellbox_sat_limits& limits)
      : tech_(tech), original_(frame), background_(background), problem_(problem), limits_(limits), rules_(tech, frame) {
    for (const auto& net : frame.special_nets) {
      supply_nets_.insert(net.name);
    }
    for (const auto& cell : problem.cells) {
      const auto& macro = tech.macros.at(frame.components.at(cell.component).macro);
      for (const auto& [name, pin] : macro.pins) {
        (void)name;
        if (pin.use == "POWER" || pin.use == "GROUND") {
          for (const auto& shape : pin.shapes) {
            supply_pin_layers_.insert(shape.layer);
          }
        }
      }
    }
  }
  Cellbox_sat_result run();

private:
  std::map<std::string, size_t> external_failures_;  // diagnostic: external join/tap proposal failures by reason
};

Cellbox_sat_result Search::run() {
  try {
    {
      Phase_time setup{result_.metrics.setup_seconds, result_.metrics.setup_cpu_seconds};
      input();
    }
    {
      Phase_time placement{result_.metrics.layout_seconds,
                           result_.metrics.layout_cpu_seconds,
                           &result_.metrics.sat_seconds,
                           &result_.metrics.sat_cpu_seconds};
      layouts();
    }
    if (layouts_.empty()) {
      throw Search_limit("no native-admitted complete layout");
    }
    if (problem_.nets.empty() && select(1)) {
      return result_;
    }
    size_t                examined_layouts = 0;
    std::optional<size_t> solved_revision;
    for (size_t goal = 1; goal <= limits_.candidates_per_net_layout && Clock::now() < generation_stop(); ++goal) {
      bool progress = true;
      while (progress && Clock::now() < generation_stop()) {
        progress = false;
        for (size_t lid = 0; lid < layouts_.size() && Clock::now() < generation_stop(); ++lid) {
          {
            Phase_time generation{result_.metrics.candidate_seconds, result_.metrics.candidate_cpu_seconds};
            progress |= generate(lid, goal);
          }
          examined_layouts = std::max(examined_layouts, lid + 1);
          const bool ready = std::ranges::all_of(layouts_[lid].candidates, [](const auto& menu) { return !menu.empty(); });
          if (ready && (!solved_revision || *solved_revision != candidates_.size())) {
            // Rebuild CNF for expanded menus; no old at-least-one clause
            // silently limits the selection to a previous candidate subset.
            if (select(examined_layouts)) {
              return result_;
            }
            solved_revision = candidates_.size();
          }
        }
      }
    }
    result_.metrics.generation_time_limit |= Clock::now() >= generation_stop();
    if (result_.metrics.generation_time_limit) {
      // which obligations still have no alternative in any layout (diagnostic only)
      size_t bare_external = 0, bare_internal = 0, bare_external_calls = 0, bare_internal_calls = 0;
      for (size_t n = 0; n < problem_.nets.size(); ++n) {
        if (std::ranges::any_of(layouts_, [&](const auto& layout) { return !layout.candidates[n].empty(); })) {
          continue;
        }
        size_t calls = 0;
        for (const auto& layout : layouts_) {
          calls += layout.calls[n];
        }
        (problem_.nets[n].external ? bare_external : bare_internal)             += 1;
        (problem_.nets[n].external ? bare_external_calls : bare_internal_calls) += calls;
      }
      std::string top;
      size_t      top_count = 0, all_failures = 0;
      for (const auto& [why, count] : external_failures_) {
        all_failures += count;
        if (count > top_count) {
          top       = why;
          top_count = count;
        }
      }
      throw Search_limit(std::format("bounded candidate-generation time slice exhausted ({} of {} nets without any candidate: "
                                     "{} external ({} trials), {} internal ({} trials); {} layouts; external failures {}, "
                                     "most {}x '{}')",
                                     bare_external + bare_internal,
                                     problem_.nets.size(),
                                     bare_external,
                                     bare_external_calls,
                                     bare_internal,
                                     bare_internal_calls,
                                     layouts_.size(),
                                     all_failures,
                                     top_count,
                                     top));
    }
    for (size_t n = 0; n < problem_.nets.size(); ++n) {
      if (std::ranges::none_of(layouts_, [&](const auto& layout) { return !layout.candidates[n].empty(); })) {
        throw Search_limit("empty generated whole-net route domain; native search is incomplete");
      }
    }
    if (select(layouts_.size())) {
      return result_;
    }
    result_.status = Cellbox_sat_status::finite_pool_unsat;
    result_.reason = "completed generated layout/whole-net route pool is UNSAT; not physical infeasibility";
  } catch (const Search_limit& error) {
    result_.status = Cellbox_sat_status::unknown;
    result_.reason = error.what();
  } catch (const std::exception& error) {
    result_.status = Cellbox_sat_status::invalid;
    result_.reason = error.what();
  }
  result_.poses.clear();
  result_.routes.clear();
  return result_;
}

}  // namespace

Cellbox_sat_result solve_cellbox_sat(const ph::Technology& tech, const ph::Floorplan& frame,
                                     const ph::Routing_occupancy& background, const Cellbox_sat_problem& problem,
                                     const Cellbox_sat_limits& limits) {
  try {
    const auto start                 = Clock::now();
    const auto cpu_start             = thread_cpu();
    auto       result                = Search(tech, frame, background, problem, limits).run();
    result.metrics.total_seconds     = std::chrono::duration<double>(Clock::now() - start).count();
    result.metrics.total_cpu_seconds = thread_cpu() - cpu_start;
    return result;
  } catch (const std::exception& error) {
    Cellbox_sat_result result;
    result.status = Cellbox_sat_status::invalid;
    result.reason = error.what();
    return result;
  }
}
}  // namespace livehd::placement
