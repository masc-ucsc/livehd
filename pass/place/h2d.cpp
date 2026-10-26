// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "h2d.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "dreamplace/native/macro/check.hpp"
#include "dreamplace/native/macro/render.hpp"
#include "dreamplace/native/rudy.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "track_capacity.hpp"
namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace mp = dp::macro;
namespace gu = graph_util;
namespace fs = std::filesystem;
namespace {
std::string read(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot read macro topology cache");
  }
  return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::string& path, std::string_view text) {
  fs::create_directories(fs::path(path).parent_path());
  auto          temp = path + ".tmp";
  std::ofstream out(temp);
  out << text << '\n';
  out.close();
  if (!out) {
    throw std::runtime_error("cannot write macro topology report");
  }
  fs::rename(temp, path);
}
}  // namespace
Macro_hierarchy macro_hierarchy(hhds::Graph& source, const physical::Technology& tech, const std::string& liberty) {
  Macro_hierarchy out;
  out.design = std::string(source.get_name());
  for (const auto& c : liberty::scan_dff_cells(liberty)) {
    out.sequential_masters.insert(c.name);
  }
  for (const auto& c : liberty::scan_async_dff_cells(liberty)) {
    out.sequential_masters.insert(c.name);
  }
  for (const auto& c : liberty::scan_latch_cells(liberty)) {
    out.sequential_masters.insert(c.name);
  }
  auto macro = [&](hhds::Node n) {
    if (gu::type_op_of(n) == Ntype_op::Memory) {
      return true;
    }
    auto io = n.get_subnode_io();
    if (!io) {
      return false;
    }
    auto found = tech.macros.find(std::string(io->get_name()));
    return found != tech.macros.end() && !found->second.klass.starts_with("CORE");
  };
  mp::HierarchyCallbacks cb;
  cb.instance_name = [](auto n) { return gu::default_instance_name(n); };
  cb.opaque        = [&](auto n) {
    auto io = n.get_subnode_io();
    return macro(n) || (io && tech.macros.contains(std::string(io->get_name())));
  };
  cb.kind = [&](auto n) {
    if (macro(n)) {
      return mp::FlowKind::macro;
    }
    auto io = n.get_subnode_io();
    if (gu::is_type_register(n) || (io && out.sequential_masters.contains(std::string(io->get_name())))) {
      return mp::FlowKind::sequential;
    }
    return mp::FlowKind::combinational;
  };
  cb.terminal_name = [](auto p) { return std::string(gu::pin_name_of(p)); };
  out.hierarchy    = mp::read_hierarchy(source, cb);
  return out;
}
dp::Orientation native_orientation(Ann_place::Orientation o) {
  switch (o) {
    case Ann_place::Orientation::N : return dp::Orientation::N;
    case Ann_place::Orientation::FN: return dp::Orientation::FN;
    case Ann_place::Orientation::FS: return dp::Orientation::FS;
    case Ann_place::Orientation::S : return dp::Orientation::S;
    default                        : throw std::invalid_argument("H2D supports N/FN/FS/S, not 90 degree rotations");
  }
}
Ann_place::Orientation physical_orientation(dp::Orientation o) {
  switch (o) {
    case dp::Orientation::N : return Ann_place::Orientation::N;
    case dp::Orientation::FN: return Ann_place::Orientation::FN;
    case dp::Orientation::FS: return Ann_place::Orientation::FS;
    case dp::Orientation::S : return Ann_place::Orientation::S;
  }
  throw std::invalid_argument("invalid native orientation");
}
void write_gp_snapshot(const dp::Design& d, const std::vector<mp::NodeInfo>& info, const std::string& path, int iteration) {
  if (info.size() != d.node_x.size()) {
    throw std::invalid_argument("GP snapshot physical metadata mismatch");
  }
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
  json.StartObject();
  json.Key("stage");
  json.String("global_placement_before_legalization");
  json.Key("coordinate_units");
  json.String("DBU");
  json.Key("iteration");
  json.Int(iteration);
  json.Key("hpwl_dbu");
  json.Double(dp::hpwl(d));
  json.Key("nodes");
  json.StartArray();
  for (size_t i = 0; i < info.size(); ++i) {
    if (info[i].io || d.placement_mask(i) != dp::PlacementMask::none) {
      continue;
    }
    json.StartArray();
    json.String(info[i].name.c_str());
    json.Double(d.node_x[i]);
    json.Double(d.node_y[i]);
    const auto orientation = d.node_orientation.empty() ? dp::Orientation::N : d.node_orientation[i];
    json.Int(static_cast<int>(physical_orientation(orientation)));
    json.EndArray();
  }
  json.EndArray();
  json.EndObject();
  write(path, buffer.GetString());
}
mp::Parameters macro_parameters(const Options& o, const physical::Technology& tech) {
  auto p = o.macro_parameters;
  if (!p.target_density) {
    p.target_density = o.density;
  }
  const auto [horizontal, vertical] = lef_track_capacity(tech);
  if (!p.route_cap_h) {
    p.route_cap_h = horizontal;
  }
  if (!p.route_cap_v) {
    p.route_cap_v = vertical;
  }
  if (!(p.route_cap_h > 0 && p.route_cap_v > 0)) {
    throw std::invalid_argument(
        "H2D RUDY needs LEF routing PITCH or explicit synth.place.macro.route_cap_h/route_cap_v (tracks per DBU)");
  }
  p.validate();
  return p;
}
Macro_run place_macros(dp::Design& d, const std::vector<mp::NodeInfo>& info, const Macro_hierarchy& hierarchy, const Options& o,
                       const std::string& environment) {
  Macro_run out;
  out.environment         = environment;
  const auto        p     = o.macro_parameters;
  const std::string cache = o.cache_dir.empty() ? std::string{} : o.cache_dir + "/macro_topology.json";
  std::string       cache_miss;
  if (!cache.empty() && fs::is_regular_file(cache)) {
    try {
      const auto          text = read(cache);
      rapidjson::Document json;
      json.Parse(text.c_str());
      if (json.HasParseError() || !json.IsObject() || !json.HasMember("environment") || !json["environment"].IsString()
          || json["environment"].GetString() != environment) {
        throw std::invalid_argument("macro environment changed");
      }
      const auto policy = resolve_route_policy(o, o.macro_route);
      if (!json.HasMember("route_model") || !json["route_model"].IsString()) {
        throw std::invalid_argument("cached macro route model missing");
      }
      const std::string model  = json["route_model"].GetString();
      const auto        active = model == "uroute" ? dp::RouteModel::uroute
                                 : model == "rsmt" ? dp::RouteModel::rsmt
                                 : model == "rudy" ? dp::RouteModel::rudy
                                                   : dp::RouteModel::off;
      const bool        latches
          = policy.model == dp::RouteModel::automatic
            || (policy.probe && (policy.model == dp::RouteModel::rudy || policy.probe_model == dp::RouteModel::uroute));
      if ((model != "off" && model != "rudy" && model != "rsmt" && model != "uroute")
          || (latches ? active == dp::RouteModel::off : active != policy.model)) {
        throw std::invalid_argument("cached macro route model inconsistent with policy");
      }
      auto topology  = mp::cached_topology(text, d, info, p);
      auto frozen    = mp::abstract_design(d, info, topology, p, true);
      auto candidate = d;
      mp::expand(frozen, topology, candidate);
      std::vector<int> macros;
      for (int i = 0; i < int(info.size()); ++i) {
        if (info[i].macro) {
          macros.push_back(i);
        }
      }
      if (!mp::check(candidate, macros).legal()) {
        throw std::invalid_argument("cached macro frame violates clearance");
      }
      for (const auto& anchor : topology.anchors) {
        if (anchor.strength.hard) {
          auto single                       = topology;
          single.anchors                    = {anchor};
          single.anchors[0].strength.weight = 1;
          if (mp::anchor_deviation(single, d.layout) > 1e-6) {
            throw std::invalid_argument("cached hard anchor violated");
          }
        }
      }
      for (const auto& edge : topology.order) {
        if (edge.strength.hard) {
          auto single                     = topology;
          single.order                    = {edge};
          single.order[0].strength.weight = 1;
          if (mp::order_violation(single) > 1e-6) {
            throw std::invalid_argument("cached hard order violated");
          }
        }
      }
      out.cache_hit                       = true;
      out.result.topology                 = std::move(topology);
      out.result.resolved                 = p;
      out.result.legal                    = true;
      out.result.placement.route_model    = active;
      out.result.placement.uroute_skipped    = o.uroute_selection.skipped;
      out.result.placement.uroute_sample_ids = o.uroute_selection.sample_ids;
      if (json.HasMember("route_probe_overflow") && json["route_probe_overflow"].IsNumber()) {
        out.result.placement.route_probe_overflow = json["route_probe_overflow"].GetDouble();
      }
      if (json.HasMember("route_uroute_switch_iteration") && json["route_uroute_switch_iteration"].IsInt()) {
        out.result.placement.route_uroute_switch_iteration = json["route_uroute_switch_iteration"].GetInt();
      }
      d = std::move(candidate);
      return out;
    } catch (const std::invalid_argument& e) {  // A stale/malformed cache is never user input.
      cache_miss = e.what();
    } catch (const std::runtime_error& e) {
      cache_miss = e.what();
    }
  }
  mp::Topology requested = o.macro_topology ? *o.macro_topology : mp::Topology{};
  auto         seed      = mp::seed(d, info, hierarchy.hierarchy, p, requested);
  seed.design            = hierarchy.design;
  dp::Options cells;
  cells.route            = resolve_route_policy(o, o.macro_route);
  cells.route_initialize = initialize_route_tables;
  o.uroute_selection.apply(cells);
  if (o.uroute.export_features && cells.route.probe && !o.uroute_stage_identity.top.empty()) {
    cells.route_probe_observer = uroute_probe_exporter(
        fs::path(o.output_dir) / "uroute_export/macro_probe",
        [identity = o.uroute_stage_identity] { return identity; },
        p.route_cap_h,
        p.route_cap_v);
  }
  cells.max_iterations = o.iterations;
  cells.bins_x = cells.bins_y = o.bins;
  cells.num_threads           = o.threads;
  cells.timing_spread_weight  = o.timing_spread_weight;
  cells.target_density        = o.density;
  cells.add_fillers           = false;
  if (o.snapshots) {
    cells.observer_interval = o.iterations + 1;
    cells.observer          = [&, round = 0](const dp::Design& snapshot, dp::PlacementStage stage, int iteration) mutable {
      if (stage == dp::PlacementStage::global_placement) {
        write_gp_snapshot(snapshot, info, o.output_dir + "/h2d_round" + std::to_string(round++) + "_gp.json", iteration);
      }
    };
  }
  out.result = mp::place_h2d(d, info, p, seed, cells);
  if (!cache_miss.empty()) {
    out.result.topology.diagnostics.push_back("macro cache miss: " + cache_miss);
  }
  return out;
}
void write_macro_report(const Macro_run& run, const dp::Design& d, const std::vector<mp::NodeInfo>& info, const Options& o,
                        int64_t dbu_per_um) {
  if (o.output_dir.empty()) {
    return;
  }
  auto result = run.result;
  result.hpwl = dp::hpwl(d);
  dp::RudyOptions route_options{result.resolved.bins,
                                result.resolved.bins,
                                result.resolved.route_cap_h,
                                result.resolved.route_cap_v};
  const auto      policy = resolve_route_policy(o, o.macro_route);
  const bool latches = policy.model == dp::RouteModel::automatic
                       || (policy.probe && (policy.model == dp::RouteModel::rudy || policy.probe_model == dp::RouteModel::uroute));
  const auto active  = latches ? run.result.placement.route_model : policy.model;
  route_options.model
      = active == dp::RouteModel::rsmt || active == dp::RouteModel::uroute ? dp::RudyModel::rsmt : dp::RudyModel::bbox;
  route_options.degree     = policy.degree;
  route_options.accuracy   = policy.accuracy;
  route_options.initialize = initialize_route_tables;
  result.rudy_overflow     = policy.model == dp::RouteModel::off ? 0 : dp::rudy(d, route_options).overflow;
  result.merit
      = result.hpwl + result.resolved.alpha * ((d.layout.xh - d.layout.xl) + (d.layout.yh - d.layout.yl)) * result.rudy_overflow;
  auto                report = mp::report_json(result, d, info);
  rapidjson::Document json;
  json.Parse(report.c_str());
  auto& allocator = json.GetAllocator();
  json.AddMember("environment", rapidjson::Value(run.environment.c_str(), allocator), allocator);
  json.AddMember("cache_hit", run.cache_hit, allocator);
  json.AddMember(
      "route_model",
      rapidjson::Value(dp::route_model_name(policy.model == dp::RouteModel::off ? dp::RouteModel::off : active), allocator),
      allocator);
  json.AddMember("route_score_model",
                 rapidjson::Value(route_options.model == dp::RudyModel::rsmt ? "rsmt" : "rudy", allocator),
                 allocator);
  add_uroute_status(json, allocator, result.placement.uroute_skipped, result.placement.uroute_sample_ids);
  json.AddMember("route_probe_overflow", result.placement.route_probe_overflow, allocator);
  json.AddMember("route_uroute_switch_iteration", result.placement.route_uroute_switch_iteration, allocator);
  json["units"].AddMember("dbu_per_um", dbu_per_um, allocator);
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  json.Accept(writer);
  write(o.output_dir + "/macro_topology.json", buffer.GetString());
  if (!o.cache_dir.empty()) {
    write(o.cache_dir + "/macro_topology.json", buffer.GetString());
  }
  mp::write_png(o.output_dir + "/h2d.png", d, info, result.topology);
}
}  // namespace livehd::placement
