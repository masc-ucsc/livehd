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
#include "repair.hpp"
#include "steiner_repair.hpp"
#include "technology_rc.hpp"

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
const Buffer*         combinational(Node n, const Inventory& cells);
bool                  identity(const Buffer& cell);
bool                  matches_dont_use(const std::string& name, const Options& options);
const Buffer*         buffer(Node n, const Inventory& cells);
bool                  fixed(Node n);
Pin                   output(Node n, const Buffer& cell);
std::vector<Pin>      sinks(Pin driver);
Inventory             inventory(const Options& options, const physical::Technology& tech);
Special_pins          scan_special_pins(const std::string& libs, bool protect_reset);
Special_pins                   scan_special_pins(const liberty::Cell_library& library, bool protect_reset);
std::set<std::string> protected_buffers(hhds::Graph& graph, const Inventory& cells, const Special_pins& special, bool protect_reset,
                                        const std::string& control_ports = {});
std::string           physical_label(Pin p);
std::shared_ptr<hhds::GraphIO> buffer_io(hhds::Graph& graph, const Buffer& b);
void                           change_master(Node n, const Buffer& from, const Buffer& to);
Physical_sink                  physical_pin(hhds::Graph& graph, const std::string& label, const Node_index& nodes, bool driver);
void                           connect_bit(hhds::Graph& graph, const Physical_sink& sink, Pin driver);
// Re-target timing weights after an accepted repair trial. Untimed placements
// (no weights) stay unweighted, and aggregate-only feedback has no per-net
// target to move toward.
inline void                    refresh_repair_weights(Result& state, const Options& options, const Timing_feedback& timing) {
  if (!state.net_weights.empty() && !timing.pins.empty()) {
    refresh_timing_weights(state, options, timing);
  }
}
// Cached Liberty characterization for pre-route tree proposals. A proposal is
// a private graph with only newly inserted repeaters fixed at free legal sites;
// it is not an accepted placement until its physical branches route and pass STA.
struct Repeater_graph {
  std::shared_ptr<hhds::GraphLibrary>              library;
  std::shared_ptr<hhds::Graph>                     graph;
  Buffer_tree                                      tree;
  std::vector<std::string>                         sinks;
  std::vector<physical::Component>                 cells;  // tree buffer index order
  std::vector<std::pair<std::string, std::string>> ports;  // input/output Liberty names
};
class Repeater_proposer {
  const Options&                                                       options_;
  const physical::Technology&                                          tech_;
  Inventory                                                            cells_;
  Special_pins                                                         special_;
  bool                                                                 prepared_ = false;
  std::map<std::pair<std::string, std::string>, liberty::Output_drive> drives_;

public:
  Repeater_proposer(const Options&, const physical::Technology&);
  double                        length_limit_um(Layer_rc routing_rc) const;
  // Shared long-net characterization; defer until after the short-net batch.
  void                          prepare();
  std::optional<Repeater_graph> propose(const Result&, const Timing_feedback&, const std::string& driver, Layer_rc routing_rc,
                                        std::chrono::steady_clock::time_point deadline);
};
}  // namespace livehd::placement::detail
