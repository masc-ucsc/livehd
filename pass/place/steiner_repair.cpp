// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "steiner_repair.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
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

Buffer_tree plan_buffer_choices(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                                const std::vector<double>& required_ps, const std::vector<Buffer_drive>& drives,
                                const Buffer_drive& driver, double wire_r_ohm_dbu, double wire_c_ff_dbu,
                                const Buffer_limits& limits) {
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
  bool                      found = false;
  std::vector<Buffer_drive> topologies;
  for (const auto& drive : drives) {
    std::vector<unsigned> fanouts{std::min(4u, drive.max_fanout), std::min(16u, drive.max_fanout), drive.max_fanout};
    std::sort(fanouts.begin(), fanouts.end());
    fanouts.erase(std::unique(fanouts.begin(), fanouts.end()), fanouts.end());
    for (const auto fanout : fanouts) {
      auto topology       = drive;
      topology.max_fanout = fanout;
      topologies.push_back(topology);
    }
  }
  // Include both the original connection and a single source repeater. A
  // source can satisfy electrical limits yet benefit from isolating its load;
  // requiring an electrical violation would omit this compact alternative.
  for (size_t candidate = 0; candidate < topologies.size() + 2; ++candidate) {
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
      } else {
        const auto& topology = topologies[candidate - 2];
        plan                 = plan_buffers(root,
                                            loads,
                                            capacitances,
                                            topology.input_ff,
                                            topology.max_cap_ff,
                                            topology.max_fanout,
                                            topology.max_distance_dbu,
                                            wire_c_ff_dbu);
      }
    } catch (const std::runtime_error&) {
      continue;
    }
    struct State {
      double cap, required, area;
    };
    std::vector<State> states;
    const auto         aggregate = [&](Point origin, const std::vector<Buffer_tree::Load>& children) {
      State              result{0, std::numeric_limits<double>::infinity(), 0};
      double             longest = 0;
      std::vector<Point> endpoints{origin};
      for (const auto& child : children) {
        const auto xy = child.buffer ? plan.buffers.at(child.index).position : loads.at(child.index);
        const auto state
            = child.buffer ? states.at(child.index) : State{capacitances.at(child.index), required_ps.at(child.index), 0};
        const double length     = std::abs(xy.first - origin.first) + std::abs(xy.second - origin.second);
        const double wire_cap   = length * wire_c_ff_dbu;
        const double wire_delay = length * wire_r_ohm_dbu * (state.cap + .5 * wire_cap) * 1e-3;
        endpoints.push_back(xy);
        result.cap      += state.cap;
        result.required  = std::min(result.required, state.required - wire_delay);
        result.area     += state.area;
        longest          = std::max(longest, length);
      }
      const auto wires = steiner_tree(endpoints);
      for (const auto& [a, b] : wires.edges) {
        const auto p = wires.points[a], q = wires.points[b];
        result.cap += (std::abs(p.first - q.first) + std::abs(p.second - q.second)) * wire_c_ff_dbu;
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
        viable = false;
        break;
      }
      branch.drive  = choice;
      const auto& b = drives[choice];
      states.push_back({b.input_ff, packet.required - b.intrinsic_ps - b.resistance_ohm * packet.cap * 1e-3, packet.area + b.area});
    }
    if (!viable) {
      continue;
    }
    const auto [packet, longest] = aggregate(root, plan.root);
    (void)longest;
    if (plan.buffers.size() > limits.cells || packet.area > limits.area + 1e-9) {
      continue;
    }
    const double required = packet.required - driver.intrinsic_ps - driver.resistance_ohm * packet.cap * 1e-3;
    if (!found || required > best_required + 1e-6 || (std::abs(required - best_required) <= 1e-6 && packet.area < best_area)) {
      best          = std::move(plan);
      best_required = required;
      best_area     = packet.area;
      found         = true;
    }
  }
  if (!found) {
    throw std::runtime_error("no buffer-tree alternative meets driver/load limits");
  }
  return best;
}
}  // namespace livehd::placement
