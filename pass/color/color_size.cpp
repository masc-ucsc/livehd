// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_size.hpp"

#include <algorithm>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "node_util.hpp"

namespace livehd::color {

namespace {

using livehd::graph_util::bits_of;
using livehd::graph_util::ge_weight;

constexpr int NO_REGION = -1;

// The region graph: one vertex per region, one weighted edge per adjacent pair.
//
// Region ids are DENSE (0..n-1) and minted in forward_class() order, so every
// loop below is deterministic without sorting a hash map. Merging is a union-find
// over region ids -- never a rescan of the node map, which is what makes a merge
// O(neighbours) instead of O(nodes) (the shape that makes color_acyclic's merge
// quadratic).
class Region_graph {
public:
  Region_graph(hhds::Graph* g, const Node2Id& node2id);

  [[nodiscard]] size_t   size() const { return weight_.size(); }
  [[nodiscard]] bool     alive(int r) const { return alive_[r]; }
  [[nodiscard]] uint64_t weight(int r) const { return weight_[r]; }

  [[nodiscard]] int find(int r) {
    while (rep_[r] != r) {
      rep_[r] = rep_[rep_[r]];  // path halving; iterative by mandate (color_common.hpp)
      r       = rep_[r];
    }
    return r;
  }

  [[nodiscard]] const absl::flat_hash_map<int, uint64_t>& neighbours(int r) const { return adj_[r]; }
  [[nodiscard]] const std::vector<hhds::Node_class>&      members(int r) const { return members_[r]; }

  // Fold `b` into `a`, keeping the SMALLER id as the survivor so the result is a
  // function of the id allocation order rather than of the merge order.
  int merge(int a, int b);

  // Region of `n`, resolved through the union-find.
  [[nodiscard]] int region_of(const hhds::Node_class& n) {
    auto it = node2region_.find(n);
    return it == node2region_.end() ? NO_REGION : find(it->second);
  }

private:
  std::vector<uint64_t>                            weight_;
  std::vector<bool>                                alive_;
  std::vector<int>                                 rep_;
  std::vector<absl::flat_hash_map<int, uint64_t>>  adj_;      // region -> neighbour -> crossing bits
  std::vector<std::vector<hhds::Node_class>>       members_;  // forward_class order
  absl::flat_hash_map<hhds::Node_class, int>       node2region_;
};

Region_graph::Region_graph(hhds::Graph* g, const Node2Id& node2id) {
  // 1. Components: two same-id nodes joined by a direct edge are one region. This
  //    is split_continuous's rule -- a color that is two disjoint clouds is two
  //    regions to pass.partition, so it must be two vertices here too.
  Union_find uf;
  for (auto n : g->forward_class()) {
    auto it = node2id.find(n);
    if (it == node2id.end()) {
      continue;
    }
    uf.find(n);  // present even when isolated
    for (const auto& e : n.out_edges()) {
      auto snode = e.sink.get_master_node();
      auto sit   = node2id.find(snode);
      if (sit != node2id.end() && sit->second == it->second) {
        uf.merge(n, snode);
      }
    }
  }

  // 2. Mint dense ids in forward_class() first-encounter order.
  absl::flat_hash_map<hhds::Node_class, int> root2region;
  for (auto n : g->forward_class()) {
    if (!node2id.contains(n)) {
      continue;
    }
    auto root = uf.find(n);
    auto it   = root2region.find(root);
    if (it == root2region.end()) {
      it = root2region.emplace(root, static_cast<int>(weight_.size())).first;
      weight_.emplace_back(0);
      alive_.emplace_back(true);
      rep_.emplace_back(static_cast<int>(rep_.size()));
      adj_.emplace_back();
      members_.emplace_back();
    }
    const int r      = it->second;
    node2region_[n]  = r;
    weight_[r]      += ge_weight(n);
    members_[r].emplace_back(n);
  }

  // 3. Edges: weight = total driver bits crossing the boundary. Bits, not edge
  //    count -- a 64-bit bus binds two regions far more tightly than a 1-bit
  //    enable, and cutting it costs 64 ports.
  for (auto n : g->forward_class()) {
    auto it = node2region_.find(n);
    if (it == node2region_.end()) {
      continue;
    }
    const int r = it->second;
    for (const auto& e : n.out_edges()) {
      auto sit = node2region_.find(e.sink.get_master_node());
      if (sit == node2region_.end() || sit->second == r) {
        continue;
      }
      const auto bits = static_cast<uint64_t>(std::max(bits_of(e.driver), 1));
      adj_[r][sit->second] += bits;
      adj_[sit->second][r] += bits;
    }
  }
}

int Region_graph::merge(int a, int b) {
  a = find(a);
  b = find(b);
  if (a == b) {
    return a;
  }
  if (b < a) {
    std::swap(a, b);  // survivor is the smaller id
  }
  weight_[a] += weight_[b];
  members_[a].insert(members_[a].end(), members_[b].begin(), members_[b].end());
  members_[b].clear();
  members_[b].shrink_to_fit();

  adj_[a].erase(b);
  for (const auto& [nb, w] : adj_[b]) {
    if (nb == a) {
      continue;
    }
    adj_[a][nb] += w;
    adj_[nb].erase(b);
    adj_[nb][a] += w;
  }
  adj_[b].clear();
  alive_[b] = false;
  rep_[b]   = a;
  return a;
}

// Best-Choice score (ISPD'05): connectivity normalized by the size of what it
// would produce, so a merge prefers a tightly bound SMALL neighbour over a
// loosely bound big one. Compared as a cross-multiplication rather than a double
// division: exact, and it cannot make the pick depend on rounding.
// cut_a/(w_a) > cut_b/(w_b)  <=>  cut_a*w_b > cut_b*w_a
[[nodiscard]] bool better_score(uint64_t cut_a, uint64_t w_a, uint64_t cut_b, uint64_t w_b) {
  return static_cast<unsigned __int128>(cut_a) * w_b > static_cast<unsigned __int128>(cut_b) * w_a;
}

// The best region to fold `r` into: maximize connectivity/(combined size) among
// neighbours whose union with `r` still fits under `cap` (0 = uncapped).
// Deterministic tie-break on the smaller region id.
[[nodiscard]] int best_partner(Region_graph& rg, int r, uint64_t cap) {
  int      best     = NO_REGION;
  uint64_t best_cut = 0;
  uint64_t best_w   = 0;
  for (const auto& [nb_raw, cut] : rg.neighbours(r)) {
    const int nb = rg.find(nb_raw);
    if (nb == r || !rg.alive(nb)) {
      continue;
    }
    const uint64_t w = rg.weight(r) + rg.weight(nb);
    if (cap != 0 && w > cap) {
      continue;
    }
    if (best == NO_REGION || better_score(cut, w, best_cut, best_w) || (!better_score(best_cut, best_w, cut, w) && nb < best)) {
      best     = nb;
      best_cut = cut;
      best_w   = w;
    }
  }
  return best;
}

// Merge every region under `min_ge` into its best-scoring neighbour, never
// letting the union pass `max_ge`.
//
// Best-Choice with lazy invalidation: a heap keyed by each small region's best
// score, popped until a live, still-small, still-current entry surfaces. A stale
// entry (its region merged, or its weight changed) is recomputed and re-pushed
// rather than scanned for, which is what keeps this near-linear instead of the
// pivot-restart O(n^3) loop next door in color_acyclic.
void merge_small(Region_graph& rg, uint64_t min_ge, uint64_t max_ge, Size_window_stats& st) {
  if (min_ge == 0) {
    return;
  }
  // (score, region, weight-at-push). The weight stamp is the staleness check:
  // any merge that touched this region changed its weight.
  struct Entry {
    double   score;
    int      region;
    uint64_t stamp;
    // Greater score first; ties by smaller id so the heap order is total and
    // reproducible.
    bool operator<(const Entry& o) const {
      if (score != o.score) {
        return score < o.score;
      }
      return region > o.region;
    }
  };
  std::priority_queue<Entry> pq;

  auto score_of = [&](int r, int nb) -> double {
    const auto it = rg.neighbours(r).find(nb);
    if (it == rg.neighbours(r).end()) {
      return 0.0;
    }
    const uint64_t w = rg.weight(r) + rg.weight(nb);
    return w == 0 ? 0.0 : static_cast<double>(it->second) / static_cast<double>(w);
  };
  auto push_if_small = [&](int r) {
    if (!rg.alive(r) || rg.weight(r) >= min_ge) {
      return;
    }
    const int nb = best_partner(rg, r, max_ge);
    if (nb == NO_REGION) {
      return;  // nothing it may legally merge with -- resolved at the tally below
    }
    pq.push(Entry{score_of(r, nb), r, rg.weight(r)});
  };

  for (int r = 0; r < static_cast<int>(rg.size()); ++r) {
    push_if_small(r);
  }

  while (!pq.empty()) {
    const auto e = pq.top();
    pq.pop();
    if (!rg.alive(e.region) || rg.find(e.region) != e.region) {
      continue;  // merged away
    }
    if (rg.weight(e.region) >= min_ge) {
      continue;  // grew past min while it sat in the heap
    }
    if (rg.weight(e.region) != e.stamp) {
      push_if_small(e.region);  // stale score: re-rank rather than act on it
      continue;
    }
    const int nb = best_partner(rg, e.region, max_ge);
    if (nb == NO_REGION) {
      continue;
    }
    const int keep = rg.merge(e.region, nb);
    ++st.merges;
    push_if_small(keep);  // still small? it gets another turn
  }
}

// ---------------------------------------------------------------------------
// Split-large
// ---------------------------------------------------------------------------

// Fan-out degree, capped at 2. NEVER out_edges().size(): that view is lazy and
// size() re-walks it (hhds graph.hpp). Only the "exactly one reader" distinction
// matters here, which is the same cap color_acyclic and pass_submatch use.
[[nodiscard]] size_t fanout_upto2(const hhds::Node_class& n) {
  size_t k = 0;
  for (const auto& e : n.out_edges()) {
    (void)e;
    if (++k >= 2) {
      break;
    }
  }
  return k;
}

// MFFC micro-clusters over one region's members (Cong DAC'94).
//
// A maximal fanout-free cone is the largest set of nodes whose every path out
// reaches the rest of the design through ONE root. Cones are disjoint, need no
// duplication, and -- the reason to use them here -- a cut never runs through the
// middle of one, so each piece is still a shape ABC maps well.
//
// Roots are every member that is not a single-fanout node feeding a member: those
// are exactly the reconvergence and boundary points. Growth then claims each
// root's fanout-free backward cone. Because every fanout>1 node is already a
// root, "claim any unclaimed non-root driver" IS the textbook dominance test --
// the same identity `color_acyclic` relies on to be an MFFC partitioner.
//
// Returns one cluster id per member, dense from 0.
[[nodiscard]] absl::flat_hash_map<hhds::Node_class, int> mffc_clusters(Region_graph& rg, int r) {
  const auto&                                in_region = rg.members(r);
  absl::flat_hash_set<hhds::Node_class>      member(in_region.begin(), in_region.end());
  absl::flat_hash_map<hhds::Node_class, int> cluster;

  auto is_root = [&](const hhds::Node_class& n) {
    if (fanout_upto2(n) != 1) {
      return true;  // fans out (reconvergence) or drives nothing (a region output)
    }
    for (const auto& e : n.out_edges()) {
      return !member.contains(e.sink.get_master_node());  // its one reader left the region
    }
    return true;
  };

  // Roots first, in members order (which is forward_class order): the cluster ids
  // are then deterministic.
  std::vector<hhds::Node_class> roots;
  for (const auto& n : in_region) {
    if (is_root(n)) {
      cluster[n] = static_cast<int>(roots.size());
      roots.emplace_back(n);
    }
  }

  // Grow each cone backward. Explicit worklist: a cone is as deep as the design
  // is long, so recursion here is a stack overflow (color_common.hpp).
  std::vector<hhds::Node_class> work;
  for (size_t i = 0; i < roots.size(); ++i) {
    work.assign(1, roots[i]);
    while (!work.empty()) {
      auto n = work.back();
      work.pop_back();
      for (const auto& e : n.inp_edges()) {
        auto d = e.driver.get_master_node();
        if (!member.contains(d) || cluster.contains(d)) {
          continue;  // outside the region, already a root, or already claimed
        }
        cluster[d] = static_cast<int>(i);
        work.emplace_back(d);
      }
    }
  }

  // Totality sweep. A ring of single-fanout nodes inside one region has no root
  // and would otherwise be claimed by nobody; give each leftover its own cluster
  // so every member is placed exactly once.
  int next = static_cast<int>(roots.size());
  for (const auto& n : in_region) {
    if (!cluster.contains(n)) {
      cluster[n] = next++;
    }
  }
  return cluster;
}

// Chop one over-max cluster into topological chunks of at most `max_ge`.
// The guaranteed-terminating fallback (Kernighan'71-style): whatever the cone
// structure, filling chunks in topological order always makes progress. The one
// thing it cannot fix is a single node heavier than max.
void topo_chunk(const std::vector<hhds::Node_class>& nodes, uint64_t max_ge, int& next_id,
                absl::flat_hash_map<hhds::Node_class, int>& out) {
  uint64_t acc = 0;
  int      id  = next_id++;
  for (const auto& n : nodes) {  // members are already in forward_class order
    const uint64_t w = ge_weight(n);
    if (acc != 0 && acc + w > max_ge) {
      id  = next_id++;
      acc = 0;
    }
    out[n] = id;
    acc += w;
  }
}

// Split every region over `max_ge`. Returns the new per-node ids (fresh space).
void split_large(hhds::Graph* g, Region_graph& rg, uint64_t max_ge, absl::flat_hash_map<hhds::Node_class, int>& out,
                 int& next_id, Size_window_stats& st) {
  if (max_ge == 0) {
    return;
  }
  for (int r = 0; r < static_cast<int>(rg.size()); ++r) {
    if (!rg.alive(r) || rg.weight(r) <= max_ge) {
      continue;
    }
    ++st.splits;

    // 1. MFFC micro-clusters over this region's nodes.
    const auto cl  = mffc_clusters(rg, r);
    int        ncl = 0;
    for (const auto& [n, c] : cl) {
      (void)n;
      ncl = std::max(ncl, c + 1);
    }
    std::vector<std::vector<hhds::Node_class>> bucket(static_cast<size_t>(ncl));
    for (const auto& n : rg.members(r)) {
      bucket[static_cast<size_t>(cl.at(n))].emplace_back(n);
    }

    // 2. A cluster that is ITSELF over max is chopped topologically -- an MFFC is
    //    not size-bounded, so this is the case that fallback exists for.
    //    Everything else keeps its cone intact.
    Node2Id sub;
    int     next_cluster = 1;
    for (auto& b : bucket) {
      if (b.empty()) {
        continue;
      }
      uint64_t w = 0;
      for (const auto& n : b) {
        w += ge_weight(n);
      }
      if (w <= max_ge) {
        const int id = next_cluster++;
        for (const auto& n : b) {
          sub[n] = id;
        }
        continue;
      }
      absl::flat_hash_map<hhds::Node_class, int> chopped;
      int                                        chop_next = 0;
      topo_chunk(b, max_ge, chop_next, chopped);
      const int base = next_cluster;
      next_cluster += chop_next;
      for (const auto& n : b) {
        sub[n] = base + chopped.at(n);
      }
    }

    // 3. Re-agglomerate the micro-clusters up to max, THROUGH THE REGION GRAPH.
    //
    //    This must go through adjacency, not a greedy fill in topological order.
    //    A region IS a connected component of equal-id nodes -- that is what
    //    pass.partition emits -- so packing two clusters that do not touch into
    //    one id does not make one region, it makes two that happen to share a
    //    label, and the next component split takes them straight back apart.
    //    (Measured: a topological fill left XSCore at 1.37M regions of ~5k GE,
    //    one per MFFC cone, instead of filling to the cap.)
    //
    //    Reusing merge_small with min == max == the cap IS the agglomeration:
    //    "merge anything below the cap into its best-connected neighbour, so long
    //    as the union still fits". Same Best-Choice scoring, same determinism.
    Region_graph      rg_sub(g, sub);
    Size_window_stats sub_st;
    merge_small(rg_sub, max_ge, max_ge, sub_st);

    absl::flat_hash_map<int, int> piece2id;
    for (const auto& n : rg.members(r)) {
      const int rr = rg_sub.region_of(n);
      if (rr == NO_REGION) {
        continue;
      }
      auto it = piece2id.find(rr);
      if (it == piece2id.end()) {
        it = piece2id.emplace(rr, next_id++).first;
      }
      out[n] = it->second;
    }
  }
}

}  // namespace

Node2Id apply_size_window(hhds::Graph* g, const Node2Id& node2id, uint64_t min_ge, uint64_t max_ge, Size_window_stats* st) {
  Size_window_stats local;
  Size_window_stats& s = st == nullptr ? local : *st;

  Region_graph rg(g, node2id);
  s.regions_in = rg.size();

  // Split BEFORE merge. Splitting an over-max region manufactures fresh small
  // pieces, so running merge-small afterwards is what leaves both halves of the
  // window satisfied at once; the other order would have to split, then re-merge,
  // then re-split.
  absl::flat_hash_map<hhds::Node_class, int> split_id;
  int                                        next_split = 0;
  split_large(g, rg, max_ge, split_id, next_split, s);

  // When nothing was over max -- the common case, and every def on a design with
  // no monster -- the graph already in hand IS the post-split one. Rebuilding it
  // would cost two more whole-def walks per def for nothing; on XSCore that is
  // 3,200 walks over 17.6M nodes.
  std::optional<Region_graph> rebuilt;
  if (s.splits != 0) {
    // Re-form the region graph over the post-split coloring so merge-small scores
    // against the regions that actually exist now. Ids are re-minted, which is
    // fine: the final renumber below is what fixes the labels.
    Node2Id after_split;
    after_split.reserve(node2id.size());
    // Split pieces get ids above every surviving region id, so the two spaces
    // cannot collide.
    const int base = static_cast<int>(rg.size()) + 1;
    for (const auto& [n, id] : node2id) {
      (void)id;
      auto it = split_id.find(n);
      if (it != split_id.end()) {
        after_split[n] = base + it->second;
      } else {
        const int r = rg.region_of(n);
        if (r != NO_REGION) {
          after_split[n] = r + 1;  // ids are only labels here; renumbered below
        }
      }
    }
    rebuilt.emplace(g, after_split);
  }
  Region_graph& rg2 = rebuilt.has_value() ? *rebuilt : rg;

  merge_small(rg2, min_ge, max_ge, s);

  // Final renumber: 1..k in forward_class first-encounter order. This is the
  // whole reason the engine owns the component split rather than leaving it to
  // apply_coloring's `continuous` -- deterministic ids the caller can pin.
  Node2Id                       out;
  absl::flat_hash_map<int, int> region2color;
  out.reserve(node2id.size());
  for (auto n : g->forward_class()) {
    const int r = rg2.region_of(n);
    if (r == NO_REGION) {
      continue;
    }
    auto it = region2color.find(r);
    if (it == region2color.end()) {
      it = region2color.emplace(r, static_cast<int>(region2color.size()) + 1).first;
    }
    out[n] = it->second;
  }

  s.regions_out = region2color.size();
  for (int r = 0; r < static_cast<int>(rg2.size()); ++r) {
    if (!rg2.alive(r)) {
      continue;
    }
    if (min_ge != 0 && rg2.weight(r) < min_ge) {
      ++s.left_under;
    }
    if (max_ge != 0 && rg2.weight(r) > max_ge) {
      ++s.left_over;
    }
  }
  return out;
}

}  // namespace livehd::color
