// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "steiner_topology.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <string>
#include <tuple>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include "inou/lefdef/index_union_find.hpp"
#include "thirdparty/flute/flute.hpp"
namespace livehd::placement {
namespace fs = std::filesystem;
std::unique_ptr<rules_cc::cc::runfiles::Runfiles> runfiles() {
  using rules_cc::cc::runfiles::Runfiles;
  std::string               error;
  std::unique_ptr<Runfiles> result(Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error));
  if (result) {
    return result;
  }
  // Standard executable-adjacent Bazel runfiles. /proc is available on Linux;
  // macOS supplies _NSGetExecutablePath through the platform branch below.
  std::string executable;
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  executable.resize(size);
  _NSGetExecutablePath(executable.data(), &size);
  executable.resize(std::char_traits<char>::length(executable.c_str()));
#else
  std::error_code ec;
  executable = fs::read_symlink("/proc/self/exe", ec).string();
#endif
  result.reset(Runfiles::Create(executable, BAZEL_CURRENT_REPOSITORY, &error));
  if (!result) {
    throw std::runtime_error("cannot locate FLUTE runfiles: " + error);
  }
  return result;
}
void initialize_flute_tables() {
  static std::once_flag once;
  std::call_once(once, [] {
    auto       files  = runfiles();
    const auto locate = [&](const char* name) {
      // Resolve the apparent archive name through this module's repository mapping.
      auto path = files->Rlocation(std::string("dreamplace/thirdparty/flute/lut.ICCAD2015/") + name);
      if (!fs::is_regular_file(path)) {
        throw std::runtime_error("FLUTE lookup table missing from runfiles: " + path);
      }
      return path;
    };
    const auto powv = locate("POWV9.dat"), post = locate("POST9.dat");
    flute::readLUT(powv.c_str(), post.c_str());
  });
}

Steiner_topology steiner_topology(const std::vector<Steiner_location>& pins) {
  if (pins.empty()) {
    throw std::invalid_argument("Steiner topology requires pins");
  }
  std::map<Steiner_location, size_t> vertices;
  constexpr auto                     limit = std::numeric_limits<int>::max() / 4;
  for (const auto& p : pins) {
    if (p.first < -limit || p.first > limit || p.second < -limit || p.second > limit) {
      throw std::invalid_argument("coordinates exceed FLUTE integer range");
    }
    vertices.emplace(p, 0);
  }
  if (vertices.size() > 1000) {
    throw std::invalid_argument("Steiner topology exceeds FLUTE degree limit");
  }
  Steiner_topology out;
  std::vector<int> xs, ys;
  for (auto& [p, index] : vertices) {
    index = out.locations.size();
    out.locations.push_back(p);
    xs.push_back(p.first);
    ys.push_back(p.second);
  }
  for (const auto& p : pins) {
    out.terminals.push_back(vertices.at(p));
  }
  if (vertices.size() == 1) {
    return out;
  }
  initialize_flute_tables();
  auto                                                 tree = flute::flute(xs.size(), xs.data(), ys.data(), 3);
  // Keep ownership across allocations/validation failures below.
  std::unique_ptr<flute::Branch, decltype(&std::free)> branches(tree.branch, &std::free);
  if (!branches || tree.deg != static_cast<int>(xs.size())) {
    throw std::runtime_error("invalid FLUTE topology");
  }
  const auto vertex = [&](Steiner_location p) {
    auto [it, added] = vertices.emplace(p, out.locations.size());
    if (added) {
      out.locations.push_back(p);
    }
    return it->second;
  };
  for (int i = 0; i < 2 * tree.deg - 2; ++i) {
    const auto& a = tree.branch[i];
    if (a.n < 0 || a.n >= 2 * tree.deg - 2) {
      throw std::runtime_error("invalid FLUTE branch index");
    }
    const auto& b  = tree.branch[a.n];
    const auto  ai = vertex({a.x, a.y}), bi = vertex({b.x, b.y});
    if (ai != bi) {
      out.edges.emplace_back(std::min(ai, bi), std::max(ai, bi));
    }
  }
  std::sort(out.edges.begin(), out.edges.end());
  out.edges.erase(std::unique(out.edges.begin(), out.edges.end()), out.edges.end());
  // Zero-length branch coalescing must leave a tree for RC traversal.
  physical::Index_union_find connected(out.locations.size());
  std::erase_if(out.edges, [&](const auto& edge) { return !connected.merge(edge.first, edge.second); });
  if (out.edges.size() + 1 != out.locations.size()) {
    throw std::runtime_error("disconnected FLUTE topology");
  }
  return out;
}

std::vector<std::vector<size_t>> detail::timing_neighbors(const std::vector<Steiner_location>& locations) {
  const size_t count = locations.size();
  // Linear scans have lower setup cost on small nets; indexed queries pay off
  // only once the quadratic scan dominates. This changes no neighbor choices.
  if (count <= 512) {
    std::vector<size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
      return std::pair{locations[a].second, locations[a].first} < std::pair{locations[b].second, locations[b].first};
    });
    std::vector<std::vector<size_t>> neighbors(count);
    std::vector<int64_t>             upper_right(count, std::numeric_limits<int64_t>::max());
    std::vector<int64_t>             upper_left(count, std::numeric_limits<int64_t>::min());
    for (size_t idx = 0; idx < count; ++idx) {
      const auto n = order[idx];
      const auto x = locations[n].first;
      for (size_t i = 0; i < idx; ++i) {
        const auto below = order[i];
        const auto bx    = locations[below].first;
        if (bx <= x && x < upper_right[below]) {
          neighbors[below].push_back(n);
          upper_right[below] = x;
        } else if (upper_left[below] < x && x < bx) {
          neighbors[below].push_back(n);
          upper_left[below] = x;
        }
      }
      int64_t right = std::numeric_limits<int64_t>::max(), left = std::numeric_limits<int64_t>::min();
      for (size_t i = idx; i > 0; --i) {
        const auto below = order[i - 1];
        const auto bx    = locations[below].first;
        if (x <= bx && bx < right) {
          neighbors[n].push_back(below);
          right = bx;
        } else if (left < bx && bx < x) {
          neighbors[n].push_back(below);
          left = bx;
        }
      }
    }
    return neighbors;
  }
  std::vector<size_t> order(count), x_rank(count);
  std::iota(order.begin(), order.end(), 0);
  std::vector<int64_t> xs;
  for (const auto& p : locations) {
    xs.push_back(p.first);
  }
  std::sort(xs.begin(), xs.end());
  xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  for (size_t i = 0; i < count; ++i) {
    x_rank[i] = std::lower_bound(xs.begin(), xs.end(), locations[i].first) - xs.begin();
  }
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
    return std::pair{locations[a].second, locations[a].first} < std::pair{locations[b].second, locations[b].first};
  });
  size_t base = 1;
  while (base < xs.size()) {
    base *= 2;
  }
  std::vector<size_t> tree(2 * base);
  const auto          latest = [&](size_t left, size_t right) {
    size_t stamp = 0;
    for (left += base, right += base; left < right; left /= 2, right /= 2) {
      if (left & 1) {
        stamp = std::max(stamp, tree[left++]);
      }
      if (right & 1) {
        stamp = std::max(stamp, tree[--right]);
      }
    }
    return stamp;
  };
  std::vector<std::vector<size_t>> neighbors(count);
  // Separated rows can expose quadratically many visible pairs. Preserve
  // previous inputs exactly, but bound work before trying a spatial fallback
  // on larger nets. Count both directed sweeps in the budget.
  size_t                           visible_edges = 0;
  const size_t                     edge_budget   = count <= 1000 ? std::numeric_limits<size_t>::max() : 64 * count;
  for (bool reverse : {false, true}) {
    std::fill(tree.begin(), tree.end(), 0);
    for (size_t step = 0; step < count; ++step) {
      const size_t n    = order[reverse ? count - step - 1 : step];
      const size_t x    = x_rank[n];
      size_t       left = latest(0, x), right = latest(x, xs.size());
      // The newest visible point in each quadrant bounds the next query.
      // Merge the quadrants by sweep time, reproducing the old scan's order.
      while (left || right) {
        const size_t stamp = std::max(left, right);
        const size_t other = order[reverse ? count - stamp : stamp - 1];
        if (++visible_edges > edge_budget) {
          throw std::length_error("timing Steiner visibility exceeds work bound");
        }
        neighbors[n].push_back(other);
        if (left > right) {
          left = latest(x_rank[other] + 1, x);
        } else {
          right = latest(x, x_rank[other]);
        }
      }
      size_t node = base + x;
      tree[node]  = step + 1;
      while ((node /= 2) != 0) {
        tree[node] = std::max(tree[2 * node], tree[2 * node + 1]);
      }
    }
  }
  return neighbors;
}

Steiner_topology timing_steiner_topology(const std::vector<Steiner_location>& pins, size_t driver) {
  if (driver >= pins.size()) {
    throw std::invalid_argument("timing Steiner topology requires a valid driver");
  }
  std::map<Steiner_location, size_t> vertices;
  constexpr auto                     limit = std::numeric_limits<int>::max() / 4;
  for (const auto& p : pins) {
    if (p.first < -limit || p.first > limit || p.second < -limit || p.second > limit) {
      throw std::invalid_argument("coordinates exceed Steiner integer range");
    }
    vertices.emplace(p, 0);
  }
  Steiner_topology out;
  for (auto& [p, index] : vertices) {
    index = out.locations.size();
    out.locations.push_back(p);
  }
  for (const auto& p : pins) {
    out.terminals.push_back(vertices.at(p));
  }
  const size_t count = out.locations.size();
  if (count == 1) {
    return out;
  }
  const auto distance = [&](size_t a, size_t b) {
    return std::abs(out.locations[a].first - out.locations[b].first) + std::abs(out.locations[a].second - out.locations[b].second);
  };
  // Indexed quadrant queries cost O((pins + visible edges) log pins), rather
  // than scanning every earlier pin, without changing neighbor or tie order.
  const auto neighbors = detail::timing_neighbors(out.locations);
  // Alpha weights distance already traveled from the driver. An unrooted
  // minimum-length tree can leave distant sinks behind long shared RC trunks.
  // The 0.3 balance matches the placement estimator used for independent STA.
  using Search         = std::tuple<float, size_t, size_t, int64_t, size_t>;
  std::priority_queue<Search, std::vector<Search>, std::greater<Search>> search;
  std::vector<float>                                                     weights(count, std::numeric_limits<float>::infinity());
  std::vector<size_t>                                                    generations(count);
  std::vector<bool>                                                      visited(count);
  std::vector<std::vector<size_t>>                                       adjacent(count);
  const auto                                                             connect = [&](size_t a, size_t b) {
    adjacent[a].push_back(b);
    adjacent[b].push_back(a);
  };
  const size_t root = out.terminals[driver];
  weights[root]     = 0;
  search.emplace(0, root, root, 0, 0);
  size_t reached = 0;
  while (!search.empty()) {
    const auto [weight, parent, node, length, generation] = search.top();
    search.pop();
    if (visited[node] || generation != generations[node]) {
      continue;
    }
    visited[node] = true;
    ++reached;
    if (node != root) {
      connect(parent, node);
    }
    for (const auto next : neighbors[node]) {
      if (visited[next]) {
        continue;
      }
      const auto  edge_length = distance(node, next);
      const float cost        = edge_length + 0.3F * length;
      if (cost <= weights[next]) {
        weights[next] = cost;
        search.emplace(cost, node, next, length + edge_length, ++generations[next]);
      }
    }
  }
  if (reached != count) {
    throw std::logic_error("disconnected timing Steiner neighbor graph");
  }
  // Share the longest common rectilinear portion of two incident branches.
  // This never increases driver-to-sink path length and reduces wire load.
  struct Candidate {
    int64_t          gain;
    Steiner_location point;
    size_t           node, first, second, generation;
    bool             operator<(const Candidate& other) const {
      return std::tie(gain, point, node) < std::tie(other.gain, other.point, other.node);
    }
  };
  std::priority_queue<Candidate> candidates;
  generations.assign(count, 0);
  size_t       branch_work   = 0;
  const size_t branch_budget = count <= 1000 ? std::numeric_limits<size_t>::max() : 1024 * count;
  const auto   refresh       = [&](size_t node) {
    const auto  center = out.locations[node];
    Candidate   best{0, center, node, node, node, ++generations[node]};
    const auto& links = adjacent[node];
    for (size_t i = links.size(); i > 0; --i) {
      const auto a = out.locations[links[i - 1]];
      for (size_t j = i - 1; j > 0; --j) {
        if (++branch_work > branch_budget) {
          throw std::length_error("timing Steiner branch sharing exceeds work bound");
        }
        const auto             b = out.locations[links[j - 1]];
        const Steiner_location point{std::clamp(center.first, std::min(a.first, b.first), std::max(a.first, b.first)),
                                     std::clamp(center.second, std::min(a.second, b.second), std::max(a.second, b.second))};
        const auto             gain = std::abs(point.first - center.first) + std::abs(point.second - center.second);
        if (gain > best.gain) {
          best.gain   = gain;
          best.point  = point;
          best.first  = links[i - 1];
          best.second = links[j - 1];
        }
      }
    }
    if (best.gain > 0) {
      candidates.push(best);
    }
  };
  for (size_t node = 0; node < count; ++node) {
    refresh(node);
  }
  while (!candidates.empty()) {
    const auto best = candidates.top();
    candidates.pop();
    if (best.generation != generations[best.node]) {
      continue;
    }
    size_t junction = out.locations.size();
    if (best.point == out.locations[best.first]) {
      junction = best.first;
    } else if (best.point == out.locations[best.second]) {
      junction = best.second;
    }
    const bool added = junction == out.locations.size();
    if (added) {
      out.locations.push_back(best.point);
      adjacent.emplace_back();
      generations.push_back(0);
    }
    for (const auto other : {best.first, best.second}) {
      if (other != junction) {
        std::erase(adjacent[best.node], other);
        std::erase(adjacent[other], best.node);
        connect(junction, other);
      }
    }
    if (added) {
      connect(best.node, junction);
      refresh(junction);
    }
    refresh(best.node);
    refresh(best.first);
    refresh(best.second);
  }
  for (size_t a = 0; a < adjacent.size(); ++a) {
    for (const auto b : adjacent[a]) {
      if (a < b) {
        out.edges.emplace_back(a, b);
      }
    }
  }
  if (out.edges.size() + 1 != out.locations.size()) {
    throw std::logic_error("invalid timing Steiner topology");
  }
  return out;
}

}  // namespace livehd::placement
