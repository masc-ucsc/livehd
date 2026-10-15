// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace livehd::physical {
struct Rc_node {
  std::string pin;
  double      capacitance_ff = 0;
};
struct Rc_edge {
  unsigned from = 0, to = 0;
  double   resistance_ohm = 0;
};
struct Rc_net {
  std::string          driver;
  std::vector<Rc_node> nodes;
  std::vector<Rc_edge> edges;
};
struct Parasitics {
  std::string         identity;
  bool                approximate = true;
  std::vector<Rc_net> nets;
  struct Io {
    std::string name;
    int64_t     x = 0, y = 0;
  };
  std::vector<Io> terminals;
  struct Cell {
    std::string name;
    int64_t     x = 0, y = 0;
    unsigned    orientation = 0;
  };
  std::vector<Cell> cells;
};
std::string serialize_parasitics(const Parasitics& rc);
Parasitics  deserialize_parasitics(std::string_view text);
}  // namespace livehd::physical
