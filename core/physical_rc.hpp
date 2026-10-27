// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace livehd::physical {
struct Rc_node {
  std::string pin;
  double      capacitance_ff                   = 0;
  bool        operator==(const Rc_node&) const = default;
};
struct Rc_edge {
  unsigned from = 0, to = 0;
  double   resistance_ohm                   = 0;
  bool     operator==(const Rc_edge&) const = default;
};
struct Rc_net {
  std::string          driver;
  std::vector<Rc_node> nodes;
  std::vector<Rc_edge> edges;
  double               wire_length_um                  = 0;  // actual estimated Steiner tree, independent of RC units
  bool                 operator==(const Rc_net&) const = default;
};
struct Parasitics {
  std::string         identity;
  bool                approximate = true;
  std::vector<Rc_net> nets;
  struct Io {
    std::string name;
    int64_t     x = 0, y = 0;
    bool        operator==(const Io&) const = default;
  };
  std::vector<Io> terminals;
  struct Cell {
    std::string name;
    int64_t     x = 0, y = 0;
    unsigned    orientation = 0;
    std::string master;
    int64_t     width = 0, height = 0;
    bool        operator==(const Cell&) const = default;
  };
  std::vector<Cell>     cells;
  // False-path distribution drivers carried with this exact physical graph.
  // Persist alongside RC so standalone STA and completed-placement replay use
  // the same reset policy as the optimization that produced the graph.
  std::set<std::string> timing_cuts{};
};
std::string serialize_parasitics(const Parasitics& rc);
Parasitics  deserialize_parasitics(std::string_view text);
// Explicit full-precision numeric parsing for exact checkpoint replay. Existing
// consumers retain the ordinary reader and its historical conversion behavior.
Parasitics  deserialize_parasitics_exact(std::string_view text);
// OpenTimer's RC traversal requires a connected resistor tree. Reject invalid
// producer/what-if topology before its recursive load calculation.
void        validate_rc_tree(const Rc_net& net);
}  // namespace livehd::physical
