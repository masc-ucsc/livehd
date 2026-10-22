// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "dreamplace/native/placement.hpp"

namespace livehd::placement {
struct Busorder_options {
  bool     enabled    = false;
  unsigned iterations = 100, refresh = 20, max_cells = 10000;
  double   weight = 4, merge_um = 5, max_displacement_um = 20;
};
void validate_busorder_options(const Busorder_options&);
struct Bus_bit {
  std::string bus;
  int         bit = -1;
};
std::optional<Bus_bit> indexed_bus_bit(const std::string&);
struct Busorder_io {
  std::string name;
  double      x = 0, y = 0;
};
using Busorder_point = std::pair<double, double>;
using Busorder_tree  = std::vector<std::pair<Busorder_point, Busorder_point>>;
using Busorder_route = std::function<Busorder_tree(const std::vector<Busorder_point>&)>;
struct Busorder_rect {
  double   xl, yl, xh, yh;
  unsigned cells = 0;
};
struct Busorder_edge {
  int    axis     = 0;  // normal axis: vertical boundary = 0, horizontal = 1
  double position = 0, low = 0, high = 0;
};
// Split shared collinear boundaries at every endpoint, including T junctions.
std::vector<Busorder_edge> busorder_edges(const std::vector<Busorder_rect>&);
struct Busorder_crossing {
  int    edge = -1, net = -1, bit = -1, pin = -1;
  double coordinate = 0;
};
struct Busorder_sample {
  std::string stage;
  unsigned    iterations = 0, groups = 0, unordered = 0, constraints = 0;
  unsigned    branching = 0, immovable = 0, high_degree = 0;
  unsigned    initially_unordered = 0, resolved = 0, remaining = 0, ambiguous = 0, no_longer_comparable = 0;
  double      hpwl = 0, overflow = -1, penalty = 0, seconds = 0;
};
// Temporary final-GP model. Proxies are pin-coordinate expressions, not nodes.
// Bus identities and real macro IDs are supplied by LiveHD; net_mask excludes
// clocks/reset. All geometry is in the native design's coordinate units.
class Busorder {
  Busorder_options                                     options;
  double                                               dbu;
  std::vector<Bus_bit>                                 bits;
  Busorder_route                                       route;
  std::vector<Busorder_rect>                           mesh;
  std::vector<Busorder_edge>                           edges;
  std::map<std::pair<int, std::string>, bool>          descending;
  std::vector<Busorder_sample>                         history;
  std::map<std::pair<int, std::string>, std::set<int>> initial_unordered;
  bool                                                 cohort_initialized = false;
  unsigned                                             initial_rectangles = 0, removed_h = 0, removed_v = 0;
  double                                               build_seconds = 0;
  std::string                                          output;
  int                                                  density_bins_x = 0, density_bins_y = 0;
  double                                               density_target      = 0;
  bool                                                 density_scale_macro = false;
  std::vector<Busorder_crossing>                       crossings(const DreamPlace::native::Design&, unsigned&) const;
  Busorder_sample                                      evaluate(const DreamPlace::native::Design&, const std::string&, unsigned,
                                                                std::vector<DreamPlace::native::RelativeOrder>*);
  void                                                 write() const;

public:
  Busorder(const DreamPlace::native::Design&, const std::vector<int>& macros, const std::vector<Busorder_io>&,
           const std::vector<Bus_bit>&, Busorder_route, double dbu_per_um, Busorder_options, std::string output_dir = {});
  void                                           refine(DreamPlace::native::Design&, const DreamPlace::native::Options&);
  void                                           observe(const DreamPlace::native::Design&, const std::string& stage);
  const std::vector<Busorder_rect>&              rectangles() const { return mesh; }
  const std::vector<Busorder_sample>&            samples() const { return history; }
  std::vector<DreamPlace::native::RelativeOrder> constraints(const DreamPlace::native::Design&);
};
}  // namespace livehd::placement
