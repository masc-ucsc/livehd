// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Native Local geometry emission and independently reusable geometry checks.
// Final placement/routing is implemented exclusively by legroute_final_sat.cpp.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <queue>
#include <regex>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>

#include "core/json_util.hpp"
#include "inou/lefdef/cut_rules.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/routing_rules.hpp"
#include "inou/lefdef/signal_via.hpp"
#include "legroute_internal.hpp"
#include "pin_escape_guard.hpp"
#include "signal_route_domain.hpp"
#include "signal_router.hpp"
#include "steiner_topology.hpp"

namespace livehd::placement::lr {
namespace ph = livehd::physical;

Planned_wire_geometry planned_wire_geometry(const ph::Technology& tech, int64_t dbu, const ph::Rect& die,
                                            const std::string& layer) {
  Planned_wire_geometry out;
  const auto&           l     = tech.layers.at(layer);
  const auto            width = ph::wire_width_dbu(l, dbu);
  out.layer                   = layer;
  out.width                   = width ? *width : std::max<int64_t>(2, std::llround(l.width * dbu) / 2 * 2);
  out.area                    = l.rules.area;
  out.dbu                     = dbu;
  out.die                     = die;
  if (const auto grid = ph::manufacturing_grid_dbu(tech, dbu)) {
    out.grid = *grid;
  }
  return out;
}

ph::Shape planned_wire_shape(const Planned_wire_geometry& geometry, bool horizontal, int64_t a, int64_t b, int64_t at) {
  if (a > b) {
    std::swap(a, b);
  }
  const int64_t w   = geometry.width;
  int64_t       ext = w / 2;
  // Minimum area: extend symmetrically on the manufacturing grid, exactly as the emitted wire does.
  if (const double area = geometry.area; area > 0) {
    const auto need = int64_t(std::ceil(area * double(geometry.dbu) * geometry.dbu / w));
    if (b - a + 2 * ext < need) {
      const auto half = (need - (b - a) + 1) / 2;
      ext             = (half + geometry.grid - 1) / geometry.grid * geometry.grid;
    }
  }
  // An extension past the die grows inward instead.
  int64_t      lo = a - ext, hi = b + ext;
  const double dl = horizontal ? geometry.die.xl : geometry.die.yl;
  const double dh = horizontal ? geometry.die.xh : geometry.die.yh;
  if (double(hi) > dh) {
    lo -= hi - int64_t(std::floor(dh));
    hi  = int64_t(std::floor(dh));
  }
  if (double(lo) < dl) {
    hi += int64_t(std::ceil(dl)) - lo;
    lo  = int64_t(std::ceil(dl));
  }
  return {
      geometry.layer,
      horizontal ? ph::Rect{        double(lo), double(at - w / 2),         double(hi), double(at + w / 2)}
                 : ph::Rect{double(at - w / 2),         double(lo), double(at + w / 2),         double(hi)}
  };
}

namespace {
// The metal stack bottom-up and the via between adjacent metals.
struct Stack {
  std::vector<std::string>                     metals;
  std::map<std::string, int>                   index;
  std::map<std::string, Planned_wire_geometry> geometry;
  struct Adjacent_via {
    std::string              name;
    std::array<ph::Shape, 2> plates;
  };
  std::map<std::pair<int, int>, std::vector<Adjacent_via>> adjacent;
  Stack(const ph::Technology& tech, const ph::Floorplan& frame, bool prepare_adjacent) {
    metals = tech.routing_layers;
    for (size_t k = 0; k < metals.size(); ++k) {
      index[metals[k]] = int(k);
      geometry.emplace(metals[k], planned_wire_geometry(tech, frame.dbu, frame.die, metals[k]));
    }
    if (!prepare_adjacent) {
      return;
    }
    std::vector<std::string> names;
    for (const auto& [name, via] : tech.vias) {
      if (via.is_default && via.unsupported.empty()) {
        names.push_back(name);
      }
    }
    std::ranges::sort(names);
    for (const auto& name : names) {
      std::vector<ph::Shape> plates;
      size_t                 cuts = 0;
      try {
        for (const auto& shape : ph::via_geometry(tech, frame, {name})) {
          if (tech.cut_layers.contains(shape.layer)) {
            ++cuts;
          } else if (index.contains(shape.layer)) {
            plates.push_back(shape);
          } else {
            plates.clear();
            break;
          }
        }
      } catch (const std::exception&) {
        continue;
      }
      if (cuts != 1 || plates.size() != 2) {
        continue;
      }
      const int a = index.at(plates[0].layer), b = index.at(plates[1].layer);
      if (std::abs(a - b) == 1) {
        adjacent[{
                     std::min(a, b),
                     std::max(a, b)
        }]
            .push_back({name, {plates[0], plates[1]}});
      }
    }
  }
};

}  // namespace

std::vector<int64_t> via_landing_reach(const ph::Technology& tech, const ph::Floorplan& frame, const Grid& g) {
  std::vector<int64_t> reach(g.layers.size(), 0);
  const Stack          st(tech, frame, true);
  for (const auto& [pair, vias] : st.adjacent) {
    for (const auto& via : vias) {
      for (const auto& plate : via.plates) {
        for (size_t l = 0; l < g.layers.size(); ++l) {
          if (g.layers[l].name != plate.layer) {
            continue;
          }
          const auto& r     = plate.rect;
          const auto  along = g.layers[l].horizontal ? std::max(-r.xl, r.xh) : std::max(-r.yl, r.yh);
          reach[l]          = std::max(reach[l], int64_t(std::ceil(along)));
        }
      }
    }
  }
  return reach;
}

namespace {
struct Geo {
  const Stack&                st;
  std::vector<ph::Route_path> paths;
  std::vector<ph::Shape>      wire_shapes;
  std::vector<ph::Route_path> adjacent_join_vias;
  // Track_state of the L5 track each path was drawn on (the caller sets `state` before wire_on)
  uint8_t                     state = 0;
  std::vector<uint8_t>        path_state;
  size_t                      vias = 0;
  explicit Geo(const Stack& s) : st(s) {}
  void wire_on(const std::string& layer, bool horizontal, int64_t a, int64_t b, int64_t at) {
    const auto shape = planned_wire_shape(st.geometry.at(layer), horizontal, a, b, at);
    paths.push_back(ph::flush_wire_path(shape, horizontal));
    wire_shapes.push_back(shape);
    path_state.push_back(state);
  }
  // a connection between two layers at (x, y): the shared router makes it (vias checked against everything placed)
  std::vector<std::tuple<std::string, std::string, int64_t, int64_t>> joins;
  void                                                                stack(int from, int to, int64_t x, int64_t y) {
    if (from != to) {
      joins.emplace_back(st.metals[from], st.metals[to], x, y);
    }
  }
  void stack(const std::string& a, const std::string& b, int64_t x, int64_t y) { stack(st.index.at(a), st.index.at(b), x, y); }
};

// Only the producer may enlarge a frozen backbone. Cover the outer plates
// of a deterministic native via stack by axial growth of existing wires.
// Intermediate metal is native via geometry; no track or lateral route is added.
// All grown metal and selected vias still require native ownership and DRC checks.
void adjacent_join_landings(const ph::Technology& tech, const ph::Floorplan& frame, Geo& geo,
                            std::span<const ph::Shape> original_io) {
  struct Step {
    ph::Route_path        path;
    ph::Shape             low;
    ph::Shape             high;
    size_t                previous = 0;
    std::optional<size_t> low_wire;
    std::optional<size_t> high_wire;
    ph::Shape             low_grown;
    ph::Shape             high_grown;
  };
  const auto touches = [](const ph::Shape& lhs, const ph::Shape& rhs) {
    const auto dx = std::min(lhs.rect.xh, rhs.rect.xh) - std::max(lhs.rect.xl, rhs.rect.xl);
    const auto dy = std::min(lhs.rect.yh, rhs.rect.yh) - std::max(lhs.rect.yl, rhs.rect.yl);
    return lhs.layer == rhs.layer && dx >= 0 && dy >= 0 && (dx > 0 || dy > 0);
  };
  for (const auto& [from, to, x, y] : geo.joins) {
    const int a = geo.st.index.at(from), b = geo.st.index.at(to);
    const int lo = std::min(a, b), hi = std::max(a, b);
    if (lo == hi) {
      continue;
    }
    const auto grow_outer = [&](const ph::Shape& plate, std::optional<size_t>& index, ph::Shape& grown) {
      const bool horizontal  = tech.layers.at(plate.layer).direction == "HORIZONTAL";
      double     best_growth = std::numeric_limits<double>::infinity();
      for (size_t k = 0; k < geo.wire_shapes.size(); ++k) {
        const auto& old = geo.wire_shapes[k];
        if (old.layer != plate.layer || x < old.rect.xl || x > old.rect.xh || y < old.rect.yl || y > old.rect.yh) {
          continue;
        }
        if (horizontal ? old.rect.yl > plate.rect.yl || old.rect.yh < plate.rect.yh
                       : old.rect.xl > plate.rect.xl || old.rect.xh < plate.rect.xh) {
          continue;
        }
        const auto rect = old.rect.hull(plate.rect);
        if (!frame.die.contains(rect)) {
          continue;
        }
        const double growth
            = horizontal ? rect.xh - rect.xl - (old.rect.xh - old.rect.xl) : rect.yh - rect.yl - (old.rect.yh - old.rect.yl);
        if (growth < best_growth) {
          index       = k;
          best_growth = growth;
          grown       = {old.layer, rect};
        }
      }
      if (index.has_value() || std::ranges::any_of(original_io, [&](const auto& pin) {
            return pin.layer == plate.layer && pin.rect.contains(plate.rect);
          })) {
        return true;
      }
      // A checked IO access may start directly at a terminal, with no Local
      // wire on that outer layer. Propose its finite, same-width axial cap as
      // separate metal; never enlarge the original terminal or FIXED source.
      // Native ownership/spacing/cut checks still admit the complete union.
      for (const auto& pin : original_io) {
        if (pin.layer != plate.layer || x < pin.rect.xl || x > pin.rect.xh || y < pin.rect.yl || y > pin.rect.yh
            || (horizontal ? pin.rect.yl > plate.rect.yl || pin.rect.yh < plate.rect.yh
                           : pin.rect.xl > plate.rect.xl || pin.rect.xh < plate.rect.xh)) {
          continue;
        }
        const auto rect = pin.rect.hull(plate.rect);
        if (!frame.die.contains(rect)) {
          continue;
        }
        const double growth
            = horizontal ? rect.xh - rect.xl - (pin.rect.xh - pin.rect.xl) : rect.yh - rect.yl - (pin.rect.yh - pin.rect.yl);
        if (growth < best_growth) {
          best_growth = growth;
          grown       = {pin.layer, rect};
        }
      }
      return !grown.layer.empty();
    };
    // Keep only choices reachable through actual touching native plates.
    // No geometry is published until both outer carriers cover a complete chain.
    std::vector<std::vector<Step>> chain;
    for (int layer = lo; layer < hi; ++layer) {
      const auto found = geo.st.adjacent.find({layer, layer + 1});
      if (found == geo.st.adjacent.end()) {
        break;
      }
      std::vector<Step> choices;
      for (const auto& candidate : found->second) {
        auto plates = candidate.plates;
        for (auto& plate : plates) {
          plate.rect = plate.rect.translated(x, y);
        }
        const auto low
            = std::ranges::find_if(plates, [&](const auto& plate) { return plate.layer == geo.st.metals[size_t(layer)]; });
        const auto high
            = std::ranges::find_if(plates, [&](const auto& plate) { return plate.layer == geo.st.metals[size_t(layer + 1)]; });
        if (low == plates.end() || high == plates.end()) {
          continue;
        }
        Step step;
        step.path = ph::via_path(geo.st.metals[size_t(layer)], x, y, candidate.name);
        step.low  = *low;
        step.high = *high;
        if (layer == lo) {
          if (!grow_outer(step.low, step.low_wire, step.low_grown)) {
            continue;
          }
        } else {
          const auto previous
              = std::ranges::find_if(chain.back(), [&](const auto& prior) { return touches(prior.high, step.low); });
          if (previous == chain.back().end()) {
            continue;
          }
          step.previous = size_t(previous - chain.back().begin());
        }
        if (layer + 1 == hi && !grow_outer(step.high, step.high_wire, step.high_grown)) {
          continue;
        }
        choices.push_back(std::move(step));
      }
      if (choices.empty()) {
        break;
      }
      chain.push_back(std::move(choices));
    }
    if (chain.size() != size_t(hi - lo)) {
      continue;
    }
    std::vector<size_t> selected(chain.size());
    for (size_t k = chain.size() - 1; k > 0; --k) {
      selected[k - 1] = chain[k][selected[k]].previous;
    }
    const auto& first         = chain.front()[selected.front()];
    const auto& last          = chain.back()[selected.back()];
    const auto  publish_outer = [&](const std::optional<size_t>& index, const ph::Shape& grown) {
      if (!index) {
        if (!grown.layer.empty()) {
          geo.wire_shapes.push_back(grown);
          geo.paths.push_back(ph::flush_wire_path(grown, tech.layers.at(grown.layer).direction == "HORIZONTAL"));
          // This is a deterministic landing proposal, not a searched track.
          // Its physical validity is checked with all other emitted metal.
          geo.path_state.push_back(uint8_t(Track_state::checked));
        }
        return;
      }
      geo.wire_shapes[*index] = grown;
      geo.paths[*index]       = ph::flush_wire_path(grown, tech.layers.at(grown.layer).direction == "HORIZONTAL");
    };
    publish_outer(first.low_wire, first.low_grown);
    publish_outer(last.high_wire, last.high_grown);
    for (size_t k = 0; k < chain.size(); ++k) {
      geo.adjacent_join_vias.push_back(std::move(chain[k][selected[k]].path));
    }
  }
}

int64_t snap(int64_t v, int64_t grid) { return grid > 1 ? (v + grid / 2) / grid * grid : v; }

// Access is selected from actual finite DEF tracks, not from a pin's center.
// This is only an access candidate; native geometry still validates its use.
std::optional<int64_t> pin_track(const ph::Floorplan& fp, const std::string& layer, bool horizontal, const ph::Rect& pin) {
  const double           lo = horizontal ? pin.yl : pin.xl, hi = horizontal ? pin.yh : pin.xh;
  const double           center = (lo + hi) / 2;
  std::optional<int64_t> best;
  for (const auto& track : fp.tracks) {
    if (track.layer != layer || track.axis != (horizontal ? "Y" : "X") || !track.supported()) {
      continue;
    }
    const auto [first, last] = track.index_range(lo, hi);
    if (first > last) {
      continue;
    }
    const auto index      = std::clamp<int64_t>(std::llround((center - track.start) / track.step), first, last);
    const auto coordinate = track.start + index * track.step;
    if (!best || std::abs(coordinate - center) < std::abs(*best - center)
        || (std::abs(coordinate - center) == std::abs(*best - center) && coordinate < *best)) {
      best = coordinate;
    }
  }
  return best;
}
}  // namespace

std::optional<Planned_terminal_access> planned_terminal_access(const Design& d, const Grid& g, const ph::Terminal& terminal) {
  const auto vertical = std::ranges::find(g.layers, false, &Layer::horizontal);
  if (vertical == g.layers.end()) {
    return {};
  }
  const auto grid = ph::manufacturing_grid_dbu(d.tech, d.fp.dbu).value_or(1);
  for (const auto& shape : terminal.shapes) {
    if (std::ranges::find(d.tech.routing_layers, shape.layer) == d.tech.routing_layers.end()) {
      continue;
    }
    const auto  rect         = ph::orient_rect(shape.rect, 0, 0, terminal.orientation).translated(terminal.x, terminal.y);
    const bool  horizontal   = d.tech.layers.at(shape.layer).direction == "HORIZONTAL";
    const auto& access_layer = horizontal ? vertical->name : shape.layer;
    const auto  px           = pin_track(d.fp, access_layer, false, rect);
    const auto  py           = horizontal ? pin_track(d.fp, shape.layer, true, rect)
                                          : std::optional<int64_t>{snap(std::llround((rect.yl + rect.yh) / 2), grid)};
    if (px && py && *py >= rect.yl && *py <= rect.yh) {
      return Planned_terminal_access{
          {shape.layer, rect},
          *px,
          *py,
          access_layer
      };
    }
  }
  return {};
}

struct Final_cut_state::State {
  ph::Signal_cut_set                                                   cuts;
  std::set<std::tuple<std::string, ph::Orientation, int64_t, int64_t>> vias;
  State(const ph::Technology& tech, const ph::Floorplan& frame) : cuts(tech, frame) {}
};
Final_cut_state::Final_cut_state(const ph::Technology& tech, const ph::Floorplan& frame)
    : state_(std::make_unique<State>(tech, frame)) {}
Final_cut_state::~Final_cut_state() = default;

std::vector<Signal_route_via> Final_cut_state::vias() const {
  std::vector<Signal_route_via> result;
  result.reserve(state_->vias.size());
  for (const auto& [name, orientation, x, y] : state_->vias) {
    result.push_back({
        {name, orientation},
        x,
        y
    });
  }
  return result;
}

std::string Final_cut_state::add(std::span<const ph::Route_path> paths) {
  // Signal_cut_set::add can insert some cuts before a later cut fails. Neither
  // those cuts nor the reuse identities may leak from a rejected proposal.
  auto next = std::make_unique<State>(*state_);
  try {
    for (const auto& path : paths) {
      std::optional<ph::Route_point> at;
      for (const auto& step : path.steps) {
        if (const auto* point = std::get_if<ph::Route_point>(&step)) {
          at = *point;
        } else if (const auto* via = std::get_if<ph::Route_via>(&step)) {
          if (!at) {
            return "route cut via without a preceding point";
          }
          if (next->vias.emplace(via->name, via->orientation, at->x, at->y).second && !next->cuts.add(*via, at->x, at->y)) {
            return "self cut spacing across route pieces";
          }
        }
      }
    }
  } catch (const std::exception& e) {
    return std::string("route cuts: ") + e.what();
  }
  state_.swap(next);
  return {};
}

bool write_local_join_probe(const Design& d, const Grid& g, const Final_planned_geometry& planned, const ph::Floorplan& frame,
                            const ph::Routing_occupancy& base, const std::string& directory, Clock::time_point deadline) {
  // A failed rerun must never leave a successful witness from an older candidate in this directory.
  std::filesystem::remove(directory + "/local_backbone_join_probe.def");
  std::filesystem::remove(directory + "/local_backbone_join_probe.json");
  struct Result {
    std::string                   net, reason;
    size_t                        joins = 0, witnessed = 0;
    bool                          accepted = false, timed_out = false;
    std::vector<Signal_route_via> vias;
  };
  auto                work      = frame;
  auto                occupancy = base;
  const auto          scope     = final_net_scope(d);
  std::vector<Result> results;
  size_t              joins_total = 0, joins_witnessed = 0;
  bool                complete = true;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (!scope.required[n] || planned.joins[n].empty()) {
      continue;
    }
    auto joins = planned.joins[n];
    for (auto& [from, to, x, y] : joins) {
      (void)x;
      (void)y;
      if (from > to) {
        std::swap(from, to);
      }
    }
    std::ranges::sort(joins);
    joins.erase(std::unique(joins.begin(), joins.end()), joins.end());
    Result result;
    result.net    = work.nets.at(d.nets[n].frame).name;
    result.joins  = joins.size();
    joins_total  += joins.size();
    if (!d.profile) {
      result.reason = "no technology routing profile";
    } else if (!planned.io_stub_failure[n].empty()) {
      result.reason = planned.io_stub_failure[n];
    } else if (Clock::now() >= deadline) {
      result.reason    = "shared Local diagnostic deadline";
      result.timed_out = true;
    } else {
      // Whole-net transaction: rejected joins leave no new shapes, cuts, width rules or paths in later nets' scene.
      auto                        trial_frame = work;
      auto                        trial_occ   = occupancy;
      Final_cut_state             cuts(d.tech, trial_frame);
      std::vector<ph::Route_path> added_paths;
      try {
        const ph::Signal_metal_rules rules(d.tech, trial_frame);
        Signal_router                router(d.tech, trial_frame, trial_occ, rules, signal_route_profile(*d.profile, d.tech));
        auto                         metal = ph::route_geometry(d.tech, trial_frame, trial_frame.nets.at(d.nets[n].frame));
        for (const auto& [from, to, x, y] : joins) {
          if (Clock::now() >= deadline) {
            result.reason    = "shared Local diagnostic deadline";
            result.timed_out = true;
            break;
          }
          const auto at = [&](const std::string& layer) {
            std::vector<ph::Shape> found;
            for (const auto& shape : metal) {
              if (Clock::now() >= deadline) {
                result.timed_out = true;
                return found;
              }
              if (shape.layer == layer && shape.rect.xl <= x && shape.rect.xh >= x && shape.rect.yl <= y && shape.rect.yh >= y) {
                found.push_back(shape);
              }
            }
            // Die IO pins may be one side of a join; movable cell pins are deliberately never routed by this probe.
            for (const auto& pin : d.nets[n].pins) {
              if (Clock::now() >= deadline) {
                result.timed_out = true;
                return found;
              }
              if (pin.term < 0) {
                continue;
              }
              const auto access = planned_terminal_access(d, g, d.fp.terminals[size_t(pin.term)]);
              if (access && access->shape.layer == layer && access->shape.rect.xl <= x && access->shape.rect.xh >= x
                  && access->shape.rect.yl <= y && access->shape.rect.yh >= y) {
                found.push_back(access->shape);
              }
            }
            return found;
          };
          auto sources = at(from), anchors = at(to);
          if (result.timed_out) {
            result.reason = "shared Local diagnostic deadline";
            break;
          }
          if (sources.empty() || anchors.empty()) {
            result.reason = std::format("join {}>{} at {},{} has no retained endpoint metal", from, to, x, y);
            break;
          }
          const auto piece_deadline = std::min(deadline, Clock::now() + std::chrono::milliseconds(50));
          const auto prior          = cuts.vias();
          auto       proposal
              = router.propose_join({result.net, {}, {}, "SIGNAL"}, {64, 2048, piece_deadline}, sources, anchors, {}, prior);
          if (!proposal.geometry) {
            result.reason    = proposal.reason.empty() ? "no join witness in bounded domain" : proposal.reason;
            result.timed_out = proposal.timed_out;
            break;
          }
          const auto& geometry = *proposal.geometry;
          if (auto reason = cuts.add(geometry.paths); !reason.empty()) {
            result.reason = std::move(reason);
            break;
          }
          apply_signal_route_rules(trial_frame, geometry);
          ph::Net piece;
          piece.name = result.net;
          piece.wires.push_back({"ROUTED", geometry.paths});
          const auto shapes = ph::route_geometry(d.tech, trial_frame, piece);
          for (const auto& shape : shapes) {
            trial_occ.insert({shape, result.net, true});
          }
          metal.insert(metal.end(), shapes.begin(), shapes.end());
          added_paths.insert(added_paths.end(), geometry.paths.begin(), geometry.paths.end());
          ++result.witnessed;
        }
        if (result.witnessed == result.joins && result.reason.empty()) {
          result.vias = cuts.vias();
          trial_frame.nets.at(d.nets[n].frame).wires.push_back({"ROUTED", std::move(added_paths)});
          if (Clock::now() >= deadline) {
            result.reason    = "shared Local diagnostic deadline";
            result.timed_out = true;
            result.vias.clear();
          } else {
            work             = std::move(trial_frame);
            occupancy        = std::move(trial_occ);
            joins_witnessed += result.joins;
            result.accepted  = true;
          }
        }
      } catch (const std::exception& error) {
        result.accepted = false;
        result.reason   = error.what();
      }
    }
    complete &= result.accepted;
    results.push_back(std::move(result));
  }
  std::ofstream out(directory + "/local_backbone_join_probe.json");
  out << std::format(R"({{"physical_certificate":false,"scope":"backbone_joins_only_no_cell_pin_taps",)"
                     R"("all_join_requests_witnessed":{},"joins":{},"accepted_joins":{},"per_join_ms":50,"nets":[)",
                     complete,
                     joins_total,
                     joins_witnessed);
  for (size_t n = 0; n < results.size(); ++n) {
    const auto& result = results[n];
    out << (n ? "," : "")
        << std::format(R"({{"net":"{}","joins":{},"witnessed_before_rollback":{},"accepted":{},)"
                       R"("timed_out":{},"reason":"{}","vias":[)",
                       json_util::escape(result.net),
                       result.joins,
                       result.witnessed,
                       result.accepted,
                       result.timed_out,
                       json_util::escape(result.reason));
    for (size_t v = 0; v < result.vias.size(); ++v) {
      const auto& via = result.vias[v];
      out << (v ? "," : "") << std::format(R"(["{}",{},{}])", json_util::escape(via.via.name), via.x, via.y);
    }
    out << "]}";
  }
  out << "]}\n";
  if (!out) {
    throw std::runtime_error("cannot write Local backbone join probe");
  }
  if (complete) {
    ph::write_def(work, directory + "/local_backbone_join_probe.def");
  }
  return complete;
}

struct Final_geometry_emitter::State {
  const Design&                      d;
  const Grid&                        g;
  const Boxes&                       boxes;
  const Routing&                     routes;
  Stack                              stack;
  int                                low_h = 0;
  size_t                             segment_count, spine_count, tree_count;
  std::map<int, std::vector<size_t>> spines_of;
  std::vector<std::vector<int>>      segs_of;

  State(const Design& design, const Grid& grid, const Boxes& cellboxes, const Routing& routing)
      : d(design)
      , g(grid)
      , boxes(cellboxes)
      , routes(routing)
      , stack(d.tech, d.fp, true)
      , segment_count(routes.segments.size())
      , spine_count(routes.spines.size())
      , tree_count(routes.trees.size())
      , segs_of(d.nets.size()) {
    for (int l = 0; l < int(g.layers.size()); ++l) {
      if (g.layers[l].horizontal) {
        low_h = l;
        break;
      }
    }
    for (size_t i = 0; i < routes.spines.size(); ++i) {
      spines_of[routes.spines[i].net].push_back(i);
    }
    for (int si = 0; si < int(routes.segments.size()); ++si) {
      const auto& segment = routes.segments[si];
      if (!segment.clock || segment.conn >= 0 || segment.node_lo >= 0 || segment.node_hi >= 0) {
        segs_of.at(size_t(segment.net)).push_back(si);
      }
    }
  }
};

Final_geometry_emitter::Final_geometry_emitter(const Design& d, const Grid& g, const Boxes& boxes, const Routing& routes)
    : state_(std::make_unique<State>(d, g, boxes, routes)) {}
Final_geometry_emitter::~Final_geometry_emitter() = default;

Final_net_geometry Final_geometry_emitter::emit(size_t n) const {
  const auto& state     = *state_;
  const auto& d         = state.d;
  const auto& g         = state.g;
  const auto& boxes     = state.boxes;
  const auto& R         = state.routes;
  const auto& segs_of   = state.segs_of;
  const auto& spines_of = state.spines_of;
  const int   low_h     = state.low_h;
  if (R.segments.size() != state.segment_count || R.spines.size() != state.spine_count || R.trees.size() != state.tree_count
      || d.nets.size() != segs_of.size()) {
    throw std::logic_error("Final geometry emitter: routing topology changed during cached emission");
  }
  if (n >= d.nets.size() || n >= R.trees.size()) {
    throw std::out_of_range("Final geometry emitter: invalid net index");
  }
  const auto                          coord = [&](const Segment& segment) { return g.track_coord(segment.layer, segment.track); };
  const auto                          lname = [&](int layer) -> const std::string& { return g.layers[layer].name; };
  Geo                                 geo(state.stack);
  std::map<int, std::vector<int64_t>> rch;
  std::string                         io_stub_failure;
  const auto                          finish = [&] {
    if (io_stub_failure.empty()) {
      std::vector<ph::Shape> original_io;
      for (const auto& pin : d.nets.at(n).pins) {
        if (pin.term < 0) {
          continue;
        }
        const auto& terminal = d.fp.terminals.at(size_t(pin.term));
        for (const auto& shape : terminal.shapes) {
          original_io.push_back(
              {shape.layer, ph::orient_rect(shape.rect, 0, 0, terminal.orientation).translated(terminal.x, terminal.y)});
        }
      }
      adjacent_join_landings(d.tech, d.fp, geo, original_io);
    }
    return Final_net_geometry{std::move(geo.paths),
                              std::move(geo.joins),
                              std::move(geo.path_state),
                              std::move(io_stub_failure),
                              std::move(geo.adjacent_join_vias)};
  };
  const auto& T = R.trees[n];
  if (T.nodes.empty() && !spines_of.contains(int(n))) {
    return finish();
  }
  const auto riser_x   = [&](int node) { return g.track_coord(T.nodes[node].riser_layer, T.nodes[node].riser_track); };
  const auto has_riser = [&](int node) {
    return node >= 0 && T.nodes[node].riser_track >= 0 && !(d.opt.cellbox_full_flow && T.nodes[node].direct_carrier_tap);
  };
  // bend partner coordinate of a segment end inside a conn
  const auto partner = [&](const Segment& s, int si, int k) -> std::optional<int64_t> {
    if (s.conn < 0) {
      return {};
    }
    for (int o : R.conns[s.conn].segs) {
      const auto& p = R.segments[o];
      if (o != si && p.band == k && g.layers[p.layer].horizontal != g.layers[s.layer].horizontal && p.track >= 0) {
        // the partner's band is this end's tile index, and this segment's band lies in the partner's range
        if (s.band >= p.lo && s.band <= p.hi) {
          return coord(p);
        }
      }
    }
    return {};
  };
  for (int si : segs_of[n]) {
    const auto& s = R.segments[si];
    if (s.track < 0) {
      continue;
    }
    const bool    h  = g.layers[s.layer].horizontal;
    const int64_t at = coord(s);
    int64_t       ends[2];
    for (int e = 0; e < 2; ++e) {
      const int k    = e ? s.hi : s.lo;
      const int node = e ? s.node_hi : s.node_lo;
      const int tile = h ? g.tile(k, s.band) : g.tile(s.band, k);
      if (node >= 0 && has_riser(node)) {
        if (h) {
          ends[e] = riser_x(node);
          geo.stack(lname(s.layer), lname(T.nodes[node].riser_layer), ends[e], at);
          rch[node].push_back(at);
        } else if (T.nodes[node].riser_layer == s.layer && T.nodes[node].riser_track == s.track) {
          ends[e] = g.cy(g.row(tile));  // the riser itself: it spans the node tile
          rch[node].push_back(ends[e]);
        } else {
          // The jog uses the horizontal layer L7 selected; old fixtures omit it and retain the lowest-H interpretation.
          const auto jt = e ? s.jog_hi : s.jog_lo;
          const int  jl = (e ? s.jog_hi_layer : s.jog_lo_layer) >= 0 ? (e ? s.jog_hi_layer : s.jog_lo_layer) : low_h;
          const auto yj = g.track_coord(jl, jt >= 0 ? jt : g.tracks(jl, g.row(tile)).first);
          ends[e]       = yj;
          // the recorded provenance, also when the pick failed (track -1, fallback): the default track drawn here is not checked
          geo.state = (e ? s.jog_hi_state : s.jog_lo_state) != uint8_t(Track_state::none) ? (e ? s.jog_hi_state : s.jog_lo_state)
                                                                                          : uint8_t(Track_state::none);
          geo.wire_on(lname(jl), true, at, riser_x(node), yj);
          geo.stack(lname(s.layer), lname(jl), at, yj);
          geo.stack(lname(jl), lname(T.nodes[node].riser_layer), riser_x(node), yj);
          rch[node].push_back(yj);
        }
      } else if (const auto p = partner(s, si, k)) {
        ends[e] = *p;
        if (h) {  // one via stack per bend, from the horizontal side
          for (int o : R.conns[s.conn].segs) {
            const auto& q = R.segments[o];
            if (o != si && q.band == k && !g.layers[q.layer].horizontal && q.track >= 0 && s.band >= q.lo && s.band <= q.hi) {
              geo.stack(lname(s.layer), lname(q.layer), *p, at);
              break;
            }
          }
        }
      } else if (node >= 0 && d.opt.cellbox_full_flow && T.nodes[node].direct_carrier_tap && T.nodes[node].box >= 0) {
        // the box edge (or the deeper end L5 staggered it to): Final extends the carrier into the box
        const auto staggered = e ? s.tap_hi : s.tap_lo;
        ends[e]              = staggered >= 0 ? staggered : carrier_tap_end(g, h, k, e == 1);
      } else {
        ends[e] = h ? g.cx(k) : g.cy(k);
      }
    }
    if (d.opt.cellbox_full_flow && !h) {
      // L5 reserved (and checked against every neighbor) a vertical join end over its whole end tile: the attachment
      // coordinates were not known yet. Stopping the metal at the join instead creates line ends L5 never checked
      // (EOL/corner keepouts are not monotone in the span), so the metal keeps the reserved extent.
      for (int e = 0; e < 2; ++e) {
        const int node = e ? s.node_hi : s.node_lo;
        if (node >= 0 && has_riser(node)) {
          ends[e] = e ? g.bottom(s.hi) + g.tile_h : g.bottom(s.lo);
        }
      }
    }
    geo.state = s.track_state;
    geo.wire_on(lname(s.layer), h, ends[0], ends[1], at);
  }
  // die IO terminals: a stub joins the node riser (cell pins, boxed or fixed, tap with the router)
  for (int k = 0; k < int(T.nodes.size()); ++k) {
    const auto& node = T.nodes[k];
    if (!has_riser(k)) {
      continue;
    }
    for (int pi : node.pins) {
      const auto& p = d.nets[n].pins[pi];
      if (p.comp >= 0) {
        continue;
      }
      const auto access = p.term >= 0 ? planned_terminal_access(d, g, d.fp.terminals[p.term]) : std::nullopt;
      if (!access) {
        // No partial stub may enter occupancy. The ordinary whole-net router
        // can try a different access topology, with the original pin intact.
        io_stub_failure = "die IO stub: no explicit preferred track inside the pin";
        geo.paths.clear();
        geo.joins.clear();
        geo.path_state.clear();
        geo.path_state.clear();
        return finish();
      }
      const auto& s  = access->shape;
      const auto  px = access->x, py = access->y;
      const auto  rx = riser_x(k);
      // pin -> selected vertical access layer -> up or down to the stub track (L7) -> across on the selected horizontal layer
      // -> the riser
      const int   sl = node.stub_layer >= 0 ? node.stub_layer : low_h;
      const auto  ys = node.stub_track >= 0 ? g.track_coord(sl, node.stub_track) : py;
      const auto& hl = lname(sl);
      const auto& vl = access->access_layer;
      if (node.stub_state == uint8_t(Track_state::fallback)
          || (ys != py && node.stub_access_state == uint8_t(Track_state::fallback))) {
        io_stub_failure = "die IO stub: H stub or V pin access remains an unchecked fallback";
      }
      if (ys != py) {
        geo.stack(s.layer, vl, px, py);
        geo.state = node.stub_access_state;  // Local admits all pin-to-stub vertical pieces together with the H stub.
        geo.wire_on(vl, false, py, ys, px);
        geo.stack(vl, hl, px, ys);
      } else {
        geo.stack(s.layer, hl, px, ys);  // no vertical piece: the pin joins the stub directly (none if same layer)
      }
      geo.state = node.stub_state;
      geo.wire_on(hl, true, px, rx, ys);
      geo.stack(hl, lname(node.riser_layer), rx, ys);
      rch[k].push_back(ys);
    }
  }
  // risers: from the lowest to the highest y any attachment needs, at the riser track
  for (const auto& [node, ys] : rch) {
    if (!has_riser(node) || ys.empty()) {
      continue;
    }
    const auto [lo, hi] = std::minmax_element(ys.begin(), ys.end());
    const int  tile     = T.nodes[node].tile;
    // full flow: the whole node tile, as L5 reserved and checked it
    const auto ylo      = std::min(*lo, g.bottom(g.row(tile)) + (d.opt.cellbox_full_flow ? 0 : g.tile_h / 4)),
               yhi      = std::max(*hi, g.bottom(g.row(tile)) + (d.opt.cellbox_full_flow ? g.tile_h : 3 * g.tile_h / 4));
    geo.state           = T.nodes[node].riser_state;
    geo.wire_on(lname(T.nodes[node].riser_layer), false, ylo, yhi, riser_x(node));
  }
  // clock spines of this net
  if (const auto it = spines_of.find(int(n)); it != spines_of.end()) {
    for (const auto spine_index : it->second) {
      const auto* sp    = &R.spines[spine_index];
      const auto& trunk = R.segments[sp->trunk];
      if (trunk.track < 0) {
        continue;
      }
      const auto                           tx  = coord(trunk);
      int64_t                              ylo = INT64_MAX, yhi = INT64_MIN;
      std::vector<std::pair<int, int64_t>> trunk_joins;  // (rib layer, rib y): made only if the trunk wire exists
      for (int ri : sp->ribs) {
        const auto& rib = R.segments[ri];
        if (rib.track < 0) {
          continue;
        }
        const auto ry = coord(rib);
        ylo           = std::min(ylo, ry);
        yhi           = std::max(yhi, ry);
        int64_t xlo = tx, xhi = tx;
        for (const auto& [b, ts] : sp->taps) {
          const auto& tap = R.segments[ts];
          if (tap.track < 0 || (tap.lo != rib.band && tap.hi != rib.band)) {
            continue;
          }
          const auto x = coord(tap);
          xlo          = std::min(xlo, x);
          xhi          = std::max(xhi, x);
          geo.stack(lname(tap.layer), lname(rib.layer), x, ry);
        }
        // the clock tree enters on the middle rib through the entry node's riser
        if (sp->node >= 0 && has_riser(sp->node) && g.row(T.nodes[sp->node].tile) == rib.band) {
          const auto rx = riser_x(sp->node);
          xlo           = std::min(xlo, rx);
          xhi           = std::max(xhi, rx);
          geo.stack(lname(T.nodes[sp->node].riser_layer), lname(rib.layer), rx, ry);
          geo.state = T.nodes[sp->node].riser_state;
          geo.wire_on(lname(T.nodes[sp->node].riser_layer),
                      false,
                      std::min(ry, g.cy(trunk.band >= 0 ? rib.band : 0)),
                      std::max(ry, g.cy(rib.band)),
                      rx);
        }
        geo.state = rib.track_state;
        geo.wire_on(lname(rib.layer), true, xlo, xhi, ry);
        trunk_joins.emplace_back(rib.layer, ry);
      }
      // A spine with a single drawn rib has no trunk wire (nothing to connect): a rib->trunk join there would name
      // trunk metal that does not exist.
      if (yhi > ylo) {
        geo.state = trunk.track_state;
        geo.wire_on(lname(trunk.layer), false, ylo, yhi, tx);
        for (const auto& [layer, ry] : trunk_joins) {
          geo.stack(lname(layer), lname(trunk.layer), tx, ry);
        }
      }
      // taps: from the box's tap point (in-box route) up to the rib
      for (const auto& [b, ts] : sp->taps) {
        const auto& tap = R.segments[ts];
        if (tap.track < 0) {
          continue;
        }
        const int rib_row = tap.lo == g.row(boxes.boxes[b].tile) ? tap.hi : tap.lo;
        int64_t   ry      = g.cy(rib_row);
        for (int ri : sp->ribs) {
          if (R.segments[ri].band == rib_row && R.segments[ri].track >= 0) {
            ry = coord(R.segments[ri]);
          }
        }
        // over the whole box tile (the in-box route taps it on any box-layer track) and up to the rib
        const int64_t box_lo = g.bottom(g.row(boxes.boxes[b].tile)), box_hi = box_lo + g.tile_h;
        geo.state = tap.track_state;
        geo.wire_on(lname(tap.layer), false, std::min(ry, box_lo), std::max(ry, box_hi), coord(tap));
      }
    }
  }
  return finish();
}

namespace {
Final_planned_geometry selected_planned_geometry(const Design& d, const Grid& g, const Boxes& boxes, const Routing& routes,
                                                 std::span<const std::string> rejected) {
  Final_geometry_emitter emitter(d, g, boxes, routes);
  Final_planned_geometry out;
  out.paths.resize(d.nets.size());
  out.joins.resize(d.nets.size());
  out.path_state.resize(d.nets.size());
  out.io_stub_failure.resize(d.nets.size());
  out.adjacent_join_vias.resize(d.nets.size());
  parallel(d.nets.size(), d.opt.threads, [&](size_t n) {
    if (!rejected.empty() && !rejected[n].empty()) {
      return;  // Rejected Local owners contribute neither metal nor join anchors.
    }
    auto net                  = emitter.emit(n);
    out.paths[n]              = std::move(net.paths);
    out.joins[n]              = std::move(net.joins);
    out.path_state[n]         = std::move(net.path_state);
    out.adjacent_join_vias[n] = std::move(net.adjacent_join_vias);
    out.io_stub_failure[n]    = std::move(net.io_stub_failure);
  });
  return out;
}
}  // namespace

Final_planned_geometry final_planned_geometry(const Design& d, const Grid& g, const Boxes& boxes, const Routing& routes) {
  return selected_planned_geometry(d, g, boxes, routes, {});
}

Final_net_scope final_net_scope(const Design& d) {
  Final_net_scope scope;
  scope.required.resize(d.nets.size());
  std::vector<bool> source_seen;
  if (d.opt.cellbox_full_flow) {
    if (d.nets.size() != d.fp.nets.size()) {
      throw std::invalid_argument("critical Final: source net inventory changed");
    }
    source_seen.resize(d.fp.nets.size(), false);
  }
  std::optional<std::regex> skip_re;
  if (!d.opt.cellbox_full_flow && !d.opt.final_skip_nets.empty()) {
    skip_re.emplace(d.opt.final_skip_nets, std::regex::ECMAScript | std::regex::icase);
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& net = d.nets[n];
    if (d.opt.cellbox_full_flow) {
      if (net.frame >= source_seen.size() || source_seen[net.frame]) {
        throw std::invalid_argument("critical Final: duplicate or missing source net identity");
      }
      source_seen[net.frame] = true;
      if (net.power) {
        continue;
      }
      if (net.control_scope) {
        scope.skipped += d.fp.nets[net.frame].connections.size() >= 2;
        continue;
      }
      if (net.pins.size() != d.fp.nets[net.frame].connections.size()) {
        throw std::invalid_argument("critical Final: unresolved original physical endpoint on " + d.fp.nets[net.frame].name);
      }
      std::vector<std::pair<std::string_view, std::string_view>> expected, actual;
      expected.reserve(net.pins.size());
      actual.reserve(net.pins.size());
      for (const auto& connection : d.fp.nets[net.frame].connections) {
        expected.emplace_back(connection.instance, connection.pin);
      }
      for (const auto& pin : net.pins) {
        if (pin.comp >= 0 && pin.term < 0 && size_t(pin.comp) < d.fp.components.size()) {
          actual.emplace_back(d.fp.components[size_t(pin.comp)].name, pin.name);
        } else if (pin.comp < 0 && pin.term >= 0 && size_t(pin.term) < d.fp.terminals.size()
                   && pin.name == d.fp.terminals[size_t(pin.term)].name) {
          actual.emplace_back("PIN", pin.name);
        } else {
          throw std::invalid_argument("critical Final: invalid original physical endpoint on " + d.fp.nets[net.frame].name);
        }
      }
      std::ranges::sort(expected);
      std::ranges::sort(actual);
      if (actual != expected) {
        throw std::invalid_argument("critical Final: original physical endpoint identity changed on " + d.fp.nets[net.frame].name);
      }
      scope.required[n] = net.pins.size() >= 2;
      continue;
    }
    if (!net.requires_final_route()) {
      continue;
    }
    if ((d.opt.final_skip_clock && net.clock) || (skip_re && std::regex_search(d.fp.nets[net.frame].name, *skip_re))) {
      ++scope.skipped;
    } else {
      scope.required[n] = 1;
    }
  }
  return scope;
}

}  // namespace livehd::placement::lr
