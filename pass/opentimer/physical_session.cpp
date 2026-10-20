// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <format>
#include <stdexcept>

#include "node_util.hpp"
#include "pass_opentimer.hpp"

namespace livehd::opentimer {
struct Physical_session::Impl : Pass_opentimer {
  Statistics                         stats;
  bool                               initialized = false;
  std::map<std::string, std::string> masters, ports, net_names, connections;
  size_t                             next_gate = 0, next_net = 0;

  explicit Impl(const Eprp_var& options) : Pass_opentimer(options) {
    // Reporting/caching is a separate consumer, never the feedback transport.
    qor_path.clear();
    cache_dir_.clear();
    stats_ = false;
    if (critical_) {
      throw std::invalid_argument("physical timing sessions do not annotate synthesis criticality colors");
    }
    if (!spef_file_list.empty()) {
      throw std::invalid_argument("physical timing sessions require in-memory placement RC, not SPEF");
    }
  }

  std::string pin_name(const std::string& physical) const {
    if (auto it = physical_native_pins_.find(physical); it != physical_native_pins_.end()) {
      return it->second;
    }
    if (auto it = ports.find(physical); it != ports.end()) {
      return it->second;
    }
    const auto colon = physical.rfind(':');
    if (colon != std::string::npos) {
      if (auto it = physical_instances_.find(physical.substr(0, colon)); it != physical_instances_.end()) {
        return it->second + physical.substr(colon);
      }
    }
    throw std::runtime_error("physical timing pin has no timing identity: " + physical);
  }

  void initialize(const std::shared_ptr<hhds::Graph>& graph) {
    setup_hier(graph);
    if (hier_mode_) {
      throw std::invalid_argument("physical timing sessions require the flattened placement graph");
    }
    build_circuit(graph);
    for (auto node : graph->body().nodes()) {
      if (!node.get_subnode_io() && graph_util::type_op_of(node) != Ntype_op::Memory) {
        continue;
      }
      physical_instances_.emplace(std::string(node.get_name()),
                                  std::format("{}__n{}", graph_util::default_instance_name(node), node.get_debug_nid()));
    }
    for (const auto& cell : physical_rc_->cells) {
      masters.emplace(cell.name, cell.master);
    }
    const auto add_ports = [&](const auto& declarations) {
      for (const auto& d : declarations) {
        for (unsigned bit = 0; bit < std::max(1u, static_cast<unsigned>(d.bits)); ++bit) {
          ports.emplace(d.bits > 1 ? std::format("{}[{}]", d.name, bit) : d.name, bit ? std::format("{}.{}", d.name, bit) : d.name);
        }
      }
    };
    add_ports(graph->get_io()->get_input_pin_decls());
    add_ports(graph->get_io()->get_output_pin_decls());
    read_sdc_spef();
    apply_case_analysis();
    apply_physical_rc(graph);
    prepare_critical_targets();
    apply_physical_captures();
    timer.update_timing();
    for (const auto& net : physical_rc_->nets) {
      const auto* wire = timer.pins().at(pin_name(net.driver)).net();
      if (!wire) {
        throw std::runtime_error("physical timing driver has no net: " + net.driver);
      }
      net_names.emplace(net.driver, wire->name());
      for (const auto& node : net.nodes) {
        if (!node.pin.empty()) {
          connections.emplace(pin_name(node.pin), wire->name());
        }
      }
    }
    // The persistent timer owns strings and Liberty cells, not graph handles.
    // Annotation maps from the initial scratch graph are not needed here.
    timing_pin_nodes_.clear();
    timing_net_nodes_.clear();
    overwrite_dpin2net.clear();
    ++stats.builds;
  }

  bool same_connectivity() const {
    if (!previous_physical_rc_ || previous_physical_rc_->nets.size() != physical_rc_->nets.size()) {
      return false;
    }
    for (size_t i = 0; i < physical_rc_->nets.size(); ++i) {
      const auto& a = previous_physical_rc_->nets[i];
      const auto& b = physical_rc_->nets[i];
      if (a.driver != b.driver) {
        return false;
      }
      auto x = a.nodes.begin(), y = b.nodes.begin();
      while (true) {
        while (x != a.nodes.end() && x->pin.empty()) {
          ++x;
        }
        while (y != b.nodes.end() && y->pin.empty()) {
          ++y;
        }
        if (x == a.nodes.end() || y == b.nodes.end()) {
          if (x != a.nodes.end() || y != b.nodes.end()) {
            return false;
          }
          break;
        }
        if (x->pin != y->pin) {
          return false;
        }
        ++x;
        ++y;
      }
    }
    return true;
  }

  void update(const std::shared_ptr<hhds::Graph>& graph) {
    std::map<std::string, std::string> next_masters;
    for (const auto& cell : physical_rc_->cells) {
      next_masters.emplace(cell.name, cell.master);
    }
    const auto is_gate
        = [&](const std::string& master) { return timer.celllib(ot::MIN)->cell(master) && timer.celllib(ot::MAX)->cell(master); };
    bool changed = false, shape_changed = false;
    for (const auto& [name, master] : masters) {
      if (!next_masters.contains(name)) {
        if (!is_gate(master)) {
          throw std::runtime_error("cannot remove a native timing boundary: " + name);
        }
        timer.remove_gate(physical_instances_.at(name));
        physical_instances_.erase(name);
        ++stats.removed_gates;
        shape_changed = true;
        changed       = true;
      }
    }
    for (const auto& [name, master] : next_masters) {
      const auto old = masters.find(name);
      if (old != masters.end() && old->second == master) {
        continue;
      }
      if (!is_gate(master)) {
        throw std::runtime_error("new physical cell lacks Liberty timing: " + master);
      }
      if (old == masters.end()) {
        const auto instance       = "__lhd_physical_gate_" + std::to_string(next_gate++);
        physical_instances_[name] = instance;
        timer.insert_gate(instance, master);
        ++stats.inserted_gates;
        shape_changed = true;
      } else {
        if (!is_gate(old->second)) {
          throw std::runtime_error("cannot resize a native timing boundary: " + name);
        }
        for (auto corner : {ot::MIN, ot::MAX}) {
          const auto* a = timer.celllib(corner)->cell(old->second);
          const auto* b = timer.celllib(corner)->cell(master);
          if (a->cellpins.size() != b->cellpins.size()) {
            throw std::runtime_error("resized timing pins differ: " + name);
          }
          for (const auto& [pin, definition] : a->cellpins) {
            const auto* next = b->cellpin(pin);
            if (!next || next->direction != definition.direction) {
              throw std::runtime_error("resized timing pins differ: " + name);
            }
          }
        }
        timer.repower_gate(physical_instances_.at(name), master);
        ++stats.repowered_gates;
      }
      changed = true;
    }
    // Flush gate edits before resolving newly inserted pins. Constraints and
    // unchanged gates remain in this same Timer throughout candidate rollback.
    if (changed) {
      timer.update_timing();
    }
    // Resizing and coordinate-only updates preserve net membership. Avoid
    // reconstructing a million-pin connection map for those common calls.
    if (!shape_changed && same_connectivity()) {
      masters = std::move(next_masters);
      finish_update(graph, changed);
      return;
    }
    std::map<std::string, std::string> next_nets, next_connections;
    for (const auto& net : physical_rc_->nets) {
      auto       old  = net_names.find(net.driver);
      const auto name = old == net_names.end() ? "__lhd_physical_net_" + std::to_string(next_net++) : old->second;
      next_nets.emplace(net.driver, name);
      if (old == net_names.end()) {
        timer.insert_net(name);
      }
      for (const auto& node : net.nodes) {
        if (node.pin.empty()) {
          continue;
        }
        const auto pin            = pin_name(node.pin);
        const auto [it, inserted] = next_connections.emplace(pin, name);
        if (!inserted && it->second != name) {
          throw std::runtime_error("physical pin belongs to multiple nets: " + node.pin);
        }
      }
    }
    for (const auto& [pin, net] : connections) {
      if (!next_connections.contains(pin) && timer.pins().contains(pin)) {
        timer.disconnect_pin(pin);
        changed = true;
      }
    }
    for (const auto& [pin, net] : next_connections) {
      if (auto old = connections.find(pin); old == connections.end() || old->second != net) {
        timer.disconnect_pin(pin).connect_pin(pin, net);
        changed = true;
      }
    }
    for (const auto& [driver, name] : net_names) {
      if (!next_nets.contains(driver)) {
        timer.remove_net(name);
      }
    }
    masters     = std::move(next_masters);
    net_names   = std::move(next_nets);
    connections = std::move(next_connections);
    finish_update(graph, changed);
  }

  void finish_update(const std::shared_ptr<hhds::Graph>& graph, bool changed) {
    if (changed) {
      timer.update_timing();
      apply_case_analysis();
      std::vector<std::string> ideal;
      for (const auto& [name, target] : timing_targets_) {
        if ((!target.ports.empty() || !target.pin.empty()) && !propagated_clocks_.contains(name)) {
          ideal.push_back(name);
        }
      }
      timer.set_ideal_clocks(std::move(ideal), {physical_clock_endpoints_.begin(), physical_clock_endpoints_.end()});
    }
    apply_physical_rc(graph);
  }

  livehd::timing::Feedback evaluate(const std::shared_ptr<hhds::Graph>&                 graph,
                                    std::shared_ptr<const livehd::physical::Parasitics> rc) {
    if (!graph || !rc) {
      throw std::invalid_argument("physical timing requires a graph and in-memory RC");
    }
    const auto start = std::chrono::steady_clock::now();
    physical_rc_     = std::move(rc);
    validate_physical_rc(graph);
    livehd::timing::Feedback result;
    feedback_ = &result;
    try {
      if (!initialized) {
        initialize(graph);
        initialized = true;
      } else {
        update(graph);
      }
      compute_timing(graph);
      previous_physical_rc_  = physical_rc_;
      stats.updated_nets    += physical_updated_nets_;
      ++stats.evaluations;
      stats.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    } catch (...) {
      feedback_ = nullptr;
      throw;
    }
    feedback_ = nullptr;
    return result;
  }
};
Physical_session::Physical_session(const Eprp_var& options) : options_(options) {
  options_.graphs.clear();
  options_.physical_rc.reset();
  impl_ = std::make_unique<Impl>(options_);
}
Physical_session::~Physical_session() = default;
livehd::timing::Feedback Physical_session::evaluate(const std::shared_ptr<hhds::Graph>&                 graph,
                                                    std::shared_ptr<const livehd::physical::Parasitics> rc) {
  try {
    return impl_->evaluate(graph, std::move(rc));
  } catch (...) {
    // Invalid candidate inputs must not poison the next (possibly rolled-back)
    // trial. Normal accepted/rejected trials never recreate this timer.
    const auto previous_stats = impl_->stats;
    impl_                     = std::make_unique<Impl>(options_);
    impl_->stats              = previous_stats;
    throw;
  }
}
const Physical_session::Statistics& Physical_session::statistics() const { return impl_->stats; }
}  // namespace livehd::opentimer
