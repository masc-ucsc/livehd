// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "bind_graph.hpp"
#include "core/interned_name.hpp"
#include "core/physical_rc.hpp"
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
    size_t binds = 0, ranks = 0, remaps = 0, bad_trees = 0, unresolved_labels = 0;
    double bind_seconds = 0, map_seconds = 0, wire_seconds = 0, engine_seconds = 0, feedback_seconds = 0;
  };

  explicit Physical_ranker(Options options);

  [[nodiscard]] livehd::timing::Feedback rank(const std::shared_ptr<hhds::Graph>&                  graph,
                                              const std::shared_ptr<const physical::Parasitics>& rc,
                                              const std::set<std::string>&                       cuts);

  [[nodiscard]] const Statistics&  statistics() const { return stats_; }
  [[nodiscard]] const Engine*      engine() const { return engine_.get(); }
  [[nodiscard]] const Bound_graph* bound() const { return bound_.get(); }
  // What the constraint reader and the binding could not honor, for the log.
  [[nodiscard]] std::vector<std::string> unsupported() const;

private:
  struct Mapping {
    std::vector<Pin_label> labels;      // per RC node of every net, in order (empty for a junction)
    std::vector<uint32_t>  pin;         // the engine pin of each, no_index for a junction or an unmapped label
    std::vector<uint32_t>  net_start;   // per RC net: first entry; plus one more
    std::vector<uint32_t>  net_driver;  // per RC net: engine pin of the driver, no_index when unmapped
    std::vector<uint32_t>  net_root;    // per RC net: the node of the driver
    std::shared_ptr<const livehd::timing::Feedback::Ranking::Layout> layout;
    std::vector<uint32_t>                                            layout_pin;  // per label of the layout
  };

  void     bind(const std::shared_ptr<hhds::Graph>& graph);
  void     build_engine(const std::set<std::string>& cuts, const physical::Parasitics& rc);
  bool     mapping_valid(const physical::Parasitics& rc) const;
  void     remap(const physical::Parasitics& rc);
  uint32_t resolve_label(const Pin_label& label) const;
  Wire_model wire_model(const physical::Parasitics& rc);

  Options                    options_;
  std::vector<liberty::timing::Library> libraries_;
  Cell_types                 types_;
  Sdc                        sdc_;
  std::unique_ptr<Bound_graph> bound_;
  const hhds::Graph*         bound_graph_ = nullptr;
  uint64_t                   bound_epoch_ = 0;
  std::unique_ptr<Engine>    engine_;
  Constraints                constraints_;
  std::set<std::string>      engine_cuts_;
  // Views into the graph (valid while it is bound): instance name -> cell, bare port name -> its pin.
  absl::flat_hash_map<std::string_view, uint32_t> cell_index_, port_index_;
  Mapping                    mapping_;
  bool                       mapping_bound_ = false;
  Statistics                 stats_;
  Elmore                     solver_;
};

}  // namespace livehd::timing::fast
