// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "steiner_repair.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "core/debug_env.hpp"
namespace livehd::placement {
Buffer_tree plan_buffers(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                         double buffer_input_ff, double max_cap_ff, unsigned max_fanout, double max_distance_dbu,
                         double wire_cap_ff_dbu) {
  if (loads.empty() || loads.size() != capacitances.size() || max_fanout < 2 || !std::isfinite(buffer_input_ff)
      || buffer_input_ff < 0 || !(max_cap_ff > buffer_input_ff) || !(max_distance_dbu > 0) || !std::isfinite(wire_cap_ff_dbu)
      || wire_cap_ff_dbu < 0) {
    throw std::invalid_argument("invalid Steiner buffer limits");
  }
  std::vector<Point> points{root};
  points.insert(points.end(), loads.begin(), loads.end());
  const auto                         tree        = steiner_tree(points);
  const auto                         root_vertex = tree.terminals.front();
  std::vector<std::vector<unsigned>> adjacent(tree.points.size()), local(tree.points.size());
  for (const auto& [a, b] : tree.edges) {
    adjacent[a].push_back(b);
    adjacent[b].push_back(a);
  }
  for (unsigned i = 0; i < loads.size(); ++i) {
    if (!std::isfinite(capacitances[i]) || capacitances[i] < 0) {
      throw std::invalid_argument("invalid sink capacitance");
    }
    local[tree.terminals[i + 1]].push_back(i);
  }
  const auto            none = std::numeric_limits<unsigned>::max();
  std::vector<unsigned> parent(tree.points.size(), none), order{root_vertex};
  parent[root_vertex] = root_vertex;
  for (size_t i = 0; i < order.size(); ++i) {
    for (const auto n : adjacent[order[i]]) {
      if (parent[n] == none) {
        parent[n] = order[i];
        order.push_back(n);
      }
    }
  }
  if (order.size() != tree.points.size()) {
    throw std::runtime_error("disconnected Steiner tree");
  }
  struct Item {
    Buffer_tree::Load load;
    double            cap = 0, distance = 0;
  };
  using Packet = std::vector<Item>;
  Buffer_tree result;
  const auto  total_cap
      = [](const Packet& p) { return std::accumulate(p.begin(), p.end(), 0.0, [](double c, const auto& i) { return c + i.cap; }); };
  const auto distance = [](const Packet& p) {
    double d = 0;
    for (const auto& i : p) {
      d = std::max(d, i.distance);
    }
    return d;
  };
  const auto buffer = [&](Packet p, Point point) -> Packet {
    if (p.empty()) {
      return {};
    }
    if (p.size() > max_fanout || total_cap(p) > max_cap_ff * (1 + 1e-9)) {
      throw std::runtime_error("buffer output exceeds selected drive limits");
    }
    if (result.buffers.size() == 1000000) {
      throw std::runtime_error("Steiner repair exceeds one million buffers (resource limit)");
    }
    Buffer_tree::Branch b{point, {}};
    for (const auto& i : p) {
      b.loads.push_back(i.load);
    }
    const auto id = static_cast<unsigned>(result.buffers.size());
    result.buffers.push_back(std::move(b));
    return {
        {{true, id}, buffer_input_ff, 0}
    };
  };
  const auto compact = [&](Packet p, Point point) {
    while (p.size() > max_fanout || total_cap(p) > max_cap_ff * (1 + 1e-9)) {
      Packet next, group;
      double group_cap = 0;
      for (const auto& i : p) {
        if (i.cap > max_cap_ff * (1 + 1e-9)) {
          throw std::runtime_error("sink load exceeds selected buffer drive");
        }
        if (!group.empty() && (group.size() == max_fanout || group_cap + i.cap > max_cap_ff)) {
          auto b = buffer(std::move(group), point);
          next.push_back(b.front());
          group.clear();
          group_cap = 0;
        }
        group.push_back(i);
        group_cap += i.cap;
      }
      if (!group.empty()) {
        auto b = buffer(std::move(group), point);
        next.push_back(b.front());
      }
      if (next.size() >= p.size() && total_cap(next) >= total_cap(p) - 1e-9) {
        throw std::runtime_error("selected drive cannot split this load");
      }
      p = std::move(next);
    }
    return p;
  };
  std::vector<Packet> packets(tree.points.size());
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    const auto n = *it;
    auto       p = std::move(packets[n]);
    for (const auto leaf : local[n]) {
      p.push_back({
          {false, leaf},
          capacitances[leaf],
          0
      });
    }
    p = compact(std::move(p), tree.points[n]);
    if (n == root_vertex) {
      for (const auto& i : p) {
        result.root.push_back(i.load);
      }
      break;
    }
    Point  xy = tree.points[n], end = tree.points[parent[n]];
    double remaining = std::abs(xy.first - end.first) + std::abs(xy.second - end.second);
    while (!p.empty() && remaining > 1e-7) {
      double room = max_distance_dbu - distance(p);
      if (wire_cap_ff_dbu > 0) {
        room = std::min(room, (max_cap_ff - total_cap(p)) / wire_cap_ff_dbu);
      }
      if (room <= 1e-7) {
        p = buffer(std::move(p), xy);
        continue;
      }
      const double advance    = std::min(room, remaining);
      const double dx         = std::copysign(std::min(std::abs(end.first - xy.first), advance), end.first - xy.first);
      xy.first               += dx;
      xy.second              += std::copysign(advance - std::abs(dx), end.second - xy.second);
      const double added_cap  = advance * wire_cap_ff_dbu / p.size();
      for (auto& i : p) {
        i.cap      += added_cap;
        i.distance += advance;
      }
      remaining -= advance;
      if (remaining > 1e-7) {
        p = buffer(std::move(p), xy);
      }
    }
    auto& up = packets[parent[n]];
    up.insert(up.end(), p.begin(), p.end());
  }
  return result;
}

namespace {
// Partition the sinks before building the distribution backbone. Cutting only
// a sink-level Steiner tree can leave overlapping, long leaf nets at forks.
// These alternatives keep leaf loads spatially local; the usual timing model
// still chooses every drive and compares the complete tree with direct wiring.
Buffer_tree clustered_buffers(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                              const Buffer_drive& drive, double wire_c, bool nearest_root) {
  std::vector<std::vector<unsigned>> groups(1);
  groups.front().resize(loads.size());
  std::iota(groups.front().begin(), groups.front().end(), 0);
  Buffer_tree        result;
  std::vector<Point> centers;
  for (size_t i = 0; i < groups.size(); ++i) {
    auto   group = std::move(groups[i]);
    auto   low = loads[group.front()], high = low;
    double cap = 0;
    for (const auto index : group) {
      const auto xy  = loads[index];
      low.first      = std::min(low.first, xy.first);
      low.second     = std::min(low.second, xy.second);
      high.first     = std::max(high.first, xy.first);
      high.second    = std::max(high.second, xy.second);
      cap           += capacitances[index];
    }
    const Point center
        = nearest_root ? Point{std::clamp(root.first, low.first, high.first), std::clamp(root.second, low.second, high.second)}
                       : Point{(low.first + high.first) / 2, (low.second + high.second) / 2};
    bool fits = group.size() <= drive.max_fanout && cap <= drive.max_cap_ff;
    if (fits && wire_c > 0) {
      std::vector<Point> points{center};
      for (const auto index : group) {
        points.push_back(loads[index]);
      }
      const auto tree = steiner_tree(points);
      for (const auto& [a, b] : tree.edges) {
        cap += wire_c
               * (std::abs(tree.points[a].first - tree.points[b].first) + std::abs(tree.points[a].second - tree.points[b].second));
      }
      fits = cap <= drive.max_cap_ff;
    }
    if (!fits) {
      if (group.size() == 1) {
        throw std::runtime_error("sink load exceeds clustered buffer drive");
      }
      const bool x_axis = high.first - low.first >= high.second - low.second;
      const auto middle = group.begin() + group.size() / 2;
      std::nth_element(group.begin(), middle, group.end(), [&](unsigned a, unsigned b) {
        const double lhs = x_axis ? loads[a].first : loads[a].second;
        const double rhs = x_axis ? loads[b].first : loads[b].second;
        return lhs != rhs ? lhs < rhs : a < b;
      });
      groups.emplace_back(group.begin(), middle);
      groups.emplace_back(middle, group.end());
      continue;
    }
    if (result.buffers.size() == 1000000) {
      throw std::runtime_error("Steiner repair exceeds one million buffers (resource limit)");
    }
    Buffer_tree::Branch leaf{center, {}};
    for (const auto index : group) {
      leaf.loads.push_back({false, index});
    }
    centers.push_back(center);
    result.buffers.push_back(std::move(leaf));
  }
  const auto backbone = plan_buffers(root,
                                     centers,
                                     std::vector<double>(centers.size(), drive.input_ff),
                                     drive.input_ff,
                                     drive.max_cap_ff,
                                     drive.max_fanout,
                                     drive.max_distance_dbu,
                                     wire_c);
  if (result.buffers.size() + backbone.buffers.size() > 1000000) {
    throw std::runtime_error("Steiner repair exceeds one million buffers (resource limit)");
  }
  const auto offset = static_cast<unsigned>(result.buffers.size());
  const auto remap  = [&](const std::vector<Buffer_tree::Load>& children) {
    std::vector<Buffer_tree::Load> mapped;
    for (const auto child : children) {
      mapped.push_back({true, child.index + (child.buffer ? offset : 0)});
    }
    return mapped;
  };
  for (const auto& branch : backbone.buffers) {
    result.buffers.push_back({branch.position, remap(branch.loads), 0});
  }
  result.root = remap(backbone.root);
  return result;
}
}  // namespace

namespace {
// LHD_STEINER_DUMP=<file>: append the inputs of a plan that found no alternative (with LHD_STEINER_DUMP_ALL: of every
// plan) on a net of at least LHD_STEINER_DUMP_LOADS (default 100) loads, so the planner can be replayed offline
// (steiner_replay_test).
void dump_plan_inputs(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                      const std::vector<double>& required_ps, const std::vector<Buffer_drive>& drives, const Buffer_drive& driver,
                      double r, double c, const Buffer_limits& limits) {
  const char* path = livehd::debug::env<"LHD_STEINER_DUMP">();
  if (path == nullptr) {
    return;
  }
  const char*  min_text = livehd::debug::env<"LHD_STEINER_DUMP_LOADS">();
  const size_t minimum  = min_text ? std::strtoul(min_text, nullptr, 10) : 100;
  if (loads.size() < minimum) {
    return;
  }
  std::FILE* f = std::fopen(path, "a");
  if (f == nullptr) {
    return;
  }
  const auto drive_line = [&](const Buffer_drive& d) {
    std::fprintf(f, "%.17g %.17g %.17g %u %.17g %.17g %.17g\n", d.input_ff, d.max_cap_ff, d.max_distance_dbu, d.max_fanout,
                 d.intrinsic_ps, d.resistance_ohm, d.area);
  };
  std::fprintf(f, "plan %zu %zu\nroot %.17g %.17g\nwire %.17g %.17g\nlimits %zu %.17g\ndriver\n", loads.size(), drives.size(),
               root.first, root.second, r, c, limits.cells, limits.area);
  drive_line(driver);
  std::fprintf(f, "drives\n");
  for (const auto& d : drives) {
    drive_line(d);
  }
  std::fprintf(f, "loads\n");
  for (size_t i = 0; i < loads.size(); ++i) {
    std::fprintf(f, "%.17g %.17g %.17g %.17g\n", loads[i].first, loads[i].second, capacitances[i], required_ps[i]);
  }
  std::fprintf(f, "end\n");
  std::fclose(f);
}
}  // namespace

Buffer_tree plan_buffer_choices(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                                const std::vector<double>& required_ps, const std::vector<Buffer_drive>& drives,
                                const Buffer_drive& driver, double wire_r_ohm_dbu, double wire_c_ff_dbu,
                                const Buffer_limits& limits, double effort, size_t min_topologies) {
  // LHD_STEINER_TRACE=<n>: explain every rejected candidate on nets of at least n loads (empty or 0: n = 100).
  const char*  trace_text = livehd::debug::env<"LHD_STEINER_TRACE">();
  const size_t trace_min  = trace_text && std::strtoul(trace_text, nullptr, 10) > 0 ? std::strtoul(trace_text, nullptr, 10) : 100;
  const bool   trace      = trace_text != nullptr && loads.size() >= trace_min;
  const auto note  = [&](size_t candidate, const char* what, const char* detail = "") {
    if (trace) {
      std::fprintf(stderr, "[steiner] %zu loads candidate %zu: %s %s\n", loads.size(), candidate, what, detail);
    }
  };
  if (drives.empty() || loads.size() != required_ps.size() || loads.size() != capacitances.size() || !std::isfinite(wire_r_ohm_dbu)
      || wire_r_ohm_dbu < 0) {
    throw std::invalid_argument("invalid buffer-choice model");
  }
  for (const auto t : required_ps) {
    if (!std::isfinite(t)) {
      throw std::invalid_argument("invalid sink required time");
    }
  }
  Buffer_tree               best;
  double                    best_required = -std::numeric_limits<double>::infinity(), best_area = best_required;
  double                    best_hpwl = std::numeric_limits<double>::infinity(), spatial_hpwl_limit = best_hpwl;
  bool                      found = false;
  // The topologies to build: one per distinct set of limits (two drives with the same limits give the same tree), and,
  // for a net of little consequence, an even spread of them over the library instead of all of them.
  struct Topology {
    Buffer_drive limits;
    size_t       drive;  // the library drive whose limits these are (the spatial alternatives follow it)
  };
  std::vector<Topology> all_topologies;
  for (size_t d = 0; d < drives.size(); ++d) {
    const auto& drive = drives[d];
    std::vector<unsigned> fanouts{std::min(4u, drive.max_fanout), std::min(16u, drive.max_fanout), drive.max_fanout};
    std::sort(fanouts.begin(), fanouts.end());
    fanouts.erase(std::unique(fanouts.begin(), fanouts.end()), fanouts.end());
    for (const auto fanout : fanouts) {
      auto topology       = drive;
      topology.max_fanout = fanout;
      all_topologies.push_back({topology, d});
    }
  }
  const auto same_limits = [](const Buffer_drive& a, const Buffer_drive& b) {
    return a.input_ff == b.input_ff && a.max_cap_ff == b.max_cap_ff && a.max_fanout == b.max_fanout
           && a.max_distance_dbu == b.max_distance_dbu;
  };
  std::vector<Topology> distinct;
  for (const auto& t : all_topologies) {
    if (std::none_of(distinct.begin(), distinct.end(), [&](const auto& u) { return same_limits(u.limits, t.limits); })) {
      distinct.push_back(t);
    }
  }
  std::vector<Topology> chosen;
  if (effort >= 1.0 || distinct.size() <= min_topologies) {
    chosen = distinct;
  } else {
    std::stable_sort(distinct.begin(), distinct.end(), [](const auto& a, const auto& b) {
      return std::tie(a.limits.max_cap_ff, a.limits.max_fanout, a.limits.max_distance_dbu)
             < std::tie(b.limits.max_cap_ff, b.limits.max_fanout, b.limits.max_distance_dbu);
    });
    const size_t keep = std::clamp<size_t>(static_cast<size_t>(std::ceil(std::max(effort, 0.0) * double(distinct.size()))),
                                           min_topologies, distinct.size());
    for (size_t k = 0; k < keep; ++k) {
      chosen.push_back(distinct[keep == 1 ? 0 : k * (distinct.size() - 1) / (keep - 1)]);
    }
  }
  std::vector<Buffer_drive> topologies;
  std::vector<size_t>       spatial_drives;  // library drives whose spatial alternatives are tried, in library order
  for (const auto& t : chosen) {
    topologies.push_back(t.limits);
    spatial_drives.push_back(t.drive);
  }
  std::sort(spatial_drives.begin(), spatial_drives.end());
  spatial_drives.erase(std::unique(spatial_drives.begin(), spatial_drives.end()), spatial_drives.end());
  // Include both the original connection and a single source repeater. A
  // source can satisfy electrical limits yet benefit from isolating its load;
  // requiring an electrical violation would omit this compact alternative.
  const size_t spatial_candidates = loads.size() > 16 ? 2 * spatial_drives.size() : 0;
  for (size_t candidate = 0; candidate < topologies.size() + 2 + spatial_candidates; ++candidate) {
    if (candidate == topologies.size() + 2) {
      // New topologies may improve delay without duplicating more wire than
      // the best existing alternative. Keep that reference fixed as other
      // spatial candidates improve timing, so the bound cannot drift.
      spatial_hpwl_limit = best_hpwl;
    }
    Buffer_tree plan;
    try {
      if (candidate < 2) {
        for (unsigned i = 0; i < loads.size(); ++i) {
          plan.root.push_back({false, i});
        }
        if (candidate == 1) {
          plan.buffers.push_back({root, std::move(plan.root), 0});
          plan.root = {
              {true, 0}
          };
        }
      } else if (candidate < topologies.size() + 2) {
        const auto& topology = topologies[candidate - 2];
        plan                 = plan_buffers(root,
                                            loads,
                                            capacitances,
                                            topology.input_ff,
                                            topology.max_cap_ff,
                                            topology.max_fanout,
                                            topology.max_distance_dbu,
                                            wire_c_ff_dbu);
      } else {
        const auto spatial = candidate - topologies.size() - 2;
        plan               = clustered_buffers(root, loads, capacitances, drives[spatial_drives[spatial / 2]], wire_c_ff_dbu, spatial % 2 != 0);
      }
    } catch (const std::runtime_error& error) {
      note(candidate, "topology failed:", error.what());
      continue;
    }
    struct State {
      double cap, required, area, hpwl = 0;
    };
    std::vector<State> states;
    // The state a driver at `origin` sees over `children`: the capacitance of all of them, the wire, and the
    // required time at the driver's output. The wire delay is the Elmore delay on the net's Steiner tree, so
    // the resistance of the SHARED trunk carries the capacitance of everything behind it: a star of
    // independent wires (each load alone behind its own wire) makes a long net with hundreds of loads look
    // nearly free, and no buffer tree could then beat leaving it as it is.
    const auto         aggregate = [&](Point origin, const std::vector<Buffer_tree::Load>& children) {
      State              result{0, std::numeric_limits<double>::infinity(), 0};
      double             longest = 0;
      auto               low = origin, high = origin;
      std::vector<Point> endpoints{origin};
      std::vector<State> child_state;
      for (const auto& child : children) {
        const auto xy = child.buffer ? plan.buffers.at(child.index).position : loads.at(child.index);
        const auto state
            = child.buffer ? states.at(child.index) : State{capacitances.at(child.index), required_ps.at(child.index), 0};
        const double length = std::abs(xy.first - origin.first) + std::abs(xy.second - origin.second);
        endpoints.push_back(xy);
        child_state.push_back(state);
        result.cap  += state.cap;
        result.area += state.area;
        result.hpwl += state.hpwl;
        low.first    = std::min(low.first, xy.first);
        low.second   = std::min(low.second, xy.second);
        high.first   = std::max(high.first, xy.first);
        high.second  = std::max(high.second, xy.second);
        longest      = std::max(longest, length);
      }
      result.hpwl      += high.first - low.first + high.second - low.second;
      const auto wires  = steiner_tree(endpoints);
      for (const auto& [a, b] : wires.edges) {
        const auto p = wires.points[a], q = wires.points[b];
        result.cap += (std::abs(p.first - q.first) + std::abs(p.second - q.second)) * wire_c_ff_dbu;
      }
      // Elmore delay from the driver to every child over the tree.
      const size_t                       vertices = wires.points.size();
      std::vector<std::vector<unsigned>> adjacent(vertices);
      for (const auto& [a, b] : wires.edges) {
        adjacent[a].push_back(b);
        adjacent[b].push_back(a);
      }
      constexpr auto        none = std::numeric_limits<unsigned>::max();
      const auto            root_vertex = wires.terminals.front();
      std::vector<unsigned> parent(vertices, none), order{root_vertex};
      parent[root_vertex] = root_vertex;
      for (size_t k = 0; k < order.size(); ++k) {
        for (const auto n : adjacent[order[k]]) {
          if (parent[n] == none) {
            parent[n] = order[k];
            order.push_back(n);
          }
        }
      }
      std::vector<double> below(vertices, 0), length(vertices, 0), arrival(vertices, 0);
      for (size_t k = 0; k < children.size(); ++k) {
        below[wires.terminals[k + 1]] += child_state[k].cap;
      }
      for (size_t k = 1; k < order.size(); ++k) {
        const auto v = order[k], up = parent[v];
        length[v]    = std::abs(wires.points[v].first - wires.points[up].first) + std::abs(wires.points[v].second - wires.points[up].second);
      }
      for (size_t k = order.size(); k-- > 1;) {  // leaves first: a vertex's subtree is complete before its parent reads it
        const auto v = order[k];
        below[parent[v]] += below[v] + length[v] * wire_c_ff_dbu;
      }
      for (size_t k = 1; k < order.size(); ++k) {
        const auto v = order[k], up = parent[v];
        arrival[v]   = arrival[up] + length[v] * wire_r_ohm_dbu * (below[v] + .5 * length[v] * wire_c_ff_dbu) * 1e-3;
      }
      for (size_t k = 0; k < children.size(); ++k) {
        result.required = std::min(result.required, child_state[k].required - arrival[wires.terminals[k + 1]]);
      }
      return std::pair{result, longest};
    };
    bool viable = true;
    // Complete the tree at the root if its original driver cannot drive the
    // packet. In particular, a strong uniform buffer must not leave a weak
    // logic gate driving the same large cap that triggered repair.
    for (size_t i = 0; i <= plan.buffers.size(); ++i) {
      if (i == plan.buffers.size()) {
        const auto [packet, longest] = aggregate(root, plan.root);
        if (packet.cap <= driver.max_cap_ff + (2 * plan.root.size() + 2) * wire_c_ff_dbu && plan.root.size() <= driver.max_fanout
            && longest <= driver.max_distance_dbu + 2) {
          break;
        }
        if (plan.root.size() == 1 && plan.root.front().buffer && plan.buffers[plan.root.front().index].position == root) {
          note(candidate, "rejected: the root buffer cannot drive its own packet (cap, fanout or distance)");
          viable = false;
          break;
        }
        const auto children = std::move(plan.root);
        plan.buffers.push_back({root, children, 0});
        plan.root = {
            {true, static_cast<unsigned>(i)}
        };
      }
      auto& branch                 = plan.buffers[i];
      const auto [packet, longest] = aggregate(branch.position, branch.loads);
      (void)longest;
      double   best_input_required = -std::numeric_limits<double>::infinity();
      unsigned choice              = 0;
      bool     selected            = false;
      for (unsigned j = 0; j < drives.size(); ++j) {
        const auto& b = drives[j];
        // Repeater spacing generates alternatives; it is not an electrical
        // wire-length limit. Judge a longer branch using its RC delay, load
        // and fanout, just as for the original source.
        if (packet.cap > b.max_cap_ff * (1 + 1e-9) + (2 * branch.loads.size() + 2) * wire_c_ff_dbu
            || branch.loads.size() > b.max_fanout) {
          continue;
        }
        const double input_required = packet.required - b.intrinsic_ps - b.resistance_ohm * packet.cap * 1e-3;
        // Account for the cost of presenting this cell's input cap upstream.
        const double score          = input_required - driver.resistance_ohm * b.input_ff * 1e-3;
        if (!selected || score > best_input_required + 1e-6
            || (std::abs(score - best_input_required) <= 1e-6 && b.area < drives[choice].area)) {
          selected            = true;
          choice              = j;
          best_input_required = score;
        }
      }
      if (!selected) {
        if (trace) {
          std::fprintf(stderr, "[steiner] %zu loads candidate %zu: no drive fits branch %zu (%zu loads, cap %.1f fF)\n", loads.size(),
                       candidate, i, branch.loads.size(), packet.cap);
        }
        viable = false;
        break;
      }
      branch.drive  = choice;
      const auto& b = drives[choice];
      states.push_back(
          {b.input_ff, packet.required - b.intrinsic_ps - b.resistance_ohm * packet.cap * 1e-3, packet.area + b.area, packet.hpwl});
    }
    if (!viable) {
      continue;
    }
    const auto [packet, longest] = aggregate(root, plan.root);
    (void)longest;
    if (plan.buffers.size() > limits.cells || packet.area > limits.area + 1e-9 || packet.hpwl > spatial_hpwl_limit + 1e-9) {
      note(candidate,
           plan.buffers.size() > limits.cells ? "rejected: growth cell limit"
           : packet.area > limits.area + 1e-9 ? "rejected: growth area limit"
                                              : "rejected: spatial wire budget");
      continue;
    }
    const double required = packet.required - driver.intrinsic_ps - driver.resistance_ohm * packet.cap * 1e-3;
    if (trace) {
      std::fprintf(stderr, "[steiner] %zu loads candidate %zu: scored required %.1f ps, %zu buffers, area %.3f\n", loads.size(), candidate,
                   required, plan.buffers.size(), packet.area);
    }
    if (!found || required > best_required + 1e-6 || (std::abs(required - best_required) <= 1e-6 && packet.area < best_area)) {
      best          = std::move(plan);
      best_required = required;
      best_area     = packet.area;
      best_hpwl     = packet.hpwl;
      found         = true;
    }
  }
  if (!found || livehd::debug::env<"LHD_STEINER_DUMP_ALL">() != nullptr) {
    dump_plan_inputs(root, loads, capacitances, required_ps, drives, driver, wire_r_ohm_dbu, wire_c_ff_dbu, limits);
  }
  if (!found) {
    throw std::runtime_error("no buffer-tree alternative meets driver/load limits");
  }
  return best;
}
}  // namespace livehd::placement
