// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_rank.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "absl/container/flat_hash_map.h"

namespace livehd::timing::fast {
namespace {
using Steady = std::chrono::steady_clock;
double since(Steady::time_point start) { return std::chrono::duration<double>(Steady::now() - start).count(); }
}  // namespace

Physical_ranker::Physical_ranker(Options options) : options_(std::move(options)) {
  if (options_.liberty_texts.empty()) {
    throw std::invalid_argument("Physical_ranker needs at least one Liberty corner");
  }
  std::vector<const liberty::timing::Library*> pointers;
  libraries_.reserve(options_.liberty_texts.size());
  for (const auto& text : options_.liberty_texts) {
    libraries_.push_back(liberty::timing::parse(text));
  }
  for (const auto& library : libraries_) {
    pointers.push_back(&library);
  }
  std::vector<std::string> names = options_.corner_names;
  for (size_t i = names.size(); i < libraries_.size(); ++i) {
    names.push_back("corner" + std::to_string(i));
  }
  types_ = Cell_types::build(pointers, names);
  options_.engine.true_slack_in_rank = true;  // the placer's consumers read the slack's sign and size
  sdc_   = read_sdc(options_.sdc_text, {libraries_.front().time_unit_ps, libraries_.front().capacitance_unit_ff});
}

std::vector<std::string> Physical_ranker::unsupported() const {
  std::vector<std::string> out = constraints_.unsupported;
  if (bound_) {
    out.insert(out.end(), bound_->unsupported.begin(), bound_->unsupported.end());
  }
  return out;
}

void Physical_ranker::bind(const std::shared_ptr<hhds::Graph>& graph) {
  const auto start = Steady::now();
  engine_.reset();
  mapping_bound_ = false;
  bound_         = std::make_unique<Bound_graph>(bind_graph(*graph, types_));
  bound_graph_   = graph.get();
  bound_epoch_   = graph->body_epoch();
  cell_index_.clear();
  port_index_.clear();
  const auto& netlist = *bound_->netlist;
  cell_index_.reserve(bound_->cell_node.size());
  for (uint32_t cell = 0; cell < bound_->cell_node.size(); ++cell) {
    if (bound_->cell_node[cell].is_valid()) {
      cell_index_.emplace(bound_->cell_node[cell].get_name(), cell);
    }
  }
  for (const auto& port : netlist.ports()) {
    port_index_.emplace(port.name, netlist.pin(port.cell, 0u));
  }
  constraints_ = resolve(sdc_, netlist);
  ++stats_.binds;
  stats_.bind_seconds += since(start);
}

uint32_t Physical_ranker::resolve_label(const Pin_label& label) const {
  const auto& netlist = *bound_->netlist;
  if (label.bare()) {
    const auto it = port_index_.find(label.pin().view());
    return it == port_index_.end() ? no_index : it->second;
  }
  if (!added_cells_.empty()) {
    if (const auto added = added_cells_.find(label.instance().str()); added != added_cells_.end()) {
      return netlist.pin(added->second, label.pin().view());
    }
  }
  const auto it = cell_index_.find(label.instance().view());
  return it == cell_index_.end() ? no_index : netlist.pin(it->second, label.pin().view());
}

void Physical_ranker::build_engine(const std::set<std::string>& cuts, const physical::Parasitics& rc) {
  auto constraints = constraints_;
  std::set<std::string> all = cuts;
  all.insert(rc.timing_cuts.begin(), rc.timing_cuts.end());
  for (const auto& text : all) {
    if (const auto pin = resolve_label(Pin_label(text)); pin != no_index) {
      constraints.cut_pins.push_back(pin);
    }
  }
  engine_ = std::make_unique<Engine>(*bound_->netlist, std::move(constraints), options_.engine);
  engine_cuts_ = std::move(all);
}

bool Physical_ranker::mapping_valid(const physical::Parasitics& rc) const {
  if (!mapping_bound_ || mapping_.source != nullptr || mapping_.net_driver.size() != rc.nets.size()) {
    return false;
  }
  size_t at = 0;
  for (size_t i = 0; i < rc.nets.size(); ++i) {
    const auto& net = rc.nets[i];
    if (mapping_.net_start[i] != at || at + net.nodes.size() > mapping_.labels.size()) {
      return false;
    }
    for (const auto& node : net.nodes) {
      if (!(mapping_.labels[at++] == node.pin)) {
        return false;
      }
    }
  }
  return at == mapping_.labels.size();
}

void Physical_ranker::remap(const physical::Parasitics& rc) {
  const auto start = Steady::now();
  auto&      m     = mapping_;
  m                = Mapping{};
  size_t nodes     = 0;
  for (const auto& net : rc.nets) {
    nodes += net.nodes.size();
  }
  m.labels.reserve(nodes);
  m.pin.reserve(nodes);
  m.net_start.reserve(rc.nets.size() + 1);
  m.net_driver.reserve(rc.nets.size());
  m.net_root.reserve(rc.nets.size());
  std::vector<std::pair<Pin_label, uint32_t>> named;  // for the layout: every labelled node
  named.reserve(nodes);
  for (const auto& net : rc.nets) {
    m.net_start.push_back(static_cast<uint32_t>(m.labels.size()));
    const auto driver = net.driver.empty() ? no_index : resolve_label(net.driver);
    m.net_driver.push_back(driver);
    uint32_t root = 0;
    for (uint32_t i = 0; i < net.nodes.size(); ++i) {
      const auto& label = net.nodes[i].pin;
      m.labels.push_back(label);
      if (label.empty()) {
        m.pin.push_back(no_index);
        continue;
      }
      const auto pin = resolve_label(label);
      if (pin == no_index) {
        ++stats_.unresolved_labels;
      }
      m.pin.push_back(pin);
      named.emplace_back(label, pin);
      if (label == net.driver) {
        root = i;
      }
    }
    m.net_root.push_back(root);
  }
  m.net_start.push_back(static_cast<uint32_t>(m.labels.size()));
  build_layout(m, named, m.labels, rc.nets.size());
  mapping_bound_ = true;
  ++mapping_generation_;
  ++stats_.remaps;
  stats_.map_seconds += since(start);
}

void Physical_ranker::build_layout(Mapping& m, std::vector<std::pair<Pin_label, uint32_t>>& named, std::span<const Pin_label> labels,
                                   size_t nets) {
  // The ranking layout: sorted unique labels, and per net the layout index of each of its labelled nodes.
  std::ranges::sort(named, [](const auto& a, const auto& b) { return a.first < b.first; });
  named.erase(std::unique(named.begin(), named.end(), [](const auto& a, const auto& b) { return a.first == b.first; }), named.end());
  auto layout = std::make_shared<livehd::timing::Feedback::Ranking::Layout>();
  layout->labels.reserve(named.size());
  m.layout_pin.reserve(named.size());
  for (const auto& [label, pin] : named) {
    layout->labels.push_back(label.str());
    m.layout_pin.push_back(pin);
  }
  layout->net_offsets.reserve(nets + 1);
  for (size_t n = 0; n < nets; ++n) {
    layout->net_offsets.push_back(layout->net_pins.size());
    for (uint32_t k = m.net_start[n]; k < m.net_start[n + 1]; ++k) {
      if (!labels[k].empty()) {
        layout->net_pins.push_back(layout->find(labels[k]));
      }
    }
  }
  layout->net_offsets.push_back(layout->net_pins.size());
  auto index    = std::make_shared<livehd::timing::Feedback::Pin_index>();
  index->labels = layout->labels;
  m.index       = std::move(index);
  m.layout      = std::move(layout);
}

// The Elmore solution of one RC net at every corner: the wire capacitance (the tree's total without the pin
// capacitances the engine adds itself), and the delay and slew at every node ([node][corner]). `node_pins` are the
// engine pins of the nodes (no_index for a junction). False: the tree is not a tree.
bool Physical_ranker::solve_net(const physical::Rc_net& net, std::span<const uint32_t> node_pins, uint32_t root, uint32_t driver,
                                Solved& out) {
  edges_.clear();
  for (const auto& e : net.edges) {
    edges_.push_back({e.from, e.to, static_cast<float>(e.resistance_ohm)});
  }
  node_wire_cap_.clear();
  for (const auto& node : net.nodes) {
    node_wire_cap_.push_back(static_cast<float>(node.capacitance_ff));
  }
  return solve_arrays(node_wire_cap_, edges_, node_pins, root, driver, out);
}

// The Elmore solution of one RC tree at every corner: the wire capacitance (the tree's total without the pin
// capacitances the engine adds itself), and the delay and slew at every node ([node][corner]). `node_pins` are the engine
// pins of the first nodes (no_index for a junction; nodes past its end are junctions). False: it is not a tree.
bool Physical_ranker::solve_arrays(std::span<const float> wire_cap, std::span<const Wire_edge> edges,
                                   std::span<const uint32_t> node_pins, uint32_t root, uint32_t driver, Solved& out) {
  const auto&  netlist = *bound_->netlist;
  const auto&  types   = netlist.types();
  const size_t C       = types.corners();
  const size_t count   = wire_cap.size();
  out.cap.assign(C, 0.f);
  out.delay.assign(count * C, 0.f);
  out.slew.assign(count * C, 0.f);
  std::vector<float>& cap = node_cap_;
  for (size_t k = 0; k < C; ++k) {
    cap.assign(count, 0.f);
    float sink_pins = 0;
    for (size_t i = 0; i < count; ++i) {
      float c = wire_cap[i];
      if (i < node_pins.size() && node_pins[i] != no_index) {
        const auto pin   = node_pins[i];
        const auto cell  = netlist.cell_of(pin);
        const float input = netlist.is_driver(pin) || pin == driver
                                ? 0.f
                                : std::max(types.pin_cap(netlist.type_of(cell), netlist.local_of(pin), k, 0),
                                           types.pin_cap(netlist.type_of(cell), netlist.local_of(pin), k, 1));
        c         += input;
        sink_pins += input;
      }
      cap[i] = c;
    }
    if (!solver_.solve(cap, edges, root)) {
      return false;
    }
    out.cap[k] = std::max(0.f, solver_.total_cap_ff() - sink_pins);
    for (size_t i = 0; i < count; ++i) {
      out.delay[i * C + k] = solver_.delay_ps(static_cast<uint32_t>(i));
      out.slew[i * C + k]  = std::sqrt(solver_.impulse_ps2(static_cast<uint32_t>(i)));
    }
  }
  return true;
}

Wire_model Physical_ranker::wire_model(const physical::Parasitics& rc) {
  const auto  start   = Steady::now();
  const auto& netlist = *bound_->netlist;
  const size_t C      = netlist.types().corners();
  Wire_model wire;
  wire.net_cap_ff.assign(netlist.nets() * C, 0.f);
  wire.sink_delay_ps.assign(netlist.pins() * C, 0.f);
  wire.sink_slew_ps.assign(netlist.pins() * C, 0.f);
  const auto& m = mapping_;
  Solved      solved;
  for (size_t n = 0; n < rc.nets.size(); ++n) {
    const auto  driver = m.net_driver[n];
    const auto& net    = rc.nets[n];
    if (driver == no_index || netlist.net_of(driver) == no_index || net.nodes.empty()) {
      continue;
    }
    const auto first = m.net_start[n];
    if (!solve_net(net, std::span<const uint32_t>(m.pin.data() + first, net.nodes.size()), m.net_root[n], driver, solved)) {
      ++stats_.bad_trees;
      continue;
    }
    const auto engine_net = netlist.net_of(driver);
    for (size_t k = 0; k < C; ++k) {
      wire.net_cap_ff[engine_net * C + k] = solved.cap[k];
    }
    for (size_t i = 0; i < net.nodes.size(); ++i) {
      const auto pin = m.pin[first + i];
      if (pin == no_index || pin == driver) {
        continue;
      }
      for (size_t k = 0; k < C; ++k) {
        wire.sink_delay_ps[static_cast<size_t>(pin) * C + k] = solved.delay[i * C + k];
        wire.sink_slew_ps[static_cast<size_t>(pin) * C + k]  = solved.slew[i * C + k];
      }
    }
  }
  stats_.wire_seconds += since(start);
  return wire;
}

void Physical_ranker::remap_trees(const std::shared_ptr<const Tree_topology>& topology) {
  const auto start = Steady::now();
  auto&      m     = mapping_;
  m                = Mapping{};
  m.source         = topology.get();
  const auto& t    = *topology;
  m.net_start      = t.net_pin_start;
  m.pin.reserve(t.pin_label.size());
  std::vector<std::pair<Pin_label, uint32_t>> named;
  named.reserve(t.pin_label.size());
  for (size_t k = 0; k < t.pin_label.size(); ++k) {
    const auto pin = resolve_label(t.pin_label[k]);
    if (pin == no_index) {
      ++stats_.unresolved_labels;
    }
    m.pin.push_back(pin);
    named.emplace_back(t.pin_label[k], pin);
  }
  for (size_t n = 0; n < t.trees(); ++n) {
    const auto driver_node = t.net_pin_start[n] + t.driver_index[n];
    m.net_driver.push_back(m.pin[driver_node]);
    m.net_root.push_back(t.driver_index[n]);
  }
  build_layout(m, named, t.pin_label, t.trees());
  mapping_topology_ = topology;
  mapping_bound_    = true;
  ++mapping_generation_;
  ++stats_.remaps;
  stats_.map_seconds += since(start);
}

Wire_model Physical_ranker::wire_model_trees(const Wire_trees& trees) {
  const auto  start   = Steady::now();
  const auto& netlist = *bound_->netlist;
  const size_t C      = netlist.types().corners();
  Wire_model wire;
  wire.net_cap_ff.assign(netlist.nets() * C, 0.f);
  wire.sink_delay_ps.assign(netlist.pins() * C, 0.f);
  wire.sink_slew_ps.assign(netlist.pins() * C, 0.f);
  const auto& m = mapping_;
  Solved      solved;
  for (size_t n = 0; n < trees.node_start.size() - 1; ++n) {
    const auto driver = m.net_driver[n];
    if (driver == no_index || netlist.net_of(driver) == no_index) {
      continue;
    }
    const auto pin_first = m.net_start[n], pins = m.net_start[n + 1] - pin_first;
    const auto nodes     = trees.node_start[n + 1] - trees.node_start[n];
    const auto edges_n   = trees.edge_start[n + 1] - trees.edge_start[n];
    if (!solve_arrays(std::span<const float>(trees.node_cap_ff.data() + trees.node_start[n], nodes),
                      std::span<const Wire_edge>(trees.edges.data() + trees.edge_start[n], edges_n),
                      std::span<const uint32_t>(m.pin.data() + pin_first, pins), m.net_root[n], driver, solved)) {
      ++stats_.bad_trees;
      continue;
    }
    const auto engine_net = netlist.net_of(driver);
    for (size_t k = 0; k < C; ++k) {
      wire.net_cap_ff[engine_net * C + k] = solved.cap[k];
    }
    for (size_t i = 0; i < pins; ++i) {
      const auto pin = m.pin[pin_first + i];
      if (pin == no_index || pin == driver) {
        continue;
      }
      for (size_t k = 0; k < C; ++k) {
        wire.sink_delay_ps[static_cast<size_t>(pin) * C + k] = solved.delay[i * C + k];
        wire.sink_slew_ps[static_cast<size_t>(pin) * C + k]  = solved.slew[i * C + k];
      }
    }
  }
  stats_.wire_seconds += since(start);
  return wire;
}

livehd::timing::Feedback Physical_ranker::rank_trees(const std::shared_ptr<hhds::Graph>& graph, const Wire_trees& trees,
                                                     const std::shared_ptr<const physical::Parasitics>& skeleton,
                                                     const std::set<std::string>&                       cuts) {
  if (!graph || !skeleton || !trees.topology) {
    throw std::invalid_argument("fast ranking from trees requires a graph, trees and their skeleton");
  }
  if (engine_) {
    engine_->end_edits();
  }
  added_cells_.clear();
  added_names_.clear();
  rc_.reset();
  if (!bound_ || bound_graph_ != graph.get() || bound_epoch_ != graph->body_epoch()) {
    bind(graph);
  }
  if (!mapping_bound_ || mapping_.source != trees.topology.get()) {
    remap_trees(trees.topology);
    engine_.reset();
  }
  auto combined = cuts;
  combined.insert(skeleton->timing_cuts.begin(), skeleton->timing_cuts.end());
  if (!engine_ || combined != engine_cuts_) {
    build_engine(cuts, *skeleton);
  }
  engine_->set_wire(wire_model_trees(trees));
  const auto engine_start = Steady::now();
  engine_->rank();
  stats_.engine_seconds += since(engine_start);
  return ranking_feedback(skeleton);
}

void Physical_ranker::prepare(const std::shared_ptr<hhds::Graph>& graph, const std::shared_ptr<const physical::Parasitics>& rc,
                              const std::set<std::string>& cuts) {
  if (!graph || !rc) {
    throw std::invalid_argument("fast timing requires a graph and in-memory RC");
  }
  if (engine_) {
    engine_->end_edits();  // what a what-if left pending is discarded before a full evaluation
  }
  added_cells_.clear();
  added_names_.clear();
  rc_ = rc;
  if (!bound_ || bound_graph_ != graph.get() || bound_epoch_ != graph->body_epoch()) {
    bind(graph);
  }
  if (!mapping_valid(*rc)) {
    remap(*rc);
    engine_.reset();
  }
  auto combined = cuts;
  combined.insert(rc->timing_cuts.begin(), rc->timing_cuts.end());
  if (!engine_ || combined != engine_cuts_) {
    build_engine(cuts, *rc);
  }
  engine_->set_wire(wire_model(*rc));
}

livehd::timing::Feedback Physical_ranker::rank(const std::shared_ptr<hhds::Graph>&                  graph,
                                               const std::shared_ptr<const physical::Parasitics>& rc,
                                               const std::set<std::string>&                       cuts) {
  prepare(graph, rc, cuts);
  const auto engine_start = Steady::now();
  engine_->rank();
  stats_.engine_seconds += since(engine_start);
  return ranking_feedback(rc);
}

livehd::timing::Feedback Physical_ranker::ranking_feedback(const std::shared_ptr<const physical::Parasitics>& rc) {
  const auto feedback_start = Steady::now();
  livehd::timing::Feedback result;
  result.ranking_only = true;
  result.time_unit_ps = 1;
  auto& ranking       = result.ranking.emplace();
  ranking.layout      = mapping_.layout;
  ranking.parasitics  = rc;
  ranking.pins.resize(mapping_.layout_pin.size());
  const auto& netlist = *bound_->netlist;
  for (size_t i = 0; i < ranking.pins.size(); ++i) {
    const auto pin = mapping_.layout_pin[i];
    if (pin == no_index) {
      continue;
    }
    const float slack = engine_->slack(pin);
    ranking.pins[i].slack = std::isfinite(slack) ? slack : std::numeric_limits<double>::infinity();
    ranking.pins[i].clock = engine_->clock_network(pin) || netlist.pin_type(pin).clock;
  }
  ++stats_.ranks;
  stats_.feedback_seconds += since(feedback_start);
  return result;
}

}  // namespace livehd::timing::fast

namespace livehd::timing::fast {

livehd::timing::Feedback Physical_ranker::evaluate(const std::shared_ptr<hhds::Graph>&                  graph,
                                                   const std::shared_ptr<const physical::Parasitics>& rc,
                                                   const std::set<std::string>&                       cuts) {
  prepare(graph, rc, cuts);
  const auto engine_start = Steady::now();
  engine_->evaluate();
  stats_.engine_seconds += since(engine_start);

  const auto feedback_start = Steady::now();
  const auto& netlist       = *bound_->netlist;
  const auto& types         = netlist.types();
  const size_t C            = types.corners();
  const auto&  report       = engine_->report();
  livehd::timing::Feedback result;
  result.time_unit_ps = 1;
  const auto finite = [](float v) { return std::isfinite(v); };
  if (report.endpoints != 0 && finite(report.wns_ps)) {
    result.wns = report.wns_ps;
    result.tns = report.tns_ps;
  }
  float hold_wns = std::numeric_limits<float>::infinity();
  float delay    = 0;
  for (const auto pin : engine_->endpoint_pins()) {
    hold_wns = std::min(hold_wns, engine_->hold_slack(pin));
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        if (finite(engine_->arrival(pin, k, t))) {
          delay = std::max(delay, engine_->arrival(pin, k, t));
        }
      }
    }
  }
  if (finite(hold_wns)) {
    result.hold_wns = hold_wns;
  }
  result.delay = delay;
  for (uint32_t cell = 0; cell < netlist.cells(); ++cell) {
    result.area += types.type(netlist.type_of(cell)).area;
  }
  result.violations              = static_cast<unsigned>(report.slew_violations + report.capacitance_violations);
  result.electrical_excess       = report.electrical_excess;
  result.electrical_pin_feedback = true;

  absl::flat_hash_map<uint32_t, std::pair<unsigned, double>> electrical;
  for (const auto& e : engine_->electrical_pins()) {
    electrical.emplace(e.pin, std::pair{unsigned(e.count), double(e.excess)});
  }
  auto& dense = result.dense.emplace();
  dense.index = mapping_.index;
  dense.values.resize(mapping_.layout_pin.size());
  for (size_t i = 0; i < dense.values.size(); ++i) {
    const auto pin = mapping_.layout_pin[i];
    if (pin == no_index) {
      continue;
    }
    auto& value = dense.values[i];
    if (const auto it = electrical.find(pin); it != electrical.end()) {
      value.violations        = it->second.first;
      value.electrical_excess = it->second.second;
    }
    float arrival = -std::numeric_limits<float>::infinity(), slew = arrival;
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        arrival = std::max(arrival, engine_->arrival(pin, k, t));
        if (finite(engine_->arrival(pin, k, t))) {
          slew = std::max(slew, engine_->slew(pin, k, t));
        }
      }
    }
    if (finite(arrival)) {
      value.arrival = arrival;
      value.slew    = slew;
    }
    float required = std::numeric_limits<float>::infinity();
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        if (finite(engine_->arrival(pin, k, t))) {
          required = std::min(required, engine_->required(pin, k, t));
        }
      }
    }
    if (finite(required)) {
      value.required = required;
    }
    if (const auto slack = engine_->slack(pin); finite(slack)) {
      value.slack = slack;
    }
    if (const auto hold = engine_->hold_slack(pin); finite(hold)) {
      value.hold_slack = hold;
    }
    const auto cell  = netlist.cell_of(pin);
    const auto type  = netlist.type_of(cell);
    const auto local = netlist.local_of(pin);
    if (const auto net = netlist.net_of(pin); net != no_index && (netlist.is_driver(pin))) {
      float load = 0;
      for (size_t k = 0; k < C; ++k) {
        load = std::max(load, engine_->net_load_ff(net, k));
      }
      value.load_ff = load;
    }
    if (const float cap = std::max(types.pin_cap(type, local, 0, 0), types.pin_cap(type, local, 0, 1)); cap > 0) {
      value.capacitance_ff = cap;
    }
    value.clock = engine_->clock_network(pin) || netlist.pin_type(pin).clock;
  }
  // The worst path, as labels.
  if (result.wns) {
    absl::flat_hash_map<uint32_t, size_t> label_of;
    for (const auto& point : engine_->worst_path()) {
      if (label_of.empty()) {
        label_of.reserve(mapping_.layout_pin.size());
        for (size_t i = 0; i < mapping_.layout_pin.size(); ++i) {
          label_of.emplace(mapping_.layout_pin[i], i);
        }
      }
      if (const auto it = label_of.find(point.pin); it != label_of.end()) {
        result.path.push_back(mapping_.layout->labels[it->second]);
      }
    }
  }
  ++stats_.evaluations;
  stats_.feedback_seconds += since(feedback_start);
  return result;
}

}  // namespace livehd::timing::fast

namespace livehd::timing::fast {

class Physical_ranker::Whatif_impl final : public livehd::timing::Whatif {
public:
  explicit Whatif_impl(Physical_ranker& owner) : o_(owner) {}

  [[nodiscard]] size_t mark() const override { return engine().mark(); }
  void                 rollback(size_t to) override {
    engine().rollback(to);
    while (!rc_log_.empty() && rc_log_.back().mark >= to) {
      auto& entry = rc_log_.back();
      if (entry.had) {
        edited_rc_[entry.driver] = std::move(entry.net);
      } else {
        edited_rc_.erase(entry.driver);
      }
      rc_log_.pop_back();
    }
    while (!added_log_.empty() && added_log_.back().first >= to) {
      o_.added_names_.erase(o_.added_cells_.at(added_log_.back().second));
      o_.added_cells_.erase(added_log_.back().second);
      added_log_.pop_back();
    }
  }
  void resize(const std::string& instance, const std::string& master) override {
    auto&      e    = engine();
    const auto cell = cell_of(instance);
    e.resize_cell(cell, type_of(master));
    // The wire delay of a net depends on the capacitance of the pins it ends on: solve the nets of the new cell's
    // input pins again.
    const auto& netlist = *o_.bound_->netlist;
    const auto  first   = netlist.first_pin(cell);
    for (uint32_t local = 0; local < o_.types_.type(netlist.type_of(cell)).pin_count; ++local) {
      const auto pin = first + local;
      if (netlist.pin_type(pin).direction == Direction::input && netlist.net_of(pin) != no_index) {
        refresh_wire(netlist.net_of(pin));
      }
    }
  }
  void add_cell(const std::string& instance, const std::string& master) override {
    const auto type = type_of(master);
    const auto at   = engine().mark();
    const auto cell = engine().add_cell(type);
    o_.added_cells_[instance] = cell;
    o_.added_names_[cell]     = instance;
    added_log_.emplace_back(at, instance);
  }
  void set_net(const physical::Rc_net& net) override {
    auto&        e       = engine();
    {
      const auto at = e.mark();
      const auto driver_pin = o_.resolve_label(net.driver);
      auto       it         = edited_rc_.find(driver_pin);
      rc_log_.push_back({at, driver_pin, it != edited_rc_.end(), it != edited_rc_.end() ? it->second : physical::Rc_net{}});
      edited_rc_[driver_pin] = net;
    }
    const auto&  netlist = *o_.bound_->netlist;
    const size_t C       = netlist.types().corners();
    const auto   driver  = o_.resolve_label(net.driver);
    if (driver == no_index) {
      throw std::invalid_argument("what-if net driver is not in the timing netlist: " + net.driver.str());
    }
    std::vector<uint32_t> pins(net.nodes.size(), no_index), sinks;
    uint32_t              root = 0;
    for (uint32_t i = 0; i < net.nodes.size(); ++i) {
      if (net.nodes[i].pin.empty()) {
        continue;
      }
      pins[i] = o_.resolve_label(net.nodes[i].pin);
      if (pins[i] == no_index) {
        throw std::invalid_argument("what-if net pin is not in the timing netlist: " + net.nodes[i].pin.str());
      }
      if (pins[i] == driver) {
        root = i;
      } else {
        sinks.push_back(pins[i]);
      }
    }
    Solved solved;
    if (!o_.solve_net(net, pins, root, driver, solved)) {
      throw std::invalid_argument("what-if net is not an RC tree");
    }
    std::vector<float> delay, slew;
    for (uint32_t i = 0; i < net.nodes.size(); ++i) {
      if (pins[i] == no_index || pins[i] == driver) {
        continue;
      }
      delay.insert(delay.end(), solved.delay.begin() + i * C, solved.delay.begin() + (i + 1) * C);
      slew.insert(slew.end(), solved.slew.begin() + i * C, solved.slew.begin() + (i + 1) * C);
    }
    e.set_net(driver, sinks, solved.cap, delay, slew);
  }

  double wns() override { return finite_or(engine().wns(), 0.0); }
  double tns() override { return engine().tns(); }
  Pin_timing pin(const std::string& label) override {
    Pin_timing out;
    const auto p = o_.resolve_label(Pin_label(label));
    if (p == no_index) {
      return out;
    }
    auto&        e = engine();
    const size_t C = o_.bound_->netlist->types().corners();
    if (const auto slack = e.settled_slack(p); std::isfinite(slack)) {
      out.slack = slack;
    }
    float arrival = -std::numeric_limits<float>::infinity(), slew = arrival, required = std::numeric_limits<float>::infinity();
    for (size_t k = 0; k < C; ++k) {
      for (unsigned t = 0; t < 2; ++t) {
        if (std::isfinite(e.arrival(p, k, t))) {
          arrival  = std::max(arrival, e.arrival(p, k, t));
          slew     = std::max(slew, e.slew(p, k, t));
          required = std::min(required, e.required(p, k, t));
        }
      }
    }
    if (std::isfinite(arrival)) {
      out.arrival = arrival;
      out.slew    = slew;
    }
    if (std::isfinite(required)) {
      out.required = required;
    }
    return out;
  }
  std::optional<double> load(const std::string& driver) override {
    const auto p = o_.resolve_label(Pin_label(driver));
    if (p == no_index) {
      return std::nullopt;
    }
    auto&      e   = engine();
    const auto net = o_.bound_->netlist->net_of(p);
    if (net == no_index) {
      return std::nullopt;
    }
    e.settle();
    float load = 0;
    for (size_t k = 0; k < o_.bound_->netlist->types().corners(); ++k) {
      load = std::max(load, e.net_load_ff(net, k));
    }
    return load;
  }
  std::vector<std::pair<std::string, double>> failing_endpoints(double below_ps) override {
    std::vector<std::pair<std::string, double>> out;
    for (const auto& [pin, slack] : engine().failing_endpoints(static_cast<float>(below_ps))) {
      out.emplace_back(label_of(pin), slack);
    }
    return out;
  }
  std::pair<unsigned, double> electrical(const std::vector<std::string>& labels) override {
    unsigned violations = 0;
    double   excess     = 0;
    for (const auto& label : labels) {
      if (const auto p = o_.resolve_label(Pin_label(label)); p != no_index) {
        const auto e = engine().pin_electrical(p);
        violations  += e.count;
        excess      += e.excess;
      }
    }
    return {violations, excess};
  }
  std::optional<std::pair<double, double>> arc(const std::string& master, const std::string& from, const std::string& to, double slew_ps,
                                               double load_ff) override {
    const auto& types = o_.types_;
    const auto  type  = types.find(master);
    if (type == no_index) {
      return std::nullopt;
    }
    const auto a = types.find_pin(type, from), b = types.find_pin(type, to);
    if (a == no_index || b == no_index) {
      return std::nullopt;
    }
    std::optional<std::pair<double, double>> worst;
    for (const auto& arc : types.arcs(type)) {
      if (arc.kind != Arc_kind::combinational || arc.from != a || arc.to != b) {
        continue;
      }
      for (unsigned out = 0; out < 2; ++out) {
        const auto delay_table = types.table(arc, 0, 0, out), slew_table = types.table(arc, 0, 1, out);
        if (delay_table == no_index) {
          continue;
        }
        const Query query{static_cast<float>(slew_ps), static_cast<float>(load_ff), 0.f, 0.f};
        const double delay = types.lookup(delay_table, query);
        const double slew  = slew_table == no_index ? 0.0 : std::max(0.f, types.lookup(slew_table, query));
        if (!worst) {
          worst = {delay, slew};
        } else {
          worst = {std::max(worst->first, delay), std::max(worst->second, slew)};
        }
      }
    }
    return worst;
  }
  std::optional<double> pin_capacitance(const std::string& master, const std::string& pin) override {
    const auto& types = o_.types_;
    const auto  type  = types.find(master);
    if (type == no_index) {
      return std::nullopt;
    }
    const auto local = types.find_pin(type, pin);
    if (local == no_index) {
      return std::nullopt;
    }
    return std::max(types.pin_cap(type, local, 0, 0), types.pin_cap(type, local, 0, 1));
  }
  double clock_period() override {
    double best = 0;
    for (const auto& clock : o_.constraints_.clocks) {
      best = best == 0 ? clock.period_ps : std::min<double>(best, clock.period_ps);
    }
    return best;
  }

private:
  static double finite_or(float v, double fallback) { return std::isfinite(v) ? v : fallback; }
  Engine&       engine() const {
    if (!o_.engine_) {
      throw std::logic_error("what-if edits need an evaluation first");
    }
    return *o_.engine_;
  }
  uint32_t cell_of(const std::string& instance) const {
    if (const auto it = o_.added_cells_.find(instance); it != o_.added_cells_.end()) {
      return it->second;
    }
    const auto it = o_.cell_index_.find(instance);
    if (it == o_.cell_index_.end()) {
      throw std::invalid_argument("what-if instance is not in the timing netlist: " + instance);
    }
    return it->second;
  }
  uint32_t type_of(const std::string& master) const {
    const auto type = o_.types_.find(master);
    if (type == no_index) {
      throw std::invalid_argument("what-if master has no Liberty model: " + master);
    }
    return type;
  }
  std::string label_of(uint32_t pin) const {
    const auto& netlist = *o_.bound_->netlist;
    const auto  cell    = netlist.cell_of(pin);
    for (const auto& port : netlist.ports()) {
      if (port.cell == cell) {
        return port.name;
      }
    }
    std::string name;
    if (const auto it = o_.added_names_.find(cell); it != o_.added_names_.end()) {
      name = it->second;
    } else {
      name = std::string(o_.bound_->cell_node[cell].get_name());
    }
    return name + ":" + netlist.pin_type(pin).name;
  }

  // Solve the wire of a net again with the pin capacitances it has now, from the RC it was last given.
  void refresh_wire(uint32_t net) {
    const auto& netlist = *o_.bound_->netlist;
    const size_t C      = netlist.types().corners();
    const auto  driver  = netlist.driver_of(net);
    const physical::Rc_net* rc = nullptr;
    std::vector<uint32_t>   pins;
    uint32_t                root = 0;
    if (const auto it = edited_rc_.find(driver); it != edited_rc_.end()) {
      rc = &it->second;
      for (const auto& node : rc->nodes) {
        pins.push_back(node.pin.empty() ? no_index : o_.resolve_label(node.pin));
      }
    } else {
      if (by_driver_generation_ != o_.mapping_generation_) {
        by_driver_.clear();
        for (uint32_t n = 0; n < o_.mapping_.net_driver.size(); ++n) {
          if (o_.mapping_.net_driver[n] != no_index) {
            by_driver_[o_.mapping_.net_driver[n]] = n;
          }
        }
        by_driver_generation_ = o_.mapping_generation_;
      }
      const auto base = by_driver_.find(driver);
      if (base == by_driver_.end() || !o_.rc_) {
        return;
      }
      rc = &o_.rc_->nets[base->second];
      const auto first = o_.mapping_.net_start[base->second];
      pins.assign(o_.mapping_.pin.begin() + first, o_.mapping_.pin.begin() + first + rc->nodes.size());
    }
    for (uint32_t i = 0; i < pins.size(); ++i) {
      if (pins[i] == driver) {
        root = i;
      }
    }
    Solved solved;
    if (!o_.solve_net(*rc, pins, root, driver, solved)) {
      return;
    }
    std::vector<uint32_t> sinks;
    std::vector<float>    delay, slew;
    for (uint32_t i = 0; i < pins.size(); ++i) {
      if (pins[i] == no_index || pins[i] == driver || netlist.net_of(pins[i]) != net) {
        continue;
      }
      sinks.push_back(pins[i]);
      delay.insert(delay.end(), solved.delay.begin() + i * C, solved.delay.begin() + (i + 1) * C);
      slew.insert(slew.end(), solved.slew.begin() + i * C, solved.slew.begin() + (i + 1) * C);
    }
    engine().set_net_wire(net, solved.cap, sinks, delay, slew);
  }

  struct Rc_log {
    size_t             mark;
    uint32_t           driver;
    bool               had;
    physical::Rc_net   net;
  };
  Physical_ranker&                                 o_;
  std::vector<std::pair<size_t, std::string>>      added_log_;
  std::unordered_map<uint32_t, physical::Rc_net>   edited_rc_;  // by driver pin, the RC a what-if set
  std::vector<Rc_log>                              rc_log_;
  std::unordered_map<uint32_t, uint32_t>           by_driver_;  // driver pin -> net of the evaluated RC snapshot
  size_t                                           by_driver_generation_ = ~size_t{0};
};

Physical_ranker::~Physical_ranker() = default;

livehd::timing::Whatif& Physical_ranker::whatif() {
  if (!whatif_) {
    whatif_ = std::make_unique<Whatif_impl>(*this);
  }
  return *whatif_;
}

}  // namespace livehd::timing::fast
