// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
struct Seed_expired {};

bool complete_forest(const Design& d, const Grid& g, const Boxes& boxes, const Global_routes& routes) {
  if (routes.trees.size() != d.nets.size() || routes.unrouted || routes.overflow_edges || routes.overflow_tracks) {
    return false;
  }
  for (const auto& request : all_signal_requests(d, g, boxes)) {
    const auto& tree = routes.trees[size_t(request.net)];
    if (!tree.routed) {
      return false;
    }
    std::map<int, std::vector<int>> adjacent;
    std::set<int>                   witnessed(request.endpoints.begin(), request.endpoints.end());
    std::set<std::pair<int, int>>   edges;
    for (const auto& [a, b] : tree.edges) {
      if (a < 0 || b <= a || size_t(b) >= g.size() || std::abs(g.col(a) - g.col(b)) + std::abs(g.row(a) - g.row(b)) != 1
          || !edges.emplace(a, b).second) {
        return false;
      }
      witnessed.insert(a);
      witnessed.insert(b);
      adjacent[a].push_back(b);
      adjacent[b].push_back(a);
    }
    if (witnessed.empty()) {
      continue;
    }
    std::vector<int> queue{*witnessed.begin()};
    std::set<int>    reached{queue.front()};
    for (size_t at = 0; at < queue.size(); ++at) {
      for (int q : adjacent[queue[at]]) {
        if (reached.insert(q).second) {
          queue.push_back(q);
        }
      }
    }
    if (reached != witnessed) {
      return false;
    }
  }
  return true;
}
}  // namespace

Box_route_seed_result prepare_box_route_seed(const Design& d, const Grid& g, const Boxes& original, const std::vector<int>& cap_h,
                                             const std::vector<int>& cap_v, std::string_view stage) {
  const auto            start = Clock::now();
  Box_route_seed_result out;
  out.boxes         = original;
  const auto finish = [&]() {
    out.seconds = seconds_since(start);
    if (!d.opt.output_dir.empty()) {
      // Callers supply these two literal stages, not arbitrary filename text.
      if (stage != "plan" && stage != "local") {
        throw std::invalid_argument("box route seed report stage must be plan or local");
      }
      std::filesystem::create_directories(d.opt.output_dir);
      std::ofstream report(d.opt.output_dir + "/box_route_seed_" + std::string(stage) + ".json");
      report << "{\"accepted\":" << (out.accepted ? "true" : "false") << ",\"reason\":\"" << out.reason
             << "\",\"endpoint_basis\":\"provisional_box_tiles\",\"physical_cells_moved\":false,\"matched_boxes\":" << out.matched
             << ",\"moved_box_anchors\":" << out.moved << ",\"matching_seconds\":" << out.matching_seconds
             << ",\"seconds\":" << out.seconds << ",\"assignments\":[";
      for (size_t b = 0; b < out.boxes.boxes.size(); ++b) {
        report << (b ? "," : "") << '[' << b << ',' << g.tile_of(original.boxes[b].cx, original.boxes[b].cy) << ','
               << out.boxes.boxes[b].tile << ']';
      }
      report << "]}\n";
      if (!report) {
        throw std::runtime_error("cannot write box route seed report");
      }
    }
  };
  if (!d.opt.box_route_seed || !movable_route_partition(d.opt)) {
    out.reason = "disabled";
    finish();
    return out;
  }
  if (g.region.size() != g.size() || cap_h.size() != g.size() || cap_v.size() != g.size()) {
    throw std::invalid_argument("box route seed grid arrays do not match");
  }
  // This is optional proposal work, not a reason to reject the admitted original forest.
  const auto deadline = start + std::chrono::milliseconds(std::min(1000u, d.opt.sat_ms));
  const auto check    = [&] {
    if (Clock::now() >= deadline) {
      throw Seed_expired{};
    }
  };
  try {
    std::vector<std::vector<int>> region_boxes(size_t(g.regions)), region_tiles(size_t(g.regions));
    for (size_t t = 0; t < g.size(); ++t) {
      if (g.region[t] < 0 || g.region[t] >= g.regions) {
        throw std::invalid_argument("box route seed has invalid region ownership");
      }
      region_tiles[size_t(g.region[t])].push_back(int(t));
    }
    for (size_t b = 0; b < original.boxes.size(); ++b) {
      const auto& box  = original.boxes[b];
      const int   home = box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy);
      if (home < 0 || size_t(home) >= g.size()) {
        throw std::invalid_argument("box route seed has invalid original tile");
      }
      const int region = g.region[size_t(home)];
      region_boxes[size_t(region)].push_back(int(b));
      out.boxes.boxes[b].region = region;
    }
    const std::unordered_map<int, std::vector<int>> allowed;
    const std::vector<char>                         pinned(g.size());
    for (int r = 0; r < g.regions; ++r) {
      check();
      const auto& B = region_boxes[size_t(r)];
      const auto& T = region_tiles[size_t(r)];
      if (B.empty()) {
        continue;
      }
      const auto domains = prepare_box_domains(d, d.opt, g, r, B, T, original, allowed, pinned, check);
      if (domains.matching.size() != B.size()) {
        out.reason           = "incomplete_native_domains";
        out.matching_seconds = seconds_since(start);
        finish();
        return out;
      }
      std::vector<std::vector<int>> choices(B.size());
      for (size_t i = 0; i < B.size(); ++i) {
        choices[i]      = box_domain_tiles(d, g, B[i], domains.windows[i], original, allowed, domains.lifted, pinned, T, check);
        const auto& box = original.boxes[size_t(B[i])];
        const auto  distance
            = [&](int t) { return std::abs(double(g.cx(g.col(t))) - box.cx) + std::abs(double(g.cy(g.row(t))) - box.cy); };
        std::ranges::sort(choices[i], [&](int a, int b) { return std::pair{distance(a), a} < std::pair{distance(b), b}; });
      }
      std::vector<int>               owner(g.size(), -1), assignment(B.size(), -1);
      std::vector<char>              seen(g.size());
      const std::function<bool(int)> augment = [&](int i) {
        check();
        for (int t : choices[size_t(i)]) {
          if (seen[size_t(t)]) {
            continue;
          }
          seen[size_t(t)] = 1;
          if (owner[size_t(t)] < 0 || augment(owner[size_t(t)])) {
            owner[size_t(t)]      = i;
            assignment[size_t(i)] = t;
            return true;
          }
        }
        return false;
      };
      for (size_t i = 0; i < B.size(); ++i) {
        std::ranges::fill(seen, 0);
        if (!augment(int(i))) {
          throw std::logic_error("box seed matching lost a native complete domain witness");
        }
      }
      for (size_t i = 0; i < B.size(); ++i) {
        const int t   = assignment[i];
        auto&     box = out.boxes.boxes[size_t(B[i])];
        if (box.fixed && t != box.tile) {
          out.reason           = "fixed_box_moved";
          out.matching_seconds = seconds_since(start);
          finish();
          return out;
        }
        ++out.matched;
        out.moved += t != g.tile_of(box.cx, box.cy);
        box.tile   = t;
        box.cx     = double(g.cx(g.col(t)));
        box.cy     = double(g.cy(g.row(t)));
      }
    }
    out.matching_seconds = seconds_since(start);
    check();
    out.routes = route_all_signals(d, g, out.boxes, cap_h, cap_v);
    if (!complete_forest(d, g, out.boxes, out.routes)) {
      out.reason = "incomplete_candidate_route";
    } else {
      // Membership and density are still assessed from the original physical frame.
      const auto        gates = assess_legroute_gates(d, g, &original, out.routes, {}, cap_h, cap_v, true);
      std::vector<char> critical(d.nets.size());
      for (size_t n = 0; n < d.nets.size(); ++n) {
        critical[n] = d.nets[n].rank < d.opt.partition_critical;
      }
      const auto cuts
          = route_cuts(g, out.routes, critical, cap_h, cap_v, admission_share(d.opt), d.opt.partition_critical_cost < 0);
      out.accepted = !gates.requires_replan() && cuts.allows(g, g.region);
      out.reason   = out.accepted ? "verified_aggregate_candidate" : "candidate_admission_failed";
    }
  } catch (const Seed_expired&) {
    out.reason           = "matching_budget";
    out.matching_seconds = seconds_since(start);
  }
  finish();
  return out;
}
}  // namespace livehd::placement::lr
