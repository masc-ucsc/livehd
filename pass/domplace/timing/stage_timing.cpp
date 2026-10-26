// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "stage_timing.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

#include "inou/lefdef/mapped_graph.hpp"
#include "json_writer.hpp"
#include "pass/place/require.hpp"

namespace livehd::domplace::timing {
namespace {
namespace model = liberty::timing;
using json::flag;
using json::number;
using json::real;
using json::text;
using json::Writer;

// The fixed stage schedule evaluates in the second half of the cycle.
void require_half_duty(const std::string& clock, double period, double rise, double fall) {
  if (std::abs((fall - rise) - period / 2) > 1e-9 * std::max(1., period)) {
    throw std::invalid_argument(
        std::format("clock {} has a non-50% waveform (rise {} ps, fall {} ps, period {} ps): the domino stage schedule is a "
                    "fixed T/2",
                    clock,
                    rise,
                    fall,
                    period));
  }
}
template <class Declarations>
std::vector<std::string> port_names(const Declarations& declarations) {
  std::vector<std::string> names;
  for (const auto& declaration : declarations) {
    names.push_back(declaration.name);
  }
  return names;
}
void maximum(std::optional<double>& target, std::optional<double> value) {
  if (value && (!target || *value > *target)) {
    target = value;
  }
}
// The larger rise/fall value, absent when neither is known.
std::optional<double> larger(std::optional<double> rise, std::optional<double> fall) {
  maximum(rise, fall);
  return rise;
}
// Worst endpoint slack over the transitions that have an arrival and a requirement.
struct Slack {
  double   value      = 0;
  unsigned transition = 0;
};
std::optional<Slack> worst_slack(const late::Pin_result& pin) {
  std::optional<Slack> out;
  for (unsigned t = 0; t < 2; ++t) {
    const auto& state = pin.transitions[t];
    if (state.arrival_ps && state.required_ps && (!out || *state.required_ps - *state.arrival_ps < out->value)) {
      out = Slack{*state.required_ps - *state.arrival_ps, t};
    }
  }
  return out;
}
struct Class_summary {
  size_t                endpoints = 0;
  std::optional<double> wns_ps, tns_ps, period_ps;
  std::string           worst;
};
}  // namespace

bool word_valued_ports(const hhds::Graph& graph) {
  const auto word = [](const auto& declarations) {
    return std::any_of(declarations.begin(), declarations.end(), [](const auto& d) { return d.bits > 1; });
  };
  return word(graph.get_io()->get_input_pin_decls()) || word(graph.get_io()->get_output_pin_decls());
}

void check_clock_spec(const Clock_spec& clock) {
  if (!clock.unresolved.empty()) {
    throw std::invalid_argument(clock.unresolved);
  }
  if (!std::isfinite(clock.period_ps) || !(clock.period_ps > 0)) {
    throw std::invalid_argument("stage-timing clock period must be positive");
  }
  if (clock.waveform_ps) {
    require_half_duty(clock.name.empty() ? clock.port : clock.name,
                      clock.period_ps,
                      (*clock.waveform_ps)[0],
                      (*clock.waveform_ps)[1]);
  }
}

Constraint_source stage_constraints(const hhds::Graph& graph, const std::optional<std::string>& sdc_text, std::string_view design,
                                    late::Sdc_units units, const std::optional<Clock_spec>& clock, double clock_uncertainty_ps) {
  placement::require_nonnegative(clock_uncertainty_ps, "invalid stage-timing clock uncertainty");
  const auto        inputs  = port_names(graph.get_io()->get_input_pin_decls());
  const auto        outputs = port_names(graph.get_io()->get_output_pin_decls());
  Constraint_source source;
  std::string       refused;
  if (sdc_text) {
    try {
      source.constraints = late::read_saved_sdc(*sdc_text, design, units);
      source.kind        = "saved-sdc";
    } catch (const std::exception& error) {
      refused = error.what();
    }
  }
  if (source.kind.empty()) {
    if (!clock) {
      throw std::invalid_argument(sdc_text ? "the SDC is outside the saved-SDC subset (" + refused + ") and no clock port is named"
                                           : std::string("stage timing needs an SDC or a clock port"));
    }
    if (!clock->unresolved.empty() && sdc_text) {
      throw std::invalid_argument(clock->unresolved + "; the saved-SDC subset refused it (" + refused + ")");
    }
    check_clock_spec(*clock);
    if (std::find(inputs.begin(), inputs.end(), clock->port) == inputs.end()) {
      throw std::invalid_argument("stage-timing clock port is not a design input: " + clock->port);
    }
    const auto        name = clock->name.empty() ? clock->port : clock->name;
    late::Ideal_clock ideal{.period_ps = clock->period_ps, .uncertainty_ps = clock_uncertainty_ps, .roots = {clock->port}};
    if (clock->waveform_ps) {
      ideal.phase_ps   = (*clock->waveform_ps)[0];
      ideal.falling_ps = (*clock->waveform_ps)[1];
    }
    source.constraints.clocks[name] = ideal;
    source.constraints.complete     = true;
    source.kind                     = "clock-scan";
    if (sdc_text) {
      source.notes.push_back("saved SDC not used (" + refused + "); one clock on " + clock->port);
    }
  } else {
    for (auto& [name, ideal] : source.constraints.clocks) {
      if (ideal.falling_ps) {
        require_half_duty(name, ideal.period_ps, ideal.phase_ps, *ideal.falling_ps);
      }
      ideal.uncertainty_ps = clock_uncertainty_ps;
    }
  }
  std::set<std::string> roots;
  for (const auto& [name, ideal] : source.constraints.clocks) {
    (void)name;
    roots.insert(ideal.roots.begin(), ideal.roots.end());
  }
  for (const auto& [name, alias] : source.constraints.clock_aliases) {
    (void)name;
    roots.insert(alias.roots.begin(), alias.roots.end());
  }
  auto& cuts = source.constraints.cuts;
  for (const auto& port : inputs) {
    if (!roots.contains(port) && !source.constraints.inputs.contains(port) && cuts.insert(port).second) {
      source.excluded.emplace_back(port, "io-unconstrained: no input delay");
    }
  }
  for (const auto& port : outputs) {
    if (!roots.contains(port) && !source.constraints.outputs.contains(port) && cuts.insert(port).second) {
      source.excluded.emplace_back(port, "io-unconstrained: no output delay");
    }
  }
  return source;
}

Stage_timing evaluate_stage_timing(const hhds::Graph& graph, const physical::Parasitics* parasitics,
                                   std::shared_ptr<const model::Library>          library,
                                   std::shared_ptr<const late::Provisional_cells> provisional, const Domino_timing_view* view,
                                   Constraint_source source, uint64_t topology_generation, const Stage_timing_options& options) {
  if (!library) {
    throw std::invalid_argument("stage timing needs a Liberty timing model");
  }
  const auto&              constraints = source.constraints;
  late::Proxy              proxy(graph, library, constraints, topology_generation, options.model_generation, provisional);
  auto                     all = proxy.evaluate(parasitics);
  // An endpoint whose fan-in holds no constrained launch (only cut ports) is
  // outside the measured subset: cut it with that reason, not uncovered.
  std::vector<std::string> unreached;
  for (const auto& pin : all.pins) {
    if (pin.endpoint && !pin.transitions[0].arrival_ps && !pin.transitions[1].arrival_ps) {
      unreached.push_back(pin.name);
    }
  }
  if (!unreached.empty()) {
    for (const auto& name : unreached) {
      source.constraints.cuts.insert(name);
      source.excluded.emplace_back(name, "unreached: no constrained launch in its fan-in");
    }
    proxy = late::Proxy(graph, library, constraints, topology_generation, options.model_generation, provisional);
    all   = proxy.evaluate(parasitics);
  }
  const auto domino_master
      = [&](std::string_view master) { return provisional && provisional->library.cells.contains(std::string(master)); };
  const auto characterized = [&](const std::string& master) -> const model::Cell* {
    if (const auto found = library->cells.find(master); found != library->cells.end()) {
      return &found->second;
    }
    if (provisional) {
      if (const auto found = provisional->library.cells.find(master); found != provisional->library.cells.end()) {
        return &found->second;
      }
    }
    return nullptr;
  };

  // Bound instances by name, and every bound pin's model. Ports have no ':'.
  struct Instance {
    std::string                   master;
    const model::Cell*            cell   = nullptr;
    bool                          domino = false;
    std::map<std::string, size_t> pins;
    std::optional<std::string>    clock;  // the ideal clock at its clock pin
  };
  std::map<std::string, Instance> instances;
  std::vector<const model::Pin*>  pin_model(all.pins.size());
  std::vector<std::string>        owner(all.pins.size());  // instance name, empty for a port
  std::vector<bool>               sink(all.pins.size());   // reached through a wire
  for (size_t i = 0; i < all.pins.size(); ++i) {
    const auto& pin   = all.pins[i];
    const auto  colon = pin.name.rfind(':');
    auto        node  = pin.graph_node;
    const auto  io    = colon == std::string::npos || node.is_invalid() ? nullptr : node.get_subnode_io();
    if (!io) {
      sink[i] = true;  // a port: the input port is a source without predecessor
      continue;
    }
    owner[i]        = pin.name.substr(0, colon);
    auto& instance  = instances[owner[i]];
    instance.master = std::string(io->get_name());
    instance.cell   = characterized(instance.master);
    instance.domino = domino_master(instance.master);
    instance.pins.emplace(pin.name.substr(colon + 1), i);
    if (instance.cell) {
      if (const auto found = instance.cell->pins.find(pin.name.substr(colon + 1)); found != instance.cell->pins.end()) {
        pin_model[i] = &found->second;
        sink[i]      = found->second.direction == "input";
        if (found->second.clock && pin.clock_name) {
          instance.clock = pin.clock_name;
        }
      }
    }
  }
  const auto clock_period = [&](const std::optional<std::string>& clock) -> std::optional<double> {
    if (!clock) {
      return std::nullopt;
    }
    const auto found = constraints.clocks.find(*clock);
    return found == constraints.clocks.end() ? std::nullopt : std::optional<double>{found->second.period_ps};
  };
  // Capture identity of an endpoint: class, fraction and clock.
  struct Capture {
    std::string                cls;
    double                     fraction = 1;
    std::optional<std::string> clock;
  };
  const auto capture = [&](size_t pin) {
    Capture out;
    if (owner[pin].empty()) {
      out.cls = "output";
      if (const auto boundary = constraints.outputs.find(all.pins[pin].name);
          boundary != constraints.outputs.end() && !boundary->second.clock.empty()) {
        out.clock = boundary->second.clock;
      }
      return out;
    }
    const auto& instance = instances.at(owner[pin]);
    out.cls              = instance.domino ? "domino" : all.pins[pin].clock_gate ? "gate" : "flop";
    out.clock            = instance.clock;
    out.fraction         = all.pins[pin].capture_share;  // a half-cycle pair through an inverted clock
    if (instance.domino) {
      if (const auto budget = provisional->capture.find(instance.master); budget != provisional->capture.end()) {
        out.fraction *= budget->second.fraction;
      }
    }
    return out;
  };
  const auto launch_class = [&](size_t pin) {
    return owner[pin].empty()                ? std::string("input")
           : instances.at(owner[pin]).domino ? std::string("domino")
                                             : std::string("flop");
  };
  const auto launch_clock = [&](size_t pin) -> std::optional<std::string> {
    if (owner[pin].empty()) {
      const auto input = constraints.inputs.find(all.pins[pin].name);
      return input == constraints.inputs.end() || input->second.clock.empty() ? std::nullopt
                                                                              : std::optional<std::string>{input->second.clock};
    }
    return instances.at(owner[pin]).clock;
  };
  // The worst path to an endpoint transition, split at each step: a step into
  // an input pin or an output port is a wire, a step into a cell output is a
  // cell arc; the launch is the path's first arrival (clock-to-Q or input).
  struct Path {
    size_t                launch = 0;
    std::optional<double> launch_ps;
    double                cell_ps = 0, wire_ps = 0;
  };
  const auto trace = [&](size_t pin, unsigned transition) {
    Path path;
    for (size_t steps = 0; steps <= 2 * all.pins.size(); ++steps) {
      const auto& state = all.pins[pin].transitions[transition];
      if (!state.predecessor) {
        break;
      }
      const auto& previous                       = all.pins[state.predecessor->pin].transitions[state.predecessor->transition];
      const auto  step                           = state.arrival_ps.value_or(0) - previous.arrival_ps.value_or(0);
      (sink[pin] ? path.wire_ps : path.cell_ps) += step;
      pin                                        = state.predecessor->pin;
      transition                                 = state.predecessor->transition;
    }
    path.launch    = pin;
    path.launch_ps = all.pins[pin].transitions[transition].arrival_ps;
    return path;
  };

  // Per (launch, capture) class: the full evaluation names the classes of
  // the worst paths; the filtered evaluations give each class its own slack.
  std::map<std::string, Class_summary> classes;
  const auto                           accumulate = [&](const late::Result& result, const std::string& launch) {
    for (size_t i = 0; i < result.pins.size(); ++i) {
      if (!result.pins[i].endpoint) {
        continue;
      }
      const auto slack = worst_slack(result.pins[i]);
      if (!slack) {
        continue;
      }
      const auto end     = capture(i);
      auto&      summary = classes[launch + "->" + end.cls];
      ++summary.endpoints;
      summary.tns_ps = summary.tns_ps.value_or(0) + std::min(0., slack->value);
      if (!summary.wns_ps || slack->value < *summary.wns_ps) {
        summary.wns_ps = slack->value;
        summary.worst  = result.pins[i].name;
      }
      if (const auto period = clock_period(end.clock)) {
        maximum(summary.period_ps, *period - slack->value / end.fraction);
      }
    }
  };
  if (options.classes) {
    const auto flops
        = proxy.evaluate(parasitics,
                         nullptr,
                         {.launch_filter = [&](std::string_view cell) { return !domino_master(cell); }, .seed_inputs = false});
    const auto dominos
        = proxy.evaluate(parasitics,
                         nullptr,
                         {.launch_filter = [&](std::string_view cell) { return domino_master(cell); }, .seed_inputs = false});
    const auto inputs = proxy.evaluate(parasitics, nullptr, {.launch_filter = [](std::string_view) { return false; }});
    accumulate(flops, "flop");
    accumulate(dominos, "domino");
    accumulate(inputs, "input");
  }

  // Coverage and the graph period over the measured subset.
  Stage_timing out;
  out.excluded        = source.excluded.size();
  const auto graph_ps = all.certified ? all.achievable_period_ps : std::nullopt;
  if (!all.certified) {
    std::string detail
        = all.unsupported.empty() ? std::string("no timed endpoint in the measured subset") : all.unsupported.front();
    if (all.unsupported.size() > 1) {
      detail += std::format(" (+{} more)", all.unsupported.size() - 1);
    }
    out.reasons.push_back("uncertified: " + detail);
  } else if (!graph_ps) {
    out.reasons.push_back("no single-clock period: the measured subset has several active clocks or an absolute requirement");
  }
  std::optional<std::string> limiting_endpoint;
  {
    std::optional<double> worst;
    for (size_t i = 0; i < all.pins.size(); ++i) {
      const auto slack  = all.pins[i].endpoint ? worst_slack(all.pins[i]) : std::nullopt;
      const auto end    = slack ? capture(i) : Capture{};
      const auto period = slack ? clock_period(end.clock) : std::nullopt;
      if (period && (!worst || *period - slack->value / end.fraction > *worst)) {
        worst             = *period - slack->value / end.fraction;
        limiting_endpoint = launch_class(trace(i, slack->transition).launch) + "->" + end.cls + " " + all.pins[i].name;
      }
    }
  }

  // The view indexed once: variants by name, sidecar owners by state name.
  std::map<std::string, const Domino_timing_view::Variant*, std::less<>>               variants;
  std::map<std::string, std::vector<const Domino_timing_view::Instance*>, std::less<>> owners;
  if (view) {
    for (const auto& variant : view->variants) {
      variants.emplace(variant.name, &variant);
    }
    for (const auto& instance : view->instances) {
      owners[instance.state].push_back(&instance);
    }
  }
  const auto variant_of = [&](std::string_view master) -> const Domino_timing_view::Variant* {
    const auto found = variants.find(master);
    return found == variants.end() ? nullptr : found->second;
  };

  // Domino stages: internal and precharge checks per instance.
  const Stage_values    stage = view ? view->stage : Stage_values{};
  std::set<std::string> unmeasured;
  std::optional<double> internal_ps, precharge_ps;
  std::string           internal_at, precharge_at;
  size_t                domino_instances = 0;
  for (const auto& [name, instance] : instances) {
    if (!instance.domino) {
      continue;
    }
    ++domino_instances;
    const auto* variant = variant_of(instance.master);
    const auto  need    = [&](const char* coefficient, std::optional<double> value) {
      if (!value) {
        unmeasured.insert(coefficient);
      }
      return value;
    };
    need("setup_eval_ps", stage.setup_eval_ps);
    need("uncertainty_first_ps", stage.uncertainty_first_ps);
    const auto delays = variant_delays(stage, variant);
    const auto eval   = need("eval_delay_ps", delays.eval_delay_ps);
    const auto setup  = need("setup_capture_ps", variant ? variant->setup_capture_ps : std::nullopt);
    const auto u2     = need("uncertainty_second_ps", stage.uncertainty_second_ps);
    const auto pre    = need("precharge_ps", delays.precharge_ps);
    const auto margin = need("precharge_margin_ps", stage.precharge_margin_ps);
    if (eval && setup && u2 && (!internal_ps || 2 * (*eval + *setup + *u2) > *internal_ps)) {
      internal_ps = 2 * (*eval + *setup + *u2);
      internal_at = name;
    }
    if (pre && margin && (!precharge_ps || 2 * (*pre + *margin) > *precharge_ps)) {
      precharge_ps = 2 * (*pre + *margin);
      precharge_at = name;
    }
  }
  out.unmeasured.assign(unmeasured.begin(), unmeasured.end());
  if (!out.unmeasured.empty()) {
    std::string names;
    for (const auto& name : out.unmeasured) {
      names += (names.empty() ? "" : ", ") + name;
    }
    out.reasons.push_back("unmeasured: " + names + " null");
  }
  if (!source.excluded.empty()) {
    out.reasons.push_back(std::format("excluded: {} pin(s) outside the measured subset", source.excluded.size()));
  }
  out.status = !graph_ps || !out.unmeasured.empty() ? "incomplete" : source.excluded.empty() ? "complete" : "partial";
  std::vector<std::pair<std::string, std::optional<double>>> by_class{
      {    "graph",     graph_ps},
      { "internal",  internal_ps},
      {"precharge", precharge_ps}
  };
  if (out.status != "incomplete") {
    out.period_ps = graph_ps;
    out.limiting  = limiting_endpoint.value_or("graph");
    if (internal_ps && *internal_ps > *out.period_ps) {
      out.period_ps = internal_ps;
      out.limiting  = "internal " + internal_at;
    }
    if (precharge_ps && *precharge_ps > *out.period_ps) {
      out.period_ps = precharge_ps;
      out.limiting  = "precharge " + precharge_at;
    }
  }

  // Clock accounting: sinks are the clock pins of sequential and domino
  // cells; net loads cover every clock-net driver (buffers included); wire
  // capacitance is the placement RC estimate of those nets.
  struct Clock_sink {
    std::string           pin, master;
    bool                  domino = false;
    std::optional<double> cap_ff;
  };
  struct Clock_account {
    size_t                                           domino_sinks = 0, cmos_sinks = 0, missing = 0;
    double                                           sink_ff = 0, domino_ff = 0, cmos_ff = 0, net_load_ff = 0;
    std::optional<double>                            wire_ff;
    std::map<std::string, std::pair<size_t, double>> masters;  // sinks, capacitance
    std::set<std::string>                            drivers;
    std::vector<Clock_sink>                          sinks;  // per instance, in pin order
  };
  std::map<std::string, Clock_account> clock_accounts;
  for (size_t i = 0; i < all.pins.size(); ++i) {
    const auto& pin = all.pins[i];
    if (!pin.clock || !pin.clock_name) {
      continue;
    }
    auto& account        = clock_accounts[*pin.clock_name];
    account.net_load_ff += larger(pin.transitions[0].load_ff, pin.transitions[1].load_ff).value_or(0);
    account.drivers.insert(pin.name);
    if (!pin_model[i] || !pin_model[i]->clock || pin_model[i]->direction != "input") {
      continue;
    }
    const auto& instance = instances.at(owner[i]);
    const auto  cap      = larger(pin_model[i]->capacitance_ff[0], pin_model[i]->capacitance_ff[1]);
    account.sinks.push_back({pin.name, instance.master, instance.domino, cap});
    (instance.domino ? account.domino_sinks : account.cmos_sinks) += 1;
    auto& master                                                   = account.masters[instance.master];
    ++master.first;
    if (!cap) {
      ++account.missing;
      continue;
    }
    master.second                                           += *cap;
    account.sink_ff                                         += *cap;
    (instance.domino ? account.domino_ff : account.cmos_ff) += *cap;
  }
  if (parasitics) {
    for (const auto& net : parasitics->nets) {
      for (auto& [name, account] : clock_accounts) {
        (void)name;
        if (account.drivers.contains(net.driver)) {
          double wire = 0;
          for (const auto& node : net.nodes) {
            wire += node.capacitance_ff;
          }
          account.wire_ff = account.wire_ff.value_or(0) + wire;
        }
      }
    }
  }
  out.c_clk_ff = 0.;
  for (const auto& [name, account] : clock_accounts) {
    (void)name;
    if (account.missing) {
      out.c_clk_ff.reset();
      break;
    }
    *out.c_clk_ff += account.sink_ff;
  }

  // Native electrical limits against the evaluated loads and slews.
  struct Violation {
    std::string pin, kind;
    double      value = 0, limit = 0;
  };
  std::vector<Violation> violations;
  for (size_t i = 0; i < all.pins.size(); ++i) {
    if (!pin_model[i]) {
      continue;
    }
    const auto& pin  = all.pins[i];
    const auto  load = larger(pin.transitions[0].load_ff, pin.transitions[1].load_ff);
    const auto  slew = larger(pin.transitions[0].slew_ps, pin.transitions[1].slew_ps);
    if (pin_model[i]->direction == "output" && pin_model[i]->max_capacitance_ff && load
        && *load > *pin_model[i]->max_capacitance_ff) {
      violations.push_back({pin.name, "max_capacitance", *load, *pin_model[i]->max_capacitance_ff});
    }
    if (pin_model[i]->max_transition_ps && slew && *slew > *pin_model[i]->max_transition_ps) {
      violations.push_back({pin.name, "max_transition", *slew, *pin_model[i]->max_transition_ps});
    }
  }

  // Area: the LEF footprint (placement RC poses), the domino footprint and
  // the Liberty basis (domino cells at their Liberty-basis estimate).
  double domino_footprint = 0, liberty_area = 0;
  for (const auto& [name, instance] : instances) {
    (void)name;
    const auto* variant  = instance.domino ? variant_of(instance.master) : nullptr;
    domino_footprint    += variant ? variant->a_footprint_um2 : 0;
    liberty_area        += variant ? variant->a_estimate_um2 : instance.cell ? instance.cell->area : 0;
  }
  const auto            dbu = options.dbu ? options.dbu : view ? view->dbu : std::nullopt;
  std::optional<double> footprint;
  if (parasitics && dbu && *dbu > 0 && !parasitics->cells.empty()) {
    double area = 0;
    for (const auto& cell : parasitics->cells) {
      area += double(cell.width) * double(cell.height);
    }
    footprint = area / (double(*dbu) * double(*dbu));
  }

  if (out.period_ps && all.achievable_period_ps) {
    out.feedback               = all.feedback();
    out.feedback.delay         = *out.period_ps;
    out.feedback.area          = footprint.value_or(liberty_area);
    out.feedback.violations    = static_cast<unsigned>(violations.size());
    out.feedback.source_report = "native_stage_timing";
    if (all.fractional_endpoints) {
      // Period-terms slack: per capture fraction, the pin slack against only
      // that budget's requirements, divided by the fraction.
      std::set<double> fractions{1.};
      for (const auto& [cell, budget] : provisional->capture) {
        (void)cell;
        fractions.insert(budget.fraction);
      }
      std::vector<std::optional<double>> scaled(all.pins.size());
      for (const double fraction : fractions) {
        const auto split = proxy.evaluate(parasitics, nullptr, {.capture_filter = [fraction](double f) { return f == fraction; }});
        for (size_t i = 0; i < split.pins.size(); ++i) {
          if (const auto slack = worst_slack(split.pins[i])) {
            scaled[i] = std::min(scaled[i].value_or(slack->value / fraction), slack->value / fraction);
          }
        }
      }
      out.feedback.wns.reset();
      out.feedback.tns = 0.;
      for (size_t i = 0; i < all.pins.size(); ++i) {
        if (!scaled[i]) {
          continue;
        }
        out.feedback.pins[all.pins[i].name].slack = scaled[i];
        if (all.pins[i].endpoint) {
          out.feedback.wns   = std::min(out.feedback.wns.value_or(*scaled[i]), *scaled[i]);
          *out.feedback.tns += std::min(0., *scaled[i]);
        }
      }
    }
  }

  // The report.
  rapidjson::StringBuffer buffer;
  Writer                  w(buffer);
  w.StartObject();
  number(w, "schema", 1);
  text(w, "kind", "stage_timing");
  text(w, "status", out.status);
  w.Key("reasons");
  w.StartArray();
  for (const auto& reason : out.reasons) {
    json::string(w, reason);
  }
  w.EndArray();

  w.Key("model");
  w.StartObject();
  text(w, "timer", "native_late_proxy");
  text(w, "netlist", options.netlist);
  text(w, "generation", std::format("{:016x}", options.model_generation));
  w.Key("units");
  w.StartObject();
  text(w, "time", "ps");
  text(w, "capacitance", "fF");
  w.EndObject();
  w.Key("provisional");
  if (!view) {
    w.Null();
  } else {
    w.StartObject();
    text(w, "label", provisional ? provisional->label : std::string{});
    number(w, "stage_version", view->stage_version);
    number(w, "model_version", view->model_version);
    number(w, "bundle_schema", view->bundle_schema);
    text(w, "producer_salt", view->producer_salt);
    text(w, "bundle_digest", view->digest);
    text(w, "corner", view->corner);
    text(w, "clock", view->clock);
    real(w, "fo4_ps", view->fo4_ps);
    w.Key("coefficients");
    w.StartObject();
    for (const auto& [name, value] : stage_coefficients(view->stage)) {
      w.Key(name.c_str());
      w.StartObject();
      real(w, "value", value);
      text(w, "source", view->sources.contains(name) ? view->sources.at(name) : std::string("bundle"));
      // A per-variant coefficient without an override: stages[] has each value.
      text(w, "scope", per_variant_coefficient(name) && !value ? "per-variant" : "stage");
      w.EndObject();
    }
    w.EndObject();
    w.EndObject();
  }
  w.EndObject();

  w.Key("clocks");
  w.StartObject();
  for (const auto& [name, clock] : constraints.clocks) {
    w.Key(name.c_str());
    w.StartObject();
    real(w, "period_ps", clock.period_ps);
    real(w, "phase_ps", clock.phase_ps);
    real(w, "falling_ps", clock.falling_ps);
    real(w, "uncertainty_ps", clock.uncertainty_ps);
    w.Key("roots");
    w.StartArray();
    for (const auto& root : clock.roots) {
      json::string(w, root);
    }
    w.EndArray();
    text(w, "domino_schedule", "fixed T/2: precharge in the first half, evaluate in the second");
    w.EndObject();
  }
  w.EndObject();

  w.Key("constraints");
  w.StartObject();
  text(w, "kind", source.kind);
  w.Key("excluded");
  w.StartArray();
  for (const auto& [pin, reason] : source.excluded) {
    w.StartObject();
    text(w, "pin", pin);
    text(w, "reason", reason);
    w.EndObject();
  }
  w.EndArray();
  w.Key("notes");
  w.StartArray();
  for (const auto& note : source.notes) {
    json::string(w, note);
  }
  w.EndArray();
  w.EndObject();

  w.Key("period");
  w.StartObject();
  real(w, "estimate_ps", out.period_ps);
  w.Key("limiting");
  if (out.limiting.empty()) {
    w.Null();
  } else {
    json::string(w, out.limiting);
  }
  w.Key("values");
  w.StartObject();
  for (const auto& [name, value] : by_class) {
    real(w, (name + "_ps").c_str(), value);
  }
  for (const auto& [name, summary] : classes) {
    real(w, (name + "_ps").c_str(), summary.period_ps);
  }
  w.EndObject();
  w.Key("absent");
  w.StartArray();
  for (const auto* launch : {"flop", "domino", "input"}) {
    for (const auto* captured : {"flop", "domino", "output"}) {
      const auto name = std::string(launch) + "->" + captured;
      if (options.classes && !classes.contains(name)) {
        json::string(w, name);
      }
    }
  }
  if (!domino_instances) {
    json::string(w, "internal");
    json::string(w, "precharge");
  }
  w.EndArray();
  w.Key("unmeasured");
  w.StartArray();
  for (const auto& name : out.unmeasured) {
    json::string(w, name);
  }
  w.EndArray();
  w.EndObject();

  w.Key("coverage");
  w.StartObject();
  flag(w, "certified", all.certified);
  number(w, "excluded", source.excluded.size());
  number(w, "fractional_endpoints", all.fractional_endpoints);
  w.Key("unsupported");
  w.StartArray();
  for (const auto& issue : all.unsupported) {
    json::string(w, issue);
  }
  w.EndArray();
  w.Key("warnings");
  w.StartArray();
  for (const auto& warning : all.warnings) {
    json::string(w, warning);
  }
  w.EndArray();
  text(w, "early", "unknown");
  text(w, "early_reason", "setup-only native proxy: no early (hold) models or clock skew assumptions");
  w.EndObject();

  w.Key("classes");
  w.StartArray();
  for (const auto& [name, summary] : classes) {
    const auto arrow = name.find("->");
    w.StartObject();
    text(w, "launch", name.substr(0, arrow));
    text(w, "capture", name.substr(arrow + 2));
    number(w, "endpoints", summary.endpoints);
    real(w, "wns_ps", summary.wns_ps);
    real(w, "tns_ps", summary.tns_ps);
    real(w, "period_ps", summary.period_ps);
    text(w, "worst", summary.worst);
    w.EndObject();
  }
  w.EndArray();

  // One timed endpoint: arrival, requirement, slack, launch and path split.
  const auto endpoint = [&](size_t pin) {
    const auto& result = all.pins[pin];
    const auto  slack  = worst_slack(result);
    const auto  end    = capture(pin);
    w.StartObject();
    text(w, "pin", result.name);
    text(w, "capture", end.cls);
    w.Key("capture_clock");
    if (end.clock) {
      json::string(w, *end.clock);
    } else {
      w.Null();
    }
    real(w, "fraction", end.fraction);
    if (!slack) {
      w.Key("arrival_ps");
      w.Null();
      text(w, "timing", "untimed: see coverage");
      w.EndObject();
      return;
    }
    const auto& state = result.transitions[slack->transition];
    real(w, "arrival_ps", state.arrival_ps);
    real(w, "required_ps", state.required_ps);
    real(w, "slack_ps", slack->value);
    text(w, "transition", slack->transition ? "fall" : "rise");
    const auto path = trace(pin, slack->transition);
    w.Key("launch");
    w.StartObject();
    text(w, "pin", all.pins[path.launch].name);
    text(w, "class", launch_class(path.launch));
    w.Key("clock");
    if (const auto clock = launch_clock(path.launch)) {
      json::string(w, *clock);
    } else {
      w.Null();
    }
    w.EndObject();
    w.Key("path");
    w.StartObject();
    real(w, owner[path.launch].empty() ? "input_ps" : "clock_to_q_ps", path.launch_ps);
    real(w, "cell_ps", path.cell_ps);
    real(w, "wire_ps", path.wire_ps);
    w.EndObject();
    w.EndObject();
  };
  // Stage IDs: the flat instance path, joined to the sidecar's original
  // state by its leaf name (the state name) and master base: placement swaps
  // the rail template and sizing the drive variant, never the function.
  const auto source_state = [&](const std::string& name, const std::string& master) {
    const auto leaf = name.substr(name.rfind('.') == std::string::npos ? 0 : name.rfind('.') + 1);
    std::vector<const Domino_timing_view::Instance*> found;
    if (const auto candidates = owners.find(leaf); candidates != owners.end()) {
      const auto* variant = variant_of(master);
      for (const auto* instance : candidates->second) {
        const auto* owned = variant_of(instance->master);
        if (!variant || !owned || owned->base == variant->base) {
          found.push_back(instance);
        }
      }
    }
    w.Key("source_state");
    if (found.size() == 1) {
      w.StartObject();
      text(w, "region", found[0]->region);
      number(w, "state_index", found[0]->state_index);
      text(w, "state", found[0]->state);
      w.EndObject();
    } else {
      w.Null();
      text(w,
           "source_reason",
           !view           ? std::string("no bundle sidecar")
           : found.empty() ? "no sidecar instance owns state " + leaf
                           : std::format("{} sidecar instances own state {}", found.size(), leaf));
    }
  };
  std::map<std::string, std::string> excluded(source.excluded.begin(), source.excluded.end());
  w.Key("stages");
  w.StartArray();
  for (const auto& [name, instance] : instances) {
    if (!instance.domino) {
      continue;
    }
    const auto* variant = variant_of(instance.master);
    const auto  period  = clock_period(instance.clock);
    w.StartObject();
    text(w, "stage_id", name);
    text(w, "master", instance.master);
    text(w, "family", variant ? variant->family : std::string{});
    text(w, "register", variant ? variant->register_cell : std::string{});
    source_state(name, instance.master);
    w.Key("clock");
    if (instance.clock) {
      json::string(w, *instance.clock);
    } else {
      w.Null();
    }
    // This variant's labeled CLK load, as clock accounting sums it.
    const auto clk = instance.pins.find(kDominoPins.clock);
    real(w,
         "clock_cap_ff",
         clk != instance.pins.end() && pin_model[clk->second]
             ? larger(pin_model[clk->second]->capacitance_ff[0], pin_model[clk->second]->capacitance_ff[1])
             : std::nullopt);
    // Rails in pin order; an excluded rail is listed with its reason.
    w.Key("rails");
    w.StartArray();
    for (const auto& rail : variant ? variant->rails : std::vector<std::string>{}) {
      const auto found = instance.pins.find(rail);
      if (found != instance.pins.end() && all.pins[found->second].endpoint) {
        endpoint(found->second);
      } else {
        const auto label   = name + ":" + rail;
        const auto exclude = excluded.find(label);
        w.StartObject();
        text(w, "pin", label);
        text(w, "timing", exclude == excluded.end() ? "unbound" : "excluded: " + exclude->second);
        w.EndObject();
      }
    }
    w.EndArray();
    // Constants per variant: moving cells cannot shorten them.
    const auto check = [&](const char* key, std::initializer_list<std::pair<const char*, std::optional<double>>> terms) {
      w.Key(key);
      w.StartObject();
      std::optional<double> sum = 0.;
      for (const auto& [term, value] : terms) {
        real(w, term, value);
        sum = sum && value ? std::optional<double>{*sum + *value} : std::nullopt;
      }
      real(w, "arrival_ps", sum);
      real(w, "required_ps", period ? std::optional<double>{*period / 2} : std::nullopt);
      real(w, "slack_ps", sum && period ? std::optional<double>{*period / 2 - *sum} : std::nullopt);
      real(w, "period_ps", sum ? std::optional<double>{2 * *sum} : std::nullopt);
      text(w, "status", sum ? "measured" : "unmeasured");
      w.EndObject();
    };
    const auto delays = variant_delays(stage, variant);
    check("internal",
          {
              {        "eval_delay_ps",                               delays.eval_delay_ps},
              {     "setup_capture_ps", variant ? variant->setup_capture_ps : std::nullopt},
              {"uncertainty_second_ps",                        stage.uncertainty_second_ps}
    });
    check("precharge",
          {
              {       "precharge_ps",       delays.precharge_ps},
              {"precharge_margin_ps", stage.precharge_margin_ps}
    });
    w.Key("output");
    w.StartObject();
    if (const auto q = instance.pins.find(kDominoPins.output); q != instance.pins.end()) {
      const auto& pin   = all.pins[q->second];
      const auto  load  = larger(pin.transitions[0].load_ff, pin.transitions[1].load_ff);
      const auto  slew  = larger(pin.transitions[0].slew_ps, pin.transitions[1].slew_ps);
      const auto  t_cq  = larger(pin.transitions[0].arrival_ps, pin.transitions[1].arrival_ps);
      const auto* limit = pin_model[q->second];
      real(w, "load_ff", load);
      real(w, "slew_ps", slew);
      real(w, "t_cq_ps", t_cq);
      real(w, "max_capacitance_ff", limit ? limit->max_capacitance_ff : std::nullopt);
      real(w, "max_transition_ps", limit ? limit->max_transition_ps : std::nullopt);
      number(w, "violations", std::count_if(violations.begin(), violations.end(), [&](const auto& v) {
               return v.pin == pin.name;
             }));
    }
    w.EndObject();
    text(w, "hold", "unknown");
    w.EndObject();
  }
  w.EndArray();

  w.Key("endpoints");
  w.StartArray();
  for (size_t i = 0; i < all.pins.size(); ++i) {
    if (all.pins[i].endpoint && (owner[i].empty() || !instances.at(owner[i]).domino)) {
      endpoint(i);
    }
  }
  w.EndArray();

  w.Key("clock_accounting");
  w.StartObject();
  text(w,
       "basis",
       "ideal pre-CTS clock: clock-pin capacitances (domino CLK is a labeled estimate) and the placement RC wire estimate; "
       "no CTS power or skew claim; kept separate from HPWL");
  w.Key("clocks");
  w.StartObject();
  for (const auto& [name, account] : clock_accounts) {
    w.Key(name.c_str());
    w.StartObject();
    number(w, "domino_sinks", account.domino_sinks);
    number(w, "cmos_sinks", account.cmos_sinks);
    number(w, "missing_capacitance", account.missing);
    real(w, "sink_cap_ff", account.missing ? std::nullopt : std::optional<double>{account.sink_ff});
    real(w, "domino_cap_ff", account.domino_ff);
    real(w, "cmos_cap_ff", account.cmos_ff);
    real(w, "net_load_ff", account.net_load_ff);
    real(w, "wire_cap_ff", account.wire_ff);
    text(w,
         "wire_source",
         !parasitics       ? "no placement RC"
         : account.wire_ff ? "placement RC estimate"
                           : "placement RC has no clock net");
    w.Key("masters");
    w.StartObject();
    for (const auto& [master, entry] : account.masters) {
      w.Key(master.c_str());
      w.StartObject();
      number(w, "sinks", entry.first);
      real(w, "cap_ff", entry.second);
      w.EndObject();
    }
    w.EndObject();
    // Every clock sink: the instance's clock pin, its master (a domino
    // master names its variant) and its capacitance.
    w.Key("sinks");
    w.StartArray();
    for (const auto& clock_sink : account.sinks) {
      w.StartObject();
      text(w, "pin", clock_sink.pin);
      text(w, "master", clock_sink.master);
      text(w, "kind", clock_sink.domino ? "domino" : "cmos");
      real(w, "cap_ff", clock_sink.cap_ff);
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndObject();
  real(w, "c_clk_ff", out.c_clk_ff);
  w.EndObject();

  w.Key("electrical");
  w.StartObject();
  number(w, "max_capacitance_violations", std::count_if(violations.begin(), violations.end(), [](const auto& v) {
           return v.kind == "max_capacitance";
         }));
  number(w, "max_transition_violations", std::count_if(violations.begin(), violations.end(), [](const auto& v) {
           return v.kind == "max_transition";
         }));
  w.Key("violations");
  w.StartArray();
  for (const auto& v : violations) {
    w.StartObject();
    text(w, "pin", v.pin);
    text(w, "kind", v.kind);
    real(w, "value", v.value);
    real(w, "limit", v.limit);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();

  w.Key("area");
  w.StartObject();
  real(w, "lef_footprint_um2", footprint);
  real(w, "domino_footprint_um2", domino_footprint);
  real(w, "liberty_um2", liberty_area);
  text(w, "liberty_basis", "Liberty cell areas, domino cells at their Liberty-basis estimate");
  number(w, "domino_instances", domino_instances);
  w.EndObject();
  w.EndObject();
  out.json = std::string(buffer.GetString()) + "\n";
  if (options.require_complete) {
    require_complete(out);
  }
  return out;
}

Stage_timer::Stage_timer(Stage_timer_inputs inputs) : inputs_(std::move(inputs)) {
  library_ = std::make_shared<const model::Library>(model::parse(inputs_.liberty));
  inputs_.liberty.clear();  // parsed; the text is not needed again
  if (inputs_.bundle) {
    std::string why;
    view_ = read_domino_timing(*inputs_.bundle, &why);
    if (!view_) {
      throw std::runtime_error(why);
    }
    apply_overrides(*view_, inputs_.overrides);
    cells_ = domino_timing_cells(*view_, *library_, &generation_, nullptr);
  } else if (!inputs_.overrides.empty()) {
    throw std::invalid_argument("stage coefficient overrides need a bundle");
  }
}

Stage_timing Stage_timer::evaluate(const hhds::Graph& graph, const physical::Parasitics* parasitics,
                                   const physical::Floorplan* floorplan, Stage_timing_options options) const {
  hhds::GraphLibrary           bits;
  std::shared_ptr<hhds::Graph> imported;
  const hhds::Graph*           timed = &graph;
  if (word_valued_ports(graph)) {
    if (!floorplan || !inputs_.technology) {
      throw std::invalid_argument("the netlist keeps word-valued ports: its bit-level import needs the placed floorplan and LEFs");
    }
    imported        = physical::mapped_graph(bits, *floorplan, *inputs_.technology);
    timed           = imported.get();
    options.netlist = kWordPortNetlist;
  }
  options.model_generation = generation_;
  if (inputs_.technology) {
    options.dbu = inputs_.technology->dbu;
  }
  auto source = stage_constraints(*timed, inputs_.sdc, inputs_.design, inputs_.units, inputs_.clock, inputs_.clock_uncertainty_ps);
  return evaluate_stage_timing(*timed, parasitics, library_, cells_, view(), std::move(source), 0, options);
}

void require_complete(const Stage_timing& timing) {
  if (timing.status == "complete") {
    return;
  }
  std::string message = "stage timing coverage is " + timing.status;
  for (const auto& reason : timing.reasons) {
    message += "; " + reason;
  }
  throw std::runtime_error(message);
}
}  // namespace livehd::domplace::timing
