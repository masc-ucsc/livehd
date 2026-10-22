// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "node_util.hpp"
#include "pass_opentimer.hpp"
#include "timing_whatif.hpp"

namespace livehd::opentimer {
struct Physical_session::Impl : Pass_opentimer {
  Statistics                         stats;
  bool                               initialized = false;
  std::map<std::string, std::string> masters, ports, net_names, connections;
  size_t                             next_gate = 0, next_net = 0;
  std::set<std::string>              data_exclusions;
  bool                               exclusions_dirty = true;

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
      exclusions_dirty = true;
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

  // ---- what-if edits (core/timing_whatif.hpp) -------------------------------
  // Transient master/topology/RC edits on the evaluated state. Every timer
  // change is logged and undone in reverse order; RC restores run last so a
  // restored net only names pins that are back on it. The session maps
  // (masters, net_names, connections) always describe the evaluated state.
  struct Whatif_impl final : livehd::timing::Whatif {
    Impl& s;
    enum class Kind { repower, gate, net, pin, rc };
    struct Undo {
      Kind                                    kind;
      std::string                             a, b;
      std::optional<livehd::physical::Rc_net> rc;
    };
    std::vector<Undo>                                                log;
    std::unordered_map<std::string, std::string>                     conn;  // timer pin -> net ("" = none), overrides s.connections
    std::unordered_map<std::string, std::string>                     nets;  // what-if driver label -> timer net
    std::unordered_map<std::string, std::string>                     gates;    // what-if instance -> master
    std::unordered_map<std::string, std::string>                     resized;  // instance -> current master
    std::unordered_map<std::string, livehd::physical::Rc_net>        rc_now;   // driver label -> current what-if RC
    std::unordered_map<std::string, const livehd::physical::Rc_net*> base;     // evaluated RC by driver
    const livehd::physical::Parasitics*                              base_owner = nullptr;
    size_t                                                           serial     = 0;
    std::unordered_map<std::string, std::string>                     rc_labels;  // timer net -> physical driver
    std::set<std::string>                                            pending_rc;

    explicit Whatif_impl(Impl& owner) : s(owner) {}

    void index_base() {
      if (base_owner == s.previous_physical_rc_.get()) {
        return;
      }
      base.clear();
      rc_labels.clear();
      pending_rc.clear();
      base_owner = s.previous_physical_rc_.get();
      if (base_owner) {
        for (const auto& net : base_owner->nets) {
          base.emplace(net.driver, &net);
          if (const auto name = net_of_driver(net.driver); !name.empty()) {
            rc_labels.emplace(name, net.driver);
          }
        }
      }
    }
    double      unit_ps() { return s.timer.time_unit() ? s.timer.time_unit()->value() * 1e12 : 1000; }
    double      cap_ff() { return s.timer.capacitance_unit() ? s.timer.capacitance_unit()->value() / 1e-15 : 1; }
    std::string timer_pin(const std::string& label) const { return s.pin_name(label); }
    std::string net_of_pin(const std::string& pin) const {
      if (const auto it = conn.find(pin); it != conn.end()) {
        return it->second;
      }
      const auto it = s.connections.find(pin);
      return it == s.connections.end() ? std::string{} : it->second;
    }
    std::string net_of_driver(const std::string& driver) const {
      if (const auto it = nets.find(driver); it != nets.end()) {
        return it->second;
      }
      const auto it = s.net_names.find(driver);
      return it == s.net_names.end() ? std::string{} : it->second;
    }
    spef::Net to_spef(const livehd::physical::Rc_net& net, const std::string& name) const {
      livehd::physical::validate_rc_tree(net);
      spef::Net                mapped(name, 0);
      std::vector<std::string> labels;
      for (size_t i = 0; i < net.nodes.size(); ++i) {
        const auto& node  = net.nodes[i];
        auto        label = node.pin.empty() ? std::format("{}:__lhd_steiner_{}", name, i) : timer_pin(node.pin);
        if (!std::isfinite(node.capacitance_ff) || node.capacitance_ff < 0) {
          throw std::invalid_argument("invalid what-if capacitance");
        }
        mapped.caps.emplace_back(label, "", node.capacitance_ff);
        mapped.lcap += node.capacitance_ff;
        labels.push_back(std::move(label));
      }
      for (const auto& edge : net.edges) {
        if (edge.from >= labels.size() || edge.to >= labels.size() || !std::isfinite(edge.resistance_ohm)
            || edge.resistance_ohm < 0) {
          throw std::invalid_argument("invalid what-if RC edge");
        }
        mapped.ress.emplace_back(labels[edge.from], labels[edge.to], edge.resistance_ohm);
      }
      return mapped;
    }
    void attach(const livehd::physical::Rc_net& net, const std::string& name) {
      spef::Spef data;
      data.capacitance_unit = "1 FF";
      data.resistance_unit  = "1 OHM";
      data.nets.push_back(to_spef(net, name));
      s.timer.read_parasitics(std::move(data));
    }
    [[nodiscard]] size_t mark() const override { return log.size(); }
    void                 require_state() const {
      if (!s.previous_physical_rc_) {
        throw std::logic_error("what-if timing requires a prior physical evaluation");
      }
    }
    std::string master_of(const std::string& instance) const {
      if (const auto it = resized.find(instance); it != resized.end()) {
        return it->second;
      }
      if (const auto it = gates.find(instance); it != gates.end()) {
        return it->second;
      }
      const auto it = s.masters.find(instance);
      if (it == s.masters.end()) {
        throw std::runtime_error("what-if resize of an unknown instance: " + instance);
      }
      return it->second;
    }
    void resize(const std::string& instance, const std::string& master) override {
      require_state();
      const auto old = master_of(instance);
      if (old == master) {
        return;
      }
      if (!s.timer.celllib(ot::MIN)->cell(master) || !s.timer.celllib(ot::MAX)->cell(master)) {
        throw std::runtime_error("what-if master lacks Liberty timing: " + master);
      }
      const auto timer_instance = s.physical_instances_.at(instance);
      s.timer.repower_gate(timer_instance, master);
      log.push_back({Kind::repower, instance, old, {}});
      resized[instance] = master;
    }
    void add_cell(const std::string& instance, const std::string& master) override {
      require_state();
      if (s.physical_instances_.contains(instance) || s.masters.contains(instance)) {
        throw std::runtime_error("what-if instance already exists: " + instance);
      }
      if (!s.timer.celllib(ot::MIN)->cell(master) || !s.timer.celllib(ot::MAX)->cell(master)) {
        throw std::runtime_error("what-if master lacks Liberty timing: " + master);
      }
      s.timer.insert_gate(instance, master);
      s.physical_instances_.emplace(instance, instance);
      gates.emplace(instance, master);
      log.push_back({Kind::gate, instance, master, {}});
    }
    void move_pin(const std::string& pin, const std::string& net) {
      const auto old = net_of_pin(pin);
      if (old == net) {
        return;
      }
      if (!old.empty()) {
        s.timer.disconnect_pin(pin);
      }
      if (!net.empty()) {
        s.timer.connect_pin(pin, net);
      }
      pending_rc.insert(old);
      pending_rc.insert(net);
      conn[pin] = net;
      log.push_back({Kind::pin, pin, old, {}});
    }
    void set_net(const livehd::physical::Rc_net& net) override {
      livehd::physical::validate_rc_tree(net);  // reject before any what-if state changes
      require_state();
      index_base();
      auto name = net_of_driver(net.driver);
      if (name.empty()) {
        name = std::format("__lhd_whatif_net_{}", serial++);
        s.timer.insert_net(name);
        nets.emplace(net.driver, name);
        log.push_back({Kind::net, net.driver, name, {}});
      }
      rc_labels[name] = net.driver;
      for (const auto& node : net.nodes) {
        if (!node.pin.empty()) {
          move_pin(timer_pin(node.pin), name);
        }
      }
      std::optional<livehd::physical::Rc_net> old;
      if (const auto it = rc_now.find(net.driver); it != rc_now.end()) {
        old = it->second;
      } else if (const auto b = base.find(net.driver); b != base.end()) {
        old = *b->second;
      }
      log.push_back({Kind::rc, net.driver, name, std::move(old)});
      rc_now[net.driver] = net;
      pending_rc.insert(name);
    }
    void rollback(size_t to) override {
      if (to > log.size()) {
        throw std::logic_error("what-if rollback past the edit log");
      }
      std::vector<std::pair<std::string, std::optional<livehd::physical::Rc_net>>> restore;  // oldest value last
      while (log.size() > to) {
        auto undo = std::move(log.back());
        log.pop_back();
        switch (undo.kind) {
          case Kind::repower: {
            s.timer.repower_gate(s.physical_instances_.at(undo.a), undo.b);
            if (s.masters.contains(undo.a) && s.masters.at(undo.a) == undo.b) {
              resized.erase(undo.a);
            } else {
              resized[undo.a] = undo.b;
            }
            break;
          }
          case Kind::pin: {
            pending_rc.insert(net_of_pin(undo.a));
            pending_rc.insert(undo.b);
            if (!net_of_pin(undo.a).empty()) {
              s.timer.disconnect_pin(undo.a);
            }
            if (!undo.b.empty()) {
              s.timer.connect_pin(undo.a, undo.b);
            }
            const auto evaluated = s.connections.find(undo.a);
            if ((evaluated == s.connections.end() && undo.b.empty())
                || (evaluated != s.connections.end() && evaluated->second == undo.b)) {
              conn.erase(undo.a);
            } else {
              conn[undo.a] = undo.b;
            }
            break;
          }
          case Kind::net: {
            s.timer.remove_net(undo.b);
            pending_rc.erase(undo.b);
            rc_labels.erase(undo.b);
            nets.erase(undo.a);
            break;
          }
          case Kind::gate: {
            s.timer.remove_gate(undo.a);
            s.physical_instances_.erase(undo.a);
            gates.erase(undo.a);
            resized.erase(undo.a);
            break;
          }
          case Kind::rc: {
            restore.emplace_back(undo.a, std::move(undo.rc));
            break;
          }
        }
      }
      // Reverse pop order: the last entry holds the value from before all undone edits.
      std::unordered_map<std::string, std::optional<livehd::physical::Rc_net>> final_rc;
      for (auto& [driver, rc] : restore) {
        final_rc[driver] = std::move(rc);
      }
      index_base();
      for (auto& [driver, rc] : final_rc) {
        const auto name = net_of_driver(driver);
        if (!rc || name.empty()) {
          rc_now.erase(driver);
          pending_rc.erase(name);
          continue;
        }
        const auto b = base.find(driver);
        if (b != base.end() && *b->second == *rc) {
          rc_now.erase(driver);
        } else {
          rc_now[driver] = *rc;
        }
        pending_rc.insert(name);
      }
    }
    void update() {
      index_base();
      // A later set_net can move a sink out of an earlier net and invalidate
      // that net's RC. Attach touched trees only after all topology edits,
      // including rollback, have been queued; untouched nets remain incremental.
      for (const auto& name : pending_rc) {
        const auto label = rc_labels.find(name);
        if (label == rc_labels.end()) {
          continue;
        }
        if (const auto it = rc_now.find(label->second); it != rc_now.end()) {
          attach(it->second, name);
        } else if (const auto base_it = base.find(label->second); base_it != base.end()) {
          attach(*base_it->second, name);
        }
      }
      pending_rc.clear();
      s.timer.update_timing();
    }
    double wns() override {
      update();
      const auto value = s.timer.report_wns(ot::MAX);
      return value && std::isfinite(*value) ? *value * unit_ps() : 0;
    }
    double tns() override {
      update();
      std::unordered_map<std::string_view, float> worst;
      const auto                                  add = [&](std::string_view pin, std::optional<float> slack) {
        if (!slack || !std::isfinite(*slack)) {
          return;
        }
        auto [it, inserted] = worst.emplace(pin, *slack);
        if (!inserted) {
          it->second = std::min(it->second, *slack);
        }
      };
      for (const auto& [name, output] : s.timer.primary_outputs()) {
        for (auto tran : {ot::RISE, ot::FALL}) {
          add(name, output.slack(ot::MAX, tran));
        }
      }
      for (const auto& test : s.timer.tests()) {
        for (auto tran : {ot::RISE, ot::FALL}) {
          add(test.constrained_pin().name(), test.slack(ot::MAX, tran));
        }
      }
      double total = 0;
      for (const auto& [pin, slack] : worst) {
        total += std::min(0.0f, slack);
      }
      return total * unit_ps();
    }
    std::vector<std::pair<std::string, double>> failing_endpoints(double below_ps) override {
      update();
      const double                                scale = unit_ps();
      std::unordered_map<std::string_view, float> worst;
      const auto                                  add = [&](std::string_view pin, std::optional<float> slack) {
        if (!slack || !std::isfinite(*slack)) {
          return;
        }
        auto [it, inserted] = worst.emplace(pin, *slack);
        if (!inserted) {
          it->second = std::min(it->second, *slack);
        }
      };
      for (const auto& [name, output] : s.timer.primary_outputs()) {
        for (auto tran : {ot::RISE, ot::FALL}) {
          add(name, output.slack(ot::MAX, tran));
        }
      }
      for (const auto& test : s.timer.tests()) {
        for (auto tran : {ot::RISE, ot::FALL}) {
          add(test.constrained_pin().name(), test.slack(ot::MAX, tran));
        }
      }
      if (reverse_pins.empty()) {
        for (const auto& [physical, timer] : s.physical_timing_pins_) {
          reverse_pins.emplace(timer, physical);
        }
      }
      std::vector<std::pair<std::string, double>> result;
      for (const auto& [pin, slack] : worst) {
        if (slack * scale >= below_ps) {
          continue;
        }
        const auto physical = reverse_pins.find(std::string(pin));
        result.emplace_back(physical == reverse_pins.end() ? std::string(pin) : physical->second, slack * scale);
      }
      std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::tie(a.second, a.first) < std::tie(b.second, b.first);
      });
      return result;
    }
    std::unordered_map<std::string, std::string> reverse_pins;
    double                                       clock_period() override {
      update();
      double best = 0;
      for (const auto& [name, clock] : s.timer.clocks()) {
        if (clock.period() > 0 && (best == 0 || clock.period() < best)) {
          best = clock.period();
        }
      }
      return best * unit_ps();
    }
    std::pair<unsigned, double> electrical(const std::vector<std::string>& labels) override {
      update();
      unsigned violations = 0;
      double   excess     = 0;
      for (const auto& label : labels) {
        const auto it = s.timer.pins().find(timer_pin(label));
        if (it == s.timer.pins().end() || it->second.is_ideal_clock()) {
          continue;
        }
        const auto& pin = it->second;
        for (const auto split : {ot::MIN, ot::MAX}) {
          const auto* cp = pin.cellpin(split);
          if (!cp) {
            continue;
          }
          const auto check = [&](double actual, const std::optional<float>& limit) {
            if (limit && *limit > 0 && actual > *limit * (1 + 1e-6)) {
              ++violations;
              excess += actual / *limit - 1;
            }
          };
          for (const auto tran : {ot::RISE, ot::FALL}) {
            if (const auto slew = pin.slew(split, tran)) {
              check(*slew, cp->max_transition);
            }
            if (pin.is_output() && pin.net()) {
              if (const auto value = s.timer.report_load(pin.net()->name(), split, tran)) {
                check(*value, cp->max_capacitance);
              }
            }
          }
          if (pin.is_output() && pin.net()) {
            check((pin.net()->num_pins() - 1), cp->max_fanout);
          }
        }
      }
      return {violations, excess};
    }
    Pin_timing pin(const std::string& label) override {
      update();
      Pin_timing result;
      const auto it = s.timer.pins().find(timer_pin(label));
      if (it == s.timer.pins().end()) {
        return result;
      }
      const double scale   = unit_ps();
      const auto   extreme = [&](auto getter, bool largest) -> std::optional<double> {
        std::optional<double> best;
        for (const auto tran : {ot::RISE, ot::FALL}) {
          if (const auto v = getter(tran); v && std::isfinite(*v)) {
            best = best ? (largest ? std::max(*best, double(*v)) : std::min(*best, double(*v))) : double(*v);
          }
        }
        return best ? std::optional<double>(*best * scale) : std::nullopt;
      };
      const auto& p   = it->second;
      result.slack    = extreme([&](ot::Tran t) { return p.slack(ot::MAX, t); }, false);
      result.arrival  = extreme([&](ot::Tran t) { return p.at(ot::MAX, t); }, true);
      result.required = extreme([&](ot::Tran t) { return p.rat(ot::MAX, t); }, false);
      result.slew     = extreme([&](ot::Tran t) { return p.slew(ot::MAX, t); }, true);
      return result;
    }
    std::optional<double> load(const std::string& driver) override {
      update();
      const auto name = net_of_driver(driver);
      const auto it   = s.timer.nets().find(name);
      if (name.empty() || it == s.timer.nets().end()) {
        return std::nullopt;
      }
      return std::max(it->second.load(ot::MAX, ot::RISE), it->second.load(ot::MAX, ot::FALL)) * cap_ff();
    }
    std::optional<std::pair<double, double>> arc(const std::string& master, const std::string& from, const std::string& to,
                                                 double slew_ps, double load_ff) override {
      const auto* cell = s.timer.celllib(ot::MAX) ? s.timer.celllib(ot::MAX)->cell(master) : nullptr;
      const auto* pin  = cell ? cell->cellpin(to) : nullptr;
      if (!pin) {
        return std::nullopt;
      }
      const double                             scale = unit_ps();
      std::optional<std::pair<double, double>> worst;
      for (const auto& timing : pin->timings) {
        if (timing.related_pin != from || timing.is_constraint()) {
          continue;
        }
        for (auto irf : {ot::RISE, ot::FALL}) {
          for (auto orf : {ot::RISE, ot::FALL}) {
            const auto d = timing.delay(irf, orf, slew_ps / scale, load_ff / cap_ff());
            const auto t = timing.slew(irf, orf, slew_ps / scale, load_ff / cap_ff());
            if (!d || !std::isfinite(*d)) {
              continue;
            }
            const double delay = *d * scale, out = t && std::isfinite(*t) ? *t * scale : 0;
            if (!worst) {
              worst = {delay, out};
            } else {
              worst = {std::max(worst->first, delay), std::max(worst->second, out)};
            }
          }
        }
      }
      return worst;
    }
    std::optional<double> pin_capacitance(const std::string& master, const std::string& name) override {
      const auto* cell = s.timer.celllib(ot::MAX) ? s.timer.celllib(ot::MAX)->cell(master) : nullptr;
      const auto* pin  = cell ? cell->cellpin(name) : nullptr;
      if (!pin || !pin->capacitance) {
        return std::nullopt;
      }
      return *pin->capacitance * cap_ff();
    }
  };
  std::unique_ptr<Whatif_impl> whatif_;
  Whatif_impl&                 whatif() {
    if (!whatif_) {
      whatif_ = std::make_unique<Whatif_impl>(*this);
    }
    return *whatif_;
  }
  void discard_whatif() {
    if (whatif_ && whatif_->mark() > 0) {
      whatif_->rollback(0);
    }
    if (whatif_) {
      whatif_->reverse_pins.clear();
    }
  }

  void apply_data_exclusions(const std::set<std::string>& exclusions) {
    if (!exclusions_dirty && exclusions == data_exclusions) {
      return;
    }
    if (!exclusions_dirty) {
      // Restore the SDC/Liberty masks before replacing our private subset.
      apply_case_analysis();
    }
    timer.update_timing();
    std::unordered_set<std::string> roots;
    for (const auto& net : physical_rc_->nets) {
      if (exclusions.contains(net.driver)) {
        roots.insert(pin_name(net.driver));
      }
    }
    std::vector<std::pair<size_t, uint8_t>> masks;
    for (const auto& arc : timer.arcs()) {
      if (arc.is_net_arc() && roots.contains(arc.from().name())) {
        masks.emplace_back(arc.idx(), 0);
      }
    }
    if (!masks.empty()) {
      timer.refine_case_masks(std::move(masks));
    }
    data_exclusions  = exclusions;
    exclusions_dirty = false;
  }

  livehd::timing::Feedback evaluate(const std::shared_ptr<hhds::Graph>&                 graph,
                                    std::shared_ptr<const livehd::physical::Parasitics> rc,
                                    const std::set<std::string>&                        exclusions) {
    if (!graph || !rc) {
      throw std::invalid_argument("physical timing requires a graph and in-memory RC");
    }
    discard_whatif();
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
      apply_data_exclusions(exclusions);
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
// Forwards to the current Impl: a failed evaluation rebuilds Impl (and drops its edits).
struct Physical_session::Proxy final : livehd::timing::Whatif {
  Physical_session& owner;
  explicit Proxy(Physical_session& o) : owner(o) {}
  Impl::Whatif_impl&   w() { return owner.impl_->whatif(); }
  [[nodiscard]] size_t mark() const override { return owner.impl_->whatif().mark(); }
  void                 rollback(size_t m) override { w().rollback(m); }
  void                 resize(const std::string& instance, const std::string& master) override { w().resize(instance, master); }
  void                 add_cell(const std::string& instance, const std::string& master) override { w().add_cell(instance, master); }
  void                 set_net(const livehd::physical::Rc_net& net) override { w().set_net(net); }
  double               wns() override { return w().wns(); }
  double               tns() override { return w().tns(); }
  Pin_timing           pin(const std::string& label) override { return w().pin(label); }
  std::optional<double>                       load(const std::string& driver) override { return w().load(driver); }
  std::vector<std::pair<std::string, double>> failing_endpoints(double below_ps) override {
    return w().failing_endpoints(below_ps);
  }
  std::optional<std::pair<double, double>> arc(const std::string& master, const std::string& from, const std::string& to,
                                               double slew_ps, double load_ff) override {
    return w().arc(master, from, to, slew_ps, load_ff);
  }
  std::optional<double> pin_capacitance(const std::string& master, const std::string& pin) override {
    return w().pin_capacitance(master, pin);
  }
  std::pair<unsigned, double> electrical(const std::vector<std::string>& labels) override { return w().electrical(labels); }
  double                      clock_period() override { return w().clock_period(); }
};
Physical_session::Physical_session(const Eprp_var& options) : options_(options) {
  proxy_ = std::make_unique<Proxy>(*this);
  options_.graphs.clear();
  options_.physical_rc.reset();
  impl_ = std::make_unique<Impl>(options_);
}
Physical_session::~Physical_session() = default;
livehd::timing::Feedback Physical_session::evaluate(const std::shared_ptr<hhds::Graph>&                 graph,
                                                    std::shared_ptr<const livehd::physical::Parasitics> rc,
                                                    const std::set<std::string>&                        data_exclusions) {
  try {
    return impl_->evaluate(graph, std::move(rc), data_exclusions);
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
livehd::timing::Whatif&             Physical_session::whatif() { return *proxy_; }
}  // namespace livehd::opentimer
