// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "chunk_refine.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <tuple>

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
uint64_t mix(uint64_t key, uint64_t x) { return (key ^ x) * 1099511628211ULL; }
uint64_t number(uint64_t key, double x) { return mix(key, std::bit_cast<uint64_t>(x)); }
uint64_t text(uint64_t key, std::string_view s) {
  key = mix(key, s.size());
  for (unsigned char c : s) {
    key = mix(key, c);
  }
  return key;
}
using Point = std::array<double, 2>;
struct Local_net {
  int                  net;
  std::vector<int>     pins;
  std::optional<Point> port;
};
}  // namespace
Chunk_refine_stats refine_chunk_seeds(dp::Design& d, const Chunk_model& model, const Chunk_plan& plan, std::span<const int> owners,
                                      std::span<const std::string> names, int iterations, int threads, bool detailed,
                                      const std::function<void(size_t, Chunk_job&)>& export_job, std::optional<size_t> only) {
  Chunk_refine_stats stats;
  if (!export_job && (plan.chunks.size() < 2 || iterations <= 0)) {
    return stats;
  }
  std::vector<std::vector<int>>       nodes(plan.chunks.size());
  std::vector<std::vector<Local_net>> nets(plan.chunks.size());
  std::vector<int>                    idx(owners.size(), -1), home(owners.size(), -1);
  auto movable = [&](int i) { return i < d.num_movable && (export_job || !model.objects[owners[i]].macro); };
  for (size_t i = 0; i < owners.size(); ++i) {
    if (owners[i] >= 0) {
      home[i] = plan.home[owners[i]];
      if (!only || size_t(home[i]) == *only) {
        nodes[home[i]].push_back(i);
      }
    }
  }
  std::vector<std::map<size_t, Point>> planned_ports(plan.chunks.size());
  for (size_t j = 0; j < plan.chunks.size(); ++j) {
    if (only && j != *only) {
      continue;
    }
    for (const auto& port : plan.chunks[j].ports) {
      planned_ports[j].emplace(port.net_index, port.xy);
    }
  }
  // Each worker excludes the other owner's half of every shared strip. Choose
  // complementary halves from their centers once, so even a ring cannot award
  // the same area to two independent local detailed solves.
  std::vector<std::vector<Chunk_box>> excluded(plan.chunks.size());
  for (const auto& e : plan.overlaps) {
    const auto& a = plan.chunks[e.a].box;
    const auto& b = plan.chunks[e.b].box;
    std::array  delta{a[0] + a[2] - b[0] - b[2], a[1] + a[3] - b[1] - b[3]};
    int         k  = std::abs(delta[0]) >= std::abs(delta[1]) ? 0 : 1;
    auto        lo = e.box, hi = e.box;
    lo[k + 2] = hi[k] = (e.box[k] + e.box[k + 2]) * .5;
    excluded[e.a].push_back(delta[k] <= 0 ? hi : lo);
    excluded[e.b].push_back(delta[k] <= 0 ? lo : hi);
  }
  std::vector<int> obstacles;
  for (size_t i = 0; i < d.node_x.size(); ++i) {
    if (d.node_size_x[i] > 0 && d.node_size_y[i] > 0
        && (i >= static_cast<size_t>(d.num_movable) || (i < owners.size() && owners[i] >= 0 && model.objects[owners[i]].macro))) {
      obstacles.push_back(i);
    }
  }
  // Split once, not a flat-netlist scan per chunk. Outside pins enter the local solve at the boundary.
  std::vector<int> incident;
  const bool       indexed = only && d.flat_node2pin_start.size() == d.node_x.size() + 1 && d.pin2net.size() == d.pin2node.size();
  if (indexed) {
    for (int node : nodes.at(*only)) {
      for (int j = d.flat_node2pin_start[node]; j < d.flat_node2pin_start[node + 1]; ++j) {
        incident.push_back(d.pin2net[d.flat_node2pin[j]]);
      }
    }
    std::sort(incident.begin(), incident.end());
    incident.erase(std::unique(incident.begin(), incident.end()), incident.end());
  }
  const auto net_count = indexed ? incident.size() : d.flat_net2pin_start.size() - 1;
  for (size_t k_net = 0; k_net < net_count; ++k_net) {
    const size_t n = indexed ? size_t(incident[k_net]) : k_net;
    if (!d.net_mask.empty() && !d.net_mask[n]) {
      continue;
    }
    std::map<int, std::vector<int>> split;
    Point                           sum{};
    size_t                          count = 0;
    for (int j = d.flat_net2pin_start[n]; j < d.flat_net2pin_start[n + 1]; ++j) {
      int pin = d.flat_net2pin[j], i = d.pin2node[pin];
      if (i < int(home.size()) && home[i] >= 0 && (!only || size_t(home[i]) == *only)) {
        split[home[i]].push_back(pin);
      }
      sum[0] += d.node_x[i] + d.pin_offset_x[pin];
      sum[1] += d.node_y[i] + d.pin_offset_y[pin];
      ++count;
    }
    for (auto& [cid, pins] : split) {
      std::optional<Point> port;
      if (auto it = planned_ports[cid].find(n); it != planned_ports[cid].end()) {
        port = it->second;
      } else if (pins.size() != count) {
        auto ext = sum;
        for (int pin : pins) {
          int i   = d.pin2node[pin];
          ext[0] -= d.node_x[i] + d.pin_offset_x[pin];
          ext[1] -= d.node_y[i] + d.pin_offset_y[pin];
        }
        const auto& b = plan.chunks[cid].box;
        Point       xy{};
        for (int k = 0; k < 2; ++k) {
          xy[k] = std::clamp(ext[k] / (count - pins.size()), b[k], b[k + 2]);
        }
        std::array<double, 4> gap{xy[0] - b[0], xy[1] - b[1], b[2] - xy[0], b[3] - xy[1]};
        auto                  side = std::min_element(gap.begin(), gap.end()) - gap.begin();
        xy[side % 2]               = b[side];
        port                       = xy;
      }
      if (pins.size() > 1 || port) {
        nets[cid].push_back({int(n), std::move(pins), port});
      }
    }
  }
  struct Seed {
    uint64_t           context;
    std::vector<Point> xy;
  };
  std::map<std::tuple<std::string, std::string, uint64_t>, Seed> representatives;
  for (size_t j = 0; j < plan.chunks.size(); ++j) {
    if (only && j != *only) {
      continue;
    }
    const auto&  c = plan.chunks[j];
    const auto&  b = c.box;
    const double w = b[2] - b[0], h = b[3] - b[1];
    auto&        ids = nodes[j];
    std::sort(ids.begin(), ids.end(), [&](int i, int k) {
      if (movable(i) != movable(k)) {
        return movable(i);
      }
      return std::tie(names[i], i) < std::tie(names[k], k);
    });
    if (!export_job && (ids.empty() || !movable(ids.front()))) {
      continue;
    }
    Chunk_job job;
    auto&     local = job.physical;
    local.layout    = {0, 0, w, h, d.layout.site_width, d.layout.row_height};
    uint64_t key = 14695981039346656037ULL, context = number(number(number(key, w), h), c.density);
    context  = number(context, c.shared_dbu2);
    auto add = [&](double x, double y, double sx, double sy, bool macro) {
      local.node_x.push_back(x);
      local.node_y.push_back(y);
      local.node_size_x.push_back(sx);
      local.node_size_y.push_back(sy);
      local.node_macro.push_back(macro);
    };
    bool multirow = false;
    for (int i : ids) {
      idx[i]          = int(local.node_x.size());
      const double sx = d.node_size_x[i], sy = d.node_size_y[i];
      if (movable(i)) {
        add(d.node_x[i] - b[0], d.node_y[i] - b[1], sx, sy, model.objects[owners[i]].macro);
        if (export_job) {
          job.names.push_back(names[i]);
        }
      } else {
        // Keep the true intersection; a remote fixed cell is just a boundary contact.
        double x = std::clamp(d.node_x[i] - b[0], 0., w), y = std::clamp(d.node_y[i] - b[1], 0., h);
        double dx = std::clamp(d.node_x[i] + sx - b[0], 0., w) - x, dy = std::clamp(d.node_y[i] + sy - b[1], 0., h) - y;
        add(x, y, dx > 0 && dy > 0 ? dx : 0, dx > 0 && dy > 0 ? dy : 0, model.objects[owners[i]].macro);
      }
      local.num_movable += movable(i);
      multirow          |= movable(i) && !model.objects[owners[i]].macro && sy > d.layout.row_height + 1e-6;
      key                = text(key, names[i]);
      key                = number(number(mix(key, movable(i)), sx), sy);
      context            = number(context, plan.density[owners[i]]);
      if (!movable(i)) {
        context = number(context, local.node_x.back());
        context = number(context, local.node_y.back());
      }
    }
    // Stepped multi-height rows are handled by the final row-aware legalizer.
    if (multirow && !export_job) {
      ++stats.deferred;
      continue;
    }
    for (const auto& strip : excluded[j]) {
      double x = strip[0] - b[0], y = strip[1] - b[1], sx = strip[2] - strip[0], sy = strip[3] - strip[1];
      add(x, y, sx, sy, true);
      for (double v : {x, y, sx, sy}) {
        context = number(context, v);
      }
    }
    for (int i : obstacles) {
      if (static_cast<size_t>(i) < home.size() && home[i] == int(j)) {
        continue;
      }
      const double xl = std::max(b[0], d.node_x[i]), yl = std::max(b[1], d.node_y[i]);
      const double xh = std::min(b[2], d.node_x[i] + d.node_size_x[i]), yh = std::min(b[3], d.node_y[i] + d.node_size_y[i]);
      if (xl < xh && yl < yh) {
        add(xl - b[0], yl - b[1], xh - xl, yh - yl, true);
        for (double coord : {xl - b[0], yl - b[1], xh - xl, yh - yl}) {
          context = number(context, coord);
        }
      }
    }
    for (const auto& part : nets[j]) {
      uint64_t edge = 0;
      auto     pin  = [&](int i, double x, double y) {
        local.flat_net2pin.push_back(local.pin2node.size());
        local.pin2node.push_back(i);
        local.pin_offset_x.push_back(x);
        local.pin_offset_y.push_back(y);
      };
      for (int pp : part.pins) {
        int    orig = d.pin2node[pp], i = idx[orig];
        double dx = d.pin_offset_x[pp], dy = d.pin_offset_y[pp];
        if (!movable(orig)) {
          dx      = std::clamp(d.node_x[orig] + dx - b[0], 0., w) - local.node_x[i];
          dy      = std::clamp(d.node_y[orig] + dy - b[1], 0., h) - local.node_y[i];
          context = number(number(context, dx), dy);
        }
        pin(i, dx, dy);
        edge += number(number(mix(2166136261U, i), d.pin_offset_x[pp]), d.pin_offset_y[pp]);
      }
      if (part.port) {
        if (export_job) {
          job.boundary_nets.push_back(local.net_weights.size());
          job.original_nets.push_back(part.net);
        }
        int    i  = local.node_x.size();
        double px = (*part.port)[0] - b[0], py = (*part.port)[1] - b[1];
        add(px, py, 0, 0, false);
        pin(i, 0, 0);
        context += number(number(edge, px), py);
      }
      key += mix(edge, part.pins.size());
      local.net_weights.push_back(export_job && !d.net_weights.empty() ? d.net_weights[part.net] : 1);
      local.flat_net2pin_start.push_back(local.pin2node.size());
    }
    if (export_job) {
      job.multirow = multirow;
      // In the seed path macros are fixed. Independent jobs place their own movable
      // macros and retain the physical halo and supplied pin coordinates.
      for (auto [dst, src] : {
               std::pair{&local.node_halo_x, &d.node_halo_x},
               std::pair{&local.node_halo_y, &d.node_halo_y}
      }) {
        if (!src->empty()) {
          dst->resize(local.node_x.size());
          for (int i = 0; i < local.num_movable; ++i) {
            (*dst)[i] = (*src)[ids[i]];
          }
        }
      }
      export_job(j, job);
      continue;
    }
    auto family = std::tuple{c.partition, c.implementation, key};
    auto cached = representatives.find(family);
    if (cached != representatives.end()) {
      for (int i = 0; i < local.num_movable; ++i) {
        local.node_x[i] = std::clamp(cached->second.xy[i][0] * w, 0., std::max(0., w - local.node_size_x[i]));
        local.node_y[i] = std::clamp(cached->second.xy[i][1] * h, 0., std::max(0., h - local.node_size_y[i]));
      }
    }
    if (cached == representatives.end() || cached->second.context != context) {
      local.build_maps();
      dp::Options opts;
      opts.max_iterations = std::min(cached == representatives.end() ? 24 : 8, iterations);
      opts.min_iterations = std::min(opts.min_iterations, opts.max_iterations);
      opts.target_density = c.density;
      opts.num_threads    = threads;
      opts.bins_x = opts.bins_y = 8;
      opts.legalize             = true;
      opts.detailed_place       = detailed;
      // Some trial rectangles cannot be legalized in isolation (e.g. irregular rows). Their soft FP seeds remain valid.
      try {
        if (!dp::place(local, opts).legal) {
          ++stats.deferred;
          continue;
        }
      } catch (const std::runtime_error& error) {
        auto why = std::string_view(error.what());
        if (why.find("legal") == why.npos && why.find("fit") == why.npos) {
          throw;
        }
        ++stats.deferred;
        continue;
      }
      if (cached == representatives.end()) {
        ++stats.solved;
        if (!model.objects[owners[ids.front()]].module.empty()) {
          Seed seed{context, {}};
          for (int i = 0; i < local.num_movable; ++i) {
            seed.xy.push_back({local.node_x[i] / w, local.node_y[i] / h});
          }
          representatives.emplace(family, std::move(seed));
        }
      } else {
        ++stats.refined;
      }
    } else {
      ++stats.reused;
    }
    for (int i = 0; i < local.num_movable; ++i) {
      d.node_x[ids[i]] = local.node_x[i] + b[0];
      d.node_y[ids[i]] = local.node_y[i] + b[1];
    }
  }
  return stats;
}
}  // namespace livehd::placement
