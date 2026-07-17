// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_stats.hpp"

#include <algorithm>
#include <format>
#include <numeric>
#include <print>
#include <tuple>

namespace livehd::color {

void Color_stats::add(std::string_view def, const Def_color_sizes& sizes, uint64_t instances) {
  Def_row row;
  row.def       = std::string{def};
  row.nodes     = sizes.partitionable;
  row.uncolored = sizes.uncolored;
  row.colors    = sizes.color_nodes.size();
  row.instances = instances;
  defs_.emplace_back(std::move(row));

  total_nodes_ += sizes.partitionable;
  total_uncolored_ += sizes.uncolored;
  flat_nodes_ += sizes.partitionable * instances;

  for (const auto& [color, nodes] : sizes.color_nodes) {
    partitions_.emplace_back(Partition{std::string{def}, color, nodes});
  }
}

std::vector<uint64_t> Color_stats::sizes_desc() const {
  auto sorted = partitions_;
  // Sort by (nodes desc, def, color): color ids are minted from hash-map
  // iteration in the continuous split, so an id-ordered report would not be
  // reproducible run to run.
  std::sort(sorted.begin(), sorted.end(), [](const Partition& a, const Partition& b) {
    return std::tie(b.nodes, a.def, a.color) < std::tie(a.nodes, b.def, b.color);
  });
  std::vector<uint64_t> out;
  out.reserve(sorted.size());
  for (const auto& p : sorted) {
    out.emplace_back(p.nodes);
  }
  return out;
}

void Color_stats::report(std::string_view alg, bool per_def) const {
  const auto sizes = sizes_desc();
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

  if (per_def) {
    auto rows = defs_;
    std::sort(rows.begin(), rows.end(), [](const Def_row& a, const Def_row& b) {
      return std::tie(b.nodes, a.def) < std::tie(a.nodes, b.def);
    });
    std::print(stderr, "color[stats]: {:<52}{:>8}{:>9}{:>10}\n", "def", "nodes", "colors", "uncolored");
    for (const auto& r : rows) {
      std::print(stderr, "color[stats]: {:<52}{:>8}{:>9}{:>10}\n", r.def, r.nodes, r.colors, r.uncolored);
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
