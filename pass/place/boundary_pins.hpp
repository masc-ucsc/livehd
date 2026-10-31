// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "busorder.hpp"

namespace livehd::placement {
struct Boundary_pin_options {
  bool     enabled = false, split = true, order = false, align = false;
  unsigned iterations          = 50;
  double   max_displacement_um = 20;
  // With order: GP iterations at the original crossings before the bus order is chosen, so the order follows where
  // the settled cells pull (0 orders at once).
  unsigned settle_iterations   = 20;
  // With order: spacing between assigned pins on an edge (0 = the caller's automatic pitch); an edge without room
  // halves it down to one site.
  double   pitch_um            = 0;
  // Clean region cuts (needs split): a cell whose nets all leave its rectangle through one side moves across it (at
  // most max_displacement_um), and an RSMT piece with no pin that enters and leaves a rectangle through one boundary
  // line is dropped, its crossings joined on the other side.
  bool     clean_cuts          = false;
  // With align: also align a net's input and output pins on opposite edges of a rectangle with cells in between when
  // they are within this many pin pitches (0: only across zero or one cell).
  double   align_near          = 0;
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
  unsigned narrowed_edges = 0;  // edges assigned below the requested pitch
  double   seconds = 0, before = 0, after = 0;
  double   min_pitch = 0;  // smallest pitch used on an ordered edge (design units)
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
  unsigned                   dips_removed = 0;  // remove_dips: pinless pieces dropped
};
// Cells whose nets all leave their rectangle through one side (no other pin of those nets inside it), moved across
// that side into the neighbor rectangle when it is within max_move (design units) and under the cell cap. Returns the
// moved cells.
unsigned                 move_dipping_cells(DreamPlace::native::Design&, const Region_mesh&, double max_move);
// Temporary optimization topology only. Each original pin occurs exactly once;
// connected RSMT pieces in different rectangles share zero-area fixed terminals.
// remove_dips: an RSMT piece without a real pin whose crossings all sit on one boundary line into one other
// rectangle is dropped, its crossings joined in that rectangle.
Boundary_pin_model       split_boundary_nets(const DreamPlace::native::Design&, const Region_mesh&, const Busorder_route&,
                                             bool remove_dips = false);
// pitch (design units): spacing of the assigned pins on an edge; 0 or below one site uses one site. An edge
// without room for its pins at the pitch retries at half of it, down to one site.
Boundary_pin_order_stats order_boundary_pins(Boundary_pin_model&, const Region_mesh&, const std::vector<Bus_bit>&,
                                             const std::vector<int>& macros = {}, double pitch = 0);
struct Pin_align_pair {
  int         input = -1, output = -1, rectangle = -1, cell = -1;
  std::string outcome;
  int         input_cell_pin = -1, output_cell_pin = -1;  // temporary split-model pin IDs
  double      input_target = 0, output_target = 0;        // cell-pin tangent coordinates at assignment
};
struct Pin_align_stats {
  unsigned                    direct = 0, one_cell = 0, near = 0, aligned = 0, already_aligned = 0, moved = 0;
  unsigned                    ambiguous = 0, order_conflicts = 0, spacing_conflicts = 0, range_conflicts = 0, macro_conflicts = 0;
  std::vector<Pin_align_pair> pairs;
};
// near (design units, 0: off): a net's input and output pins on opposite edges with cells in between align when they
// are this close (pairs with cell -2)
Pin_align_stats align_boundary_pins(Boundary_pin_model&, const Region_mesh&, const std::vector<Bus_bit>&,
                                    const std::vector<int>& macros = {}, double near = 0);
struct Boundary_pin_sample {
  std::string stage;
  unsigned    iteration = 0;
  double      hpwl = 0, overflow = 0, split_hpwl = 0, seconds = 0;
};
class Boundary_pins {
  Boundary_pin_options             options;
  Region_mesh                      mesh;
  Busorder_route                   route;
  double                           dbu;
  std::string                      output;
  Boundary_pin_model               model;
  std::vector<Bus_bit>             identities;
  std::vector<int>                 macros;
  std::vector<std::string>         drivers;  // per original net: driver "instance/pin" or the IO name (pins.tsv)
  std::vector<Boundary_pin>        original_anchors;
  Boundary_pin_order_stats         ordering;
  Pin_align_stats                  alignment;
  unsigned                         moved_cells = 0;  // clean_cuts
  std::vector<Boundary_pin_sample> history;
  DreamPlace::native::Options      measure_options;
  double                           build_seconds  = 0;
  size_t                           original_nodes = 0, original_pins = 0, original_nets = 0;
  void                             write() const;

public:
  Boundary_pins(const Region_mesh&, Busorder_route, double, Boundary_pin_options, std::string output_dir = {},
                std::vector<Bus_bit> bus_bits = {}, std::vector<int> macro_nodes = {}, std::vector<std::string> net_drivers = {});
  void                                    refine(DreamPlace::native::Design&, const DreamPlace::native::Options&);
  void                                    observe(const DreamPlace::native::Design&, const std::string& stage);
  const std::vector<Boundary_pin_sample>& samples() const { return history; }
  // The fixed virtual pins (anchors) with their original nets, as refined: legroute cuts region routes there.
  const Boundary_pin_model&               pin_model() const { return model; }
};
}  // namespace livehd::placement
