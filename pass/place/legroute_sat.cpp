// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// legroute.local placement: per region, a SAT model places every cell-box on a free tile so that the box-to-box
// routes fit the tracks by construction. Movable route-driven placement chooses an arbitrary connected subgraph
// inside each admitted coarse corridor, sharing actual net-edge literals across its regional parts. Conditional
// connectivity cuts and edge-capacity constraints are checked before any candidate is published. Legacy modes
// use straight connections or one of their two L-shapes (one bit); a leg
// covers every tile from end to end in its row or column; a tile counts a net once per direction however many of
// its legs cover it; and per tile edge and direction the nets crossing the edge stay within the tracks of the
// narrower tile less the edges the routes fixed ahead (hops, trees, IO pre-routes) cross. The in-box wires keep to
// their own tile (the boxing pin limit and legroute.final bound them). The bounds are lazy (added when an answer
// overflows them) and the fallback placement is tried first as assumptions. This certifies coarse capacity only. Each box
// stays in a window around its placement position (window_tiles), which keeps the model linear in the region size.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>

#include "algorithms/flow/push_relabel.h"  // VieCut: the s-t min cuts that pick a failing region's spill
#include "data_structure/mutable_graph.h"
#include "legroute_internal.hpp"
#include "sat_solver.hpp"

namespace livehd::placement::lr {
std::vector<unsigned> region_solve_waves(const Grid& g, const std::vector<size_t>& priority) {
  if (priority.size() != size_t(g.regions)) {
    throw std::invalid_argument("region priority must contain every region");
  }
  std::vector<std::set<int>> adjacent(size_t(g.regions));
  for (int t = 0; t < int(g.size()); ++t) {
    for (int q : {g.col(t) + 1 < g.cols ? t + 1 : -1, t + g.cols < int(g.size()) ? t + g.cols : -1}) {
      if (q >= 0 && g.region[t] != g.region[q]) {
        adjacent[size_t(g.region[t])].insert(g.region[q]);
        adjacent[size_t(g.region[q])].insert(g.region[t]);
      }
    }
  }
  std::vector<unsigned> wave(size_t(g.regions));
  std::vector<char>     scheduled(size_t(g.regions));
  for (size_t r : priority) {
    if (r >= wave.size() || scheduled[r]) {
      throw std::invalid_argument("region priority must be a permutation");
    }
    for (int neighbor : adjacent[r]) {
      if (scheduled[size_t(neighbor)]) {
        wave[r] = std::max(wave[r], wave[size_t(neighbor)] + 1);
      }
    }
    scheduled[r] = 1;
  }
  return wave;
}
namespace {
// Packing first: every region must hold its boxes on tiles they fit (taps and fixed cells narrow tiles, so a count
// is not enough; an infeasible packing would leave the SAT a pigeonhole proof). Per region a bipartite matching of
// boxes to fitting tiles; a box left over moves to the nearest region with a free tile it fits, through that
// region's augmenting paths. Returns the moves.
size_t balance_regions(const Design& d, const Grid& g, Boxes& boxes) {
  std::vector<std::vector<int>> tiles(g.regions);
  for (int t = 0; t < int(g.size()); ++t) {
    if (!g.blocked[t]) {
      tiles[g.region[t]].push_back(t);
    }
  }
  std::vector<std::unordered_map<int, int>> owner(g.regions);  // per region: tile -> box
  std::vector<char>                         seen(g.size(), 0);
  std::vector<int>                          touched;
  const auto                                dist = [&](int b, int t) {
    return std::abs(double(g.cx(g.col(t))) - boxes.boxes[b].cx) + std::abs(double(g.cy(g.row(t))) - boxes.boxes[b].cy);
  };
  std::function<bool(int, int)> augment = [&](int r, int b) {
    std::vector<int> cand;
    for (int t : tiles[r]) {
      if (!seen[t] && box_fits(d, g, boxes.boxes[b], t)) {
        cand.push_back(t);
      }
    }
    std::ranges::sort(cand, [&](int x, int y) { return dist(b, x) < dist(b, y); });
    for (int t : cand) {
      if (seen[t]) {
        continue;
      }
      seen[t] = 1;
      touched.push_back(t);
      const auto it = owner[r].find(t);
      if (it == owner[r].end() || augment(r, it->second)) {
        owner[r][t] = b;
        return true;
      }
    }
    return false;
  };
  const auto try_region = [&](int r, int b) {
    const bool ok = augment(r, b);
    for (int t : touched) {
      seen[t] = 0;
    }
    touched.clear();
    return ok;
  };
  std::vector<int> left;
  for (int b = 0; b < int(boxes.boxes.size()); ++b) {
    if (boxes.boxes[b].placed) {  // legal_boxes: on its legal tile, which no other box gets
      owner[boxes.boxes[b].region][g.tile_of(boxes.boxes[b].cx, boxes.boxes[b].cy)] = b;
      continue;
    }
    if (!try_region(boxes.boxes[b].region, b)) {
      left.push_back(b);
    }
  }
  size_t moved = 0, split = 0;
  for (size_t li = 0; li < left.size(); ++li) {  // the list grows as boxes split
    const int                           b = left[li];
    std::vector<std::pair<double, int>> order;  // regions by their nearest tile to the box
    for (int r = 0; r < g.regions; ++r) {
      double best = INFINITY;
      for (int t : tiles[r]) {
        best = std::min(best, dist(b, t));
      }
      order.emplace_back(best, r);
    }
    std::ranges::sort(order);
    bool placed = false;
    for (const auto& [_, r] : order) {
      if (r != boxes.boxes[b].region && try_region(r, b)) {
        boxes.boxes[b].region = r;
        ++moved;
        placed = true;
        break;
      }
    }
    if (!placed && boxes.boxes[b].cells.size() > 1) {  // a box no region holds whole splits into its cells
      auto& box = boxes.boxes[b];
      for (size_t k = 1; k < box.cells.size(); ++k) {
        Box single                         = box;
        single.cells                       = {box.cells[k]};
        single.sites                       = cell_sites(d, g, box.cells[k]);
        single.internal                    = 0;
        single.external                    = int(d.nets_of[size_t(box.cells[k])].size());
        boxes.box_of[size_t(box.cells[k])] = int(boxes.boxes.size());
        left.push_back(int(boxes.boxes.size()));
        boxes.boxes.push_back(std::move(single));
      }
      box.cells.resize(1);
      box.sites    = cell_sites(d, g, box.cells[0]);
      box.internal = 0;
      box.external = int(d.nets_of[size_t(box.cells[0])].size());
      ++split;
      left.push_back(b);  // the first cell tries again as a single
      placed = true;
    }
    if (!placed) {
      size_t free_tiles = 0, fitting = 0;
      for (int t = 0; t < int(g.size()); ++t) {
        free_tiles += !g.blocked[t];
        fitting    += !g.blocked[t] && box_fits(d, g, boxes.boxes[b], t);
      }
      throw Legroute_unfit(
          std::format("legroute: no free tile fits box {} ({} cells, {} sites, region {} of {}; {} free tiles, {} fit it)",
                      b,
                      boxes.boxes[b].cells.size(),
                      boxes.boxes[b].sites,
                      boxes.boxes[b].region,
                      g.regions,
                      free_tiles,
                      fitting));
    }
  }
  if (split > 0) {
    std::fprintf(stderr, "legroute: %zu boxes no region held whole split into their cells\n", split);
  }
  return moved;
}

struct Region_budget_expired {};
struct Region_budget {
  Clock::time_point         start, deadline;
  size_t                    checkpoints = 0;
  bool                      decoded     = false;  // a complete capacity-checked answer, safe to keep if a timing tier expires
  std::vector<int>          fallback;
  double                    build_seconds = -1, initial_seconds = 0;
  std::optional<Sat_status> initial_status, seed_status;
  unsigned                  remaining_ms(Clock::time_point now = Clock::now()) const {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    return left > 0 ? unsigned(std::min<long long>(left, std::numeric_limits<unsigned>::max())) : 0;
  }
  void check(bool force = false) {
    if ((force || (++checkpoints & 255u) == 0) && Clock::now() >= deadline) {
      throw Region_budget_expired{};
    }
  }
};

// CNF with a constant-true literal: clauses fold constants away.
struct Cnf {
  Region_budget& budget;
  Sat_solver     solver;
  int            t;  // the true literal
  size_t         clauses = 0, variables = 1;
  explicit Cnf(Region_budget& b) : budget(b), solver(Sat_backend::cadical) {
    t = solver.new_variable();
    solver.clause({t});
  }
  int var() {
    budget.check();
    ++variables;
    return solver.new_variable();
  }
  void add(std::initializer_list<int> lits) {
    budget.check();
    int    buf[8];
    size_t n = 0;
    for (int l : lits) {
      if (l == t) {
        return;
      }
      if (l != -t) {
        buf[n++] = l;
      }
    }
    solver.clause(std::span<const int>(buf, n));
    ++clauses;
  }
  void add(const std::vector<int>& lits) {
    budget.check();
    std::vector<int> c;
    for (int l : lits) {
      if (l == t) {
        return;
      }
      if (l != -t) {
        c.push_back(l);
      }
    }
    solver.clause(c);
    ++clauses;
  }
  // at most k of xs: a totalizer whose unary counts stop at k + 1 (only the upward implications)
  std::vector<int> count(const std::vector<int>& xs, size_t lo, size_t hi, int k) {
    if (hi - lo == 1) {
      return {xs[lo]};
    }
    const size_t     mid = (lo + hi) / 2;
    const auto       a = count(xs, lo, mid, k), b = count(xs, mid, hi, k);
    const int        m = std::min<int>(int(a.size() + b.size()), k + 1);
    std::vector<int> c(m);
    for (auto& v : c) {
      v = var();
    }
    for (int i = 0; i < int(a.size()); ++i) {
      add({-a[i], c[std::min(i, m - 1)]});
    }
    for (int j = 0; j < int(b.size()); ++j) {
      add({-b[j], c[std::min(j, m - 1)]});
    }
    for (int i = 0; i < int(a.size()); ++i) {
      for (int j = 0; j < int(b.size()) && i + j + 1 < m; ++j) {
        add({-a[i], -b[j], c[i + j + 1]});
      }
    }
    return c;
  }
  // the literal "more than k of xs" (0: never), for a bound an assumption holds
  int over(const std::vector<int>& xs, int k) {
    if (int(xs.size()) <= k) {
      return 0;
    }
    if (k <= 0) {
      const int o = var();
      for (int x : xs) {
        add({-x, o});
      }
      return o;
    }
    return count(xs, 0, xs.size(), k)[k];
  }
  void at_most(const std::vector<int>& xs, int k) {
    if (int(xs.size()) <= k) {
      return;
    }
    if (k <= 0) {
      for (int x : xs) {
        add({-x});
      }
      return;
    }
    if (k == 1) {  // pairwise-free ladder for one
      int prev = 0;
      for (size_t i = 0; i < xs.size(); ++i) {
        if (prev) {
          add({-xs[i], -prev});
        }
        if (i + 1 < xs.size()) {
          const int s = var();
          add({-xs[i], s});
          if (prev) {
            add({-prev, s});
          }
          prev = s;
        }
      }
      return;
    }
    const auto c = count(xs, 0, xs.size(), k);
    add({-c[k]});
  }
};

// diagnostic: clauses Cnf::at_most(n inputs, k) adds (the same totalizer recursion, nothing built)
size_t at_most_clauses(int n, int k) {
  if (n <= k) {
    return 0;
  }
  if (k <= 0) {
    return size_t(n);
  }
  if (k == 1) {
    return 3 * size_t(n);
  }
  std::map<int, std::pair<int, size_t>>      memo;  // inputs -> (outputs, clauses)
  std::function<std::pair<int, size_t>(int)> count = [&](int s) -> std::pair<int, size_t> {
    if (s == 1) {
      return {1, 0};
    }
    if (const auto it = memo.find(s); it != memo.end()) {
      return it->second;
    }
    const auto [a, ca] = count(s / 2);
    const auto [b, cb] = count(s - s / 2);
    const int m        = std::min(a + b, k + 1);
    size_t    c        = ca + cb + size_t(a) + size_t(b);
    for (int i = 0; i < a; ++i) {
      c += size_t(std::max(0, std::min(b, m - 1 - i)));
    }
    return memo[s] = {m, c};
  };
  return count(n).second + 1;
}

struct Endpoint {
  int box = -1, tile = -1;  // a box of the region (placed by the solver) or a fixed tile
  int hop           = -1;   // hop_choice: a free hop, its tile chosen by this region's SAT among the hop's candidates
                            // (tile stays the hop chooser's tile for every reader outside the model)
  int part          = -1;   // connected component of the global tree inside this region; -1 without a routed tree
  int anchor        = -1;   // immutable attachment to an admitted coarse backbone
  int interface_id  = -1;   // stable crossing identity, distinct from a coincident pin or another crossing
  int anchor_choice = -1;   // model-local bounded choice among existing nodes of the same coarse component
};
struct Region_conn {
  int net = -1, a = -1, b = -1;  // endpoint ids of the region
  int part = -1;                 // >=0: a charged attachment stub within this coarse component's corridor
};
struct Net_ends {
  std::map<int, std::vector<Endpoint>>         per_region;
  int                                          driver_region = -1;
  // Exact connected coarse components containing real endpoints. Pass-through
  // components remain the existing retained pieces. An isolated attachment is
  // represented by a singleton set, never by an invented interior connection.
  std::map<std::pair<int, int>, std::set<int>> coarse_parts;      // (region, part) -> backbone tiles
  std::map<std::pair<int, int>, std::set<int>> coarse_junctions;  // existing piece ends; selecting one preserves graph attachment
  std::map<std::pair<int, int>, std::set<std::pair<int, int>>>
       coarse_edges;  // exact seed edges, never inferred from adjacent tiles
  bool preserve_coarse = false;
  bool refine_corridor = false;  // production movable route-driven flow; old backbone model remains a reference
};

// A limitation of the optional fixed-backbone proposal, not a failure of the
// complete corridor model. Other malformed-witness/model errors still escape.
struct Coarse_stub_unsupported : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// A selectable graph for one connected component of the admitted regional
// forest. Edge literals are shared by physical net/edge across components;
// connectivity is always checked within this component's permitted graph.
struct Corridor_model {
  int                                           net = -1, part = -1;
  std::set<int>                                 tiles;
  std::vector<std::vector<std::pair<int, int>>> terminals;  // endpoint domain: (tile, selected literal)
  std::vector<std::tuple<int, int, int>>        edges;      // adjacent (a, b, selected net-edge literal)
  std::set<std::pair<int, int>>                 seed;
  bool                                          identity = false;
  // These definitions belong to this model's Cnf and stay valid across all
  // endpoint choices. Never copy this cache into a model with another solver.
  mutable std::set<std::vector<int>>            connectivity_cuts;
};

std::vector<std::pair<int, int>> corridor_selected_terminals(const Cnf& cnf, const Corridor_model& model) {
  std::vector<std::pair<int, int>> result;
  for (const auto& domain : model.terminals) {
    std::optional<std::pair<int, int>> selected;
    for (const auto& candidate : domain) {
      if (cnf.solver.value(candidate.second)) {
        if (selected) {
          throw std::logic_error("legroute corridor endpoint has multiple selected tiles");
        }
        selected = candidate;
      }
    }
    if (!selected || !model.tiles.contains(selected->first)) {
      throw std::logic_error("legroute corridor endpoint has no selected permitted tile");
    }
    result.push_back(*selected);
  }
  return result;
}

size_t add_corridor_connectivity_cuts(Cnf& cnf, const std::vector<Corridor_model>& models, Region_budget& budget) {
  struct Cut {
    const Corridor_model* model;
    std::vector<int>      subset, crossing, inside, outside;
    bool                  always_inside = false, always_outside = false;
  };
  std::vector<Cut> pending;
  for (const auto& model : models) {
    budget.check();
    const auto terminals = corridor_selected_terminals(cnf, model);
    if (terminals.size() < 2) {
      continue;
    }
    std::map<int, std::vector<int>> adjacency;
    for (const auto& [a, b, lit] : model.edges) {
      budget.check();
      if (cnf.solver.value(lit)) {
        adjacency[a].push_back(b);
        adjacency[b].push_back(a);
      }
    }
    std::set<int>              visited;
    std::vector<std::set<int>> components;
    for (const auto& [tile, literal] : terminals) {
      (void)literal;
      if (visited.contains(tile)) {
        continue;
      }
      auto& reached = components.emplace_back();
      reached.insert(tile);
      visited.insert(tile);
      std::vector<int> queue{tile};
      for (size_t at = 0; at < queue.size(); ++at) {
        for (int q : adjacency[queue[at]]) {
          budget.check();
          if (reached.insert(q).second) {
            visited.insert(q);
            queue.push_back(q);
          }
        }
      }
    }
    if (components.size() < 2) {
      continue;
    }
    std::set<std::vector<int>> batch;
    for (const auto& reached : components) {
      std::vector<int> subset(reached.begin(), reached.end()), complement;
      for (int t : model.tiles) {
        budget.check();
        if (!reached.contains(t)) {
          complement.push_back(t);
        }
      }
      // S and its complement define the same cut. Canonicalize before
      // allocating membership variables or duplicating any clauses.
      if (complement.size() < subset.size() || (complement.size() == subset.size() && complement < subset)) {
        subset.swap(complement);
      }
      if (!batch.insert(subset).second) {
        continue;
      }
      if (model.connectivity_cuts.contains(subset)) {
        throw std::logic_error("legroute corridor assignment violates an already imposed connectivity cut");
      }
      const auto inside = [&](int t) { return std::binary_search(subset.begin(), subset.end(), t); };
      Cut        cut{&model, subset};
      for (const auto& [a, b, lit] : model.edges) {
        budget.check();
        if (inside(a) != inside(b)) {
          cut.crossing.push_back(lit);
        }
      }
      for (const auto& domain : model.terminals) {
        bool all_inside = !domain.empty(), all_outside = !domain.empty();
        for (const auto& [tile, literal] : domain) {
          budget.check();
          const bool in = inside(tile);
          (in ? cut.inside : cut.outside).push_back(literal);
          all_inside  &= in;
          all_outside &= !in;
        }
        // Exactly one position of each endpoint is selected. An endpoint
        // whose whole domain is on one side makes that membership constant.
        cut.always_inside  |= all_inside;
        cut.always_outside |= all_outside;
      }
      pending.push_back(std::move(cut));
    }
  }
  // Finish reading every SAT value before allocating variables or adding a
  // clause invalidates the current assignment. Membership ranges over ALL
  // endpoint positions, so a cut survives later movement of root and leaves.
  const auto membership = [&](std::vector<int>& literals, bool always) {
    std::ranges::sort(literals);
    literals.erase(std::unique(literals.begin(), literals.end()), literals.end());
    if (always || std::binary_search(literals.begin(), literals.end(), cnf.t)) {
      return cnf.t;
    }
    if (literals.empty()) {
      return -cnf.t;
    }
    if (literals.size() == 1) {
      return literals.front();
    }
    const int result = cnf.var();
    for (int literal : literals) {
      cnf.add({-literal, result});
    }
    std::vector<int> reverse{-result};
    reverse.insert(reverse.end(), literals.begin(), literals.end());
    cnf.add(reverse);
    return result;
  };
  for (auto& cut : pending) {
    const int inside  = membership(cut.inside, cut.always_inside);
    const int outside = membership(cut.outside, cut.always_outside);
    cut.crossing.push_back(-inside);
    cut.crossing.push_back(-outside);
    std::ranges::sort(cut.crossing);
    cut.crossing.erase(std::unique(cut.crossing.begin(), cut.crossing.end()), cut.crossing.end());
    cnf.add(cut.crossing);
    cut.model->connectivity_cuts.insert(std::move(cut.subset));
  }
  return pending.size();
}

std::set<std::pair<int, int>> decode_corridor_edges(const Cnf& cnf, const Corridor_model& model, Region_budget& budget) {
  const auto terminals = corridor_selected_terminals(cnf, model);
  if (terminals.empty()) {
    throw std::logic_error("legroute corridor component has no attachments");
  }
  std::map<int, std::vector<int>> adjacency;
  std::set<std::pair<int, int>>   selected;
  for (const auto& [a, b, lit] : model.edges) {
    budget.check();
    if (cnf.solver.value(lit)) {
      adjacency[a].push_back(b);
      adjacency[b].push_back(a);
      selected.emplace(a, b);
    }
  }
  std::map<int, int> parent{
      {terminals.front().first, -1}
  };
  std::vector<int> queue{terminals.front().first};
  for (size_t at = 0; at < queue.size(); ++at) {
    for (int q : adjacency[queue[at]]) {
      budget.check();
      if (parent.emplace(q, queue[at]).second) {
        queue.push_back(q);
      }
    }
  }
  std::set<std::pair<int, int>> result;
  for (const auto& [terminal, lit] : terminals) {
    (void)lit;
    if (!parent.contains(terminal)) {
      throw std::runtime_error("legroute corridor connectivity model error: decoded attachments disconnected");
    }
    for (int t = terminal; parent.at(t) >= 0; t = parent.at(t)) {
      budget.check();
      const int q = parent.at(t);
      result.emplace(std::min(t, q), std::max(t, q));
    }
  }
  // In the explicit radius-zero identity diagnostic, preserve every original
  // edge. Otherwise remove disconnected/cyclic/unused selected geometry only
  // by taking a connected endpoint-spanning subset; the emitted union is then
  // independently recounted against capacity before commit.
  if (model.identity) {
    if (selected != model.seed) {
      throw std::runtime_error("legroute corridor identity model error: original edges changed");
    }
    return selected;
  }
  return result;
}

std::set<int> coarse_corridor(const Grid& g, int region, const std::set<int>& backbone, unsigned radius) {
  std::set<int>    result = backbone;
  std::vector<int> frontier(backbone.begin(), backbone.end());
  for (int t : frontier) {
    if (t < 0 || size_t(t) >= g.size() || g.region[size_t(t)] != region) {
      throw std::runtime_error("legroute coarse witness repair required: backbone attachment leaves its region");
    }
  }
  for (unsigned step = 0; step < radius && !frontier.empty(); ++step) {
    std::vector<int> next;
    for (int t : frontier) {
      for (int q : {g.col(t) > 0 ? t - 1 : -1, g.col(t) + 1 < g.cols ? t + 1 : -1, t - g.cols, t + g.cols}) {
        if (q >= 0 && size_t(q) < g.size() && g.region[size_t(q)] == region && result.insert(q).second) {
          next.push_back(q);
        }
      }
    }
    frontier = std::move(next);
  }
  return result;
}

std::vector<int> coarse_anchor_candidates(const Grid& g, const std::set<int>& backbone, const std::set<int>& junctions,
                                          int original, unsigned radius) {
  if (!backbone.contains(original)) {
    throw std::runtime_error("legroute coarse witness repair required: original attachment is outside its backbone");
  }
  std::vector<int> result{original};
  for (int tile : junctions) {
    if (!backbone.contains(tile)) {
      throw std::runtime_error("legroute coarse witness repair required: attachment junction is outside its backbone");
    }
    if (tile != original && unsigned(std::abs(g.col(tile) - g.col(original)) + std::abs(g.row(tile) - g.row(original))) <= radius) {
      result.push_back(tile);
    }
  }
  std::ranges::sort(result, [&](int a, int b) {
    const auto distance = [&](int t) { return std::abs(g.col(t) - g.col(original)) + std::abs(g.row(t) - g.row(original)); };
    return std::pair{distance(a), a} < std::pair{distance(b), b};
  });
  // A bounded optional extension of the original witness, not a wider global
  // route search. Existing nodes avoid implicit junctions inside emitted runs.
  constexpr size_t limit = 8;
  if (result.size() > limit) {
    result.resize(limit);
  }
  return result;
}

bool coarse_stub_inside(const Grid& g, const std::set<int>& corridor, int a, int b, bool horizontal_first, Region_budget& budget) {
  const int hr = g.row(horizontal_first ? a : b), vc = g.col(horizontal_first ? b : a);
  for (int c = std::min(g.col(a), g.col(b)); c <= std::max(g.col(a), g.col(b)); ++c) {
    budget.check();
    if (!corridor.contains(g.tile(c, hr))) {
      return false;
    }
  }
  for (int r = std::min(g.row(a), g.row(b)); r <= std::max(g.row(a), g.row(b)); ++r) {
    budget.check();
    if (!corridor.contains(g.tile(vc, r))) {
      return false;
    }
  }
  return true;
}

std::map<int, std::vector<Endpoint>> endpoint_groups(const std::vector<Endpoint>& endpoints) {
  std::map<int, std::vector<Endpoint>> groups;
  for (const auto& e : endpoints) {
    groups[e.part].push_back(e);
  }
  return groups;
}

// Components of one net may merge or share a track inside the region. Count the
// union of their forced cuts, once per net, rather than summing their demands.
void add_forced_cuts(std::vector<std::pair<int, int>>& ranges, std::vector<int>& forced, int origin,
                     const std::set<int>* reserved = nullptr) {
  std::ranges::sort(ranges);
  int        lo = -1, hi = -1;
  const auto add = [&] {
    if (hi > lo) {
      ++forced[size_t(lo + 1 - origin)];
      --forced[size_t(hi + 1 - origin)];
      // A fixed piece of this same net already pays for crossing this cut.
      // This is an optimistic lower bound; the edge rows below still enforce
      // the exact union of fixed and variable usage.
      if (reserved) {
        for (auto it = reserved->upper_bound(lo); it != reserved->end() && *it <= hi; ++it) {
          --forced[size_t(*it - origin)];
          ++forced[size_t(*it + 1 - origin)];
        }
      }
    }
  };
  for (const auto& [a, b] : ranges) {
    if (b <= a) {
      continue;
    }
    if (a > hi) {
      add();
      lo = a;
    }
    hi = std::max(hi, b);
  }
  add();
}

// Bipartite matching of items to their candidate tiles (augmenting paths, candidates in preference order). The
// tile of each item, or empty when some item finds none.
std::vector<int> match_tiles(const std::vector<std::vector<int>>& pref, size_t tiles) {
  std::unordered_map<int, int> owner;  // tile -> item
  std::vector<char>            seen(tiles, 0);
  std::vector<int>             touched;
  std::function<bool(int)>     augment = [&](int i) {
    for (int t : pref[i]) {
      if (seen[t]) {
        continue;
      }
      seen[t] = 1;
      touched.push_back(t);
      const auto it = owner.find(t);
      if (it == owner.end() || augment(it->second)) {
        owner[t] = i;
        return true;
      }
    }
    return false;
  };
  for (int i = 0; i < int(pref.size()); ++i) {
    const bool ok = augment(i);
    for (int t : touched) {
      seen[t] = 0;
    }
    touched.clear();
    if (!ok) {
      return {};
    }
  }
  std::vector<int> tile(pref.size(), -1);
  for (const auto& [t, i] : owner) {
    tile[i] = t;
  }
  return tile;
}

// Critical pre-placement: in each region, the crit_boxes fraction of its boxes with the highest criticality take tiles for timing
// before the SAT, which then keeps them fixed (fewer free boxes, the critical routes known). Per region, critical
// boxes go greedily (most critical first) to the tile in their window with the least weighted star length of their
// nets (Manhattan tiles plus half a tile per bend: short and straight for RC), the other endpoints at their
// placement tiles; a few passes then move each to a better free tile. Critical boxes that are neighbors without a
// shared net pay a spread penalty, so routes around them keep room. A region whose other boxes no longer match
// its free tiles loses its pins. Marks Box::fixed and Box::tile.
void place_critical(const Design& d, const Legroute_options& o, const Grid& g, Boxes& boxes, const std::vector<Net_ends>& ends,
                    Legroute_report& rep) {
  std::vector<int> order;
  for (int b = 0; b < int(boxes.boxes.size()); ++b) {
    if (boxes.boxes[b].crit > 0 && !boxes.boxes[b].fixed) {  // a box fixed already (legal_boxes, a global slot) keeps its tile
      order.push_back(b);
    }
  }
  std::ranges::stable_sort(order, [&](int a, int b) {
    const auto &x = boxes.boxes[a], &y = boxes.boxes[b];
    return x.crit != y.crit ? x.crit > y.crit : x.ramp > y.ramp;
  });
  {  // the top fraction of each region (critical boxes often crowd a few regions)
    std::vector<size_t> in_region(g.regions, 0), quota(g.regions, 0);
    for (const auto& box : boxes.boxes) {
      ++in_region[box.region];
    }
    for (int r = 0; r < g.regions; ++r) {
      quota[r] = size_t(std::ceil(o.crit_boxes * double(in_region[r])));
    }
    std::erase_if(order, [&](int b) {
      auto& q = quota[boxes.boxes[b].region];
      return q == 0 || (--q, false);
    });
  }
  if (order.empty()) {
    return;
  }
  std::vector<int> home(boxes.boxes.size()), cur(boxes.boxes.size(), -1);  // placement tile, chosen tile (pinned)
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    home[b] = g.tile_of(boxes.boxes[b].cx, boxes.boxes[b].cy);
  }
  // per pinned box: its nets with two or more endpoints in its region
  std::unordered_map<int, std::vector<int>> nets_of;
  std::set<int>                             all_nets;
  for (int b : order) {
    std::set<int> seen;
    for (int c : boxes.boxes[b].cells) {
      for (int n : d.nets_of[c]) {
        const auto it = ends[n].per_region.find(boxes.boxes[b].region);
        if (it != ends[n].per_region.end() && it->second.size() >= 2 && seen.insert(n).second) {
          nets_of[b].push_back(n);
          all_nets.insert(n);
        }
      }
    }
  }
  const auto pos      = [&](const Endpoint& e) { return e.box < 0 ? e.tile : cur[e.box] >= 0 ? cur[e.box] : home[e.box]; };
  const auto net_cost = [&](int n, int region) {
    const auto& list = ends[n].per_region.at(region);
    const int   t0   = pos(list[0]);
    double      cost = 0;
    for (size_t k = 1; k < list.size(); ++k) {
      const int t = pos(list[k]), dc = std::abs(g.col(t) - g.col(t0)), dr = std::abs(g.row(t) - g.row(t0));
      cost += dc + dr + (dc && dr ? .5 : 0);
    }
    return cost * d.nets[n].weight;
  };
  const auto total = [&] {
    double cost = 0;
    for (int n : all_nets) {
      for (const auto& [region, list] : ends[n].per_region) {
        if (list.size() >= 2) {
          cost += net_cost(n, region);
        }
      }
    }
    return cost;
  };
  rep.crit_cost_before += total();
  std::vector<char>            taken(g.size(), 0);
  std::unordered_map<int, int> at;  // tile -> pinned box
  constexpr double             spread   = 4;
  const auto                   box_cost = [&](int b, int t) {
    const int was = cur[b];
    cur[b]        = t;
    double cost   = 0;
    for (int n : nets_of[b]) {
      cost += net_cost(n, boxes.boxes[b].region);
    }
    for (int dr = -1; dr <= 1; ++dr) {
      for (int dc = -1; dc <= 1; ++dc) {
        const int c = g.col(t) + dc, r = g.row(t) + dr;
        if ((dc || dr) && c >= 0 && c < g.cols && r >= 0 && r < g.rows) {
          const auto it = at.find(g.tile(c, r));
          if (it != at.end() && it->second != b && std::ranges::none_of(nets_of[b], [&](int n) {
                return std::ranges::any_of(ends[n].per_region.at(boxes.boxes[b].region),
                                           [&](const Endpoint& e) { return e.box == it->second; });
              })) {
            cost += spread;
          }
        }
      }
    }
    cur[b] = was;
    return cost;
  };
  const auto candidates = [&](int b) {
    const auto&      box = boxes.boxes[b];
    std::vector<int> list;
    for (int w = int(o.window_tiles); list.empty() && w <= 4 * int(o.window_tiles) + 4; w += 2) {
      for (int r = std::max(0, g.row(home[b]) - w); r <= std::min(g.rows - 1, g.row(home[b]) + w); ++r) {
        for (int c = std::max(0, g.col(home[b]) - w); c <= std::min(g.cols - 1, g.col(home[b]) + w); ++c) {
          const int t = g.tile(c, r);
          if (g.region[t] == box.region && !g.blocked[t] && (!taken[t] || at.at(t) == b) && box_fits(d, g, box, t)) {
            list.push_back(t);
          }
        }
      }
    }
    return list;
  };
  const auto best_tile = [&](int b) {
    int    best = -1;
    double cost = INFINITY;
    for (int t : candidates(b)) {
      const double c = box_cost(b, t);
      if (c < cost - 1e-9 || (c < cost + 1e-9 && best >= 0 && t == cur[b])) {
        cost = c;
        best = t;
      }
    }
    return best;
  };
  const auto put = [&](int b, int t) {
    if (cur[b] >= 0) {
      taken[cur[b]] = 0;
      at.erase(cur[b]);
    }
    cur[b] = t;
    if (t >= 0) {
      taken[t] = 1;
      at[t]    = b;
    }
  };
  std::vector<int> placed;
  for (int b : order) {
    const int t = best_tile(b);
    if (t >= 0) {
      put(b, t);
      placed.push_back(b);
    }
  }
  for (int pass = 0; pass < 8; ++pass) {
    bool moved = false;
    for (int b : placed) {
      const int t = best_tile(b);
      if (t >= 0 && t != cur[b]) {
        put(b, t);
        moved = true;
      }
    }
    if (!moved) {
      break;
    }
  }
  // the other boxes of each region must still match its free tiles
  std::map<int, std::vector<int>> free_boxes, region_tiles;
  for (int b = 0; b < int(boxes.boxes.size()); ++b) {
    if (cur[b] < 0) {
      free_boxes[boxes.boxes[b].region].push_back(b);
    }
  }
  for (int t = 0; t < int(g.size()); ++t) {
    if (!g.blocked[t] && !taken[t]) {
      region_tiles[g.region[t]].push_back(t);
    }
  }
  std::set<int> lost;
  for (const auto& [r, list] : free_boxes) {
    std::vector<std::vector<int>> pref(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
      for (int t : region_tiles[r]) {
        if (box_fits(d, g, boxes.boxes[list[i]], t)) {
          pref[i].push_back(t);
        }
      }
    }
    if (match_tiles(pref, g.size()).empty()) {
      lost.insert(r);
    }
  }
  rep.crit_unpinned_regions += lost.size();
  for (int b : placed) {
    if (lost.contains(boxes.boxes[b].region)) {
      put(b, -1);
    }
  }
  rep.crit_cost_after += total();
  for (int b : placed) {
    if (cur[b] >= 0) {
      boxes.boxes[b].fixed = true;
      boxes.boxes[b].tile  = cur[b];
      ++rep.crit_pinned;
    }
  }
}
// A piece of a fixed tree: a straight run between two tiles (a trunk or a rib), for the routing model.
struct Tree_piece {
  int net = -1, region = -1, a = -1, b = -1;
};
// A fixed tree: the boxes it fixed, the (tile, h) tracks it took (one each) and its pieces.
struct Fixed_tree {
  int                              net = -1, region = -1;
  std::vector<int>                 boxes, members;  // boxes it fixed; every box of the net in the region
  std::vector<std::pair<int, int>> uses;
  std::vector<Tree_piece>          pieces;
  bool                             dropped = false;
};

// Fixed 1:n trees (Legroute_options::tree_boxes): per region, the 1:n nets from the highest fanout down, whole fanout
// levels, while the region's fixed boxes (the pinned critical ones count first) stay within tree_boxes of its boxes.
// Most critical net first, its free boxes move at most tree_moves tiles from their placement tiles to shorten a
// single-trunk Steiner tree (an H trunk on one row with vertical ribs to its members, or a V trunk with horizontal
// ribs; lengths in um, so the tile's aspect counts), away from tiles other trees already load. The boxes are then
// fixed and the tree's tracks leave the tile capacities: the SAT sees the boxes as fixed endpoints and never the net.
// A region whose other boxes no longer match its free tiles keeps no tree. Marks treed (net, region).
std::vector<Fixed_tree> place_trees(const Design& d, const Legroute_options& o, const Grid& g, Boxes& boxes,
                                    const std::vector<Net_ends>& ends, const std::vector<int>& dem_h, const std::vector<int>& dem_v,
                                    std::vector<int>& cap_h, std::vector<int>& cap_v, std::set<std::pair<int, int>>& treed,
                                    Legroute_report& rep) {
  const int                     moves = int(o.tree_moves);
  const double                  tw = double(g.tile_w) / double(g.dbu), th = double(g.tile_h) / double(g.dbu);  // um
  constexpr double              overlap = 4, displace = .1;  // per unit of another tree's load share; per um moved
  std::vector<Fixed_tree>       trees;
  std::vector<std::vector<int>> region_boxes(g.regions);
  for (int b = 0; b < int(boxes.boxes.size()); ++b) {
    region_boxes[boxes.boxes[b].region].push_back(b);
  }
  std::vector<int>  load_h(g.size(), 0), load_v(g.size(), 0);
  std::vector<char> taken(g.size(), 0);
  for (const auto& box : boxes.boxes) {
    if (box.fixed) {
      taken[box.tile] = 1;
    }
  }
  const auto                    home    = [&](int b) { return g.tile_of(boxes.boxes[b].cx, boxes.boxes[b].cy); };
  // tree_load selection: the fixed demand per tile and direction besides the tree tracks (fixed boxes' in-box demand,
  // straight connections between fixed endpoints), and each box's nets
  const bool                    by_load = o.tree_load > 0;
  std::vector<int>              fixed_h(g.size(), 0), fixed_v(g.size(), 0);
  std::vector<std::vector<int>> box_nets(boxes.boxes.size());
  if (by_load) {
    for (size_t n = 0; n < d.nets.size(); ++n) {
      for (const auto& [r, list] : ends[n].per_region) {
        for (const auto& e : list) {
          if (e.box >= 0 && (box_nets[e.box].empty() || box_nets[e.box].back() != int(n))) {
            box_nets[e.box].push_back(int(n));
          }
        }
      }
    }
  }
  // the straight connections (the SAT's star from a net's first endpoint) between two placed endpoints with one of
  // the changed boxes at an end, nets other than skip and not treed, onto add as (tile, h) -> tracks
  const auto straight = [&](int                                 r,
                            const std::vector<int>&             changed,
                            const std::function<int(int)>&      tile_of_box,
                            int                                 skip,
                            std::map<std::pair<int, int>, int>& add) {
    std::set<std::pair<int, int>> done;  // (net, endpoint)
    const std::set<int>           changed_set(changed.begin(), changed.end());
    for (int b : changed) {
      for (int m : box_nets[b]) {
        const auto it = ends[m].per_region.find(r);
        if (m == skip || it == ends[m].per_region.end() || treed.contains({m, r})) {
          continue;
        }
        const auto& list = it->second;
        const auto  at   = [&](const Endpoint& e) { return e.box < 0 ? e.tile : tile_of_box(e.box); };
        const int   ta   = at(list[0]);
        for (size_t k = 1; ta >= 0 && k < list.size(); ++k) {
          const bool mine
              = (list[0].box >= 0 && changed_set.contains(list[0].box)) || (list[k].box >= 0 && changed_set.contains(list[k].box));
          const int tb = at(list[k]);
          if (!mine || tb < 0 || tb == ta || (g.row(ta) != g.row(tb) && g.col(ta) != g.col(tb))
              || !done.emplace(m, int(k)).second) {
            continue;
          }
          if (list[0].box >= 0 && list[k].box >= 0 && d.nets[m].pins.size() == 2
              && std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb)) == 1) {
            continue;  // neighbors: in-box metal
          }
          const int h = g.row(ta) == g.row(tb);
          for (int i = std::min(g.col(ta), g.col(tb)); i <= std::max(g.col(ta), g.col(tb)); ++i) {
            for (int j = std::min(g.row(ta), g.row(tb)); j <= std::max(g.row(ta), g.row(tb)); ++j) {
              if (g.region[g.tile(i, j)] == r) {
                ++add[{g.tile(i, j), h}];
              }
            }
          }
        }
      }
    }
  };
  const auto       fixed_tile_of = [&](int b) { return boxes.boxes[b].fixed ? boxes.boxes[b].tile : -1; };
  // the capacity clauses of the region's SAT model (its totalizers, ~97% of the model): per tile and direction, the
  // nets whose legs may cover it (windows of window_tiles around the placement, fixed boxes on their tiles) and the
  // occupant's in-box demand, over the tracks the trees leave
  std::vector<int> stamp(2 * g.size(), -1), inputs(2 * g.size(), 0);
  const auto       estimate = [&](int r, const std::vector<int>& B) {
    int rc0 = g.cols, rc1 = -1, rr0 = g.rows, rr1 = -1;
    for (int t : g.region_tiles[r]) {
      rc0 = std::min(rc0, g.col(t));
      rc1 = std::max(rc1, g.col(t));
      rr0 = std::min(rr0, g.row(t));
      rr1 = std::max(rr1, g.row(t));
    }
    const int  w   = int(o.window_tiles);
    const auto win = [&](const Endpoint& e) -> std::array<int, 4> {
      const int t = e.box < 0 ? e.tile : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : -1;
      if (t >= 0) {
        return {g.col(t), g.col(t), g.row(t), g.row(t)};
      }
      const int h0 = home(e.box);
      return {std::max(rc0, g.col(h0) - w),
              std::min(rc1, g.col(h0) + w),
              std::max(rr0, g.row(h0) - w),
              std::min(rr1, g.row(h0) + w)};
    };
    for (int t : g.region_tiles[r]) {
      stamp[2 * t] = stamp[2 * t + 1] = -1;
      inputs[2 * t] = inputs[2 * t + 1] = 0;
    }
    const auto mark = [&](int n, int t, int h) {
      if (g.region[t] == r && stamp[2 * t + h] != n) {
        stamp[2 * t + h] = n;
        ++inputs[2 * t + h];
      }
    };
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto it = ends[n].per_region.find(r);
      if (it == ends[n].per_region.end() || it->second.size() < 2 || treed.contains({int(n), r})) {
        continue;
      }
      const auto& list = it->second;
      const auto  wa   = win(list[0]);
      for (size_t k = 1; k < list.size(); ++k) {
        const auto wb = win(list[k]);
        const int  x0 = std::min(wa[0], wb[0]), x1 = std::max(wa[1], wb[1]), y0 = std::min(wa[2], wb[2]),
                   y1 = std::max(wa[3], wb[3]);
        for (const auto& wv : {wa, wb}) {
          for (int j = wv[2]; j <= wv[3]; ++j) {
            for (int i = x0; i <= x1; ++i) {
              mark(int(n), g.tile(i, j), 1);
            }
          }
          for (int i = wv[0]; i <= wv[1]; ++i) {
            for (int j = y0; j <= y1; ++j) {
              mark(int(n), g.tile(i, j), 0);
            }
          }
        }
      }
    }
    std::vector<std::array<int, 2>> top(g.size(), {0, 0});
    for (int b : B) {
      const auto wv = win(Endpoint{b, -1});
      for (int i = wv[0]; i <= wv[1]; ++i) {
        for (int j = wv[2]; j <= wv[3]; ++j) {
          auto& tp = top[g.tile(i, j)];
          tp       = {std::max(tp[0], dem_v[b]), std::max(tp[1], dem_h[b])};
        }
      }
    }
    size_t total = 0;
    for (int t : g.region_tiles[r]) {
      for (int h : {1, 0}) {
        const int k  = std::max(0, edge_limit((h ? cap_h : cap_v)[t], o) - (h ? load_h : load_v)[t]);
        total       += at_most_clauses(inputs[2 * t + h] + top[t][h], k);
      }
    }
    return total;
  };
  // tree_congestion (DCGP-style congestion potential): per tile and direction, the routing demand of every region net's
  // star connections at their placement tiles (each spreads its length over its bounding box, RUDY) plus the boxes'
  // in-box demand, over the tracks; a Poisson potential of it (Jacobi, zero-flux edges, scaled to [0, 1]) lets a trunk
  // or rib feel congestion nearby, so the trees route around it
  std::vector<double> psi_h, psi_v;
  const double        cong = 4 * o.tree_congestion;  // per tile at full potential, in tile lengths
  if (cong > 0) {
    std::vector<double> rho_h(g.size(), 0), rho_v(g.size(), 0);
    const auto          at = [&](const Endpoint& e) {
      return e.box < 0 ? e.tile : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : home(e.box);
    };
    for (size_t b = 0; b < boxes.boxes.size(); ++b) {
      rho_h[size_t(at({int(b), -1}))] += dem_h[b];
      rho_v[size_t(at({int(b), -1}))] += dem_v[b];
    }
    for (const auto& E : ends) {
      for (const auto& [r, list] : E.per_region) {
        for (size_t k = 1; k < list.size(); ++k) {
          const int    a = at(list[0]), b = at(list[k]);
          const int    c0 = std::min(g.col(a), g.col(b)), c1 = std::max(g.col(a), g.col(b));
          const int    r0 = std::min(g.row(a), g.row(b)), r1 = std::max(g.row(a), g.row(b));
          const double area = double(c1 - c0 + 1) * double(r1 - r0 + 1);
          for (int j = r0; j <= r1; ++j) {
            for (int i = c0; i <= c1; ++i) {
              rho_h[size_t(g.tile(i, j))] += double(c1 - c0) / area;
              rho_v[size_t(g.tile(i, j))] += double(r1 - r0) / area;
            }
          }
        }
      }
    }
    const auto potential = [&](std::vector<double>& rho, const std::vector<int>& cap) {
      double mean = 0;
      for (size_t t = 0; t < g.size(); ++t) {
        rho[t] /= double(std::max(1, cap[t]));
        mean   += rho[t];
      }
      mean /= double(std::max<size_t>(1, g.size()));
      std::vector<double> psi(g.size(), 0), next(g.size(), 0);
      for (int iter = 0; iter < 200; ++iter) {
        for (int t = 0; t < int(g.size()); ++t) {
          double sum = rho[size_t(t)] - mean;
          int    deg = 0;
          for (int q : {g.col(t) > 0 ? t - 1 : -1, g.col(t) + 1 < g.cols ? t + 1 : -1, t - g.cols, t + g.cols}) {
            if (q >= 0 && q < int(g.size())) {
              sum += psi[size_t(q)];
              ++deg;
            }
          }
          next[size_t(t)] = deg ? sum / deg : 0;
        }
        psi.swap(next);
      }
      const auto [lo, hi] = std::ranges::minmax_element(psi);
      for (auto& v : psi) {
        v = *hi > *lo ? (v - *lo) / (*hi - *lo) : 0;
      }
      return psi;
    };
    psi_h = potential(rho_h, cap_h);
    psi_v = potential(rho_v, cap_v);
  }
  // the tree of members on tiles with its trunk on axis (a row for H, a column for V): length (um) and, with the
  // penalties, the cost of the tracks it shares with other trees (and with tree_congestion, the potential it crosses)
  const auto tree = [&](const std::vector<int>& tiles, bool h, int axis, bool penalized) {
    int lo = INT32_MAX, hi = INT32_MIN;
    for (int t : tiles) {
      lo = std::min(lo, h ? g.col(t) : g.row(t));
      hi = std::max(hi, h ? g.col(t) : g.row(t));
    }
    double cost = (hi - lo) * (h ? tw : th);
    for (int t : tiles) {
      cost += std::abs((h ? g.row(t) : g.col(t)) - axis) * (h ? th : tw);
    }
    if (penalized) {
      for (int k = lo; k <= hi; ++k) {
        const int t  = h ? g.tile(k, axis) : g.tile(axis, k);
        cost        += overlap * (h ? tw : th) * (h ? load_h : load_v)[t] / std::max(1, (h ? cap_h : cap_v)[t]);
        if (cong > 0) {
          cost += cong * (h ? tw : th) * (h ? psi_h : psi_v)[size_t(t)];
        }
      }
      for (int t : tiles) {  // the ribs
        const int along = h ? g.col(t) : g.row(t), across = h ? g.row(t) : g.col(t);
        for (int k = std::min(across, axis); cong > 0 && across != axis && k <= std::max(across, axis); ++k) {
          cost += cong * (h ? th : tw) * (h ? psi_v : psi_h)[size_t(h ? g.tile(along, k) : g.tile(k, along))];
        }
      }
    }
    return cost;
  };
  const auto median = [&](const std::vector<int>& tiles, bool h) {
    std::vector<int> v;
    for (int t : tiles) {
      v.push_back(h ? g.row(t) : g.col(t));
    }
    std::ranges::nth_element(v, v.begin() + v.size() / 2);
    return v[v.size() / 2];
  };
  for (int r = 0; r < g.regions; ++r) {
    const auto& B = region_boxes[r];
    if (B.empty()) {
      continue;
    }
    const size_t                     pinned = size_t(std::ranges::count_if(B, [&](int b) { return boxes.boxes[b].fixed; }));
    std::vector<std::pair<int, int>> by_fan;  // (fanout, net): endpoints in the region beyond the first
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto it = ends[n].per_region.find(r);
      if (it != ends[n].per_region.end() && it->second.size() >= 3) {
        by_fan.emplace_back(int(it->second.size()) - 1, int(n));
      }
    }
    std::ranges::sort(by_fan, std::greater<>{});
    std::set<int>    touched;
    std::vector<int> chosen;
    if (by_load) {  // every 1:n net in order; the load limit and the clause target decide
      for (const auto& [_, n] : by_fan) {
        chosen.push_back(n);
      }
      if (o.tree_by_crit) {
        std::ranges::stable_sort(chosen, [&](int a, int b) { return d.nets[a].crit > d.nets[b].crit; });
      }
      std::ranges::fill(fixed_h, 0);  // the region's fixed demand so far (its pinned critical boxes)
      std::ranges::fill(fixed_v, 0);
      std::vector<int> pinned_here;
      for (int b : B) {
        if (boxes.boxes[b].fixed) {
          pinned_here.push_back(b);
          fixed_h[boxes.boxes[b].tile] += dem_h[b];
          fixed_v[boxes.boxes[b].tile] += dem_v[b];
        }
      }
      std::map<std::pair<int, int>, int> add;
      straight(r, pinned_here, fixed_tile_of, -1, add);
      for (const auto& [key, v] : add) {
        (key.second ? fixed_h : fixed_v)[key.first] += v;
      }
    }
    for (size_t i = 0; !by_load && i < by_fan.size();) {
      size_t        j    = i;
      std::set<int> more = touched;
      for (; j < by_fan.size() && by_fan[j].first == by_fan[i].first; ++j) {
        for (const auto& e : ends[by_fan[j].second].per_region.at(r)) {
          if (e.box >= 0 && !boxes.boxes[e.box].fixed) {
            more.insert(e.box);
          }
        }
      }
      if (double(more.size() + pinned) > o.tree_boxes * double(B.size())) {
        break;
      }
      touched = std::move(more);
      for (size_t k = i; k < j; ++k) {
        chosen.push_back(by_fan[k].second);
      }
      i = j;
    }
    if (!by_load) {
      std::ranges::stable_sort(chosen, [&](int a, int b) { return d.nets[a].crit > d.nets[b].crit; });
    }
    size_t est = by_load ? estimate(r, B) : 0;
    if (by_load) {
      rep.tree_clauses_before += est;
    }
    std::vector<int>                       fixed_here;
    std::vector<std::tuple<int, int, int>> loaded;  // (tile, h, tracks)
    const size_t                           trees_mark = trees.size();
    size_t                                 nets_here  = 0;
    double                                 before = 0, after = 0;
    for (int n : chosen) {
      if (by_load && double(est) < o.tree_clauses) {
        break;
      }
      if (by_load && o.tree_boxes > 0
          && double(std::ranges::count_if(B, [&](int b) { return boxes.boxes[b].fixed; })) > o.tree_boxes * double(B.size())) {
        break;
      }
      const auto&                   list = ends[n].per_region.at(r);
      std::vector<int>              fixed(list.size(), -1);  // tile of a fixed member
      std::vector<std::vector<int>> cand(list.size());
      std::vector<int>              start(list.size());
      bool                          stuck = false;
      for (size_t i = 0; i < list.size(); ++i) {
        const auto& e = list[i];
        if (e.box < 0 || boxes.boxes[e.box].fixed) {
          fixed[i] = start[i] = e.box < 0 ? e.tile : boxes.boxes[e.box].tile;
          continue;
        }
        const int h0 = start[i] = home(e.box);
        for (int dr = -moves; dr <= moves; ++dr) {
          for (int dc = -moves; dc <= moves; ++dc) {
            const int c = g.col(h0) + dc, rr = g.row(h0) + dr;
            if (c < 0 || c >= g.cols || rr < 0 || rr >= g.rows) {
              continue;
            }
            const int t = g.tile(c, rr);
            if (g.region[t] == r && !g.blocked[t] && !taken[t] && box_fits(d, g, boxes.boxes[e.box], t)) {
              cand[i].push_back(t);
            }
          }
        }
        stuck = stuck || cand[i].empty();
      }
      if (stuck) {
        ++rep.tree_skipped_nets;
        continue;
      }
      double before_net = INFINITY;
      for (bool h : {true, false}) {
        before_net = std::min(before_net, tree(start, h, median(start, h), false));
      }
      double           best      = INFINITY;
      bool             best_h    = true;
      int              best_axis = 0;
      std::vector<int> best_tiles;
      for (bool h : {true, false}) {
        std::vector<int> tiles = start;
        int              axis  = median(tiles, h);
        for (int iter = 0; iter < 3; ++iter) {
          std::set<int> used;
          for (size_t i = 0; i < list.size(); ++i) {
            if (fixed[i] >= 0) {
              used.insert(fixed[i]);
            }
          }
          for (size_t i = 0; i < list.size(); ++i) {
            if (fixed[i] >= 0) {
              continue;
            }
            int lo = INT32_MAX, hi = INT32_MIN;  // the others along the trunk
            for (size_t j = 0; j < list.size(); ++j) {
              if (j != i) {
                lo = std::min(lo, h ? g.col(tiles[j]) : g.row(tiles[j]));
                hi = std::max(hi, h ? g.col(tiles[j]) : g.row(tiles[j]));
              }
            }
            double cost = INFINITY;
            int    pick = -1;
            for (int t : cand[i]) {
              if (used.contains(t)) {
                continue;
              }
              const int along = h ? g.col(t) : g.row(t), across = h ? g.row(t) : g.col(t);
              double    c = std::abs(across - axis) * (h ? th : tw)
                            + (std::max(0, lo - along) + std::max(0, along - hi)) * (h ? tw : th)
                            + displace * (std::abs(g.col(t) - g.col(start[i])) * tw + std::abs(g.row(t) - g.row(start[i])) * th);
              for (int k = std::min(across, axis); k <= std::max(across, axis); ++k) {  // the rib's tiles
                const int q  = h ? g.tile(g.col(t), k) : g.tile(k, g.row(t));
                c           += overlap * (h ? th : tw) * (h ? load_v : load_h)[q] / std::max(1, (h ? cap_v : cap_h)[q]);
                if (cong > 0 && across != axis) {
                  c += cong * (h ? th : tw) * (h ? psi_v : psi_h)[size_t(q)];
                }
              }
              if (c < cost) {
                cost = c;
                pick = t;
              }
            }
            if (pick < 0) {
              stuck = true;
              break;
            }
            tiles[i] = pick;
            used.insert(pick);
          }
          if (stuck) {
            break;
          }
          axis = median(tiles, h);
        }
        if (stuck) {
          break;
        }
        if (cong > 0) {  // the trunk's Steiner axis: the cheapest near the median within the members' span
          int lo = INT32_MAX, hi = INT32_MIN;
          for (int t : tiles) {
            lo = std::min(lo, h ? g.row(t) : g.col(t));
            hi = std::max(hi, h ? g.row(t) : g.col(t));
          }
          int    pick = axis;
          double low  = tree(tiles, h, axis, true);
          for (int a = std::max(lo, axis - moves); a <= std::min(hi, axis + moves); ++a) {
            const double c = tree(tiles, h, a, true);
            if (c < low) {
              low  = c;
              pick = a;
            }
          }
          axis = pick;
        }
        double cost = tree(tiles, h, axis, true);
        if (cost < best) {
          best       = cost;
          best_h     = h;
          best_axis  = axis;
          best_tiles = tiles;
        }
      }
      if (stuck || best_tiles.empty()) {
        ++rep.tree_skipped_nets;
        continue;
      }
      if (by_load) {  // the tree tracks, the new boxes' in-box demand and the straight connections they make stay
                      // within tree_load of every tile's tracks
        std::map<std::pair<int, int>, int> add;
        std::vector<int>                   moved;
        std::unordered_map<int, int>       at;
        const bool                         th_ = best_h;
        int                                lo_ = INT32_MAX, hi_ = INT32_MIN;
        for (size_t i = 0; i < list.size(); ++i) {
          lo_ = std::min(lo_, th_ ? g.col(best_tiles[i]) : g.row(best_tiles[i]));
          hi_ = std::max(hi_, th_ ? g.col(best_tiles[i]) : g.row(best_tiles[i]));
          if (fixed[i] < 0) {
            moved.push_back(list[i].box);
            at[list[i].box]          = best_tiles[i];
            add[{best_tiles[i], 1}] += dem_h[list[i].box];
            add[{best_tiles[i], 0}] += dem_v[list[i].box];
          }
        }
        std::set<std::pair<int, int>> tracks;
        for (int k = lo_; k <= hi_; ++k) {
          tracks.emplace(th_ ? g.tile(k, best_axis) : g.tile(best_axis, k), th_ ? 1 : 0);
        }
        for (int t : best_tiles) {
          const int along = th_ ? g.col(t) : g.row(t), across = th_ ? g.row(t) : g.col(t);
          for (int k = std::min(across, best_axis); across != best_axis && k <= std::max(across, best_axis); ++k) {
            tracks.emplace(th_ ? g.tile(along, k) : g.tile(k, along), th_ ? 0 : 1);
          }
        }
        for (const auto& key : tracks) {
          ++add[key];
        }
        straight(
            r,
            moved,
            [&](int b) {
              const auto it = at.find(b);
              return it != at.end() ? it->second : fixed_tile_of(b);
            },
            n,
            add);
        bool over = false;
        for (const auto& [key, v] : add) {
          const auto [t, hh] = key;
          over
              = over || double((hh ? load_h : load_v)[t] + (hh ? fixed_h : fixed_v)[t] + v) > o.tree_load * (hh ? cap_h : cap_v)[t];
        }
        if (over) {
          ++rep.tree_load_skipped;
          continue;
        }
        for (const auto& [key, v] : add) {
          if (!tracks.contains(key)) {  // the tree tracks go to load below
            (key.second ? fixed_h : fixed_v)[key.first] += v;
          } else {
            (key.second ? fixed_h : fixed_v)[key.first] += v - 1;
          }
        }
      }
      // commit: fix the boxes, reserve the tracks (a net counts once per tile and direction), keep the pieces
      const bool h  = best_h;
      int        lo = INT32_MAX, hi = INT32_MIN;
      Fixed_tree ft;
      ft.net    = n;
      ft.region = r;
      for (size_t i = 0; i < list.size(); ++i) {
        const int t = best_tiles[i];
        lo          = std::min(lo, h ? g.col(t) : g.row(t));
        hi          = std::max(hi, h ? g.col(t) : g.row(t));
        if (fixed[i] < 0) {
          auto& box = boxes.boxes[list[i].box];
          box.fixed = true;
          box.tile  = t;
          taken[t]  = 1;
          fixed_here.push_back(list[i].box);
          ft.boxes.push_back(list[i].box);
        }
        if (list[i].box >= 0) {
          ft.members.push_back(list[i].box);
        }
      }
      std::set<std::pair<int, int>> uses;  // (tile, h)
      for (int k = lo; k <= hi; ++k) {
        uses.emplace(h ? g.tile(k, best_axis) : g.tile(best_axis, k), h ? 1 : 0);
      }
      if (lo < hi) {
        ft.pieces.push_back(
            {n, r, h ? g.tile(lo, best_axis) : g.tile(best_axis, lo), h ? g.tile(hi, best_axis) : g.tile(best_axis, hi)});
      }
      for (int t : best_tiles) {
        const int along = h ? g.col(t) : g.row(t), across = h ? g.row(t) : g.col(t);
        for (int k = std::min(across, best_axis); across != best_axis && k <= std::max(across, best_axis); ++k) {
          uses.emplace(h ? g.tile(along, k) : g.tile(k, along), h ? 0 : 1);  // a rib (none on the trunk)
        }
        if (across != best_axis) {
          ft.pieces.push_back({n, r, t, h ? g.tile(along, best_axis) : g.tile(best_axis, along)});
        }
      }
      for (const auto& [t, dir] : uses) {
        ++(dir ? load_h : load_v)[t];
        loaded.emplace_back(t, dir, 1);
        ft.uses.emplace_back(t, dir);
      }
      trees.push_back(std::move(ft));
      treed.emplace(n, r);
      ++nets_here;
      before += before_net;
      after  += tree(best_tiles, h, best_axis, false);
      if (by_load) {
        est = estimate(r, B);
      }
    }
    if (by_load) {
      rep.tree_clauses_after += est;
    }
    // the other boxes of the region must still match its free tiles
    std::vector<std::vector<int>> pref;
    for (int b : B) {
      if (boxes.boxes[b].fixed) {
        continue;
      }
      pref.emplace_back();
      for (int t = 0; t < int(g.size()); ++t) {
        if (g.region[t] == r && !g.blocked[t] && !taken[t] && box_fits(d, g, boxes.boxes[b], t)) {
          pref.back().push_back(t);
        }
      }
    }
    if (!pref.empty() && match_tiles(pref, g.size()).empty()) {
      for (int b : fixed_here) {
        taken[boxes.boxes[b].tile] = 0;
        boxes.boxes[b].fixed       = false;
        boxes.boxes[b].tile        = -1;
      }
      for (const auto& [t, dir, k] : loaded) {
        (dir ? load_h : load_v)[t] -= k;
      }
      for (size_t k = trees_mark; k < trees.size(); ++k) {  // only this pass's entries: the region may hold global pieces
        treed.erase({trees[k].net, trees[k].region});
      }
      trees.resize(trees_mark);
      ++rep.tree_dropped_regions;
      continue;
    }
    rep.tree_nets      += nets_here;
    rep.tree_boxes     += fixed_here.size();
    rep.tree_um_before += before;
    rep.tree_um_after  += after;
  }
  for (size_t t = 0; t < g.size(); ++t) {
    for (int h : {1, 0}) {
      const int load            = (h ? load_h : load_v)[t];
      auto&     cap             = (h ? cap_h : cap_v)[t];
      rep.tree_max_load         = std::max(rep.tree_max_load, double(load) / std::max(1, cap));
      rep.tree_tiles_over_half += 2 * load > cap;
      cap                      -= load;
    }
  }
  return trees;
}

// Demand no SAT placement can avoid: a fixed box's in-box demand on its tile plus each straight connection (the SAT's
// star from a net's first endpoint) between two fixed endpoints, on the tiles it crosses. While a tile's fixed demand
// is over its tracks, drop the least critical tree with a box among the tile's contributors (its boxes come free and
// its tracks return), else unpin the least critical contributing box.
void repair_forced(const Design& d, const Legroute_options& o, const Grid& g, Boxes& boxes, const std::vector<Net_ends>& ends,
                   const std::vector<int>& dem_h, const std::vector<int>& dem_v, std::vector<int>& cap_h, std::vector<int>& cap_v,
                   std::vector<Fixed_tree>& trees, std::set<std::pair<int, int>>& treed, Legroute_report& rep) {
  std::unordered_map<int, int> tree_of;  // box -> tree that fixed it
  for (int i = 0; i < int(trees.size()); ++i) {
    for (int b : trees[i].boxes) {
      tree_of[b] = i;
    }
  }
  const auto fixed_tile = [&](const Endpoint& e) {
    return e.box < 0 ? e.tile : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : -1;
  };
  for (;;) {
    std::map<std::pair<int, int>, int>              forced;  // (tile, h) -> tracks
    std::map<std::pair<int, int>, std::vector<int>> who;     // (tile, h) -> contributing boxes
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      if (boxes.boxes[b].fixed) {
        for (int h : {1, 0}) {
          forced[{boxes.boxes[b].tile, h}] += (h ? dem_h : dem_v)[b];
          who[{boxes.boxes[b].tile, h}].push_back(b);
        }
      }
    }
    for (size_t n = 0; n < d.nets.size(); ++n) {
      for (const auto& [r, list] : ends[n].per_region) {
        if (list.size() < 2 || treed.contains({int(n), r})) {
          continue;
        }
        const int ta = fixed_tile(list[0]);
        for (size_t k = 1; ta >= 0 && k < list.size(); ++k) {
          const int tb = fixed_tile(list[k]);
          if (tb < 0 || tb == ta || (g.row(ta) != g.row(tb) && g.col(ta) != g.col(tb))) {
            continue;
          }
          if (list[0].box >= 0 && list[k].box >= 0 && d.nets[n].pins.size() == 2
              && std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb)) == 1) {
            continue;  // neighbors: in-box metal
          }
          const int h = g.row(ta) == g.row(tb);
          for (int i = std::min(g.col(ta), g.col(tb)); i <= std::max(g.col(ta), g.col(tb)); ++i) {
            for (int j = std::min(g.row(ta), g.row(tb)); j <= std::max(g.row(ta), g.row(tb)); ++j) {
              const int t = g.tile(i, j);
              if (g.region[t] != r) {
                continue;
              }
              ++forced[{t, h}];
              for (const auto* e : {&list[0], &list[k]}) {
                if (e->box >= 0) {
                  who[{t, h}].push_back(e->box);
                }
              }
            }
          }
        }
      }
    }
    std::set<int> drop_trees, unpin;
    for (const auto& [key, v] : forced) {
      const int k = edge_limit((key.second ? cap_h : cap_v)[key.first], o);
      if (v <= k) {
        continue;
      }
      int    tree = -1, box = -1;
      double tree_crit = INFINITY, box_crit = INFINITY;
      for (int b : who[key]) {
        if (const auto it = tree_of.find(b); it != tree_of.end() && !trees[it->second].dropped) {
          const double c = d.nets[trees[it->second].net].crit;
          if (c < tree_crit) {
            tree_crit = c;
            tree      = it->second;
          }
        } else if (boxes.boxes[b].fixed && !boxes.boxes[b].placed && boxes.boxes[b].crit < box_crit) {  // a placed box stays
          box_crit = boxes.boxes[b].crit;
          box      = b;
        }
      }
      if (tree >= 0) {
        drop_trees.insert(tree);
      } else if (box >= 0) {
        unpin.insert(box);
      }
    }
    if (drop_trees.empty() && unpin.empty()) {
      return;
    }
    for (int b : unpin) {
      boxes.boxes[b].fixed = false;
      boxes.boxes[b].tile  = -1;
      ++rep.forced_crit_unpinned;
    }
    // a dropped tree frees its boxes; a tree whose member came free lost its shape and goes too
    for (bool more = true; more;) {
      more = false;
      for (int i = 0; i < int(trees.size()); ++i) {
        auto& ft = trees[i];
        if (ft.dropped
            || (!drop_trees.contains(i) && std::ranges::all_of(ft.members, [&](int b) { return boxes.boxes[b].fixed; }))) {
          continue;
        }
        ft.dropped = true;
        more       = true;
        for (int b : ft.boxes) {
          boxes.boxes[b].fixed = false;
          boxes.boxes[b].tile  = -1;
        }
        for (const auto& [t, h] : ft.uses) {
          ++(h ? cap_h : cap_v)[t];
        }
        treed.erase({ft.net, ft.region});
        ++rep.forced_trees_dropped;
      }
    }
  }
}

// An IO pre-route: one track between two fixed pin tiles of a region (H leg on ta's row then V on tb's column when
// h_first). A feedthrough net (net_a, no box) runs on it alone; a pin -> box -> pin pair (net_a at ta, net_b at tb)
// shares it end to end and the box sits on one of its tiles.
struct Io_route {
  int  region = -1, ta = -1, tb = -1, box = -1, net_a = -1, net_b = -1;
  bool h_first = true;
};
std::vector<std::pair<int, int>> io_path(const Grid& g, int ta, int tb, bool h_first) {  // (tile, h)
  std::vector<std::pair<int, int>> path;
  const int                        row = h_first ? g.row(ta) : g.row(tb), col = h_first ? g.col(tb) : g.col(ta);
  for (int i = std::min(g.col(ta), g.col(tb)); g.col(ta) != g.col(tb) && i <= std::max(g.col(ta), g.col(tb)); ++i) {
    path.emplace_back(g.tile(i, row), 1);
  }
  for (int j = std::min(g.row(ta), g.row(tb)); g.row(ta) != g.row(tb) && j <= std::max(g.row(ta), g.row(tb)); ++j) {
    path.emplace_back(g.tile(col, j), 0);
  }
  return path;
}

// Legroute_options::io_preroute: per region, the 2-endpoint nets between fixed pins (feedthroughs) and the free boxes
// whose only pin-to-box nets are one pair of 2-endpoint nets get one track each before the SAT: the track leaves the
// tile capacities, the nets leave the model (marked in routed_ahead) and such a box may only take the track's tiles
// near its placement (allowed). The orientation with more room (and, with a box, closer to the box) wins.
std::vector<Io_route> io_preroute(const Design& d, const Legroute_options& o, const Grid& g, const Boxes& boxes,
                                  const std::vector<Net_ends>& ends, std::vector<int>& cap_h, std::vector<int>& cap_v,
                                  const std::vector<int>& cap_h0, const std::vector<int>& cap_v0,
                                  std::set<std::pair<int, int>>& routed_ahead, std::unordered_map<int, std::vector<int>>& allowed,
                                  Legroute_report& rep) {
  std::vector<Io_route> routes;
  std::vector<char>     taken(g.size(), 0);
  for (const auto& box : boxes.boxes) {
    if (box.fixed) {
      taken[box.tile] = 1;
    }
  }
  // per (box, region): its 2-endpoint nets whose other endpoint is a fixed pin, (net, pin tile)
  std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> io_nets;
  std::vector<std::tuple<int, int, int, int>>                     feed;  // (net, region, ta, tb)
  for (size_t n = 0; n < d.nets.size(); ++n) {
    for (const auto& [r, list] : ends[n].per_region) {
      if (list.size() != 2 || routed_ahead.contains({int(n), r})) {
        continue;
      }
      if (list[0].box < 0 && list[1].box < 0) {
        feed.emplace_back(int(n), r, list[0].tile, list[1].tile);
      } else if ((list[0].box < 0) != (list[1].box < 0)) {
        const auto& b = list[0].box >= 0 ? list[0] : list[1];
        const auto& p = list[0].box >= 0 ? list[1] : list[0];
        io_nets[{b.box, r}].emplace_back(int(n), p.tile);
      }
    }
  }
  const int  w    = int(o.window_tiles);
  // load share, or INFINITY: a tile whose routes fixed ahead (with this one) would pass reserve_share of its tracks
  // has no room
  const auto room = [&](const std::vector<std::pair<int, int>>& path, int r) {
    double cost = 0;
    for (const auto& [t, h] : path) {
      const int c = (h ? cap_h : cap_v)[t], c0 = (h ? cap_h0 : cap_v0)[t];
      if (g.region[t] != r || c < 1 || double(c0 - c + 1) > o.reserve_share * double(c0)) {
        return double(INFINITY);
      }
      cost += 1.0 / c;
    }
    return cost;
  };
  const auto reserve = [&](const std::vector<std::pair<int, int>>& path) {
    for (const auto& [t, h] : path) {
      --(h ? cap_h : cap_v)[t];
    }
  };
  for (const auto& [n, r, ta, tb] : feed) {
    double best = INFINITY;
    bool   hf   = true;
    for (bool h : {true, false}) {
      if (const double c = room(io_path(g, ta, tb, h), r); c < best) {
        best = c;
        hf   = h;
      }
    }
    if (best == INFINITY) {
      ++rep.io_skipped;
      continue;
    }
    reserve(io_path(g, ta, tb, hf));
    routed_ahead.emplace(n, r);
    routes.push_back({r, ta, tb, -1, n, -1, hf});
    ++rep.io_feedthroughs;
  }
  for (const auto& [key, list] : io_nets) {
    const auto [b, r] = key;
    const auto& box   = boxes.boxes[b];
    if (list.size() != 2 || box.fixed) {
      continue;
    }
    const int        h0 = g.tile_of(box.cx, box.cy);
    const int        ta = list[0].second, tb = list[1].second;
    double           best = INFINITY;
    bool             hf   = true;
    std::vector<int> best_tiles;
    for (bool h : {true, false}) {
      const auto path = io_path(g, ta, tb, h);
      double     cost = room(path, r);
      if (cost == INFINITY) {
        continue;
      }
      std::vector<int> tiles;  // the track's free fitting tiles near the box's placement
      std::set<int>    seen;
      for (int t : {ta, tb}) {
        seen.insert(t);
      }
      std::vector<int> cand(seen.begin(), seen.end());
      for (const auto& [t, _] : path) {
        if (seen.insert(t).second) {
          cand.push_back(t);
        }
      }
      int near = INT32_MAX;
      for (int t : cand) {
        const int dist = std::max(std::abs(g.col(t) - g.col(h0)), std::abs(g.row(t) - g.row(h0)));
        if (dist <= 2 * w + 1 && !g.blocked[t] && !taken[t] && box_fits(d, g, box, t)) {
          tiles.push_back(t);
          near = std::min(near, dist);
        }
      }
      if (tiles.empty()) {
        continue;
      }
      cost += near;
      if (cost < best) {
        best       = cost;
        hf         = h;
        best_tiles = std::move(tiles);
      }
    }
    if (best_tiles.empty()) {
      ++rep.io_skipped;
      continue;
    }
    reserve(io_path(g, ta, tb, hf));
    routed_ahead.emplace(list[0].first, r);
    routed_ahead.emplace(list[1].first, r);
    allowed[b] = std::move(best_tiles);
    routes.push_back({r, ta, tb, b, list[0].first, list[1].first, hf});
    ++rep.io_boxes;
  }
  return routes;
}
// legroute.global: the run of a critical net through a region holding none of its pins, fixed ahead (an L from a to
// b, H first or V first) and reserved on the edges it crosses
struct Global_piece {
  int  net = -1, region = -1, a = -1, b = -1;
  bool h_first   = true;
  int  part      = -1;     // a retained component, or -1 for the legacy whole-region policy
  bool backbone  = false;  // coarse witness metadata
  bool refinable = false;  // seed geometry consumed by the corridor model, never fixed demand or appended after solving
};
// The tracks the routes fixed ahead (hops, fixed trees, IO pre-routes) take on each tile edge, one per
// net and edge it crosses (keyed by the left / lower tile), instead of a whole tile per route piece: a hop takes its
// boundary edge only, a trunk the edges along it
struct Edge_reserve {
  std::vector<int>                                   h, v;
  std::set<std::tuple<int, int, int>>                edges;  // (net, edge tile, h), including fixed/local sharing
  std::map<std::tuple<int, int, int>, std::set<int>> cuts;   // (net, region, h) -> boundaries already crossed

  const std::set<int>* cuts_of(int net, int region, int horizontal) const {
    const auto it = cuts.find({net, region, horizontal});
    return it == cuts.end() ? nullptr : &it->second;
  }
};

// A published boundary edge above the admitted capacity (the structural checks throw runtime_error/logic_error).
struct Crossing_capacity_error : std::runtime_error {
  using std::runtime_error::runtime_error;
};
void validate_crossing_edges(const Grid& g, const std::vector<std::pair<int, int>>& hops, const std::vector<int>& hop_net,
                             const std::vector<int>& cap_h, const std::vector<int>& cap_v, double share, double cap_scale = 1,
                             double ceiling = .90) {
  if (hops.size() != hop_net.size() || cap_h.size() != g.size() || cap_v.size() != g.size()) {
    throw std::logic_error("legroute shared interface check has inconsistent arrays");
  }
  std::map<std::pair<int, int>, std::set<int>> use;
  for (size_t i = 0; i < hops.size(); ++i) {
    const auto [a, b] = hops[i];
    if (a < 0 || b < 0 || size_t(a) >= g.size() || size_t(b) >= g.size()
        || std::abs(g.col(a) - g.col(b)) + std::abs(g.row(a) - g.row(b)) != 1 || g.region[a] == g.region[b]) {
      throw std::runtime_error("legroute shared interface repair required: crossing is not an adjacent two-region edge");
    }
    use[{std::min(a, b), int(g.row(a) == g.row(b))}].insert(hop_net[i]);
  }
  for (const auto& [edge, nets] : use) {
    const auto [t, h] = edge;
    const int   q     = t + (h ? 1 : g.cols);
    const auto& cap   = h ? cap_h : cap_v;
    const int   room  = std::min(cap[t], cap[q]);
    const int   limit = std::max(0, int(std::floor(room * std::min({cap_scale, ceiling, share}))));
    if (int(nets.size()) > limit) {
      throw Crossing_capacity_error(std::format(
          "legroute shared interface repair required: {} boundary edge {} -> {} uses {} distinct nets, admitted capacity {}",
          h ? 'H' : 'V',
          t,
          q,
          nets.size(),
          limit));
    }
  }
}

Edge_reserve edge_reserve(const Grid& g, const std::vector<std::pair<int, int>>& hops, const std::vector<int>& hop_net,
                          const std::vector<Fixed_tree>& trees, const std::vector<Io_route>& io_routes,
                          const std::vector<Global_piece>& global) {
  Edge_reserve res;
  res.h.assign(g.size(), 0);
  res.v.assign(g.size(), 0);
  const auto take = [&](int n, int t, int h) {
    if (res.edges.emplace(n, t, h).second) {
      ++(h ? res.h : res.v)[size_t(t)];
      const int q = h ? t + 1 : t + g.cols;
      if (g.region[size_t(t)] == g.region[size_t(q)]) {
        res.cuts[{n, g.region[size_t(t)], h}].insert((h ? g.col(t) : g.row(t)) + 1);
      }
    }
  };
  const auto segment = [&](int n, int ta, int tb, bool h_first) {  // an L from ta to tb: the edges its legs cross
    const int hr = h_first ? g.row(ta) : g.row(tb), vc = h_first ? g.col(tb) : g.col(ta);
    for (int i = std::min(g.col(ta), g.col(tb)); i < std::max(g.col(ta), g.col(tb)); ++i) {
      take(n, g.tile(i, hr), 1);
    }
    for (int j = std::min(g.row(ta), g.row(tb)); j < std::max(g.row(ta), g.row(tb)); ++j) {
      take(n, g.tile(vc, j), 0);
    }
  };
  for (size_t k = 0; k < hops.size(); ++k) {
    const auto [a, b] = hops[k];
    take(hop_net[k], std::min(a, b), g.row(a) == g.row(b) ? 1 : 0);
  }
  for (const auto& ft : trees) {
    for (const auto& piece : ft.pieces) {
      if (!ft.dropped) {
        segment(piece.net, piece.a, piece.b, true);
      }
    }
  }
  for (const auto& io : io_routes) {
    if (io.box < 0) {
      segment(io.net_a, io.ta, io.tb, io.h_first);
    }  // a pin -> box -> pin route: its box moves on the track, the legs are the region's connections
  }
  for (const auto& gp : global) {  // legroute.global: the pass-through runs
    if (!gp.refinable) {
      segment(gp.net, gp.a, gp.b, gp.h_first);
    }
  }
  return res;
}

// One region's SAT placement under one policy's options and pre-placement (boxes, edge reservations, nets routed ahead,
// boxes held to a track): the boxes' tiles land in boxes, the connections, adjacent nets and counters in out.
struct Region_out {
  using Piece                                    = Route_piece;
  Sat_status                             status  = Sat_status::unknown;
  bool                                   skipped = false;
  std::string                            unfit;            // spill rounds: the region had more boxes than fitting tiles (no model)
  size_t                                 unfit_boxes = 0;  // how many found no tile
  std::vector<Piece>                     conns;            // the SAT's connections, then the policy's trees and IO pre-routes here
  std::vector<int>                       local;            // 1-1 nets between neighbor boxes
  std::vector<std::pair<int, int>>       tiles;            // (box, tile)
  std::vector<std::tuple<int, int, int>> hop_tiles;        // (net, the hop chooser's tile, the tile chosen) per free hop
  std::map<int, int>                     hop_choices;      // stable hop id -> selected tile (coincident interfaces must not alias)
  int                                    cut_at = -1;      // cut bound: the column (cut_h) or row boundary the placement overloads
  bool                                   cut_h  = true;
  Legroute_report                        rep;  // the sat_* counters
  double                                 cost = 0, seconds = 0;
  bool                                   capture_seed_domains = false;
  std::map<int, std::vector<int>>        seed_box_domains;  // exact placement domains, only for a private seed proposal
};

bool contains_global_piece(const Region_out& out, const Global_piece& piece) {
  return std::ranges::any_of(out.conns, [&](const auto& candidate) {
    return candidate.net == piece.net && candidate.ta == piece.a && candidate.tb == piece.b && candidate.h_first == piece.h_first;
  });
}

void append_global_pieces(Region_out& out, int region, const std::vector<Global_piece>& pieces) {
  for (const auto& piece : pieces) {
    if (!piece.refinable && piece.region == region && !contains_global_piece(out, piece)) {
      out.conns.push_back({piece.net, piece.a, piece.b, piece.h_first});
    }
  }
}

void adopt_region_alternative(Region_out& target, Region_out&& candidate, int region, const std::vector<Global_piece>& pieces) {
  // An alternative solve emits its variable connections only. The immutable
  // global geometry is still reserved and must accompany the adopted answer.
  append_global_pieces(candidate, region, pieces);
  target = std::move(candidate);
}

// Acceptance check over the geometry that will actually leave the regional solver. Do not use the model's
// cover literals or lazy decoder here: a wrong endpoint/shape decode must not certify its own capacity usage.
// Separate components of one net can connect outside this region, so check each piece's mesh path rather than
// requiring the union of that net's regional pieces to be connected.
void validate_region_edges(const Grid& g, int r, const std::vector<Region_out::Piece>& pieces, const Edge_reserve& reserve,
                           const std::vector<int>& cap_h0, const std::vector<int>& cap_v0, const Legroute_options& o,
                           Region_budget* budget = nullptr) {
  const auto check_budget = [&] {
    if (budget) {
      budget->check();
    }
  };
  if (budget) {
    budget->check(true);
  }
  if (cap_h0.size() != g.size() || cap_v0.size() != g.size() || reserve.h.size() != g.size() || reserve.v.size() != g.size()
      || (o.untracked && o.untracked->size() != g.size())) {
    throw std::logic_error("legroute: decoded regional capacity check has inconsistent grid arrays");
  }
  std::vector<int>                    use_h = reserve.h, use_v = reserve.v;
  std::set<std::tuple<int, int, int>> local_edges;
  const auto                          in_region = [&](int t) { return t >= 0 && size_t(t) < g.size() && g.region[size_t(t)] == r; };
  const auto                          take      = [&](int net, int t, bool h) {
    check_budget();
    const int q = h ? (g.col(t) + 1 < g.cols ? t + 1 : -1) : (g.row(t) + 1 < g.rows ? t + g.cols : -1);
    if (!in_region(t) || !in_region(q)) {
      throw std::runtime_error(
          std::format("legroute: decoded regional model error: net {} region {} {} edge at tile {} leaves mesh",
                      net,
                      r,
                      h ? 'H' : 'V',
                      t));
    }
    const auto key = std::tuple{net, t, int(h)};
    if (local_edges.insert(key).second && !reserve.edges.contains(key)) {
      ++(h ? use_h : use_v)[size_t(t)];
    }
  };
  for (const auto& pc : pieces) {
    check_budget();
    if (pc.net < 0 || !in_region(pc.ta) || !in_region(pc.tb)) {
      throw std::runtime_error(
          std::format("legroute: decoded regional model error: net {} region {} has invalid endpoints {} -> {}",
                      pc.net,
                      r,
                      pc.ta,
                      pc.tb));
    }
    const int hr = pc.h_first ? g.row(pc.ta) : g.row(pc.tb), vc = pc.h_first ? g.col(pc.tb) : g.col(pc.ta);
    for (int col = std::min(g.col(pc.ta), g.col(pc.tb)); col < std::max(g.col(pc.ta), g.col(pc.tb)); ++col) {
      take(pc.net, g.tile(col, hr), true);
    }
    for (int row = std::min(g.row(pc.ta), g.row(pc.tb)); row < std::max(g.row(pc.ta), g.row(pc.tb)); ++row) {
      take(pc.net, g.tile(vc, row), false);
    }
  }
  // Include reserved-only demand too. Boundary edges are checked by the global crossing model, not this region.
  for (size_t t = 0; t < g.size(); ++t) {
    check_budget();
    if (g.region[t] != r) {
      continue;
    }
    for (bool h : {true, false}) {
      const int q = h ? (g.col(int(t)) + 1 < g.cols ? int(t) + 1 : -1) : (g.row(int(t)) + 1 < g.rows ? int(t) + g.cols : -1);
      if (!in_region(q) || (o.untracked && (*o.untracked)[t])) {
        continue;  // An untracked checker proposal still needs a subsequent full-capacity solve.
      }
      const auto& cap   = h ? cap_h0 : cap_v0;
      const int   limit = std::max(0, edge_limit(std::min(cap[t], cap[size_t(q)]), o));
      const int   use   = (h ? use_h : use_v)[t];
      if (use > limit) {
        throw std::runtime_error(std::format(
            "legroute: decoded regional capacity model error: region {} {} edge ({},{}) -> ({},{}) uses {} tracks, capacity {}",
            r,
            h ? 'H' : 'V',
            g.col(int(t)),
            g.row(int(t)),
            g.col(q),
            g.row(q),
            use,
            limit));
      }
    }
  }
  if (budget) {
    budget->check(true);
  }
}

void solve_region_model(const Design& d, const Legroute_options& o, const Grid& g, int r, const std::vector<int>& B,
                        const std::vector<int>& T, Boxes& boxes, const std::vector<Net_ends>& ends,
                        const std::set<std::pair<int, int>>& treed, const std::unordered_map<int, std::vector<int>>& allowed,
                        const std::vector<char>& pinned_tile, const std::string& tag, const std::vector<int>& cap_h0,
                        const std::vector<int>& cap_v0, const Edge_reserve& reserve, const std::vector<std::vector<int>>& hop_tiles,
                        const std::vector<int>& hop_room, Region_out& out, Region_budget& budget, Cnf& cnf) {
  auto&      rep   = out.rep;
  const auto start = budget.start;
  if (T.empty()) {
    return;
  }
  int c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : T) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  // Fixed demand inside this region is still a hard capacity constraint, even when there are no variable routes.
  // Boundary edges belong to the global crossing check, as with the variable-route capacity rows below.
  // Clamping negative residual capacity to zero would otherwise certify an already-overflowed fixed route.
  for (int t : T) {
    for (bool h : {true, false}) {
      const int q = h ? (g.col(t) + 1 < g.cols ? t + 1 : -1) : (g.row(t) + 1 < g.rows ? t + g.cols : -1);
      if (q < 0 || g.region[size_t(q)] != r || (o.untracked && (*o.untracked)[size_t(t)])) {
        continue;
      }
      const int tracks = std::max(0, edge_limit(std::min((h ? cap_h0 : cap_v0)[t], (h ? cap_h0 : cap_v0)[q]), o));
      if ((h ? reserve.h : reserve.v)[size_t(t)] > tracks) {
        cnf.add({});
        ++rep.sat_fixed_overflow;
      }
    }
  }
  // order literals of a box: col >= k for k in (c0, c1], row >= k for k in (r0, r1]
  std::unordered_map<int, int> slot;  // box -> index
  for (int i = 0; i < int(B.size()); ++i) {
    slot[B[i]] = i;
  }
  std::vector<std::vector<int>> X(B.size()), Y(B.size());
  for (size_t i = 0; i < B.size(); ++i) {
    for (int k = c0 + 1; k <= c1; ++k) {
      X[i].push_back(cnf.var());
      if (k > c0 + 1) {
        cnf.add({-X[i].back(), X[i][X[i].size() - 2]});
      }
    }
    for (int k = r0 + 1; k <= r1; ++k) {
      Y[i].push_back(cnf.var());
      if (k > r0 + 1) {
        cnf.add({-Y[i].back(), Y[i][Y[i].size() - 2]});
      }
    }
  }
  // free hops (hop_choice): a crossing net's hop this region chooses among its candidate boundary tiles: order
  // literals over the candidates' hull and a tile literal per candidate (no tile is taken by it), and at most
  // hop_room hops on one boundary tile
  struct Free_hop {
    int                              id = -1, net = -1, def = -1, c0 = 0, c1 = 0, r0 = 0, r1 = 0;
    std::vector<int>                 X, Y;
    std::vector<std::pair<int, int>> tiles;  // (tile, literal)
  };
  std::vector<Free_hop>        hops_free;
  std::vector<Free_hop>        anchors_free;  // finite-point choices without interface capacity/ownership
  std::unordered_map<int, int> hslot;         // hop id -> index in hops_free
  {
    std::map<int, std::vector<int>> hops_at;  // boundary tile -> the free hops' literals there
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto it = ends[n].per_region.find(r);
      if (it == ends[n].per_region.end() || (it->second.size() < 2 && !ends[n].preserve_coarse) || treed.contains({int(n), r})) {
        continue;
      }
      for (const auto& e : it->second) {
        if (e.box >= 0 || e.hop < 0 || size_t(e.hop) >= hop_tiles.size() || hop_tiles[size_t(e.hop)].size() < 2
            || hslot.contains(e.hop)) {
          continue;
        }
        Free_hop fh;
        fh.id  = e.hop;
        fh.net = int(n);
        fh.def = e.tile;
        fh.c0  = g.cols;
        fh.c1  = -1;
        fh.r0  = g.rows;
        fh.r1  = -1;
        for (int t : hop_tiles[size_t(e.hop)]) {
          fh.c0 = std::min(fh.c0, g.col(t));
          fh.c1 = std::max(fh.c1, g.col(t));
          fh.r0 = std::min(fh.r0, g.row(t));
          fh.r1 = std::max(fh.r1, g.row(t));
        }
        for (int k = fh.c0 + 1; k <= fh.c1; ++k) {
          fh.X.push_back(cnf.var());
          if (k > fh.c0 + 1) {
            cnf.add({-fh.X.back(), fh.X[fh.X.size() - 2]});
          }
        }
        for (int k = fh.r0 + 1; k <= fh.r1; ++k) {
          fh.Y.push_back(cnf.var());
          if (k > fh.r0 + 1) {
            cnf.add({-fh.Y.back(), fh.Y[fh.Y.size() - 2]});
          }
        }
        const auto       cge = [&](int k) { return k <= fh.c0 ? cnf.t : k > fh.c1 ? -cnf.t : fh.X[size_t(k - fh.c0 - 1)]; };
        const auto       rge = [&](int k) { return k <= fh.r0 ? cnf.t : k > fh.r1 ? -cnf.t : fh.Y[size_t(k - fh.r0 - 1)]; };
        std::vector<int> any;
        for (int t : hop_tiles[size_t(e.hop)]) {
          const int p = cnf.var();
          cnf.add({-p, cge(g.col(t))});
          cnf.add({-p, -cge(g.col(t) + 1)});
          cnf.add({-p, rge(g.row(t))});
          cnf.add({-p, -rge(g.row(t) + 1)});
          fh.tiles.emplace_back(t, p);
          any.push_back(p);
          hops_at[t].push_back(p);
        }
        cnf.add(any);
        hslot[e.hop] = int(hops_free.size());
        hops_free.push_back(std::move(fh));
      }
    }
    for (const auto& [t, lits] : hops_at) {
      const int room = size_t(t) < hop_room.size() ? hop_room[size_t(t)] : 0;
      if (int(lits.size()) > room) {
        cnf.at_most(lits, std::max(0, room));
      }
    }
  }
  const auto free_hop = [&](const Endpoint& e) -> const Free_hop* {
    if (e.anchor_choice >= 0) {
      return &anchors_free.at(size_t(e.anchor_choice));
    }
    if (e.box >= 0 || e.hop < 0) {
      return nullptr;
    }
    const auto hs = hslot.find(e.hop);
    return hs == hslot.end() ? nullptr : &hops_free[size_t(hs->second)];
  };
  const auto add_anchor_choice = [&](const std::vector<int>& candidates) {
    Free_hop point;
    point.c0 = g.cols;
    point.c1 = -1;
    point.r0 = g.rows;
    point.r1 = -1;
    for (int t : candidates) {
      point.c0 = std::min(point.c0, g.col(t));
      point.c1 = std::max(point.c1, g.col(t));
      point.r0 = std::min(point.r0, g.row(t));
      point.r1 = std::max(point.r1, g.row(t));
    }
    for (int k = point.c0 + 1; k <= point.c1; ++k) {
      point.X.push_back(cnf.var());
      if (point.X.size() > 1) {
        cnf.add({-point.X.back(), point.X[point.X.size() - 2]});
      }
    }
    for (int k = point.r0 + 1; k <= point.r1; ++k) {
      point.Y.push_back(cnf.var());
      if (point.Y.size() > 1) {
        cnf.add({-point.Y.back(), point.Y[point.Y.size() - 2]});
      }
    }
    const auto       cge = [&](int k) { return k <= point.c0 ? cnf.t : k > point.c1 ? -cnf.t : point.X[size_t(k - point.c0 - 1)]; };
    const auto       rge = [&](int k) { return k <= point.r0 ? cnf.t : k > point.r1 ? -cnf.t : point.Y[size_t(k - point.r0 - 1)]; };
    std::vector<int> any;
    for (int t : candidates) {
      const int lit = cnf.var();
      cnf.add({-lit, cge(g.col(t))});
      cnf.add({-lit, -cge(g.col(t) + 1)});
      cnf.add({-lit, rge(g.row(t))});
      cnf.add({-lit, -rge(g.row(t) + 1)});
      point.tiles.emplace_back(t, lit);
      any.push_back(lit);
    }
    cnf.add(any);
    const int id = int(anchors_free.size());
    anchors_free.push_back(std::move(point));
    return id;
  };
  const auto col_ge = [&](const Endpoint& e, int k) {
    if (e.box < 0) {
      if (const auto* fh = free_hop(e)) {
        return k <= fh->c0 ? cnf.t : k > fh->c1 ? -cnf.t : fh->X[size_t(k - fh->c0 - 1)];
      }
      return g.col(e.tile) >= k ? cnf.t : -cnf.t;
    }
    const int i = slot.at(e.box);
    return k <= c0 ? cnf.t : k > c1 ? -cnf.t : X[size_t(i)][size_t(k - c0 - 1)];
  };
  const auto row_ge = [&](const Endpoint& e, int k) {
    if (e.box < 0) {
      if (const auto* fh = free_hop(e)) {
        return k <= fh->r0 ? cnf.t : k > fh->r1 ? -cnf.t : fh->Y[size_t(k - fh->r0 - 1)];
      }
      return g.row(e.tile) >= k ? cnf.t : -cnf.t;
    }
    const int i = slot.at(e.box);
    return k <= r0 ? cnf.t : k > r1 ? -cnf.t : Y[size_t(i)][size_t(k - r0 - 1)];
  };
  // Shared native preparation also supplies the global witness proposal; both
  // consumers keep the same original-center windows and adaptive matching.
  auto prepared            = prepare_box_domains(d, o, g, r, B, T, boxes, allowed, pinned_tile, [&] { budget.check(true); });
  auto win                 = std::move(prepared.windows);
  auto lifted              = std::move(prepared.lifted);
  rep.io_lifted           += lifted.size();
  rep.sat_relaxed_windows += prepared.relaxed_windows;
  if (!prepared.matching.empty()) {
    budget.fallback = std::move(prepared.matching);
  }
  using Win           = Box_window;
  const auto on_track = [&](int b, int t) {
    const auto it = allowed.find(b);
    return it == allowed.end() || lifted.contains(b) || std::ranges::find(it->second, t) != it->second.end();
  };
  const auto window_tiles = [&](size_t i) {
    return box_domain_tiles(d, g, B[i], win[i], boxes, allowed, lifted, pinned_tile, T, [&] { budget.check(true); });
  };
  // the fallback placement: boxes on fitting tiles of their windows, nearest first, by a bipartite matching
  // (augmenting paths, so a box with few fitting tiles still gets one); a box whose window is full takes any fitting
  // tile of the region. Used for a region without a routable answer, and by freeze for every region.
  const auto fallback_tiles = [&](const std::function<std::vector<int>(size_t)>& candidates) {
    budget.check(true);
    std::unordered_map<int, int>  owner;  // tile -> box index
    std::vector<std::vector<int>> pref(B.size());
    const auto                    nearest = [&](size_t i) {
      const auto& box = boxes.boxes[B[i]];
      std::ranges::sort(pref[i], [&](int x, int y) {
        return std::abs(double(g.cx(g.col(x))) - box.cx) + std::abs(double(g.cy(g.row(x))) - box.cy)
               < std::abs(double(g.cx(g.col(y))) - box.cx) + std::abs(double(g.cy(g.row(y))) - box.cy);
      });
    };
    for (size_t i = 0; i < B.size(); ++i) {
      pref[i] = candidates(i);
      nearest(i);
    }
    std::vector<char>        seen;
    std::function<bool(int)> augment = [&](int i) {
      budget.check();
      for (int t : pref[size_t(i)]) {
        if (seen[size_t(t)]) {
          continue;
        }
        seen[size_t(t)] = 1;
        const auto it   = owner.find(t);
        if (it == owner.end() || augment(it->second)) {
          owner[t] = i;
          return true;
        }
      }
      return false;
    };
    const auto any_tile = [&](size_t i) {  // any tile of the region the box fits, nearest first
      const auto& box = boxes.boxes[B[i]];
      pref[i].clear();
      for (int t : T) {
        if ((!box.fixed || t == box.tile) && !g.blocked[size_t(t)] && (box.fixed || !pinned_tile[size_t(t)])
            && box_fits(d, g, box, t)) {
          pref[i].push_back(t);
        }
      }
      nearest(i);
    };
    bool full = false;  // the windows crowd the tiles: every movable box may then take any tile it fits, once
    for (size_t i = 0; i < B.size(); ++i) {
      seen.assign(g.size(), 0);
      if (augment(int(i))) {
        continue;
      }
      any_tile(i);  // its window is full
      seen.assign(g.size(), 0);
      if (augment(int(i))) {
        continue;
      }
      if (!full) {
        full = true;
        for (size_t j = 0; j < B.size(); ++j) {
          if (!boxes.boxes[B[j]].fixed) {
            any_tile(j);
          }
        }
        owner.clear();
        i = size_t(-1);  // the matching starts over with the wide lists
        continue;
      }
      ++out.unfit_boxes;  // counted on, so a spill round knows how many must leave
    }
    if (out.unfit_boxes > 0) {
      throw Legroute_unfit(
          std::format("legroute: region {} has more boxes than fitting tiles ({} boxes without a tile)", r, out.unfit_boxes));
    }
    std::vector<int> tile(B.size(), -1);
    for (const auto& [t, i] : owner) {
      tile[size_t(i)] = t;
    }
    budget.fallback = tile;
    return tile;
  };
  if (o.freeze) {  // every box on the tile the fallback gives it (the placement a run without an answer keeps and an
    // external router can be given): the SAT decides the route shapes only
    const auto tiles = fallback_tiles(window_tiles);
    for (size_t i = 0; i < B.size(); ++i) {
      if (tiles[i] >= 0) {
        win[i] = {g.col(tiles[i]), g.col(tiles[i]), g.row(tiles[i]), g.row(tiles[i])};
      }
    }
    rep.frozen_boxes += B.size();
  }
  if (out.skipped) {  // fail_fast skipped this region: its boxes take fitting tiles (the fallback), nothing is solved
    const auto tiles = fallback_tiles(window_tiles);
    for (size_t i = 0; i < B.size(); ++i) {
      if (tiles[i] >= 0) {
        boxes.boxes[B[i]].tile = tiles[i];
      }
    }
    for (int b : B) {
      out.tiles.emplace_back(b, boxes.boxes[b].tile);
    }
    rep.sat_regions += 1;
    ++rep.sat_skipped_regions;
    out.status = Sat_status::unknown;
    return;
  }
  for (size_t i = 0; i < B.size(); ++i) {
    const Endpoint e{B[i], -1};
    cnf.add({col_ge(e, win[i].c0)});
    cnf.add({-col_ge(e, win[i].c1 + 1)});
    cnf.add({row_ge(e, win[i].r0)});
    cnf.add({-row_ge(e, win[i].r1 + 1)});
  }
  const auto win_of = [&](const Endpoint& e) {
    if (e.box >= 0) {
      return win[slot.at(e.box)];
    }
    if (const auto* fh = free_hop(e)) {
      return Win{fh->c0, fh->c1, fh->r0, fh->r1};
    }
    return Win{g.col(e.tile), g.col(e.tile), g.row(e.tile), g.row(e.tile)};
  };
  const std::set<int> here(T.begin(), T.end());
  // the cut bound: a net with an endpoint held on each side of a tile-column (row) boundary covers an edge across it,
  // so the nets forced across a boundary are at most the tracks of its edges. A region over the bound is unsat before
  // any solving, and the boundary is the diagnosis. A 2-pin net between boxes that may sit side by side across the
  // boundary takes no track (in-box metal) and does not count. A relaxed model (untracked tiles) has no bound.
  if (!o.sat_relax_rounds && !o.untracked) {
    std::vector<int> fc(size_t(c1 - c0 + 2), 0), fr(size_t(r1 - r0 + 2), 0);  // forced nets per boundary, as differences
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto it = ends[n].per_region.find(r);
      if (it == ends[n].per_region.end() || it->second.size() < 2 || treed.contains({int(n), r})) {
        continue;
      }
      std::vector<std::pair<int, int>> cols, rows;
      for (const auto& [part, group] : endpoint_groups(it->second)) {
        if (group.size() < 2) {
          continue;
        }
        int lo_c = g.cols, hi_c = -1, lo_r = g.rows, hi_r = -1;  // leftmost endpoint's last column, rightmost's first
        for (const auto& e : group) {
          const auto wn = win_of(e);
          lo_c          = std::min(lo_c, std::clamp(wn.c1, c0, c1));
          hi_c          = std::max(hi_c, std::clamp(wn.c0, c0, c1));
          lo_r          = std::min(lo_r, std::clamp(wn.r1, r0, r1));
          hi_r          = std::max(hi_r, std::clamp(wn.r0, r0, r1));
        }
        if (group.size() == 2 && d.nets[n].pins.size() == 2 && group[0].box >= 0 && group[1].box >= 0) {
          const auto wa = win_of(group[0]), wb = win_of(group[1]);
          if (hi_c == lo_c + 1 && wa.r0 <= wb.r1 && wb.r0 <= wa.r1) {
            hi_c = lo_c;  // side by side across its one forced column boundary
          }
          if (hi_r == lo_r + 1 && wa.c0 <= wb.c1 && wb.c0 <= wa.c1) {
            hi_r = lo_r;
          }
        }
        cols.emplace_back(lo_c, hi_c);
        rows.emplace_back(lo_r, hi_r);
      }
      add_forced_cuts(cols, fc, c0, reserve.cuts_of(int(n), r, 1));
      add_forced_cuts(rows, fr, r0, reserve.cuts_of(int(n), r, 0));
    }
    struct Cut {
      int  at = -1, forced = 0, tracks = 0;
      bool h = true;  // a column boundary (H edges cross it) or a row boundary (V edges)
    } worst;
    for (int c = c0 + 1, run = 0; c <= c1; ++c) {
      run        += fc[size_t(c - c0)];
      int tracks  = 0;
      for (int j = r0; j <= r1; ++j) {
        const int t = g.tile(c - 1, j), q = g.tile(c, j);
        if (here.contains(t) && here.contains(q)) {
          tracks += std::max(0, edge_limit(std::min(cap_h0[t], cap_h0[q]), o) - reserve.h[size_t(t)]);
        }
      }
      if (run - tracks > worst.forced - worst.tracks) {
        worst = {c, run, tracks, true};
      }
    }
    for (int j = r0 + 1, run = 0; j <= r1; ++j) {
      run        += fr[size_t(j - r0)];
      int tracks  = 0;
      for (int i = c0; i <= c1; ++i) {
        const int t = g.tile(i, j - 1), q = g.tile(i, j);
        if (here.contains(t) && here.contains(q)) {
          tracks += std::max(0, edge_limit(std::min(cap_v0[t], cap_v0[q]), o) - reserve.v[size_t(t)]);
        }
      }
      if (run - tracks > worst.forced - worst.tracks) {
        worst = {j, run, tracks, false};
      }
    }
    if (worst.at >= 0) {
      ++rep.sat_regions;
      ++rep.sat_unsat_regions;
      ++rep.sat_cut_regions;
      const double secs   = seconds_since(start);
      rep.sat_seconds_max = std::max(rep.sat_seconds_max, secs);
      std::fprintf(stderr,
                   "legroute sat region %d: boxes %zu tiles %zu status unsat by the cut bound: %d nets forced across %s %d, "
                   "%d tracks, %.2f s%s\n",
                   r,
                   B.size(),
                   T.size(),
                   worst.forced,
                   worst.h ? "column boundary" : "row boundary",
                   worst.at,
                   worst.tracks,
                   secs,
                   tag.c_str());
      out.status       = Sat_status::unsatisfiable;
      out.cut_at       = worst.at;
      out.cut_h        = worst.h;
      const auto tiles = fallback_tiles(window_tiles);  // the region keeps its boxes on fitting tiles (not routable)
      for (size_t i = 0; i < B.size(); ++i) {
        if (tiles[i] >= 0) {
          boxes.boxes[B[i]].tile = tiles[i];
        }
      }
      for (int b : B) {
        out.tiles.emplace_back(b, boxes.boxes[b].tile);
      }
      return;
    }
  }
  // tile literals: a box on a free tile it fits; one tile per box (the order literals pin it), one box per tile
  std::unordered_map<int, std::vector<std::pair<int, int>>> at_tile;  // tile -> (box index, literal)
  std::vector<std::vector<std::pair<int, int>>>             tiles_of(B.size());
  for (size_t i = 0; i < B.size(); ++i) {
    const auto&      box = boxes.boxes[B[i]];
    std::vector<int> any;
    for (int t : T) {
      if (g.col(t) < win[i].c0 || g.col(t) > win[i].c1 || g.row(t) < win[i].r0 || g.row(t) > win[i].r1 || g.blocked[t]
          || (pinned_tile[t] && !box.fixed) || !box_fits(d, g, box, t) || !on_track(B[i], t)) {
        continue;
      }
      const int      p = cnf.var();
      const Endpoint e{B[i], -1};
      cnf.add({-p, col_ge(e, g.col(t))});
      cnf.add({-p, -col_ge(e, g.col(t) + 1)});
      cnf.add({-p, row_ge(e, g.row(t))});
      cnf.add({-p, -row_ge(e, g.row(t) + 1)});
      at_tile[t].emplace_back(int(i), p);
      tiles_of[i].emplace_back(t, p);
      any.push_back(p);
    }
    if (any.empty()) {
      throw Legroute_unfit(std::format("legroute: box {} fits no tile of region {}", B[i], r));
    }
    cnf.add(any);
    if (out.capture_seed_domains) {
      auto& domain = out.seed_box_domains[B[i]];
      for (const auto& [tile, literal] : tiles_of[i]) {
        (void)literal;
        domain.push_back(tile);
      }
    }
  }
  for (auto& [t, list] : at_tile) {
    std::vector<int> xs;
    for (const auto& [_, p] : list) {
      xs.push_back(p);
    }
    cnf.at_most(xs, 1);
  }
  // Connect each component of a net's regional tree independently: separate
  // components are already joined outside this region. Each component uses a
  // star or a single-trunk Steiner tree, with its interior chosen by SAT.
  struct Steiner {
    int              net = -1, first = -1, count = 0;
    int              h_trunk = 0;     // true: an H trunk on a row (rib V), false: a V trunk on a column (ribs H)
    std::vector<int> row_ge, col_ge;  // the trunk's row / column, order encoded over the endpoints' window hull
    int              x0 = 0, x1 = 0, y0 = 0, y1 = 0;
  };
  std::vector<Endpoint>                        eps;
  std::vector<Region_conn>                     conns;
  std::vector<Steiner>                         steiner;
  std::vector<int>                             nets_here;
  std::map<std::pair<int, int>, std::set<int>> corridors;  // (net, part), clipped to this region
  std::vector<Corridor_model>                  corridor_models;
  std::map<std::tuple<int, int, int>, int>     corridor_edges;  // one physical net-edge charge across every part
  std::set<int>                                corridor_seed_literals;
  size_t                                       unsupported_stub_homes = 0;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto it = ends[n].per_region.find(r);
    if (it == ends[n].per_region.end() || (it->second.size() < 2 && !ends[n].preserve_coarse) || treed.contains({int(n), r})) {
      continue;
    }
    for (const auto& [part, group] : endpoint_groups(it->second)) {
      if (ends[n].preserve_coarse) {
        const auto forest = ends[n].coarse_parts.find({r, part});
        if (part < 0 || forest == ends[n].coarse_parts.end() || forest->second.empty()) {
          throw std::runtime_error(
              std::format("legroute coarse witness repair required: net {} region {} part {} has no represented backbone",
                          n,
                          r,
                          part));
        }
        corridors[{int(n), part}] = coarse_corridor(g, r, forest->second, o.window_tiles);
        if (ends[n].refine_corridor) {
          Corridor_model model;
          model.net      = int(n);
          model.part     = part;
          model.tiles    = corridors.at({int(n), part});
          model.identity = o.window_tiles == 0;
          if (const auto seed = ends[n].coarse_edges.find({r, part}); seed != ends[n].coarse_edges.end()) {
            model.seed = seed->second;
          }
          for (const auto& endpoint : group) {
            if (endpoint.anchor < 0 || !forest->second.contains(endpoint.anchor)) {
              throw std::runtime_error("legroute corridor has a stale coarse endpoint attachment");
            }
            std::vector<std::pair<int, int>> domain;
            if (endpoint.box >= 0) {
              domain = tiles_of[size_t(slot.at(endpoint.box))];
            } else if (const auto* point = free_hop(endpoint)) {
              domain = point->tiles;
            } else {
              domain.emplace_back(endpoint.tile, cnf.t);
            }
            std::vector<std::pair<int, int>> permitted;
            for (const auto& [tile, lit] : domain) {
              if (model.tiles.contains(tile)) {
                permitted.emplace_back(tile, lit);
              } else {
                cnf.add({-lit});
              }
            }
            if (permitted.empty()) {
              cnf.add({});
            }
            model.terminals.push_back(std::move(permitted));
          }
          for (int t : model.tiles) {
            for (int q : {g.col(t) + 1 < g.cols ? t + 1 : -1, g.row(t) + 1 < g.rows ? t + g.cols : -1}) {
              budget.check();
              if (q < 0 || !model.tiles.contains(q) || (model.identity && !model.seed.contains({t, q}))) {
                continue;
              }
              const int  h       = g.row(t) == g.row(q);
              const auto key     = std::tuple{int(n), t, h};
              auto [edge, fresh] = corridor_edges.try_emplace(key, 0);
              if (fresh) {
                edge->second = reserve.edges.contains(key) ? cnf.t : cnf.var();
                if (edge->second != cnf.t) {
                  cnf.solver.prefer(-edge->second);
                }
              }
              model.edges.emplace_back(t, q, edge->second);
              if (model.seed.contains({t, q})) {
                corridor_seed_literals.insert(edge->second);
                // Preference only: every other corridor edge remains available.
                // Shared edges prefer true if any part's witness used them.
                if (edge->second != cnf.t) {
                  cnf.solver.prefer(edge->second);
                }
              }
              if (model.identity) {
                cnf.add({edge->second});
              }
            }
          }
          for (auto [a, b] : model.seed) {
            if (a < 0 || b <= a || !model.tiles.contains(a) || !model.tiles.contains(b)
                || std::abs(g.col(a) - g.col(b)) + std::abs(g.row(a) - g.row(b)) != 1) {
              throw std::runtime_error("legroute corridor seed contains an invalid edge");
            }
          }
          corridor_models.push_back(std::move(model));
          continue;
        }

        if (nets_here.empty() || nets_here.back() != int(n)) {
          nets_here.push_back(int(n));
        }
        for (const auto& endpoint : group) {
          if (endpoint.anchor < 0 || !forest->second.contains(endpoint.anchor)) {
            throw std::runtime_error(
                std::format("legroute coarse witness repair required: net {} region {} part {} has a stale attachment",
                            n,
                            r,
                            part));
          }
          std::vector<int> anchors{endpoint.anchor};
          if ((endpoint.box >= 0 && !boxes.boxes[size_t(endpoint.box)].fixed) || endpoint.interface_id >= 0 || endpoint.hop >= 0) {
            const auto nodes = ends[n].coarse_junctions.find({r, part});
            if (nodes != ends[n].coarse_junctions.end()) {
              anchors = coarse_anchor_candidates(g, forest->second, nodes->second, endpoint.anchor, o.window_tiles);
            }
          }
          std::vector<int> domain;
          if (endpoint.box >= 0) {
            for (const auto& [tile, lit] : tiles_of[size_t(slot.at(endpoint.box))]) {
              (void)lit;
              domain.push_back(tile);
            }
          } else if (const auto* point = free_hop(endpoint)) {
            for (const auto& [tile, lit] : point->tiles) {
              (void)lit;
              domain.push_back(tile);
            }
          } else {
            domain.push_back(endpoint.tile);
          }
          const auto& corridor  = corridors.at({int(n), part});
          const auto  supported = [&](int tile) {
            return std::ranges::any_of(anchors, [&](int anchor) {
              return coarse_stub_inside(g, corridor, tile, anchor, true, budget)
                     || coarse_stub_inside(g, corridor, tile, anchor, false, budget);
            });
          };
          // Empty placement domains belong to the existing placement constraints,
          // not to the corridor representation failure reported here.
          if (!domain.empty() && !std::ranges::any_of(domain, supported)) {
            throw Coarse_stub_unsupported(std::format(
                "legroute coarse stub construction blocker: net {} region {} part {} endpoint box {} has no corridor-valid L over "
                "{} legal endpoint tiles and {} anchor choices; route repair required, not an area proof",
                n,
                r,
                part,
                endpoint.box,
                domain.size(),
                anchors.size()));
          }
          const int home = endpoint.box >= 0 ? g.tile_of(boxes.boxes[size_t(endpoint.box)].cx, boxes.boxes[size_t(endpoint.box)].cy)
                                             : endpoint.tile;
          unsupported_stub_homes += !domain.empty() && !supported(home);
          Endpoint attachment{-1, endpoint.anchor};
          if (anchors.size() > 1) {
            attachment.anchor_choice = add_anchor_choice(anchors);
            ++rep.coarse_attachment_choices;
          }
          const int first = int(eps.size());
          eps.push_back(endpoint);
          eps.push_back(attachment);
          conns.push_back({int(n), first, first + 1, part});
          ++rep.coarse_attachment_stubs;
        }
        continue;
      }
      if (group.size() < 2) {
        continue;
      }
      if (nets_here.empty() || nets_here.back() != int(n)) {
        nets_here.push_back(int(n));
      }
      const int first = int(eps.size());
      eps.insert(eps.end(), group.begin(), group.end());
      if (group.size() >= std::max(2u, o.steiner_min) && group.size() <= o.steiner_pins) {
        steiner.push_back({int(n), first, int(group.size())});
        continue;
      }
      const auto fixed_tile = [&](const Endpoint& e) {
        return e.box >= 0 ? (boxes.boxes[size_t(e.box)].fixed ? boxes.boxes[size_t(e.box)].tile : -1) : (free_hop(e) ? -1 : e.tile);
      };
      const auto inside = [&](int ta, int tb, bool horizontal_first) {
        budget.check();
        const int hr = horizontal_first ? g.row(ta) : g.row(tb), vc = horizontal_first ? g.col(tb) : g.col(ta);
        for (int c = std::min(g.col(ta), g.col(tb)); c <= std::max(g.col(ta), g.col(tb)); ++c) {
          if (!here.contains(g.tile(c, hr))) {
            return false;
          }
        }
        for (int rr = std::min(g.row(ta), g.row(tb)); rr <= std::max(g.row(ta), g.row(tb)); ++rr) {
          if (!here.contains(g.tile(vc, rr))) {
            return false;
          }
        }
        return true;
      };
      const int  original_root          = fixed_tile(eps[size_t(first)]);
      const bool unsupported_fixed_pair = original_root >= 0 && std::ranges::any_of(group, [&](const Endpoint& e) {
                                            const int t = fixed_tile(e);
                                            return t >= 0 && !inside(original_root, t, true) && !inside(original_root, t, false);
                                          });
      if (unsupported_fixed_pair) {
        // A fixed IO need not be a usable star center in a connected mesh.
        // Try another existing endpoint of this same component, using the
        // actual fitted domains. One root position must support every leg;
        // independently supported pairs at incompatible root positions do
        // not suffice. Capacity and simultaneous placement remain SAT's job.
        std::vector<std::vector<int>> domains(group.size());
        for (size_t i = 0; i < group.size(); ++i) {
          const auto& e = group[i];
          if (e.box >= 0) {
            for (const auto& [t, lit] : tiles_of[slot.at(e.box)]) {
              (void)lit;
              domains[i].push_back(t);
            }
          } else if (const auto* hop = free_hop(e)) {
            for (const auto& [t, lit] : hop->tiles) {
              (void)lit;
              domains[i].push_back(t);
            }
          } else {
            domains[i].push_back(e.tile);
          }
        }
        for (size_t root = 1; root < group.size(); ++root) {
          const bool supported = std::ranges::any_of(domains[root], [&](int ta) {
            for (size_t i = 0; i < group.size(); ++i) {
              if (i == root) {
                continue;
              }
              const bool distinct_boxes = group[root].box >= 0 && group[i].box >= 0 && group[root].box != group[i].box;
              if (!std::ranges::any_of(domains[i], [&](int tb) {
                    return !(distinct_boxes && ta == tb) && (inside(ta, tb, true) || inside(ta, tb, false));
                  })) {
                return false;
              }
            }
            return true;
          });
          if (supported) {
            std::swap(eps[size_t(first)], eps[size_t(first) + root]);
            ++rep.sat_root_fallbacks;
            break;
          }
        }
      }
      for (int k = first + 1; k < int(eps.size()); ++k) {
        const int ta = fixed_tile(eps[size_t(first)]), tb = fixed_tile(eps[size_t(k)]);
        if (unsupported_fixed_pair && ta >= 0 && tb >= 0) {
          if (!inside(ta, tb, true) && !inside(ta, tb, false)) {
            // This proves a limitation of the chosen two-L encoding, not
            // physical unroutability. Never request more area for it. Movable
            // domains and free hops must still be decided by the actual model.
            throw std::runtime_error(
                std::format("legroute local model cannot represent fixed connection: net {} ({}), region {}, part {}, tiles {} "
                            "({},{}) -> {} ({},{}); "
                            "neither L lies inside the region; routing decomposition must be repaired, not area grown{}",
                            n,
                            d.fp.nets[d.nets[n].frame].name,
                            r,
                            part,
                            ta,
                            g.col(ta),
                            g.row(ta),
                            tb,
                            g.col(tb),
                            g.row(tb),
                            tag));
          }
        }
        conns.push_back({int(n), first, k});
      }
    }
  }
  if (unsupported_stub_homes) {
    std::fprintf(stderr,
                 "legroute sat region %d: %zu home stubs lack a corridor L, but every legal endpoint domain has support\n",
                 r,
                 unsupported_stub_homes);
  }
  // per (net, tile, direction): the net covers the tile
  std::map<std::tuple<int, int, int>, int> cover;
  // Coarse attachment stubs use actual edge literals. Combining two independent
  // tile covers would invent an edge between disjoint pieces of the same net.
  std::map<std::tuple<int, int, int>, int> stub_edges;
  const auto                               cover_lit = [&](int n, int t, bool h) {
    auto [it, fresh] = cover.try_emplace({n, t, h ? 1 : 0}, 0);
    if (fresh) {
      it->second = cnf.var();
    }
    return it->second;
  };
  std::vector<int> orient(conns.size()), adjacent(conns.size(), 0), near(conns.size(), 0);
  for (size_t k = 0; k < conns.size(); ++k) {
    const auto& cn  = conns[k];
    const auto& a   = eps[cn.a];
    const auto& b   = eps[cn.b];
    const int   ori = cnf.var();  // true: H-first (H leg on a's row, V leg on b's column)
    orient[k]       = ori;
    // the legs stay inside the hull of the two endpoints' windows
    const auto wa = win_of(a), wb = win_of(b);
    const int  x0 = std::min(wa.c0, wb.c0), x1 = std::max(wa.c1, wb.c1), y0 = std::min(wa.r0, wb.r0), y1 = std::max(wa.r1, wb.r1);
    if (cn.part >= 0) {
      const auto& corridor = corridors.at({cn.net, cn.part});
      const auto  edge_lit = [&](int t, bool h) {
        const int q = t + (h ? 1 : g.cols);
        if (!corridor.contains(t) || !corridor.contains(q)) {
          return -cnf.t;
        }
        if (reserve.edges.contains({cn.net, t, int(h)})) {
          return cnf.t;  // the same physical net already paid for this exact edge
        }
        auto [it, fresh] = stub_edges.try_emplace({cn.net, t, int(h)}, 0);
        if (fresh) {
          it->second = cnf.var();
        }
        return it->second;
      };
      for (const auto& [e, w, first] : {
               std::tuple{&a, wa,  true},
               std::tuple{&b, wb, false}
      }) {
        const int sel = first ? -ori : ori;
        for (int j = w.r0; j <= w.r1; ++j) {
          for (int i = x0; i < x1; ++i) {
            const int edge = edge_lit(g.tile(i, j), true);
            cnf.add({sel, -row_ge(*e, j), row_ge(*e, j + 1), col_ge(a, i + 1), -col_ge(b, i + 1), edge});
            cnf.add({sel, -row_ge(*e, j), row_ge(*e, j + 1), col_ge(b, i + 1), -col_ge(a, i + 1), edge});
          }
        }
      }
      for (const auto& [e, w, first] : {
               std::tuple{&b, wb,  true},
               std::tuple{&a, wa, false}
      }) {
        const int sel = first ? -ori : ori;
        for (int i = w.c0; i <= w.c1; ++i) {
          for (int j = y0; j < y1; ++j) {
            const int edge = edge_lit(g.tile(i, j), false);
            cnf.add({sel, -col_ge(*e, i), col_ge(*e, i + 1), row_ge(a, j + 1), -row_ge(b, j + 1), edge});
            cnf.add({sel, -col_ge(*e, i), col_ge(*e, i + 1), row_ge(b, j + 1), -row_ge(a, j + 1), edge});
          }
        }
      }
      continue;
    }
    // a 1-1 net between two boxes costs no track when the boxes are neighbors (in-box metal): lone may hold only
    // then (columns or rows equal, the other off by one)
    int lone = -cnf.t;
    if (a.box >= 0 && b.box >= 0 && d.nets[cn.net].pins.size() == 2) {
      lone          = cnf.var();
      const int eqc = cnf.var(), eqr = cnf.var();
      for (int i = x0; i <= x1 + 1; ++i) {
        cnf.add({-lone, -col_ge(a, i + 1), col_ge(b, i)});
        cnf.add({-lone, -col_ge(b, i + 1), col_ge(a, i)});
        cnf.add({-eqc, -col_ge(a, i), col_ge(b, i)});
        cnf.add({-eqc, col_ge(a, i), -col_ge(b, i)});
      }
      for (int j = y0; j <= y1 + 1; ++j) {
        cnf.add({-lone, -row_ge(a, j + 1), row_ge(b, j)});
        cnf.add({-lone, -row_ge(b, j + 1), row_ge(a, j)});
        cnf.add({-eqr, -row_ge(a, j), row_ge(b, j)});
        cnf.add({-eqr, row_ge(a, j), -row_ge(b, j)});
      }
      cnf.add({-lone, eqc, eqr});
      adjacent[k] = lone;
    }
    // a critical connection between two boxes: near may hold only when the boxes are within one tile of each other
    // in both directions (a tier's literal; the adjacency above also needs a shared row or column)
    if (a.box >= 0 && b.box >= 0 && !o.tiers.empty() && d.nets[cn.net].rank < o.tier_fraction) {
      const int nr = cnf.var();
      for (int i = x0; i <= x1 + 1; ++i) {
        cnf.add({-nr, -col_ge(a, i + 1), col_ge(b, i)});
        cnf.add({-nr, -col_ge(b, i + 1), col_ge(a, i)});
      }
      for (int j = y0; j <= y1 + 1; ++j) {
        cnf.add({-nr, -row_ge(a, j + 1), row_ge(b, j)});
        cnf.add({-nr, -row_ge(b, j + 1), row_ge(a, j)});
      }
      near[k] = nr;
    }
    // a leg of zero length takes no track of its direction (same column: the V leg alone covers the tiles)
    const int same_c = cnf.var(), same_r = cnf.var();
    for (int i = x0; i <= x1 + 1; ++i) {
      cnf.add({-same_c, -col_ge(a, i), col_ge(b, i)});
      cnf.add({-same_c, col_ge(a, i), -col_ge(b, i)});
    }
    for (int j = y0; j <= y1 + 1; ++j) {
      cnf.add({-same_r, -row_ge(a, j), row_ge(b, j)});
      cnf.add({-same_r, row_ge(a, j), -row_ge(b, j)});
    }
    // between literals: column i between a's and b's columns, row j between their rows
    std::vector<int> bc(x1 - x0 + 1), br(y1 - y0 + 1);
    for (int i = x0; i <= x1; ++i) {
      const int v = bc[i - x0] = cnf.var();
      for (const auto* x : {&a, &b}) {
        for (const auto* y : {&a, &b}) {
          cnf.add({col_ge(*x, i + 1), -col_ge(*y, i), v});  // col(x) <= i and col(y) >= i
        }
      }
    }
    for (int j = y0; j <= y1; ++j) {
      const int v = br[j - y0] = cnf.var();
      for (const auto* x : {&a, &b}) {
        for (const auto* y : {&a, &b}) {
          cnf.add({row_ge(*x, j + 1), -row_ge(*y, j), v});
        }
      }
    }
    // a leg lies on an endpoint's row or column: only that endpoint's window rows (H) or columns (V) cross the span
    const auto lit = [&](int i, int j, bool h) {
      const int t = g.tile(i, j);
      return g.region[t] == r ? cover_lit(cn.net, t, h) : -cnf.t;
    };
    for (const auto& [e, w, first] : {
             std::tuple{&a, wa,  true},
             std::tuple{&b, wb, false}
    }) {
      const int sel = first ? -ori : ori;   // H-first: H leg on row(a), V leg on col(b); V-first: the other way
      for (int j = w.r0; j <= w.r1; ++j) {  // H leg on row(e)
        for (int i = x0; i <= x1; ++i) {
          cnf.add({sel, -row_ge(*e, j), row_ge(*e, j + 1), -bc[i - x0], lone, same_c, lit(i, j, true)});
        }
      }
    }
    for (const auto& [e, w, h_first] : {
             std::tuple{&b, wb,  true},
             std::tuple{&a, wa, false}
    }) {
      const int sel = h_first ? -ori : ori;  // the V leg: col(b) when H-first, col(a) when V-first
      for (int i = w.c0; i <= w.c1; ++i) {
        for (int j = y0; j <= y1; ++j) {
          cnf.add({sel, -col_ge(*e, i), col_ge(*e, i + 1), -br[j - y0], lone, same_r, lit(i, j, false)});
        }
      }
    }
  }
  // single-trunk Steiner trees (the RSMT for three endpoints): a trunk on a row (H) or a column (V) from the first to
  // the last endpoint along it, and a perpendicular rib from each endpoint to the trunk; the trunk's row or column is
  // a variable over the hull of the endpoints' windows
  for (auto& st : steiner) {
    const auto h = [&](int k) -> const Endpoint& { return eps[size_t(st.first + k)]; };
    st.x0 = g.cols, st.x1 = -1, st.y0 = g.rows, st.y1 = -1;
    for (int k = 0; k < st.count; ++k) {
      const auto w = win_of(h(k));
      st.x0        = std::min(st.x0, w.c0);
      st.x1        = std::max(st.x1, w.c1);
      st.y0        = std::min(st.y0, w.r0);
      st.y1        = std::max(st.y1, w.r1);
    }
    st.h_trunk = cnf.var();
    for (auto [axis, lo, hi] : {
             std::tuple{&st.row_ge, st.y0, st.y1},
             std::tuple{&st.col_ge, st.x0, st.x1}
    }) {
      for (int k = lo + 1; k <= hi; ++k) {
        axis->push_back(cnf.var());
        if (axis->size() > 1) {
          cnf.add({-axis->back(), (*axis)[axis->size() - 2]});
        }
      }
    }
    const auto axis_ge = [&](bool rows, int k) {
      const auto& v  = rows ? st.row_ge : st.col_ge;
      const int   lo = rows ? st.y0 : st.x0, hi = rows ? st.y1 : st.x1;
      return k <= lo ? cnf.t : k > hi ? -cnf.t : v[size_t(k - lo - 1)];
    };
    const auto lit = [&](int i, int j, bool hh) {
      const int t = g.tile(i, j);
      return g.region[t] == r ? cover_lit(st.net, t, hh) : -cnf.t;
    };
    for (bool ht : {true, false}) {
      const int        sel = ht ? -st.h_trunk : st.h_trunk;  // a clause holds when this orientation is chosen
      // between along the trunk: some endpoint at or before k and some at or after it
      const int        a0 = ht ? st.x0 : st.y0, a1 = ht ? st.x1 : st.y1;  // along
      const int        m0 = ht ? st.y0 : st.x0, m1 = ht ? st.y1 : st.x1;  // across (the axis)
      std::vector<int> between(size_t(a1 - a0 + 1));
      const auto       along_ge  = [&](const Endpoint& e, int k) { return ht ? col_ge(e, k) : row_ge(e, k); };
      const auto       across_ge = [&](const Endpoint& e, int k) { return ht ? row_ge(e, k) : col_ge(e, k); };
      for (int k = a0; k <= a1; ++k) {
        const int v = between[size_t(k - a0)] = cnf.var();
        for (int x = 0; x < st.count; ++x) {
          for (int y = 0; y < st.count; ++y) {
            cnf.add({along_ge(h(x), k + 1), -along_ge(h(y), k), v});
          }
        }
      }
      for (int m = m0; m <= m1; ++m) {  // the trunk on axis m
        for (int k = a0; k <= a1; ++k) {
          cnf.add({sel, -axis_ge(ht, m), axis_ge(ht, m + 1), -between[size_t(k - a0)], ht ? lit(k, m, true) : lit(m, k, false)});
        }
      }
      for (int x = 0; x < st.count; ++x) {  // the ribs: along fixed at the endpoint, across from it to the axis
        const auto w  = win_of(h(x));
        const int  b0 = ht ? w.c0 : w.r0, b1 = ht ? w.c1 : w.r1;
        const int  on = cnf.var();  // the endpoint lies on the trunk: no rib (a zero-length rib takes no track)
        for (int m = m0; m <= m1 + 1; ++m) {
          cnf.add({-on, -across_ge(h(x), m), axis_ge(ht, m)});
          cnf.add({-on, across_ge(h(x), m), -axis_ge(ht, m)});
        }
        for (int k = b0; k <= b1; ++k) {
          for (int m = m0; m <= m1; ++m) {
            const int cov = ht ? lit(k, m, false) : lit(m, k, true);
            // endpoint at or below m, axis at or above m; and the mirror
            cnf.add({sel, on, -along_ge(h(x), k), along_ge(h(x), k + 1), across_ge(h(x), m + 1), -axis_ge(ht, m), cov});
            cnf.add({sel, on, -along_ge(h(x), k), along_ge(h(x), k + 1), -across_ge(h(x), m), axis_ge(ht, m + 1), cov});
          }
        }
      }
    }
  }
  // capacity per tile edge and direction: the nets that cross the edge (cover both its tiles). The in-box wires keep
  // to their own tile (charging them on the edges rejected a placement OpenROAD routed), and two nets that end in a
  // tile from opposite sides share a track, each on its own edge
  std::map<std::pair<int, int>, std::vector<int>> inputs;  // (left / lower tile, h) -> the crossing literals
  const auto next_tile = [&](int t, int h) {               // the neighbor across the right (H) or upper (V) edge, -1 if outside
    const int q = h ? (g.col(t) + 1 < g.cols ? t + 1 : -1) : (t + g.cols < int(g.size()) ? t + g.cols : -1);
    return q >= 0 && here.contains(q) ? q : -1;
  };
  for (const auto& [key, lit] : cover) {
    const auto [n, t, h] = key;
    const int q          = next_tile(t, h);
    if (q < 0 || reserve.edges.contains({n, t, h})) {
      continue;
    }
    if (const auto it = cover.find({n, q, h}); it != cover.end()) {
      const int e = cnf.var();  // the net crosses the edge t | q
      cnf.add({-lit, -it->second, e});
      inputs[{t, h}].push_back(e);
    }
  }
  for (const auto& [key, lit] : stub_edges) {
    const auto [n, t, h] = key;
    (void)n;
    inputs[{t, h}].push_back(lit);
  }
  for (const auto& [key, lit] : corridor_edges) {
    const auto [n, t, h] = key;
    if (!reserve.edges.contains(key)) {
      inputs[{t, h}].push_back(lit);
    }
  }
  rep.corridor_components      = corridor_models.size();
  rep.corridor_candidate_edges = corridor_edges.size();
  size_t       binding         = 0;
  const size_t cl_conn         = cnf.clauses;
  size_t       cap_in = 0, cap_k = 0, cap_n = 0, cap_est = 0;
  struct Bound {
    int t, h, k, over;
  };
  std::vector<Bound> bounds;  // with sat_relax_rounds: each edge's bound is an assumption
  // capacity_lazy: a bound is added only once an answer overflows it (1: the V bounds, 2: every bound); the model
  // starts with the placement and the legs alone, and pays for counting where the answers are tight
  struct Lazy {
    int              t, h, k;
    std::vector<int> xs;
  };
  std::vector<Lazy> lazy;
  const auto        lazy_dir = [&](int h) { return o.capacity_lazy && !o.sat_relax_rounds && (o.capacity_lazy == 2 || !h); };
  for (int t : T) {
    for (int h : {1, 0}) {
      const int q = next_tile(t, h);
      if (q < 0) {
        continue;  // no edge of the region to the right / above
      }
      auto& xs = inputs[{t, h}];
      if (o.untracked && (*o.untracked)[size_t(t)]) {
        continue;  // the checker alternative: no bound on the edges this tile owns
      }
      // the edge's tracks: the physical tracks of the narrower tile less the routes fixed ahead that cross the edge
      const int k = std::max(
          0,
          edge_limit(std::min((h ? cap_h0 : cap_v0)[t], (h ? cap_h0 : cap_v0)[q]), o) - (h ? reserve.h : reserve.v)[size_t(t)]);
      binding += int(xs.size()) > k;
      cap_in  += xs.size();
      cap_k   += k;
      ++cap_n;
      cap_est += at_most_clauses(int(xs.size()), k);
      if (lazy_dir(h)) {
        if (int(xs.size()) > k) {
          lazy.push_back({t, h, k, xs});
        }
      } else if (!o.sat_relax_rounds) {
        cnf.at_most(xs, k);
      } else if (const int ov = cnf.over(xs, k)) {
        bounds.push_back({t, h, k, ov});
      }
    }
  }
  std::fprintf(stderr,
               "legroute sat region %d model: clauses connections %zu capacity %zu, per capacity row inputs %.1f bound %.1f\n",
               r,
               cl_conn,
               cnf.clauses - cl_conn,
               double(cap_in) / double(std::max<size_t>(1, cap_n)),
               double(cap_k) / double(std::max<size_t>(1, cap_n)));
  {  // diagnostic: per edge, the routes fixed ahead plus the straight connections between two fixed endpoints (no
    // shape choice) against the edge's tracks: an edge over here is UNSAT before any search
    const auto fixed_at = [&](const Endpoint& e) {
      return e.box < 0 ? (free_hop(e) ? -1 : e.tile) : boxes.boxes[e.box].fixed ? boxes.boxes[e.box].tile : -1;
    };
    std::map<std::pair<int, int>, int> straight;  // (edge tile, h) -> straight fixed connections crossing it
    std::map<std::pair<int, int>, int> through;   // of those, between two fixed tiles (hops, pins): pass-through nets
    for (const auto& cn : conns) {
      const int ta = fixed_at(eps[size_t(cn.a)]), tb = fixed_at(eps[size_t(cn.b)]);
      if (ta < 0 || tb < 0 || ta == tb || (g.row(ta) != g.row(tb) && g.col(ta) != g.col(tb))) {
        continue;
      }
      const bool pass = eps[size_t(cn.a)].box < 0 && eps[size_t(cn.b)].box < 0;
      if (pass) {
        if (g.row(ta) == g.row(tb)) {
          for (int i = std::min(g.col(ta), g.col(tb)); i < std::max(g.col(ta), g.col(tb)); ++i) {
            if (!reserve.edges.contains({cn.net, g.tile(i, g.row(ta)), 1})) {
              ++through[{g.tile(i, g.row(ta)), 1}];
            }
          }
        } else {
          for (int j = std::min(g.row(ta), g.row(tb)); j < std::max(g.row(ta), g.row(tb)); ++j) {
            if (!reserve.edges.contains({cn.net, g.tile(g.col(ta), j), 0})) {
              ++through[{g.tile(g.col(ta), j), 0}];
            }
          }
        }
      }
      if (eps[size_t(cn.a)].box >= 0 && eps[size_t(cn.b)].box >= 0 && d.nets[cn.net].pins.size() == 2
          && std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb)) == 1) {
        continue;  // neighbors: in-box metal
      }
      if (g.row(ta) == g.row(tb)) {
        for (int i = std::min(g.col(ta), g.col(tb)); i < std::max(g.col(ta), g.col(tb)); ++i) {
          if (!reserve.edges.contains({cn.net, g.tile(i, g.row(ta)), 1})) {
            ++straight[{g.tile(i, g.row(ta)), 1}];
          }
        }
      } else {
        for (int j = std::min(g.row(ta), g.row(tb)); j < std::max(g.row(ta), g.row(tb)); ++j) {
          if (!reserve.edges.contains({cn.net, g.tile(g.col(ta), j), 0})) {
            ++straight[{g.tile(g.col(ta), j), 0}];
          }
        }
      }
    }
    struct Over {
      int t, h, reserved, straight, tracks;
    };
    std::vector<Over> over;
    for (int t : T) {
      for (int h : {1, 0}) {
        const int q = next_tile(t, h);
        if (q < 0) {
          continue;
        }
        const int tracks = std::max(0, edge_limit(std::min((h ? cap_h0 : cap_v0)[t], (h ? cap_h0 : cap_v0)[q]), o));
        const int res = (h ? reserve.h : reserve.v)[size_t(t)], st = straight[{t, h}];
        if (res + st > tracks) {
          over.push_back({t, h, res, st, tracks});
        }
      }
    }
    std::ranges::sort(over, [](const Over& a, const Over& b) {
      return a.reserved + a.straight - a.tracks > b.reserved + b.straight - b.tracks;
    });
    std::string worst;
    for (size_t i = 0; i < over.size() && i < 5; ++i) {
      worst += std::format(" ({},{}){}: fixed {}+straight {} (pass-through {}) of {}",
                           g.col(over[i].t),
                           g.row(over[i].t),
                           over[i].h ? "H" : "V",
                           over[i].reserved,
                           over[i].straight,
                           through[{over[i].t, over[i].h}],
                           over[i].tracks);
    }
    std::fprintf(stderr, "legroute sat region %d forced edges: %zu over their tracks%s\n", r, over.size(), worst.c_str());
    if (!over.empty()) {  // the straight fixed connections crossing the worst edge: their endpoints (tile, kind)
      std::string list;
      int         shown = 0;
      for (const auto& cn : conns) {
        const int ta = fixed_at(eps[size_t(cn.a)]), tb = fixed_at(eps[size_t(cn.b)]);
        if (ta < 0 || tb < 0 || ta == tb || !(over[0].h ? g.row(ta) == g.row(tb) : g.col(ta) == g.col(tb))) {
          continue;
        }
        const bool crosses = over[0].h ? (g.row(ta) == g.row(over[0].t) && std::min(g.col(ta), g.col(tb)) <= g.col(over[0].t)
                                          && g.col(over[0].t) < std::max(g.col(ta), g.col(tb)))
                                       : (g.col(ta) == g.col(over[0].t) && std::min(g.row(ta), g.row(tb)) <= g.row(over[0].t)
                                          && g.row(over[0].t) < std::max(g.row(ta), g.row(tb)));
        if (!crosses || shown++ >= 12) {
          continue;
        }
        const auto kind = [&](const Endpoint& e, int t) {
          if (e.box >= 0) {
            return std::string("box");
          }
          for (const auto& p : d.nets[cn.net].pins) {
            if (p.term >= 0 && g.tile_of(double(d.fp.terminals[p.term].x), double(d.fp.terminals[p.term].y)) == t) {
              return std::string("pin");
            }
          }
          return std::string("hop");
        };
        list += std::format(" {}:({},{}){}->({},{}){}",
                            d.fp.nets[d.nets[cn.net].frame].name,
                            g.col(ta),
                            g.row(ta),
                            kind(eps[size_t(cn.a)], ta),
                            g.col(tb),
                            g.row(tb),
                            kind(eps[size_t(cn.b)], tb));
      }
      std::fprintf(stderr,
                   "legroute sat region %d forced edge (%d,%d)%s connections:%s\n",
                   r,
                   g.col(over[0].t),
                   g.row(over[0].t),
                   over[0].h ? "H" : "V",
                   list.c_str());
    }
  }
  std::vector<int> assume;
  for (const auto& b : bounds) {
    assume.push_back(-b.over);
  }
  // sat_min_fill: a region this empty is taken as routable without a solve (its boxes on their nearest tiles)
  const auto free_tiles     = std::ranges::count_if(T, [&](int t) { return !g.blocked[t]; });
  out.skipped               = o.sat_min_fill > 0 && double(B.size()) <= o.sat_min_fill * double(free_tiles);
  budget.build_seconds      = seconds_since(start);
  auto&            deadline = budget.deadline;
  // sat_seed: the fallback placement is tried first (its tile literals assumed); a routable answer there is kept,
  // an UNSAT drops the assumptions and the free model continues with what the solver learned
  std::vector<int> seed;
  if (o.sat_seed && !o.freeze && !out.skipped) {
    const auto tiles = fallback_tiles(window_tiles);
    for (size_t i = 0; i < B.size(); ++i) {
      if (tiles[i] < 0) {
        continue;
      }
      for (const auto& [t, p] : tiles_of[i]) {
        if (t == tiles[i]) {
          seed.push_back(p);
          break;
        }
      }
    }
  }
  if (o.sat_seed && !out.skipped) {
    seed.insert(seed.end(), corridor_seed_literals.begin(), corridor_seed_literals.end());
  }
  bool       seeded     = !seed.empty();
  // one solve under the bounds' assumptions, the seed (dropped on its first UNSAT) and extra assumptions
  const auto solve_with = [&](const std::vector<int>& extra) {
    std::vector<int> a = assume;
    a.insert(a.end(), extra.begin(), extra.end());
    if (seeded) {
      std::vector<int> b = a;
      b.insert(b.end(), seed.begin(), seed.end());
      const auto left = budget.remaining_ms();
      if (!left) {
        return Sat_status::unknown;
      }
      const auto st = cnf.solver.solve(b, o.sat_conflicts, left);
      if (!budget.seed_status) {
        budget.seed_status = st;
      }
      ++rep.sat_solves;
      if (st != Sat_status::unsatisfiable) {
        return st;
      }
      seeded = false;
    }
    const auto left = budget.remaining_ms();
    if (!left) {
      return Sat_status::unknown;
    }
    ++rep.sat_solves;
    return cnf.solver.solve(a, o.sat_conflicts, left);
  };
  // Both lazy capacity checks and route emission must use the same selected
  // boundary tile. Endpoint::tile remains the pre-SAT chooser's position.
  const auto chosen_hop_tile = [&](const Endpoint& e) {
    if (const auto* fh = free_hop(e)) {
      for (const auto& [t, p] : fh->tiles) {
        if (cnf.solver.value(p)) {
          return t;
        }
      }
      throw std::logic_error("legroute: SAT answer has no selected tile for a free endpoint");
    }
    return e.tile;
  };
  // the answer's decoded legs (connections, Steiner trunks and ribs) per tile and direction
  const auto decoded = [&] {
    std::map<std::pair<int, int>, std::set<int>> covers;       // (tile, h) -> nets whose legs cover it
    std::map<std::pair<int, int>, std::set<int>> exact_edges;  // new stubs, deduplicated per physical net
    std::vector<int>                             at_box(B.size(), -1);
    for (size_t i = 0; i < B.size(); ++i) {
      for (const auto& [t, p] : tiles_of[i]) {
        if (cnf.solver.value(p)) {
          at_box[i] = t;
        }
      }
    }
    const auto ep_tile = [&](const Endpoint& e) { return e.box >= 0 ? at_box[size_t(slot.at(e.box))] : chosen_hop_tile(e); };
    const auto hcover  = [&](int n, int col_start, int col_end, int rr) {
      for (int i = std::min(col_start, col_end); i <= std::max(col_start, col_end); ++i) {
        covers[{g.tile(i, rr), 1}].insert(n);
      }
    };
    const auto vcover = [&](int n, int cc, int row_start, int row_end) {
      for (int j = std::min(row_start, row_end); j <= std::max(row_start, row_end); ++j) {
        covers[{g.tile(cc, j), 0}].insert(n);
      }
    };
    for (size_t k = 0; k < conns.size(); ++k) {
      if (adjacent[k] && cnf.solver.value(adjacent[k])) {
        continue;
      }
      const int  ta = ep_tile(eps[size_t(conns[k].a)]), tb = ep_tile(eps[size_t(conns[k].b)]);
      const bool hf = cnf.solver.value(orient[k]);
      const int  hr = hf ? g.row(ta) : g.row(tb), vc = hf ? g.col(tb) : g.col(ta);
      if (conns[k].part >= 0) {
        for (int c = std::min(g.col(ta), g.col(tb)); c < std::max(g.col(ta), g.col(tb)); ++c) {
          exact_edges[{g.tile(c, hr), 1}].insert(conns[k].net);
        }
        for (int rr = std::min(g.row(ta), g.row(tb)); rr < std::max(g.row(ta), g.row(tb)); ++rr) {
          exact_edges[{g.tile(vc, rr), 0}].insert(conns[k].net);
        }
        continue;
      }
      if (g.col(ta) != g.col(tb)) {
        hcover(conns[k].net, g.col(ta), g.col(tb), hr);
      }
      if (g.row(ta) != g.row(tb)) {
        vcover(conns[k].net, vc, g.row(ta), g.row(tb));
      }
    }
    for (const auto& st : steiner) {
      const bool ht   = cnf.solver.value(st.h_trunk);
      int        axis = ht ? st.y0 : st.x0;
      for (int lit : (ht ? st.row_ge : st.col_ge)) {
        axis += cnf.solver.value(lit) ? 1 : 0;
      }
      int lo = INT32_MAX, hi = INT32_MIN;
      for (int k = 0; k < st.count; ++k) {
        const int t     = ep_tile(eps[size_t(st.first + k)]);
        const int along = ht ? g.col(t) : g.row(t), across = ht ? g.row(t) : g.col(t);
        lo = std::min(lo, along);
        hi = std::max(hi, along);
        if (across != axis) {
          if (ht) {
            vcover(st.net, g.col(t), g.row(t), axis);
          } else {
            hcover(st.net, g.col(t), axis, g.row(t));
          }
        }
      }
      if (lo != hi) {
        if (ht) {
          hcover(st.net, lo, hi, axis);
        } else {
          vcover(st.net, axis, lo, hi);
        }
      }
    }
    // Legacy star/Steiner models keep their tile-cover semantics. New coarse
    // stubs never enter this intersection and cannot acquire fictitious edges.
    for (const auto& [key, nets] : covers) {
      const auto [t, h] = key;
      const auto other  = covers.find({next_tile(t, h), h});
      if (other != covers.end()) {
        for (int n : nets) {
          if (other->second.contains(n)) {
            exact_edges[{t, h}].insert(n);
          }
        }
      }
    }
    for (const auto& [key, lit] : corridor_edges) {
      budget.check();
      if (cnf.solver.value(lit)) {
        const auto [net, t, h] = key;
        exact_edges[{t, h}].insert(net);
      }
    }
    return exact_edges;
  };
  unsigned   lazy_rounds = 0;
  size_t     lazy_added  = 0;
  // settle: solve under extra assumptions; an answer that overflows a lazy bound adds the bound and solves again,
  // incrementally, until no bound overflows, UNSAT or the time is out. The answer kept is overflow-free on every
  // bound, lazy or not
  const auto settle      = [&](const std::vector<int>& extra) {
    const auto initial_start = Clock::now();
    Sat_status st            = solve_with(extra);
    if (!budget.initial_status) {
      budget.initial_status  = st;
      budget.initial_seconds = seconds_since(initial_start);
    }
    while (st == Sat_status::satisfiable) {
      const auto   edges                = decoded();
      const size_t connectivity         = add_corridor_connectivity_cuts(cnf, corridor_models, budget);
      rep.corridor_connectivity_cuts   += connectivity;
      rep.corridor_connectivity_rounds += connectivity > 0;
      size_t added                      = 0;
      for (auto& L : lazy) {
        if (L.xs.empty()) {
          continue;  // added in an earlier round
        }
        int use = 0;
        if (const auto edge = edges.find({L.t, L.h}); edge != edges.end()) {
          for (int n : edge->second) {
            use += !reserve.edges.contains({n, L.t, L.h});
          }
        }
        if (use > L.k) {
          cnf.at_most(L.xs, L.k);
          L.xs.clear();
          L.xs.shrink_to_fit();
          ++added;
        }
      }
      if (!added && !connectivity) {
        break;
      }
      lazy_added += added;
      ++lazy_rounds;
      if (Clock::now() >= deadline) {
        st = Sat_status::unknown;
        break;
      }
      st = solve_with(extra);
    }
    return st;
  };
  // the answer: the boxes' tiles, the connections with their shapes (neighbor boxes of a 1-1 net: in-box metal, no
  // lane), the trees as straight trunk pieces between the ribs' feet and straight ribs. Decoded after every routable
  // solve, so a tier that fails later leaves the last routable answer
  size_t     adjacent_nets = 0;
  const auto decode_answer = [&] {
    // Keep the last verified answer intact until this candidate's actual geometry passes the independent check.
    // In particular, deadline expiry while recounting a later tier must not publish an unchecked new pose.
    Region_out       candidate;
    size_t           changed_attachments = 0;
    std::vector<int> at_box(B.size(), -1);
    for (size_t i = 0; i < B.size(); ++i) {
      for (const auto& [t, p] : tiles_of[i]) {
        if (cnf.solver.value(p)) {
          at_box[i] = t;
          break;
        }
      }
      if (at_box[i] < 0) {
        throw std::logic_error("legroute: SAT answer has no selected tile for a box");
      }
    }
    for (const auto& fh : hops_free) {
      const int chosen = chosen_hop_tile({-1, fh.def, fh.id});
      candidate.hop_tiles.emplace_back(fh.net, fh.def, chosen);
      candidate.hop_choices.emplace(fh.id, chosen);
    }
    const auto ep_tile = [&](const Endpoint& e) {
      if (e.box >= 0) {
        return at_box[size_t(slot.at(e.box))];
      }
      return chosen_hop_tile(e);
    };
    for (size_t k = 0; k < conns.size(); ++k) {
      const auto& cn = conns[k];
      const auto  ta = ep_tile(eps[cn.a]);
      const auto  tb = ep_tile(eps[cn.b]);
      if (adjacent[k] && cnf.solver.value(adjacent[k])) {
        candidate.local.push_back(cn.net);
        continue;
      }
      const bool hf = cnf.solver.value(orient[k]);
      if (cn.part >= 0) {
        const auto& attachment = eps[size_t(cn.b)];
        const auto& backbone   = ends[size_t(cn.net)].coarse_parts.at({r, cn.part});
        if (!backbone.contains(tb)) {
          throw std::runtime_error(std::format(
              "legroute coarse witness repair required: selected net {} region {} part {} attachment is outside its backbone",
              cn.net,
              r,
              cn.part));
        }
        changed_attachments  += attachment.anchor_choice >= 0 && tb != attachment.tile;
        const auto& corridor  = corridors.at({cn.net, cn.part});
        const int   hr = hf ? g.row(ta) : g.row(tb), vc = hf ? g.col(tb) : g.col(ta);
        const auto  check = [&](int t) {
          budget.check();
          if (!corridor.contains(t)) {
            throw std::runtime_error(std::format(
                "legroute coarse witness repair required: decoded net {} region {} part {} leaves its corridor at tile {}",
                cn.net,
                r,
                cn.part,
                t));
          }
        };
        for (int c = std::min(g.col(ta), g.col(tb)); c <= std::max(g.col(ta), g.col(tb)); ++c) {
          check(g.tile(c, hr));
        }
        for (int rr = std::min(g.row(ta), g.row(tb)); rr <= std::max(g.row(ta), g.row(tb)); ++rr) {
          check(g.tile(vc, rr));
        }
      }
      candidate.conns.push_back({cn.net, ta, tb, hf});
    }
    for (const auto& st : steiner) {
      const bool  ht   = cnf.solver.value(st.h_trunk);
      const auto& v    = ht ? st.row_ge : st.col_ge;
      int         axis = ht ? st.y0 : st.x0;
      for (int lit : v) {
        axis += cnf.solver.value(lit) ? 1 : 0;
      }
      std::vector<int> feet;  // along coordinate of each rib's foot on the trunk
      for (int k = 0; k < st.count; ++k) {
        const auto& e    = eps[size_t(st.first + k)];
        const int   t    = ep_tile(e);
        const int   foot = ht ? g.tile(g.col(t), axis) : g.tile(axis, g.row(t));
        if (foot != t) {
          candidate.conns.push_back({st.net, t, foot, ht});
        }
        feet.push_back(ht ? g.col(t) : g.row(t));
      }
      std::ranges::sort(feet);
      feet.erase(std::unique(feet.begin(), feet.end()), feet.end());
      for (size_t k = 1; k < feet.size(); ++k) {
        const int ta = ht ? g.tile(feet[k - 1], axis) : g.tile(axis, feet[k - 1]);
        const int tb = ht ? g.tile(feet[k], axis) : g.tile(axis, feet[k]);
        candidate.conns.push_back({st.net, ta, tb, ht});
      }
    }
    std::set<std::tuple<int, int, int>> emitted_edges;
    for (const auto& model : corridor_models) {
      const auto selected = decode_corridor_edges(cnf, model, budget);
      // Independent endpoint connectivity was checked from decoded geometry;
      // exact net-edge union is emitted once even if multiple parts overlap.
      for (const auto& [a, b] : selected) {
        if (emitted_edges.emplace(model.net, a, b).second) {
          candidate.conns.push_back({model.net, a, b, g.row(a) == g.row(b)});
        }
      }
    }
    validate_region_edges(g, r, candidate.conns, reserve, cap_h0, cap_v0, o, &budget);
    rep.corridor_checked_components = corridor_models.size();
    rep.corridor_selected_edges     = emitted_edges.size();
    for (size_t i = 0; i < B.size(); ++i) {
      boxes.boxes[B[i]].tile = at_box[i];
    }
    rep.coarse_attachments_changed = changed_attachments;
    adjacent_nets                  = candidate.local.size();
    out.conns                      = std::move(candidate.conns);
    out.local                      = std::move(candidate.local);
    out.hop_tiles                  = std::move(candidate.hop_tiles);
    out.hop_choices                = std::move(candidate.hop_choices);
    budget.decoded                 = true;
  };
  // the timing proxy: weighted route length (tiles) of the answer's connections, and the plain length of the tier
  // nets' connections (slack rank under tier_fraction)
  const auto route_cost = [&](double& crit) {
    double c = 0;
    crit     = 0;
    for (const auto& pc : out.conns) {
      const double len  = double(std::abs(g.col(pc.ta) - g.col(pc.tb)) + std::abs(g.row(pc.ta) - g.row(pc.tb)));
      c                += d.nets[pc.net].weight * len;
      crit             += d.nets[pc.net].rank < o.tier_fraction ? len : 0;
    }
    return c;
  };
  if (!corridor_models.empty()) {
    std::fprintf(stderr,
                 "legroute sat region %d corridor: %zu components, %zu selectable net-edges, fixed interfaces, radius %u\n",
                 r,
                 corridor_models.size(),
                 corridor_edges.size(),
                 o.window_tiles);
  }
  Sat_status status = out.skipped ? Sat_status::unknown : settle({});
  out.status        = status;
  if (status == Sat_status::satisfiable) {
    rep.sat_seeded += seeded;  // the answer is the fallback placement
    decode_answer();
  }
  if (!lazy.empty() && !out.skipped) {
    std::fprintf(stderr,
                 "legroute sat region %d lazy: rounds %u, bounds added %zu of %zu, clauses %zu (eager estimate %zu)%s\n",
                 r,
                 lazy_rounds,
                 lazy_added,
                 lazy.size(),
                 cnf.clauses,
                 cl_conn + cap_est,
                 tag.c_str());
  }
  if (!corridor_models.empty()) {
    std::fprintf(
        stderr,
        "legroute sat region %d corridor: %zu connectivity cuts in %zu rounds, %zu checked components, %zu emitted net-edges\n",
        r,
        rep.corridor_connectivity_cuts,
        rep.corridor_connectivity_rounds,
        rep.corridor_checked_components,
        rep.corridor_selected_edges);
  }
  // ---- timing tiers: on a routable answer, each tier's constraints are tried in batches, most critical first, with
  // every box held on its tile by assumptions, so the answer moves only where a constraint needs it: an UNSAT core
  // frees the held boxes in it, or drops the constraints in it when it holds no box; a batch out of rounds or time
  // is dropped. Kept constraints become units for the batches and tiers after
  if (status == Sat_status::satisfiable && !o.sat_relax_rounds && !o.tiers.empty()) {
    seeded                = false;  // the fallback seed gives way to the current answer's tiles
    deadline              = std::min(deadline, Clock::now() + std::chrono::milliseconds(o.tier_ms));
    double crit_len       = 0;
    rep.tier_cost_before += route_cost(crit_len);
    rep.tier_crit_before += crit_len;
    const auto tile_lit   = [&](size_t i, int t) {  // the box's literal on tile t, 0 when it cannot be there
      for (const auto& [q, p] : tiles_of[i]) {
        if (q == t) {
          return p;
        }
      }
      return 0;
    };
    // the box's tile literals within one tile of t: on or beside it (Manhattan), or any neighbor (Chebyshev)
    const auto near_lits = [&](size_t i, int t, bool diagonal) {
      std::vector<int> lits;
      for (const auto& [q, p] : tiles_of[i]) {
        const int dc = std::abs(g.col(q) - g.col(t)), dr = std::abs(g.row(q) - g.row(t));
        if (diagonal ? std::max(dc, dr) <= 1 : dc + dr <= 1) {
          lits.push_back(p);
        }
      }
      return lits;
    };
    const auto implies_any = [&](const std::vector<int>& lits) {  // a fresh literal that forces one of lits
      const int        v = cnf.var();
      std::vector<int> cl{-v};
      cl.insert(cl.end(), lits.begin(), lits.end());
      cnf.add(cl);
      return v;
    };
    std::map<std::pair<size_t, int>, int> near_cache;  // (box index, tile) -> the literal holding it within one tile
    const auto                            near_lit = [&](size_t i, int t) {
      auto [it, fresh] = near_cache.try_emplace({i, t}, 0);
      if (fresh) {
        const auto lits = near_lits(i, t, true);
        it->second      = lits.empty() ? 0 : implies_any(lits);
      }
      return it->second;
    };
    const auto box_rank = [&](int b) {  // the slack rank of the box's most critical routed net
      double rk = 1;
      for (int c : boxes.boxes[b].cells) {
        for (int n : d.nets_of[size_t(c)]) {
          if (d.nets[n].routed) {
            rk = std::min(rk, d.nets[n].rank);
          }
        }
      }
      return rk;
    };
    struct Cand {
      double rank;
      int    lit;
    };
    for (const auto& name : tier_names(o.tiers)) {
      std::vector<Cand> cands;
      if (name == "adjacent") {  // a critical 1-1 net between two boxes: neighbors
        for (size_t k = 0; k < conns.size(); ++k) {
          if (adjacent[k] && d.nets[conns[k].net].rank < o.tier_fraction) {
            cands.push_back({d.nets[conns[k].net].rank, adjacent[k]});
          }
        }
      } else if (name == "near") {  // a critical connection's boxes within one tile of each other
        for (size_t k = 0; k < conns.size(); ++k) {
          if (near[k]) {
            cands.push_back({d.nets[conns[k].net].rank, near[k]});
          }
        }
      } else if (name == "pins") {           // a critical net's box on or beside the net's fixed tile
        std::set<std::pair<int, int>> seen;  // (box, fixed tile)
        for (size_t k = 0; k < conns.size(); ++k) {
          const auto& a = eps[size_t(conns[k].a)];
          const auto& b = eps[size_t(conns[k].b)];
          if ((a.box < 0) == (b.box < 0) || d.nets[conns[k].net].rank >= o.tier_fraction) {
            continue;
          }
          const int bx = a.box >= 0 ? a.box : b.box, pin = a.box < 0 ? a.tile : b.tile;
          if (boxes.boxes[bx].fixed || !seen.emplace(bx, pin).second) {
            continue;
          }
          if (const auto lits = near_lits(size_t(slot.at(bx)), pin, false); !lits.empty()) {
            cands.push_back({d.nets[conns[k].net].rank, implies_any(lits)});
          }
        }
      } else if (name == "home") {  // a critical box within one tile of its placement tile
        for (size_t i = 0; i < B.size(); ++i) {
          const auto&  box = boxes.boxes[B[i]];
          const double rk  = box_rank(B[i]);
          if (box.fixed || rk >= o.tier_fraction) {
            continue;
          }
          if (const auto lits = near_lits(i, g.tile_of(box.cx, box.cy), true); !lits.empty()) {
            cands.push_back({rk, implies_any(lits)});
          }
        }
      }
      if (cands.empty()) {
        continue;
      }
      std::ranges::stable_sort(cands, {}, &Cand::rank);
      size_t   kept_n = 0, batches = 0, solves = 0;
      unsigned rounds_total = 0;
      bool     timed_out    = false;
      for (size_t at = 0; at < cands.size() && !timed_out; at += std::max(1u, o.tier_batch), ++batches) {
        std::vector<int> active;
        for (size_t k = at; k < std::min(cands.size(), at + std::max(1u, o.tier_batch)); ++k) {
          active.push_back(cands[k].lit);
        }
        // boxes held on their current tiles (2), then within one tile of them (1), then free (0)
        std::vector<char> held(B.size(), 2);
        std::vector<int>  hold(B.size(), 0);  // the literal holding each box this round
        Sat_status        st     = Sat_status::unknown;
        unsigned          rounds = 0;
        for (; rounds <= o.tier_rounds && !active.empty(); ++rounds) {
          if (Clock::now() >= deadline) {
            st = Sat_status::unknown;
            break;
          }
          std::vector<int> extra = active;
          for (size_t i = 0; i < B.size(); ++i) {
            const int t = boxes.boxes[B[i]].tile;
            hold[i]     = held[i] == 2 ? tile_lit(i, t) : held[i] == 1 ? near_lit(i, t) : 0;
            if (hold[i]) {
              extra.push_back(hold[i]);
            }
          }
          st = settle(extra);
          ++solves;
          if (st != Sat_status::unsatisfiable) {
            break;
          }
          size_t loosened = 0;  // the core: the held boxes in it get a tile of room, then go free
          for (size_t i = 0; i < B.size(); ++i) {
            if (hold[i] && cnf.solver.failed(hold[i])) {
              --held[i];
              ++loosened;
            }
          }
          if (loosened) {
            continue;
          }
          std::vector<int> rest;  // no box to free: the constraints in the core go
          for (int v : active) {
            if (!cnf.solver.failed(v)) {
              rest.push_back(v);
            }
          }
          if (rest.size() == active.size()) {
            rest.clear();
          }
          active.swap(rest);
        }
        rounds_total += rounds;
        if (st == Sat_status::satisfiable && !active.empty()) {  // the answer first: a clause invalidates the model
          decode_answer();
          for (int v : active) {
            cnf.add({v});
          }
          kept_n += active.size();
        } else {
          timed_out = st == Sat_status::unknown;
        }
      }
      rep.tier_kept     += kept_n;
      rep.tier_dropped  += cands.size() - kept_n;
      rep.tier_timeouts += timed_out;
      rep.tier_solves   += solves;
      std::fprintf(stderr,
                   "legroute sat region %d tier %s: kept %zu of %zu, batches %zu, rounds %u, solves %zu%s%s\n",
                   r,
                   name.c_str(),
                   kept_n,
                   cands.size(),
                   batches,
                   rounds_total,
                   solves,
                   timed_out ? ", out of time" : "",
                   tag.c_str());
    }
    rep.tier_cost_after += route_cost(crit_len);
    rep.tier_crit_after += crit_len;
  }
  rep.lazy_rounds  += lazy_rounds;
  rep.lazy_bounds  += lazy_added;
  rep.lazy_skipped += size_t(std::ranges::count_if(lazy, [](const Lazy& L) { return !L.xs.empty(); }));
  if (status == Sat_status::unsatisfiable && o.sat_relax_rounds) {
    // diagnostic: what would make the region routable. Drop the tile bounds of each UNSAT core until it solves; the
    // answer's demand on the dropped tiles over their tracks is the deficit (the region stays unrouted)
    std::vector<char> dropped(bounds.size(), 0);
    unsigned          round   = 0;
    Sat_status        relaxed = status;
    while (relaxed == Sat_status::unsatisfiable && round < o.sat_relax_rounds) {
      std::vector<size_t> core;
      for (size_t i = 0; i < bounds.size(); ++i) {
        if (!dropped[i] && cnf.solver.failed(-bounds[i].over)) {
          core.push_back(i);
        }
      }
      if (core.empty()) {  // not capacity: the placement itself
        break;
      }
      for (size_t i : core) {
        dropped[i] = 1;
      }
      assume.clear();
      for (size_t i = 0; i < bounds.size(); ++i) {
        if (!dropped[i]) {
          assume.push_back(-bounds[i].over);
        }
      }
      const auto left = budget.remaining_ms();
      if (!left) {
        break;
      }
      relaxed = cnf.solver.solve(assume, o.sat_conflicts, left);
      ++rep.sat_solves;
      ++round;
    }
    size_t        tiles = 0, short_h = 0, short_v = 0, deficit_h = 0, deficit_v = 0, worst = 0, demand = 0, tracks = 0;
    // where the over-capacity tiles are: on the region's edge, holding a fixed endpoint (virtual pin, hop, fixed cell),
    // under a fixed box; and their use split into routes (covering nets) and the occupant's in-box demand
    std::set<int> pin_tiles, fixed_tiles;
    for (const auto& e : eps) {
      if (e.box < 0) {
        pin_tiles.insert(e.tile);
      }
    }
    for (int b : B) {
      if (boxes.boxes[b].fixed) {
        fixed_tiles.insert(boxes.boxes[b].tile);
      }
    }
    std::map<std::pair<int, int>, size_t> routes_at;  // (tile, h) -> covering nets in the answer
    if (relaxed == Sat_status::satisfiable) {
      for (const auto& [key, lit] : cover) {
        routes_at[{std::get<1>(key), std::get<2>(key)}] += cnf.solver.value(lit) ? 1 : 0;
      }
    }
    size_t at_edge = 0, at_pin = 0, at_fixed = 0, over_routes = 0, over_inbox = 0;
    for (size_t i = 0; i < bounds.size(); ++i) {
      if (!dropped[i]) {
        continue;
      }
      ++tiles;
      if (relaxed != Sat_status::satisfiable) {
        continue;
      }
      size_t used = 0;
      for (int x : inputs[{bounds[i].t, bounds[i].h}]) {
        used += cnf.solver.value(x);
      }
      const size_t over = used > size_t(bounds[i].k) ? used - size_t(bounds[i].k) : 0;
      if (over > 0) {
        const int t    = bounds[i].t;
        bool      edge = false;
        for (int q : {g.col(t) > 0 ? t - 1 : -1, g.col(t) + 1 < g.cols ? t + 1 : -1, t - g.cols, t + g.cols}) {
          edge = edge || q < 0 || q >= int(g.size()) || g.region[q] != r;
        }
        at_edge         += edge;
        at_pin          += pin_tiles.contains(t);
        at_fixed        += fixed_tiles.contains(t);
        const size_t rt  = routes_at[{t, bounds[i].h}];
        over_routes     += rt;
        over_inbox      += used > rt ? used - rt : 0;
      }
      (bounds[i].h ? short_h : short_v)     += over > 0;
      (bounds[i].h ? deficit_h : deficit_v) += over;
      worst                                  = std::max(worst, over);
      demand                                += used;
      tracks                                += size_t(bounds[i].k);
    }
    rep.sat_relaxed_tiles  += tiles;
    rep.sat_deficit_tracks += deficit_h + deficit_v;
    std::fprintf(stderr,
                 "legroute sat region %d relax: rounds %u %s, relaxed bounds %zu, over capacity H %zu V %zu tiles, deficit "
                 "H %zu V %zu tracks (worst %zu), relaxed tiles demand %zu of %zu tracks; over tiles at the edge %zu, with a "
                 "fixed endpoint %zu, under a fixed box %zu, their use routes %zu in-box %zu\n",
                 r,
                 round,
                 relaxed == Sat_status::satisfiable     ? "sat"
                 : relaxed == Sat_status::unsatisfiable ? "still unsat"
                                                        : "unknown",
                 tiles,
                 short_h,
                 short_v,
                 deficit_h,
                 deficit_v,
                 worst,
                 demand,
                 tracks,
                 at_edge,
                 at_pin,
                 at_fixed,
                 over_routes,
                 over_inbox);
  }
  rep.sat_variables      += cnf.variables;
  rep.sat_clauses        += cnf.clauses;
  rep.sat_binding_tiles  += binding;
  rep.sat_regions        += 1;
  const double secs       = seconds_since(start);
  size_t       pass_nets  = 0;  // nets with no box in the region: hop to hop (or pin) runs the region only carries
  for (int n : nets_here) {
    pass_nets += std::ranges::none_of(ends[n].per_region.at(r), [](const Endpoint& e) { return e.box >= 0; });
  }
  std::fprintf(stderr,
               "legroute sat region %d: boxes %zu tiles %zu nets %zu (pass %zu) conns %zu vars %zu clauses %zu binding %zu solves "
               "%zu status %s %.2f s%s\n",
               r,
               B.size(),
               T.size(),
               nets_here.size(),
               pass_nets,
               conns.size(),
               cnf.variables,
               cnf.clauses,
               binding,
               rep.sat_solves,
               status == Sat_status::satisfiable     ? "sat"
               : status == Sat_status::unsatisfiable ? "unsat"
                                                     : "unknown",
               secs,
               tag.c_str());
  rep.sat_seconds_max = std::max(rep.sat_seconds_max, secs);
  if (status != Sat_status::satisfiable) {
    ++(out.skipped                           ? rep.sat_skipped_regions
       : status == Sat_status::unsatisfiable ? rep.sat_unsat_regions
                                             : rep.sat_unknown_regions);
    // the region keeps its boxes on fitting tiles, nearest first (not routable by construction)
    const auto tiles = !budget.remaining_ms() ? budget.fallback : fallback_tiles([&](size_t i) {
      std::vector<int> list;
      for (const auto& [t, _] : tiles_of[i]) {
        list.push_back(t);
      }
      return list;
    });
    for (size_t i = 0; i < B.size(); ++i) {
      if (tiles[i] >= 0) {
        boxes.boxes[B[i]].tile = tiles[i];
      }
    }
    for (int b : B) {
      out.tiles.emplace_back(b, boxes.boxes[b].tile);
    }
    return;
  }
  rep.sat_adjacent_nets += adjacent_nets;
  rep.sat_seconds       += secs;
  for (int b : B) {
    out.tiles.emplace_back(b, boxes.boxes[b].tile);
  }
}

void solve_region_once(const Design& d, const Legroute_options& o, const Grid& g, int r, const std::vector<int>& B,
                       const std::vector<int>& T, Boxes& boxes, const std::vector<Net_ends>& ends,
                       const std::set<std::pair<int, int>>& treed, const std::unordered_map<int, std::vector<int>>& allowed,
                       const std::vector<char>& pinned_tile, const std::string& tag, const std::vector<int>& cap_h0,
                       const std::vector<int>& cap_v0, const Edge_reserve& reserve, const std::vector<std::vector<int>>& hop_tiles,
                       const std::vector<int>& hop_room, Region_out& out, Clock::time_point shared_deadline) {
  const auto    start = Clock::now();
  Region_budget budget{start, std::min(shared_deadline, start + std::chrono::milliseconds(o.sat_ms))};
  for (int b : B) {
    const auto& box = boxes.boxes[size_t(b)];
    budget.fallback.push_back(box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy));
  }
  std::optional<Cnf> cnf;
  try {
    budget.check(true);
    cnf.emplace(budget);
    solve_region_model(d,
                       o,
                       g,
                       r,
                       B,
                       T,
                       boxes,
                       ends,
                       treed,
                       allowed,
                       pinned_tile,
                       tag,
                       cap_h0,
                       cap_v0,
                       reserve,
                       hop_tiles,
                       hop_room,
                       out,
                       budget,
                       *cnf);
  } catch (const Region_budget_expired&) {
    // Setup expiry is UNKNOWN, never density evidence. A later optimization
    // timeout may retain only an already decoded, capacity-checked answer;
    // incoming or merely matched placement is not a routing witness.
    if (budget.decoded) {
      out.status = Sat_status::satisfiable;
      ++out.rep.tier_timeouts;
    } else {
      if (out.status != Sat_status::unsatisfiable) {
        out.status      = Sat_status::unknown;
        out.unfit_boxes = 0;  // incomplete matching is not an area-deficit proof
        out.unfit.clear();
      }
      out.conns.clear();
      out.local.clear();
      out.hop_tiles.clear();
      out.hop_choices.clear();
      for (size_t i = 0; i < B.size(); ++i) {
        boxes.boxes[size_t(B[i])].tile = budget.fallback[i];
      }
    }
    out.tiles.clear();
    for (int b : B) {
      out.tiles.emplace_back(b, boxes.boxes[size_t(b)].tile);
    }
    auto& rep       = out.rep;
    rep.sat_regions = std::max<size_t>(1, rep.sat_regions);
    if (out.status == Sat_status::unknown) {
      if (out.skipped) {
        rep.sat_skipped_regions = std::max<size_t>(1, rep.sat_skipped_regions);
      } else {
        rep.sat_unknown_regions = std::max<size_t>(1, rep.sat_unknown_regions);
      }
    }
    if (out.status == Sat_status::unsatisfiable) {
      rep.sat_unsat_regions = std::max<size_t>(1, rep.sat_unsat_regions);
    }
    if (cnf) {
      rep.sat_variables = std::max(rep.sat_variables, cnf->variables);
      rep.sat_clauses   = std::max(rep.sat_clauses, cnf->clauses);
    }
    const auto seconds  = seconds_since(start);
    rep.sat_seconds_max = std::max(rep.sat_seconds_max, seconds);
    std::fprintf(stderr,
                 "legroute sat region %d: shared attempt deadline, status %s, %zu solves, %.3f s%s\n",
                 r,
                 budget.decoded                            ? "sat retained"
                 : out.status == Sat_status::unsatisfiable ? "unsat retained"
                                                           : "unknown",
                 rep.sat_solves,
                 seconds,
                 tag.c_str());
  } catch (...) {
    // Keep construction-work telemetry for an optional proposal that reaches
    // a recognized template limitation. Preserve the original exception.
    if (cnf) {
      out.rep.sat_variables = std::max(out.rep.sat_variables, cnf->variables);
      out.rep.sat_clauses   = std::max(out.rep.sat_clauses, cnf->clauses);
    }
    throw;
  }
  const auto status_name = [](const std::optional<Sat_status>& status) {
    return !status                                ? "not-run"
           : *status == Sat_status::satisfiable   ? "sat"
           : *status == Sat_status::unsatisfiable ? "unsat"
                                                  : "unknown";
  };
  std::fprintf(stderr,
               "legroute sat region %d timing: build %.3f s (complete %d), initial model %s %.3f s, seed %s, "
               "capacity_lazy %u, total %.3f s%s; initial model includes fixed and any eager constraints\n",
               r,
               budget.build_seconds >= 0 ? budget.build_seconds : seconds_since(start),
               budget.build_seconds >= 0,
               status_name(budget.initial_status),
               budget.initial_seconds,
               status_name(budget.seed_status),
               o.capacity_lazy,
               seconds_since(start),
               tag.c_str());
}

// Independent acceptance of an optional restricted proposal. The full graph's
// domains and every per-part corridor remain authoritative; a rejected proposal
// says nothing about their satisfiability.
bool validate_corridor_seed(const Design& d, const Legroute_options& o, const Grid& g, int r, const std::vector<int>& B,
                            const std::vector<int>& T, const Boxes& original, const std::vector<Net_ends>& ends,
                            const std::set<std::pair<int, int>>& treed, const std::vector<char>& pinned_tile,
                            const Edge_reserve& reserve, const std::vector<int>& cap_h0, const std::vector<int>& cap_v0,
                            Region_out& candidate, Region_budget& budget) {
  budget.check(true);
  if (candidate.status != Sat_status::satisfiable || candidate.skipped || !candidate.local.empty() || !candidate.hop_choices.empty()
      || !candidate.hop_tiles.empty() || candidate.tiles.size() != B.size()) {
    return false;
  }
  const std::set<int> here(T.begin(), T.end()), members(B.begin(), B.end());
  std::map<int, int>  placement;
  std::set<int>       occupied;
  for (const auto& [b, t] : candidate.tiles) {
    budget.check();
    if (!members.contains(b) || !here.contains(t) || !occupied.insert(t).second || !placement.emplace(b, t).second
        || g.blocked[size_t(t)]) {
      return false;
    }
    const auto& box    = original.boxes[size_t(b)];
    const auto  domain = candidate.seed_box_domains.find(b);
    if ((box.fixed && box.tile != t) || (!box.fixed && !pinned_tile.empty() && pinned_tile[size_t(t)]) || !box_fits(d, g, box, t)
        || domain == candidate.seed_box_domains.end() || std::ranges::find(domain->second, t) == domain->second.end()) {
      return false;
    }
  }
  std::map<int, std::set<std::pair<int, int>>> edges;
  const auto                                   take = [&](int n, int a, int b) {
    budget.check();
    if (n < 0 || size_t(n) >= ends.size() || !here.contains(a) || !here.contains(b)) {
      return false;
    }
    edges[n].emplace(std::min(a, b), std::max(a, b));
    return true;
  };
  for (const auto& pc : candidate.conns) {
    budget.check();
    if (pc.net < 0 || size_t(pc.net) >= ends.size() || !here.contains(pc.ta) || !here.contains(pc.tb)) {
      return false;
    }
    const int hr = g.row(pc.h_first ? pc.ta : pc.tb), vc = g.col(pc.h_first ? pc.tb : pc.ta);
    for (int c = std::min(g.col(pc.ta), g.col(pc.tb)); c < std::max(g.col(pc.ta), g.col(pc.tb)); ++c) {
      if (!take(pc.net, g.tile(c, hr), g.tile(c + 1, hr))) {
        return false;
      }
    }
    for (int y = std::min(g.row(pc.ta), g.row(pc.tb)); y < std::max(g.row(pc.ta), g.row(pc.tb)); ++y) {
      if (!take(pc.net, g.tile(vc, y), g.tile(vc, y + 1))) {
        return false;
      }
    }
  }
  size_t                                       checked = 0, selected_edges = 0;
  std::map<int, std::set<std::pair<int, int>>> permitted_edges;
  for (size_t n = 0; n < ends.size(); ++n) {
    budget.check();
    if (!ends[n].refine_corridor || treed.contains({int(n), r})) {
      continue;
    }
    const auto found = ends[n].per_region.find(r);
    if (found == ends[n].per_region.end()) {
      continue;
    }
    auto& permitted_union = permitted_edges[int(n)];
    for (const auto& [part, group] : endpoint_groups(found->second)) {
      const auto forest = ends[n].coarse_parts.find({r, part});
      if (forest == ends[n].coarse_parts.end() || forest->second.empty()) {
        throw std::logic_error("legroute seed validation has missing coarse part");
      }
      const auto       corridor = coarse_corridor(g, r, forest->second, o.window_tiles);
      std::vector<int> terminals;
      for (const auto& endpoint : group) {
        if (endpoint.hop >= 0) {
          return false;  // fixed interfaces in this first fast path
        }
        const int t = endpoint.box >= 0 ? placement.at(endpoint.box) : endpoint.tile;
        if (!corridor.contains(t)) {
          return false;
        }
        terminals.push_back(t);
      }
      std::map<int, std::vector<int>> adjacent;
      for (const auto& [a, b] : edges[int(n)]) {
        budget.check();
        if (!corridor.contains(a) || !corridor.contains(b)) {
          continue;
        }
        if (!o.window_tiles) {
          const auto seed = ends[n].coarse_edges.find({r, part});
          if (seed == ends[n].coarse_edges.end() || !seed->second.contains({a, b})) {
            continue;
          }
        }
        permitted_union.emplace(a, b);
        adjacent[a].push_back(b);
        adjacent[b].push_back(a);
      }
      if (!terminals.empty()) {
        std::set<int>    seen{terminals.front()};
        std::vector<int> pending{terminals.front()};
        for (size_t i = 0; i < pending.size(); ++i) {
          budget.check();
          for (int q : adjacent[pending[i]]) {
            if (seen.insert(q).second) {
              pending.push_back(q);
            }
          }
        }
        if (!std::ranges::all_of(terminals, [&](int t) { return seen.contains(t); })) {
          return false;
        }
      }
      ++checked;
    }
    selected_edges += edges[int(n)].size();
  }
  // A valid net id is not enough: nets without an applicable corridor may
  // contribute only geometry that was already immutable in the full model.
  for (const auto& [n, es] : edges) {
    for (const auto& [a, b] : es) {
      if (!permitted_edges[n].contains({a, b}) && !reserve.edges.contains({n, a, int(g.row(a) == g.row(b))})) {
        return false;
      }
    }
  }
  // Explicitly recount actual distinct-net edges against the original reserve,
  // not the proposal's privately reintroduced backbone demand.
  auto used_h = reserve.h, used_v = reserve.v;
  for (const auto& [n, es] : edges) {
    for (const auto& [a, b] : es) {
      budget.check();
      const bool h = g.row(a) == g.row(b);
      if (!reserve.edges.contains({n, a, int(h)})) {
        ++(h ? used_h : used_v)[size_t(a)];
      }
    }
  }
  for (int t : T) {
    for (bool h : {true, false}) {
      budget.check();
      const int q = h ? (g.col(t) + 1 < g.cols ? t + 1 : -1) : (g.row(t) + 1 < g.rows ? t + g.cols : -1);
      if (!here.contains(q)) {
        continue;
      }
      const auto& cap = h ? cap_h0 : cap_v0;
      if ((h ? used_h : used_v)[size_t(t)] > std::max(0, edge_limit(std::min(cap[size_t(t)], cap[size_t(q)]), o))) {
        return false;
      }
    }
  }
  validate_region_edges(g, r, candidate.conns, reserve, cap_h0, cap_v0, o, &budget);
  // Publish only canonical actual edges, including the proposal's original
  // forest, once per net. The caller must never append refinable seed pieces.
  candidate.conns.clear();
  for (const auto& [n, es] : edges) {
    for (const auto& [a, b] : es) {
      candidate.conns.push_back({n, a, b, g.row(a) == g.row(b)});
    }
  }
  candidate.rep.corridor_components         = checked;
  candidate.rep.corridor_checked_components = checked;
  candidate.rep.corridor_selected_edges     = selected_edges;
  budget.check(true);
  return true;
}

void solve_region(const Design& d, const Legroute_options& o, const Grid& g, int r, const std::vector<int>& B,
                  const std::vector<int>& T, Boxes& boxes, const std::vector<Net_ends>& ends,
                  const std::set<std::pair<int, int>>& treed, const std::unordered_map<int, std::vector<int>>& allowed,
                  const std::vector<char>& pinned_tile, const std::string& tag, const std::vector<int>& cap_h0,
                  const std::vector<int>& cap_v0, const Edge_reserve& reserve, const std::vector<std::vector<int>>& hop_tiles,
                  const std::vector<int>& hop_room, Region_out& out, Clock::time_point shared_deadline,
                  const Boxes* route_seed = nullptr) {
  const auto     start    = Clock::now();
  const auto     deadline = std::min(shared_deadline, start + std::chrono::milliseconds(o.sat_ms));
  const auto     left     = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - start).count();
  const unsigned seed_ms  = left > 0 ? unsigned(std::min<long long>(1000, left / 4)) : 0;
  bool eligible = movable_route_partition(o) && o.window_tiles > 0 && seed_ms && !out.skipped && !o.untracked && !o.sat_relax_rounds
                  && !o.sat_min_fill;
  bool has_corridor = false;
  for (const auto& net : ends) {
    const auto region = net.per_region.find(r);
    if (!net.refine_corridor || region == net.per_region.end()) {
      continue;
    }
    has_corridor = true;
    for (const auto& endpoint : region->second) {
      if (endpoint.hop >= 0) {
        eligible = false;
      }
    }
  }
  Legroute_report seed_report;
  if (eligible && has_corridor) {
    const auto    seed_start = Clock::now();
    Region_budget proposal_budget{seed_start, std::min(deadline, start + std::chrono::milliseconds(seed_ms))};
    Region_out    candidate;
    candidate.capture_seed_domains = true;
    ++seed_report.corridor_seed_attempts;
    try {
      // Only B-owned fields may be read while neighboring region workers mutate
      // their own .tile fields. Other entries remain inaccessible sentinels.
      Boxes proposed;
      proposed.boxes.resize(boxes.boxes.size());
      for (int b : B) {
        proposal_budget.check();
        proposed.boxes[size_t(b)] = boxes.boxes[size_t(b)];
      }
      std::map<int, std::vector<int>> point_domains;
      bool                            point_proposal = false;
      if (route_seed && route_seed->boxes.size() == boxes.boxes.size()) {
        // The point proposal is private. Its domain certificate comes from the
        // original boxes, never from the singleton domains used to test it.
        const auto domains
            = prepare_box_domains(d, o, g, r, B, T, proposed, allowed, pinned_tile, [&] { proposal_budget.check(true); });
        bool valid = domains.matching.size() == B.size();
        for (size_t i = 0; valid && i < B.size(); ++i) {
          const int b = B[i], t = route_seed->boxes[size_t(b)].tile;
          auto candidates = box_domain_tiles(d, g, b, domains.windows[i], proposed, allowed, domains.lifted, pinned_tile, T, [&] {
            proposal_budget.check(true);
          });
          valid           = std::ranges::find(candidates, t) != candidates.end();
          point_domains.emplace(b, std::move(candidates));
        }
        if (valid) {
          point_proposal = true;
          for (int b : B) {
            proposed.boxes[size_t(b)].tile  = route_seed->boxes[size_t(b)].tile;
            proposed.boxes[size_t(b)].fixed = true;
          }
        } else {
          point_domains.clear();  // ordinary fixed-backbone proposal remains available
        }
      }
      auto seed_options = o;
      seed_options.tiers.clear();
      seed_options.sat_ms = seed_ms;
      if (point_proposal) {
        // Verify the supplied point, including its interface attachments.
        // Reattachment choices can add useless cycles to an already valid
        // forest. The complete fallback retains the original corridor radius.
        seed_options.window_tiles = 0;
      }
      std::vector<Net_ends>               seed_ends(ends.size());
      auto                                seed_reserve = reserve;
      std::set<std::tuple<int, int, int>> backbone;
      for (size_t n = 0; n < ends.size(); ++n) {
        proposal_budget.check();
        const auto region = ends[n].per_region.find(r);
        if (region == ends[n].per_region.end()) {
          continue;
        }
        auto& seed = seed_ends[n];
        seed.per_region.emplace(r, region->second);
        seed.preserve_coarse = ends[n].preserve_coarse;
        seed.driver_region   = ends[n].driver_region;
        for (const auto& [key, tiles] : ends[n].coarse_parts) {
          if (key.first == r) {
            seed.coarse_parts.emplace(key, tiles);
          }
        }
        for (const auto& [key, tiles] : ends[n].coarse_junctions) {
          if (key.first == r) {
            seed.coarse_junctions.emplace(key, tiles);
          }
        }
        if (!ends[n].refine_corridor || treed.contains({int(n), r})) {
          continue;
        }
        for (const auto& [key, edges] : ends[n].coarse_edges) {
          if (key.first != r) {
            continue;
          }
          for (const auto& [a, b] : edges) {
            proposal_budget.check();
            if (a < 0 || b <= a || size_t(b) >= g.size() || g.region[size_t(a)] != r || g.region[size_t(b)] != r
                || std::abs(g.col(a) - g.col(b)) + std::abs(g.row(a) - g.row(b)) != 1) {
              throw std::logic_error("legroute seed contains an invalid original edge");
            }
            const int h = g.row(a) == g.row(b);
            backbone.emplace(int(n), a, b);
            if (seed_reserve.edges.emplace(int(n), a, h).second) {
              ++(h ? seed_reserve.h : seed_reserve.v)[size_t(a)];
            }
            seed_reserve.cuts[{int(n), r, h}].insert((h ? g.col(a) : g.row(a)) + 1);
          }
        }
      }
      proposal_budget.check(true);
      solve_region_once(d,
                        seed_options,
                        g,
                        r,
                        B,
                        T,
                        proposed,
                        seed_ends,
                        treed,
                        allowed,
                        pinned_tile,
                        tag + " corridor-proposal",
                        cap_h0,
                        cap_v0,
                        seed_reserve,
                        hop_tiles,
                        hop_room,
                        candidate,
                        proposal_budget.deadline);
      if (candidate.status == Sat_status::satisfiable) {
        if (!point_domains.empty()) {
          candidate.seed_box_domains = std::move(point_domains);
        }
        for (const auto& [n, a, b] : backbone) {
          candidate.conns.push_back({n, a, b, g.row(a) == g.row(b)});
        }
        if (validate_corridor_seed(d,
                                   o,
                                   g,
                                   r,
                                   B,
                                   T,
                                   boxes,
                                   ends,
                                   treed,
                                   pinned_tile,
                                   reserve,
                                   cap_h0,
                                   cap_v0,
                                   candidate,
                                   proposal_budget)) {
          for (const auto& [b, t] : candidate.tiles) {
            boxes.boxes[size_t(b)].tile = t;
          }
          ++seed_report.corridor_seed_successes;
        }
      }
    } catch (const Region_budget_expired&) {
      // Proposal setup/validation uses the same small allowance; no partial
      // answer or narrowed domains survive into the complete model.
    } catch (const Coarse_stub_unsupported&) {
    } catch (const Legroute_unfit&) {
    }
    seed_report.corridor_seed_seconds   = seconds_since(seed_start);
    seed_report.corridor_seed_solves    = candidate.rep.sat_solves;
    seed_report.corridor_seed_variables = candidate.rep.sat_variables;
    seed_report.corridor_seed_clauses   = candidate.rep.sat_clauses;
    std::fprintf(stderr,
                 "legroute sat region %d corridor proposal: %s, %zu solves, %.3f s, allowance %u ms%s\n",
                 r,
                 seed_report.corridor_seed_successes ? "verified" : "inconclusive; full graph follows",
                 seed_report.corridor_seed_solves,
                 seed_report.corridor_seed_seconds,
                 seed_ms,
                 tag.c_str());
    if (seed_report.corridor_seed_successes) {
      candidate.capture_seed_domains = false;
      candidate.seed_box_domains.clear();
      out = std::move(candidate);
    }
  }
  if (!seed_report.corridor_seed_successes) {
    solve_region_once(d,
                      o,
                      g,
                      r,
                      B,
                      T,
                      boxes,
                      ends,
                      treed,
                      allowed,
                      pinned_tile,
                      tag,
                      cap_h0,
                      cap_v0,
                      reserve,
                      hop_tiles,
                      hop_room,
                      out,
                      deadline);
  }
  out.rep.corridor_seed_attempts  += seed_report.corridor_seed_attempts;
  out.rep.corridor_seed_successes += seed_report.corridor_seed_successes;
  out.rep.corridor_seed_solves    += seed_report.corridor_seed_solves;
  out.rep.corridor_seed_variables += seed_report.corridor_seed_variables;
  out.rep.corridor_seed_clauses   += seed_report.corridor_seed_clauses;
  out.rep.corridor_seed_seconds   += seed_report.corridor_seed_seconds;
  out.rep.sat_seconds_max          = std::max(out.rep.sat_seconds_max, seconds_since(start));
}

// The routed nets' endpoints per region (boxes, fixed tiles) and the hops that join their regions: one per fixed
// virtual pin, then chains through the regions between those the pins leave unjoined. The hops take their tracks.
struct Crossings {
  std::vector<Net_ends>               ends;
  std::vector<std::pair<int, int>>    hops;  // tile pairs across region boundaries
  std::vector<int>                    hop_net;
  std::set<std::pair<int, int>>       retained;        // complete (net, region) pieces, including parts with no interior edge
  std::set<std::tuple<int, int, int>> retained_parts;  // (net, region, part), only pass-through components
  size_t                              unjoined   = 0;
  size_t                              over_share = 0;  // hops placed past hop_share on every tile of their boundary
  std::vector<Global_piece>           pieces;          // legroute.global: the runs fixed ahead through pass-through regions
  size_t                              global_nets     = 0;
  size_t                           global_unrouted = 0, global_full = 0;  // global_route: nets without a path, edges at the share
  size_t                              global_topology_disconnected = 0;
  size_t                           global_overflow_edges = 0, global_overflow_tracks = 0;  // edges over the share after the rounds
  unsigned                         global_rounds = 0;
  std::vector<std::pair<int, int>> global_over;  // those edges (lower / left tile, h): where the cells should give way
  double                              global_seconds     = 0;
  size_t                              global_search_pops = 0, global_search_limited = 0;
  size_t                              pattern_attempts = 0, pattern_completed = 0, pattern_fallbacks = 0;
  size_t                              pattern_work = 0, pattern_work_limited = 0;
};
Crossings crossings(const Design& d, const Grid& g, const Boxes& boxes, const Legroute_regions& regions, std::vector<int>& cap_h,
                    std::vector<int>& cap_v, bool diagnose, const Global_routes* all_routes = nullptr) {
  Crossings                     out;
  auto&                         ends    = out.ends;
  auto&                         hops    = out.hops;
  auto&                         hop_net = out.hop_net;
  std::vector<std::vector<int>> net_hops(d.nets.size());  // per net: its hops (a scan of every hop per net was quadratic)
  const auto                    add_hop = [&](int n, int a, int b) {
    const int id = int(hops.size());
    net_hops[n].push_back(id);
    hops.emplace_back(a, b);
    hop_net.push_back(n);
    return id;
  };
  const bool preserve_coarse = movable_route_partition(d.opt);
  if (preserve_coarse && !all_routes) {
    throw std::runtime_error("legroute coarse witness repair required: movable regional routing has no admitted global forest");
  }
  const auto box_of_comp = [&](int comp) { return comp >= 0 && d.cell[comp] >= 0 ? boxes.box_of[d.cell[comp]] : -1; };
  ends.resize(d.nets.size());
  std::unordered_map<size_t, std::vector<std::pair<double, double>>> fixed_pins;
  if (d.opt.region_pins) {
    for (const auto& p : regions.pins) {
      fixed_pins[p.net].emplace_back(p.x, p.y);
    }
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    if (!N.routed || N.clock) {
      continue;
    }
    auto& E = ends[n];
    for (size_t k = 0; k < N.pins.size(); ++k) {
      const auto& p   = N.pins[k];
      const int   box = box_of_comp(p.comp);
      Endpoint    e;
      int         region;
      if (box >= 0) {
        e.box  = box;
        region = boxes.boxes[box].region;
      } else {
        e.tile = physical_pin_access(d, g, p).tile;
        region = g.region[e.tile];
      }
      if (int(k) == N.driver) {
        E.driver_region = region;
      }
      auto& list = E.per_region[region];
      if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.box == e.box && o.tile == e.tile; })) {
        // the driver first
        if (int(k) == N.driver) {
          list.insert(list.begin(), e);
        } else {
          list.push_back(e);
        }
      }
    }
    if (E.per_region.size() < 2) {
      continue;
    }
    // region crossings: each fixed virtual pin is a hop between the two tiles beside it; the hop's tiles are fixed
    // endpoints in their regions
    std::set<std::pair<int, int>> joined;  // region pairs with a hop
    if (const auto it = fixed_pins.find(N.frame); it != fixed_pins.end()) {
      for (const auto& [x, y] : it->second) {
        const int t0 = g.tile_of(x, y);
        int       bu = -1, bv = -1;
        double    best = INFINITY;
        for (int t : {t0, t0 - 1, t0 + 1, t0 - g.cols, t0 + g.cols}) {
          if (t < 0 || t >= int(g.size())) {
            continue;
          }
          for (int q : {t + 1, t + g.cols}) {
            if (q >= int(g.size()) || (q == t + 1 && g.col(q) == 0) || g.region[q] == g.region[t]
                || !E.per_region.contains(g.region[t]) || !E.per_region.contains(g.region[q])) {
              continue;
            }
            const double mx = (g.cx(g.col(t)) + g.cx(g.col(q))) / 2.0, my = (g.cy(g.row(t)) + g.cy(g.row(q))) / 2.0;
            const double dd = std::abs(mx - x) + std::abs(my - y);
            if (dd < best) {
              best = dd;
              bu   = t;
              bv   = q;
            }
          }
        }
        if (bu < 0) {
          continue;
        }
        for (int t : {bu, bv}) {
          auto& list = E.per_region[g.region[t]];
          if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.tile == t; })) {
            list.push_back({-1, t});
          }
        }
        add_hop(int(n), bu, bv);
        joined.emplace(std::min(g.region[bu], g.region[bv]), std::max(g.region[bu], g.region[bv]));
      }
    }
  }
  if (diagnose) {  // diagnostic: 1:n nets (n distinct sink endpoints) and the boxes they touch
    const std::array<int, 6>     fan{2, 3, 4, 6, 8, 16};
    std::array<size_t, 6>        nets{};
    std::array<std::set<int>, 6> touched;
    for (size_t n = 0; n < d.nets.size(); ++n) {
      const auto& N = d.nets[n];
      if (!N.routed || N.clock) {
        continue;
      }
      std::set<int> bs;
      std::set<int> tiles;
      for (const auto& p : N.pins) {
        const int box = box_of_comp(p.comp);
        if (box >= 0) {
          bs.insert(box);
        } else if (p.term >= 0) {
          tiles.insert(g.tile_of(double(d.fp.terminals[p.term].x), double(d.fp.terminals[p.term].y)));
        } else if (p.comp >= 0) {
          tiles.insert(g.tile_of(double(d.fp.components[p.comp].place.x), double(d.fp.components[p.comp].place.y)));
        }
      }
      const int sinks = int(bs.size() + tiles.size()) - 1;
      for (size_t k = 0; k < fan.size(); ++k) {
        if (sinks >= fan[k]) {
          ++nets[k];
          touched[k].insert(bs.begin(), bs.end());
        }
      }
    }
    for (size_t k = 0; k < fan.size(); ++k) {
      std::fprintf(stderr,
                   "legroute fanout 1:n n>=%d: nets %zu boxes %zu of %zu (%.1f%%)\n",
                   fan[k],
                   nets[k],
                   touched[k].size(),
                   boxes.boxes.size(),
                   100.0 * double(touched[k].size()) / double(std::max<size_t>(1, boxes.boxes.size())));
    }
  }
  // nets whose regions the fixed virtual pins do not join: a hop at a shared boundary, chained through the regions
  // between them along the shortest region path to the driver's region (critical nets first). A hop takes the
  // boundary edge nearest to its net's pins among those with hop room left (half the tracks of the direction).
  std::map<std::pair<int, int>, std::vector<std::pair<int, int>>> edges;  // (r, s), r < s -> tile pairs (in r, in s)
  std::vector<std::set<int>>                                      adj(g.regions);
  for (int t = 0; t < int(g.size()); ++t) {
    for (int q : {g.col(t) + 1 < g.cols ? t + 1 : -1, t + g.cols < int(g.size()) ? t + g.cols : -1}) {
      if (q < 0 || g.region[q] == g.region[t]) {
        continue;
      }
      const int a = g.region[t], b = g.region[q];
      if (a < b) {
        edges[{a, b}].emplace_back(t, q);
      } else {
        edges[{b, a}].emplace_back(q, t);
      }
      adj[a].insert(b);
      adj[b].insert(a);
    }
  }
  std::vector<std::array<int, 2>> hop_use(g.size(), {0, 0});  // per tile: H, V hops
  // hop_spread: the straight runs of pass-through nets (hop to hop inside a region) per tile edge, H right of the
  // tile and V above it, so the next pass-through takes a row or column with room instead of the same one
  std::vector<std::array<int, 2>> through(g.size(), {0, 0});
  std::vector<size_t>             order;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    if (ends[n].per_region.size() >= 2 || (all_routes && !ends[n].per_region.empty())) {
      order.push_back(n);
    }
  }
  std::ranges::stable_sort(order, [&](size_t a, size_t b) { return d.nets[a].crit > d.nets[b].crit; });
  const auto centroid = [&](const std::vector<Endpoint>& list) {
    double x = 0, y = 0;
    for (const auto& e : list) {
      const int t  = e.box >= 0 ? g.tile_of(boxes.boxes[e.box].cx, boxes.boxes[e.box].cy) : e.tile;
      x           += double(g.cx(g.col(t)));
      y           += double(g.cy(g.row(t)));
    }
    return std::pair{x / double(list.size()), y / double(list.size())};
  };
  // legroute.global: route crossing nets with their actual fixed interfaces. A failed secondary route returns
  // to planning before fallback chains or any regional SAT model can consume it.
  Global_routes       GR;
  std::vector<size_t> gr_index(d.nets.size(), SIZE_MAX);
  if (all_routes) {
    for (const size_t n : order) {
      gr_index[n] = n;
    }
    out.global_unrouted              = all_routes->unrouted;
    out.global_topology_disconnected = all_routes->topology_disconnected;
    out.global_full                  = all_routes->edges_full;
    out.global_overflow_edges        = all_routes->overflow_edges;
    out.global_overflow_tracks       = all_routes->overflow_tracks;
    out.global_rounds                = all_routes->rounds;
    out.global_over                  = all_routes->over;
    // The owner accounts for router time once; recutting crossings performs no global search.
  } else if (d.opt.global_route) {
    std::vector<Global_net> gnets;
    for (const size_t n : order) {
      Global_net  gn;
      const auto& E = ends[n];
      gn.net        = int(n);
      gn.crit       = d.nets[n].crit;
      std::set<int> seen;
      const auto    push = [&](const Endpoint& e) {
        const int t = e.box >= 0 ? g.tile_of(boxes.boxes[e.box].cx, boxes.boxes[e.box].cy) : e.tile;
        if (seen.insert(t).second) {
          gn.endpoints.push_back(t);
        }
      };
      if (const auto dr = E.per_region.find(E.driver_region); dr != E.per_region.end()) {
        for (const auto& e : dr->second) {
          push(e);
        }
      }
      for (const auto& [r, list] : E.per_region) {
        for (const auto& e : list) {
          push(e);
        }
      }
      std::set<int> pin_hops;  // the fixed virtual pins' hop tiles: not the net's own pins
      for (int k : net_hops[n]) {
        gn.fixed_edges.push_back(hops[k]);
        pin_hops.insert(hops[k].first);
        pin_hops.insert(hops[k].second);
      }
      for (const auto& [r, list] : E.per_region) {
        if (std::ranges::any_of(list, [&](const Endpoint& e) { return e.box >= 0 || !pin_hops.contains(e.tile); })) {
          gn.own_regions.push_back(r);
        }
      }
      gr_index[n] = gnets.size();
      gnets.push_back(std::move(gn));
    }
    GR                               = global_route(g,
                                                    cap_h,
                                                    cap_v,
                                                    d.opt.global_share,
                                                    d.opt.global_own_share,
                                                    gnets,
                                                    d.opt.global_margin,
                                                    d.opt.global_rounds,
                                                    d.opt.global_net_pops,
                                                    d.opt.global_astar_weight,
                                                    d.opt.global_pattern);
    // The initial all-signal screen did not include these fixed virtual IOs or
    // own-region route choices. Screen the actual secondary problem as well.
    // Fixed hops are emitted even when the router connects their endpoints by
    // another path, so include their union exactly once per net and edge.
    auto             screened_routes = GR;
    std::vector<int> tree_nets;
    for (size_t k = 0; k < gnets.size(); ++k) {
      tree_nets.push_back(gnets[k].net);
      if (!gnets[k].fixed_edges.empty()) {
        auto&                         tree = screened_routes.trees[k];
        std::set<std::pair<int, int>> net_edges(tree.edges.begin(), tree.edges.end());
        for (const auto& [a, b] : gnets[k].fixed_edges) {
          net_edges.emplace(std::min(a, b), std::max(a, b));
        }
        tree.edges.assign(net_edges.begin(), net_edges.end());
      }
    }
    // Recompute share overflow for the union using the secondary router's
    // accounting policy. Own-region interiors remain exempt when own_share is
    // zero; the independent raw 95% screen below still checks every edge.
    std::vector<int> screened_h(g.size()), screened_v(g.size());
    for (size_t k = 0; k < gnets.size(); ++k) {
      for (const auto& [a, b] : screened_routes.trees[k].edges) {
        if (d.opt.global_own_share > 0 || g.region[a] != g.region[b]
            || std::ranges::find(gnets[k].own_regions, g.region[a]) == gnets[k].own_regions.end()) {
          ++(g.row(a) == g.row(b) ? screened_h : screened_v)[size_t(a)];
        }
      }
    }
    screened_routes.edges_full = screened_routes.overflow_edges = screened_routes.overflow_tracks = 0;
    screened_routes.over.clear();
    for (size_t t = 0; t < g.size(); ++t) {
      for (bool horizontal : {true, false}) {
        const bool exists = horizontal ? g.col(int(t)) + 1 < g.cols : g.row(int(t)) + 1 < g.rows;
        if (!exists) {
          continue;
        }
        const size_t q              = t + (horizontal ? 1 : size_t(g.cols));
        const auto&  cap            = horizontal ? cap_h : cap_v;
        const int    limit          = int(std::floor(d.opt.global_share * double(std::min(cap[t], cap[q]))));
        const int    use            = (horizontal ? screened_h : screened_v)[t];
        screened_routes.edges_full += limit > 0 && use >= limit;
        if (use > limit) {
          ++screened_routes.overflow_edges;
          screened_routes.overflow_tracks += size_t(use - limit);
          screened_routes.over.emplace_back(int(t), horizontal);
        }
      }
    }
    std::vector<char> hot;
    auto              gates = assess_legroute_gates(d, g, &boxes, screened_routes, tree_nets, cap_h, cap_v, false, &hot);
    gates.stage             = Legroute_gate_report::Stage::secondary;
    if (gates.unrouted || gates.routing_dense_edges || gates.overflow_tracks) {
      // Generic failed-net feedback includes physical pins. These requests also
      // contain virtual anchors, which must appear in the repair footprint.
      for (size_t k = 0; k < gnets.size(); ++k) {
        if (GR.trees[k].routed) {
          continue;
        }
        for (int tile : gnets[k].endpoints) {
          if (!hot[size_t(tile)]) {
            hot[size_t(tile)] = true;
            gates.hotspots.push_back({double(g.left(g.col(tile))),
                                      double(g.bottom(g.row(tile))),
                                      double(g.left(g.col(tile)) + g.tile_w),
                                      double(g.bottom(g.row(tile)) + g.tile_h)});
          }
        }
      }
      if (!d.opt.output_dir.empty()) {
        std::filesystem::create_directories(d.opt.output_dir);
        std::ofstream report(d.opt.output_dir + "/pre_sat.json");
        report << legroute_gate_json(gates) << '\n';
        if (!report) {
          throw std::runtime_error("legroute: cannot write secondary global admission report");
        }
      }
      std::fprintf(stderr, "legroute: fixed-interface global reroute failed admission; return to planning before SAT\n");
      throw Legroute_replan(std::move(gates));
    }
    out.global_unrouted              = GR.unrouted;
    out.global_topology_disconnected = GR.topology_disconnected;
    out.global_full                  = GR.edges_full;
    out.global_overflow_edges        = GR.overflow_edges;
    out.global_overflow_tracks       = GR.overflow_tracks;
    out.global_rounds                = GR.rounds;
    out.global_over                  = GR.over;
    out.global_seconds               = GR.seconds;
    out.global_search_pops           = GR.search_pops;
    out.global_search_limited        = GR.search_limited;
    out.pattern_attempts             = GR.pattern_attempts;
    out.pattern_completed            = GR.pattern_completed;
    out.pattern_fallbacks            = GR.pattern_fallbacks;
    out.pattern_work                 = GR.pattern_work;
    out.pattern_work_limited         = GR.pattern_work_limited;
    std::fprintf(
        stderr,
        "legroute global: %zu crossing nets routed over the grid, %zu without a path, %zu edges at the share, %zu over it by %zu "
        "tracks after %u negotiation rounds, %zu queue pops, %zu search-limited attempts, %.1f s\n",
        gnets.size() - GR.unrouted,
        GR.unrouted,
        GR.edges_full,
        GR.overflow_edges,
        GR.overflow_tracks,
        GR.rounds,
        GR.search_pops,
        GR.search_limited,
        GR.seconds);
  }
  const auto  t_start      = Clock::now();
  const auto& global_trees = all_routes ? all_routes->trees : GR.trees;
  double      next_print   = 30;
  size_t      done         = 0;
  for (const size_t n : order) {
    if (++done % 256 == 0 && seconds_since(t_start) >= next_print) {  // a large design: where the crossings are
      std::fprintf(stderr,
                   "legroute crossings: %zu of %zu crossing nets, %zu hops, %zu traced, %.0f s\n",
                   done,
                   order.size(),
                   hops.size(),
                   out.global_nets,
                   seconds_since(t_start));
      next_print = seconds_since(t_start) + 30;
    }
    auto&         E = ends[n];
    std::set<int> hop_tiles;  // the net's endpoints that are hops, not its own pins or boxes
    for (int k : net_hops[n]) {
      hop_tiles.insert(hops[k].first);
      hop_tiles.insert(hops[k].second);
    }
    std::map<int, int> comp;  // region -> component (union-find by relabeling, few regions per net)
    for (const auto& [r, _] : E.per_region) {
      comp[r] = r;
    }
    const auto find = [&](int r) {
      while (comp.at(r) != r) {
        r = comp.at(r);
      }
      return r;
    };
    const auto unite = [&](int a, int b) {
      comp.try_emplace(a, a);
      comp.try_emplace(b, b);
      a = find(a);
      b = find(b);
      if (a != b) {
        comp[a] = b;
      }
    };
    for (int k : net_hops[n]) {  // the fixed pins already joined
      unite(g.region[hops[k].first], g.region[hops[k].second]);
    }
    const int  root_region = E.driver_region >= 0 ? E.driver_region : E.per_region.begin()->first;
    // legroute.global: a critical net joins a region with an L over the tile grid from the nearest endpoint of the
    // joined part (its pins, boxes or earlier hops) to the region's nearest own endpoint. Every boundary the L
    // crosses is a hop; a region the L only passes through keeps the run as a fixed piece. The orientation with
    // the lighter edges wins; an L over a blocked edge (a macro) is not taken, and with both blocked the hop chain
    // below joins the region instead
    const bool global      = d.opt.global_fraction > 0 && d.nets[n].rank < d.opt.global_fraction;
    bool       traced      = false;
    if (const size_t gi = gr_index[n]; gi != SIZE_MAX && global_trees[gi].routed) {
      // the routed tree: its boundary crossings are the net's hops, its runs through the regions holding none of
      // the net's own endpoints are fixed pieces (an L each), the regions it touches are joined
      const auto&   tedges = global_trees[gi].edges;
      // Keep real endpoints distinct from synthetic hops, including a fixed
      // physical pin that happens to coincide with a boundary hop.
      std::set<int> own_tiles;
      for (const auto& pin : d.nets[n].pins) {
        const int b = box_of_comp(pin.comp);
        own_tiles.insert(b >= 0 ? g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy)
                                : physical_pin_access(d, g, pin).tile);
      }
      if (preserve_coarse) {
        E.preserve_coarse = true;
        E.refine_corridor = true;
        std::set<int>                   witnessed;
        std::map<int, std::vector<int>> adjacency;
        for (const auto& [a, b] : tedges) {
          if (a < 0 || b < 0 || size_t(a) >= g.size() || size_t(b) >= g.size()
              || std::abs(g.col(a) - g.col(b)) + std::abs(g.row(a) - g.row(b)) != 1) {
            throw std::runtime_error(std::format("legroute coarse witness repair required: net {} has a non-grid edge", n));
          }
          witnessed.insert(a);
          witnessed.insert(b);
          adjacency[a].push_back(b);
          adjacency[b].push_back(a);
        }
        if (tedges.empty() && own_tiles.size() == 1) {
          witnessed.insert(*own_tiles.begin());
        }
        for (int t : own_tiles) {
          if (!witnessed.contains(t)) {
            throw std::runtime_error(
                std::format("legroute coarse witness repair required: net {} has a stale or missing endpoint attachment at tile {}",
                            n,
                            t));
          }
        }
        std::set<int>    reached;
        std::vector<int> queue;
        if (!own_tiles.empty()) {
          reached.insert(*own_tiles.begin());
          queue.push_back(*own_tiles.begin());
        }
        for (size_t at = 0; at < queue.size(); ++at) {
          for (int q : adjacency[queue[at]]) {
            if (reached.insert(q).second) {
              queue.push_back(q);
            }
          }
        }
        if (reached != witnessed) {
          throw std::runtime_error(
              std::format("legroute coarse witness repair required: net {} marked routed has disconnected original geometry", n));
        }
      }
      std::set<std::pair<int, int>> had;  // the net's hops so far (its fixed virtual pins)
      for (int k : net_hops[n]) {
        had.emplace(std::min(hops[k].first, hops[k].second), std::max(hops[k].first, hops[k].second));
      }
      std::map<int, std::vector<std::pair<int, int>>> interior;  // per region: the tree's edges inside it
      for (const auto& [a, b] : tedges) {
        const int dir = g.row(a) == g.row(b) ? 0 : 1;
        if (g.region[a] == g.region[b]) {
          ++through[a][dir];
          interior[g.region[a]].emplace_back(a, b);
          continue;
        }
        unite(g.region[a], g.region[b]);
        if (had.contains({a, b})) {
          continue;
        }
        ++hop_use[a][dir];
        ++hop_use[b][dir];
        if (double(hop_use[a][dir]) > d.opt.hop_share * (dir ? cap_v : cap_h)[a]
            || double(hop_use[b][dir]) > d.opt.hop_share * (dir ? cap_v : cap_h)[b]) {
          ++out.over_share;
        }
        const int interface_id = add_hop(int(n), a, b);
        for (int x : {a, b}) {
          auto& list = E.per_region[g.region[x]];
          if (preserve_coarse || std::ranges::none_of(list, [&](const Endpoint& o) { return o.tile == x; })) {
            Endpoint endpoint{-1, x};
            endpoint.interface_id = preserve_coarse ? interface_id : -1;
            list.push_back(endpoint);
          }
          hop_tiles.insert(x);
        }
      }
      // Removing boundary edges can split a global tree into several components
      // of the same region, including isolated hop tiles. Preserve that required
      // connectivity without fixing the interior paths chosen by local SAT.
      std::map<int, int> parent;
      const auto         root = [&](auto&& self, int t) -> int {
        const auto it = parent.try_emplace(t, t).first;
        if (it->second != t) {
          it->second = self(self, it->second);
        }
        return it->second;
      };
      for (const auto& [r, ie] : interior) {
        for (const auto& [a, b] : ie) {
          const int ra = root(root, a), rb = root(root, b);
          parent[std::max(ra, rb)] = std::min(ra, rb);
        }
      }
      for (auto& [r, list] : E.per_region) {
        for (auto& e : list) {
          const int t = e.box >= 0 ? g.tile_of(boxes.boxes[size_t(e.box)].cx, boxes.boxes[size_t(e.box)].cy) : e.tile;
          e.part      = root(root, t);
        }
      }
      std::set<int> own_parts;
      for (int t : own_tiles) {
        own_parts.insert(root(root, t));
      }
      if (preserve_coarse) {
        for (auto& [r, list] : E.per_region) {
          for (auto& e : list) {
            if (E.refine_corridor || own_parts.contains(e.part)) {
              const int t = e.box >= 0 ? g.tile_of(boxes.boxes[size_t(e.box)].cx, boxes.boxes[size_t(e.box)].cy) : e.tile;
              e.anchor    = t;
              E.coarse_parts[{r, e.part}].insert(t);
              E.coarse_junctions[{r, e.part}].insert(t);
            } else {
              out.retained_parts.emplace(int(n), r, e.part);
            }
          }
        }
        for (const auto& [r, ie] : interior) {
          for (const auto& [a, b] : ie) {
            const int part = root(root, a);
            if (E.refine_corridor || own_parts.contains(part)) {
              E.coarse_edges[{r, part}].emplace(std::min(a, b), std::max(a, b));
              auto& tiles = E.coarse_parts[{r, part}];
              tiles.insert(a);
              tiles.insert(b);
            }
          }
        }
      }
      // a path of the tree inside a pass-through component, split into straight runs; two runs make an L piece
      const auto emit = [&](int r, const std::vector<int>& path) {
        std::vector<int> corner{0};
        for (size_t i = 1; i + 1 < path.size(); ++i) {
          if ((g.row(path[i - 1]) == g.row(path[i])) != (g.row(path[i]) == g.row(path[i + 1]))) {
            corner.push_back(int(i));
          }
        }
        corner.push_back(int(path.size()) - 1);
        const bool   backbone = preserve_coarse && (E.refine_corridor || own_parts.contains(root(root, path.front())));
        // The original route is always expressible: split owned backbones at
        // every bend, branch and attachment rather than replacing them by an L.
        const size_t stride   = backbone ? 1 : 2;
        for (size_t k = 0; k + 1 < corner.size(); k += stride) {
          const size_t e    = std::min(k + stride, corner.size() - 1);
          const int    part = d.opt.global_keep ? -1 : root(root, path.front());
          out.pieces.push_back({int(n),
                                r,
                                path[corner[k]],
                                path[corner[e]],
                                g.row(path[corner[k]]) == g.row(path[corner[k] + 1]),
                                part,
                                backbone,
                                E.refine_corridor});
          if (backbone) {
            auto& nodes = E.coarse_junctions[{r, part}];
            nodes.insert(path[corner[k]]);
            nodes.insert(path[corner[e]]);
          }
          if (part >= 0 && !backbone) {
            out.retained_parts.emplace(int(n), r, part);
          }
        }
      };
      for (const auto& [r, ie] : interior) {
        std::map<int, std::vector<int>> adj_t;
        for (const auto& [a, b] : ie) {
          if (!preserve_coarse && !d.opt.global_keep && own_parts.contains(root(root, a))) {
            continue;  // only this component's real endpoints are routed by local SAT
          }
          adj_t[a].push_back(b);
          adj_t[b].push_back(a);
        }
        std::set<int> node;  // piece ends: hop tiles and branch tiles (a tree: every path ends at one)
        if (d.opt.global_keep || preserve_coarse) {
          for (const auto& e : E.per_region[r]) {
            node.insert(e.box >= 0 ? g.tile_of(boxes.boxes[size_t(e.box)].cx, boxes.boxes[size_t(e.box)].cy) : e.tile);
          }
        }
        for (const auto& [t, nb] : adj_t) {
          if (nb.size() != 2 || hop_tiles.contains(t)) {
            node.insert(t);
          }
        }
        std::set<std::pair<int, int>> walked;
        for (int s0 : node) {
          if (!adj_t.contains(s0)) {
            continue;  // this endpoint connects only through hops outside the region
          }
          for (int q : adj_t.at(s0)) {
            if (!walked.emplace(std::min(s0, q), std::max(s0, q)).second) {
              continue;
            }
            std::vector<int> path{s0, q};
            for (int prev = s0, cur = q; !node.contains(cur);) {
              const auto& nb  = adj_t.at(cur);
              const int   nxt = nb[0] == prev ? nb[1] : nb[0];
              walked.emplace(std::min(cur, nxt), std::max(cur, nxt));
              path.push_back(nxt);
              prev = cur;
              cur  = nxt;
            }
            emit(r, path);
          }
        }
      }
      if (d.opt.global_keep) {
        for (const auto& [r, list] : E.per_region) {
          out.retained.emplace(int(n), r);
        }
      }
      traced = true;
    }
    if (preserve_coarse && !traced) {
      throw std::runtime_error(std::format("legroute coarse witness repair required: net {} has no admitted global tree", n));
    }
    const auto tile_of
        = [&](const Endpoint& e) { return e.box >= 0 ? g.tile_of(boxes.boxes[e.box].cx, boxes.boxes[e.box].cy) : e.tile; };
    const auto real_endpoint = [&](const Endpoint& e) { return e.box >= 0 || !hop_tiles.contains(e.tile); };
    const auto join_global   = [&](int r) {
      int    ta = -1, tb = -1;  // the nearest pair: an endpoint of the joined part, r's own endpoint
      double best = INFINITY;
      for (const auto& er : E.per_region.at(r)) {
        const int t1 = tile_of(er);
        if (!real_endpoint(er) || g.region[t1] != r) {  // a box's placement tile can fall outside its region
          continue;
        }
        for (const auto& [q, list] : E.per_region) {
          if (!comp.contains(q) || find(q) != find(root_region)) {
            continue;
          }
          for (const auto& eq : list) {
            const int t0 = tile_of(eq);
            if (g.region[t0] != q) {
              continue;
            }
            const double dd = std::abs(g.col(t0) - g.col(t1)) + std::abs(g.row(t0) - g.row(t1));
            if (dd < best) {
              best = dd;
              ta   = t0;
              tb   = t1;
            }
          }
        }
      }
      if (ta < 0 || ta == tb) {
        return false;
      }
      struct Walk {
        std::vector<int> tiles;
        double           load    = 0;
        bool             blocked = false;
      };
      const auto walk = [&](bool h_first) {  // the L's tiles from ta to tb, the worst edge load on the way
        Walk w;
        w.tiles.reserve(size_t(std::abs(g.col(ta) - g.col(tb)) + std::abs(g.row(ta) - g.row(tb))) + 1);
        const auto step = [&](int t) {
          if (!w.tiles.empty()) {
            const int p = w.tiles.back(), dir = g.row(p) == g.row(t) ? 0 : 1, lo = std::min(p, t);
            const int cap = std::min((dir ? cap_v : cap_h)[p], (dir ? cap_v : cap_h)[t]);
            if (cap <= 0 || g.blocked[t]) {
              w.blocked = true;
            }
            const int used = through[lo][dir] + (g.region[p] != g.region[t] ? std::max(hop_use[p][dir], hop_use[t][dir]) : 0);
            w.load         = std::max(w.load, double(used) / double(std::max(1, cap)));
          }
          w.tiles.push_back(t);
        };
        const int hr = h_first ? g.row(ta) : g.row(tb), vc = h_first ? g.col(tb) : g.col(ta);
        const int dc = g.col(tb) > g.col(ta) ? 1 : -1, dr = g.row(tb) > g.row(ta) ? 1 : -1;
        step(ta);
        if (h_first) {
          for (int i = g.col(ta); i != g.col(tb); i += dc) {
            step(g.tile(i + dc, hr));
          }
          for (int j = g.row(ta); j != g.row(tb); j += dr) {
            step(g.tile(vc, j + dr));
          }
        } else {
          for (int j = g.row(ta); j != g.row(tb); j += dr) {
            step(g.tile(vc, j + dr));
          }
          for (int i = g.col(ta); i != g.col(tb); i += dc) {
            step(g.tile(i + dc, hr));
          }
        }
        return w;
      };
      const Walk wh = walk(true), wv = walk(false);
      if (wh.blocked && wv.blocked) {
        return false;
      }
      const bool  h_first = !wh.blocked && (wv.blocked || wh.load <= wv.load);
      const Walk& W       = h_first ? wh : wv;
      int         entry   = -1;  // the hop tile the trace entered the current region by (the start region: none)
      for (size_t i = 0; i + 1 < W.tiles.size(); ++i) {
        const int p = W.tiles[i], t = W.tiles[i + 1], dir = g.row(p) == g.row(t) ? 0 : 1;
        ++through[std::min(p, t)][dir];
        if (g.region[p] == g.region[t]) {
          continue;
        }
        const bool pass = entry >= 0 && std::ranges::none_of(E.per_region[g.region[p]], real_endpoint);
        if (pass) {  // the region left held none of the net's pins: its run is a fixed piece
          out.pieces.push_back({int(n), g.region[p], entry, p, h_first});
        }
        ++hop_use[p][dir];
        ++hop_use[t][dir];
        if (double(hop_use[p][dir]) > d.opt.hop_share * (dir ? cap_v : cap_h)[p]
            || double(hop_use[t][dir]) > d.opt.hop_share * (dir ? cap_v : cap_h)[t]) {
          ++out.over_share;
        }
        add_hop(int(n), p, t);
        hop_tiles.insert(p);
        hop_tiles.insert(t);
        for (int x : {p, t}) {
          auto& list = E.per_region[g.region[x]];
          if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.tile == x; })) {
            list.push_back({-1, x});
          }
        }
        unite(g.region[p], g.region[t]);
        entry = t;
      }
      unite(g.region[ta], r);
      return find(r) == find(root_region);
    };
    size_t rounds = 0;
    for (bool progress = true; progress;) {
      progress = false;
      // every round joins a region or ends the loop; a loop still running past the regions it could join is a bug
      // in the join, reported and cut rather than spun on
      if (++rounds > 2 * E.per_region.size() + 8) {
        ++out.unjoined;
        std::fprintf(stderr,
                     "legroute crossings: net %zu join stalled after %zu rounds (%zu regions, %zu hops)\n",
                     n,
                     rounds,
                     E.per_region.size(),
                     net_hops[n].size());
        break;
      }
      std::vector<int> keys;  // the regions at the round's start (the joins add pass-through regions meanwhile)
      keys.reserve(E.per_region.size());
      for (const auto& [r, _] : E.per_region) {
        keys.push_back(r);
      }
      for (const int r : keys) {
        if (find(r) == find(root_region)) {
          continue;
        }
        if (global && join_global(r)) {
          traced   = true;
          progress = true;
          continue;
        }
        // shortest region path from r's component to the root's component
        std::map<int, int> from;
        std::vector<int>   frontier;
        for (const auto& [q, _2] : comp) {
          if (find(q) == find(r)) {
            from[q] = -1;
            frontier.push_back(q);
          }
        }
        // the shortest join, and among the shortest the region holding the net's own endpoints nearest to r's
        // (a region the net only passes through, joined by earlier hops, would make r's part a detour through it)
        const auto real_in = [&](int q) {
          const auto it = E.per_region.find(q);
          return it != E.per_region.end()
                 && std::ranges::any_of(it->second, [&](const Endpoint& e) { return e.box >= 0 || !hop_tiles.contains(e.tile); });
        };
        int    hit         = -1;
        size_t level_begin = 0;
        while (hit < 0 && level_begin < frontier.size()) {
          const size_t level_end = frontier.size();
          double       best_hit  = INFINITY;
          for (size_t i = level_begin; i < level_end; ++i) {
            for (int q : adj[frontier[i]]) {
              if (from.contains(q)) {
                continue;
              }
              if (comp.contains(q) && find(q) == find(root_region)) {
                double score = real_in(q) ? 0 : 1e30;
                if (real_in(q)) {
                  const auto cr = centroid(E.per_region.at(r)), cq = centroid(E.per_region.at(q));
                  score = std::abs(cr.first - cq.first) + std::abs(cr.second - cq.second);
                }
                if (score < best_hit) {
                  best_hit = score;
                  hit      = q;
                  from[q]  = frontier[i];
                }
                continue;
              }
              from[q] = frontier[i];
              frontier.push_back(q);
            }
          }
          level_begin = level_end;
        }
        if (hit < 0) {
          ++out.unjoined;
          comp[find(r)] = find(root_region);  // unreachable: give up on this part
          continue;
        }
        std::vector<int> path{hit};
        while (from.at(path.back()) >= 0) {
          path.push_back(from.at(path.back()));
        }
        for (size_t i = 0; i + 1 < path.size(); ++i) {
          const int  a = path[i + 1], b = path[i];  // a toward r, b toward the root
          const auto key  = std::pair{std::min(a, b), std::max(a, b)};
          const auto ta   = E.per_region.contains(a) ? centroid(E.per_region.at(a)) : std::pair{0.0, 0.0};
          const auto tb   = E.per_region.contains(b) ? centroid(E.per_region.at(b)) : ta;
          const auto aim  = E.per_region.contains(a) ? std::pair{(ta.first + tb.first) / 2, (ta.second + tb.second) / 2} : tb;
          // b is a pass-through region when the net's endpoints there are hop tiles only (the hop into it was chosen
          // a step earlier): the run from that hop to this one is straight or an L with no other choice, so it costs
          // the rows and columns other pass-throughs already took (hop_spread tile widths per full load)
          int        prev = -1;
          if (d.opt.hop_spread > 0 && E.per_region.contains(b)) {
            const auto& lb = E.per_region.at(b);
            if (std::ranges::all_of(lb, [](const Endpoint& e) { return e.box < 0; })) {
              prev = lb.back().tile;
            }
          }
          // the L from prev to v, H first (H on prev's row, V on v's column) or V first (V on prev's column, H on
          // v's row): the edges it crosses
          const auto run_edges = [&](int v, bool h_first, const auto& visit) {
            const int hr = h_first ? g.row(prev) : g.row(v), vc = h_first ? g.col(v) : g.col(prev);
            for (int col = std::min(g.col(prev), g.col(v)); col < std::max(g.col(prev), g.col(v)); ++col) {
              visit(g.tile(col, hr), 0);
            }
            for (int j = std::min(g.row(prev), g.row(v)); j < std::max(g.row(prev), g.row(v)); ++j) {
              visit(g.tile(vc, j), 1);
            }
          };
          // the fullest edge on that L, sampled every few tiles on a long run (a large die's L is thousands of
          // tiles; the chooser pays this per candidate)
          const auto run_load = [&](int v, bool h_first) {
            const int len    = std::abs(g.col(prev) - g.col(v)) + std::abs(g.row(prev) - g.row(v));
            const int stride = std::max(1, len / 32);
            double    load   = 0;
            int       k      = 0;
            run_edges(v, h_first, [&](int t, int dir) {
              if (k++ % stride == 0) {
                load = std::max(load, double(through[t][dir]) / std::max(1.0, double((dir ? cap_v : cap_h)[t])));
              }
            });
            return load;
          };
          int                                       bu = -1, bv = -1;
          double                                    best = INFINITY;
          // the candidates with hop room, nearest to the aim first; the spread term (an L walk each) is paid only
          // for the nearest few, so a long boundary of a large design costs the same as a short one
          std::vector<std::tuple<double, int, int>> cands;
          for (auto [u, v] : edges.at(key)) {
            if (g.region[u] != a) {
              std::swap(u, v);
            }
            const int h = g.row(u) == g.row(v) ? 0 : 1;
            if (double(hop_use[u][h]) >= d.opt.hop_share * (h ? cap_v : cap_h)[u]
                || double(hop_use[v][h]) >= d.opt.hop_share * (h ? cap_v : cap_h)[v]) {
              continue;
            }
            const double mx = (g.cx(g.col(u)) + g.cx(g.col(v))) / 2.0, my = (g.cy(g.row(u)) + g.cy(g.row(v))) / 2.0;
            cands.emplace_back(std::abs(mx - aim.first) + std::abs(my - aim.second), u, v);
          }
          constexpr size_t nearest = 32;
          if (cands.size() > nearest) {
            std::ranges::partial_sort(cands, cands.begin() + nearest);
            cands.resize(nearest);
          }
          for (const auto& [dist, u, v] : cands) {
            double cost = dist;
            if (prev >= 0) {
              cost += d.opt.hop_spread * double(g.tile_w) * std::min(run_load(v, true), run_load(v, false));
            }
            if (cost < best) {
              best = cost;
              bu   = u;
              bv   = v;
            }
          }
          if (bu < 0) {  // every tile of the boundary is at the share: the least loaded one, nearest on a tie
            double least = INFINITY;
            for (auto [u, v] : edges.at(key)) {
              if (g.region[u] != a) {
                std::swap(u, v);
              }
              const int    h    = g.row(u) == g.row(v) ? 0 : 1;
              const double load = std::max(double(hop_use[u][h]) / std::max(1, (h ? cap_v : cap_h)[u]),
                                           double(hop_use[v][h]) / std::max(1, (h ? cap_v : cap_h)[v]));
              const double mx = (g.cx(g.col(u)) + g.cx(g.col(v))) / 2.0, my = (g.cy(g.row(u)) + g.cy(g.row(v))) / 2.0;
              const double cost = load + 1e-6 * (std::abs(mx - aim.first) + std::abs(my - aim.second)) / double(g.tile_w);
              if (cost < least) {
                least = cost;
                bu    = u;
                bv    = v;
              }
            }
            if (bu >= 0) {
              ++out.over_share;
            }
          }
          if (bu >= 0 && prev >= 0) {
            run_edges(bv, run_load(bv, true) <= run_load(bv, false), [&](int t, int dir) { ++through[t][dir]; });
          }
          if (bu < 0) {
            ++out.unjoined;
            break;
          }
          const int h = g.row(bu) == g.row(bv) ? 0 : 1;
          ++hop_use[bu][h];
          ++hop_use[bv][h];
          for (int t : {bu, bv}) {
            auto& list = E.per_region[g.region[t]];
            if (std::ranges::none_of(list, [&](const Endpoint& o) { return o.tile == t; })) {
              list.push_back({-1, t});
            }
          }
          add_hop(int(n), bu, bv);
          hop_tiles.insert(bu);
          hop_tiles.insert(bv);
          unite(a, b);
          progress = true;
        }
      }
    }
    out.global_nets += traced;
  }
  for (size_t k = 0; k < hops.size(); ++k) {
    for (int t : {hops[k].first, hops[k].second}) {
      (g.row(hops[k].first) == g.row(hops[k].second) ? cap_h : cap_v)[t] -= 1;
    }
  }
  // The model and its pre-placement policies see only local components.
  // Retained components keep their original hops and geometry in pieces;
  // another component of the same net/region must remain movable in SAT.
  for (const auto& [n, r, part] : out.retained_parts) {
    auto& list = ends[size_t(n)].per_region.at(r);
    std::erase_if(list, [&](const Endpoint& e) { return e.part == part; });
  }
  return out;
}

// ---------------------------------------------------------------- spill selection: min cuts inside a failing region
// The boxes of a failing region r to push out, and where: the region and its receiving neighbours as one graph, the
// region's movable boxes its nodes, each neighbour contracted to a terminal T_j, one terminal S for what stays. A net
// is a star of edges from its first box to its other boxes (weight 100 x the net's weight over its endpoint count
// less one: a net costs about the same however many boxes it joins); a net reaching neighbour j also pulls its boxes
// to T_j, a net with a fixed endpoint in r or an endpoint past the neighbours pulls them to S. Each neighbour's side
// is its isolating s-t min cut (VieCut's push-relabel: T_j against S and the other terminals), so the netlist decides
// which boxes leave and to which neighbour, connected clusters together. A uniform pull a of every box to the
// neighbours is bisected to the smallest value that moves at least `want` boxes; a box two cuts claim goes to the
// neighbour its nets reach most (the roomier one when tied), and a neighbour takes at most its room (the boxes its
// nets reach most first). Returns (box, region) pairs.
struct Spill_target {
  int region = -1, room = 0;
};
std::vector<std::pair<int, int>> spill_cut(const Design& d, const Boxes& boxes, const std::vector<Net_ends>& ends,
                                           const std::vector<int>& cand, int r, const std::vector<Spill_target>& to, size_t want) {
  const size_t B = cand.size(), M = to.size();
  if (B == 0 || M == 0 || want == 0) {
    return {};
  }
  want = std::min(want, B);
  std::vector<int>      node_of(boxes.boxes.size(), -1);
  std::map<int, size_t> jof;  // region -> terminal
  for (size_t i = 0; i < B; ++i) {
    node_of[size_t(cand[i])] = int(i);
  }
  for (size_t j = 0; j < M; ++j) {
    jof[to[j].region] = j;
  }
  std::vector<uint64_t>                   to_s(B, 0);
  std::vector<std::vector<uint64_t>>      to_t(M, std::vector<uint64_t>(B, 0));
  std::map<std::pair<int, int>, uint64_t> bb;  // box-box star edges (lower node first)
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& per = ends[n].per_region;
    const auto  it  = per.find(r);
    if (it == per.end()) {
      continue;
    }
    std::vector<int> mine;
    bool             anchored = false;
    for (const auto& e : it->second) {
      if (e.box >= 0 && node_of[size_t(e.box)] >= 0) {
        mine.push_back(node_of[size_t(e.box)]);
      } else {
        anchored = true;  // a fixed tile, or a box of r that stays
      }
    }
    if (mine.empty()) {
      continue;
    }
    std::ranges::sort(mine);
    mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
    std::vector<size_t> reach;  // the receiving neighbours the net reaches
    for (const auto& [rr, _] : per) {
      if (rr == r) {
        continue;
      }
      if (const auto j = jof.find(rr); j != jof.end()) {
        reach.push_back(j->second);
      } else {
        anchored = true;
      }
    }
    const size_t   k = mine.size() + reach.size() + (anchored ? 1 : 0);
    const uint64_t w = std::max<uint64_t>(1, 100 * uint64_t(d.nets[n].weight) / std::max<size_t>(1, k - 1));
    for (int i : mine) {
      if (anchored) {
        to_s[size_t(i)] += w;
      }
      for (size_t j : reach) {
        to_t[j][size_t(i)] += w;
      }
    }
    for (size_t q = 1; q < mine.size(); ++q) {
      bb[{mine[0], mine[q]}] += w;
    }
  }
  std::vector<uint64_t> hold(B, 0);  // the most a box can be held back: a pull above it leaves for sure
  for (size_t i = 0; i < B; ++i) {
    hold[i] = to_s[i];
    for (size_t j = 0; j < M; ++j) {
      hold[i] += to_t[j][i];
    }
  }
  for (const auto& [e, w] : bb) {
    hold[size_t(e.first)]  += w;
    hold[size_t(e.second)] += w;
  }
  const NodeID U = NodeID(B), T = NodeID(B + 1);
  const auto   side = [&](size_t j, uint64_t a) {  // the boxes on neighbour j's side of its isolating cut at pull a
    auto G = std::make_shared<mutable_graph>();
    G->start_construction(NodeID(B + 2));
    for (size_t i = 0; i < B + 2; ++i) {
      G->new_node();
    }
    for (const auto& [e, w] : bb) {
      G->new_edge(NodeID(e.first), NodeID(e.second), w);
    }
    for (size_t i = 0; i < B; ++i) {
      uint64_t u = to_s[i];
      for (size_t k = 0; k < M; ++k) {
        if (k != j) {
          u += to_t[k][i];
        }
      }
      if (u > 0) {
        G->new_edge(NodeID(i), U, u);
      }
      if (to_t[j][i] + a > 0) {
        G->new_edge(NodeID(i), T, to_t[j][i] + a);
      }
    }
    G->finish_construction();
    push_relabel<>    pr;
    const auto        stay = pr.solve_max_flow_min_cut(G, {U, T}, 0, true).second;
    std::vector<char> on(B, 1);
    for (NodeID v : stay) {
      if (v < B) {
        on[v] = 0;
      }
    }
    return on;
  };
  const auto claim = [&](uint64_t a) {  // per box: the neighbours whose cuts claim it
    std::vector<std::vector<size_t>> who(B);
    for (size_t j = 0; j < M; ++j) {
      const auto on = side(j, a);
      for (size_t i = 0; i < B; ++i) {
        if (on[i]) {
          who[i].push_back(j);
        }
      }
    }
    return who;
  };
  // the assignment at pull a: a box two cuts claim goes to the neighbour its nets reach most (the roomier one when
  // tied), each neighbour keeps the boxes its nets reach most, at most its room
  const auto assign = [&](uint64_t a) {
    const auto                                         who = claim(a);
    std::vector<std::vector<std::pair<uint64_t, int>>> take(M);  // per neighbour: (pull, box)
    for (size_t i = 0; i < B; ++i) {
      if (who[i].empty()) {
        continue;
      }
      size_t pick = who[i][0];
      for (size_t j : who[i]) {
        if (to_t[j][i] > to_t[pick][i] || (to_t[j][i] == to_t[pick][i] && to[j].room > to[pick].room)) {
          pick = j;
        }
      }
      take[pick].emplace_back(to_t[pick][i], cand[i]);
    }
    for (size_t j = 0; j < M; ++j) {
      std::ranges::sort(take[j], std::greater<>());
      take[j].resize(std::min(take[j].size(), size_t(std::max(0, to[j].room))));
    }
    return take;
  };
  const auto moved = [&](const std::vector<std::vector<std::pair<uint64_t, int>>>& take) {
    size_t n = 0;
    for (const auto& t : take) {
      n += t.size();
    }
    return n;
  };
  size_t room_sum = 0;
  for (const auto& t : to) {
    room_sum += size_t(std::max(0, t.room));
  }
  want        = std::min(want, room_sum);
  uint64_t lo = 0, hi = 1;
  for (size_t i = 0; i < B; ++i) {
    hi = std::max(hi, hold[i] + 1);
  }
  auto best = assign(0);
  if (moved(best) < want) {  // the smallest uniform pull that moves enough: a bisection (the claims grow with it)
    auto top = assign(hi);
    while (hi - lo > 1) {
      const uint64_t mid = lo + (hi - lo) / 2;
      auto           c   = assign(mid);
      if (moved(c) >= want) {
        hi  = mid;
        top = std::move(c);
      } else {
        lo = mid;
      }
    }
    best = std::move(top);
  }
  std::vector<std::pair<int, int>> out;
  for (size_t j = 0; j < M; ++j) {
    for (const auto& [_, b] : best[j]) {
      out.emplace_back(b, to[j].region);
    }
  }
  return out;
}

// ---------------------------------------------------------------- spill by boundary shift
// The pull of region r toward each region of `to`: the weight of the nets with an endpoint in r (a box or a fixed
// pin) and one in that region.
std::vector<uint64_t> spill_pulls(const Design& d, const std::vector<Net_ends>& ends, int r, const std::vector<int>& to) {
  std::vector<uint64_t> pull(to.size(), 0);
  std::map<int, size_t> jof;
  for (size_t j = 0; j < to.size(); ++j) {
    jof[to[j]] = j;
  }
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& per = ends[n].per_region;
    if (!per.contains(r)) {
      continue;
    }
    for (const auto& [rr, _] : per) {
      if (const auto j = jof.find(rr); j != jof.end()) {
        pull[j->second] += 100 * uint64_t(d.nets[n].weight);
      }
    }
  }
  return pull;
}
// The four tile neighbours of t.
std::vector<int> tile_neighbours(const Grid& g, int t) {
  std::vector<int> out;
  if (g.col(t) > 0) {
    out.push_back(t - 1);
  }
  if (g.col(t) + 1 < g.cols) {
    out.push_back(t + 1);
  }
  if (g.row(t) > 0) {
    out.push_back(t - g.cols);
  }
  if (g.row(t) + 1 < g.rows) {
    out.push_back(t + g.cols);
  }
  return out;
}
// The tile strip of region r next to region s, k tiles deep (breadth-first from s through r's tiles).
std::vector<int> boundary_strip(const Grid& g, int r, int s, int k) {
  std::vector<int> depth(g.size(), 0), frontier;
  for (int t = 0; t < int(g.size()); ++t) {
    if (g.region[t] == r && std::ranges::any_of(tile_neighbours(g, t), [&](int q) { return g.region[q] == s; })) {
      depth[size_t(t)] = 1;
      frontier.push_back(t);
    }
  }
  for (int dpt = 1; dpt < k && !frontier.empty(); ++dpt) {
    std::vector<int> next;
    for (int t : frontier) {
      for (int q : tile_neighbours(g, t)) {
        if (g.region[q] == r && depth[size_t(q)] == 0) {
          depth[size_t(q)] = dpt + 1;
          next.push_back(q);
        }
      }
    }
    frontier = std::move(next);
  }
  std::vector<int> strip;
  for (int t = 0; t < int(g.size()); ++t) {
    if (depth[size_t(t)] > 0) {
      strip.push_back(t);
    }
  }
  return strip;
}
// Whether region r's tiles outside `strip` stay one connected piece (and there is one).
bool connected_without(const Grid& g, int r, const std::vector<int>& strip) {
  const std::set<int> gone(strip.begin(), strip.end());
  std::vector<char>   seen(g.size(), 0);
  int                 start = -1, left = 0;
  for (int t = 0; t < int(g.size()); ++t) {
    if (g.region[t] == r && !gone.contains(t)) {
      ++left;
      if (start < 0) {
        start = t;
      }
    }
  }
  if (left == 0) {
    return false;
  }
  std::vector<int> stack{start};
  seen[size_t(start)] = 1;
  int reached         = 0;
  while (!stack.empty()) {
    const int t = stack.back();
    stack.pop_back();
    ++reached;
    for (int q : tile_neighbours(g, t)) {
      if (g.region[q] == r && !gone.contains(q) && !seen[size_t(q)]) {
        seen[size_t(q)] = 1;
        stack.push_back(q);
      }
    }
  }
  return reached == left;
}

// ---------------------------------------------------------------- alternative placements of a failing region
// A candidate: new homes (and windows) for the region's movable boxes.
struct Alt_home {
  int    box;
  double cx, cy;
  int    window;  // -1: the default
};
struct Alternative {
  std::string           kind;
  std::vector<Alt_home> homes;
};
// noise: every movable box's home moves by a uniform random offset of up to `reach` tiles, inside the region's bounds.
Alternative alt_noise(const Grid& g, const Boxes& boxes, const std::vector<int>& movable, const std::vector<int>& tiles, int reach,
                      unsigned seed) {
  Alternative out{"noise", {}};
  int         c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : tiles) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  std::mt19937                           rng(seed);
  std::uniform_real_distribution<double> u(-double(reach), double(reach));
  for (int b : movable) {
    const auto&  bx = boxes.boxes[size_t(b)];
    const double cx = std::clamp(bx.cx + u(rng) * double(g.tile_w), double(g.cx(c0)), double(g.cx(c1)));
    const double cy = std::clamp(bx.cy + u(rng) * double(g.tile_h), double(g.cy(r0)), double(g.cy(r1)));
    out.homes.push_back({b, cx, cy, -1});
  }
  return out;
}
// anchor: a few boxes of the region's densest 4x4 patch (non-critical first) move to the centre of its sparsest patch
// and keep a one-tile window there.
Alternative alt_anchor(const Grid& g, const Boxes& boxes, const std::vector<int>& movable, const std::vector<int>& tiles) {
  Alternative out{"anchor", {}};
  int         c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : tiles) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  const int  nb     = 4;
  const auto bin_of = [&](double cx, double cy) {
    const int bx = std::clamp(int((cx - g.cx(c0)) / std::max(1.0, double(g.cx(c1) - g.cx(c0))) * nb), 0, nb - 1);
    const int by = std::clamp(int((cy - g.cy(r0)) / std::max(1.0, double(g.cy(r1) - g.cy(r0))) * nb), 0, nb - 1);
    return by * nb + bx;
  };
  std::vector<int> count(size_t(nb * nb), 0), free_tiles(size_t(nb * nb), 0);
  for (int b : movable) {
    ++count[size_t(bin_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy))];
  }
  for (int t : tiles) {
    free_tiles[size_t(bin_of(double(g.cx(g.col(t))), double(g.cy(g.row(t)))))] += !g.blocked[t];
  }
  int densest = 0, sparsest = 0;
  for (int k = 0; k < nb * nb; ++k) {  // fill = boxes over free tiles
    const auto fill = [&](int q) { return free_tiles[size_t(q)] ? double(count[size_t(q)]) / double(free_tiles[size_t(q)]) : 2.0; };
    if (fill(k) > fill(densest)) {
      densest = k;
    }
    if (free_tiles[size_t(k)] > 0 && fill(k) < fill(sparsest)) {
      sparsest = k;
    }
  }
  if (densest == sparsest || count[size_t(densest)] == 0) {
    return out;
  }
  const double     tx   = double(g.cx(c0)) + (sparsest % nb + .5) * double(g.cx(c1) - g.cx(c0)) / nb;
  const double     ty   = double(g.cy(r0)) + (sparsest / nb + .5) * double(g.cy(r1) - g.cy(r0)) / nb;
  const size_t     want = std::max<size_t>(3, (movable.size() + 19) / 20);
  std::vector<int> pick;
  for (int pass = 0; pass < 2 && pick.size() < want; ++pass) {  // non-critical boxes first
    for (int b : movable) {
      const auto& bx = boxes.boxes[size_t(b)];
      if (pick.size() < want && bin_of(bx.cx, bx.cy) == densest && (pass == 1 || !bx.critical)
          && std::ranges::find(pick, b) == pick.end()) {
        pick.push_back(b);
      }
    }
  }
  for (size_t k = 0; k < pick.size(); ++k) {  // spread along x, a tile apart
    const double cx
        = std::clamp(tx + (double(k) - double(pick.size() - 1) / 2) * double(g.tile_w), double(g.cx(c0)), double(g.cx(c1)));
    out.homes.push_back({pick[k], cx, ty, 1});
  }
  return out;
}
// spread: the boxes of the patches denser than the region's mean step toward the sparsest patch, up to two tiles
// (a legroute-level inflation: the dense patches thin out, the sparse one fills).
Alternative alt_spread(const Grid& g, const Boxes& boxes, const std::vector<int>& movable, const std::vector<int>& tiles) {
  Alternative out{"spread", {}};
  int         c0 = g.cols, c1 = -1, r0 = g.rows, r1 = -1;
  for (int t : tiles) {
    c0 = std::min(c0, g.col(t));
    c1 = std::max(c1, g.col(t));
    r0 = std::min(r0, g.row(t));
    r1 = std::max(r1, g.row(t));
  }
  const int  nb     = 4;
  const auto bin_of = [&](double cx, double cy) {
    const int bx = std::clamp(int((cx - g.cx(c0)) / std::max(1.0, double(g.cx(c1) - g.cx(c0))) * nb), 0, nb - 1);
    const int by = std::clamp(int((cy - g.cy(r0)) / std::max(1.0, double(g.cy(r1) - g.cy(r0))) * nb), 0, nb - 1);
    return by * nb + bx;
  };
  std::vector<double> count(size_t(nb * nb), 0), free_tiles(size_t(nb * nb), 0);
  for (int b : movable) {
    ++count[size_t(bin_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy))];
  }
  for (int t : tiles) {
    free_tiles[size_t(bin_of(double(g.cx(g.col(t))), double(g.cy(g.row(t)))))] += !g.blocked[t];
  }
  const auto fill     = [&](int q) { return free_tiles[size_t(q)] ? count[size_t(q)] / free_tiles[size_t(q)] : 2.0; };
  double     mean     = 0;
  int        sparsest = 0, with_tiles = 0;
  for (int k = 0; k < nb * nb; ++k) {
    if (free_tiles[size_t(k)] > 0) {
      mean += fill(k);
      ++with_tiles;
      if (fill(k) < fill(sparsest) || free_tiles[size_t(sparsest)] == 0) {
        sparsest = k;
      }
    }
  }
  if (with_tiles == 0) {
    return out;
  }
  mean            /= with_tiles;
  const double tx  = double(g.cx(c0)) + (sparsest % nb + .5) * double(g.cx(c1) - g.cx(c0)) / nb;
  const double ty  = double(g.cy(r0)) + (sparsest / nb + .5) * double(g.cy(r1) - g.cy(r0)) / nb;
  for (int b : movable) {
    const auto& bx = boxes.boxes[size_t(b)];
    if (fill(bin_of(bx.cx, bx.cy)) <= mean) {
      continue;
    }
    const double dx = tx - bx.cx, dy = ty - bx.cy, len = std::abs(dx) + std::abs(dy);
    if (len <= 0) {
      continue;
    }
    const double step = std::min(len / 2, 2.0 * double(g.tile_w));
    out.homes.push_back({b, bx.cx + dx / len * step, bx.cy + dy / len * step, -1});
  }
  return out;
}
// tsm: the region's box graph (a star per net) loses edges, highest degree first and never those of nets with an
// endpoint outside the region's movable boxes, until every node has four or fewer; the reduced graph is drawn
// breadth first from its highest-degree node, each box on the free tile nearest the centroid of its placed
// neighbours (ports north, east, south and west: connected boxes adjacent), and the drawing is the new homes.
Alternative alt_tsm(const Design& d, const Grid& g, const Boxes& boxes, const std::vector<Net_ends>& ends,
                    const std::vector<int>& movable, const std::vector<int>& tiles, int r, const std::vector<char>& pinned_tile) {
  Alternative      out{"tsm", {}};
  const size_t     B = movable.size();
  std::vector<int> node_of(boxes.boxes.size(), -1);
  for (size_t i = 0; i < B; ++i) {
    node_of[size_t(movable[i])] = int(i);
  }
  struct Edge {
    int  a, b;
    bool kept = true, held = false;  // held: the net has an endpoint past the movable boxes (a fixed pin, a hop, another region)
  };
  std::vector<Edge> edges;
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& per = ends[n].per_region;
    const auto  it  = per.find(r);
    if (it == per.end()) {
      continue;
    }
    std::vector<int> mine;
    bool             held = per.size() > 1;
    for (const auto& e : it->second) {
      if (e.box >= 0 && node_of[size_t(e.box)] >= 0) {
        mine.push_back(node_of[size_t(e.box)]);
      } else {
        held = true;
      }
    }
    std::ranges::sort(mine);
    mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
    for (size_t q = 1; q < mine.size(); ++q) {
      edges.push_back({mine[0], mine[q], true, held});
    }
  }
  std::vector<std::vector<int>> adj(B);  // edge ids
  std::vector<int>              degree(B, 0);
  for (size_t e = 0; e < edges.size(); ++e) {
    adj[size_t(edges[e].a)].push_back(int(e));
    adj[size_t(edges[e].b)].push_back(int(e));
    ++degree[size_t(edges[e].a)];
    ++degree[size_t(edges[e].b)];
  }
  for (bool again = true; again;) {  // reduce: the highest-degree node drops its free edge to its busiest neighbour
    again   = false;
    int top = -1;
    for (size_t i = 0; i < B; ++i) {
      if (degree[i] > 4 && (top < 0 || degree[i] > degree[size_t(top)])) {
        top = int(i);
      }
    }
    if (top < 0) {
      break;
    }
    int drop = -1;
    for (int e : adj[size_t(top)]) {
      if (!edges[size_t(e)].kept || edges[size_t(e)].held) {
        continue;
      }
      const int other = edges[size_t(e)].a == top ? edges[size_t(e)].b : edges[size_t(e)].a;
      if (drop < 0
          || degree[size_t(other)] > degree[size_t(edges[size_t(drop)].a == top ? edges[size_t(drop)].b : edges[size_t(drop)].a)]) {
        drop = e;
      }
    }
    if (drop < 0) {                                // every edge held: this node keeps its degree, the others go on
      degree[size_t(top)] = -degree[size_t(top)];  // marked done
      again               = true;
      continue;
    }
    edges[size_t(drop)].kept = false;
    --degree[size_t(edges[size_t(drop)].a)];
    --degree[size_t(edges[size_t(drop)].b)];
    again = true;
  }
  for (auto& dg : degree) {
    dg = std::abs(dg);
  }
  // the drawing: breadth first from the busiest node
  std::vector<int> free;
  for (int t : tiles) {
    if (!g.blocked[t] && !pinned_tile[size_t(t)]) {
      free.push_back(t);
    }
  }
  if (free.empty() || B == 0) {
    return out;
  }
  std::vector<char>   placed(B, 0), taken(g.size(), 0);
  std::vector<double> px(B, 0), py(B, 0);
  std::vector<int>    order;
  std::vector<int>    queue;
  std::vector<char>   seen(B, 0);
  while (order.size() < B) {  // one component after another, busiest node first
    int root = -1;
    for (size_t i = 0; i < B; ++i) {
      if (!seen[i] && (root < 0 || degree[i] > degree[size_t(root)])) {
        root = int(i);
      }
    }
    queue.assign(1, root);
    seen[size_t(root)] = 1;
    for (size_t q = 0; q < queue.size(); ++q) {
      const int i = queue[q];
      order.push_back(i);
      std::vector<int> next;
      for (int e : adj[size_t(i)]) {
        if (!edges[size_t(e)].kept) {
          continue;
        }
        const int other = edges[size_t(e)].a == i ? edges[size_t(e)].b : edges[size_t(e)].a;
        if (!seen[size_t(other)]) {
          seen[size_t(other)] = 1;
          next.push_back(other);
        }
      }
      std::ranges::sort(next, [&](int x, int y) { return degree[size_t(x)] > degree[size_t(y)]; });
      queue.insert(queue.end(), next.begin(), next.end());
    }
  }
  double rcx = 0, rcy = 0;  // the region's centre
  for (int t : tiles) {
    rcx += double(g.cx(g.col(t)));
    rcy += double(g.cy(g.row(t)));
  }
  rcx /= double(tiles.size());
  rcy /= double(tiles.size());
  for (int i : order) {
    double tx = 0, ty = 0;
    int    n = 0;
    for (int e : adj[size_t(i)]) {
      if (!edges[size_t(e)].kept) {
        continue;
      }
      const int other = edges[size_t(e)].a == i ? edges[size_t(e)].b : edges[size_t(e)].a;
      if (placed[size_t(other)]) {
        tx += px[size_t(other)];
        ty += py[size_t(other)];
        ++n;
      }
    }
    if (n == 0) {  // a root: its own home, pulled to the region's centre
      tx = (boxes.boxes[size_t(movable[size_t(i)])].cx + rcx) / 2;
      ty = (boxes.boxes[size_t(movable[size_t(i)])].cy + rcy) / 2;
    } else {
      tx /= n;
      ty /= n;
    }
    const auto& bx     = boxes.boxes[size_t(movable[size_t(i)])];
    int         best_t = -1;
    double      best_d = INFINITY;
    for (int t : free) {
      if (taken[size_t(t)]) {
        continue;
      }
      const double dd = std::abs(double(g.cx(g.col(t))) - tx) + std::abs(double(g.cy(g.row(t))) - ty);
      if (dd < best_d && box_fits(d, g, bx, t)) {
        best_d = dd;
        best_t = t;
      }
    }
    if (best_t < 0) {
      px[size_t(i)] = bx.cx;
      py[size_t(i)] = bx.cy;
    } else {
      taken[size_t(best_t)] = 1;
      px[size_t(i)]         = double(g.cx(g.col(best_t)));
      py[size_t(i)]         = double(g.cy(g.row(best_t)));
      out.homes.push_back({movable[size_t(i)], px[size_t(i)], py[size_t(i)], -1});
    }
    placed[size_t(i)] = 1;
  }
  return out;
}

}  // namespace

void sat_place(const Design& d, Grid& g, Boxes& boxes, const Legroute_regions& regions, Routing& R, Legroute_report& rep) {
  rep.regions         = size_t(g.regions);
  // Route-driven movable boxes acquire their ownership from the new cuts.
  // Reassigning a region without moving its home would invalidate the coarse
  // endpoints; the shared post-cut fitting screen checks room without moving it.
  rep.spilled_regions = movable_route_partition(d.opt) ? 0 : balance_regions(d, g, boxes);
  // fit_first: the same deterministic whole-core matching planning applied before its route and cuts.
  if (movable_route_partition(d.opt) && d.opt.fit_first && d.opt.fit_density > 0) {
    const auto match      = match_box_fit(d, g, boxes, true);
    rep.fit_matched_boxes = match.applied;
    rep.fit_max_move      = match.max_move;
  }
  // per box: in-box demand per direction, approximate (refined from legroute.final's results later): taps of the
  // external pins and the internal nets that leave the box layer
  std::vector<int> dem_h(boxes.boxes.size(), 0), dem_v(boxes.boxes.size(), 0);
  for (size_t b = 0; b < boxes.boxes.size(); ++b) {
    std::map<int, int> per_net;
    for (int c : boxes.boxes[b].cells) {
      for (int n : d.nets_of[c]) {
        if (d.nets[n].routed) {
          for (const auto& p : d.nets[n].pins) {
            per_net[n] += p.comp == d.movable[c];
          }
        }
      }
    }
    int                                  external = 0, internal = 0;
    // inbox_span: x spans of the internal nets (cell centres; pack_cells keeps the cells' x order)
    std::vector<std::pair<int64_t, int>> ends;
    for (const auto& [n, k] : per_net) {
      const bool inside               = k == int(d.nets[n].pins.size());
      (inside ? internal : external) += inside ? 1 : k;
      if (inside && d.opt.inbox_span) {
        int64_t lo = std::numeric_limits<int64_t>::max(), hi = std::numeric_limits<int64_t>::min();
        for (const auto& p : d.nets[n].pins) {
          const int64_t x = d.fp.components[p.comp].place.x + d.width[d.cell[p.comp]] / 2;
          lo              = std::min(lo, x);
          hi              = std::max(hi, x);
        }
        ends.emplace_back(lo, 1);
        ends.emplace_back(hi + 1, -1);
      }
    }
    int box_h = internal;  // the internal nets run on the box layer, counted with H
    if (d.opt.inbox_span) {
      std::ranges::sort(ends);
      int depth = 0;
      box_h     = 0;
      for (const auto& [x, e] : ends) {
        depth += e;
        box_h  = std::max(box_h, depth);
      }
    }
    dem_v[b] = (external + 2 * internal + 3) / 4;
    dem_h[b] = box_h + (external + 3) / 4;
  }
  // per tile and direction: summed tracks of the routed layers, less the clock spine (a vertical tap and trunk
  // anywhere, a rib every rib_tiles rows)
  std::vector<int> cap_h(g.size(), 0), cap_v(g.size(), 0);
  for (size_t l = 0; l < g.layers.size(); ++l) {
    for (size_t t = 0; t < g.size(); ++t) {
      (g.layers[l].horizontal ? cap_h : cap_v)[t] += g.cap[l][t];
    }
  }
  for (size_t t = 0; t < g.size(); ++t) {  // no layer is reserved for the boxes: the box layer's tracks are H too
    cap_h[t] += g.box_cap[t];
  }
  if (d.opt.clock_spine) {  // the same clock budget as planning: the flat floor and the L4 tree estimate
    const auto clock = clock_tree_charge(d, g, &boxes, d.opt.rib_tiles);
    R.clock_reserved_h.clear();
    R.clock_reserved_v.clear();
    for (size_t t = 0; t < g.size(); ++t) {
      const int v = std::max(1, clock.v[t]), h = std::max(int(g.row(int(t)) % std::max(1u, d.opt.rib_tiles) == 0), clock.h[t]);
      cap_v[t] -= v;
      cap_h[t] -= h;
      R.clock_reserved_v.push_back(v);
      R.clock_reserved_h.push_back(h);
    }
  }
  // endpoints of every routed signal net, per region
  const auto           box_of_comp = [&](int comp) { return comp >= 0 && d.cell[comp] >= 0 ? boxes.box_of[d.cell[comp]] : -1; };
  const auto           cap_h0 = cap_h, cap_v0 = cap_v;  // the tracks before hops and pre-placement
  const bool           all_signals = d.opt.partition_cells || d.opt.global_all;
  Global_routes        all_routes;
  std::optional<Boxes> routing_boxes;
  std::vector<int>     seed_regions;
  Route_cuts           partition_cuts;
  std::vector<char>    partition_critical(d.nets.size());
  for (size_t n = 0; n < d.nets.size(); ++n) {
    partition_critical[n] = d.nets[n].rank < d.opt.partition_critical;
  }
  std::vector<std::pair<double, double>> routed_homes;
  const auto                             route_all = [&] {
    routing_boxes.reset();
    seed_regions.clear();
    if (d.opt.global_keep && std::ranges::any_of(boxes.boxes, [&](const Box& b) {
          return !b.fixed || !b.placed || b.tile != g.tile_of(b.cx, b.cy);
        })) {
      throw std::invalid_argument("legroute.global_keep requires every box fixed on its legal home tile");
    }
    all_routes = route_all_signals(d, g, boxes, cap_h0, cap_v0);
    if (d.opt.partition_cells && !all_routes.unrouted) {
      partition_cuts = route_cuts(g,
                                  all_routes,
                                  partition_critical,
                                  cap_h0,
                                  cap_v0,
                                  admission_share(d.opt),
                                  d.opt.partition_critical_cost < 0);
    }
    rep.global_seconds              += all_routes.seconds;
    rep.global_search_pops          += all_routes.search_pops;
    rep.global_search_limited       += all_routes.search_limited;
    rep.global_pattern_attempts     += all_routes.pattern_attempts;
    rep.global_pattern_completed    += all_routes.pattern_completed;
    rep.global_pattern_fallbacks    += all_routes.pattern_fallbacks;
    rep.global_pattern_work         += all_routes.pattern_work;
    rep.global_pattern_work_limited += all_routes.pattern_work_limited;
    ++rep.partition_route_calls;
    routed_homes.clear();
    for (const auto& b : boxes.boxes) {
      routed_homes.emplace_back(b.cx, b.cy);
    }
  };
  // allow_unfit: a box-fit deficit alone does not stop admission yet because the box route seed's joint
  // matching (Hall repair inside each region) may still give every box a distinct tile; it is checked again on
  // the seed's own boxes.
  const auto require_admission = [&](bool regions_ready, bool allow_unfit = false) {
    auto gates
        = assess_legroute_gates(d, g, routing_boxes ? &*routing_boxes : &boxes, all_routes, {}, cap_h0, cap_v0, regions_ready);
    // Before proposed cuts, only the global route screen can stop admission.
    // Existing seed regions may be replaced by a feasible route-driven mesh.
    const bool unfit_only = allow_unfit && gates.unfittable_regions && !gates.oversized_regions && !gates.cell_dense_regions
                            && !gates.routing_dense_edges && !gates.overflow_tracks && !gates.unrouted;
    const bool blocked    = regions_ready ? gates.requires_replan() && !unfit_only
                                          : gates.unrouted || gates.routing_dense_edges || gates.overflow_tracks;
    if (!d.opt.output_dir.empty()) {
      std::filesystem::create_directories(d.opt.output_dir);
      std::ofstream report(d.opt.output_dir + "/pre_sat.json");
      report << legroute_gate_json(gates) << '\n';
      if (!report) {
        throw std::runtime_error("legroute: cannot write pre-SAT admission report");
      }
    }
    if (blocked) {
      throw Legroute_replan(std::move(gates));
    }
  };
  // Every SAT entry has an all-signal necessary screen, including legacy seed meshes.
  // Only configured all-signal flows consume these trees in the regional crossing model.
  route_all();
  require_admission(false);
  if (d.opt.partition_cells) {
    const auto          started = Clock::now();
    std::vector<size_t> cells(g.size());
    for (const auto& b : boxes.boxes) {
      cells[size_t(b.tile >= 0 ? b.tile : g.tile_of(b.cx, b.cy))] += b.cells.size();
    }
    auto partition = partition_routes(g,
                                      cells,
                                      all_routes,
                                      partition_critical,
                                      cap_h0,
                                      cap_v0,
                                      admission_share(d.opt),
                                      d.opt.partition_cells,
                                      d.opt.partition_critical_cost,
                                      d.opt.partition_mesh);
    g.region       = std::move(partition.region);
    g.regions      = partition.regions;
    if (movable_route_partition(d.opt)) {
      const auto fitting = repair_partition_fit(d, g, boxes, partition_cuts);
      if (fitting.transfers) {
        // Report the ownership actually handed to SAT after the fitting repair.
        std::vector<size_t> counts(size_t(g.regions));
        partition.oversized = partition.max_cells = partition.crossings = partition.critical_crossings = 0;
        for (size_t t = 0; t < g.size(); ++t) {
          counts[size_t(g.region[t])] += cells[t];
          for (bool h : {true, false}) {
            if (h ? g.col(int(t)) + 1 >= g.cols : g.row(int(t)) + 1 >= g.rows) {
              continue;
            }
            const size_t q = t + (h ? 1 : size_t(g.cols));
            if (g.region[t] != g.region[q]) {
              partition.crossings          += (h ? partition_cuts.load_h : partition_cuts.load_v)[t];
              partition.critical_crossings += (h ? partition_cuts.critical_h : partition_cuts.critical_v)[t];
            }
          }
        }
        for (size_t count : counts) {
          partition.oversized += count > d.opt.partition_cells;
          partition.max_cells  = std::max(partition.max_cells, count);
        }
      }
    }
    g.region_tiles.assign(size_t(g.regions), {});
    for (size_t t = 0; t < g.size(); ++t) {
      g.region_tiles[size_t(g.region[t])].push_back(int(t));
    }
    for (auto& b : boxes.boxes) {
      b.region = g.region[size_t(b.tile >= 0 ? b.tile : g.tile_of(b.cx, b.cy))];
    }
    rep.regions                      = size_t(g.regions);
    rep.partition_oversized          = partition.oversized;
    rep.partition_indivisible_cells  = partition.indivisible_cells;
    rep.partition_crossings          = partition.crossings;
    rep.partition_critical_crossings = partition.critical_crossings;
    rep.partition_max_cells          = partition.max_cells;
    rep.partition_seconds            = seconds_since(started);
    std::fprintf(
        stderr,
        "legroute partition: %d regions, %zu oversized, largest %zu cells (target %u, indivisible component %zu), %.3f s\n",
        g.regions,
        partition.oversized,
        partition.max_cells,
        d.opt.partition_cells,
        partition.indivisible_cells,
        rep.partition_seconds);
  }
  require_admission(true, d.opt.box_route_seed);
  if (d.opt.box_route_seed) {
    ++rep.box_route_seed_attempts;
    auto seed                        = prepare_box_route_seed(d, g, boxes, cap_h0, cap_v0, "local");
    rep.global_seconds              += seed.routes.seconds;
    rep.global_search_pops          += seed.routes.search_pops;
    rep.global_search_limited       += seed.routes.search_limited;
    rep.global_pattern_attempts     += seed.routes.pattern_attempts;
    rep.global_pattern_completed    += seed.routes.pattern_completed;
    rep.global_pattern_fallbacks    += seed.routes.pattern_fallbacks;
    rep.global_pattern_work         += seed.routes.pattern_work;
    rep.global_pattern_work_limited += seed.routes.pattern_work_limited;
    rep.partition_route_calls       += !seed.routes.trees.empty();
    if (seed.accepted) {
      ++rep.box_route_seed_successes;
      routing_boxes           = std::move(seed.boxes);
      seed_regions            = g.region;
      all_routes              = std::move(seed.routes);
      partition_cuts          = route_cuts(g,
                                           all_routes,
                                           partition_critical,
                                           cap_h0,
                                           cap_v0,
                                           admission_share(d.opt),
                                           d.opt.partition_critical_cost < 0);
      rep.partition_crossings = rep.partition_critical_crossings = 0;
      for (size_t t = 0; t < g.size(); ++t) {
        for (bool h : {true, false}) {
          if (h ? g.col(int(t)) + 1 >= g.cols : g.row(int(t)) + 1 >= g.rows) {
            continue;
          }
          const size_t q = t + (h ? 1 : size_t(g.cols));
          if (g.region[t] != g.region[q]) {
            rep.partition_crossings          += (h ? partition_cuts.load_h : partition_cuts.load_v)[t];
            rep.partition_critical_crossings += (h ? partition_cuts.critical_h : partition_cuts.critical_v)[t];
          }
        }
      }
      require_admission(true);
    } else {
      require_admission(true);  // no seed: a box-fit deficit stops admission as before
    }
  }
  // the policies: the options, and with sat_alt a second set; each region solves under both at once and keeps the
  // better answer
  std::vector<Legroute_options> policies{d.opt};
  if (!d.opt.sat_alt.empty()) {
    policies.push_back(sat_alt_options(d.opt));
  }
  struct Policy {
    Boxes                                     boxes;
    std::vector<int>                          cap_h, cap_v;
    std::set<std::pair<int, int>>             treed;    // (net, region) routed by a fixed tree
    std::unordered_map<int, std::vector<int>> allowed;  // box -> the only tiles it may take (an IO pre-route's track)
    std::vector<Fixed_tree>                   trees;
    std::vector<Io_route>                     io_routes;
    std::vector<char>                         pinned_tile;
    Edge_reserve                              reserve;  // the fixed routes' tracks per edge
    Legroute_report                           rep;      // pre-placement counters
  };
  std::vector<std::vector<Region_out>> last(policies.size(), std::vector<Region_out>(g.regions));
  std::vector<int>                     chosen(g.regions, 0);
  std::vector<Policy>                  P;
  Crossings                            x;
  std::vector<uint64_t>                sig_prev(size_t(g.regions), 0);
  std::vector<unsigned>         region_extra(size_t(g.regions), 0);  // window_grow: extra window tiles of a region that failed
  std::vector<int>              gave_to(size_t(g.regions), -1);      // spill_shift: the region each region last handed a strip to
  std::set<std::pair<int, int>> joined_pairs;                        // mesh_join: region pairs joined and resplit once
  // Window growth and local alternatives do not change the crossing problem. Cache its pristine answer, before
  // hop_choice annotates/moves endpoints, and the capacities after the hops. Compare actual geometry, not a hash:
  // a spill or mesh repair invalidates it; box tile/fixed/window changes do not affect crossings' home endpoints.
  Crossings                     cached_crossings;
  std::vector<int>              cached_regions, cached_cap_h, cached_cap_v;
  std::vector<std::tuple<int, double, double>>  cached_homes;
  std::mutex                                    deadline_mutex;
  std::vector<std::optional<Clock::time_point>> region_deadlines(size_t(g.regions));
  const auto                                    deadline_for = [&](int r) {
    std::lock_guard lock(deadline_mutex);
    auto&           end = region_deadlines[size_t(r)];
    if (!end) {
      end = Clock::now() + std::chrono::milliseconds(d.opt.sat_ms);
    }
    return *end;
  };
  unsigned spill_round = 0, timeout_round = 0;
  for (unsigned round = 0;; ++round) {
    // A new mesh invalidates the provisional endpoint ownership. Rebuild the
    // original-center forest before considering any cached crossings.
    if (routing_boxes && seed_regions != g.region) {
      route_all();
    }
    // spill rounds: the region adjacency (a boundary shift changes it)
    std::vector<std::set<int>> region_adj(size_t(g.regions));
    for (int t = 0; t < int(g.size()); ++t) {
      for (int q : {g.col(t) + 1 < g.cols ? t + 1 : -1, t + g.cols < int(g.size()) ? t + g.cols : -1}) {
        if (q >= 0 && g.region[t] != g.region[q]) {
          region_adj[size_t(g.region[t])].insert(g.region[q]);
          region_adj[size_t(g.region[q])].insert(g.region[t]);
        }
      }
    }
    cap_h = cap_h0;  // the crossings take the hops' tracks off these: a fresh copy each round, not a cumulative one
    cap_v = cap_v0;
    std::vector<std::tuple<int, double, double>> homes;
    if (d.opt.spill_rounds > 0 || d.opt.partition_timeout_rounds > 0) {
      homes.reserve(boxes.boxes.size());
      for (const auto& b : boxes.boxes) {
        homes.emplace_back(b.region, b.cx, b.cy);
      }
    }
    if (round > 0 && cached_regions == g.region && cached_homes == homes) {
      x     = cached_crossings;
      cap_h = cached_cap_h;
      cap_v = cached_cap_v;
      ++rep.crossings_reused;
    } else {
      // A genuine change of regional geometry starts a separately reported
      // attempt. Window growth alone reuses crossings and the old deadline.
      region_deadlines.assign(size_t(g.regions), {});
      bool changed = routed_homes.size() != boxes.boxes.size();
      for (size_t b = 0; !changed && b < boxes.boxes.size(); ++b) {
        changed = routed_homes[b] != std::pair{boxes.boxes[b].cx, boxes.boxes[b].cy};
      }
      if (changed) {
        route_all();
      }
      require_admission(true);
      x = crossings(d,
                    g,
                    routing_boxes ? *routing_boxes : boxes,
                    regions,
                    cap_h,
                    cap_v,
                    round == 0,
                    all_signals ? &all_routes : nullptr);
      ++rep.crossings_built;
      rep.global_seconds              += x.global_seconds;
      rep.global_search_pops          += x.global_search_pops;
      rep.global_search_limited       += x.global_search_limited;
      rep.global_pattern_attempts     += x.pattern_attempts;
      rep.global_pattern_completed    += x.pattern_completed;
      rep.global_pattern_fallbacks    += x.pattern_fallbacks;
      rep.global_pattern_work         += x.pattern_work;
      rep.global_pattern_work_limited += x.pattern_work_limited;
      if (d.opt.spill_rounds > 0 || d.opt.partition_timeout_rounds > 0) {
        cached_crossings = x;
        cached_regions   = g.region;
        cached_homes     = std::move(homes);
        cached_cap_h     = cap_h;
        cached_cap_v     = cap_v;
      }
    }
    // legal_shuffle: the legal boxes on the tiles of an edge the global router left over its share may move (the
    // router's congestion says where the legalization's luck ran out); every other legal box stays fixed
    if (d.opt.legal_boxes && d.opt.legal_shuffle > 0 && !x.global_over.empty()) {
      std::vector<int> box_at(g.size(), -1);
      for (size_t b = 0; b < boxes.boxes.size(); ++b) {
        if (boxes.boxes[b].placed && boxes.boxes[b].fixed) {
          box_at[size_t(g.tile_of(boxes.boxes[b].cx, boxes.boxes[b].cy))] = int(b);
        }
      }
      size_t freed = 0;
      for (const auto& [t, h] : x.global_over) {
        for (int q : {t, h ? t + 1 : t + g.cols}) {
          if (q >= 0 && q < int(g.size()) && box_at[size_t(q)] >= 0 && boxes.boxes[size_t(box_at[size_t(q)])].fixed) {
            auto& bx  = boxes.boxes[size_t(box_at[size_t(q)])];
            bx.fixed  = false;
            bx.window = int(d.opt.legal_shuffle);
            ++freed;
          }
        }
      }
      rep.legal_shuffled += freed;
      std::fprintf(stderr,
                   "legroute: %zu legal boxes freed to move %u tiles at the %zu edges the global router left over\n",
                   freed,
                   d.opt.legal_shuffle,
                   x.global_over.size());
    }
    P.assign(policies.size(), {});
    for (size_t p = 0; p < policies.size(); ++p) {  // the policy's pre-placement on its own copy
      const auto& o = policies[p];
      if (movable_route_partition(d.opt) && (o.tree_boxes > 0 || o.tree_load > 0 || o.io_preroute)) {
        throw std::runtime_error(
            "legroute coarse witness repair required: legacy tree/IO pre-routing cannot replace a preserved movable backbone");
      }
      auto& Q = P[p];
      Q.boxes = boxes;
      Q.cap_h = cap_h;
      Q.cap_v = cap_v;
      Q.treed.insert(x.retained.begin(), x.retained.end());
      for (const auto& gp : x.pieces) {  // legacy whole-region routes; partial retention already filtered its endpoints
        if (gp.part < 0) {
          Q.treed.emplace(gp.net, gp.region);
        }
      }
      if (o.crit_boxes > 0) {
        place_critical(d, o, g, Q.boxes, x.ends, Q.rep);
      }
      if (o.tree_boxes > 0 || o.tree_load > 0) {
        Q.trees = place_trees(d, o, g, Q.boxes, x.ends, dem_h, dem_v, Q.cap_h, Q.cap_v, Q.treed, Q.rep);
      }
      if (o.io_preroute) {
        Q.io_routes = io_preroute(d, o, g, Q.boxes, x.ends, Q.cap_h, Q.cap_v, cap_h0, cap_v0, Q.treed, Q.allowed, Q.rep);
      }
      if (o.crit_boxes > 0 || !Q.trees.empty()) {
        repair_forced(d, o, g, Q.boxes, x.ends, dem_h, dem_v, Q.cap_h, Q.cap_v, Q.trees, Q.treed, Q.rep);
      }
      Q.pinned_tile.assign(g.size(), 0);
      for (const auto& box : Q.boxes.boxes) {
        if (box.fixed) {
          Q.pinned_tile[box.tile] = 1;
        }
      }
      Q.reserve = edge_reserve(g, x.hops, x.hop_net, Q.trees, Q.io_routes, x.pieces);
    }
    // ---- one SAT model per region and policy (the policies in parallel)
    std::vector<std::vector<int>> region_boxes(g.regions), region_tiles(g.regions);
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      region_boxes[boxes.boxes[b].region].push_back(b);
    }
    for (int t = 0; t < int(g.size()); ++t) {
      region_tiles[g.region[t]].push_back(t);
    }
    // a region's movable boxes must each find a tile they fit among its unpinned tiles (the pins took the rest): a
    // box a matching leaves out splits into its cells, which fit more tiles, until every box has one or none is left
    // to split (the solve then reports the region unfit and a spill round sheds its excess)
    {
      size_t split = 0;
      for (int r = 0; r < g.regions; ++r) {
        for (bool again = true; again;) {
          again = false;
          std::vector<int> movable, tiles;
          for (int b : region_boxes[size_t(r)]) {
            if (!P[0].boxes.boxes[size_t(b)].fixed && !P[0].allowed.contains(b)) {
              movable.push_back(b);
            }
          }
          for (int t : region_tiles[size_t(r)]) {
            if (!g.blocked[t] && !P[0].pinned_tile[size_t(t)]) {
              tiles.push_back(t);
            }
          }
          std::unordered_map<int, int> owner;  // tile -> box
          std::vector<char>            seen(g.size(), 0);
          std::function<bool(int)>     augment = [&](int b) {
            for (int t : tiles) {
              if (seen[size_t(t)] || !box_fits(d, g, boxes.boxes[size_t(b)], t)) {
                continue;
              }
              seen[size_t(t)] = 1;
              const auto it   = owner.find(t);
              if (it == owner.end() || augment(it->second)) {
                owner[t] = b;
                return true;
              }
            }
            return false;
          };
          for (int b : movable) {
            seen.assign(g.size(), 0);
            if (augment(b) || boxes.boxes[size_t(b)].cells.size() < 2) {
              continue;
            }
            if (movable_route_partition(d.opt)) {
              throw std::runtime_error(std::format(
                  "legroute coarse witness repair required: box {} in region {} needs regrouping; rebuild its global attachments",
                  b,
                  r));
            }
            auto& box = boxes.boxes[size_t(b)];  // split: the first cell keeps the box, the others get their own
            for (size_t k = 1; k < box.cells.size(); ++k) {
              Box single                         = box;
              single.cells                       = {box.cells[k]};
              single.sites                       = cell_sites(d, g, box.cells[k]);
              single.internal                    = 0;
              single.external                    = int(d.nets_of[size_t(box.cells[k])].size());
              single.fixed                       = false;
              const int id                       = int(boxes.boxes.size());
              boxes.box_of[size_t(box.cells[k])] = id;
              region_boxes[size_t(r)].push_back(id);
              for (auto& pol : P) {
                pol.boxes.boxes.push_back(single);
                pol.boxes.box_of[size_t(box.cells[k])] = id;
              }
              boxes.boxes.push_back(std::move(single));
            }
            auto& kept = boxes.boxes[size_t(b)];
            kept.cells.resize(1);
            kept.sites    = cell_sites(d, g, kept.cells[0]);
            kept.internal = 0;
            kept.external = int(d.nets_of[size_t(kept.cells[0])].size());
            for (auto& pol : P) {
              pol.boxes.boxes[size_t(b)].cells    = kept.cells;
              pol.boxes.boxes[size_t(b)].sites    = kept.sites;
              pol.boxes.boxes[size_t(b)].internal = 0;
              pol.boxes.boxes[size_t(b)].external = kept.external;
            }
            ++split;
            again = true;
            break;  // the lists changed: match the region again
          }
        }
      }
      if (split > 0) {
        std::fprintf(
            stderr,
            "legroute sat: round %u: %zu boxes without a fitting tile among their region's unpinned tiles split into their cells\n",
            round + 1,
            split);
      }
    }
    // a region whose boxes, fixed boxes, hops, global pieces, trees and IO pre-routes are what they were in the last
    // round keeps its answer (one policy): only the regions a spill touched solve again
    std::vector<char> reuse(size_t(g.regions), 0);
    {
      std::vector<uint64_t> sig(size_t(g.regions), 0x9e3779b97f4a7c15ull);
      const auto            mix = [](uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
      for (int r = 0; r < g.regions; ++r) {
        mix(sig[size_t(r)], region_extra[size_t(r)]);
        // A repaired boundary can remove routing tiles without moving any box or hop. Such a region's previous
        // routes are no longer a proof for its new domain, even if all endpoint signatures still match.
        for (int t : region_tiles[size_t(r)]) {
          mix(sig[size_t(r)], uint64_t(t) + 1);
        }
        for (int b : region_boxes[size_t(r)]) {
          mix(sig[size_t(r)], uint64_t(b) * 3 + (boxes.boxes[size_t(b)].fixed ? 1 : 0));
          mix(sig[size_t(r)], uint64_t(boxes.boxes[size_t(b)].tile + 1));
          mix(sig[size_t(r)], routing_boxes ? uint64_t(routing_boxes->boxes[size_t(b)].tile + 1) : 0);
          if (P[0].allowed.contains(b)) {
            mix(sig[size_t(r)], 7);
          }
        }
      }
      for (size_t k = 0; k < x.hops.size(); ++k) {
        const auto [a, b] = x.hops[k];
        for (int r : {g.region[a], g.region[b]}) {
          mix(sig[size_t(r)], (uint64_t(x.hop_net[k]) << 32) ^ (uint64_t(a) << 16) ^ uint64_t(b));
        }
      }
      for (size_t n = 0; n < x.ends.size(); ++n) {
        for (const auto& [r, list] : x.ends[n].per_region) {
          mix(sig[size_t(r)], n);
          mix(sig[size_t(r)], list.size());
          for (const auto& e : list) {
            mix(sig[size_t(r)], uint64_t(e.box + 1));
            mix(sig[size_t(r)], uint64_t(e.tile + 1));
            mix(sig[size_t(r)], uint64_t(e.part + 1));
            mix(sig[size_t(r)], uint64_t(e.anchor + 1));
            mix(sig[size_t(r)], uint64_t(e.interface_id + 1));
          }
        }
      }
      for (const auto& gp : x.pieces) {
        mix(sig[size_t(gp.region)], (uint64_t(gp.net) << 32) ^ (uint64_t(gp.a) << 16) ^ uint64_t(gp.b));
        mix(sig[size_t(gp.region)], uint64_t(gp.part + 1) * 2 + gp.h_first);
        mix(sig[size_t(gp.region)], gp.backbone);
        mix(sig[size_t(gp.region)], gp.refinable);
      }
      for (const auto& [net, region] : x.retained) {
        mix(sig[size_t(region)], 0x47b6d90000000000ull ^ uint64_t(net));
      }
      for (const auto& [net, region, part] : x.retained_parts) {
        mix(sig[size_t(region)], 0x47b6d90000000000ull ^ (uint64_t(net) << 32) ^ uint64_t(part));
      }
      for (const auto& ft : P[0].trees) {
        for (const auto& piece : ft.pieces) {
          mix(sig[size_t(piece.region)],
              (uint64_t(piece.net) << 32) ^ (uint64_t(piece.a) << 16) ^ uint64_t(piece.b) ^ (ft.dropped ? 1 : 0));
        }
      }
      for (const auto& io : P[0].io_routes) {
        mix(sig[size_t(io.region)], (uint64_t(io.net_a) << 32) ^ (uint64_t(io.ta) << 16) ^ uint64_t(io.tb));
      }
      for (int r = 0; r < g.regions; ++r) {
        reuse[size_t(r)] = round > 0 && policies.size() == 1 && sig[size_t(r)] == sig_prev[size_t(r)]
                           && last[0][size_t(r)].status == Sat_status::satisfiable;
      }
      sig_prev = sig;
    }
    rep.sat_reused_regions += size_t(std::ranges::count(reuse, char(1)));
    std::fprintf(stderr,
                 "legroute sat: %d regions, %zu virtual pins, %zu boxes, %zu hops (%zu over the share, %zu unjoined), %zu global "
                 "pieces; solving %zu at a time\n",
                 g.regions,
                 regions.pins.size(),
                 boxes.boxes.size(),
                 x.hops.size(),
                 x.over_share,
                 x.unjoined,
                 x.pieces.size(),
                 size_t(std::max(1u, d.opt.threads)));
    // the regions most at risk first: box fill plus the share of the region's tracks the routes fixed ahead took
    // (hops, pieces, trees, IO pre-routes). A failing design then fails early (fail_fast ends the solve at the
    // first failure), and the long solves start first, so the threads end together
    std::vector<double> risk(size_t(g.regions), 0), cut_ratio(size_t(g.regions), 0);
    size_t              over_cut = 0;
    {
      std::vector<double> free_t(size_t(g.regions), 0), cap(size_t(g.regions), 0), taken(size_t(g.regions), 0);
      for (int t = 0; t < int(g.size()); ++t) {
        const auto r  = size_t(g.region[t]);
        free_t[r]    += !g.blocked[t];
        cap[r]       += cap_h0[t] + cap_v0[t];
        taken[r]     += P[0].reserve.h[size_t(t)] + P[0].reserve.v[size_t(t)];
      }
      // the cut bound from the placement (the exact one is in solve_region): nets whose endpoints' windows are held
      // on both sides of a tile boundary of the region, over the tracks of the boundary's edges; a ratio above one
      // is unsat before any solving, so these regions go first
      struct Rect {
        int c0, c1, r0, r1;
      };
      std::vector<Rect> rect(size_t(g.regions), {g.cols, -1, g.rows, -1});
      for (int t = 0; t < int(g.size()); ++t) {
        auto& rc = rect[size_t(g.region[t])];
        rc       = {std::min(rc.c0, g.col(t)), std::max(rc.c1, g.col(t)), std::min(rc.r0, g.row(t)), std::max(rc.r1, g.row(t))};
      }
      std::vector<std::vector<int>> fc(size_t(g.regions)), fr(size_t(g.regions));
      for (int r = 0; r < g.regions; ++r) {
        fc[size_t(r)].assign(size_t(std::max(0, rect[size_t(r)].c1 - rect[size_t(r)].c0) + 2), 0);
        fr[size_t(r)].assign(size_t(std::max(0, rect[size_t(r)].r1 - rect[size_t(r)].r0) + 2), 0);
      }
      const int   win_t = int(policies[0].window_tiles);
      const auto& Q0    = P[0];
      for (size_t n = 0; n < d.nets.size(); ++n) {
        for (const auto& [r, eps_r] : x.ends[n].per_region) {
          const auto& rc = rect[size_t(r)];
          if (eps_r.size() < 2 || rc.c1 < 0 || Q0.treed.contains({int(n), r})) {
            continue;
          }
          std::vector<std::pair<int, int>> cols, rows;
          for (const auto& [part, group] : endpoint_groups(eps_r)) {
            if (group.size() < 2) {
              continue;
            }
            int lo_c = g.cols, hi_c = -1, lo_r = g.rows, hi_r = -1;
            for (const auto& ep : group) {
              int ec0, ec1, er0, er1;
              if (ep.box >= 0 && !Q0.boxes.boxes[size_t(ep.box)].fixed) {
                const auto& bx = Q0.boxes.boxes[size_t(ep.box)];
                const int   cc = std::clamp(g.col_of(bx.cx), rc.c0, rc.c1), rr = std::clamp(g.row_of(bx.cy), rc.r0, rc.r1);
                ec0 = std::max(rc.c0, cc - win_t);
                ec1 = std::min(rc.c1, cc + win_t);
                er0 = std::max(rc.r0, rr - win_t);
                er1 = std::min(rc.r1, rr + win_t);
              } else {
                const int t = ep.box >= 0 ? Q0.boxes.boxes[size_t(ep.box)].tile : ep.tile;
                ec0 = ec1 = std::clamp(g.col(t), rc.c0, rc.c1);
                er0 = er1 = std::clamp(g.row(t), rc.r0, rc.r1);
              }
              lo_c = std::min(lo_c, ec1);
              hi_c = std::max(hi_c, ec0);
              lo_r = std::min(lo_r, er1);
              hi_r = std::max(hi_r, er0);
            }
            if (group.size() == 2 && d.nets[n].pins.size() == 2 && group[0].box >= 0 && group[1].box >= 0) {
              if (hi_c == lo_c + 1) {  // may sit side by side: no track
                hi_c = lo_c;
              }
              if (hi_r == lo_r + 1) {
                hi_r = lo_r;
              }
            }
            cols.emplace_back(lo_c, hi_c);
            rows.emplace_back(lo_r, hi_r);
          }
          add_forced_cuts(cols, fc[size_t(r)], rc.c0, Q0.reserve.cuts_of(int(n), r, 1));
          add_forced_cuts(rows, fr[size_t(r)], rc.r0, Q0.reserve.cuts_of(int(n), r, 0));
        }
      }
      for (int r = 0; r < g.regions; ++r) {
        const auto& rc = rect[size_t(r)];
        if (rc.c1 >= 0) {
          for (int c = rc.c0 + 1, run = 0; c <= rc.c1; ++c) {
            run        += fc[size_t(r)][size_t(c - rc.c0)];
            int tracks  = 0;
            for (int j = rc.r0; j <= rc.r1; ++j) {
              const int t = g.tile(c - 1, j), q = g.tile(c, j);
              if (g.region[t] == r && g.region[q] == r) {
                tracks += std::max(0, edge_limit(std::min(cap_h0[t], cap_h0[q]), policies[0]) - Q0.reserve.h[size_t(t)]);
              }
            }
            cut_ratio[size_t(r)] = std::max(cut_ratio[size_t(r)], double(run) / std::max(1.0, double(tracks)));
          }
          for (int j = rc.r0 + 1, run = 0; j <= rc.r1; ++j) {
            run        += fr[size_t(r)][size_t(j - rc.r0)];
            int tracks  = 0;
            for (int i = rc.c0; i <= rc.c1; ++i) {
              const int t = g.tile(i, j - 1), q = g.tile(i, j);
              if (g.region[t] == r && g.region[q] == r) {
                tracks += std::max(0, edge_limit(std::min(cap_v0[t], cap_v0[q]), policies[0]) - Q0.reserve.v[size_t(t)]);
              }
            }
            cut_ratio[size_t(r)] = std::max(cut_ratio[size_t(r)], double(run) / std::max(1.0, double(tracks)));
          }
        }
        over_cut        += cut_ratio[size_t(r)] > 1;
        risk[size_t(r)]  = cut_ratio[size_t(r)] + double(region_boxes[r].size()) / std::max(1.0, free_t[size_t(r)])
                           + taken[size_t(r)] / std::max(1.0, cap[size_t(r)]);
      }
    }
    std::vector<size_t> task_order(size_t(g.regions) * policies.size());
    std::iota(task_order.begin(), task_order.end(), 0);
    std::ranges::stable_sort(task_order, [&](size_t a, size_t b) { return risk[a / policies.size()] > risk[b / policies.size()]; });
    {
      std::string head;
      for (size_t k = 0; k < std::min<size_t>(8, task_order.size()); k += policies.size()) {
        const auto r  = task_order[k] / policies.size();
        head         += std::format(" {}({:.2f}, cut {:.2f})", r, risk[r], cut_ratio[r]);
      }
      std::fprintf(stderr,
                   "legroute sat: regions by risk (cut ratio + fill + fixed share), %zu over the cut bound by the placement, "
                   "first:%s\n",
                   over_cut,
                   head.c_str());
    }
    // Interface choices flow from higher-risk regions to all lower-risk neighbors.
    // Ordinary graph coloring can put a low-risk region back in wave zero and
    // accidentally let it constrain a harder neighbor. Independent regions still
    // solve together; every shared interface follows the risk order.
    std::vector<unsigned>                         wave(size_t(g.regions), 0);
    unsigned                                      waves = 1;
    std::vector<std::vector<int>>                 hop_tiles(x.hops.size());   // per hop: the owner's candidate tiles
    std::vector<std::vector<std::pair<int, int>>> hop_across(x.hops.size());  // per hop: (owner tile, the tile across)
    std::vector<int>                              hop_owner(x.hops.size(), -1), hop_other_tile(x.hops.size(), -1);
    std::vector<int>                              hop_room(g.size(), 0);  // per boundary tile: hops it may still take
    // First corridor solve frees routes and cells with the admitted crossings fixed.
    // A failed solve can request interface repair; it does not silently move a neighbor.
    if (d.opt.hop_choice > 0 && policies.size() == 1 && !movable_route_partition(d.opt)) {
      std::vector<size_t> priority;
      for (size_t k = 0; k < task_order.size(); k += policies.size()) {
        priority.push_back(task_order[k] / policies.size());
      }
      wave = region_solve_waves(g, priority);
      for (unsigned w : wave) {
        waves = std::max(waves, w + 1);
      }
      std::vector<char> net_fixed(d.nets.size(), 0);  // nets with routes fixed ahead keep their hops
      if (movable_route_partition(d.opt)) {
        for (const auto& [n, region, part] : x.retained_parts) {
          (void)region;
          (void)part;
          net_fixed[size_t(n)] = 1;  // includes isolated retained components with no emitted interior piece
        }
      }
      for (const auto& gp : x.pieces) {
        if (!gp.backbone) {
          net_fixed[size_t(gp.net)] = 1;
        }
      }
      for (const auto& [nr, rr] : P[0].treed) {
        net_fixed[size_t(nr)] = 1;
      }
      for (const auto& io : P[0].io_routes) {
        if (io.net_a >= 0) {
          net_fixed[size_t(io.net_a)] = 1;
        }
        if (io.net_b >= 0) {
          net_fixed[size_t(io.net_b)] = 1;
        }
      }
      std::vector<int>  fixed_hops(g.size(), 0), default_hops(g.size(), 0);
      std::vector<char> hop_free(x.hops.size(), 0);
      size_t            hops_free = 0;  // this round's
      for (size_t k = 0; k < x.hops.size(); ++k) {
        const auto [a, b] = x.hops[k];
        hop_free[k]       = !net_fixed[size_t(x.hop_net[k])] && g.region[a] != g.region[b]
                            && wave[size_t(g.region[a])] != wave[size_t(g.region[b])];
        if (!hop_free[k]) {
          ++fixed_hops[size_t(a)];
          ++fixed_hops[size_t(b)];
        }
      }
      for (size_t k = 0; k < x.hops.size(); ++k) {
        if (!hop_free[k]) {
          continue;
        }
        const auto [a, b] = x.hops[k];
        const int  ra = g.region[a], rb = g.region[b];
        const bool a_owns = wave[size_t(ra)] < wave[size_t(rb)];
        const int  ao = a_owns ? a : b, bo = a_owns ? b : a, owner = a_owns ? ra : rb, other = a_owns ? rb : ra;
        const bool h  = g.row(a) == g.row(b);  // across a vertical boundary: the candidates run along the rows
        const int  dc = g.col(bo) - g.col(ao), dr = g.row(bo) - g.row(ao), hc = int(d.opt.hop_choice);
        ++default_hops[size_t(ao)];
        for (int i = -hc; i <= hc; ++i) {
          const int c = g.col(ao) + (h ? 0 : i), rr = g.row(ao) + (h ? i : 0);
          if (c < 0 || c >= g.cols || rr < 0 || rr >= g.rows || c + dc < 0 || c + dc >= g.cols || rr + dr < 0
              || rr + dr >= g.rows) {
            continue;
          }
          const int t = g.tile(c, rr), q = g.tile(c + dc, rr + dr);
          if (g.region[t] != owner || g.region[q] != other || g.blocked[t] || g.blocked[q]) {
            continue;
          }
          // the crossing uses the edge t-q: the narrower endpoint bounds it, under the share and the policy ceiling
          const int edge = std::min((h ? cap_h0 : cap_v0)[size_t(t)], (h ? cap_h0 : cap_v0)[size_t(q)]);
          const int room
              = std::min(int(std::floor(d.opt.hop_share * double(edge))), edge_limit(edge, d.opt)) - fixed_hops[size_t(t)];
          if (room <= 0 && t != ao) {
            continue;  // no room there; the chooser's own tile stays a candidate
          }
          hop_room[size_t(t)] = std::max(hop_room[size_t(t)], room);
          hop_tiles[k].push_back(t);
          hop_across[k].emplace_back(t, q);
        }
        if (hop_tiles[k].size() < 2) {  // nothing to choose
          hop_tiles[k].clear();
          hop_across[k].clear();
          continue;
        }
        hop_owner[k]      = owner;
        hop_other_tile[k] = bo;
        auto& eps_o       = x.ends[size_t(x.hop_net[k])].per_region[owner];
        for (auto& e : eps_o) {  // the owner's endpoint is free (its tile stays the default for every other reader)
          if (e.box < 0 && e.hop < 0 && (movable_route_partition(d.opt) ? e.interface_id == int(k) : e.tile == ao)) {
            e.hop = int(k);
            break;
          }
        }
        ++hops_free;
      }
      rep.sat_hops_free += hops_free;
      for (size_t t = 0; t < g.size(); ++t) {  // the hops already there stay possible
        hop_room[t] = std::max(hop_room[t], default_hops[t]);
      }
      std::fprintf(stderr,
                   "legroute sat: hop_choice %u: %u waves, %zu of %zu hops free (within %u tiles of their chooser's tile)\n",
                   d.opt.hop_choice,
                   waves,
                   hops_free,
                   x.hops.size(),
                   d.opt.hop_choice);
    }
    rep.sat_waves += waves;
    std::atomic<bool> failed_fast{false};
    // Policies, checker phases and alternatives share this geometry's
    // deadline; initialization occurs only when its first worker starts.
    // every (region, policy) solve is a task: the regions of one wave are independent once the hops are fixed
    const auto        solve_task = [&](size_t slot) {
      const size_t task  = task_order[slot];
      const int    r     = int(task / policies.size());
      const size_t p     = task % policies.size();
      auto&        Q     = P[p];
      auto&        out   = last[p][r];
      const auto   start = Clock::now();
      if (reuse[size_t(r)]) {  // a spill round: nothing of this region changed, its answer stands
        for (const auto& [b, t] : out.tiles) {
          Q.boxes.boxes[size_t(b)].tile = t;
        }
        return;
      }
      out = Region_out{};
      if (failed_fast.load()) {  // fail_fast: the placement restarts anyway; the region's boxes take fitting tiles, no model
        out.skipped = true;
        out.status  = Sat_status::unknown;
        ++out.rep.sat_fail_fast_skipped;
      }
      auto po          = policies[p];
      po.window_tiles += region_extra[size_t(r)];
      try {
        solve_region(d,
                     po,
                     g,
                     r,
                     region_boxes[r],
                     region_tiles[r],
                     Q.boxes,
                     x.ends,
                     Q.treed,
                     Q.allowed,
                     Q.pinned_tile,
                     policies.size() > 1 ? std::format(" policy {}", p) : "",
                     cap_h0,
                     cap_v0,
                     Q.reserve,
                     hop_tiles,
                     hop_room,
                     out,
                     deadline_for(r),
                     routing_boxes ? &*routing_boxes : nullptr);
      } catch (const Legroute_unfit& e) {  // more boxes than fitting tiles: a spill round pushes boxes out first
        if (spill_round >= d.opt.spill_rounds || policies.size() > 1) {
          throw;
        }
        const size_t without = out.unfit_boxes;
        out                  = Region_out{};
        out.status           = Sat_status::unsatisfiable;
        out.unfit            = e.what();
        out.unfit_boxes      = std::max<size_t>(1, without);
        std::fprintf(stderr, "legroute sat: %s; a spill round pushes its boxes out\n", e.what());
      }
      out.seconds = seconds_since(start);
      if (d.opt.fail_fast && out.status != Sat_status::satisfiable && !out.skipped) {
        if (!failed_fast.exchange(true)) {
          std::fprintf(stderr, "legroute sat: fail fast: region %d has no routable answer, the other regions are left\n", r);
        }
      }
      for (const auto& ft : Q.trees) {  // the policy's fixed trees and IO pre-routes in the region
        for (const auto& piece : ft.pieces) {
          if (!ft.dropped && piece.region == r) {
            out.conns.push_back({piece.net, piece.a, piece.b, true});
          }
        }
      }
      append_global_pieces(out, r, x.pieces);
      for (const auto& io : Q.io_routes) {  // (a box off its lifted track bends to it)
        if (io.region != r) {
          continue;
        }
        if (io.box < 0) {
          out.conns.push_back({io.net_a, io.ta, io.tb, io.h_first});
        } else {
          out.conns.push_back({io.net_a, io.ta, Q.boxes.boxes[io.box].tile, io.h_first});
          out.conns.push_back({io.net_b, Q.boxes.boxes[io.box].tile, io.tb, io.h_first});
        }
      }
      for (const auto& c : out.conns) {  // the timing proxy: weighted route length (tiles)
        out.cost += d.nets[c.net].weight * double(std::abs(g.col(c.ta) - g.col(c.tb)) + std::abs(g.row(c.ta) - g.row(c.tb)));
      }
    };
    for (unsigned w = 0; w < waves; ++w) {
      std::vector<size_t> slots;
      for (size_t k = 0; k < task_order.size(); ++k) {
        if (wave[task_order[k] / policies.size()] == w) {
          slots.push_back(k);
        }
      }
      lr::parallel(slots.size(), d.opt.threads, [&](size_t i) { solve_task(slots[i]); });
      if (w + 1 < waves) {  // this wave's hop choices are the later regions' fixed endpoints; the reserves follow
        for (size_t k = 0; k < x.hops.size(); ++k) {
          if (hop_owner[k] < 0 || wave[size_t(hop_owner[k])] != w) {
            continue;
          }
          const auto& res   = last[0][size_t(hop_owner[k])];
          const auto [a, b] = x.hops[k];
          const int ao      = g.region[a] == hop_owner[k] ? a : b;
          int       picked  = -1;
          if (res.status == Sat_status::satisfiable) {
            if (const auto found = res.hop_choices.find(int(k)); found != res.hop_choices.end()) {
              picked = found->second;
            }
          }
          if (picked < 0) {
            picked = ao;  // the region failed or kept the default: the hop stays
          }
          int across = -1;
          for (const auto& [t, q] : hop_across[k]) {
            if (t == picked) {
              across = q;
              break;
            }
          }
          const int n = x.hop_net[k];
          for (auto& e : x.ends[size_t(n)].per_region[hop_owner[k]]) {
            if (e.box < 0 && e.hop == int(k)) {
              e.tile = across >= 0 ? picked : ao;
              e.hop  = -1;
            }
          }
          if (across < 0 || picked == ao) {
            continue;
          }
          const int other = g.region[across];
          if (wave[size_t(other)] <= w) {
            throw std::runtime_error(
                std::format("legroute shared interface repair required: hop {} changes already solved region {}; rerun affected "
                            "placement and routing",
                            k,
                            other));
          }
          reuse[size_t(other)] = 0;  // an earlier spill round's answer used the old interface
          ++rep.sat_hops_moved;
          x.hops[k]     = {picked, across};
          bool remapped = false;
          for (auto& e : x.ends[size_t(n)].per_region[other]) {
            if (e.box < 0 && e.hop < 0
                && (movable_route_partition(d.opt) ? e.interface_id == int(k) : e.tile == hop_other_tile[k])) {
              e.tile   = across;
              remapped = true;
              break;
            }
          }
          if (!remapped && movable_route_partition(d.opt)) {
            throw std::runtime_error(
                std::format("legroute shared interface repair required: hop {} lost its neighbor attachment", k));
          }
        }
        // every moved crossing, in either mode (the legacy chooser is the one that moves hops), against the
        // narrower endpoint before publication
        validate_crossing_edges(g, x.hops, x.hop_net, cap_h0, cap_v0, d.opt.global_share, d.opt.cap_scale, d.opt.density_ceiling);
        for (auto& Q : P) {
          Q.reserve = edge_reserve(g, x.hops, x.hop_net, Q.trees, Q.io_routes, x.pieces);
        }
      }
    }
    // every published crossing, whatever chose it (one wave, several policies, the movable forest or the legacy
    // construction's own boundary choice): the union against the narrower endpoint before any answer is accepted
    // The movable modes publish only screened crossings: a violation is a repair. The legacy fixed-IO construction
    // may still pick a least-loaded boundary above the share: recorded and reported, not yet repaired.
    try {
      validate_crossing_edges(g, x.hops, x.hop_net, cap_h0, cap_v0, d.opt.global_share, d.opt.cap_scale, d.opt.density_ceiling);
    } catch (const Crossing_capacity_error& e) {  // structural crossing errors always propagate
      if (movable_route_partition(d.opt)) {
        throw;
      }
      rep.crossing_policy_invalid = true;  // a legacy diagnostic result: not a passing density gate
      if (!rep.crossing_over_policy++) {
        std::fprintf(stderr, "legroute sat: legacy crossing over the density policy (reported, not repaired): %s\n", e.what());
      }
    }
    for (int r = 0; r < g.regions; ++r) {  // a routable answer first, then the lower cost; none: the first policy's
      int best = -1;
      for (int p = 0; p < int(policies.size()); ++p) {
        if (last[size_t(p)][r].status == Sat_status::satisfiable
            && (best < 0 || last[size_t(p)][r].cost < last[size_t(best)][r].cost)) {
          best = p;
        }
      }
      chosen[r] = std::max(best, 0);
    }
    std::vector<int> failed;
    for (int r = 0; r < g.regions; ++r) {
      if (last[size_t(chosen[r])][size_t(r)].status != Sat_status::satisfiable) {
        failed.push_back(r);
      }
    }
    if (!failed.empty() && timeout_round < d.opt.partition_timeout_rounds) {
      std::vector<size_t> cells(g.size());
      for (const auto& b : boxes.boxes) {
        cells[size_t(g.tile_of(b.cx, b.cy))] += b.cells.size();
      }
      std::vector<unsigned> targets(size_t(g.regions), 0);
      unsigned              nominal_target = d.opt.partition_cells;
      for (unsigned attempt = 0; attempt <= timeout_round && nominal_target > 1; ++attempt) {
        nominal_target = std::max(1u, nominal_target / 2);
      }
      for (int r : failed) {
        const auto& out = last[size_t(chosen[r])][size_t(r)];
        if (out.status != Sat_status::unknown || out.skipped) {
          continue;  // UNSAT and fail-fast omissions are not timed-out searches.
        }
        size_t count = 0;
        for (int t : region_tiles[size_t(r)]) {
          count += cells[size_t(t)];
        }
        if (count > 1) {
          targets[size_t(r)] = unsigned(std::min<size_t>(count - 1, nominal_target));
        }
      }
      const auto started = Clock::now();
      auto refined = refine_route_regions(g, cells, partition_cuts, d.opt.partition_critical_cost, targets, d.opt.partition_mesh);
      rep.partition_seconds += seconds_since(started);
      if (refined != g.region) {
        const int before = g.regions;
        size_t    split  = 0;
        for (int r = 0; r < before; ++r) {
          split += std::ranges::any_of(region_tiles[size_t(r)], [&](int t) { return refined[size_t(t)] != r; });
        }
        g.region  = std::move(refined);
        g.regions = *std::max_element(g.region.begin(), g.region.end()) + 1;
        g.region_tiles.assign(size_t(g.regions), {});
        std::vector<size_t> counts(size_t(g.regions), 0);
        for (size_t t = 0; t < g.size(); ++t) {
          g.region_tiles[size_t(g.region[t])].push_back(int(t));
          counts[size_t(g.region[t])] += cells[t];
        }
        for (auto& b : boxes.boxes) {
          b.region = g.region[size_t(g.tile_of(b.cx, b.cy))];
        }
        for (auto& answers : last) {
          answers.resize(size_t(g.regions));
        }
        chosen.resize(size_t(g.regions), 0);
        sig_prev.resize(size_t(g.regions), 0);
        region_extra.resize(size_t(g.regions), 0);
        gave_to.resize(size_t(g.regions), -1);
        rep.regions = size_t(g.regions);
        rep.partition_oversized
            = size_t(std::ranges::count_if(counts, [&](size_t count) { return count > d.opt.partition_cells; }));
        rep.partition_max_cells = *std::max_element(counts.begin(), counts.end());
        rep.partition_crossings = rep.partition_critical_crossings = 0;
        for (size_t t = 0; t < g.size(); ++t) {
          for (bool h : {true, false}) {
            if (h ? g.col(int(t)) + 1 >= g.cols : g.row(int(t)) + 1 >= g.rows) {
              continue;
            }
            if (g.region[t] != g.region[t + (h ? 1 : size_t(g.cols))]) {
              rep.partition_crossings          += (h ? partition_cuts.load_h : partition_cuts.load_v)[t];
              rep.partition_critical_crossings += (h ? partition_cuts.critical_h : partition_cuts.critical_v)[t];
            }
          }
        }
        ++timeout_round;
        ++rep.partition_timeout_rounds;
        rep.partition_timeout_regions += split;
        std::fprintf(stderr,
                     "legroute partition: timeout recut %u: split %zu regions, %d -> %d regions; global trees retained\n",
                     timeout_round,
                     split,
                     before,
                     g.regions);
        continue;
      }
      if (std::ranges::any_of(targets, [](unsigned target) { return target != 0; })) {
        std::fprintf(stderr, "legroute partition: no admissible refinement of timed-out regions; current boundaries retained\n");
      }
    }
    if (failed.empty() || spill_round >= d.opt.spill_rounds || policies.size() > 1) {
      break;
    }
    // spill: each failing region pushes about spill_fraction of its boxes into its neighbours with room (the next ring
    // of regions too when the neighbours' room is short), the boxes and their neighbours chosen by min cuts inside the
    // region (spill_cut), each box onto the nearest free tile it fits in its new region; the boxes keep their cells,
    // the next round redoes the crossings and solves what changed. A failing neighbour receives too: the two sides
    // of a hot boundary fail together, and a box crossing it toward its nets takes its hops with it
    // a box the policy pinned (critical, tree, IO pre-route) is fixed on the policy's copy of the boxes
    const auto       is_fixed = [&](int b) { return P[0].boxes.boxes[size_t(b)].fixed || P[0].allowed.contains(b); };
    std::vector<int> boxes_in(size_t(g.regions), 0), fit_tiles(size_t(g.regions), 0);
    for (int b = 0; b < int(boxes.boxes.size()); ++b) {
      boxes_in[size_t(boxes.boxes[size_t(b)].region)] += !is_fixed(b);
    }
    for (int t = 0; t < int(g.size()); ++t) {  // tiles a movable box can take: unblocked and not pinned by a fixed box
      fit_tiles[size_t(g.region[t])] += !g.blocked[t] && !P[0].pinned_tile[size_t(t)];
    }
    std::set<int>            taken;  // tiles given to boxes this round
    size_t                   moved = 0, grown = 0;
    std::vector<std::string> kinds;  // alternatives, in priority order
    for (size_t at = 0; at < d.opt.alternatives.size();) {
      const size_t comma = d.opt.alternatives.find(',', at);
      const auto   kind  = d.opt.alternatives.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
      if (kind != "noise" && kind != "anchor" && kind != "spread" && kind != "tsm" && kind != "checker") {
        throw std::invalid_argument("legroute: unknown alternative placement '" + kind + "' (noise, anchor, spread, tsm, checker)");
      }
      kinds.push_back(kind);
      at = comma == std::string::npos ? d.opt.alternatives.size() : comma + 1;
    }
    // mesh_join (user, 2026-11-02): a boundary the crossings overload, or a line inside a region its own placement
    // overloads (the cut bound), is fixed by the mesh, not the placement: the two regions across the boundary join and
    // their union splits the other way (a row boundary bad: split by a column line, and the reverse), so the nets that
    // crossed it stay inside one half; a cut-bound line inside a region joins the region with a neighbour beside the
    // line (across a boundary perpendicular to it) and splits parallel to the line, so the line gets the union's width
    // of tracks. The split balances the boxes. Each pair joins once; the next round solves what changed.
    if (d.opt.mesh_join) {
      size_t            joins = 0;
      // region_tiles/region_boxes and the verdicts describe the mesh at the start of this round. A region can
      // participate in only one repair until those lists and the crossings have been rebuilt.
      std::vector<char> paired(size_t(g.regions), 0);
      for (int r : failed) {
        if (paired[size_t(r)]) {
          continue;
        }
        const auto& own          = last[size_t(chosen[r])][size_t(r)];
        // the boundary to join across: (neighbour, its boundary is a row line)
        int         nb           = -1;
        bool        row_boundary = false;
        if (own.status == Sat_status::unsatisfiable && own.cut_at >= 0) {  // a line inside r: join beside it
          // a row line (cut_h false) needs width: the neighbour across a column boundary (left or right) with the
          // longest shared side; a column line needs height: the neighbour above or below
          std::map<int, int> shared;
          for (int t : region_tiles[size_t(r)]) {
            for (int q : tile_neighbours(g, t)) {
              const bool row_side = g.col(q) == g.col(t);  // q above or below t: a row boundary between them
              if (g.region[q] != r && !paired[size_t(g.region[q])] && row_side == own.cut_h) {
                ++shared[g.region[q]];
              }
            }
          }
          for (const auto& [q, n] : shared) {
            if (nb < 0 || n > shared[nb]) {
              nb = q;
            }
          }
          row_boundary = own.cut_h;
        } else {                                          // the boundary of r the hops load most against its tracks
          std::map<int, std::pair<double, double>> load;  // neighbour -> (hops, tracks)
          for (const auto& [a, b] : x.hops) {
            if ((g.region[a] == r) != (g.region[b] == r)) {
              load[g.region[a] == r ? g.region[b] : g.region[a]].first += 1;
            }
          }
          for (int t : region_tiles[size_t(r)]) {
            for (int q : tile_neighbours(g, t)) {
              if (g.region[q] != r) {
                const bool h              = g.row(q) == g.row(t);
                load[g.region[q]].second += std::max(0, std::min((h ? cap_h0 : cap_v0)[t], (h ? cap_h0 : cap_v0)[q]));
              }
            }
          }
          double worst = 0;
          for (const auto& [q, hv] : load) {
            if (paired[size_t(q)]) {
              continue;
            }
            const double ratio = hv.first / std::max(1.0, hv.second);
            if (ratio > worst) {
              worst = ratio;
              nb    = q;
            }
          }
          if (nb >= 0) {  // the boundary's orientation: a row line when the neighbour is above or below
            int rows = 0, cols = 0;
            for (int t : region_tiles[size_t(r)]) {
              for (int q : tile_neighbours(g, t)) {
                if (g.region[q] == nb) {
                  (g.col(q) == g.col(t) ? rows : cols) += 1;
                }
              }
            }
            row_boundary = rows >= cols;
          }
        }
        if (nb < 0 || joined_pairs.contains({std::min(r, nb), std::max(r, nb)})) {
          continue;
        }
        // the union, split the other way: by a column line for a row boundary joined, by a row line otherwise, at the
        // line that balances the boxes (the regions keep their ids: r the lower / left part)
        std::vector<int> U = region_tiles[size_t(r)];
        U.insert(U.end(), region_tiles[size_t(nb)].begin(), region_tiles[size_t(nb)].end());
        std::map<int, int> boxes_at;  // column (or row) -> boxes homed there
        for (int b : region_boxes[size_t(r)]) {
          const int t = g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy);
          ++boxes_at[row_boundary ? g.col(t) : g.row(t)];
        }
        for (int b : region_boxes[size_t(nb)]) {
          const int t = g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy);
          ++boxes_at[row_boundary ? g.col(t) : g.row(t)];
        }
        int total = 0;
        for (const auto& [k, n] : boxes_at) {
          total += n;
        }
        int line = -1, acc = 0;  // the first column / row of the upper part
        for (const auto& [k, n] : boxes_at) {
          if (acc >= total / 2) {
            line = k;
            break;
          }
          acc += n;
        }
        if (line < 0 || acc == 0) {
          continue;  // every box on one side: nothing to balance
        }
        auto candidate = g.region;
        for (int t : U) {
          candidate[size_t(t)] = (row_boundary ? g.col(t) : g.row(t)) < line ? r : nb;
        }
        if (!connected_regions(g, candidate) || (d.opt.partition_cells && !partition_cuts.allows(g, candidate))) {
          ++rep.partition_rejected_repairs;
          std::fprintf(
              stderr,
              "legroute partition: rejected repair of regions %d and %d across a forbidden edge or with a disconnected region\n",
              r,
              nb);
          continue;
        }
        joined_pairs.insert({std::min(r, nb), std::max(r, nb)});
        paired[size_t(r)] = paired[size_t(nb)] = 1;
        g.region                               = std::move(candidate);
        for (int b : region_boxes[size_t(r)]) {
          boxes.boxes[size_t(b)].region = g.region[size_t(g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy))];
        }
        for (int b : region_boxes[size_t(nb)]) {
          boxes.boxes[size_t(b)].region = g.region[size_t(g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy))];
        }
        ++joins;
        std::fprintf(
            stderr,
            "legroute sat: spill round %u: region %d (%s) joins region %d across their %s boundary; the union splits at %s %d\n",
            round + 1,
            r,
            own.cut_at >= 0 ? "cut-bound" : "hops over its boundary",
            nb,
            row_boundary ? "row" : "column",
            row_boundary ? "column" : "row",
            line);
      }
      if (joins > 0) {
        rep.mesh_joins += joins;
        ++rep.spill_rounds;
        ++spill_round;
        g.region_tiles.assign(size_t(g.regions), {});
        for (size_t t = 0; t < g.size(); ++t) {
          g.region_tiles[size_t(g.region[t])].push_back(int(t));
        }
        continue;  // the next round redoes the crossings on the new mesh and solves what changed
      }
    }
    for (int r : failed) {
      if (!kinds.empty() && Clock::now() < deadline_for(r)) {  // alternatives share the current regional allowance
        std::vector<int> movable;
        for (int b : region_boxes[size_t(r)]) {
          if (!P[0].boxes.boxes[size_t(b)].fixed && !P[0].allowed.contains(b)) {
            movable.push_back(b);
          }
        }
        std::vector<Alternative> cands;
        for (const auto& kind : kinds) {
          if (kind == "noise") {
            cands.push_back(
                alt_noise(g, boxes, movable, region_tiles[size_t(r)], int(d.opt.window_tiles), 1234u + round * 131u + unsigned(r)));
          } else if (kind == "anchor") {
            cands.push_back(alt_anchor(g, boxes, movable, region_tiles[size_t(r)]));
          } else if (kind == "spread") {
            cands.push_back(alt_spread(g, boxes, movable, region_tiles[size_t(r)]));
          } else if (kind == "checker") {  // phase one's windows: wide on the untracked tiles (the parity alternates by round)
            Alternative c{"checker", {}};
            for (int b : movable) {
              const auto& bx = boxes.boxes[size_t(b)];
              const int   t  = g.tile_of(bx.cx, bx.cy);
              const bool  on = (g.col(t) + g.row(t) + int(round)) % 2 == 0;
              c.homes.push_back({b, bx.cx, bx.cy, on ? int(d.opt.checker_window) : 2 * int(d.opt.window_tiles)});
            }
            cands.push_back(std::move(c));
          } else {
            cands.push_back(alt_tsm(d, g, boxes, x.ends, movable, region_tiles[size_t(r)], r, P[0].pinned_tile));
          }
        }
        std::erase_if(cands, [](const Alternative& a) { return a.homes.empty(); });
        std::vector<Region_out>             outs(cands.size());
        const std::vector<std::vector<int>> no_free_hops(hop_tiles.size());
        const auto                          t0 = Clock::now();
        // Preserve one worker per alternative, but join every worker and
        // propagate model/construction errors to the transactional caller.
        parallel(cands.size(), unsigned(cands.size()), [&](size_t c) {
          Boxes bx = P[0].boxes;
          for (const auto& h : cands[c].homes) {
            bx.boxes[size_t(h.box)].placed = false;  // a legal box off its legal tile is an ordinary box
            bx.boxes[size_t(h.box)].cx     = h.cx;
            bx.boxes[size_t(h.box)].cy     = h.cy;
            bx.boxes[size_t(h.box)].window = h.window;
          }
          auto po          = policies[0];
          po.window_tiles += region_extra[size_t(r)];
          if (d.opt.alt_ms > 0) {
            po.sat_ms = d.opt.alt_ms;
          }
          const auto started = Clock::now();
          try {
            if (cands[c].kind == "checker") {  // the relaxed model first; its answer seeds the full one
              std::vector<char> board(g.size(), 0);
              for (int t : region_tiles[size_t(r)]) {
                board[size_t(t)] = (g.col(t) + g.row(t) + int(round)) % 2 == 0;
              }
              auto relaxed      = po;
              relaxed.untracked = &board;
              Region_out phase;
              solve_region(d,
                           relaxed,
                           g,
                           r,
                           region_boxes[size_t(r)],
                           region_tiles[size_t(r)],
                           bx,
                           x.ends,
                           P[0].treed,
                           P[0].allowed,
                           P[0].pinned_tile,
                           " alternative checker (relaxed)",
                           cap_h0,
                           cap_v0,
                           P[0].reserve,
                           no_free_hops,
                           hop_room,
                           phase,
                           deadline_for(r));
              if (phase.status != Sat_status::satisfiable) {
                outs[c] = std::move(phase);  // UNSAT relaxed: the region needs less density, not another placement
              } else {
                cands[c].homes.clear();
                for (const auto& [b, t] : phase.tiles) {
                  if (!bx.boxes[size_t(b)].fixed && !P[0].allowed.contains(b)) {
                    cands[c].homes.push_back({b, double(g.cx(g.col(t))), double(g.cy(g.row(t))), 4});
                  }
                }
                for (const auto& h : cands[c].homes) {
                  bx.boxes[size_t(h.box)].placed = false;
                  bx.boxes[size_t(h.box)].cx     = h.cx;
                  bx.boxes[size_t(h.box)].cy     = h.cy;
                  bx.boxes[size_t(h.box)].window = h.window;
                }
                solve_region(d,
                             po,
                             g,
                             r,
                             region_boxes[size_t(r)],
                             region_tiles[size_t(r)],
                             bx,
                             x.ends,
                             P[0].treed,
                             P[0].allowed,
                             P[0].pinned_tile,
                             " alternative checker",
                             cap_h0,
                             cap_v0,
                             P[0].reserve,
                             no_free_hops,
                             hop_room,
                             outs[c],
                             deadline_for(r));
              }
            } else {
              solve_region(d,
                           po,
                           g,
                           r,
                           region_boxes[size_t(r)],
                           region_tiles[size_t(r)],
                           bx,
                           x.ends,
                           P[0].treed,
                           P[0].allowed,
                           P[0].pinned_tile,
                           " alternative " + cands[c].kind,
                           cap_h0,
                           cap_v0,
                           P[0].reserve,
                           no_free_hops,
                           hop_room,
                           outs[c],
                           deadline_for(r));
            }
          } catch (const Legroute_unfit&) {
            outs[c].status = Sat_status::unsatisfiable;
          }
          outs[c].seconds = seconds_since(started);
        });
        rep.alt_tried   += cands.size();
        rep.alt_seconds += seconds_since(t0);
        Legroute_report::Alt_trial trial;
        trial.region = r;
        trial.round  = round + 1;
        trial.boxes  = movable.size();
        {
          size_t free_here = 0;
          for (int t : region_tiles[size_t(r)]) {
            free_here    += !g.blocked[t];
            trial.pinned += P[0].pinned_tile[size_t(t)] != 0;
          }
          trial.fill = free_here ? double(movable.size()) / double(free_here) : 0;
          trial.cut  = cut_ratio[size_t(r)];
          for (const auto& [a, b] : x.hops) {
            trial.hops += g.region[a] == r || g.region[b] == r;
          }
          for (int t : region_tiles[size_t(r)]) {  // the boundary's tracks: the edges to the tiles of other regions
            for (int q : tile_neighbours(g, t)) {
              if (g.region[q] != r) {
                const bool h = g.row(q) == g.row(t);
                trial.boundary_tracks
                    += size_t(std::max(0, edge_limit(std::min((h ? cap_h0 : cap_v0)[t], (h ? cap_h0 : cap_v0)[q]), policies[0])));
              }
            }
          }
          const auto& own = last[size_t(chosen[r])][size_t(r)];
          trial.status    = own.status == Sat_status::unsatisfiable ? (own.rep.sat_cut_regions ? "cut" : "unsat") : "unknown";
        }
        std::string line;
        int         winner = -1;
        for (size_t c = 0; c < cands.size(); ++c) {
          const auto st  = outs[c].status == Sat_status::satisfiable     ? "sat"
                           : outs[c].status == Sat_status::unsatisfiable ? (outs[c].rep.sat_cut_regions ? "cut" : "unsat")
                                                                         : "unknown";
          line          += std::format(" {} {} {:.1f} s", cands[c].kind, st, outs[c].seconds);
          trial.families.emplace_back(cands[c].kind, st, outs[c].seconds);
          if (winner < 0 && outs[c].status == Sat_status::satisfiable) {
            winner = int(c);
          }
        }
        if (winner >= 0) {
          trial.adopted = cands[size_t(winner)].kind;
        }
        std::fprintf(stderr,
                     "legroute sat: spill round %u: region %d (fill %.2f, cut %.2f, %zu hops over %zu boundary tracks, %s) "
                     "alternatives:%s%s\n",
                     round + 1,
                     r,
                     trial.fill,
                     trial.cut,
                     trial.hops,
                     trial.boundary_tracks,
                     trial.status.c_str(),
                     line.c_str(),
                     winner >= 0 ? std::format(" -> {} adopted", cands[size_t(winner)].kind).c_str() : " -> none routed");
        rep.alt_trials.push_back(std::move(trial));
        if (winner >= 0) {  // the candidate's answer stands; its homes stay private (the crossings and the neighbours'
                            // hops come from the homes, and the answer was solved with this round's hops); no spill here
          adopt_region_alternative(last[0][size_t(r)], std::move(outs[size_t(winner)]), r, x.pieces);
          ++rep.alt_rescued;
          ++grown;  // the round changed something: it goes on
          continue;
        }
      }
      // window_grow: freedom of movement first; the region spills once its windows are grown twice
      if (d.opt.window_grow > 0 && region_extra[size_t(r)] < 2 * d.opt.window_grow) {
        region_extra[size_t(r)] += d.opt.window_grow;
        ++grown;
        std::fprintf(stderr,
                     "legroute sat: spill round %u: region %d widens its windows to %u tiles\n",
                     round + 1,
                     r,
                     d.opt.window_tiles + region_extra[size_t(r)]);
        continue;
      }
      std::vector<int> cand;
      for (int b : region_boxes[size_t(r)]) {
        if (!is_fixed(b)) {
          cand.push_back(b);
        }
      }
      size_t want = std::min(cand.size(),
                             std::max<size_t>(1, size_t(std::ceil(d.opt.spill_fraction * double(region_boxes[size_t(r)].size())))));
      if (d.opt.spill_shift) {    // the boundary moves: the strip next to the neighbour the nets reach most, boxes included
        std::vector<int> around;  // never straight back to a region that just handed r a strip (no ping-pong)
        for (int s2 : region_adj[size_t(r)]) {
          if (gave_to[size_t(s2)] != r) {
            around.push_back(s2);
          }
        }
        const auto pulls = spill_pulls(d, x.ends, r, around);
        size_t     best  = 0;
        for (size_t j = 0; j < pulls.size(); ++j) {
          if (pulls[j] > pulls[best]) {
            best = j;
          }
        }
        if (around.empty() || pulls[best] == 0) {
          std::fprintf(stderr, "legroute sat: spill round %u: region %d has no neighbour its nets reach\n", round + 1, r);
          continue;
        }
        const int        s2 = around[best];
        std::vector<int> strip;
        size_t           on_strip = 0;
        int              deep     = 0;
        for (int k = 1; k <= 4; ++k) {  // the smallest depth holding the wanted boxes; r keeps a connected quarter at least
          const auto try_strip = boundary_strip(g, r, s2, k);
          if (try_strip.size() * 4 > region_tiles[size_t(r)].size() * 3 || !connected_without(g, r, try_strip)) {
            break;
          }
          const std::set<int> in_strip(try_strip.begin(), try_strip.end());
          size_t              n = 0;
          for (int b : region_boxes[size_t(r)]) {  // a box's tile is set by the answer: its home is its centroid's tile
            n += in_strip.contains(g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy));
          }
          strip    = try_strip;
          on_strip = n;
          deep     = k;
          if (n >= std::max<size_t>(1, want)) {
            break;
          }
        }
        if (strip.empty()) {
          std::fprintf(stderr,
                       "legroute sat: spill round %u: region %d cannot hand a strip to region %d (it would split or empty)\n",
                       round + 1,
                       r,
                       s2);
          continue;
        }
        auto candidate = g.region;
        for (int t : strip) {
          candidate[size_t(t)] = s2;
        }
        if (!connected_regions(g, candidate) || (d.opt.partition_cells && !partition_cuts.allows(g, candidate))) {
          ++rep.partition_rejected_repairs;
          std::fprintf(
              stderr,
              "legroute partition: rejected shift from region %d to %d across a forbidden edge or with a disconnected region\n",
              r,
              s2);
          continue;
        }
        g.region = std::move(candidate);
        const std::set<int> in_strip(strip.begin(), strip.end());
        for (int b : region_boxes[size_t(r)]) {
          if (in_strip.contains(g.tile_of(boxes.boxes[size_t(b)].cx, boxes.boxes[size_t(b)].cy))) {
            boxes.boxes[size_t(b)].region = s2;
          }
        }
        g.region_tiles.assign(size_t(g.regions), {});
        for (int t = 0; t < int(g.size()); ++t) {
          g.region_tiles[size_t(g.region[t])].push_back(t);
        }
        moved              += on_strip + 1;  // the tiles moved even when no box did
        gave_to[size_t(r)]  = s2;
        rep.shifted_tiles  += strip.size();
        std::fprintf(stderr,
                     "legroute sat: spill round %u: region %d hands %zu tiles (%d deep, %zu boxes) next to region %d to it\n",
                     round + 1,
                     r,
                     strip.size(),
                     deep,
                     on_strip,
                     s2);
        continue;
      }
      if (cand.empty()) {
        std::fprintf(stderr, "legroute sat: spill round %u: region %d has no movable box\n", round + 1, r);
        continue;
      }
      // a region with more movable boxes than tiles they fit (the pinned boxes took the rest) must shed the excess
      // whatever the spill fraction: that is a hard infeasibility, not a routing one
      size_t fit_here = 0;
      for (int t : region_tiles[size_t(r)]) {
        fit_here += !g.blocked[t] && !P[0].pinned_tile[size_t(t)] && box_fits(d, g, boxes.boxes[size_t(cand[cand.size() / 2])], t);
      }
      const size_t excess
          = std::max(cand.size() > fit_here ? cand.size() - fit_here : 0, last[size_t(chosen[r])][size_t(r)].unfit_boxes);
      if (d.opt.spill_fraction <= 0 && excess == 0) {
        continue;  // no box spill: the rounds carry the alternatives, the windows and the shift alone
      }
      if (excess > 0) {
        want = std::max(want, excess);
        std::fprintf(stderr,
                     "legroute sat: spill round %u: region %d holds %zu movable boxes for %zu fitting tiles: %zu must leave\n",
                     round + 1,
                     r,
                     cand.size(),
                     fit_here,
                     excess);
      }
      // a region's room: up to the fill that evens it with the failing region, and never past nine tenths of its
      // tiles (the solver needs slack); its tiles counted as those a typical box leaving (the median width) fits (a
      // tile a tap or an obstacle narrows takes no full box); the balance after the round catches the rest
      std::vector<int> by_sites = cand;
      std::ranges::sort(by_sites, [&](int a, int b) { return boxes.boxes[size_t(a)].sites < boxes.boxes[size_t(b)].sites; });
      const int          typical = by_sites.empty() ? -1 : by_sites[by_sites.size() / 2];
      std::map<int, int> fits_in;  // per region: tiles the typical box fits
      const auto         fit_count = [&](int s2) {
        auto it = fits_in.find(s2);
        if (it == fits_in.end()) {
          int n = 0;
          for (int t : region_tiles[size_t(s2)]) {
            n += !g.blocked[t] && !P[0].pinned_tile[size_t(t)] && (typical < 0 || box_fits(d, g, boxes.boxes[size_t(typical)], t));
          }
          it = fits_in.emplace(s2, n).first;
        }
        return it->second;
      };
      const auto room_of = [&](int s2) {
        const double fill = double(boxes_in[size_t(r)] + boxes_in[size_t(s2)]) / std::max(1, fit_tiles[size_t(r)] + fit_count(s2));
        return int(std::floor(std::min(0.9, fill) * double(fit_count(s2)))) - boxes_in[size_t(s2)];
      };
      std::vector<Spill_target> to;
      int                       room_sum = 0;
      const auto                add_ring = [&](const std::set<int>& ring) {
        for (int s2 : ring) {
          const int room = room_of(s2);
          if (s2 == r || room <= 0 || std::ranges::any_of(to, [&](const auto& x2) { return x2.region == s2; })) {
            continue;
          }
          to.push_back({s2, room});
          room_sum += room;
        }
      };
      add_ring(region_adj[size_t(r)]);
      if (size_t(room_sum) < want) {  // the neighbours are short of room: their neighbours too
        std::set<int> ring2;
        for (int s2 : region_adj[size_t(r)]) {
          for (int s3 : region_adj[size_t(s2)]) {
            if (!region_adj[size_t(r)].contains(s3)) {
              ring2.insert(s3);
            }
          }
        }
        add_ring(ring2);
      }
      if (to.empty()) {
        std::string around;
        for (int s2 : region_adj[size_t(r)]) {
          around += std::format(" {} (fill {:.2f}, room {})",
                                s2,
                                double(boxes_in[size_t(s2)]) / std::max(1, fit_tiles[size_t(s2)]),
                                room_of(s2));
        }
        std::fprintf(stderr,
                     "legroute sat: spill round %u: region %d (%zu boxes, fill %.2f) has no region around it with room:%s\n",
                     round + 1,
                     r,
                     region_boxes[size_t(r)].size(),
                     double(boxes_in[size_t(r)]) / std::max(1, fit_tiles[size_t(r)]),
                     around.c_str());
        continue;
      }
      const auto         picks = spill_cut(d, boxes, x.ends, cand, r, to, want);
      std::map<int, int> per_target;
      for (const auto& [b, s2] : picks) {
        auto&  bx     = boxes.boxes[size_t(b)];
        int    best_t = -1;
        double best_d = INFINITY;
        for (int t : region_tiles[size_t(s2)]) {
          if (g.blocked[t] || taken.contains(t) || P[0].pinned_tile[size_t(t)] || !box_fits(d, g, bx, t)) {
            continue;
          }
          const double dd = std::abs(double(g.cx(g.col(t))) - bx.cx) + std::abs(double(g.cy(g.row(t))) - bx.cy);
          if (dd < best_d) {
            best_d = dd;
            best_t = t;
          }
        }
        if (best_t < 0) {
          continue;
        }
        taken.insert(best_t);
        bx.region = s2;
        bx.tile   = best_t;
        bx.placed = false;  // a legal box spilled elsewhere is an ordinary box there
        bx.cx     = double(g.cx(g.col(best_t)));
        bx.cy     = double(g.cy(g.row(best_t)));
        ++boxes_in[size_t(s2)];
        --boxes_in[size_t(r)];
        ++per_target[s2];
        ++moved;
      }
      std::string where;
      for (const auto& tg : to) {
        where += std::format(" {} -> region {} (room {})",
                             per_target.contains(tg.region) ? per_target[tg.region] : 0,
                             tg.region,
                             tg.room);
      }
      std::fprintf(stderr,
                   "legroute sat: spill round %u: region %d (%zu boxes, %zu wanted) pushes%s\n",
                   round + 1,
                   r,
                   region_boxes[size_t(r)].size(),
                   want,
                   where.c_str());
    }
    rep.windows_grown += grown;
    if (moved == 0 && grown == 0) {
      break;
    }
    // a receiver that cannot hold what it took (the room is an estimate): its extra boxes go to the nearest region
    // with a fitting tile, as before the first round
    if (const size_t bounced = moved && !d.opt.spill_shift ? balance_regions(d, g, boxes) : 0; bounced > 0) {
      std::fprintf(stderr,
                   "legroute sat: spill round %u: %zu boxes bounced to the nearest region with a fitting tile\n",
                   round + 1,
                   bounced);
    }
    ++rep.spill_rounds;
    ++spill_round;
    rep.spilled_boxes += moved;
  }
  for (int r = 0; r < g.regions; ++r) {  // a region the spill rounds left with more boxes than tiles: the usual error
    if (!last[size_t(chosen[r])][size_t(r)].unfit.empty()) {
      throw Legroute_unfit(last[size_t(chosen[r])][size_t(r)].unfit);
    }
  }
  // ---- the chosen answers: tiles, counters, connections
  rep.sat_unjoined_crossings += x.unjoined;
  rep.sat_hops_over_share    += x.over_share;
  rep.global_nets            += x.global_nets;
  rep.global_pieces          += x.pieces.size();
  rep.global_retained_parts  += x.retained.size() + x.retained_parts.size();
  rep.coarse_backbone_fixed   = false;
  rep.coarse_corridor_free    = movable_route_partition(d.opt);
  for (const auto& ends : x.ends) {
    rep.coarse_backbone_parts += ends.coarse_parts.size();
  }
  for (const auto& piece : x.pieces) {
    if (piece.backbone) {
      rep.coarse_backbone_edges += size_t(std::abs(g.col(piece.a) - g.col(piece.b)) + std::abs(g.row(piece.a) - g.row(piece.b)));
    }
  }
  rep.global_unrouted              += x.global_unrouted;
  rep.global_topology_disconnected += x.global_topology_disconnected;
  rep.global_full_edges            += x.global_full;
  rep.global_overflow_edges        += x.global_overflow_edges;
  rep.global_overflow_tracks       += x.global_overflow_tracks;
  rep.global_rounds                 = std::max(rep.global_rounds, x.global_rounds);
  {  // pre-placement counters: the first policy's
    const auto& q              = P[0].rep;
    rep.crit_pinned           += q.crit_pinned;
    rep.crit_unpinned_regions += q.crit_unpinned_regions;
    rep.crit_cost_before      += q.crit_cost_before;
    rep.crit_cost_after       += q.crit_cost_after;
    rep.tree_nets             += q.tree_nets;
    rep.tree_boxes            += q.tree_boxes;
    rep.tree_skipped_nets     += q.tree_skipped_nets;
    rep.tree_dropped_regions  += q.tree_dropped_regions;
    rep.tree_tiles_over_half  += q.tree_tiles_over_half;
    rep.tree_um_before        += q.tree_um_before;
    rep.tree_um_after         += q.tree_um_after;
    rep.tree_max_load          = std::max(rep.tree_max_load, q.tree_max_load);
    rep.forced_trees_dropped  += q.forced_trees_dropped;
    rep.forced_crit_unpinned  += q.forced_crit_unpinned;
    rep.tree_load_skipped     += q.tree_load_skipped;
    rep.tree_clauses_before   += q.tree_clauses_before;
    rep.tree_clauses_after    += q.tree_clauses_after;
    rep.io_feedthroughs       += q.io_feedthroughs;
    rep.io_boxes              += q.io_boxes;
    rep.io_skipped            += q.io_skipped;
  }

  // The accepted corridor model is an exact edge union. Its pinless degree-2
  // transit tiles are not physical attachments: keeping them as nodes would
  // create extra risers/jogs and make every unit segment exempt from layer load.
  std::set<std::pair<int, int>> protected_nodes;
  if (movable_route_partition(d.opt)) {
    std::vector<int> selected_tiles;
    selected_tiles.reserve(boxes.boxes.size());
    for (const auto& box : boxes.boxes) {
      selected_tiles.push_back(box.tile);
    }
    for (int r = 0; r < g.regions; ++r) {
      for (const auto& [b, t] : last[chosen[r]][r].tiles) {
        selected_tiles[size_t(b)] = t;
      }
    }
    for (size_t n = 0; n < d.nets.size(); ++n) {
      // Preserve the exact attachment sites used by crossing construction,
      // including physical fixed-pin access and coincident interfaces.
      for (const auto& [r, endpoints] : x.ends[n].per_region) {
        for (const auto& endpoint : endpoints) {
          protected_nodes.emplace(int(n), endpoint.box >= 0 ? selected_tiles[size_t(endpoint.box)] : endpoint.tile);
        }
      }
      // Also preserve every site consumed by the final pin bookkeeping below.
      for (const auto& pin : d.nets[n].pins) {
        const int box  = box_of_comp(pin.comp);
        int       tile = -1;
        if (box >= 0) {
          tile = selected_tiles[size_t(box)];
        } else if (pin.comp >= 0) {
          const auto& component = d.fp.components[pin.comp];
          const auto& master    = *d.macro[pin.comp];
          tile                  = g.tile_of(double(component.place.x) + master.width * d.fp.dbu / 2,
                                            double(component.place.y) + master.height * d.fp.dbu / 2);
        } else if (pin.term >= 0) {
          tile = g.tile_of(double(d.fp.terminals[pin.term].x), double(d.fp.terminals[pin.term].y));
        }
        if (tile >= 0) {
          protected_nodes.emplace(int(n), tile);
        }
      }
    }
    for (size_t k = 0; k < x.hops.size(); ++k) {
      protected_nodes.emplace(x.hop_net[k], x.hops[k].first);
      protected_nodes.emplace(x.hop_net[k], x.hops[k].second);
    }
  }
  R.trees.assign(d.nets.size(), {});
  R.local.assign(d.nets.size(), 0);
  std::vector<std::map<int, int>> node_at(d.nets.size());  // per net: tile -> node
  const auto                      node = [&](int n, int tile) {
    auto [it, fresh] = node_at[n].try_emplace(tile, int(R.trees[n].nodes.size()));
    if (fresh) {
      R.trees[n].nodes.push_back({});
      R.trees[n].nodes.back().tile = tile;
    }
    return it->second;
  };
  const auto add_conn = [&](int n, int ta, int tb, bool h_first, int region) {
    Conn c;
    c.net      = n;
    c.a        = node(n, ta);
    c.b        = node(n, tb);
    c.region   = region;
    c.weight   = d.nets[n].weight;
    c.critical = d.nets[n].critical;
    c.poly.emplace_back(g.col(ta), g.row(ta));
    const std::pair<int, int> corner = h_first ? std::pair{g.col(tb), g.row(ta)} : std::pair{g.col(ta), g.row(tb)};
    if (corner != c.poly.back() && corner != std::pair{g.col(tb), g.row(tb)}) {
      c.poly.push_back(corner);
    }
    if (std::pair{g.col(tb), g.row(tb)} != c.poly.back()) {
      c.poly.emplace_back(g.col(tb), g.row(tb));
    }
    if (c.poly.size() < 2) {
      return;
    }
    R.conns.push_back(std::move(c));
    R.trees[n].conns.push_back(int(R.conns.size()) - 1);
  };
  for (int r = 0; r < g.regions; ++r) {
    const auto& out = last[chosen[r]][r];
    if (out.status == Sat_status::satisfiable) {
      // The accepted union also contains the policy's retained trees/IO pieces. Recount that complete geometry
      // before extraction, including reused answers and alternatives; a checker-only waiver cannot escape here.
      auto accepted      = policies[size_t(chosen[r])];
      accepted.untracked = nullptr;
      for (const auto& piece : x.pieces) {
        if (!piece.refinable && piece.region == r && !contains_global_piece(out, piece)) {
          throw std::runtime_error(
              std::format("legroute coarse witness repair required: accepted region {} omitted net {} part {} backbone geometry",
                          r,
                          piece.net,
                          piece.part));
        }
      }
      validate_region_edges(g, r, out.conns, P[size_t(chosen[r])].reserve, cap_h0, cap_v0, accepted);
    }
    for (const auto& [b, t] : out.tiles) {
      boxes.boxes[b].tile  = t;
      boxes.boxes[b].fixed = P[chosen[r]].boxes.boxes[b].fixed;
    }
    const auto paths = movable_route_partition(d.opt) && out.status == Sat_status::satisfiable
                           ? coalesce_region_routes(g, r, out.conns, protected_nodes)
                           : out.conns;
    for (const auto& c : paths) {
      add_conn(c.net, c.ta, c.tb, c.h_first, r);
    }
    for (int n : out.local) {
      R.local[n] = 1;
    }
    const auto& q                     = out.rep;
    rep.corridor_components          += q.corridor_components;
    rep.corridor_candidate_edges     += q.corridor_candidate_edges;
    rep.corridor_connectivity_cuts   += q.corridor_connectivity_cuts;
    rep.corridor_connectivity_rounds += q.corridor_connectivity_rounds;
    rep.corridor_checked_components  += q.corridor_checked_components;
    rep.corridor_selected_edges      += q.corridor_selected_edges;
    rep.corridor_seed_attempts       += q.corridor_seed_attempts;
    rep.corridor_seed_successes      += q.corridor_seed_successes;
    rep.corridor_seed_solves         += q.corridor_seed_solves;
    rep.corridor_seed_variables      += q.corridor_seed_variables;
    rep.corridor_seed_clauses        += q.corridor_seed_clauses;
    rep.corridor_seed_seconds        += q.corridor_seed_seconds;
    rep.coarse_attachment_stubs      += q.coarse_attachment_stubs;
    rep.coarse_attachment_choices    += q.coarse_attachment_choices;
    rep.coarse_attachments_changed   += q.coarse_attachments_changed;
    rep.sat_regions                  += q.sat_regions;
    rep.sat_variables                += q.sat_variables;
    rep.sat_clauses                  += q.sat_clauses;
    rep.sat_solves                   += q.sat_solves;
    rep.sat_binding_tiles            += q.sat_binding_tiles;
    rep.sat_fixed_overflow           += q.sat_fixed_overflow;
    rep.sat_relaxed_windows          += q.sat_relaxed_windows;
    rep.sat_root_fallbacks           += q.sat_root_fallbacks;
    rep.sat_relaxed_tiles            += q.sat_relaxed_tiles;
    rep.sat_deficit_tracks           += q.sat_deficit_tracks;
    rep.sat_unsat_regions            += q.sat_unsat_regions;
    rep.sat_cut_regions              += q.sat_cut_regions;
    rep.sat_unknown_regions          += q.sat_unknown_regions;
    rep.sat_skipped_regions          += q.sat_skipped_regions;
    rep.sat_fail_fast_skipped        += q.sat_fail_fast_skipped;
    rep.lazy_rounds                  += q.lazy_rounds;
    rep.lazy_bounds                  += q.lazy_bounds;
    rep.lazy_skipped                 += q.lazy_skipped;
    rep.frozen_boxes                 += q.frozen_boxes;
    rep.sat_seeded                   += q.sat_seeded;
    rep.tier_kept                    += q.tier_kept;
    rep.tier_dropped                 += q.tier_dropped;
    rep.tier_timeouts                += q.tier_timeouts;
    rep.tier_solves                  += q.tier_solves;
    rep.tier_cost_before             += q.tier_cost_before;
    rep.tier_cost_after              += q.tier_cost_after;
    rep.tier_crit_before             += q.tier_crit_before;
    rep.tier_crit_after              += q.tier_crit_after;
    {  // per region: its answer, and the cells of a region without a routable answer
      Legroute_report::Region_stat st;
      st.status = out.skipped ? 3 : out.status == Sat_status::satisfiable ? 0 : out.status == Sat_status::unsatisfiable ? 1 : 2;
      for (const auto& b : boxes.boxes) {
        st.boxes += b.region == r;
      }
      for (size_t t = 0; t < g.size(); ++t) {
        st.tiles += g.region[t] == r && !g.blocked[t];
      }
      rep.region_stats.push_back(st);
      if (st.status == 1 || st.status == 2) {
        for (const auto& b : boxes.boxes) {
          if (b.region == r) {
            for (int c : b.cells) {
              rep.unroutable_components.push_back(size_t(d.movable[c]));
              rep.unroutable_regions.push_back(r);
            }
          }
        }
      }
    }
    rep.sat_adjacent_nets += q.sat_adjacent_nets;
    rep.io_lifted         += q.io_lifted;
    double wall            = 0;  // the policies ran side by side
    for (const auto& per : last) {
      wall = std::max(wall, per[r].seconds);
    }
    rep.sat_seconds     += wall;
    rep.sat_seconds_max  = std::max(rep.sat_seconds_max, wall);
    if (chosen[r] > 0) {
      ++rep.sat_alt_regions;
    }
    if (std::ranges::all_of(last, [&](const auto& per) { return per[r].status == Sat_status::satisfiable; })) {
      ++rep.sat_both_regions;
    }
  }
  // the hops between regions
  for (size_t k = 0; k < x.hops.size(); ++k) {
    add_conn(x.hop_net[k], x.hops[k].first, x.hops[k].second, true, g.region[x.hops[k].first]);
  }
  if (!d.opt.output_dir.empty()) {  // legroute_tiles.json: the model's per-tile tracks and in-box charges (calibration)
    std::ofstream out(d.opt.output_dir + "/legroute_tiles.json");
    out << R"({"tile_dbu":[)" << g.tile_w << ',' << g.tile_h << R"(],"origin_dbu":[)" << g.x0 << ',' << g.y0 << R"(],"cols":)"
        << g.cols << R"(,"rows":)" << g.rows
        << R"(,"fields":["tile","col","row","region","blocked","cap_h0","cap_v0","cap_h","cap_v","cap_h_region","cap_v_region","box","dem_h","dem_v"],"tiles":[)";
    std::vector<int> occupant(g.size(), -1);
    for (size_t b = 0; b < boxes.boxes.size(); ++b) {
      if (boxes.boxes[b].tile >= 0) {
        occupant[size_t(boxes.boxes[b].tile)] = int(b);
      }
    }
    for (size_t t = 0; t < g.size(); ++t) {
      const int   b  = occupant[t];
      const auto& Pc = P[size_t(chosen[size_t(g.region[t])])];
      out << (t ? "," : "") << '[' << t << ',' << g.col(int(t)) << ',' << g.row(int(t)) << ',' << g.region[t] << ','
          << int(g.blocked[t]) << ',' << cap_h0[t] << ',' << cap_v0[t] << ',' << cap_h[t] << ',' << cap_v[t] << ',' << Pc.cap_h[t]
          << ',' << Pc.cap_v[t] << ',' << b << ',' << (b >= 0 ? dem_h[size_t(b)] : 0) << ',' << (b >= 0 ? dem_v[size_t(b)] : 0)
          << ']';
    }
    out << "]}\n";
  }
  // box and pin bookkeeping on the nodes
  for (size_t n = 0; n < d.nets.size(); ++n) {
    const auto& N = d.nets[n];
    if (R.trees[n].nodes.empty()) {
      continue;
    }
    for (size_t k = 0; k < N.pins.size(); ++k) {
      const auto& p   = N.pins[k];
      const int   box = box_of_comp(p.comp);
      int         t;
      if (box >= 0) {
        t = boxes.boxes[box].tile;
      } else if (p.comp >= 0) {
        const auto& c = d.fp.components[p.comp];
        const auto& m = *d.macro[p.comp];
        t             = g.tile_of(double(c.place.x) + m.width * d.fp.dbu / 2, double(c.place.y) + m.height * d.fp.dbu / 2);
      } else if (p.term >= 0) {
        t = g.tile_of(double(d.fp.terminals[p.term].x), double(d.fp.terminals[p.term].y));
      } else {
        continue;
      }
      const int nd = node(int(n), t);
      auto&     ND = R.trees[n].nodes[nd];
      ND.pins.push_back(int(k));
      if (box >= 0) {
        ND.box = box;
      } else if (p.term >= 0) {
        ND.term = p.term;
      }
    }
  }
  for (const auto& c : R.conns) {
    rep.critical_connections += c.critical;
  }
}
}  // namespace livehd::placement::lr
