// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Netlist/Liberty helpers shared by the legacy repair (repair.cpp) and the
// path-based resize engine (resize_engine.cpp). Internal to pass/place.
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "hhds/graph.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "place.hpp"

namespace livehd::placement::detail {
using Node         = hhds::Node_class;
using Pin          = hhds::Pin_class;
using Buffer       = liberty::Comb_cell;
using Inventory    = std::map<std::string, Buffer>;
using Special_pins = std::map<std::string, std::set<std::string>>;
using Node_index   = std::unordered_map<std::string, Node>;
struct Physical_sink {
  Pin      pin;
  unsigned bit = 0, width = 1;
};
const Buffer*                  combinational(Node n, const Inventory& cells);
bool                           identity(const Buffer& cell);
bool                           matches_dont_use(const std::string& name, const Options& options);
const Buffer*                  buffer(Node n, const Inventory& cells);
bool                           fixed(Node n);
Pin                            output(Node n, const Buffer& cell);
std::vector<Pin>               sinks(Pin driver);
Inventory                      inventory(const Options& options, const physical::Technology& tech);
Special_pins                   scan_special_pins(const std::string& libs, bool protect_reset);
std::set<std::string>          protected_buffers(hhds::Graph& graph, const Inventory& cells, const Special_pins& special,
                                                 bool protect_reset);
std::string                    physical_label(Pin p);
std::shared_ptr<hhds::GraphIO> buffer_io(hhds::Graph& graph, const Buffer& b);
void                           change_master(Node n, const Buffer& from, const Buffer& to);
Physical_sink                  physical_pin(hhds::Graph& graph, const std::string& label, const Node_index& nodes, bool driver);
void                           connect_bit(hhds::Graph& graph, const Physical_sink& sink, Pin driver);
}  // namespace livehd::placement::detail
