// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_demand.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace livehd::placement {
Chunk_demand::Chunk_demand(Box box, double requested, Pair rates) : frame(box), pitch(requested), tracks(rates) {
  if (!(pitch > 0 && frame[2] > frame[0] && frame[3] > frame[1])) {
    throw std::invalid_argument("invalid routing grid");
  }
  // Bound scratch memory even for a chip spanning many millions of tracks. Report the effective pitch.
  pitch = std::max({pitch, (frame[2] - frame[0]) / 1024, (frame[3] - frame[1]) / 1024});
  nx    = std::max(1, int(std::ceil((frame[2] - frame[0]) / pitch)));
  ny    = std::max(1, int(std::ceil((frame[3] - frame[1]) / pitch)));
  grid.resize(nx * ny);
  seen.resize(2 * nx * ny, -1);
}
std::array<int, 2> Chunk_demand::at(Pair p) const {
  return {int(std::clamp((p[0] - frame[0]) / pitch, 0., double(nx - 1))),
          int(std::clamp((p[1] - frame[1]) / pitch, 0., double(ny - 1)))};
}
double Chunk_demand::extent(int axis, int index) const { return std::min(pitch, frame[axis + 2] - frame[axis] - index * pitch); }
void   Chunk_demand::reserve(Box b, Pair fraction, bool internal_demand) {
  const auto lo = at({b[0], b[1]}), hi = at({b[2], b[3]});
  for (int y = lo[1]; y <= hi[1]; ++y) {
    for (int x = lo[0]; x <= hi[0]; ++x) {
      const double l = frame[0] + x * pitch, bot = frame[1] + y * pitch;
      const Pair   size{extent(0, x), extent(1, y)};
      const double w = std::min(l + size[0], b[2]) - std::max(l, b[0]), h = std::min(bot + size[1], b[3]) - std::max(bot, b[1]);
      auto&        tile = grid[y * nx + x];
      for (int k = 0; k < 2; ++k) {
        double share = std::max(0., w) * std::max(0., h) / (size[0] * size[1]) * fraction[k];
        if (internal_demand) {
          tile.internal[k] += share * tracks[k] * size[1 - k];
        } else {
          tile.blocked[k] += share;
        }
      }
    }
  }
}
void Chunk_demand::internal(Box b) {
  const auto   lo = at({b[0], b[1]}), hi = at({b[2], b[3]});
  const double scale = 1. / (pitch * (1 + hi[0] - lo[0]) * (1 + hi[1] - lo[1]));
  if (difference.empty()) {
    difference.resize((nx + 1) * (ny + 1));
  }
  for (int k = 0; k < 2; ++k) {
    double amount = (b[k + 2] - b[k]) * scale;
    for (auto [x, sx] : {std::pair(lo[0], 1), std::pair(hi[0] + 1, -1)}) {
      for (auto [y, sy] : {std::pair(lo[1], 1), std::pair(hi[1] + 1, -1)}) {
        difference[y * (nx + 1) + x][k] += sx * sy * amount;
      }
    }
  }
}
void Chunk_demand::integrate() {
  if (difference.empty()) {
    return;
  }
  for (int y = 0; y < ny; ++y) {
    Pair row{};
    for (int x = 0; x < nx; ++x) {
      for (int k = 0; k < 2; ++k) {
        auto& value                   = difference[y * (nx + 1) + x][k];
        row[k]                       += value;
        value                         = row[k] + (y ? difference[(y - 1) * (nx + 1) + x][k] : 0.);
        grid[y * nx + x].internal[k] += std::max(0., value);
      }
    }
  }
  difference.clear();
}
void Chunk_demand::route(std::vector<Pair> points) {
  if (points.size() < 2) {
    return;
  }
  integrate();
  ++net;
  using Point = std::array<int, 2>;
  auto walk   = [&](Point a, Point b, int first, bool commit) {
    double cost = 0;
    for (int axis : {first, 1 - first}) {
      int dir = a[axis] < b[axis] ? 1 : -1;
      while (a[axis] != b[axis]) {
        auto p   = a;
        a[axis] += dir;
        p[axis]  = std::min(p[axis], a[axis]);
        auto i   = p[1] * nx + p[0];
        if (seen[2 * i + axis] == net) {
          continue;
        }
        auto&        t    = grid[i];
        const double cap  = extent(1 - axis, p[1 - axis]) * tracks[axis] * (1 - std::min(1., t.blocked[axis]));
        cost             += (1 + t.crossing[axis] + t.internal[axis]) / std::max(.01, cap);
        if (commit) {
          ++t.crossing[axis];
          seen[2 * i + axis] = net;
        }
      }
    }
    return cost;
  };
  // Median bisection keeps high-fanout nets bounded and charges a shared segment once.
  auto tree = [&](auto&& self, std::span<Pair> p) -> Point {
    if (p.size() == 1) {
      return at(p[0]);
    }
    auto extent = [&](int axis) {
      auto [a, b] = std::minmax_element(p.begin(), p.end(), [&](auto x, auto y) { return x[axis] < y[axis]; });
      return (*b)[axis] - (*a)[axis];
    };
    int  axis   = extent(1) > extent(0);
    auto middle = p.size() / 2;
    std::nth_element(p.begin(), p.begin() + middle, p.end(), [axis](auto a, auto b) {
      return std::pair(a[axis], a[1 - axis]) < std::pair(b[axis], b[1 - axis]);
    });
    Point a = self(self, p.first(middle)), b = self(self, p.subspan(middle));
    Point c = {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2};
    for (auto tip : {a, b}) {
      walk(tip, c, walk(tip, c, 1, false) < walk(tip, c, 0, false), true);
    }
    return c;
  };
  tree(tree, points);
}
rapidjson::Value Chunk_demand::record(rapidjson::Document& doc, int count) {
  integrate();
  auto&                              a = doc.GetAllocator();
  int                                w = std::min(48, nx), h = std::min(48, ny);
  std::vector<std::array<double, 8>> sums(w * h);
  for (int y = 0; y < ny; ++y) {
    for (int x = 0; x < nx; ++x) {
      auto& sum = sums[(y * h / ny) * w + (x * w / nx)];
      auto& t   = grid[y * nx + x];
      for (int k = 0; k < 2; ++k) {
        double capacity  = std::max(0., 1 - t.blocked[k]) * tracks[k] * extent(1 - k, k ? x : y);
        sum[k]          += t.crossing[k];
        sum[2 + k]      += t.internal[k];
        sum[4 + k]      += capacity;
        sum[6 + k]      += std::max(0., t.crossing[k] + t.internal[k] - capacity);
      }
    }
  }
  rapidjson::Value r(rapidjson::kObjectType), bounds(rapidjson::kArrayType), loads(rapidjson::kArrayType);
  for (auto v : frame) {
    bounds.PushBack(v, a);
  }
  for (auto& sum : sums) {
    rapidjson::Value cell(rapidjson::kArrayType);
    for (auto v : sum) {
      cell.PushBack(v, a);
    }
    loads.PushBack(cell, a);
  }
  auto boundaries = [&](int axis, int count_cells, int bins) {
    rapidjson::Value coordinates(rapidjson::kArrayType);
    for (int i = 0; i <= bins; ++i) {
      int native = (i * count_cells + bins - 1) / bins;
      coordinates.PushBack(std::min(frame[axis + 2], frame[axis] + native * pitch), a);
    }
    return coordinates;
  };
  r.AddMember("x_edges", boundaries(0, nx, w), a).AddMember("y_edges", boundaries(1, ny, h), a);
  r.AddMember("pitch_dbu", pitch, a)
      .AddMember("track_pitches", count, a)
      .AddMember("columns", w, a)
      .AddMember("rows", h, a)
      .AddMember("frame_dbu", bounds, a)
      .AddMember("tiles", loads, a);
  return r;
}
}  // namespace livehd::placement
