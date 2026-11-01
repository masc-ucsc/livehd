// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute sub-regions (experiment): each region is bisected recursively by a minimum cut of its box graph (VieCut:
// the global minimum cut when it is balanced, else an s-t minimum cut between geometric seed quarters) until a piece
// holds about subregion_cells cells; each cut also splits the
// piece's tiles along their longer side, in proportion to the boxes, each part on the side of its centroid, so every
// sub-region is a contiguous set of tiles. The SAT placement then solves sub-regions, with the cut nets crossing
// their boundaries. A geometric bisection of the same boxes is measured alongside for comparison.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <numeric>
#include <tuple>
#include <unordered_map>

#include "algorithms/flow/push_relabel.h"
#include "algorithms/global_mincut/algorithms.h"
#include "algorithms/global_mincut/minimum_cut.h"
#include "common/configuration.h"
#include "data_structure/mutable_graph.h"
#include "legroute_internal.hpp"
#include "tools/random_functions.h"

namespace livehd::placement::lr {
namespace {
struct Piece {
  std::vector<int> boxes, tiles;
};

// net weight per box pair (a k-pin net: 12 / (k - 1), at least 1), plus 1 to each box's 4 nearest boxes so the graph
// is connected and the cut prefers geometrically coherent parts
std::map<std::pair<int, int>, uint64_t> box_graph(const Design& d, const Boxes& boxes, const std::vector<int>& set) {
  std::unordered_map<int, int> local;
  for (int i = 0; i < int(set.size()); ++i) {
    local[set[i]] = i;
  }
  std::map<std::pair<int, int>, uint64_t>   w;
  std::unordered_map<int, std::vector<int>> net_boxes;
  for (int b : set) {
    for (int c : boxes.boxes[b].cells) {
      for (int n : d.nets_of[c]) {
        if (d.nets[n].routed && !d.nets[n].clock) {
          auto& v = net_boxes[n];
          if (v.empty() || v.back() != local[b]) {
            v.push_back(local[b]);
          }
        }
      }
    }
  }
  for (auto& [n, v] : net_boxes) {
    std::ranges::sort(v);
    v.erase(std::unique(v.begin(), v.end()), v.end());
    if (v.size() < 2 || v.size() > 32) {
      continue;
    }
    const uint64_t pw = std::max<uint64_t>(1, 12 / (v.size() - 1));
    for (size_t i = 0; i < v.size(); ++i) {
      for (size_t j = i + 1; j < v.size(); ++j) {
        w[{v[i], v[j]}] += pw;
      }
    }
  }
  for (int i = 0; i < int(set.size()); ++i) {
    std::vector<std::pair<double, int>> near;
    const auto&                         a = boxes.boxes[set[i]];
    for (int j = 0; j < int(set.size()); ++j) {
      if (j != i) {
        const auto& b = boxes.boxes[set[j]];
        near.emplace_back(std::abs(a.cx - b.cx) + std::abs(a.cy - b.cy), j);
      }
    }
    const size_t k = std::min<size_t>(4, near.size());
    std::partial_sort(near.begin(), near.begin() + long(k), near.end());
    for (size_t q = 0; q < k; ++q) {
      w[{std::min(i, near[q].second), std::max(i, near[q].second)}] += 1;
    }
  }
  return w;
}

// the most balanced minimum cut: true for the boxes on one side
std::vector<char> min_cut(const std::vector<int>& set, const std::map<std::pair<int, int>, uint64_t>& w) {
  auto G = std::make_shared<mutable_graph>();
  G->start_construction(NodeID(set.size()));
  for (size_t i = 0; i < set.size(); ++i) {
    G->new_node();
  }
  for (const auto& [e, wt] : w) {
    G->new_edge(NodeID(e.first), NodeID(e.second), wt);
  }
  G->finish_construction();
  auto cfg                     = configuration::getConfig();
  cfg->find_most_balanced_cut  = true;
  cfg->find_lowest_conductance = false;
  cfg->save_cut                = true;
  cfg->threads                 = 1;
  cfg->seed                    = 0;
  random_functions::setSeed(0);
  std::unique_ptr<minimum_cut> mc(selectMincutAlgorithm<std::shared_ptr<mutable_graph>>("cactus"));
  mc->perform_minimum_cut(G);
  std::vector<char> side(set.size(), 0);
  for (size_t i = 0; i < set.size(); ++i) {
    side[i] = G->getNodeInCut(NodeID(i)) ? 1 : 0;
  }
  return side;
}

// A balanced minimum cut: the quarter of the boxes nearest each end of the piece's longer side are contracted into
// a source and a sink, and the s-t minimum cut between them (VieCut push-relabel) splits the middle half wherever
// it is cheapest. true for the boxes on the source side.
std::vector<char> seeded_cut(const Grid& g, const Boxes& boxes, const std::vector<int>& set, const std::vector<int>& tiles,
                             const std::map<std::pair<int, int>, uint64_t>& w) {
  int c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : tiles) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  const bool          by_x = double(c1 - c0 + 1) * double(g.tile_w) >= double(r1 - r0 + 1) * double(g.tile_h);
  std::vector<size_t> order(set.size());
  std::iota(order.begin(), order.end(), 0);
  std::ranges::stable_sort(order, [&](size_t x, size_t y) {
    return by_x ? boxes.boxes[set[x]].cx < boxes.boxes[set[y]].cx : boxes.boxes[set[x]].cy < boxes.boxes[set[y]].cy;
  });
  const size_t     q = set.size() / 4;
  std::vector<int> node(set.size());  // 0: source, 1: sink, else 2 + middle index
  int              next = 2;
  for (size_t k = 0; k < order.size(); ++k) {
    node[order[k]] = k < q ? 0 : k >= order.size() - q ? 1 : next++;
  }
  std::map<std::pair<int, int>, uint64_t> cw;
  for (const auto& [e, wt] : w) {
    const int a = node[e.first], b = node[e.second];
    if (a != b) {
      cw[{std::min(a, b), std::max(a, b)}] += wt;
    }
  }
  auto G = std::make_shared<mutable_graph>();
  G->start_construction(NodeID(next));
  for (int i = 0; i < next; ++i) {
    G->new_node();
  }
  for (const auto& [e, wt] : cw) {
    G->new_edge(NodeID(e.first), NodeID(e.second), wt);
  }
  G->finish_construction();
  push_relabel<>    flow;
  const auto        source_side = flow.solve_max_flow_min_cut(G, {0, 1}, 0, true).second;
  std::vector<char> in_source(next, 0);
  for (const auto v : source_side) {
    in_source[v] = 1;
  }
  in_source[0] = 1;
  in_source[1] = 0;
  std::vector<char> side(set.size(), 0);
  for (size_t i = 0; i < set.size(); ++i) {
    side[i] = in_source[node[i]];
  }
  return side;
}

int cells_of(const Boxes& boxes, const std::vector<int>& set) {
  int n = 0;
  for (int b : set) {
    n += int(boxes.boxes[b].cells.size());
  }
  return n;
}

// the tiles split along the longer side, the first part's share by its boxes, the part with the lower centroid first
std::pair<std::vector<int>, std::vector<int>> split_tiles(const Grid& g, const Boxes& boxes, const std::vector<int>& tiles,
                                                          const std::vector<int>& a, const std::vector<int>& b) {
  int c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : tiles) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  const bool by_x = double(c1 - c0 + 1) * double(g.tile_w) >= double(r1 - r0 + 1) * double(g.tile_h);
  const auto mean = [&](const std::vector<int>& s) {
    double v = 0;
    for (int k : s) {
      v += by_x ? boxes.boxes[k].cx : boxes.boxes[k].cy;
    }
    return s.empty() ? 0.0 : v / double(s.size());
  };
  auto order = tiles;
  std::ranges::stable_sort(order, [&](int x, int y) {
    return by_x ? std::pair{g.col(x), g.row(x)} < std::pair{g.col(y), g.row(y)}
                : std::pair{g.row(x), g.col(x)} < std::pair{g.row(y), g.col(y)};
  });
  const bool   a_first = mean(a) <= mean(b);
  const auto&  first   = a_first ? a : b;
  const size_t n
      = std::clamp<size_t>(size_t(std::lround(double(order.size()) * double(first.size()) / double(a.size() + b.size()))),
                           first.size(),
                           order.size() - (a_first ? b : a).size());
  std::vector<int> lo(order.begin(), order.begin() + long(n)), hi(order.begin() + long(n), order.end());
  return a_first ? std::pair{lo, hi} : std::pair{hi, lo};
}

// the geometric alternative: the boxes split at the median of the longer side
std::pair<std::vector<int>, std::vector<int>> geometric(const Grid& g, const Boxes& boxes, const std::vector<int>& set,
                                                        const std::vector<int>& tiles) {
  int c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : tiles) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  const bool by_x = double(c1 - c0 + 1) * double(g.tile_w) >= double(r1 - r0 + 1) * double(g.tile_h);
  auto       s    = set;
  std::ranges::stable_sort(s, [&](int x, int y) {
    return by_x ? boxes.boxes[x].cx < boxes.boxes[y].cx : boxes.boxes[x].cy < boxes.boxes[y].cy;
  });
  const auto half = s.begin() + long(s.size() / 2);
  return {std::vector<int>(s.begin(), half), std::vector<int>(half, s.end())};
}

// pieces spanned per net, summed over nets, minus one per net: the crossings a partition adds
size_t crossings(const Design& d, const Boxes& boxes, const std::vector<int>& part_of_box) {
  size_t total = 0;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    if (!N.routed || N.clock) {
      continue;
    }
    std::vector<int> parts;
    for (const auto& p : N.pins) {
      if (p.comp >= 0 && d.cell[p.comp] >= 0) {
        parts.push_back(part_of_box[boxes.box_of[d.cell[p.comp]]]);
      }
    }
    std::ranges::sort(parts);
    parts.erase(std::unique(parts.begin(), parts.end()), parts.end());
    total += parts.empty() ? 0 : parts.size() - 1;
  }
  return total;
}
}  // namespace

void subdivide_regions(const Design& d, Grid& g, Boxes& boxes, Legroute_report& rep) {
  const int target = int(d.opt.subregion_cells);
  if (target <= 0) {
    return;
  }
  const auto         start = Clock::now();
  std::vector<Piece> work, leaves, geo_work, geo_leaves;
  for (int r = 0; r < g.regions; ++r) {
    Piece p;
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      if (boxes.boxes[b].region == r) {
        p.boxes.push_back(b);
      }
    }
    p.tiles = g.region_tiles[r];
    work.push_back(p);
    geo_work.push_back(p);
  }
  size_t unbalanced = 0;
  while (!work.empty()) {
    auto p = std::move(work.back());
    work.pop_back();
    if (cells_of(boxes, p.boxes) <= target || p.boxes.size() < 8) {
      leaves.push_back(std::move(p));
      continue;
    }
    const auto w    = box_graph(d, boxes, p.boxes);
    auto       side = min_cut(p.boxes, w);
    size_t     ones = size_t(std::count(side.begin(), side.end(), char(1)));
    if (std::min(ones, side.size() - ones) * 5 < side.size()) {  // a lopsided global cut: the seeded balanced cut
      ++unbalanced;
      side = seeded_cut(g, boxes, p.boxes, p.tiles, w);
    }
    std::vector<int> a, b;
    for (size_t i = 0; i < p.boxes.size(); ++i) {
      (side[i] ? a : b).push_back(p.boxes[i]);
    }
    if (a.empty() || b.empty()) {
      std::tie(a, b) = geometric(g, boxes, p.boxes, p.tiles);
    }
    auto [ta, tb] = split_tiles(g, boxes, p.tiles, a, b);
    work.push_back({std::move(a), std::move(ta)});
    work.push_back({std::move(b), std::move(tb)});
  }
  while (!geo_work.empty()) {
    auto p = std::move(geo_work.back());
    geo_work.pop_back();
    if (cells_of(boxes, p.boxes) <= target || p.boxes.size() < 8) {
      geo_leaves.push_back(std::move(p));
      continue;
    }
    auto [a, b]   = geometric(g, boxes, p.boxes, p.tiles);
    auto [ta, tb] = split_tiles(g, boxes, p.tiles, a, b);
    geo_work.push_back({std::move(a), std::move(ta)});
    geo_work.push_back({std::move(b), std::move(tb)});
  }
  std::vector<int> part(boxes.boxes.size(), -1), geo_part(boxes.boxes.size(), -1), region0(boxes.boxes.size());
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    region0[b] = boxes.boxes[b].region;
  }
  for (int k = 0; k < int(geo_leaves.size()); ++k) {
    for (int b : geo_leaves[k].boxes) {
      geo_part[b] = k;
    }
  }
  rep.cut_crossings_regions   = crossings(d, boxes, region0);
  rep.cut_crossings_geometric = crossings(d, boxes, geo_part);
  // the min-cut sub-regions become the regions
  g.regions                   = int(leaves.size());
  g.region_tiles.assign(g.regions, {});
  for (int k = 0; k < g.regions; ++k) {
    for (int t : leaves[k].tiles) {
      g.region[t] = k;
      g.region_tiles[k].push_back(t);
    }
    for (int b : leaves[k].boxes) {
      boxes.boxes[b].region = k;
      part[b]               = k;
    }
  }
  rep.cut_crossings_mincut = crossings(d, boxes, part);
  rep.cut_subregions       = leaves.size();
  rep.cut_unbalanced       = unbalanced;
  rep.cut_seconds          = seconds_since(start);
  std::fprintf(stderr,
               "legroute cut: %zu sub-regions (%zu geometric), crossings regions %zu min-cut %zu geometric %zu, %zu unbalanced "
               "cuts, %.2f s\n",
               leaves.size(),
               geo_leaves.size(),
               rep.cut_crossings_regions,
               rep.cut_crossings_mincut,
               rep.cut_crossings_geometric,
               unbalanced,
               rep.cut_seconds);
}
}  // namespace livehd::placement::lr
