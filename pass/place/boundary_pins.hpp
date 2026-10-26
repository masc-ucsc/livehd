// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "busorder.hpp"

namespace livehd::placement {
struct Boundary_pin_options {
  bool     enabled = false, split = true, order = false, align = false;
  unsigned iterations          = 50;
  double   max_displacement_um = 20;
};
void validate_boundary_pin_options(const Boundary_pin_options&);
struct Boundary_pin {
  int    node = -1, original_net = -1;
  double x = 0, y = 0;
  int    edge = -1;
};
struct Boundary_pin_order_stats {
  unsigned groups = 0, unordered_before = 0, unordered_after = 0, ambiguous_groups = 0, capacity_edges = 0, corner_pins = 0,
           moved = 0, macro_pins_before = 0, macro_pins_after = 0;
  double seconds = 0, before = 0, after = 0;
};
struct Boundary_pin_model {
  DreamPlace::native::Design design;
  std::vector<Boundary_pin>  anchors;
  std::vector<int>           parent_net;
  // Each local net belongs to one rectangle; -1 denotes exterior/unsplit.
  std::vector<int>           piece_region;
  // Original nets are driver-first (the LiveHD native design convention).
  std::vector<bool>          pin_is_driver;
  std::vector<Region_edge>   edges;
  unsigned                   split_nets = 0, skipped_high_degree = 0;
};
// Temporary optimization topology only. Each original pin occurs exactly once;
// connected RSMT pieces in different rectangles share zero-area fixed terminals.
Boundary_pin_model split_boundary_nets(const DreamPlace::native::Design&, const std::vector<Region_rect>&, const Busorder_route&);
Boundary_pin_order_stats order_boundary_pins(Boundary_pin_model&, const std::vector<Region_rect>&, const std::vector<Bus_bit>&,
                                             const std::vector<int>& macros = {});
struct Pin_align_pair {
  int         input = -1, output = -1, rectangle = -1, cell = -1;
  std::string outcome;
  int         input_cell_pin = -1, output_cell_pin = -1;  // temporary split-model pin IDs
  double      input_target = 0, output_target = 0;        // cell-pin tangent coordinates at assignment
};
struct Pin_align_stats {
  unsigned                    direct = 0, one_cell = 0, aligned = 0, already_aligned = 0, moved = 0;
  unsigned                    ambiguous = 0, order_conflicts = 0, spacing_conflicts = 0, range_conflicts = 0, macro_conflicts = 0;
  std::vector<Pin_align_pair> pairs;
};
Pin_align_stats align_boundary_pins(Boundary_pin_model&, const std::vector<Region_rect>&, const std::vector<Bus_bit>&,
                                    const std::vector<int>& macros = {});
struct Boundary_pin_sample {
  std::string stage;
  unsigned    iteration = 0;
  double      hpwl = 0, overflow = 0, split_hpwl = 0, seconds = 0;
};
class Boundary_pins {
  Boundary_pin_options             options;
  std::vector<Region_rect>         mesh;
  Busorder_route                   route;
  double                           dbu;
  std::string                      output;
  Boundary_pin_model               model;
  std::vector<Bus_bit>             identities;
  std::vector<int>                 macros;
  std::vector<Boundary_pin>        original_anchors;
  Boundary_pin_order_stats         ordering;
  Pin_align_stats                  alignment;
  std::vector<Boundary_pin_sample> history;
  DreamPlace::native::Options      measure_options;
  double                           build_seconds  = 0;
  size_t                           original_nodes = 0, original_pins = 0, original_nets = 0;
  void                             write() const;

public:
  Boundary_pins(const Region_mesh&, Busorder_route, double, Boundary_pin_options, std::string output_dir = {},
                std::vector<Bus_bit> bus_bits = {}, std::vector<int> macro_nodes = {});
  void                                    refine(DreamPlace::native::Design&, const DreamPlace::native::Options&);
  void                                    observe(const DreamPlace::native::Design&, const std::string& stage);
  const std::vector<Boundary_pin_sample>& samples() const { return history; }
};
}  // namespace livehd::placement
