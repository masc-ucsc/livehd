// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_census.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <tuple>

#include "hhds/attrs/srcid.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/timing/bind_graph.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::pcrs {
namespace {
namespace fast = timing::fast;
using fast::no_index;
using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

struct Sink {
  uint32_t local;
  unsigned edges;
  double   cap_ff;
  bool     has_cap;
};
struct Master {
  enum class Kind { logic, flop, latch, memory, gate } kind = Kind::logic;
  std::vector<Sink>                             sinks;
  // For every output that only distributes the clock, the input and polarity.
  std::map<uint32_t, std::pair<uint32_t, bool>> links;
  std::map<uint32_t, bool>                      ties;
  std::map<uint32_t, std::vector<uint32_t>>     positive_and;
  std::string                                   gate_style;
  uint32_t                                      enable = no_index, test = no_index;
};

// Recognize a single Liberty literal, including parenthesized forms. All other
// functions are boundaries. In particular an AND or mux is never transparent.
std::pair<std::string, bool> literal(std::string expr) {
  std::erase_if(expr, [](unsigned char c) { return std::isspace(c); });
  bool inverted = false;
  while (!expr.empty()) {
    if (expr.front() == '!' || expr.front() == '~') {
      expr.erase(0, 1);
      inverted = !inverted;
    } else if (expr.back() == '\'') {
      expr.pop_back();
      inverted = !inverted;
    } else if (expr.front() == '(' && expr.back() == ')') {
      expr = expr.substr(1, expr.size() - 2);
    } else {
      break;
    }
  }
  return {expr, inverted};
}

std::vector<Master> classify(const fast::Cell_types& types, const liberty::timing::Library& lib,
                             const liberty::timing::Clock_structures& clocks_by_master) {
  std::vector<Master> result(types.types());
  for (const auto& [name, cell] : lib.cells) {
    const auto type       = types.find(name);
    auto&      role       = result.at(type);
    role.kind             = cell.clock_gate   ? Master::Kind::gate
                            : cell.memory     ? Master::Kind::memory
                            : cell.latch      ? Master::Kind::latch
                            : cell.sequential ? Master::Kind::flop
                                              : Master::Kind::logic;
    const auto& structure = clocks_by_master.at(name);
    role.gate_style       = structure.gate_style;
    std::map<std::string, unsigned> edges;
    for (const auto& expression : structure.state_clocks) {
      const auto [pin, inv]  = literal(expression);
      edges[pin]            |= inv ? 2u : 1u;
    }
    // Macros may only describe their clock through timing arcs.
    for (const auto& [pin_name, pin] : cell.pins) {
      for (const auto& arc : pin.arcs) {
        if (arc.type == "rising_edge" || arc.type == "falling_edge") {
          edges[arc.related_pin] |= arc.type == "rising_edge" ? 1u : 2u;
        }
      }
    }
    std::vector<uint32_t> clocks, enables, tests;
    liberty::Comb_cell    comb;
    for (const auto& [pin_name, pin] : cell.pins) {
      if (pin.direction != "input") {
        continue;
      }
      comb.inputs.push_back(pin_name);
      const auto local = types.find_pin(type, pin_name);
      if (std::ranges::contains(structure.gate_enables, pin_name)) {
        enables.push_back(local);
      }
      if (std::ranges::contains(structure.gate_tests, pin_name)) {
        tests.push_back(local);
      }
      const auto it = edges.find(pin_name);
      if (!pin.clock && it == edges.end()) {
        continue;
      }
      clocks.push_back(local);
      if (role.kind == Master::Kind::logic || role.kind == Master::Kind::gate) {
        continue;
      }
      role.sinks.push_back({local,
                            it == edges.end() ? 0u : it->second,
                            std::max(pin.capacitance_ff[0].value_or(0), pin.capacitance_ff[1].value_or(0)),
                            pin.capacitance_ff[0].has_value() || pin.capacitance_ff[1].has_value()});
    }
    for (const auto& [pin_name, pin] : cell.pins) {
      if (pin.direction != "output" || pin.three_state) {
        continue;
      }
      const auto local = types.find_pin(type, pin_name);
      if (role.kind == Master::Kind::gate && std::ranges::contains(structure.gate_outputs, pin_name) && clocks.size() == 1
          && enables.size() == 1 && tests.size() <= 1) {
        role.links[local] = {clocks.front(), false};
        role.enable       = enables.front();
        if (!tests.empty()) {
          role.test = tests.front();
        }
      }
      if (role.kind == Master::Kind::logic) {
        const auto [input, inv] = literal(pin.function);
        if (input == "0" || input == "1") {
          role.ties[local] = (input == "1") != inv;
        }
        if (comb.inputs.size() >= 2) {
          comb.function   = pin.function;
          const auto form = liberty::clock_gate_form(comb);
          if (form && form->active_output && std::ranges::all_of(form->levels, [](bool high) { return high; })) {
            for (const auto& name_in : comb.inputs) {
              role.positive_and[local].push_back(types.find_pin(type, name_in));
            }
          }
        }
        if (const auto it = cell.pins.find(input); it != cell.pins.end() && it->second.direction == "input") {
          role.links[local] = {types.find_pin(type, input), inv};
        }
      }
    }
  }
  return result;
}

struct Path {
  uint32_t root = no_index, function = 0, depth = 0;
  bool     inverted                                 = false;
  enum class Stop { unbound, zero, one, loop } stop = Stop::unbound;
};
struct Load {
  uint32_t sinks = 0, flops = 0, latches = 0, memories = 0, cap_missing = 0, depth = 0, witness = no_index;
  double   cap = 0;

  void add(const Sink& sink, Master::Kind kind, Path path, uint32_t pin) {
    ++sinks;
    flops       += kind == Master::Kind::flop;
    latches     += kind == Master::Kind::latch;
    memories    += kind == Master::Kind::memory;
    cap         += sink.cap_ff;
    cap_missing += !sink.has_cap;
    depth        = std::max(depth, path.depth);

    if (witness == no_index) {
      witness = pin;
    }
  }
};

using Signal = std::pair<std::string, uint32_t>;  // wire / literal / unresolved sink
struct Function {
  uint32_t            parent = 0, root = no_index;
  bool                inverted = false;
  std::string         style;
  std::vector<Signal> terms;
  Signal              test;
  auto                operator<=>(const Function&) const = default;
};
struct Gate_group {
  Function           key;
  Signal             enable;
  std::set<uint32_t> gates;
  Load               load;
};

class Census {
  const fast::Bound_graph&                   bound;
  const fast::Netlist&                       net;
  const std::vector<Master>&                 masters;
  const absl::flat_hash_map<uint32_t, bool>& constants;
  std::map<uint32_t, Path>                   paths;
  std::map<Function, uint32_t>               ids;
  std::map<uint32_t, Gate_group>             groups;
  std::map<uint32_t, Load>                   domains;
  std::map<uint32_t, std::array<Load, 2>>    edge_load;
  std::map<uint32_t, Load>                   latch_load;
  std::map<Path::Stop, Load>                 stops;
  std::map<uint32_t, std::string>            ports;
  uint32_t                                   flops = 0, unresolved = 0, unclassified_state = 0;

  uint32_t driver(uint32_t pin) const {
    const auto n = net.net_of(pin);
    return n == no_index ? no_index : net.driver_of(n);
  }
  Signal signal(uint32_t cell, uint32_t local) const {
    if (local == no_index) {
      return {"absent", 0};
    }
    const auto pin = net.pin(cell, local);
    if (const auto d = driver(pin); d != no_index) {
      const auto& ties = masters[net.type_of(net.cell_of(d))].ties;
      if (const auto it = ties.find(net.local_of(d)); it != ties.end()) {
        return {"literal", it->second};
      }
      return {"wire", d};
    }
    if (const auto it = constants.find(pin); it != constants.end()) {
      return {"literal", it->second};
    }
    return {"unresolved", pin};
  }
  // An enable AND of up to six independent positive inputs is recognized from
  // its full Liberty truth table. Other logic retains its actual output identity.
  std::vector<Signal> terms(const Signal& en) const {
    if (en.first != "wire") {
      return {en};
    }
    const auto  pin = en.second, cell = net.cell_of(pin);
    const auto& products = masters[net.type_of(cell)].positive_and;
    const auto  product  = products.find(net.local_of(pin));
    if (product == products.end()) {
      return {en};
    }
    std::set<Signal> inputs;
    for (auto local : product->second) {
      const auto sig = signal(cell, local);
      if (sig.first == "unresolved") {
        return {en};
      }
      if (sig.first != "literal") {
        inputs.insert(sig);
      } else if (!sig.second) {
        return {sig};
      }
    }
    if (inputs.empty()) {
      return {
          {"literal", 1}
      };
    }
    return {inputs.begin(), inputs.end()};
  }
  Path trace(uint32_t input) {
    auto                  output = driver(input);
    std::vector<uint32_t> chain;
    std::set<uint32_t>    seen;
    Path                  path;
    while (output != no_index) {
      if (const auto it = paths.find(output); it != paths.end()) {
        path = it->second;
        break;
      }
      const auto  cell = net.cell_of(output);
      const auto& role = masters[net.type_of(cell)];
      const auto  it   = role.links.find(net.local_of(output));
      if (net.has_published_clock() && !net.published_clock_driver(output)) {
        path.root = output;
        paths.emplace(output, path);
        break;
      }
      const auto discrete = net.discrete_clock_gates().find(output);
      if (it == role.links.end() && discrete == net.discrete_clock_gates().end()) {
        const auto tie = role.ties.find(net.local_of(output));
        if (tie == role.ties.end()) {
          path.root = output;
        } else {
          path.stop = tie->second ? Path::Stop::one : Path::Stop::zero;
        }
        paths.emplace(output, path);
        break;
      }
      if (!seen.insert(output).second) {
        path.stop = Path::Stop::loop;
        break;
      }
      chain.push_back(output);
      input  = discrete != net.discrete_clock_gates().end() ? discrete->second.clock : net.pin(cell, it->second.first);
      output = driver(input);
    }
    if (output == no_index) {
      if (auto lit = constants.find(input); lit != constants.end()) {
        path.stop = lit->second ? Path::Stop::one : Path::Stop::zero;
      }
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      const auto  pin = *it, cell = net.cell_of(pin);
      const auto& role  = masters[net.type_of(cell)];
      const auto  found = net.discrete_clock_gates().find(pin);
      const auto* gate  = found == net.discrete_clock_gates().end() ? nullptr : &found->second;
      if (path.root != no_index && (gate || role.kind == Master::Kind::gate)) {
        const auto en = gate ? signal(net.cell_of(gate->latch_data), net.local_of(gate->latch_data)) : signal(cell, role.enable);
        Function   key{path.function, path.root, path.inverted, role.gate_style, terms(en), signal(cell, role.test)};
        if (gate) {
          const auto& shape = net.types().type(net.type_of(cell)).clock_shape;
          key.style         = shape.polarity != gate->inverted ? "discrete_and" : "discrete_or";
          if (net.types().type(net.type_of(net.cell_of(gate->latch_data))).clock_shape.kind
              == liberty::Clock_cell_kind::flip_flop) {
            key.style += "_edge_enable";
          }
          if (!gate->enabled_high) {
            key.style += "_low_enable";
            key.terms  = {en};
          }
          for (size_t i = 0; i < shape.inputs.size(); ++i) {
            const auto input_pin = net.pin(cell, shape.inputs[i]);
            if (input_pin == gate->clock || input_pin == gate->control) {
              continue;
            }
            auto term  = signal(cell, shape.inputs[i]);
            // Distinguish unlatched inputs from the data sampled by the latch.
            term.first = (shape.levels[i] ? "positive_" : "negative_") + term.first;
            key.terms.push_back(std::move(term));
          }
          std::ranges::sort(key.terms);
        }
        const auto [id, inserted] = ids.try_emplace(key, ids.size() + 1);
        path.function             = id->second;
        auto& group               = groups[path.function];
        if (inserted) {
          group.key    = std::move(key);
          group.enable = en;
        }
        group.gates.insert(pin);
        ++path.depth;
      }
      path.inverted ^= gate ? gate->inverted : role.links.at(net.local_of(pin)).second;
      paths.emplace(pin, path);
    }
    return path;
  }
  std::string label(uint32_t pin) const {
    const auto cell = net.cell_of(pin);
    if (const auto it = ports.find(cell); it != ports.end()) {
      return it->second;
    }
    return std::format("{}:{}", bound.name(cell), net.pin_type(pin).name);
  }
  std::string source(uint32_t pin) const {
    const auto& node = bound.cell_node[net.cell_of(pin)];
    if (node.is_invalid()) {
      return {};
    }
    const auto id = node.attr(hhds::attrs::srcid);
    if (!id.has() || !id.get()) {
      return {};
    }
    const auto span = node.get_graph()->source_locator().resolve_spans(static_cast<hhds::SourceId>(id.get())).primary;
    if (span.is_null()) {
      return {};
    }
    return span.start_line ? std::format("{}:{}", span.file, *span.start_line) : std::string(span.file);
  }
  void add(Load& load, const Sink& sink, Master::Kind kind, Path path, uint32_t pin) {
    if (load.witness != no_index) {
      const auto has_source = [&](uint32_t p) {
        const auto id = bound.cell_node[net.cell_of(p)].attr(hhds::attrs::srcid);
        return id.has() && id.get();
      };
      if (!has_source(load.witness) && has_source(pin)) {
        load.witness = pin;
      }
    }
    load.add(sink, kind, path, pin);
  }
  static void load_fields(Writer& w, const Load& load) {
    w.Key("clock_pins");
    w.Uint(load.sinks);
    w.Key("flop_clock_pins");
    w.Uint(load.flops);
    w.Key("latch_clock_pins");
    w.Uint(load.latches);
    w.Key("memory_clock_pins");
    w.Uint(load.memories);
    w.Key("clock_pin_cap_ff");
    w.Double(load.cap);
    w.Key("pins_missing_capacitance");
    w.Uint(load.cap_missing);
  }
  void write_signal(Writer& w, const Signal& sig) const {
    w.StartObject();
    w.Key("kind");
    w.String(sig.first.c_str());
    if (sig.first == "literal" || sig.first.ends_with("_literal")) {
      w.Key("value");
      w.Bool(sig.second);
    } else if (sig.first != "absent") {
      w.Key("pin");
      w.String(label(sig.second).c_str());
      if (const auto location = source(sig.second); !location.empty()) {
        w.Key("source");
        w.String(location.c_str());
      }
    }
    w.EndObject();
  }

public:
  Census(const fast::Bound_graph& graph, const std::vector<Master>& roles, const absl::flat_hash_map<uint32_t, bool>& literals)
      : bound(graph), net(*bound.netlist), masters(roles), constants(literals) {
    for (const auto& port : net.ports()) {
      ports.emplace(port.cell, port.name);
    }
    for (uint32_t cell = 0; cell < net.cells(); ++cell) {
      const auto& role = masters[net.type_of(cell)];
      if (role.kind != Master::Kind::logic && role.kind != Master::Kind::gate && role.sinks.empty()) {
        ++unclassified_state;
      }
      for (const auto& sink : role.sinks) {
        const auto pin   = net.pin(cell, sink.local);
        flops           += role.kind == Master::Kind::flop;
        const auto path  = trace(pin);
        if (path.root == no_index) {
          unresolved += path.stop == Path::Stop::unbound || path.stop == Path::Stop::loop;
          add(stops[path.stop], sink, role.kind, path, pin);
          continue;
        }
        add(domains[path.root], sink, role.kind, path, pin);
        if (role.kind == Master::Kind::flop) {
          for (unsigned edge : {0u, 1u}) {
            if (sink.edges & (1u << edge)) {
              add(edge_load[path.root][edge ^ path.inverted], sink, role.kind, path, pin);
            }
          }
        } else if (role.kind == Master::Kind::latch) {
          add(latch_load[path.root], sink, role.kind, path, pin);
        }
        if (path.function) {
          add(groups.at(path.function).load, sink, role.kind, path, pin);
        }
      }
    }
  }

  std::string json(std::string_view top, size_t gate_masters, size_t dont_use, const std::vector<liberty::Icg_cell>& usable) const {
    rapidjson::StringBuffer buf;
    Writer                  w(buf);
    struct Item {
      std::string kind, action;
      uint32_t    pin;
      const Load* load;
      uint32_t    related = no_index;
    };
    std::vector<Item> items;
    w.StartObject();
    w.Key("schema_version");
    w.Uint(1);
    w.Key("top");
    w.String(top.data(), top.size());
    w.Key("integrated_gate_library");
    w.StartObject();
    w.Key("declared_masters");
    w.Uint64(gate_masters);
    w.Key("dont_use_masters");
    w.Uint64(dont_use);
    w.Key("mapper_eligible_masters");
    w.StartArray();
    for (const auto& gate : usable) {
      w.String(gate.name.c_str());
    }
    w.EndArray();
    w.EndObject();
    w.Key("flop_clock_pins");
    w.Uint(flops);
    w.Key("unresolved_clock_pins");
    w.Uint(unresolved);
    uint32_t constant = 0;
    for (const auto& [reason, load] : stops) {
      if (reason == Path::Stop::zero || reason == Path::Stop::one) {
        constant += load.sinks;
      }
    }
    w.Key("constant_source_clock_pins");
    w.Uint(constant);
    w.Key("pins_without_active_root");
    w.StartArray();
    for (const auto& [reason, load] : stops) {
      const char* names[] = {"no_bound_driver", "constant_zero", "constant_one", "clock_loop"};
      w.StartObject();
      w.Key("reason");
      w.String(names[static_cast<unsigned>(reason)]);
      w.Key("example_pin");
      w.String(label(load.witness).c_str());
      load_fields(w, load);
      w.EndObject();
    }
    w.EndArray();
    w.Key("unmapped_logic_nodes");
    w.Uint64(bound.unmapped_nodes);
    w.Key("state_instances_without_clock_pin");
    w.Uint(unclassified_state);
    w.Key("opaque_instances");
    w.Uint64(bound.opaque_cells);
    w.Key("native_memories_without_liberty");
    w.Uint64(bound.memory_nodes);
    w.Key("domains");
    w.StartArray();
    for (const auto& [root, load] : domains) {
      const auto cell = net.cell_of(root);
      const auto port = net.type_of(cell) == fast::Cell_types::input_port_type;
      w.StartObject();
      w.Key("root");
      w.String(label(root).c_str());
      if (!port) {
        const auto  type   = net.type_of(cell);
        const auto& master = net.types().type(type);
        w.Key("driver_master");
        w.String(master.name.c_str());
        w.Key("driver_inputs");
        w.StartObject();
        for (uint32_t local = 0; local < master.pin_count; ++local) {
          const auto& pin = net.types().pin(type, local);
          if (pin.direction == fast::Direction::input) {
            w.Key(pin.name.c_str());
            write_signal(w, signal(cell, local));
          }
        }
        w.EndObject();
      }
      load_fields(w, load);
      w.Key("max_gate_depth");
      w.Uint(load.depth);
      const auto edges = edge_load.find(root);
      if (edges != edge_load.end()) {
        w.Key("rising_flop_pins");
        w.Uint(edges->second[0].sinks);
        w.Key("falling_flop_pins");
        w.Uint(edges->second[1].sinks);
      }
      w.EndObject();
      if (!port) {
        const auto& role  = masters[net.type_of(cell)];
        const bool  state = role.kind == Master::Kind::flop || role.kind == Master::Kind::latch;
        items.push_back(
            {state ? "state_driven_clock" : "logic_driven_clock",
             state ? "Review using the existing clock with a periodic enable."
             : usable.empty()
                 ? "Review a synchronous enable or a root clock mux. The mapping library has no eligible integrated gate."
                 : "Review the clock logic and use a recognized clock gate or a root clock mux.",
             root,
             &load});
      }
      if (load.sinks < 6) {
        items.push_back({"small_domain", "Review sharing a clock and using a local enable.", root, &load});
      }
      if (edges != edge_load.end() && edges->second[0].sinks && edges->second[1].sinks) {
        const auto& minority = edges->second[edges->second[0].sinks < edges->second[1].sinks ? 0 : 1];
        items.push_back({"mixed_edges", "Review using the majority clock edge for these registers.", minority.witness, &minority});
      }
      if (const auto it = latch_load.find(root); it != latch_load.end()) {
        items.push_back({"latch_clock", "Review whether the level-sensitive latch is required.", it->second.witness, &it->second});
      }
    }
    w.EndArray();
    w.Key("gating_functions");
    w.StartArray();
    uint32_t                                          small = 0, large = 0;
    // Look up peers with one term removed in the same occurrence. A shared
    // primary clock alone does not make unrelated blocks candidates.
    std::map<std::pair<uint32_t, Function>, uint32_t> peers;
    for (const auto& [id, group] : groups) {
      if (!group.load.flops) {
        continue;
      }
      for (auto gate : group.gates) {
        peers.try_emplace({bound.cell_prefix[net.cell_of(gate)], group.key}, gate);
      }
    }
    for (const auto& [id, group] : groups) {
      const auto& key = group.key;
      w.StartObject();
      w.Key("id");
      w.Uint(id);
      w.Key("parent");
      w.Uint(key.parent);
      w.Key("root");
      w.String(label(key.root).c_str());
      w.Key("inverted_clock");
      w.Bool(key.inverted);
      w.Key("style");
      w.String(key.style.c_str());
      w.Key("enable");
      write_signal(w, group.enable);
      w.Key("enable_terms");
      w.StartArray();
      for (const auto& term : key.terms) {
        write_signal(w, term);
      }
      w.EndArray();
      w.Key("test");
      write_signal(w, key.test);
      w.Key("gates");
      w.StartArray();
      for (auto pin : group.gates) {
        w.String(label(pin).c_str());
      }
      w.EndArray();
      load_fields(w, group.load);
      w.EndObject();
      if (group.load.flops > 0 && group.load.flops < 6) {
        small += group.load.flops;
        items.push_back(
            {"small_gating_function", "Review sharing this enable with neighboring registers.", *group.gates.begin(), &group.load});
      }
      if (group.load.flops && key.terms.size() > 1) {
        auto base  = key;
        bool found = false;
        for (size_t i = 0; i < key.terms.size() && !found; ++i) {
          base.terms = key.terms;
          base.terms.erase(base.terms.begin() + i);
          for (auto gate : group.gates) {
            const auto peer = peers.find({bound.cell_prefix[net.cell_of(gate)], base});
            if (peer != peers.end()) {
              items.push_back({"related_gating_functions",
                               "Review sharing the simpler enable if the extra condition is redundant.",
                               gate,
                               &group.load,
                               peer->second});
              found = true;
              break;
            }
          }
        }
      }
      if (group.load.flops > 30) {
        large += group.load.flops;
      }
    }
    w.EndArray();
    w.Key("flop_share_in_functions_below_6");
    w.Double(flops ? double(small) / flops : 0);
    w.Key("flop_share_in_functions_above_30");
    w.Double(flops ? double(large) / flops : 0);
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
      if (a.load->cap != b.load->cap) {
        return a.load->cap > b.load->cap;
      }
      return std::tie(a.kind, a.pin) < std::tie(b.kind, b.pin);
    });
    w.Key("items");
    w.StartArray();
    for (const auto& item : items) {
      w.StartObject();
      w.Key("kind");
      w.String(item.kind.c_str());
      w.Key("pin");
      w.String(label(item.pin).c_str());
      if (item.related != no_index) {
        w.Key("related_gate");
        w.String(label(item.related).c_str());
      }
      auto     src     = source(item.pin);
      uint32_t src_pin = item.pin;
      if (src.empty()) {
        src_pin = item.load->witness;
        src     = source(src_pin);
      }
      w.Key("source");
      if (src.empty()) {
        w.Null();
      } else {
        w.String(src.c_str());
      }
      if (!src.empty()) {
        w.Key("source_pin");
        w.String(label(src_pin).c_str());
      }
      w.Key("example_sink");
      w.String(label(item.load->witness).c_str());
      load_fields(w, *item.load);
      w.Key("suggestion");
      w.String(item.action.c_str());
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    return {buf.GetString(), buf.GetSize()};
  }
};
}  // namespace

std::string clock_census(hhds::Graph& top, const std::string& text) {
  liberty::timing::Clock_structures   clocks;
  const auto                          lib     = liberty::timing::parse(text, clocks);
  const liberty::timing::Library*     libs[]  = {&lib};
  const std::string                   names[] = {"synthesis"};
  const auto                          types   = fast::Cell_types::build(libs, names);
  const auto                          roles   = classify(types, lib, clocks);
  absl::flat_hash_map<uint32_t, bool> constants;
  const auto                          graph        = fast::bind_hier(top, types, &constants);
  size_t                              gate_masters = 0, dont_use = 0;
  for (const auto& [name, structure] : clocks) {
    if (!structure.gate_style.empty()) {
      ++gate_masters;
      dont_use += structure.dont_use;
    }
  }
  const auto usable = gate_masters > dont_use ? liberty::scan_icg_cells_text(text) : std::vector<liberty::Icg_cell>{};
  return Census(graph, roles, constants).json(top.get_name(), gate_masters, dont_use, usable);
}
}  // namespace livehd::pcrs
