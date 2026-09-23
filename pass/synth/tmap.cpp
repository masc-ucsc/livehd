// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tmap.hpp"

#include <bit>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <numeric>
#include <map>

namespace livehd::synth {

namespace {
Mapped_network map_pass(const Unate_network& network, Tmap_backend& backend, const Mapping_request& defaults,
                        const Network_environment* environment, const std::vector<double>& loads, std::vector<double>& next_loads) {
  Mapped_network                 result;
  std::vector<Input_environment> arrivals(network.nodes.size());
  std::vector<Id>                wires(network.nodes.size(), std::numeric_limits<Id>::max());
  std::vector<double>            required(network.nodes.size(), defaults.required_ps);
  if (environment && defaults.required_ps > 0) {
    for (size_t po = 0; po < network.outputs.size(); ++po) {
      const auto id = network.outputs[po];
      required[id]  = std::min(required[id], std::max(1.0, defaults.required_ps - environment->outputs[po].downstream_ps));
    }
  }
  for (Id id = 0; id < network.nodes.size(); ++id) {
    if (network.nodes[id].kind == Node_kind::source) {
      if (environment) {
        if (network.nodes[id].origin >= environment->inputs.size()) {
          return {.status = Map_status::invalid, .reason = "missing source timing environment"};
        }
        arrivals[id] = environment->inputs[network.nodes[id].origin];
      }
      wires[id] = static_cast<Id>(result.source_origins.size());
      result.source_origins.push_back(network.nodes[id].origin);
    }
  }
  for (Id id = 0; id < network.nodes.size(); ++id) {
    const auto& n = network.nodes[id];
    if (defaults.admission && !defaults.admission()) {
      return {.status = Map_status::exhausted, .reason = "mapping resource budget exhausted"};
    }
    if (n.kind == Node_kind::source) {
      continue;
    }
    Mapping_request request = defaults;
    request.inputs.clear();
    for (auto port : n.ports) {
      if (port >= id || wires[port] == std::numeric_limits<Id>::max()) {
        return {.status = Map_status::invalid, .reason = "non-topological unate port"};
      }
      request.inputs.push_back(environment ? arrivals[port] : Input_environment{});
      request.inputs.back().name = "i" + std::to_string(request.inputs.size() - 1);
    }
    if (environment) {
      request.output_load_ff = loads[id];
      // Initial level-proportional required times are sizing estimates.
      // Actual end-to-end timing decides acceptance after stitching.
      if (defaults.required_ps > 0 && network.depth > 0) {
        request.required_ps = std::max(1.0, defaults.required_ps * std::max(1u, n.level) / network.depth);
      }
      request.required_ps = std::min(request.required_ps, required[id]);
    }
    request.terms    = n.terms;
    request.inverter = n.kind == Node_kind::source_inverter;
    auto fragment    = backend.map(request);
    if (fragment.status != Map_status::mapped) {
      return {.status = fragment.status, .reason = std::move(fragment.reason)};
    }
    if (defaults.admission && !defaults.admission()) {
      return {.status = Map_status::exhausted, .reason = "mapping resource budget exhausted"};
    }
    if (fragment.inputs != n.ports.size() || fragment.cells.size() > request.max_cells
        || fragment.output >= fragment.inputs + fragment.cells.size()) {
      return {.status = Map_status::invalid, .reason = "invalid tmap port correspondence"};
    }
    if (environment) {
      if (fragment.input_loads_ff.size() != n.ports.size() || !std::isfinite(fragment.output_arrival_ps)
          || fragment.output_arrival_ps < 0) {
        return {.status = Map_status::unsupported, .reason = "missing fragment timing characterization"};
      }
      arrivals[id] = fragment.output_environment;
      for (size_t p = 0; p < n.ports.size(); ++p) {
        if (!std::isfinite(fragment.input_loads_ff[p]) || fragment.input_loads_ff[p] < 0) {
          return {.status = Map_status::invalid, .reason = "invalid fragment pin capacitance"};
        }
        next_loads[n.ports[p]] += fragment.input_loads_ff[p];
      }
    }
    std::vector<Id> translation;
    for (auto port : n.ports) {
      translation.push_back(wires[port]);
    }
    double area = 0;
    for (auto cell : fragment.cells) {
      if (cell.inputs.size() != cell.input_pins.size() || !std::isfinite(cell.area) || cell.area < 0) {
        return {.status = Map_status::invalid, .reason = "invalid mapped cell"};
      }
      for (auto& input : cell.inputs) {
        if (input >= translation.size()) {
          return {.status = Map_status::invalid, .reason = "non-topological mapped cell"};
        }
        input = translation[input];
      }
      translation.push_back(static_cast<Id>(result.source_origins.size() + result.cells.size()));
      area += cell.area;
      result.cells.push_back(std::move(cell));
      result.cell_origins.push_back(id);
    }
    if (!std::isfinite(fragment.area) || std::abs(area - fragment.area) > 1e-8 * std::max(1.0, area)) {
      return {.status = Map_status::invalid, .reason = "fragment area does not count its cells"};
    }
    result.area += area;
    wires[id]    = translation[fragment.output];
  }
  for (auto out : network.outputs) {
    if (out >= wires.size() || wires[out] == std::numeric_limits<Id>::max()) {
      return {.status = Map_status::invalid, .reason = "invalid unate output"};
    }
    result.outputs.push_back(wires[out]);
  }
  result.status = Map_status::mapped;
  return result;
}
}  // namespace

Mapped_network map_network(const Unate_network& network, Tmap_backend& backend, const Mapping_request& defaults,
                           const Network_environment* environment) {
  if (defaults.admission && !defaults.admission()) {
    return {.status = Map_status::exhausted, .reason = "mapping resource budget exhausted"};
  }
  std::vector<double> loads(network.nodes.size(), 0), external(loads), next(loads);
  if (!environment) {
    return map_pass(network, backend, defaults, nullptr, loads, next);
  }
  if (environment->outputs.size() != network.outputs.size() || !std::isfinite(environment->internal_load_ff)
      || environment->internal_load_ff < 0) {
    return {.status = Map_status::invalid, .reason = "invalid network timing environment"};
  }
  for (const auto& node : network.nodes) {
    for (auto port : node.ports) {
      if (port >= loads.size()) {
        return {.status = Map_status::invalid, .reason = "invalid unate port"};
      }
      loads[port] += environment->internal_load_ff;
    }
  }
  for (size_t po = 0; po < network.outputs.size(); ++po) {
    const auto id   = network.outputs[po];
    const auto load = environment->outputs[po].load_ff;
    if (id >= loads.size() || !std::isfinite(load) || !std::isfinite(environment->outputs[po].downstream_ps)) {
      return {.status = Map_status::invalid, .reason = "invalid output timing environment"};
    }
    external[id] += load >= 0 ? load : environment->internal_load_ff;
  }
  for (size_t i = 0; i < loads.size(); ++i) {
    loads[i] += external[i];
  }
  // Bounded feedback: map once to learn each real sink-pin capacitance, then
  // remap with summed loads (including all readers of a shared producer).
  next       = external;
  auto first = map_pass(network, backend, defaults, environment, loads, next);
  if (first.status != Map_status::mapped) {
    return first;
  }
  loads = std::move(next);
  next  = external;
  first = {};  // retain the learned loads, not a second full mapped network
  return map_pass(network, backend, defaults, environment, loads, next);
}
Split_mapping map_split(const Logic_network& source, const Split_result& split, Tmap_backend& backend,
                        const Mapping_request& defaults, const Block_request& block_defaults) {
  constexpr Id  none = std::numeric_limits<Id>::max();
  Split_mapping out;
  auto&         net  = out.network;
  const auto    fail = [&](Map_status status, std::string reason) {
    Split_mapping failed;
    failed.network.status = status;
    failed.network.reason = std::move(reason);
    return failed;
  };
  const auto    n = source.nodes.size();
  // Positive and complemented wire of every signal (source nodes, then the
  // synthetic bound-set gates), as far as one exists.
  std::vector<Id> pos(n + split.synthetic, none), neg(n + split.synthetic, none);
  for (Id id = 0; id < n; ++id) {
    if (source.nodes[id].source) {
      pos[id] = static_cast<Id>(net.source_origins.size());
      net.source_origins.push_back(id);
    }
  }
  const auto nsrc = static_cast<Id>(net.source_origins.size());
  // Append cells whose local wires [0, translation.size()) are `translation`;
  // returns the translation extended by every appended cell.
  const auto splice = [&](std::vector<Mapped_cell>& cells, std::vector<Id> translation, Id owner, double area) -> std::optional<std::vector<Id>> {
    double sum = 0;
    for (auto& cell : cells) {
      if (cell.inputs.size() != cell.input_pins.size() || !std::isfinite(cell.area) || cell.area < 0) {
        return std::nullopt;
      }
      for (auto& input : cell.inputs) {
        if (input >= translation.size()) {
          return std::nullopt;
        }
        input = translation[input];
      }
      translation.push_back(nsrc + static_cast<Id>(net.cells.size()));
      sum += cell.area;
      net.cells.push_back(std::move(cell));
      net.cell_origins.push_back(owner);
    }
    if (!std::isfinite(area) || std::abs(sum - area) > 1e-8 * std::max(1.0, sum)) {
      return std::nullopt;
    }
    net.area += sum;
    return translation;
  };
  const auto admitted = [&] { return !defaults.admission || defaults.admission(); };

  if (!split.remainder_outputs.empty()) {
    // The remainder: every node under a remainder output, computed from the
    // sources alone (the sources keep their order, so block wire k is wire k).
    std::vector<uint8_t> needed(n, 0);
    std::vector<Id>      work(split.remainder_outputs.begin(), split.remainder_outputs.end());
    while (!work.empty()) {
      const auto v = work.back();
      work.pop_back();
      if (v >= n || needed[v]) {
        continue;
      }
      needed[v] = 1;
      for (auto in : source.nodes[v].inputs) {
        work.push_back(in);
      }
    }
    Block_request   request = block_defaults;
    Logic_network&  block   = request.logic;
    std::vector<Id> local(n, none);
    for (Id id = 0; id < n; ++id) {
      if (source.nodes[id].source) {
        local[id] = block.add_source();
      }
    }
    for (Id id = 0; id < n; ++id) {
      if (source.nodes[id].source || !needed[id]) {
        continue;
      }
      std::vector<Id> inputs;
      for (auto in : source.nodes[id].inputs) {
        inputs.push_back(local[in]);
      }
      local[id] = block.add_function(std::move(inputs), source.nodes[id].table);
    }
    for (auto v : split.remainder_outputs) {
      block.outputs.push_back(local[v]);
    }
    const auto start  = std::chrono::steady_clock::now();
    auto       mapped = backend.map_block(request);
    out.remainder_ms  = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (mapped.status != Map_status::mapped) {
      return fail(mapped.status, "remainder: " + mapped.reason);
    }
    if (mapped.inputs != nsrc || mapped.outputs.size() != split.remainder_outputs.size()) {
      return fail(Map_status::invalid, "remainder port correspondence");
    }
    std::vector<Id> translation(nsrc);
    std::iota(translation.begin(), translation.end(), Id{0});
    out.remainder_cells = mapped.cells.size();
    out.remainder_area  = mapped.area;
    auto wires          = splice(mapped.cells, std::move(translation), none, mapped.area);
    if (!wires) {
      return fail(Map_status::invalid, "invalid remainder cell");
    }
    for (size_t k = 0; k < mapped.outputs.size(); ++k) {
      if (mapped.outputs[k] >= wires->size()) {
        return fail(Map_status::invalid, "invalid remainder output");
      }
      pos[split.remainder_outputs[k]] = (*wires)[mapped.outputs[k]];
    }
  }

  const auto map_one = [&](Mapping_request request, const std::vector<Id>& ports, Id owner) -> std::optional<Id> {
    request.inputs.clear();
    for (size_t k = 0; k < ports.size(); ++k) {
      request.inputs.push_back({.name = "i" + std::to_string(k)});
    }
    auto fragment = backend.map(request);
    if (fragment.status != Map_status::mapped) {
      out.network.status = fragment.status;
      out.network.reason = std::move(fragment.reason);
      return std::nullopt;
    }
    if (fragment.inputs != ports.size() || fragment.cells.size() > request.max_cells
        || fragment.output >= fragment.inputs + fragment.cells.size()) {
      out.network.status = Map_status::invalid;
      out.network.reason = "invalid tmap port correspondence";
      return std::nullopt;
    }
    const auto cells = fragment.cells.size();
    auto       wires = splice(fragment.cells, ports, owner, fragment.area);
    if (!wires) {
      out.network.status = Map_status::invalid;
      out.network.reason = "invalid mapped cell";
      return std::nullopt;
    }
    (request.inverter ? out.inverters : out.gate_cells) += request.inverter ? 1 : cells;
    return (*wires)[fragment.output];
  };
  // A one-leaf gate is a wire or an inverter of its leaf: no cell of its own,
  // its readers read the leaf in the matching polarity.
  std::vector<std::pair<Id, bool>> alias(n + split.synthetic, {none, false});
  for (const auto& gate : split.gates) {
    if (gate.leaves.size() == 1 && gate.form.cubes.size() == 1 && std::popcount(gate.form.cubes.front().care) == 1) {
      alias[gate.root] = {gate.leaves.front(), (gate.form.cubes.front().ones == 0) != gate.complemented};
    }
  }
  // The wire of `v` in the asked polarity; one shared inverter otherwise.
  std::function<std::optional<Id>(Id, bool)> signal = [&](Id v, bool positive) -> std::optional<Id> {
    if (alias[v].first != none) {
      return signal(alias[v].first, positive != alias[v].second);
    }
    auto& want  = positive ? pos[v] : neg[v];
    auto& other = positive ? neg[v] : pos[v];
    if (want != none) {
      return want;
    }
    if (other == none) {
      out.network.status = Map_status::invalid;
      out.network.reason = "split leaf without a producer";
      return std::nullopt;
    }
    Mapping_request request = defaults;
    request.inverter        = true;
    request.terms.clear();
    auto wire = map_one(std::move(request), {other}, v);
    if (wire) {
      want = *wire;
    }
    return wire;
  };
  for (const auto& gate : split.gates) {
    if (alias[gate.root].first != none) {
      continue;
    }
    if (!admitted()) {
      return fail(Map_status::exhausted, "mapping resource budget exhausted");
    }
    // One port per (leaf, polarity) the form reads, positive before negative.
    std::vector<Id>                    ports;
    std::map<std::pair<uint32_t, bool>, uint32_t> port_of;
    for (uint32_t j = 0; j < gate.leaves.size(); ++j) {
      for (const bool positive : {true, false}) {
        if (((positive ? gate.form.positive : gate.form.negative) >> j) & 1) {
          auto wire = signal(gate.leaves[j], positive);
          if (!wire) {
            return fail(out.network.status, out.network.reason);
          }
          port_of[{j, positive}] = static_cast<uint32_t>(ports.size());
          ports.push_back(*wire);
        }
      }
    }
    Mapping_request request = defaults;
    request.inverter        = false;
    request.terms.clear();
    for (const auto& cube : gate.form.cubes) {
      std::vector<uint32_t> term;
      for (uint32_t j = 0; j < gate.leaves.size(); ++j) {
        if ((cube.care >> j) & 1) {
          term.push_back(port_of.at({j, ((cube.ones >> j) & 1) != 0}));
        }
      }
      request.terms.push_back(std::move(term));
    }
    auto wire = map_one(std::move(request), ports, gate.root);
    if (!wire) {
      return fail(out.network.status, out.network.reason);
    }
    (gate.complemented ? neg : pos)[gate.root] = *wire;
  }
  for (auto root : source.outputs) {
    auto wire = root < n ? signal(root, true) : std::nullopt;
    if (!wire) {
      return fail(Map_status::invalid, "split output without a producer");
    }
    net.outputs.push_back(*wire);
  }
  net.status = Map_status::mapped;
  return out;
}

}  // namespace livehd::synth
