// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "io_timing.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

#include "attrs.hpp"
#include "diag.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/partition/flatten.hpp"
#include "pass/synth/region_cache.hpp"
#include "pass/synth/region_qor.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "technology_rc.hpp"

namespace livehd::placement {
namespace {
std::map<std::string, std::shared_ptr<hhds::Graph>> regions(hhds::Graph& source) {
  std::map<std::string, std::shared_ptr<hhds::Graph>> result;
  for (const auto& graph : source.definitions().graphs()) {
    if (graph->get_input_node().attr(attrs::synth_region).has()) {
      result.emplace(std::string(graph->get_name()), graph);
    }
  }
  return result;
}
std::string stage_name(Placement_stage stage) {
  return stage == Placement_stage::floorplan ? "fp" : stage == Placement_stage::macro ? "macro" : "final";
}
std::string instance(const std::string& pin) {
  const auto colon = pin.rfind(':');
  return colon == std::string::npos ? std::string{} : pin.substr(0, colon);
}
}  // namespace
Io_timing_flow::Io_timing_flow(std::shared_ptr<hhds::Graph> source, const Options& options, std::shared_ptr<synth::Io_store> store,
                               const std::map<std::string, std::string>& mapper_options, Evaluate evaluate, double percent)
    : source_(std::move(source))
    , options_(options)
    , store_(std::move(store))
    , mapper_(synth::make_io_mapper("abc", mapper_options))
    , evaluate_(std::move(evaluate))
    , percent_(percent) {
  if (mapper_.attach_store) {
    mapper_.attach_store(store_);
  }
}

std::map<std::string, synth::Io_environment> Io_timing_flow::harvest(const Result& placed, const Timing_feedback& timing) {
  std::map<std::string, synth::Io_environment> result;
  const auto                                   modules = regions(*source_);
  const auto                                   tech    = physical::read_lef(options_.lefs);
  const auto                                   rc
      = technology_wire_rc(tech, options_.liberty, options_.tech_rc_file, options_.resistance_ohm_um, options_.capacitance_ff_um);
  std::map<std::string, std::set<std::string>> controls;
  auto                                         flops = liberty::scan_dff_cells(options_.liberty);
  for (const auto& c : liberty::scan_async_dff_cells(options_.liberty)) {
    flops.push_back(c);
  }
  for (const auto& c : liberty::scan_latch_cells(options_.liberty)) {
    flops.push_back(c);
  }
  for (const auto& c : flops) {
    for (const auto& name : {c.clk_pin, c.reset0_pin, c.reset1_pin}) {
      if (!name.empty()) {
        controls[c.name].insert(name);
      }
    }
  }
  for (const auto& c : liberty::scan_icg_cells(options_.liberty)) {
    controls[c.name].insert(c.clk_pin);
  }
  for (const auto& c : liberty::scan_memory_cells(options_.liberty)) {
    for (const auto& pin : c.pins) {
      if (pin.clock_input) {
        controls[c.name].insert(pin.name);
      }
    }
  }
  hhds::GraphLibrary            flat_library;
  partition::Flat_origin_map    origins;
  std::unordered_set<hhds::Gid> preserved;
  for (const auto& graph : source_->definitions().graphs()) {
    if (tech.macros.contains(std::string(graph->get_name()))) {
      preserved.insert(graph->get_gid());
    }
  }
  auto flat = partition::flatten_hierarchy(source_.get(), &flat_library, source_->get_name(), &origins, false, preserved, {}, true);
  if (!flat) {
    throw std::runtime_error("cannot map physical timing back to color ports");
  }
  // Instance paths are used only for matching the measured physical pins. The
  // persisted key is the stable region module + port bit, never a flat net name.
  std::map<std::pair<std::string, std::string>, std::vector<std::string>> occurrences;
  std::map<std::string, std::string>                                      owners, masters;
  for (const auto& [node, origin] : origins) {
    const auto module = std::string(origin.src_node.get_graph()->get_name());
    const auto name   = std::string(node.get_name());
    if (modules.contains(module)) {
      occurrences[{module, std::string(origin.src_node.get_name())}].push_back(name);
      owners[name] = module;
    }
    if (auto io = node.get_subnode_io()) {
      masters[name] = std::string(io->get_name());
    }
  }
  // Resizing preserves identities; new repair buffers carry their region ID.
  std::map<uint32_t, std::string> module_ids;
  for (const auto& [name, graph] : modules) {
    if (const auto id = graph->get_input_node().attr(attrs::synth_region_id); id.has()) {
      module_ids[id.get()] = name;
    }
  }
  for (auto node : placed.graph->body().nodes()) {
    if (const auto id = node.attr(attrs::synth_region_id); id.has() && module_ids.contains(id.get())) {
      owners[std::string(node.get_name())] = module_ids.at(id.get());
    }
    if (auto io = node.get_subnode_io()) {
      masters[std::string(node.get_name())] = std::string(io->get_name());
    }
  }
  std::set<std::string> protected_pins;
  for (const auto& net : placed.parasitics->nets) {
    bool protected_net = false;
    for (const auto& node : net.nodes) {
      const auto pin   = timing.pins.find(node.pin);
      const auto colon = node.pin.rfind(':');
      if (pin != timing.pins.end() && pin->second.clock) {
        protected_net = true;
      }
      if (colon != std::string::npos) {
        const auto master = masters.find(node.pin.substr(0, colon));
        if (master != masters.end() && controls[master->second].contains(node.pin.substr(colon + 1))) {
          protected_net = true;
        }
      }
    }
    if (protected_net) {
      for (const auto& node : net.nodes) {
        if (!node.pin.empty()) {
          protected_pins.insert(node.pin);
        }
      }
    }
  }
  for (const auto& [module, graph] : modules) {
    auto& env           = result[module];
    env.wire            = store_->wire(module);
    const auto bindings = mapper_.bindings(*graph);
    for (const auto& binding : bindings) {
      const auto names = occurrences.find({module, binding.instance});
      if (names == occurrences.end()) {
        continue;
      }
      for (const auto& name : names->second) {
        const auto key = name + ":" + binding.pin;
        const auto pin = timing.pins.find(key);
        if (protected_pins.contains(key) || pin == timing.pins.end() || pin->second.clock) {
          continue;
        }
        const auto value = binding.input ? pin->second.arrival : pin->second.required;
        if (!value || !std::isfinite(*value)) {
          continue;
        }
        const auto ps    = *value * timing.time_unit_ps;
        auto&      times = binding.input ? env.inputs : env.required;
        const auto old   = times.find(binding.port);
        if (old == times.end()) {
          times[binding.port] = ps;
        } else {
          old->second = binding.input ? std::max(old->second, ps) : std::min(old->second, ps);
        }
      }
    }
    // Fresh measurements replace the previous stage's connectivity histogram.
    env.wire                   = {};
    env.wire.capacitance_ff_um = (rc.horizontal_c + rc.vertical_c) / 2;
    env.wire.resistance_ohm_um = (rc.horizontal_r + rc.vertical_r) / 2;
  }
  synth::Wire_load global;
  global.capacitance_ff_um = (rc.horizontal_c + rc.vertical_c) / 2;
  global.resistance_ohm_um = (rc.horizontal_r + rc.vertical_r) / 2;
  for (const auto& net : placed.parasitics->nets) {
    if (protected_pins.contains(net.driver)) {
      continue;
    }
    unsigned fanout = 0;
    for (const auto& n : net.nodes) {
      if (!n.pin.empty() && n.pin != net.driver) {
        ++fanout;
      }
    }
    if (!fanout) {
      continue;
    }
    double length = net.wire_length_um;
    if (length <= 0) {
      double xl = INFINITY, yl = INFINITY, xh = -INFINITY, yh = -INFINITY;
      for (const auto& n : net.nodes) {
        if (const auto p = placed.pin_locations.find(n.pin); p != placed.pin_locations.end()) {
          xl = std::min(xl, p->second.first);
          xh = std::max(xh, p->second.first);
          yl = std::min(yl, p->second.second);
          yh = std::max(yh, p->second.second);
        }
      }
      if (std::isfinite(xl)) {
        length = (xh - xl + yh - yl) / tech.dbu;
      }
    }
    if (!std::isfinite(length) || length < 0) {
      continue;
    }
    global.length_um[fanout] += length;
    ++global.samples[fanout];
    const auto owner = owners.find(instance(net.driver));
    if (owner != owners.end()) {
      auto& wire              = result[owner->second].wire;
      wire.length_um[fanout] += length;
      ++wire.samples[fanout];
    }
  }
  for (auto& [fanout, length] : global.length_um) {
    length /= global.samples.at(fanout);
  }
  store_->global_wire = global;
  for (auto& [module, env] : result) {
    (void)module;
    for (auto& [fanout, length] : env.wire.length_um) {
      length /= env.wire.samples.at(fanout);
    }
    if (env.wire.empty()) {
      env.wire = global;
    }
  }
  return result;
}

bool Io_timing_flow::check(const std::string& stage, const std::map<std::string, synth::Io_environment>* measurements,
                           bool automatic) {
  const auto modules = regions(*source_);
  bool       fired = false, accepted = false;
  for (const auto& [module, graph] : modules) {
    const auto identity = synth::mapped_identity(*graph);
    if (measurements) {
      const auto env = measurements->find(module);
      if (env == measurements->end()) {
        continue;
      }
      if (!store_->record(module, identity, stage, env->second)) {
        continue;
      }
    }
    auto row = store_->rows.find(module);
    if (row == store_->rows.end() || row->second.netlist != identity || row->second.stage.empty()) {
      continue;
    }
    const auto environment  = row->second.placed;
    const auto used         = row->second.used;
    auto       decision     = mapper_(*graph, environment, used, store_->period_ps, percent_, automatic && !automatic_used_);
    fired                  |= decision.fired;
    accepted               |= decision.accepted;
    if (decision.accepted) {
      store_->used(module, synth::mapped_identity(*graph), environment);
    }
    if (decision.fired && !automatic) {
      diag::warn("synth", "io-resynth-pending", "timing")
          .msg("color '{}' would resynthesize after {} (slack {} ps, tightening {} ps); rerun lhd synth on the same workdir",
               module,
               stage,
               decision.slack_before_ps,
               decision.delta_ps)
          .emit();
    }
    samples_.push_back({stage, std::move(decision), environment, used, automatic});
  }
  if (fired && automatic) {
    automatic_used_ = true;
  }
  if (accepted) {
    if (mapper_.refine) {
      mapper_.refine(*source_->get_io()->get_library(), std::string(source_->get_name()));
    }
    // Boundary refinement can resize neighbouring colors. Publish those bodies
    // too, retaining every pre-body and recipe gate verbatim.
    for (const auto& [module, graph] : modules) {
      const auto found    = store_->rows.find(module);
      const auto env      = found == store_->rows.end() ? synth::Io_environment{} : found->second.used;
      auto       measured = mapper_(*graph, env, env, store_->period_ps, percent_, false);
      if (found != store_->rows.end()) {
        const auto identity = synth::mapped_identity(*graph);
        if (found->second.netlist != identity) {
          // A neighbour changed during refinement. Its old physical measurement
          // belongs to the previous body; retain what mapping used, then wait
          // for fresh placement before attaching another measured environment.
          store_->used(module, identity, env);
        }
      }
      if (store_->cache && measured.checked) {
        synth::Region_qor q;
        q.module   = module;
        q.area     = measured.area;
        q.gates    = measured.gates;
        q.delay    = measured.wire_delay_ps;
        q.delay_ps = true;
        store_->cache->update_mapped(module, *source_->get_io()->get_library(), q, synth::environment_identity(env));
      }
    }
    if (store_->cache) {
      store_->cache->save();
    }
  }
  store_->save();
  std::filesystem::create_directories(options_.output_dir);
  std::ofstream output(options_.output_dir + "/io_resynth.json");
  if (!(output << report() << '\n')) {
    throw std::runtime_error("cannot save IO resynthesis report");
  }
  return accepted;
}
bool Io_timing_flow::before_placement() { return check("before_placement", nullptr, true); }
bool Io_timing_flow::after_stage(Placement_stage stage, const Result& placed) {
  const bool automatic = first_stage_ && !automatic_used_;
  first_stage_         = false;
  const auto timing    = evaluate_(placed);
  const auto values    = harvest(placed, timing);
  const bool restart   = check(stage_name(stage), &values, automatic);
  if (restart) {
    ++restarted_;
    restarted_stage_ = stage_name(stage);
  }
  return restart;
}
std::string Io_timing_flow::report() const {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
  w.StartObject();
  w.Key("schema");
  w.Int(1);
  w.Key("percent");
  w.Double(percent_);
  w.Key("restarts");
  w.Uint(restarted_);
  w.Key("restarted_stage");
  w.String(restarted_stage_.c_str());
  unsigned checked = 0, fired = 0, accepted = 0, rejected = 0, warnings = 0;
  double   seconds = 0;
  for (const auto& sample : samples_) {
    checked  += sample.decision.checked;
    fired    += sample.decision.fired;
    accepted += sample.decision.accepted;
    rejected += sample.automatic && sample.decision.fired && !sample.decision.accepted;
    warnings += !sample.automatic && sample.decision.fired;
    seconds  += sample.decision.seconds;
  }
  w.Key("checked");
  w.Uint(checked);
  w.Key("fired");
  w.Uint(fired);
  w.Key("accepted");
  w.Uint(accepted);
  w.Key("rejected");
  w.Uint(rejected);
  w.Key("warnings");
  w.Uint(warnings);
  w.Key("seconds");
  w.Double(seconds);
  w.Key("colors");
  w.StartArray();
  for (const auto& sample : samples_) {
    const auto& d = sample.decision;
    w.StartObject();
    w.Key("module");
    w.String(d.module.c_str());
    w.Key("stage");
    w.String(sample.stage.c_str());
    w.Key("decision");
    w.String(d.decision.c_str());
    w.Key("checked");
    w.Bool(d.checked);
    w.Key("automatic");
    w.Bool(sample.automatic);
    w.Key("fired");
    w.Bool(d.fired);
    w.Key("accepted");
    w.Bool(d.accepted);
    w.Key("slack_before_ps");
    w.Double(d.slack_before_ps);
    w.Key("slack_after_ps");
    w.Double(d.slack_after_ps);
    w.Key("tightening_ps");
    w.Double(d.delta_ps);
    w.Key("input");
    w.String(d.input.c_str());
    w.Key("output");
    w.String(d.output.c_str());
    w.Key("no_wire_delay_ps");
    w.Double(d.no_wire_delay_ps);
    w.Key("wire_delay_ps");
    w.Double(d.wire_delay_ps);
    w.Key("used");
    const auto used = synth::describe_environment(sample.used);
    w.RawValue(used.data(), used.size(), rapidjson::kObjectType);
    w.Key("placed");
    const auto placed = synth::describe_environment(sample.placed);
    w.RawValue(placed.data(), placed.size(), rapidjson::kObjectType);
    w.Key("input_bits");
    w.Uint(sample.placed.inputs.size());
    w.Key("output_bits");
    w.Uint(sample.placed.required.size());
    w.Key("seconds");
    w.Double(d.seconds);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buffer.GetString();
}
}  // namespace livehd::placement
