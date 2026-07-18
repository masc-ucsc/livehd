// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_size.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "node_util.hpp"

namespace livehd::color {

namespace {

using livehd::graph_util::bits_of;
// The window weighs what ABC will BLAST, not what the region touches: a Sub
// counts ~1 (node_util mappable_ge_weight). With Sub port bits in the weight,
// a 2-node glue+instance region "weighs" thousands of GE, dodges the min floor
// forever, and XSCore ends up with tens of thousands of zero-logic regions the
// mapper pays call overhead for.
using livehd::graph_util::mappable_ge_weight;

constexpr int NO_REGION = -1;

// A region whose adjacency is larger than this never ACTS as a merge initiator
// (it still receives). Acting means one best_partner scan over the whole
// adjacency; a hub -- a reset/flush/clock cone adjacent to every flop region,
// 100k+ neighbours on an XSCore def -- stays under any sane floor for
// thousands of merges, and Best-Choice scoring makes it prefer 1-GE partners,
// so letting it act is a quadratic scan loop (measured: minutes per def).
// Its tiny neighbours each pick the hub as THEIR best partner with an
// O(own-degree) scan, so the hub still fills past the floor purely by
// receiving. Degree is a function of the input graph: deterministic.
constexpr size_t kActor_degree_cap = 4096;

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

  // Fold one region into the other; the LARGER-degree side survives (tie:
  // smaller id) so the fold iterates the smaller neighbour map -- see the
  // definition for why. Returns the surviving region id.
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
    weight_[r]      += mappable_ge_weight(n);
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
  // Survivor is the LARGER-degree side (tie: smaller id): folding iterates the
  // dissolved side's neighbour map, so the merge costs the SMALLER degree. The
  // old smaller-id rule iterated a hub's whole adjacency (a reset/clock cone
  // touches tens of thousands of regions) on every second merge into it --
  // quadratic the moment the window makes hub-adjacent singletons mergeable.
  // Still deterministic (degree and id are functions of the input graph), and
  // the caller-visible ids only feed the final forward_class renumber anyway.
  if (adj_[b].size() > adj_[a].size() || (adj_[b].size() == adj_[a].size() && b < a)) {
    std::swap(a, b);  // a survives, b dissolves
  }
  weight_[a] += weight_[b];
  // Append the SHORTER member list (the vectors are freely swappable: nothing
  // after a merge relies on members order beyond determinism, and split_large's
  // MFFC only ever sees pre-merge regions).
  if (members_[b].size() > members_[a].size()) {
    std::swap(members_[a], members_[b]);
  }
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
// Worklist, FIFO by region id: pop a region, merge it once into its
// best-scoring neighbour, re-enqueue the survivor. Dropping a region with no
// fitting partner is FINAL -- weights only grow, so a union that breaches max
// now breaches it forever, and a region gains new neighbours only by merging
// itself -- which makes the fixpoint identical to a priority ordering: no
// under-min region with a legal partner survives. Cost is one best_partner
// scan per pop and at most ~2x the region count of pops.
//
// This replaced a Best-Choice score heap with lazy invalidation: sound, but a
// hub region (a reset/flush cone adjacent to every flop region -- 100k+
// neighbours on an XSCore def) stayed under min for thousands of merges, and
// every stale-entry pop re-ranked it with a full O(degree) best_partner scan.
// The heap's global score order bought a marginally better merge TREE for
// minutes of rescans; per-actor Best-Choice keeps the scoring where it counts.
void merge_small(Region_graph& rg, uint64_t min_ge, uint64_t max_ge, Size_window_stats& st) {
  if (min_ge == 0) {
    return;
  }
  std::deque<int> work;
  for (int r = 0; r < static_cast<int>(rg.size()); ++r) {
    if (rg.alive(r) && rg.weight(r) < min_ge) {
      work.push_back(r);
    }
  }
  while (!work.empty()) {
    const int r = rg.find(work.front());
    work.pop_front();
    if (!rg.alive(r) || rg.weight(r) >= min_ge) {
      continue;
    }
    if (rg.neighbours(r).size() > kActor_degree_cap) {
      continue;  // hub: receives only (see kActor_degree_cap)
    }
    const int nb = best_partner(rg, r, max_ge);
    if (nb == NO_REGION) {
      continue;  // final: no future merge can make a partner fit (see above)
    }
    work.push_back(rg.merge(r, nb));
    ++st.merges;
  }
}

// Pack regions up to `cap` in ONE actor pass, ascending id: each still-alive
// region acts once, folding itself into its best-scoring neighbour whose union
// fits. Bins only ever RECEIVE -- an absorbed id resolves away (find(r) != r)
// and a receiving bin is never re-scanned -- so total cost is one small
// best_partner scan per original region, O(edges). The price is a weaker
// fixpoint than merge_small's: two under-cap bins can remain adjacent without
// merging. That is fine HERE: the split re-pack only needs pieces comfortably
// under the cap and above the floor, and the outer merge_small that follows
// apply_size_window's split step folds any piece that ended under min.
//
// merge_small itself must NOT be used with min == cap: every bin then stays
// "small" until the cap, and its worklist re-enqueues the bin after each
// receive -- re-scanning an adjacency that grows with every absorbed cluster
// (measured: a 200k-cluster monster region spent minutes there).
void agglomerate_to_cap(Region_graph& rg, uint64_t cap, Size_window_stats& st) {
  // `done` is monotonic: a region with no fitting partner NOW never gains one
  // (weights only grow; a region gains neighbours only by merging itself), so
  // each round only revisits regions that merged last round. Rounds shrink the
  // bin count geometrically -- a SINGLE pass instead left the split monsters as
  // ~half-million mid-size bins (measured on XSCore) because adjacent under-cap
  // bins never re-acted.
  std::vector<char> done(rg.size(), 0);
  bool              merged = true;
  while (merged) {
    merged = false;
    for (int r = 0; r < static_cast<int>(rg.size()); ++r) {
      if (done[r] != 0 || !rg.alive(r) || rg.find(r) != r || rg.weight(r) >= cap) {
        continue;
      }
      if (rg.neighbours(r).size() > kActor_degree_cap) {
        continue;  // hub: receives only (see kActor_degree_cap)
      }
      const int nb = best_partner(rg, r, cap);
      if (nb == NO_REGION) {
        done[r] = 1;  // final by monotonicity
        continue;
      }
      rg.merge(r, nb);
      ++st.merges;
      merged = true;
    }
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
    const uint64_t w = mappable_ge_weight(n);
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
        w += mappable_ge_weight(n);
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
    //    agglomerate_to_cap is "merge anything below the cap into its
    //    best-connected neighbour, so long as the union still fits" -- one
    //    actor pass; leftovers under min are folded by the outer merge_small.
    Region_graph      rg_sub(g, sub);
    Size_window_stats sub_st;
    agglomerate_to_cap(rg_sub, max_ge, sub_st);

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

  // Leftover packing. After the merge fixpoint, every region still under min is
  // an ENTIRE connected component below min -- def IO and constants are
  // adjacency holes, so a PI->flop->PO register cloud or a wrapper's glue snippet
  // has no neighbour it may legally merge with, and XSCore leaves 14k such
  // regions (median 2 nodes) for ABC to pay per-call overhead on. Fold them per
  // def into 'misc' bins of ~min_ge each, ascending region id for determinism.
  // Disjoint clouds inside one region are sound everywhere downstream (cgen /
  // ABC / LEC see disjoint cones behind one port list); pass.partition honors
  // the packing through the coloring_info "packed" flag (anchor union), else its
  // component split would silently undo this.
  if (min_ge != 0) {
    int bin = NO_REGION;
    for (int r = 0; r < static_cast<int>(rg2.size()); ++r) {
      if (!rg2.alive(r) || rg2.weight(r) >= min_ge) {
        continue;
      }
      if (bin == NO_REGION || bin == r) {
        bin = r;
        continue;
      }
      // Never overfill past max: each leftover is < min and a bin closes at
      // >= min, so with a sane window (min <= max/2) this never trips; the guard
      // is for direct callers handing a degenerate window.
      if (max_ge != 0 && rg2.weight(bin) + rg2.weight(r) > max_ge) {
        bin = r;
        continue;
      }
      bin = rg2.merge(bin, r);
      ++s.packed;
      if (rg2.weight(bin) >= min_ge) {
        bin = NO_REGION;  // bin full: the next leftover opens a fresh one
      }
    }
  }

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
