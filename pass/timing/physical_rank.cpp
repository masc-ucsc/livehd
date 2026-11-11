// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_rank.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

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
  if (!mapping_bound_ || mapping_.net_driver.size() != rc.nets.size()) {
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
  layout->net_offsets.reserve(rc.nets.size() + 1);
  for (size_t n = 0; n < rc.nets.size(); ++n) {
    layout->net_offsets.push_back(layout->net_pins.size());
    for (uint32_t k = m.net_start[n]; k < m.net_start[n + 1]; ++k) {
      if (!m.labels[k].empty()) {
        layout->net_pins.push_back(layout->find(m.labels[k]));
      }
    }
  }
  layout->net_offsets.push_back(layout->net_pins.size());
  m.layout       = std::move(layout);
  mapping_bound_ = true;
  ++stats_.remaps;
  stats_.map_seconds += since(start);
}

Wire_model Physical_ranker::wire_model(const physical::Parasitics& rc) {
  const auto  start   = Steady::now();
  const auto& netlist = *bound_->netlist;
  const auto& types   = netlist.types();
  const size_t C      = types.corners();
  Wire_model wire;
  wire.net_cap_ff.assign(netlist.nets() * C, 0.f);
  wire.sink_delay_ps.assign(netlist.pins() * C, 0.f);
  wire.sink_slew_ps.assign(netlist.pins() * C, 0.f);
  std::vector<float>     cap;
  std::vector<Wire_edge> edges;
  const auto&            m = mapping_;
  for (size_t n = 0; n < rc.nets.size(); ++n) {
    const auto  driver = m.net_driver[n];
    const auto& net    = rc.nets[n];
    if (driver == no_index || netlist.net_of(driver) == no_index || net.nodes.empty()) {
      continue;
    }
    const auto  engine_net = netlist.net_of(driver);
    const auto  first      = m.net_start[n];
    const auto  count      = net.nodes.size();
    edges.clear();
    for (const auto& e : net.edges) {
      edges.push_back({e.from, e.to, static_cast<float>(e.resistance_ohm)});
    }
    for (size_t k = 0; k < C; ++k) {
      cap.assign(count, 0.f);
      float sink_pins = 0;
      for (size_t i = 0; i < count; ++i) {
        float c = static_cast<float>(net.nodes[i].capacitance_ff);
        if (const auto pin = m.pin[first + i]; pin != no_index) {
          const auto  cell  = netlist.cell_of(pin);
          const float input = netlist.is_driver(pin)
                                  ? 0.f
                                  : std::max(types.pin_cap(netlist.type_of(cell), netlist.local_of(pin), k, 0),
                                             types.pin_cap(netlist.type_of(cell), netlist.local_of(pin), k, 1));
          c         += input;
          sink_pins += input;
        }
        cap[i] = c;
      }
      if (!solver_.solve(cap, edges, m.net_root[n])) {
        ++stats_.bad_trees;
        break;
      }
      wire.net_cap_ff[engine_net * C + k] = std::max(0.f, solver_.total_cap_ff() - sink_pins);
      for (size_t i = 0; i < count; ++i) {
        const auto pin = m.pin[first + i];
        if (pin == no_index || pin == driver) {
          continue;
        }
        wire.sink_delay_ps[static_cast<size_t>(pin) * C + k] = solver_.delay_ps(static_cast<uint32_t>(i));
        wire.sink_slew_ps[static_cast<size_t>(pin) * C + k]  = std::sqrt(solver_.impulse_ps2(static_cast<uint32_t>(i)));
      }
    }
  }
  stats_.wire_seconds += since(start);
  return wire;
}

livehd::timing::Feedback Physical_ranker::rank(const std::shared_ptr<hhds::Graph>&                  graph,
                                               const std::shared_ptr<const physical::Parasitics>& rc,
                                               const std::set<std::string>&                       cuts) {
  if (!graph || !rc) {
    throw std::invalid_argument("fast ranking requires a graph and in-memory RC");
  }
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
  auto wire = wire_model(*rc);
  const auto engine_start = Steady::now();
  engine_->set_wire(std::move(wire));
  engine_->rank();
  stats_.engine_seconds += since(engine_start);

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
