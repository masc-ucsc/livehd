// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <set>
#include <stdexcept>
#include <tuple>

#include "legroute_internal.hpp"

namespace livehd::placement::lr {
namespace {
using Edge = std::tuple<int, int, int>;

std::set<Edge> expand(const Grid& g, int region, std::span<const Route_piece> pieces) {
  std::set<Edge> edges;
  const auto     tile_ok = [&](int tile) { return tile >= 0 && size_t(tile) < g.size() && g.region[size_t(tile)] == region; };
  const auto     take    = [&](int net, int a, int b) {
    if (!tile_ok(a) || !tile_ok(b)) {
      throw std::logic_error("legroute coalescing: route leaves its region");
    }
    edges.emplace(net, std::min(a, b), std::max(a, b));
  };
  for (const auto& piece : pieces) {
    if (piece.net < 0 || !tile_ok(piece.ta) || !tile_ok(piece.tb)) {
      throw std::logic_error("legroute coalescing: invalid route endpoint");
    }
    const int row = g.row(piece.h_first ? piece.ta : piece.tb), col = g.col(piece.h_first ? piece.tb : piece.ta);
    for (int x = std::min(g.col(piece.ta), g.col(piece.tb)); x < std::max(g.col(piece.ta), g.col(piece.tb)); ++x) {
      take(piece.net, g.tile(x, row), g.tile(x + 1, row));
    }
    for (int y = std::min(g.row(piece.ta), g.row(piece.tb)); y < std::max(g.row(piece.ta), g.row(piece.tb)); ++y) {
      take(piece.net, g.tile(col, y), g.tile(col, y + 1));
    }
  }
  return edges;
}
}  // namespace

std::vector<Route_piece> coalesce_region_routes(const Grid& g, int region, std::span<const Route_piece> pieces,
                                                const std::set<std::pair<int, int>>& protected_nodes) {
  if (g.cols <= 0 || g.rows <= 0 || g.region.size() != g.size() || region < 0 || region >= g.regions) {
    throw std::logic_error("legroute coalescing: invalid region grid");
  }
  const auto                                  original = expand(g, region, pieces);
  std::map<int, std::map<int, std::set<int>>> graph;
  std::set<std::pair<int, int>>               zero_length;
  for (const auto& piece : pieces) {
    if (piece.ta == piece.tb) {
      zero_length.emplace(piece.net, piece.ta);
      graph[piece.net][piece.ta];
    }
  }
  for (const auto& [net, a, b] : original) {
    graph[net][a].insert(b);
    graph[net][b].insert(a);
  }
  std::vector<Route_piece> result;
  for (const auto& [net, adjacent] : graph) {
    const auto terminal = [&](int tile) {
      const auto& neighbors = adjacent.at(tile);
      if (protected_nodes.contains({net, tile}) || zero_length.contains({net, tile}) || neighbors.size() != 2) {
        return true;
      }
      const int a = *neighbors.begin(), b = *neighbors.rbegin();
      return !((g.row(a) == g.row(tile) && g.row(b) == g.row(tile)) || (g.col(a) == g.col(tile) && g.col(b) == g.col(tile)));
    };
    std::set<std::pair<int, int>> visited;
    size_t                        degree_sum = 0;
    for (const auto& [a, neighbors] : adjacent) {
      degree_sum += neighbors.size();
      if (!terminal(a)) {
        continue;
      }
      if (neighbors.empty()) {
        result.push_back({net, a, a, true});
      }
      for (int next : neighbors) {
        if (visited.contains(std::minmax(a, next))) {
          continue;
        }
        int previous = a, current = next;
        visited.emplace(std::min(previous, current), std::max(previous, current));
        while (!terminal(current)) {
          const auto& ns    = adjacent.at(current);
          int         after = *ns.begin();
          if (after == previous) {
            after = *ns.rbegin();
          }
          previous = current;
          current  = after;
          if (!visited.emplace(std::min(previous, current), std::max(previous, current)).second) {
            throw std::logic_error("legroute coalescing: repeated straight-run edge");
          }
        }
        result.push_back({net, a, current, g.row(a) == g.row(current)});
      }
    }
    if (visited.size() * 2 != degree_sum) {
      throw std::logic_error("legroute coalescing: omitted route component");
    }
  }
  // This independent expansion is an acceptance condition, not a debug-only
  // assertion: neither capacity nor connectivity may change in refinement.
  if (expand(g, region, result) != original) {
    throw std::logic_error("legroute coalescing: changed net-edge union");
  }
  std::set<std::pair<int, int>> endpoints;
  for (const auto& piece : result) {
    endpoints.emplace(piece.net, piece.ta);
    endpoints.emplace(piece.net, piece.tb);
  }
  for (const auto& [net, adjacent] : graph) {
    for (const auto& [tile, neighbors] : adjacent) {
      if ((protected_nodes.contains({net, tile}) || zero_length.contains({net, tile})) && !endpoints.contains({net, tile})) {
        throw std::logic_error("legroute coalescing: lost protected attachment");
      }
    }
  }
  return result;
}
}  // namespace livehd::placement::lr
