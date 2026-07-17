// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_stats.hpp"

#include <algorithm>
#include <format>
#include <numeric>
#include <print>
#include <tuple>

namespace livehd::color {

void Color_stats::add(std::string_view def, const Def_color_sizes& sizes, uint64_t instances) {
  uint64_t def_ge = 0;
  for (const auto& [color, ge] : sizes.color_ge) {
    (void)color;
    def_ge += ge;
  }

  Def_row row;
  row.def       = std::string{def};
  row.nodes     = sizes.partitionable;
  row.uncolored = sizes.uncolored;
  row.colors    = sizes.color_nodes.size();
  row.instances = instances;
  row.ge        = def_ge;
  defs_.emplace_back(std::move(row));

  total_nodes_ += sizes.partitionable;
  total_uncolored_ += sizes.uncolored;
  flat_nodes_ += sizes.partitionable * instances;
  total_ge_ += def_ge;

  for (const auto& [color, nodes] : sizes.color_nodes) {
    auto it = sizes.color_ge.find(color);
    partitions_.emplace_back(Partition{std::string{def}, color, nodes, it == sizes.color_ge.end() ? 0 : it->second});
  }
}

std::vector<uint64_t> Color_stats::sizes_desc(bool by_ge) const {
  auto sorted = partitions_;
  // Sort by (size desc, def, color): color ids are minted from hash-map
  // iteration in the continuous split, so an id-ordered report would not be
  // reproducible run to run.
  std::sort(sorted.begin(), sorted.end(), [by_ge](const Partition& a, const Partition& b) {
    const uint64_t as = by_ge ? a.ge : a.nodes;
    const uint64_t bs = by_ge ? b.ge : b.nodes;
    return std::tie(bs, a.def, a.color) < std::tie(as, b.def, b.color);
  });
  std::vector<uint64_t> out;
  out.reserve(sorted.size());
  for (const auto& p : sorted) {
    out.emplace_back(by_ge ? p.ge : p.nodes);
  }
  return out;
}

// Log2-bucketed size histogram, decade-ish buckets chosen to straddle the two
// numbers that matter: the 1k-5k incremental-resynthesis band (2opt-incr E) and
// the 200k-GE memory-fit ceiling (2c-color-size R2).
void Color_stats::print_histogram(const std::vector<uint64_t>& ge_desc) {
  struct Bucket {
    uint64_t    lo, hi;
    const char* label;
  };
  static constexpr Bucket buckets[] = {
      {0, 1, "1"},
      {2, 9, "2-9"},
      {10, 99, "10-99"},
      {100, 999, "100-999"},
      {1000, 4999, "1k-5k"},
      {5000, 49999, "5k-50k"},
      {50000, 199999, "50k-200k"},
      {200000, UINT64_MAX, ">=200k"},
  };
  std::print(stderr, "color[stats]: GE histogram\n");
  for (const auto& b : buckets) {
    const auto cnt = std::count_if(ge_desc.begin(), ge_desc.end(), [&](uint64_t g) { return g >= b.lo && g <= b.hi; });
    if (cnt == 0) {
      continue;
    }
    // Bar scaled to the largest bucket would need a second pass; a plain count
    // is what a reader compares across runs anyway.
    std::print(stderr, "color[stats]:   {:>9} GE  {:>8} region(s)\n", b.label, cnt);
  }
}

void Color_stats::report(std::string_view alg, bool per_def, uint64_t min_ge, uint64_t max_ge) const {
  const auto sizes = sizes_desc(false);
  if (sizes.empty()) {
    std::print(stderr, "color[stats]: alg {} -- no partitions ({} node(s), all uncolored)\n", alg, total_nodes_);
    return;
  }

  const uint64_t total = std::accumulate(sizes.begin(), sizes.end(), uint64_t{0});
  const uint64_t max   = sizes.front();
  const uint64_t min   = sizes.back();
  const double   avg   = static_cast<double>(total) / static_cast<double>(sizes.size());
  const uint64_t med   = sizes[sizes.size() / 2];
  const auto     singl = std::count(sizes.begin(), sizes.end(), uint64_t{1});
  // The 2opt-incr sweet spot: synthesis time is super-linear above it, tool
  // overhead dominates below it (todo/livehd/2opt-incr.html).
  const auto band = std::count_if(sizes.begin(), sizes.end(), [](uint64_t n) { return n >= 1000 && n <= 5000; });
  // "% of design" is against the INSTANCE-WEIGHTED total, not the per-def sum. A
  // 100-node partition in a top that also holds a 10-node def instantiated 1000
  // times is 1% of the design, not 91% of it -- and against the per-def sum the
  // "degenerate" verdict below would fire on a perfectly good coloring.
  const auto pct = [&](uint64_t n) {
    return flat_nodes_ == 0 ? 0.0 : 100.0 * static_cast<double>(n) / static_cast<double>(flat_nodes_);
  };

  std::print(stderr,
             "color[stats]: alg {} -- {} partition(s) over {} def(s), {} partitionable node(s)\n",
             alg,
             sizes.size(),
             defs_.size(),
             total_nodes_);
  std::print(stderr,
             "color[stats]: size      max {} ({:.1f}% of design), min {}, avg {:.1f}, median {}\n",
             max,
             pct(max),
             min,
             avg,
             med);
  std::print(stderr,
             "color[stats]: shape     {} singleton(s), {} in the 1k-5k band, {} uncolored node(s) ({:.1f}%)\n",
             singl,
             band,
             total_uncolored_,
             pct(total_uncolored_));

  // Gate equivalents: the unit the size window bounds and the one that predicts
  // ABC's memory. Reported next to the node counts rather than instead of them --
  // the gap between the two IS the finding on a wide-datapath design.
  const auto ge = sizes_desc(true);
  if (!ge.empty() && total_ge_ != 0) {
    const double   ge_avg = static_cast<double>(total_ge_) / static_cast<double>(ge.size());
    const uint64_t ge_pct_den = total_ge_;
    std::print(stderr,
               "color[stats]: GE        max {} ({:.1f}% of GE), min {}, avg {:.1f}, median {}, total {}\n",
               ge.front(),
               100.0 * static_cast<double>(ge.front()) / static_cast<double>(ge_pct_den),
               ge.back(),
               ge_avg,
               ge[ge.size() / 2],
               total_ge_);
    print_histogram(ge);

    // Did the window actually hold? A leftover here is not noise: pass.abc's
    // admission FATALs the whole run on the first over-max region it meets.
    if (min_ge != 0 || max_ge != 0) {
      const auto under = min_ge == 0 ? 0 : std::count_if(ge.begin(), ge.end(), [&](uint64_t g) { return g < min_ge; });
      const auto over  = max_ge == 0 ? 0 : std::count_if(ge.begin(), ge.end(), [&](uint64_t g) { return g > max_ge; });
      std::print(stderr,
                 "color[stats]: window    min {} / max {} GE -- {} region(s) under min, {} over max\n",
                 min_ge,
                 max_ge,
                 under,
                 over);
      if (under != 0) {
        std::print(stderr,
                   "color[stats]:           under-min leftovers are def-bound: a def whose whole body is "
                   "below min has no neighbour to merge with (absorb folds those into their parents)\n");
      }
      if (over != 0) {
        std::print(stderr,
                   "color[stats]:           OVER-MAX regions remain -- pass.abc admission may refuse this "
                   "design; an indivisible single node (a wide Mult) can exceed max on its own\n");
      }
    }
  }
  if (absorbed_defs_ != 0) {
    std::print(stderr, "color[stats]: absorb    {} def(s) inlined into their parents (below min)\n", absorbed_defs_);
  }

  if (per_def) {
    auto rows = defs_;
    std::sort(rows.begin(), rows.end(), [](const Def_row& a, const Def_row& b) {
      return std::tie(b.nodes, a.def) < std::tie(a.nodes, b.def);
    });
    std::print(stderr, "color[stats]: {:<52}{:>8}{:>10}{:>9}{:>10}\n", "def", "nodes", "GE", "colors", "uncolored");
    for (const auto& r : rows) {
      std::print(stderr, "color[stats]: {:<52}{:>8}{:>10}{:>9}{:>10}\n", r.def, r.nodes, r.ge, r.colors, r.uncolored);
    }
  }

  // `flat` is the one coloring whose partitions are NOT what ABC maps: it turns
  // on pass.abc's flatten=auto, so the hierarchy is inlined and the whole design
  // becomes a SINGLE region holding every instance. Reporting the per-def rows
  // would understate that region by the instance multiplier -- on a deep
  // hierarchy, by orders of magnitude. Instance counts come from hier_range,
  // which walks the structure tree only, so this costs nothing.
  if (alg == "flat") {
    std::print(stderr,
               "color[stats]: FLATTENED pass.abc inlines the hierarchy into ONE region: {} node(s) "
               "({} def(s) x their instance counts)\n",
               flat_nodes_,
               defs_.size());
    return;
  }

  // The verdict line: one partition holding the whole design is the shape that
  // makes pass.abc bit-blast everything at once, so say so rather than leaving
  // it to be read out of the numbers.
  if (sizes.size() == 1) {
    std::print(stderr, "color[stats]: => ONE partition holds the whole design -- pass.abc will map it as a single region\n");
  } else if (pct(max) >= 90.0) {
    std::print(stderr,
               "color[stats]: => degenerate: one partition holds {:.1f}% of the design; the rest are noise\n",
               pct(max));
  }
}

}  // namespace livehd::color
