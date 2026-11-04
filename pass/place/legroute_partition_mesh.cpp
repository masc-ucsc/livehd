// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>

#include "algorithms/flow/push_relabel.h"
#include "data_structure/mutable_graph.h"
#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
std::array<int, 4> neighbours(const Grid& g, int t) {
  return {g.col(t) > 0 ? t - 1 : -1,
          g.col(t) + 1 < g.cols ? t + 1 : -1,
          g.row(t) > 0 ? t - g.cols : -1,
          g.row(t) + 1 < g.rows ? t + g.cols : -1};
}
}  // namespace

bool connected_regions(const Grid& g, const std::vector<int>& regions) {
  if (regions.size() != g.size()) {
    return false;
  }
  std::vector<char> seen(g.size());
  std::set<int>     labels;
  std::vector<int>  queue;
  for (size_t seed = 0; seed < g.size(); ++seed) {
    if (seen[seed]) {
      continue;
    }
    if (regions[seed] < 0 || !labels.insert(regions[seed]).second) {
      return false;  // another connected component of a label already visited
    }
    seen[seed] = 1;
    queue.assign(1, int(seed));
    for (size_t at = 0; at < queue.size(); ++at) {
      for (int q : neighbours(g, queue[at])) {
        if (q >= 0 && !seen[size_t(q)] && regions[size_t(q)] == regions[seed]) {
          seen[size_t(q)] = 1;
          queue.push_back(q);
        }
      }
    }
  }
  return true;
}

namespace {
std::vector<int> partition_subset(const Grid& g, const std::vector<size_t>& cells, const Route_cuts& cuts, double critical_cost,
                                  unsigned max_cells, const std::vector<int>& input, bool mesh) {
  std::vector<int> regions(g.size(), -1), local(g.size(), -1);
  int              next_region = 0;
  const auto       split       = [&](auto&& self, const std::vector<int>& tiles) -> void {
    size_t total = 0;
    for (int t : tiles) {
      total += cells[size_t(t)];
    }
    const auto leaf = [&] {
      for (int t : tiles) {
        regions[size_t(t)] = next_region;
      }
      ++next_region;
    };
    if (total <= max_cells) {
      leaf();
      return;
    }
    if (!mesh) {
      std::vector<char> member(g.size(), 0);
      int               c0 = g.cols, c1 = 0, r0 = g.rows, r1 = 0;
      for (int t : tiles) {
        member[size_t(t)] = 1;
        c0                = std::min(c0, g.col(t));
        c1                = std::max(c1, g.col(t) + 1);
        r0                = std::min(r0, g.row(t));
        r1                = std::max(r1, g.row(t) + 1);
      }
      const bool rectangle = tiles.size() == size_t(c1 - c0) * size_t(r1 - r0);
      const auto connected = [&](const std::vector<int>& part) {
        std::vector<char> seen(g.size(), 1);
        for (int t : part) {
          seen[size_t(t)] = 0;
        }
        std::vector<int> queue{part.front()};
        seen[size_t(part.front())] = 1;
        for (size_t at = 0; at < queue.size(); ++at) {
          for (int q : neighbours(g, queue[at])) {
            if (q >= 0 && !seen[size_t(q)]) {
              seen[size_t(q)] = 1;
              queue.push_back(q);
            }
          }
        }
        return queue.size() == part.size();
      };
      double           best = std::numeric_limits<double>::infinity();
      std::vector<int> left, right;
      for (bool h : {true, false}) {
        for (int line = (h ? c0 : r0) + 1; line < (h ? c1 : r1); ++line) {
          size_t           count = 0, load = 0, critical = 0, capacity = 0;
          bool             allowed = true;
          std::vector<int> lo, hi;
          for (int t : tiles) {
            const bool lower = (h ? g.col(t) : g.row(t)) < line;
            (lower ? lo : hi).push_back(t);
            count += lower ? cells[size_t(t)] : 0;
            if ((h ? g.col(t) : g.row(t)) != line - 1) {
              continue;
            }
            const int q = t + (h ? 1 : g.cols);
            if (!member[size_t(q)]) {
              continue;
            }
            allowed  &= !(h ? cuts.forbidden_h : cuts.forbidden_v)[size_t(t)];
            load     += (h ? cuts.load_h : cuts.load_v)[size_t(t)];
            critical += (h ? cuts.critical_h : cuts.critical_v)[size_t(t)];
            capacity += size_t((h ? cuts.limit_h : cuts.limit_v)[size_t(t)]);
          }
          if (!allowed || !count || count == total || (!rectangle && (!connected(lo) || !connected(hi)))) {
            continue;
          }
          const double score
              = (double(load) + std::max(0.0, critical_cost) * double(critical)) / double(std::max<size_t>(1, capacity))
                + std::abs(2.0 * double(count) - double(total)) / double(total);
          if (score < best) {
            best  = score;
            left  = std::move(lo);
            right = std::move(hi);
          }
        }
      }
      if (left.empty()) {
        leaf();
      } else {
        self(self, left);
        self(self, right);
      }
      return;
    }
    std::fill(local.begin(), local.end(), -1);
    const size_t n = tiles.size();
    for (size_t i = 0; i < n; ++i) {
      local[size_t(tiles[i])] = int(i);
    }
    std::vector<std::vector<int>> adjacent(n);
    std::vector<int>              occupied;
    for (size_t i = 0; i < n; ++i) {
      if (cells[size_t(tiles[i])]) {
        occupied.push_back(int(i));
      }
      for (int q : neighbours(g, tiles[i])) {
        if (q >= 0 && local[size_t(q)] >= 0) {
          adjacent[i].push_back(local[size_t(q)]);
        }
      }
    }
    if (occupied.size() < 2) {
      leaf();  // one occupied tile is indivisible
      return;
    }
    const auto furthest = [&](int seed) {
      std::vector<int> distance(n, -1), queue{seed};
      distance[size_t(seed)] = 0;
      int far                = seed;
      for (size_t at = 0; at < queue.size(); ++at) {
        const int t = queue[at];
        if (cells[size_t(tiles[size_t(t)])] && distance[size_t(t)] > distance[size_t(far)]) {
          far = t;
        }
        for (int q : adjacent[size_t(t)]) {
          if (distance[size_t(q)] < 0) {
            distance[size_t(q)] = distance[size_t(t)] + 1;
            queue.push_back(q);
          }
        }
      }
      return far;
    };
    // Try a diameter and the two geometric extremes. Each pair grows connected terminal patches, so a
    // source/sink cut can follow a staircase around busy edges without producing detached islands.
    std::set<std::pair<int, int>> seeds;
    const auto                    seed_pair = [&](int a, int b) {
      if (a != b) {
        seeds.emplace(std::min(a, b), std::max(a, b));
      }
    };
    const int far = furthest(occupied.front());
    seed_pair(far, furthest(far));
    for (bool h : {true, false}) {
      const auto coord = [&](int i) {
        const int t = tiles[size_t(i)];
        return std::pair{h ? g.col(t) : g.row(t), t};
      };
      const auto [lo, hi]
          = std::minmax_element(occupied.begin(), occupied.end(), [&](int a, int b) { return coord(a) < coord(b); });
      seed_pair(*lo, *hi);
    }
    struct Edge {
      int      a, b;
      uint64_t weight;
      bool     forbidden;
      size_t   load, critical;
      int      limit;
    };
    std::vector<Edge> edges;
    // Bound the sum of all infinite anchor capacities as well as the finite weights in signed FlowType.
    const uint64_t    weight_limit = uint64_t(std::numeric_limits<FlowType>::max()) / (32 * (n + 2)) / (4 * n + 1);
    if (!weight_limit) {
      throw std::length_error("legroute.partition mesh is too large for flow capacities");
    }
    uint64_t finite_sum = 0;
    for (size_t i = 0; i < n; ++i) {
      for (int j : adjacent[i]) {
        if (size_t(j) <= i) {
          continue;
        }
        const int      a = tiles[i], b = tiles[size_t(j)];
        const bool     h    = g.row(a) == g.row(b);
        const size_t   at   = size_t(std::min(a, b));
        const size_t   load = (h ? cuts.load_h : cuts.load_v)[at], critical = (h ? cuts.critical_h : cuts.critical_v)[at];
        const int      limit     = (h ? cuts.limit_h : cuts.limit_v)[at];
        const bool     forbidden = (h ? cuts.forbidden_h : cuts.forbidden_v)[at];
        const double   cost      = 1 + 1024 * (double(load) + std::max(0.0, critical_cost) * double(critical)) / std::max(1, limit);
        const uint64_t weight    = uint64_t(std::ceil(std::min(double(weight_limit), cost)));
        edges.push_back({int(i), j, weight, forbidden, load, critical, limit});
        if (!forbidden) {
          finite_sum += weight;
        }
      }
    }
    const uint64_t    infinity  = finite_sum + 1;  // any forbidden edge costs more than every admissible cut
    double            best_cost = std::numeric_limits<double>::infinity();
    std::vector<char> best;
    for (const auto& [a, b] : seeds) {
      std::vector<int> anchor(n, -1);
      anchor[size_t(a)] = 0;
      anchor[size_t(b)] = 1;
      std::array<std::vector<int>, 2> queue{
          {{a}, {b}}
      };
      std::array<size_t, 2> head{0, 0}, count{cells[size_t(tiles[size_t(a)])], cells[size_t(tiles[size_t(b)])]};
      const size_t          quota    = std::max<size_t>(1, total / 4);
      bool                  progress = true;
      while ((count[0] < quota || count[1] < quota) && progress) {
        progress = false;
        for (int side = 0; side < 2; ++side) {
          if (count[size_t(side)] >= quota || head[size_t(side)] == queue[size_t(side)].size()) {
            continue;
          }
          progress    = true;
          const int t = queue[size_t(side)][head[size_t(side)]++];
          for (int q : adjacent[size_t(t)]) {
            if (anchor[size_t(q)] < 0 && count[size_t(side)] < quota) {
              anchor[size_t(q)] = side;
              queue[size_t(side)].push_back(q);
              count[size_t(side)] += cells[size_t(tiles[size_t(q)])];
            }
          }
        }
      }
      if (count[0] < quota || count[1] < quota) {
        continue;
      }
      auto graph = std::make_shared<mutable_graph>();
      graph->start_construction(NodeID(n + 2));
      for (size_t i = 0; i < n + 2; ++i) {
        graph->new_node();
      }
      for (const auto& e : edges) {
        graph->new_edge(NodeID(e.a), NodeID(e.b), e.forbidden ? infinity : e.weight);
      }
      for (size_t i = 0; i < n; ++i) {
        if (anchor[i] >= 0) {
          graph->new_edge(NodeID(i), NodeID(n + size_t(anchor[i])), infinity);
        }
      }
      graph->finish_construction();
      push_relabel<> flow;
      const auto     cut = flow.solve_max_flow_min_cut(graph, {NodeID(n), NodeID(n + 1)}, 0, true, 0, 0);
      if (cut.first < 0 || uint64_t(cut.first) >= infinity) {
        continue;
      }
      std::vector<char> on(n);
      for (NodeID v : cut.second) {
        if (size_t(v) < n) {
          on[size_t(v)] = 1;
        }
      }
      size_t left = 0, load = 0, capacity = 0, critical = 0;
      for (size_t i = 0; i < n; ++i) {
        if (on[i]) {
          left += cells[size_t(tiles[i])];
        }
      }
      if (!left || left == total) {
        continue;
      }
      bool valid = true;
      for (const auto& e : edges) {
        if (on[size_t(e.a)] != on[size_t(e.b)]) {
          valid    &= !e.forbidden;
          load     += e.load;
          critical += e.critical;
          capacity += size_t(e.limit);
        }
      }
      // Check connectivity explicitly; correctness does not depend on the flow solver's choice among ties.
      for (int side = 0; valid && side < 2; ++side) {
        std::vector<char> seen(n);
        std::vector<int>  visit;
        for (size_t i = 0; i < n; ++i) {
          if (on[i] == side) {
            visit.push_back(int(i));
            seen[i] = 1;
            break;
          }
        }
        for (size_t at = 0; at < visit.size(); ++at) {
          for (int q : adjacent[size_t(visit[at])]) {
            if (!seen[size_t(q)] && on[size_t(q)] == side) {
              seen[size_t(q)] = 1;
              visit.push_back(q);
            }
          }
        }
        valid &= visit.size() == size_t(std::ranges::count(on, char(side)));
      }
      const double score = (double(load) + std::max(0.0, critical_cost) * double(critical)) / double(std::max<size_t>(1, capacity))
                           + std::abs(2.0 * double(left) - double(total)) / double(total);
      if (valid && score < best_cost) {
        best_cost = score;
        best      = std::move(on);
      }
    }
    if (best.empty()) {
      leaf();
      return;
    }
    std::vector<int> left, right;
    for (size_t i = 0; i < n; ++i) {
      (best[i] ? left : right).push_back(tiles[i]);
    }
    self(self, left);
    self(self, right);
  };
  split(split, input);
  return regions;
}
}  // namespace

std::vector<int> partition_mesh_regions(const Grid& g, const std::vector<size_t>& cells, const Route_cuts& cuts,
                                        double critical_cost, unsigned max_cells) {
  std::vector<int> tiles(g.size());
  std::iota(tiles.begin(), tiles.end(), 0);
  auto regions = partition_subset(g, cells, cuts, critical_cost, max_cells, tiles, true);
  if (!connected_regions(g, regions) || !cuts.allows(g, regions)) {
    throw std::logic_error("legroute.partition produced a disconnected region or a forbidden cut");
  }
  return regions;
}

std::vector<int> refine_route_regions(const Grid& g, const std::vector<size_t>& cells, const Route_cuts& cuts, double critical_cost,
                                      const std::vector<unsigned>& targets, bool mesh) {
  if (g.regions <= 0 || targets.size() != size_t(g.regions) || cells.size() != g.size() || !connected_regions(g, g.region)
      || !cuts.allows(g, g.region) || !std::isfinite(critical_cost) || !(critical_cost == -1 || critical_cost >= 0)) {
    throw std::invalid_argument("legroute.partition refinement requires a valid connected partition and one target per region");
  }
  std::vector<std::vector<int>> tiles(size_t(g.regions));
  for (size_t t = 0; t < g.size(); ++t) {
    if (g.region[t] >= g.regions) {
      throw std::invalid_argument("legroute.partition refinement region label is out of range");
    }
    tiles[size_t(g.region[t])].push_back(int(t));
  }
  auto regions = g.region;
  int  next    = g.regions;
  for (int r = 0; r < g.regions; ++r) {
    if (!targets[size_t(r)] || tiles[size_t(r)].empty()) {
      continue;
    }
    const auto         children = partition_subset(g, cells, cuts, critical_cost, targets[size_t(r)], tiles[size_t(r)], mesh);
    std::map<int, int> labels{
        {0, r}
    };
    for (int t : tiles[size_t(r)]) {
      const int child = children[size_t(t)];
      if (!labels.contains(child)) {
        labels.emplace(child, next++);
      }
      regions[size_t(t)] = labels.at(child);
    }
  }
  if (!connected_regions(g, regions) || !cuts.allows(g, regions)) {
    throw std::logic_error("legroute.partition refinement produced a disconnected region or a forbidden cut");
  }
  return regions;
}
}  // namespace livehd::placement::lr
