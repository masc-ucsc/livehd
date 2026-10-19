// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "color_centroids.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace livehd::placement {
namespace dp = DreamPlace::native;

void add_color_centroids(dp::Design& d, double minimum) {
  if (!std::isfinite(minimum) || minimum < 0 || d.centroid_node_area.size() != d.node_x.size()
      || d.node_centroid_color.size() != d.node_x.size() || d.node_macro.size() != d.node_x.size()) {
    throw std::invalid_argument("invalid color centroid metadata or minimum area");
  }
  struct Color {
    double area  = 0;
    int    cells = 0, macros = 0, movable_macros = 0;
    bool   covered = false;
  };
  std::vector<Color> colors(d.centroid_color_names.size());
  int                family = 0;
  for (const auto& group : d.centroid_groups) {
    family = std::max(family, group.family + 1);
    for (int color : group.colors) {
      colors.at(color).covered = true;
    }
  }
  for (size_t n = 0; n < d.node_x.size(); ++n) {
    const int color = d.node_centroid_color[n];
    if (color < 0) {
      continue;
    }
    auto& c  = colors.at(color);
    c.area  += d.centroid_node_area[n];
    ++c.cells;
    c.macros         += d.node_macro[n] != 0;
    c.movable_macros += d.node_macro[n] && n < static_cast<size_t>(d.num_movable);
  }
  const double unit = dp::detail::centroid_cell_area(d);
  for (size_t color = 0; color < colors.size(); ++color) {
    const auto& c = colors[color];
    if (c.covered || !c.movable_macros || c.area / unit <= minimum) {
      continue;
    }
    dp::CentroidGroup g;
    g.name              = "@color:" + d.centroid_color_names[color];
    g.family            = family;
    g.scope_macros      = c.macros;
    g.scope_cells       = c.cells;
    g.equivalent_cells  = c.area / unit;
    g.colors            = {static_cast<int>(color)};
    g.copies_per_parent = 1;
    d.centroid_groups.push_back(std::move(g));
  }
  for (auto& g : d.centroid_groups) {
    g.repulsion_scope = g.parent + 1;
  }
}

dp::FloorplanSnap snap_centroid_targets(const std::vector<dp::FloorplanCircle>& circles, size_t hierarchy_count,
                                        const dp::Layout& layout, const dp::FloorplanOptions& options) {
  if (hierarchy_count > circles.size()) {
    throw std::invalid_argument("invalid hierarchy centroid count");
  }
  const std::vector<dp::FloorplanCircle> hierarchy(circles.begin(), circles.begin() + hierarchy_count);
  auto result = dp::snap_floorplan(hierarchy, layout.xl, layout.yl, layout.xh, layout.yh, options);
  result.circles.insert(result.circles.end(), circles.begin() + hierarchy_count, circles.end());
  return result;
}
}  // namespace livehd::placement
