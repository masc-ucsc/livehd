// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "bind_graph.hpp"
#include "core/interned_name.hpp"
#include "core/physical_rc.hpp"
#include "core/timing_whatif.hpp"
#include "core/timing_feedback.hpp"
#include "engine.hpp"
#include "sdc.hpp"
#include "wire.hpp"

// The fast engine behind the placement `rank` callback (todo 2q-fasttimer, step E): the placed, mapped
// graph and the placer's RC snapshot in, the `Feedback::Ranking` the global placer weights nets with out.
//
// The graph is bound to the numeric netlist when its structure changes (hhds body epoch); a resize keeps the
// cell index and only changes its type, so it rebinds too. The RC snapshot is mapped to engine pins by label
// once per snapshot topology; every call then solves the Elmore tree of each net per corner, hands the engine
// the net capacitance, sink delay and sink slew, and runs `Engine::rank()`.
//
// Same inputs as the OpenTimer session's ranking evaluation (Elmore RC model, late setup slack per pin, the
// ideal clock marked), so the two can be compared label by label.
namespace livehd::timing::fast {

// The identity of the wire trees of a placed design, built once while the placed graph keeps its connectivity: for each
// tree (a net with at least two pins) its pins by label, and which of them drives. Pointer identity tells the ranker
// that the labels are the same as last time.
struct Tree_topology {
  std::vector<uint32_t>  net_pin_start;  // [tree] first pin; one more entry at the end
  std::vector<Pin_label> pin_label;      // per tree pin
  std::vector<uint32_t>  driver_index;   // per tree: the index of the driver among its pins
  [[nodiscard]] size_t   trees() const { return driver_index.size(); }
};
// The wire of every tree of one placement state as numbers (no labels, no strings): the nodes of tree k are the pins of
// the topology in order, then the junctions; the edges are the resistors between nodes of the tree.
struct Wire_trees {
  std::shared_ptr<const Tree_topology> topology;
  std::vector<uint32_t>                node_start, edge_start;  // [tree], one more entry at the end
  std::vector<float>                   node_cap_ff;             // the wire capacitance at each node
  std::vector<Wire_edge>               edges;
  [[nodiscard]] size_t                 bytes() const {
    return (node_start.capacity() + edge_start.capacity()) * 4 + node_cap_ff.capacity() * 4 + edges.capacity() * sizeof(Wire_edge);
  }
};

class Physical_ranker {
public:
  struct Options {
    std::vector<std::string> liberty_texts;  // one concatenated Liberty text per corner; the first is the setup corner
    std::vector<std::string> corner_names;   // optional, same length
    std::string              sdc_text;
    // Unit-less SDC numbers are in the units of the first Liberty (as every SDC consumer reads them).
    fast::Options            engine;
  };
  struct Statistics {
    size_t binds = 0, ranks = 0, evaluations = 0, remaps = 0, bad_trees = 0, unresolved_labels = 0;
    double bind_seconds = 0, map_seconds = 0, wire_seconds = 0, engine_seconds = 0, feedback_seconds = 0;
  };

  explicit Physical_ranker(Options options);
  ~Physical_ranker();

  [[nodiscard]] livehd::timing::Feedback rank(const std::shared_ptr<hhds::Graph>&                  graph,
                                              const std::shared_ptr<const physical::Parasitics>& rc,
                                              const std::set<std::string>&                       cuts);

  // The same ranking from numeric wire trees (see Wire_trees). `skeleton` is what the caller keeps as its parasitics: its
  // nets are the trees, in order (only the driver label of each is read), and the ranking refers to it.
  [[nodiscard]] livehd::timing::Feedback rank_trees(const std::shared_ptr<hhds::Graph>& graph, const Wire_trees& trees,
                                                    const std::shared_ptr<const physical::Parasitics>& skeleton,
                                                    const std::set<std::string>&                       cuts);

  // The detailed evaluation: everything `rank` does, then true requirements, hold, the electrical limits, and the
  // per-pin arrival, required, slack, hold slack, slew, load and capacitance (`Feedback::dense`), the worst path and
  // the totals, as OpenTimer's detailed physical evaluation returns them.
  [[nodiscard]] livehd::timing::Feedback evaluate(const std::shared_ptr<hhds::Graph>&                  graph,
                                                  const std::shared_ptr<const physical::Parasitics>& rc,
                                                  const std::set<std::string>&                       cuts);

  // Transient edits on top of the last evaluate(), answered incrementally by the engine (see Engine's edit
  // sessions). Valid after an evaluate(); the next rank() or evaluate() discards what is pending. The hold slack
  // of a pin is not kept up to date by edits (Pin_timing::hold_slack is empty). The electrical checks are the Liberty
  // slew and capacitance limits and the fanout limit of the SDC (set_max_fanout on the design).
  [[nodiscard]] livehd::timing::Whatif& whatif();

  [[nodiscard]] const Statistics&  statistics() const { return stats_; }
  [[nodiscard]] const Engine*      engine() const { return engine_.get(); }
  [[nodiscard]] const Bound_graph* bound() const { return bound_.get(); }
  // What the constraint reader and the binding could not honor, for the log.
  [[nodiscard]] std::vector<std::string> unsupported() const;

private:
  class Whatif_impl;
  struct Mapping {
    std::vector<Pin_label> labels;      // per RC node of every net, in order (empty for a junction)
    std::vector<uint32_t>  pin;         // the engine pin of each, no_index for a junction or an unmapped label
    std::vector<uint32_t>  net_start;   // per RC net: first entry; plus one more
    std::vector<uint32_t>  net_driver;  // per RC net: engine pin of the driver, no_index when unmapped
    std::vector<uint32_t>  net_root;    // per RC net: the node of the driver
    const void*                                                      source = nullptr;  // the Tree_topology of a tree mapping
    std::shared_ptr<const livehd::timing::Feedback::Ranking::Layout> layout;
    std::vector<uint32_t>                                            layout_pin;  // per label of the layout
    std::shared_ptr<const livehd::timing::Feedback::Pin_index>       index;       // the same labels, for dense feedback
  };

  void     prepare(const std::shared_ptr<hhds::Graph>& graph, const std::shared_ptr<const physical::Parasitics>& rc,
                   const std::set<std::string>& cuts);
  void     bind(const std::shared_ptr<hhds::Graph>& graph);
  void     build_engine(const std::set<std::string>& cuts, const physical::Parasitics& rc);
  bool     mapping_valid(const physical::Parasitics& rc) const;
  void     remap(const physical::Parasitics& rc);
  uint32_t resolve_label(const Pin_label& label) const;
  Wire_model wire_model(const physical::Parasitics& rc);
  struct Solved {
    std::vector<float> cap, delay, slew;
  };
  bool solve_net(const physical::Rc_net& net, std::span<const uint32_t> node_pins, uint32_t root, uint32_t driver, Solved& out);
  bool solve_arrays(std::span<const float> wire_cap, std::span<const Wire_edge> edges, std::span<const uint32_t> node_pins,
                    uint32_t root, uint32_t driver, Solved& out);
  livehd::timing::Feedback ranking_feedback(const std::shared_ptr<const physical::Parasitics>& rc);
  void build_layout(Mapping& m, std::vector<std::pair<Pin_label, uint32_t>>& named, std::span<const Pin_label> labels, size_t nets);
  void remap_trees(const std::shared_ptr<const Tree_topology>& topology);
  Wire_model wire_model_trees(const Wire_trees& trees);

  Options                    options_;
  std::vector<liberty::timing::Library> libraries_;
  Cell_types                 types_;
  Sdc_units                  sdc_units_;
  std::unique_ptr<Bound_graph> bound_;
  const hhds::Graph*         bound_graph_ = nullptr;
  uint64_t                   bound_epoch_ = 0;
  std::unique_ptr<Engine>    engine_;
  Constraints                constraints_;
  std::set<std::string>      engine_cuts_;
  // Views into the graph (valid while it is bound): instance name -> cell, bare port name -> its pin.
  absl::flat_hash_map<std::string_view, uint32_t> port_index_;
  std::unique_ptr<Bound_names>                    names_;
  Mapping                    mapping_;
  bool                       mapping_bound_ = false;
  Statistics                 stats_;
  // The cells an open what-if added, by instance name, and the other way round.
  std::unordered_map<std::string, uint32_t> added_cells_;
  std::unordered_map<uint32_t, std::string> added_names_;
  std::unique_ptr<Whatif_impl>              whatif_;
  std::shared_ptr<const physical::Parasitics> rc_;  // the snapshot the engine was last evaluated with
  size_t                                    mapping_generation_ = 0;
  Elmore                     solver_;
  std::vector<Wire_edge>     edges_;
  std::vector<float>         node_cap_, node_wire_cap_;
  std::shared_ptr<const Tree_topology> mapping_topology_;  // keeps the topology of a tree mapping alive
};

}  // namespace livehd::timing::fast
