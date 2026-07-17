// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_absorb.hpp"

#include <vector>

#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"

namespace livehd::color {

namespace {

namespace gu = livehd::graph_util;

using Gid2Graph = absl::flat_hash_map<hhds::Gid, hhds::Graph*>;

// A def's weight if it were inlined: its own gates, plus -- for each instance it
// holds -- the whole weight of what that instance points at. This is what decides
// absorption, so it must be the POST-inline number: absorbing a 900-GE def that
// itself instantiates 900 GE of children would otherwise smuggle 1800 GE past a
// 1000-GE floor.
//
// A def already on the current path is a recursive hierarchy: fall back to the
// Sub's own port weight rather than recursing forever. Such a def can never be
// under a sane min anyway.
class Def_weigher {
public:
  explicit Def_weigher(const Gid2Graph& g2g) : g2g_(g2g) {}

  uint64_t total(hhds::Graph* g) {
    if (g == nullptr) {
      return 0;
    }
    const auto gid = g->get_gid();
    if (auto it = memo_.find(gid); it != memo_.end()) {
      return it->second;
    }
    if (!on_path_.insert(gid).second) {
      return 0;  // recursive: the Sub's port weight is charged by the caller
    }
    uint64_t sum = 0;
    for (auto n : g->fast_class()) {
      if (gu::type_op_of(n) == Ntype_op::Sub) {
        auto it = g2g_.find(n.get_subnode_gid());
        if (it != g2g_.end() && it->second != nullptr) {
          sum += total(it->second);
          continue;
        }
      }
      sum += gu::ge_weight(n);  // a black box is worth its boundary, and nothing more
    }
    on_path_.erase(gid);
    memo_.emplace(gid, sum);
    return sum;
  }

private:
  const Gid2Graph&                     g2g_;
  absl::flat_hash_map<hhds::Gid, uint64_t> memo_;
  absl::flat_hash_set<hhds::Gid>           on_path_;
};

// Reachable defs in CHILDREN-FIRST order. A def appears only after every def it
// instantiates, so inlining it into its parents is a flat clone of a body that is
// already fully absorbed.
void post_order(hhds::Graph* g, const Gid2Graph& g2g, absl::flat_hash_set<hhds::Gid>& seen,
                std::vector<hhds::Graph*>& order) {
  if (g == nullptr || !seen.insert(g->get_gid()).second) {
    return;
  }
  for (auto n : g->fast_class()) {
    if (gu::type_op_of(n) != Ntype_op::Sub) {
      continue;
    }
    if (auto it = g2g.find(n.get_subnode_gid()); it != g2g.end()) {
      post_order(it->second, g2g, seen, order);
    }
  }
  order.emplace_back(g);
}

}  // namespace

bool absorb_small_defs(hhds::Graph* top, const Gid2Graph& gid2graph, uint64_t min_ge, Absorb_stats* st) {
  Absorb_stats  local;
  Absorb_stats& s = st == nullptr ? local : *st;
  if (top == nullptr || min_ge == 0) {
    return true;
  }

  absl::flat_hash_set<hhds::Gid> seen;
  std::vector<hhds::Graph*>      order;
  post_order(top, gid2graph, seen, order);

  Def_weigher weigher(gid2graph);

  // Decide absorption for every def UP FRONT, from the pre-inline hierarchy. The
  // decision must not drift as bodies grow: a def is absorbed at all of its sites
  // or at none, which is what makes it safe to leave the def in the library.
  absl::flat_hash_set<hhds::Gid> absorb;
  absl::flat_hash_map<hhds::Gid, uint64_t> weight;
  for (auto* g : order) {
    if (g == top) {
      continue;  // the top has no parent to fold into
    }
    const uint64_t w = weigher.total(g);
    weight[g->get_gid()] = w;
    if (w < min_ge) {
      absorb.insert(g->get_gid());
    }
  }
  s.defs_absorbed = absorb.size();
  if (absorb.empty()) {
    return true;
  }

  // Children-first: by the time `g` is inlined into anyone, its own small
  // children are already part of its body.
  absl::flat_hash_map<hhds::Gid, uint64_t> sites;
  for (auto* g : order) {
    // Snapshot the instances first: inline_sub_instance deletes nodes, and
    // fast_class() is a live view over the node table.
    std::vector<hhds::Node_class> victims;
    for (auto n : g->fast_class()) {
      if (gu::type_op_of(n) == Ntype_op::Sub && absorb.contains(n.get_subnode_gid())) {
        victims.emplace_back(n);
      }
    }
    for (const auto& v : victims) {
      const auto gid = v.get_subnode_gid();
      if (!gu::inline_sub_instance(g, v, "pass.color")) {
        return false;
      }
      ++s.sites_inlined;
      ++sites[gid];
    }
  }

  // What absorbing COST: a def inlined at N sites is N copies of logic that ABC
  // used to map once. N-1 of them are new.
  for (const auto& [gid, n] : sites) {
    if (n > 1) {
      s.ge_duplicated += (n - 1) * weight[gid];
    }
  }
  return true;
}

}  // namespace livehd::color
