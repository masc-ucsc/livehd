// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "preroute_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <tuple>

#include "attrs.hpp"
#include "core/hash_util.hpp"
#include "dreamplace/native/placement.hpp"
#include "inou/lefdef/route_store.hpp"
#include "memory_proxy.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/partition/flatten.hpp"
#include "placement_technology.hpp"
#include "prects_networks.hpp"
#include "preroute.hpp"
#include "preroute_repair.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "track_capacity.hpp"

namespace livehd::placement {
namespace {
std::string json_text(const rapidjson::Value& value) {
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  value.Accept(writer);
  return buffer.GetString();
}
void write_text(const std::string& path, const std::string& text) {
  std::ofstream file(path);
  if (!(file << text << '\n')) {
    throw std::runtime_error("cannot write pre-route output: " + path);
  }
}
}  // namespace
void preroute_placed(Result& placed, const Options& options, const Evaluate& evaluate) {
  if (!options.preroute.enabled) {
    return;
  }
  if (options.satlegalize_route && !options.satlegalize) {
    throw std::invalid_argument("satlegalize.route requires satlegalize");
  }
  if (options.satlegalize_route_vias && !options.satlegalize_route) {
    throw std::invalid_argument("satlegalize.route.vias requires satlegalize.route");
  }
  if (options.satlegalize_pg_after && !options.satlegalize_route) {
    throw std::invalid_argument("satlegalize.route.pg_after requires satlegalize.route");
  }
  const auto& cfg = options.preroute;
  if (!placed.graph || !placed.parasitics || !placed.preroute_design || !options.whatif || !evaluate || !options.full
      || !options.handoff_taps || options.pg == "none" || options.pg == "off" || placed.floorplan.special_nets.empty()) {
    throw std::invalid_argument("pre-routing requires completed final placement, timing, taps and native power grid");
  }
  if (cfg.budget_ms <= 0 || cfg.stage_budget_ms <= 0 || cfg.bins <= 0 || cfg.bins > 512 || !std::isfinite(cfg.setup_margin_ps)
      || cfg.setup_margin_ps < 0 || !std::isfinite(cfg.hold_margin_ps) || (cfg.hold_margin_ps < 0 && cfg.hold_margin_ps != -1)
      || !std::isfinite(cfg.nominal_slew_ps) || cfg.nominal_slew_ps < 0 || !std::isfinite(cfg.capacity_fraction)
      || cfg.capacity_fraction < 0 || cfg.capacity_fraction > 1 || cfg.max_rows < 0 || cfg.max_sites < 0) {
    throw std::invalid_argument("invalid pre-route budgets, grid or margins");
  }
  auto tech = physical::read_lef(options.lefs);
  memory_proxies(*placed.graph, tech, options.liberty);
  const auto&      tech_profile   = physical::resolve_tech_profile(tech, options.pg);
  auto&            timer          = *options.whatif;
  const auto       initial_timing = evaluate(placed);  // Repaired netlist and final legalized pin coordinates.
  Preroute_options route;
  route.tech_profile           = tech_profile.name;
  route.per_net_budget         = std::chrono::milliseconds(cfg.budget_ms);
  route.timing.setup_margin_ps = cfg.setup_margin_ps;
  route.timing.hold_margin_ps  = cfg.hold_margin_ps;
  std::string buffer_name;
  double      nominal_load = 0;
  if (cfg.hold_margin_ps < 0) {
    buffer_name = cfg.hold_buffer == "auto" ? tech_profile.hold_buffer : cfg.hold_buffer;
    bool found  = false;
    for (const auto& cell : liberty::scan_comb_cells(options.liberty)) {
      if (cell.name != buffer_name) {
        continue;
      }
      if (cell.inputs.size() != 1 || liberty::cell_truth(cell) != 0xaaaaaaaaaaaaaaaaULL) {
        throw std::invalid_argument("HOLD_BUF must be a characterized noninverting buffer");
      }
      const auto cap = timer.pin_capacitance(cell.name, cell.inputs.front());
      if (!cap || !std::isfinite(*cap) || *cap <= 0) {
        throw std::invalid_argument("HOLD_BUF nominal fanout-one load is uncharacterized");
      }
      nominal_load = *cap;
      route.timing.hold_margin_ps
          = preroute_hold_margin(timer, cell.name, cell.inputs.front(), cell.output, cfg.nominal_slew_ps, nominal_load);
      found = true;
      break;
    }
    if (!found) {
      throw std::invalid_argument("HOLD_BUF absent from Liberty; specify preroute.hold_buffer or hold_margin");
    }
  }
  const auto& layout                   = placed.preroute_design->design.layout;
  route.capacity.bounds                = {layout.xl, layout.yl, layout.xh, layout.yh};
  route.capacity.max_preroute_fraction = cfg.capacity_fraction;
  route.max_rows                       = cfg.max_rows;
  route.max_sites                      = cfg.max_sites;
  route.capacity.bins_x = route.capacity.bins_y = cfg.bins;
  route.demand.bins_x = route.demand.bins_y = cfg.bins;
  route.demand.model                        = DreamPlace::native::RudyModel::rsmt;
  route.demand.macro_blockages = route.demand.pin_access = true;
  route.demand.degree                                    = options.route_degree;
  route.demand.accuracy                                  = options.route_accuracy;
  route.demand.initialize                                = initialize_route_tables;
  route.straight_only                                    = options.satlegalize_route;
  route.critical_fraction                                = options.satlegalize_route_critical_fraction;
  route.capacity.count_only_usable_tracks                = route.straight_only;
  if (route.straight_only) {
    for (const auto& net : placed.floorplan.nets) {
      std::set<std::string> pins;
      for (const auto& pin : net.connections) {
        pins.insert(pin.instance + ":" + pin.pin);
      }
      const auto witness = placed.preroute_design->sat_routes.find(pins);
      if (net.connections.size() == 2 && witness != placed.preroute_design->sat_routes.end()) {
        route.straight_routes.emplace(net.name, witness->second);
      }
    }
  }
  // Straight pin-layer segments are accounted alongside the signal layers.
  const auto route_layers    = signal_route_profile(tech_profile, tech);
  auto       capacity_layers = route_layers.signal_layers;
  if (route.straight_only) {
    capacity_layers.insert(capacity_layers.begin(), route_layers.access.pin);
  }
  route.capacity.layers = capacity_layers;
  for (const auto& name : capacity_layers) {
    const auto& layer      = tech.layers.at(name);
    const bool  horizontal = layer.direction == "HORIZONTAL" && layer.pitch_y > 0;
    const bool  vertical   = layer.direction == "VERTICAL" && layer.pitch_x > 0;
    if (!horizontal && !vertical) {
      throw std::invalid_argument("pre-route signal layer lacks preferred pitch");
    }
  }
  std::tie(route.demand.horizontal_capacity, route.demand.vertical_capacity) = lef_track_capacity(tech, capacity_layers);
  std::optional<Result> signal_first;
  if (options.satlegalize_pg_after) {
    route.deferred_pg           = power_grid_profile(tech_profile);
    route.deferred_pg->channels = options.pg_channels;
    signal_first                = placed;
    auto&                 base  = signal_first->floorplan;
    std::set<std::string> supplies;
    for (const auto& net : base.special_nets) {
      supplies.insert(net.name);
    }
    std::erase_if(base.terminals, [&](const auto& terminal) { return supplies.contains(terminal.net); });
    const auto via_names = [](const auto& nets) {
      std::set<std::string> names;
      for (const auto& net : nets) {
        for (const auto& wire : net.wires) {
          for (const auto& path : wire.paths) {
            for (const auto& step : path.steps) {
              if (const auto* via = std::get_if<physical::Route_via>(&step)) {
                names.insert(via->name);
              }
            }
          }
        }
      }
      return names;
    };
    const auto pg_vias = via_names(base.special_nets), signal_vias = via_names(base.nets);
    std::erase_if(base.vias, [&](const auto& via) { return pg_vias.contains(via.name) && !signal_vias.contains(via.name); });
    base.special_nets.clear();
  }
  const auto wire        = technology_route_rc(tech, options.liberty, options.tech_rc_file);
  const auto bindings    = bind_preroute(placed.floorplan, *placed.parasitics, *placed.preroute_design);
  route.timing.deadline  = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.stage_budget_ms);
  const auto route_start = std::chrono::steady_clock::now();
  auto       stage
      = detail::preroute_stage(signal_first ? *signal_first : placed, tech, wire, bindings, timer, options, route, initial_timing);
  auto                candidate = std::move(stage.placed);
  Preroute_result     routed{std::move(candidate.floorplan),
                             *candidate.parasitics,
                             std::move(stage.records),
                             std::move(stage.timings_ms)};
  const double        route_ms = elapsed_ms(route_start);
  rapidjson::Document report(rapidjson::kObjectType);
  auto&               alloc = report.GetAllocator();
  report.AddMember("schema_version", 2, alloc);
  report.AddMember("runtime_ms", route_ms, alloc);
  // Export the optimization view explicitly for matched timing audits. These
  // are transient physical identities, not persistent graph attributes or
  // implicit changes to the downstream signoff constraints.
  const auto       timing_controls = prects_networks(tech, routed.frame, candidate.prects);
  rapidjson::Value timing_cut_pins(rapidjson::kArrayType);
  for (const auto& pin : timing_controls.timing_cuts) {
    timing_cut_pins.PushBack(rapidjson::Value(pin.c_str(), alloc), alloc);
  }
  report.AddMember("timing_cut_pins", timing_cut_pins, alloc);
  report.AddMember("satlegalize_route", route.straight_only, alloc);
  report.AddMember("satlegalize_route_vias", options.satlegalize_route_vias, alloc);
  report.AddMember("satlegalize_route_critical_fraction", route.critical_fraction, alloc);
  report.AddMember("pg_after_sat_routes", bool(route.deferred_pg), alloc);
  report.AddMember("satlegalize_selected_nets", uint64_t(route.straight_routes.size()), alloc);
  report.AddMember("source_mapped_nets", uint64_t(bindings.size()), alloc);
  report.AddMember("repeater_trees", uint64_t(stage.repeater_trees), alloc);
  report.AddMember("repeater_cells", uint64_t(stage.repeater_cells), alloc);
  rapidjson::Value repeater_limit;
  if (stage.repeater_length_limit_um) {
    repeater_limit.SetDouble(*stage.repeater_length_limit_um);
  }
  report.AddMember("repeater_length_limit_um", repeater_limit, alloc);
  report.AddMember("repeater_candidates", uint64_t(stage.repeater_candidates), alloc);
  report.AddMember("repeater_margin_eligible", uint64_t(stage.repeater_margin_eligible), alloc);
  report.AddMember("repeater_attempted", uint64_t(stage.repeater_attempted), alloc);
  report.AddMember("repeater_classification_complete", stage.repeater_classification_complete, alloc);
  report.AddMember("setup_margin_ps", route.timing.setup_margin_ps, alloc);
  report.AddMember("hold_margin_ps", route.timing.hold_margin_ps, alloc);
  report.AddMember("capacity_fraction", cfg.capacity_fraction, alloc);
  report.AddMember("max_rows", cfg.max_rows, alloc);
  report.AddMember("max_sites", cfg.max_sites, alloc);
  report.AddMember("hold_buffer", rapidjson::Value(buffer_name.c_str(), alloc), alloc);
  report.AddMember("nominal_slew_ps", cfg.nominal_slew_ps, alloc);
  report.AddMember("nominal_load_ff", nominal_load, alloc);
  rapidjson::Value records(rapidjson::kArrayType);
  size_t           accepted = 0, attempted = 0, via_count = 0;
  using Counts = std::array<size_t, 3>;  // mapped nets, search attempted, accepted
  std::map<std::string, Counts>   pin_buckets, length_buckets;
  std::map<std::string, uint64_t> rejections;
  std::map<std::string, double>   layer_lengths;
  // A string-keyed map of lengths, times or counts as one JSON object.
  const auto                      map_json = [&](const auto& values) {
    rapidjson::Value object(rapidjson::kObjectType);
    for (const auto& [key, value] : values) {
      object.AddMember(rapidjson::Value(key.c_str(), alloc), rapidjson::Value(value), alloc);
    }
    return object;
  };
  report.AddMember("phase_runtime_ms", map_json(routed.timings_ms), alloc);
  for (const auto& record : routed.records) {
    rapidjson::Value entry(rapidjson::kObjectType);
    entry.AddMember("net", rapidjson::Value(record.net.c_str(), alloc), alloc);
    entry.AddMember("pin_count", uint64_t(record.pin_count), alloc);
    entry.AddMember("attempted", record.attempted, alloc);
    if (record.attempted) {
      entry.AddMember("runtime_ms", map_json(record.timings_ms), alloc);
    }
    entry.AddMember("estimated_length_um",
                    std::isfinite(record.estimated_length_um) ? rapidjson::Value(record.estimated_length_um) : rapidjson::Value(),
                    alloc);
    entry.AddMember("layer_wire_length_um", map_json(record.layer_wire_length_um), alloc);
    entry.AddMember("accepted", record.accepted, alloc);
    if (record.timing_at_acceptance) {
      const auto&      trial = *record.timing_at_acceptance;
      rapidjson::Value timing(rapidjson::kObjectType);
      const auto       metric = [&](const char* key, const std::optional<double>& value) {
        timing.AddMember(rapidjson::Value(key, alloc),
                         value && std::isfinite(*value) ? rapidjson::Value(*value) : rapidjson::Value(),
                         alloc);
      };
      metric("net_setup_slack_ps", trial.net_setup_slack_ps);
      metric("net_hold_slack_ps", trial.net_hold_slack_ps);
      metric("including_locked_setup_slack_ps", trial.setup_slack_ps);
      metric("including_locked_hold_slack_ps", trial.hold_slack_ps);
      entry.AddMember("timing_at_acceptance", timing, alloc);
    }
    entry.AddMember("reason", rapidjson::Value(record.reason.c_str(), alloc), alloc);
    if (!record.capacity_detail.empty()) {
      entry.AddMember("capacity_detail", rapidjson::Value(record.capacity_detail.c_str(), alloc), alloc);
    }
    entry.AddMember("wire_length_um", record.wire_length_um, alloc);
    entry.AddMember("vias", uint64_t(record.vias), alloc);
    entry.AddMember("patterns", uint64_t(record.patterns), alloc);
    entry.AddMember("critical_layer_search", record.critical_layer_search, alloc);
    entry.AddMember("layer_candidates", uint64_t(record.layer_candidates), alloc);
    records.PushBack(entry, alloc);
    accepted                 += record.accepted;
    attempted                += record.attempted;
    via_count                += record.vias;
    const auto pin_bucket     = record.pin_count <= 2    ? "2"
                                : record.pin_count <= 4  ? "3-4"
                                : record.pin_count <= 8  ? "5-8"
                                : record.pin_count <= 16 ? "9-16"
                                                         : "17+";
    const auto length         = record.estimated_length_um;
    const auto length_bucket  = !std::isfinite(length) || length < 0 ? "unknown"
                                : length < 1                         ? "[0,1)"
                                : length < 10                        ? "[1,10)"
                                : length < 100                       ? "[10,100)"
                                                                     : "[100,inf)";
    for (auto* counts : {&pin_buckets[pin_bucket], &length_buckets[length_bucket]}) {
      ++(*counts)[0];
      (*counts)[1] += record.attempted;
      (*counts)[2] += record.accepted;
    }
    if (!record.accepted) {
      ++rejections[record.reason];
    }
    for (const auto& [layer, length_um] : record.layer_wire_length_um) {
      layer_lengths[layer] += length_um;
    }
  }
  const auto buckets_json = [&](const auto& buckets) {
    rapidjson::Value object(rapidjson::kObjectType);
    for (const auto& [bucket, counts] : buckets) {
      rapidjson::Value entry(rapidjson::kObjectType);
      entry.AddMember("nets", uint64_t(counts[0]), alloc);
      entry.AddMember("attempted", uint64_t(counts[1]), alloc);
      entry.AddMember("accepted", uint64_t(counts[2]), alloc);
      entry.AddMember("accepted_fraction_of_attempted",
                      counts[1] ? rapidjson::Value(double(counts[2]) / counts[1]) : rapidjson::Value(),
                      alloc);
      object.AddMember(rapidjson::Value(bucket.c_str(), alloc), entry, alloc);
    }
    return object;
  };
  report.AddMember("mapped_nets", uint64_t(routed.records.size()), alloc);
  report.AddMember("attempted_nets", uint64_t(attempted), alloc);
  report.AddMember("accepted_nets", uint64_t(accepted), alloc);
  report.AddMember("vias", uint64_t(via_count), alloc);
  report.AddMember("layer_wire_length_um", map_json(layer_lengths), alloc);
  report.AddMember("by_pin_count", buckets_json(pin_buckets), alloc);
  report.AddMember("by_estimated_length_um", buckets_json(length_buckets), alloc);
  report.AddMember("rejections", map_json(rejections), alloc);
  report.AddMember("nets", records, alloc);
  // Diagnostic provenance for matched controls. Production handoff locks still
  // come from DEF; this report is not needed to preserve fixed wiring.
  const auto status_name = [](Ann_place::Status status) -> const char* {
    switch (status) {
      case Ann_place::Status::UNPLACED: return "UNPLACED";
      case Ann_place::Status::PLACED  : return "PLACED";
      case Ann_place::Status::FIXED   : return "FIXED";
      case Ann_place::Status::LOCKED  : return "LOCKED";
    }
    throw std::invalid_argument("invalid pre-route placement status");
  };
  std::map<std::string, Ann_place> locks;
  rapidjson::Value                 endpoint_locks(rapidjson::kArrayType);
  if (routed.frame.components.size() < placed.floorplan.components.size()) {
    throw std::invalid_argument("pre-routing removed original components");
  }
  for (size_t i = 0; i < routed.frame.components.size(); ++i) {
    const auto& cell     = routed.frame.components[i];
    const bool  inserted = i >= placed.floorplan.components.size();
    if (!inserted && cell.name != placed.floorplan.components[i].name) {
      throw std::invalid_argument("pre-routing changed original component order");
    }
    if (!inserted && cell.place.status == placed.floorplan.components[i].place.status) {
      continue;
    }
    if (!locks.emplace(cell.name, cell.place).second) {
      throw std::invalid_argument("duplicate pre-route endpoint lock");
    }
    rapidjson::Value entry(rapidjson::kObjectType);
    entry.AddMember("instance", rapidjson::Value(cell.name.c_str(), alloc), alloc);
    entry.AddMember("inserted", inserted, alloc);
    entry.AddMember(
        "original_status",
        inserted ? rapidjson::Value() : rapidjson::Value(status_name(placed.floorplan.components[i].place.status), alloc),
        alloc);
    entry.AddMember("final_status", rapidjson::Value(status_name(cell.place.status), alloc), alloc);
    endpoint_locks.PushBack(entry, alloc);
  }
  report.AddMember("endpoint_locks", endpoint_locks, alloc);
  rapidjson::Document qor;
  qor.Parse(placed.qor.c_str());
  if (qor.HasParseError() || !qor.IsObject()) {
    throw std::invalid_argument("invalid final placement QoR before pre-routing");
  }
  qor.RemoveMember("preroute");
  qor.AddMember("preroute", rapidjson::Value(report, qor.GetAllocator()), qor.GetAllocator());
  const auto store_timing = [&](const Timing_feedback& feedback) {
    rapidjson::Value timing(rapidjson::kObjectType);
    const auto       metric = [&](const char* key, const std::optional<double>& value) {
      timing.AddMember(rapidjson::Value(key, qor.GetAllocator()),
                       value && std::isfinite(*value) ? rapidjson::Value(*value * feedback.time_unit_ps) : rapidjson::Value(),
                       qor.GetAllocator());
    };
    metric("wns_ps", feedback.wns);
    metric("tns_ps", feedback.tns);
    metric("hold_wns_ps", feedback.hold_wns);
    qor.RemoveMember("preroute_timing");
    qor.AddMember("preroute_timing", timing, qor.GetAllocator());
  };
  // Rejected trials roll back their timing journal and leave the source design
  // unchanged. Preserve that graph/DEF/RC instead of cloning, serializing and
  // re-timing it merely to publish the rejection census.
  if (accepted == 0 && stage.repeater_cells == 0 && candidate.graph == placed.graph && !route.deferred_pg) {
    store_timing(initial_timing);
    auto updated_qor = json_text(qor);
    write_text(options.output_dir + "/preroute.json", json_text(report));
    write_text(options.output_dir + "/qor.json", updated_qor);
    placed.qor = std::move(updated_qor);
    placed.preroute_design.reset();
    return;
  }
  // A stage without accepted repeaters still shares the source graph. Clone it
  // before applying endpoint locks, so failed emission cannot alter the input.
  if (candidate.graph == placed.graph) {
    auto library = std::make_shared<hhds::GraphLibrary>();
    auto graph   = partition::flatten_hierarchy(candidate.graph.get(),
                                                library.get(),
                                                candidate.graph->get_name(),
                                                nullptr,
                                                false,
                                                {},
                                                {},
                                                true);
    if (!graph) {
      throw std::runtime_error("cannot clone pre-route handoff graph");
    }
    candidate.graph      = std::move(graph);
    candidate.fp_library = std::move(library);
  }
  if (routed.parasitics.identity.empty()) {
    routed.parasitics.identity
        = std::format("preroute-{:016x}", hash_util::fnv1a64(physical::serialize_parasitics(routed.parasitics)));
  }
  qor.RemoveMember("rc_identity");
  qor.AddMember("rc_identity", rapidjson::Value(routed.parasitics.identity.c_str(), qor.GetAllocator()), qor.GetAllocator());
  qor.RemoveMember("timing_approximate");
  qor.AddMember("timing_approximate", routed.parasitics.approximate, qor.GetAllocator());
  // Validate every graph binding before mutating the final graph.
  size_t matches = 0;
  for (auto node : candidate.graph->body().nodes()) {
    matches += locks.contains(std::string(node.get_name()));
  }
  if (matches != locks.size()) {
    throw std::invalid_argument("pre-route endpoint lock missing from final graph");
  }
  const auto def = options.output_dir + "/out.def.preroute";
  physical::write_def(routed.frame, def);
  physical::verify_def_roundtrip(routed.frame, physical::read_def(def));
  auto       parasitics = std::make_shared<physical::Parasitics>(std::move(routed.parasitics));
  const auto controls   = prects_networks(options.technology().lef, routed.frame, candidate.prects);
  parasitics->timing_cuts.clear();
  for (const auto& net : parasitics->nets) {
    if (controls.timing_cuts.contains(net.driver)) {
      parasitics->timing_cuts.insert(net.driver);
    }
  }
  for (auto node : candidate.graph->body().nodes()) {
    if (auto lock = locks.find(std::string(node.get_name())); lock != locks.end()) {
      node.attr(attrs::place).set(lock->second);
    }
  }
  physical::publish_routes(*candidate.graph, routed.frame, physical::route_technology_identity(options.lefs));
  candidate.graph->get_input_node().attr(attrs::placement_rc).set(physical::serialize_parasitics(*parasitics));
  candidate.floorplan  = std::move(routed.frame);
  candidate.parasitics = std::move(parasitics);
  if (stage.repeater_cells) {
    for (const auto* key : {"cells", "nets"}) {
      if (!qor.HasMember(key) || !qor[key].IsUint64()) {
        throw std::invalid_argument("missing final placement census before pre-routing");
      }
      qor[key].SetUint64(qor[key].GetUint64() + stage.repeater_cells);
    }
    auto design = candidate.preroute_design->design;
    std::fill(design.net_weights.begin(), design.net_weights.end(), 1);
    qor.RemoveMember("hpwl_um");
    qor.AddMember("hpwl_um", DreamPlace::native::hpwl(design) / tech.dbu, qor.GetAllocator());
  }
  // Evaluate the graph and actual routed RC together before publishing them.
  // Restore the caller's physical session if validation or emission fails.
  try {
    const auto feedback = evaluate(candidate);
    store_timing(feedback);
    candidate.qor = json_text(qor);
    candidate.preroute_design.reset();
    if (route.deferred_pg) {
      const auto reserved = reserve_power_grid_channels(tech, candidate.floorplan, *route.deferred_pg);
      write_power_grid_report(candidate.floorplan, reserved, options.output_dir + "/power_grid.json");
    }
    write_text(options.output_dir + "/preroute.json", json_text(report));
    write_text(options.output_dir + "/qor.json", candidate.qor);
    std::filesystem::rename(def, options.output_dir + "/out.def");
  } catch (...) {
    (void)evaluate(placed);
    throw;
  }
  const auto previous_owner = placed.fp_library;
  placed                    = std::move(candidate);
}
}  // namespace livehd::placement
