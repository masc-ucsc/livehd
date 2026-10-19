// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "color_centroids.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>

namespace livehd::placement {
namespace dp = DreamPlace::native;

std::vector<int> centroid_node_scopes(const dp::Design& d, const std::vector<std::string>& paths) {
  if (paths.size() != d.node_x.size()) {
    throw std::invalid_argument("centroid hierarchy path size mismatch");
  }
  std::vector<int> scopes(paths.size(), -1);
  for (size_t n = 0; n < paths.size(); ++n) {
    for (size_t g = 0; g < d.centroid_groups.size(); ++g) {
      const auto& name = d.centroid_groups[g].name;
      if (!name.starts_with("@") && paths[n].starts_with(name + '.')
          && (scopes[n] < 0 || name.size() > d.centroid_groups[scopes[n]].name.size())) {
        scopes[n] = g;
      }
    }
  }
  return scopes;
}

void placement_color_centroids(dp::Design& d, const std::vector<std::string>& paths, const color::Floorplan_coloring& coloring,
                               double minimum, bool frozen) {
  if (!std::isfinite(minimum) || minimum < 0 || paths.size() != d.node_x.size() || coloring.owner.size() != d.node_x.size()
      || coloring.recipients.size() != d.node_x.size() || d.centroid_node_area.size() != d.node_x.size()) {
    throw std::invalid_argument("placement color geometry mismatch");
  }
  const double     unit   = dp::detail::centroid_cell_area(d);
  const auto       within = [&](int n, int g) { return paths[n].starts_with(d.centroid_groups[g].name + '.'); };
  const auto       scopes = frozen ? std::vector<int>{} : centroid_node_scopes(d, paths);
  std::vector<int> leaf(coloring.colors.size(), -1);
  size_t           hierarchy = 0;
  int              family    = 0;
  for (size_t g = 0; g < d.centroid_groups.size(); ++g) {
    auto& group = d.centroid_groups[g];
    family      = std::max(family, group.family + 1);
    if (group.name.starts_with("@placement:")) {
      const auto name = group.name.substr(11);
      for (size_t c = 0; c < coloring.colors.size(); ++c) {
        if (coloring.colors[c].name == name) {
          leaf[c]                = g;
          group.colors           = {static_cast<int>(c)};
          group.scope_cells      = coloring.colors[c].members.size();
          group.scope_macros     = coloring.colors[c].macros;
          group.equivalent_cells = coloring.colors[c].area / unit;
        }
      }
    } else {
      ++hierarchy;
    }
  }
  if (!frozen) {
    for (size_t c = 0; c < coloring.colors.size(); ++c) {
      const auto& color = coloring.colors[c];
      if (!color.members.empty() && !std::all_of(color.members.begin(), color.members.end(), [&](int n) {
            return scopes[n] == scopes[color.members[0]];
          })) {
        throw std::invalid_argument("placement color must be split by hierarchy scope before centroid construction");
      }
      const bool movable
          = std::any_of(color.members.begin(), color.members.end(), [&](int n) { return n < d.num_movable && d.node_macro[n]; });
      if (!movable || color.area / unit <= minimum) {
        continue;
      }
      dp::CentroidGroup group;
      group.name              = "@placement:" + color.name;
      group.family            = family;
      group.colors            = {static_cast<int>(c)};
      group.scope_macros      = color.macros;
      group.scope_cells       = color.members.size();
      group.equivalent_cells  = color.area / unit;
      group.copies_per_parent = 1;
      for (size_t g = 0; g < hierarchy; ++g) {
        if (std::all_of(color.members.begin(), color.members.end(), [&](int n) { return within(n, g); })
            && (group.parent < 0 || d.centroid_groups[g].name.size() > d.centroid_groups[group.parent].name.size())) {
          group.parent = g;
        }
      }
      leaf[c] = d.centroid_groups.size();
      d.centroid_groups.push_back(std::move(group));
    }
  }
  std::vector<int> inherited(d.node_x.size(), -1);
  if (frozen) {
    for (size_t g = 0; g < hierarchy; ++g) {
      for (int n : d.centroid_groups[g].nodes) {
        inherited[n] = g;
      }
    }
  }
  for (size_t g = 0; g < d.centroid_groups.size(); ++g) {
    auto& group           = d.centroid_groups[g];
    group.repulsion_scope = group.parent + 1;
    group.repulsion_area  = 0;
    if (g < hierarchy) {
      group.nodes.clear();
      group.colors.clear();
    }
  }
  const auto allocate = [&](int owner, double area) {
    for (int g = owner; g >= 0; g = d.centroid_groups[g].parent) {
      d.centroid_groups[g].repulsion_area += area;
    }
  };
  for (size_t n = 0; n < d.node_x.size(); ++n) {
    const double area = d.centroid_node_area[n];
    if (area <= 0) {
      continue;
    }
    int owner = frozen ? inherited[n] : -1;
    for (size_t g = 0; !frozen && g < hierarchy; ++g) {
      if (within(n, g) && (owner < 0 || d.centroid_groups[g].name.size() > d.centroid_groups[owner].name.size())) {
        owner = g;
      }
    }
    const int c = coloring.owner[n];
    for (int g = owner; g >= 0; g = d.centroid_groups[g].parent) {
      d.centroid_groups[g].nodes.push_back(n);
    }
    std::vector<int> recipients;
    if (c < 0) {
      for (int recipient : coloring.recipients[n]) {
        if (leaf[recipient] >= 0) {
          recipients.push_back(leaf[recipient]);
        }
      }
    }
    if (!recipients.empty()) {
      for (int g : recipients) {
        allocate(g, area / recipients.size());
      }
    } else if (c >= 0 && leaf[c] >= 0) {
      allocate(leaf[c], area);
    } else {
      allocate(owner, area);
    }
  }
}

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
  for (size_t i = hierarchy_count; i < circles.size(); ++i) {
    const int parent = circles[i].parent;
    if (parent >= 0) {
      result.circles[i].x += result.circles.at(parent).x - circles.at(parent).x;
      result.circles[i].y += result.circles.at(parent).y - circles.at(parent).y;
    }
  }
  return result;
}
}  // namespace livehd::placement
