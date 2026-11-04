// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors.
// All rights reserved.
//
// Native adaptation of the L and HVH/VHV Z candidate selection and boundary-cost
// accumulation in OpenROAD src/grt/src/fastroute/src/route.cpp (routeSegL,
// newrouteZ_edge, newrouteZ), revision a3bffd17695ecd1d6d9a03318f49f153c4f21e64.
// OpenDB identities, fixed-degree trees, global demand state and technology/STA
// hooks are replaced by caller topology and native edge costs. This is a pattern
// stage, not the complete FastRoute algorithm or its layer assignment.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "fast_route_pattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace livehd::placement {
namespace {
using Edge = std::pair<int, int>;
struct Cost {
  long double value   = 0;
  size_t      blocked = 0, added = 0;
  Cost        operator+(const Cost& b) const { return {value + b.value, blocked + b.blocked, added + b.added}; }
  Cost        operator-(const Cost& b) const { return {value - b.value, blocked - b.blocked, added - b.added}; }
};
struct Work_limit {};
}  // namespace

Pattern_route fast_route_pattern(int cols, int rows, std::span<const int> vertices,
                                 std::span<const std::pair<size_t, size_t>> branches,
                                 const std::function<double(int, int)>& edge_cost, size_t work_limit, Pattern_route_mode mode) {
  if (cols <= 0 || rows <= 0 || int64_t(cols) * rows > std::numeric_limits<int>::max() || !edge_cost) {
    throw std::invalid_argument("pattern route: invalid grid or edge-cost callback");
  }
  Pattern_route result;
  const auto    tick = [&] {
    if (result.work == work_limit) {
      throw Work_limit{};
    }
    ++result.work;
  };
  try {
    // Validate the supplied topology without allocating any whole-grid storage.
    if (vertices.size() > work_limit) {
      throw Work_limit{};
    }
    std::vector<size_t> parent(vertices.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](size_t x) {
      while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x         = parent[x];
      }
      return x;
    };
    std::unordered_map<int, size_t> coincident;
    for (size_t i = 0; i < vertices.size(); ++i) {
      tick();
      if (vertices[i] < 0 || vertices[i] >= cols * rows) {
        throw std::invalid_argument("pattern route: vertex outside grid");
      }
      const auto [it, inserted] = coincident.emplace(vertices[i], i);
      if (!inserted) {
        parent[root(i)] = root(it->second);
      }
    }
    for (const auto& [a, b] : branches) {
      tick();
      if (a >= vertices.size() || b >= vertices.size()) {
        throw std::invalid_argument("pattern route: invalid branch vertex");
      }
      parent[root(a)] = root(b);
    }
    for (size_t i = 1; i < vertices.size(); ++i) {
      if (root(i) != root(0)) {
        return result;
      }
    }

    std::map<Edge, double> used;
    // The callback is stable within this call. Retain actual cost separately from
    // the marginal score, so shared metal is paid exactly once in the result.
    std::map<Edge, double> evaluated;
    const auto             cost = [&](int a, int b) -> Cost {
      tick();
      const Edge edge     = std::minmax(a, b);
      auto [it, inserted] = evaluated.try_emplace(edge, 0);
      if (inserted) {
        it->second = edge_cost(edge.first, edge.second);
        if (it->second < 0) {
          throw std::invalid_argument("pattern route: negative edge cost");
        }
      }
      if (!std::isfinite(it->second)) {
        return {0, 1, 0};
      }
      return used.contains(edge) ? Cost{} : Cost{it->second, 0, 1};
    };
    const auto tile = [cols](int x, int y) { return y * cols + x; };
    for (const auto& [va, vb] : branches) {
      // Canonical orientation makes endpoint reversal choose the same ties.
      const auto [a, b] = std::minmax(vertices[va], vertices[vb]);
      if (a == b) {
        continue;
      }
      const int  ax = a % cols, ay = a / cols, bx = b % cols, by = b / cols;
      const int  xl = std::min(ax, bx), xh = std::max(ax, bx), yl = ay, yh = by;
      const int  width = xh - xl, height = yh - yl;
      const auto better = [](const Cost& candidate, const Cost& incumbent) {
        return !candidate.blocked
               && (incumbent.blocked || candidate.value < incumbent.value
                   || (candidate.value == incumbent.value && candidate.added < incumbent.added));
      };
      const auto step_toward
          = [cols](int from, int to) { return from / cols == to / cols ? (from < to ? 1 : -1) : (from < to ? cols : -cols); };
      int        cursor = a;
      const auto line   = [&](int target) {
        const int step = step_toward(cursor, target);
        while (cursor != target) {
          tick();
          const int  next = cursor + step;
          const Edge edge = std::minmax(cursor, next);
          used.try_emplace(edge, evaluated.at(edge));
          cursor = next;
        }
      };
      if (mode == Pattern_route_mode::l_first) {
        const auto line_cost = [&](int from, int to) {
          Cost      value;
          const int step = step_toward(from, to);
          while (from != to && !value.blocked) {
            value  = value + cost(from, from + step);
            from  += step;
          }
          return value;
        };
        Cost best_l{0, 1, 0};
        int  bend_l = xl;
        for (int x : {xl, xh}) {
          tick();
          Cost value = line_cost(a, tile(x, ay));
          if (!value.blocked) {
            value = value + line_cost(tile(x, ay), tile(x, by));
          }
          if (!value.blocked) {
            value = value + line_cost(tile(x, by), b);
          }
          if (better(value, best_l)) {
            best_l = value;
            bend_l = x;
          }
          if (!width || !height) {
            break;  // Collinear endpoints have only one distinct L candidate.
          }
        }
        if (!best_l.blocked) {
          line(tile(bend_l, ay));
          line(tile(bend_l, by));
          line(b);
          continue;
        }
      }
      // Every boundary element entails work; reject before allocating a buffer
      // whose dimensions exceed the remaining budget.
      if (size_t(width) + size_t(height) + 2 > work_limit - result.work) {
        throw Work_limit{};
      }
      std::vector<Cost> bottom(size_t(width) + 1), top(size_t(width) + 1);
      std::vector<Cost> left(size_t(height) + 1), right(size_t(height) + 1);
      std::vector<Cost> horizontal(size_t(height) + 1), vertical(size_t(width) + 1);
      // Scan each rectangle edge once. Prefix boundary costs plus whole row/column
      // costs evaluate every L/Z candidate in O(width*height + width + height).
      for (int y = yl; y <= yh; ++y) {
        for (int x = xl; x < xh; ++x) {
          const Cost c               = cost(tile(x, y), tile(x + 1, y));
          horizontal[size_t(y - yl)] = horizontal[size_t(y - yl)] + c;
          if (y == yl) {
            bottom[size_t(x - xl) + 1] = bottom[size_t(x - xl)] + c;
          }
          if (y == yh) {
            top[size_t(x - xl) + 1] = top[size_t(x - xl)] + c;
          }
        }
      }
      for (int x = xl; x <= xh; ++x) {
        for (int y = yl; y < yh; ++y) {
          const Cost c             = cost(tile(x, y), tile(x, y + 1));
          vertical[size_t(x - xl)] = vertical[size_t(x - xl)] + c;
          if (x == xl) {
            left[size_t(y - yl) + 1] = left[size_t(y - yl)] + c;
          }
          if (x == xh) {
            right[size_t(y - yl) + 1] = right[size_t(y - yl)] + c;
          }
        }
      }
      const auto interval = [](const std::vector<Cost>& prefix, int u, int v) {
        const auto [lo, hi] = std::minmax(u, v);
        return prefix[size_t(hi)] - prefix[size_t(lo)];
      };
      Cost       best{0, 1, 0};
      bool       best_hvh  = true;
      int        best_bend = 0;
      const auto consider  = [&](const Cost& candidate, bool hvh, int bend) {
        tick();
        if (better(candidate, best)) {
          best      = candidate;
          best_hvh  = hvh;
          best_bend = bend;
        }
      };
      for (int x = xl; x <= xh; ++x) {
        consider(interval(bottom, ax - xl, x - xl) + vertical[size_t(x - xl)] + interval(top, x - xl, bx - xl), true, x);
      }
      const auto& start_side = ax == xl ? left : right;
      const auto& end_side   = bx == xl ? left : right;
      // Boundary rows duplicate the L candidates already considered above.
      for (int y = yl + 1; y < yh; ++y) {
        consider(interval(start_side, 0, y - yl) + horizontal[size_t(y - yl)] + interval(end_side, y - yl, height), false, y);
      }
      if (best.blocked) {
        return result;
      }
      if (best_hvh) {
        line(tile(best_bend, ay));
        line(tile(best_bend, by));
      } else {
        line(tile(ax, best_bend));
        line(tile(bx, best_bend));
      }
      line(b);
    }
    long double total = 0;
    for (const auto& [edge, value] : used) {
      result.edges.push_back(edge);
      total += value;
    }
    if (total > std::numeric_limits<double>::max()) {
      throw std::overflow_error("pattern route: total edge cost is not representable");
    }
    result.cost   = double(total);
    result.status = Pattern_route_status::complete;
  } catch (const Work_limit&) {
    result.status = Pattern_route_status::work_limit;
  }
  return result;
}
}  // namespace livehd::placement
