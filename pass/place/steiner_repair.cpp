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
    if (result.buffers.size() == 256) {
      throw std::runtime_error("Steiner repair exceeds 256 buffers");
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
      for (const auto& i : p) {
        if (i.cap > max_cap_ff * (1 + 1e-9)) {
          throw std::runtime_error("sink load exceeds selected buffer drive");
        }
        if (!group.empty() && (group.size() == max_fanout || total_cap(group) + i.cap > max_cap_ff)) {
          auto b = buffer(std::move(group), point);
          next.push_back(b.front());
          group.clear();
        }
        group.push_back(i);
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
}  // namespace livehd::placement
