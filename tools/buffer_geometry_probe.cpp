// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Offline geometry profiling; inputs are explicit files, never benchmark paths.
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

#include "inou/lefdef/mapped_graph.hpp"
#include "pass/place/buffering/checkpoint.hpp"
#include "pass/place/buffering/density.hpp"
#include "pass/place/buffering/gap_forces.hpp"
#include "pass/place/buffering/graph_search.hpp"
#include "pass/place/buffering/negotiation.hpp"
#include "pass/place/buffering/physical_sites.hpp"
#include "pass/place/buffering/producer.hpp"
#include "pass/place/sdc_subset.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "zlib.h"

namespace b  = livehd::placement::buffering;
namespace dp = DreamPlace::native;
namespace {
void check(bool ok, const char* message) {
  if (!ok) {
    throw std::invalid_argument(message);
  }
}
std::string read(const std::string& path) {
  if (path.ends_with(".gz")) {
    gzFile input = gzopen(path.c_str(), "rb");
    check(input, "cannot open compressed library");
    std::string result;
    char        bytes[65536];
    int         count = 0;
    while ((count = gzread(input, bytes, sizeof(bytes))) > 0) {
      result.append(bytes, count);
    }
    const int closed = gzclose(input);
    check(count >= 0 && closed == Z_OK, "cannot decompress library");
    return result;
  }
  std::ifstream input(path);
  check(bool(input), "cannot open library");
  return {std::istreambuf_iterator<char>(input), {}};
}
b::Site_rect rect(const rapidjson::Value& value) {
  check(value.IsArray() && value.Size() == 4, "rectangle needs four integer DBU coordinates");
  for (const auto& x : value.GetArray()) {
    check(x.IsInt64(), "noninteger rectangle coordinate");
  }
  return {value[0].GetInt64(), value[1].GetInt64(), value[2].GetInt64(), value[3].GetInt64()};
}
b::Route_point point(const rapidjson::Value& value) {
  check(value.IsArray() && value.Size() == 2 && value[0].IsInt64() && value[1].IsInt64(),
        "point needs two integer DBU coordinates");
  return {value[0].GetInt64(), value[1].GetInt64()};
}
const rapidjson::Value& field(const rapidjson::Value& object, const char* name) {
  check(object.IsObject() && object.HasMember(name), "missing geometry-probe field");
  return object[name];
}
double number(const rapidjson::Value& object, const char* key) {
  const auto& value = field(object, key);
  check(value.IsNumber() && std::isfinite(value.GetDouble()), "model field needs a finite number");
  return value.GetDouble();
}
std::array<double, 2> pair(const rapidjson::Value& value) {
  check(value.IsArray() && value.Size() == 2 && value[0].IsNumber() && value[1].IsNumber(), "timing pair needs two numbers");
  return {value[0].GetDouble(), value[1].GetDouble()};
}
std::string string(const rapidjson::Value& object, const char* key) {
  const auto& v = field(object, key);
  check(v.IsString(), "model field needs a string");
  return v.GetString();
}
b::Context context(const rapidjson::Value& item, const b::Geometry_request& geometry, double dbu) {
  const auto& timing = field(item, "timing_context");
  const auto& driver = field(timing, "driver");
  b::Context  c;
  c.core   = {double(geometry.core.xl) / dbu,
              double(geometry.core.yl) / dbu,
              double(geometry.core.xh) / dbu,
              double(geometry.core.yh) / dbu};
  c.driver = {
      string(driver, "name"),
      {string(driver, "cell"), string(driver, "input"), string(driver, "output")},
      {double(geometry.terminals[0].x) / dbu, double(geometry.terminals[0].y) / dbu},
      pair(field(driver, "input_arrival_ps")),
      pair(field(driver, "input_slew_ps"))
  };
  c.normalization_period_ps = number(timing, "normalization_period_ps");
  const auto& sinks         = field(timing, "sinks");
  check(sinks.IsArray() && sinks.Size() + 1 == geometry.terminals.size(), "timing sink inventory differs from geometry");
  for (size_t i = 0; i < sinks.Size(); ++i) {
    const auto& sink = sinks[rapidjson::SizeType(i)];
    const auto  p    = geometry.terminals[i + 1];
    b::Sink     out;
    out.name             = string(sink, "name");
    out.cell             = string(sink, "cell");
    out.pin              = string(sink, "pin");
    out.position         = {double(p.x) / dbu, double(p.y) / dbu};
    const auto& required = field(sink, "required_ps");
    check(required.IsArray() && required.Size() == 2, "required pair malformed");
    for (unsigned t = 0; t < 2; ++t) {
      check(required[t].IsNull() || required[t].IsNumber(), "required time must be numeric or missing");
      if (required[t].IsNumber()) {
        out.required_ps[t] = required[t].GetDouble();
        out.multiplier[t]  = .5 / sinks.Size();
      }
    }
    c.sinks.push_back(std::move(out));
  }
  if (timing.HasMember("characterized_driver_boundary")) {
    check(timing["characterized_driver_boundary"].IsBool(), "characterized boundary policy must be boolean");
    c.driver_options.characterized_boundary = timing["characterized_driver_boundary"].GetBool();
  }
  if (timing.HasMember("bounded_driver_boundary")) {
    check(timing["bounded_driver_boundary"].IsBool(), "bounded boundary policy must be boolean");
    c.driver_options.bounded_boundary = timing["bounded_driver_boundary"].GetBool();
  }
  return c;
}
const char* status(b::Graph_search_status s) {
  switch (s) {
    case b::Graph_search_status::complete           : return "complete";
    case b::Graph_search_status::unbuffered_retained: return "unbuffered_retained";
    case b::Graph_search_status::incomplete_models  : return "incomplete_models";
    case b::Graph_search_status::budget_exhausted   : return "budget_exhausted";
  }
  return "invalid";
}
const char* status(b::Geometry_status s) { return s == b::Geometry_status::complete ? "complete" : "budget_exhausted"; }
const char* status(b::Route_status s) {
  switch (s) {
    case b::Route_status::connected       : return "connected";
    case b::Route_status::disconnected    : return "disconnected";
    case b::Route_status::budget_exhausted: return "budget_exhausted";
  }
  return "invalid";
}
}  // namespace
int main(int argc, char** argv) {
  try {
    check(argc == 2, "usage: buffer_geometry_probe INPUT.json");
    std::ifstream in(argv[1]);
    check(bool(in), "cannot open geometry input");
    const std::string   text((std::istreambuf_iterator<char>(in)), {});
    rapidjson::Document input;
    input.Parse(text.c_str());
    check(!input.HasParseError() && input.IsObject(), "geometry input must be a JSON object");
    if (input.HasMember("joint_checkpoint_validation")) {
      check(input["joint_checkpoint_validation"].IsObject() && input.HasMember("physical_sites") && input.HasMember("negotiation"),
            "joint validation requires an object, physical sites and negotiation");
    }
    bool validate_gap_forces = false;
    if (input.HasMember("gap_force_validation")) {
      check(input["gap_force_validation"].IsBool(), "gap-force validation must be boolean");
      validate_gap_forces = input["gap_force_validation"].GetBool();
      check(!validate_gap_forces || input.HasMember("negotiation"), "gap-force validation requires negotiation");
    }
    bool validate_density_reservations = false;
    if (input.HasMember("density_reservation_validation")) {
      check(input["density_reservation_validation"].IsBool(), "density reservation validation must be boolean");
      validate_density_reservations = input["density_reservation_validation"].GetBool();
      check(!validate_density_reservations || input.HasMember("negotiation"), "density validation requires negotiation");
    }
    std::vector<b::Site_rect>                     joint_masks;
    std::shared_ptr<livehd::physical::Floorplan>  joint_frame;
    std::shared_ptr<livehd::physical::Technology> joint_technology;
    dp::Design                                    force_design;
    std::map<std::string, size_t>                 force_cell_index;
    const auto&                                   cases = field(input, "cases");
    check(cases.IsArray(), "cases must be an array");
    std::optional<livehd::liberty::timing::Library> library;
    if (input.HasMember("liberty")) {
      const auto& paths = input["liberty"];
      check(paths.IsArray(), "liberty must be an array");
      std::string contents;
      for (const auto& path : paths.GetArray()) {
        check(path.IsString(), "library path must be a string");
        contents += read(path.GetString());
        contents += '\n';
      }
      library = livehd::liberty::timing::parse(contents);
    }
    std::optional<b::Physical_sites>      pool;
    std::optional<b::Site_reservations>   allocator;
    std::vector<b::Physical_buffer_model> physical_models;
    std::vector<std::string>              physical_model_errors;
    size_t                                enabled_vertex_limit = 8;
    b::Site_option_settings               option_settings;
    double                                physical_setup_seconds = 0;
    if (input.HasMember("physical_sites")) {
      check(bool(library), "physical buffer profiling requires timing libraries");
      const auto               started = std::chrono::steady_clock::now();
      const auto&              cfg     = input["physical_sites"];
      std::vector<std::string> paths;
      const auto&              lefs = field(cfg, "lefs");
      check(lefs.IsArray(), "physical LEFs must be an array");
      for (const auto& path : lefs.GetArray()) {
        check(path.IsString(), "LEF path must be a string");
        paths.push_back(path.GetString());
      }
      auto                technology = livehd::physical::read_lef(paths);
      auto                frame      = livehd::physical::read_def(string(cfg, "original_def"));
      rapidjson::Document poses;
      poses.Parse(read(string(cfg, "pose_rc")).c_str());
      check(!poses.HasParseError() && poses.IsObject(), "invalid pose RC JSON");
      const auto& cells = field(poses, "cells");
      check(cells.IsArray() && cells.Size() == frame.components.size(), "pose inventory differs from original signal frame");
      std::map<std::string, livehd::physical::Component*> originals;
      for (auto& cell : frame.components) {
        originals.emplace(cell.name, &cell);
      }
      std::set<std::string> seen;
      for (const auto& entry : cells.GetArray()) {
        check(entry.IsArray() && entry.Size() == 7 && entry[0].IsString() && entry[4].IsString(), "invalid physical pose record");
        const auto found = originals.find(entry[0].GetString());
        check(found != originals.end() && seen.emplace(entry[0].GetString()).second, "unknown/duplicate source pose");
        auto& cell = *found->second;
        check(cell.macro == entry[4].GetString(), "source pose master differs from original");
        for (unsigned i : {1u, 2u, 3u, 5u, 6u}) {
          check(entry[i].IsInt64(), "physical pose field must be integer DBU");
        }
        check(entry[3].GetInt64() >= 0 && entry[3].GetInt64() < 8, "invalid physical pose orientation");
        auto orientation = static_cast<livehd::physical::Orientation>(entry[3].GetInt64());
        if (cell.place.status == livehd::physical::Status::FIXED || cell.place.status == livehd::physical::Status::LOCKED) {
          check(cell.place.x == entry[1].GetInt64() && cell.place.y == entry[2].GetInt64() && cell.place.orientation == orientation,
                "fixed source geometry changed");
        }
        cell.place.x           = entry[1].GetInt64();
        cell.place.y           = entry[2].GetInt64();
        cell.place.orientation = orientation;
        cell.place.width       = entry[5].GetInt64();
        cell.place.height      = entry[6].GetInt64();
      }
      auto extra = livehd::physical::read_def(string(cfg, "extra_occupancy_def"));
      check(extra.dbu == frame.dbu && extra.die == frame.die && extra.components.empty(),
            "physical-only mask frame differs from original");
      std::vector<b::Site_rect> masks;
      for (const auto& blockage : extra.blockages) {
        for (auto rect : blockage.rectangles) {
          check(std::floor(rect.xl) == rect.xl && std::floor(rect.yl) == rect.yl && std::floor(rect.xh) == rect.xh
                    && std::floor(rect.yh) == rect.yh,
                "physical-only mask must be integer DBU");
          masks.push_back({int64_t(rect.xl), int64_t(rect.yl), int64_t(rect.xh), int64_t(rect.yh)});
        }
      }
      if (validate_gap_forces || validate_density_reservations || input.HasMember("joint_checkpoint_validation")) {
        force_design.layout = {double(frame.die.xl), double(frame.die.yl), double(frame.die.xh), double(frame.die.yh), 1, 1};
        // Exact physical component ownership/poses. Gradient evaluation only:
        // no density solve, legalization or physical movement is requested.
        for (bool fixed_pass : {false, true}) {
          for (const auto& cell : frame.components) {
            const bool fixed = cell.place.status == livehd::physical::Status::FIXED
                               || cell.place.status == livehd::physical::Status::LOCKED
                               || !technology.macros.at(cell.macro).klass.starts_with("CORE");
            if (fixed != fixed_pass) {
              continue;
            }
            force_cell_index.emplace(cell.name, force_design.node_x.size());
            force_design.node_x.push_back(cell.place.x);
            force_design.node_y.push_back(cell.place.y);
            force_design.node_size_x.push_back(cell.place.width);
            force_design.node_size_y.push_back(cell.place.height);
          }
          if (!fixed_pass) {
            force_design.num_movable = force_design.node_x.size();
          }
        }
      }
      if (input.HasMember("joint_checkpoint_validation")) {
        joint_masks      = masks;
        joint_frame      = std::make_shared<livehd::physical::Floorplan>(frame);
        joint_technology = std::make_shared<livehd::physical::Technology>(technology);
      }
      pool = b::build_physical_sites(technology, frame, masks);
      allocator.emplace(pool->legal);
      const auto& requested = field(cfg, "models");
      check(requested.IsArray() && !requested.Empty(), "physical buffer models must be a nonempty array");
      for (const auto& model : requested.GetArray()) {
        auto result = b::physical_buffer_model(technology,
                                               *library,
                                               {string(model, "cell"), string(model, "input"), string(model, "output")});
        if (result.model) {
          physical_models.push_back(*result.model);
        } else {
          physical_model_errors.push_back(string(model, "cell") + ": " + result.reason);
        }
      }
      const auto& radius = field(cfg, "max_attachment_dbu");
      check(radius.IsInt64(), "attachment radius needs integer DBU");
      option_settings.max_attachment_dbu     = radius.GetInt64();
      option_settings.max_options_per_vertex = 4;
      if (cfg.HasMember("max_options_per_model_per_vertex")) {
        check(cfg["max_options_per_model_per_vertex"].IsUint64(), "per-model option limit must be nonnegative integer");
        option_settings.max_options_per_model_per_vertex = cfg["max_options_per_model_per_vertex"].GetUint64();
      }
      option_settings.max_seconds = 1;
      physical_setup_seconds      = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    }
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> out(buffer);
    out.StartObject();
    out.Key("scope");
    out.String(pool ? "Bounded real physical-buffer/site/native-timing diagnostic; no graph materialization or whole-design/routed "
                      "acceptance"
               : library
                   ? "Bounded topology/native-timing diagnostic with saved source slew and no physical buffer options; not "
                     "whole-design or routed acceptance"
                   : "Geometry-only diagnostic; one shortest-path topology, no buffer frontier or electrical/routed acceptance");
    if (pool) {
      out.Key("physical_environment");
      out.StartObject();
      out.Key("setup_seconds");
      out.Double(physical_setup_seconds);
      out.Key("rows");
      out.Uint64(pool->legal.size());
      out.Key("occupied_components");
      out.Uint64(pool->occupied_components);
      out.Key("placement_masks");
      out.Uint64(pool->placement_masks);
      out.Key("models");
      out.Uint64(physical_models.size());
      out.Key("model_errors");
      out.StartArray();
      for (const auto& reason : physical_model_errors) {
        out.String(reason.c_str());
      }
      out.EndArray();
      out.EndObject();
    }
    std::vector<b::Allocation_net> allocation_nets;
    std::string                    allocation_cohort;
    b::Allocation_settings         allocation_settings;
    if (input.HasMember("negotiation")) {
      check(pool && library, "negotiation requires physical sites and timing libraries");
      const auto& cfg   = input["negotiation"];
      allocation_cohort = string(cfg, "cohort");
      check(!allocation_cohort.empty(), "negotiation cohort must be explicit");
      allocation_settings.dbu_per_um = pool->dbu;
      if (cfg.HasMember("max_seconds")) {
        check(cfg["max_seconds"].IsNumber(), "negotiation time must be numeric");
        allocation_settings.max_seconds = cfg["max_seconds"].GetDouble();
      }
      if (cfg.HasMember("max_rounds")) {
        check(cfg["max_rounds"].IsUint64(), "negotiation rounds must be an integer");
        allocation_settings.max_rounds = cfg["max_rounds"].GetUint64();
      }
    }
    out.Key("cases");
    out.StartArray();
    for (const auto& item : cases.GetArray()) {
      const auto& name = field(item, "name");
      check(name.IsString(), "case name must be a string");
      std::optional<size_t> allocation_index;
      if (!allocation_cohort.empty() && string(item, "cohort") == allocation_cohort) {
        allocation_index = allocation_nets.size();
        b::Allocation_net net;
        net.owner                 = string(item, "driver");
        net.input_search_complete = false;
        allocation_nets.push_back(std::move(net));
      }
      const auto          owner = allocation_index ? allocation_nets[*allocation_index].owner : std::string("probe");
      b::Geometry_request request;
      request.core          = rect(field(item, "core"));
      const auto& terminals = field(item, "terminals");
      check(terminals.IsArray() && terminals.Size() >= 2, "terminals must include a driver and at least one sink");
      for (const auto& t : terminals.GetArray()) {
        request.terminals.push_back(point(t));
      }
      const auto& obstacles = field(item, "routing_exclusions");
      check(obstacles.IsArray(), "routing exclusions must be an array");
      for (const auto& r : obstacles.GetArray()) {
        request.routing_exclusions.push_back(rect(r));
      }
      if (item.HasMember("sites")) {
        check(item["sites"].IsArray(), "sites must be an array");
        for (const auto& p : item["sites"].GetArray()) {
          request.sites.push_back(point(p));
        }
      }
      const auto& units = field(item, "dbu_per_um");
      check(units.IsNumber(), "dbu_per_um must be numeric");
      const auto& wire = field(item, "directional_rc");
      check(wire.IsArray() && wire.Size() == 4, "explicit directional RC needs four numbers");
      std::array<double, 4> rc;
      for (unsigned i = 0; i < 4; ++i) {
        check(wire[i].IsNumber(), "nonnumeric directional RC");
        rc[i] = wire[i].GetDouble();
      }
      b::Geometry_cache cache;
      auto              built = cache.get(request);
      auto              hit   = cache.get(request);
      out.StartObject();
      out.Key("name");
      out.String(name.GetString(), name.GetStringLength());
      out.Key("fanout");
      out.Uint64(request.terminals.size() - 1);
      out.Key("first_cache_hit");
      out.Bool(built.cache_hit);
      out.Key("geometry_status");
      out.String(status(built.status));
      out.Key("grid_points");
      out.Uint64(built.grid_points);
      out.Key("obstacle_checks");
      out.Uint64(built.obstacle_checks);
      out.Key("build_seconds");
      out.Double(built.seconds);
      out.Key("repeat_seconds");
      out.Double(hit.seconds);
      out.Key("repeat_cache_hit");
      out.Bool(hit.cache_hit);
      if (built.graph) {
        size_t blocked = 0;
        for (auto terminal : built.graph->terminals) {
          blocked += terminal == std::numeric_limits<size_t>::max();
        }
        out.Key("blocked_terminals");
        out.Uint64(blocked);
        out.Key("vertices");
        out.Uint64(built.graph->vertices.size());
        out.Key("edges");
        out.Uint64(built.graph->edges.size());
        auto routed = b::shortest_path_tree(*built.graph, units.GetDouble(), rc);
        out.Key("route_status");
        out.String(status(routed.status));
        out.Key("route_seconds");
        out.Double(routed.seconds);
        out.Key("relaxations");
        out.Uint64(routed.relaxations);
        if (routed.tree) {
          out.Key("tree_vertices");
          out.Uint64(routed.tree->vertices.size());
          out.Key("length_um");
          out.Double(double(routed.length_dbu) / units.GetDouble());
        }
      }
      if (library && built.graph) {
        if (item.HasMember("context_error")) {
          out.Key("graph_status");
          out.String("context_incomplete");
          out.Key("graph_reason");
          check(item["context_error"].IsString(), "context error must be a string");
          out.String(item["context_error"].GetString());
        } else {
          auto                     timing = context(item, request, units.GetDouble());
          b::Graph_search_settings settings;
          settings.tree.dbu_per_um = units.GetDouble();
          settings.max_seconds     = .2;
          std::vector<std::vector<b::Tree_option>> options(built.graph->vertices.size());
          b::Site_reservations                     empty_reservations(b::Legal_sites{});
          const auto*                              reservations              = &empty_reservations;
          bool                                     physical_options_complete = true;
          if (pool) {
            check(pool->dbu == units.GetDouble(), "physical site and graph DBU differ");
            reservations = &*allocator;
            auto route   = b::shortest_path_tree(*built.graph, units.GetDouble(), rc);
            if (route.tree) {
              std::map<std::pair<double, double>, size_t> indices;
              for (size_t i = 0; i < built.graph->vertices.size(); ++i) {
                auto p = built.graph->vertices[i];
                indices.emplace(std::pair{double(p.x) / pool->dbu, double(p.y) / pool->dbu}, i);
              }
              std::vector<size_t>         chosen;
              std::vector<b::Route_point> anchors;
              size_t                      count = std::min(enabled_vertex_limit, route.tree->vertices.size());
              for (size_t i = 0; i < count; ++i) {
                size_t     index = count == 1 ? 0 : i * (route.tree->vertices.size() - 1) / (count - 1);
                auto       p     = route.tree->vertices[index].position;
                const auto id    = indices.at({p.x, p.y});
                chosen.push_back(id);
                anchors.push_back(built.graph->vertices[id]);
              }
              auto generated            = b::physical_site_options(*pool,
                                                        *allocator,
                                                        owner,
                                                        anchors,
                                                        physical_models,
                                                        request.routing_exclusions,
                                                        option_settings);
              physical_options_complete = generated.status == b::Site_option_status::complete && physical_model_errors.empty();
              out.Key("physical_option_status");
              out.String(physical_options_complete ? "complete" : "budget_or_model_incomplete");
              out.Key("physical_option_vertices");
              out.Uint64(chosen.size());
              out.Key("physical_options");
              out.Uint64(generated.emitted);
              out.Key("physical_option_seconds");
              out.Double(generated.seconds);
              out.Key("physical_candidate_checks");
              out.Uint64(generated.candidate_checks);
              out.Key("physical_routing_rejected");
              out.Uint64(generated.routing_rejected);
              std::map<std::string, size_t> emitted_types;
              for (const auto& alternatives : generated.options) {
                for (const auto& option : alternatives) {
                  ++emitted_types[option.buffer.model.cell];
                }
              }
              out.Key("physical_option_types");
              out.StartObject();
              for (const auto& [model_name, model_count] : emitted_types) {
                out.Key(model_name.c_str());
                out.Uint64(model_count);
              }
              out.EndObject();
              for (size_t i = 0; i < chosen.size(); ++i) {
                options[chosen[i]] = std::move(generated.options[i]);
                for (const auto& option : options[chosen[i]]) {
                  for (const auto& use : option.buffer.demand) {
                    timing.capacity[use.resource] = generated.capacity.at(use.resource);
                  }
                }
              }
              settings.tree.max_assignments = 1000;
            }
          }
          auto selected = b::search_graph(*library, timing, *built.graph, options, rc, *reservations, owner, settings);
          if (allocation_index) {
            auto& net                 = allocation_nets[*allocation_index];
            net.context               = timing;
            net.input_search_complete = selected.topology_space_exhausted && physical_options_complete;
            if (selected.best) {
              net.candidates.push_back({selected.best->solution.candidate, selected.best->solution.sites, {}});
            }
            for (const auto& option : selected.frontier) {
              if (!selected.best || option.edges != selected.best->edges
                  || option.solution.choices != selected.best->solution.choices) {
                net.candidates.push_back({option.solution.candidate, option.solution.sites, {}});
              }
            }
            // Keep a direct seed even when a timing/resource frontier omits it.
            // The common evaluator decides electrical feasibility afresh.
            auto route = b::shortest_path_tree(*built.graph, units.GetDouble(), rc);
            if (route.tree) {
              net.candidates.push_back(
                  {b::expand_tree(timing, *route.tree, std::vector<size_t>(route.tree->vertices.size())), {}, {}});
            }
          }
          out.Key("graph_status");
          out.String(physical_options_complete ? status(selected.status) : "budget_exhausted");
          out.Key("electrical_graph_status");
          out.String(status(selected.status));
          out.Key("graph_seconds");
          out.Double(selected.seconds);
          out.Key("graph_states");
          out.Uint64(selected.states);
          out.Key("graph_transitions");
          out.Uint64(selected.transitions);
          out.Key("graph_topologies");
          out.Uint64(selected.trials.size());
          out.Key("graph_assignments");
          out.Uint64(selected.assignments);
          out.Key("graph_fits");
          out.Uint64(selected.waveform_fits);
          out.Key("graph_frontier");
          out.Uint64(selected.frontier.size());
          out.Key("graph_best_exists");
          out.Bool(bool(selected.best));
          out.Key("graph_space_exhausted");
          out.Bool(selected.topology_space_exhausted && physical_options_complete);
          size_t electrical = 0, site_rejected = 0, resource_rejected = 0;
          for (const auto& trial : selected.trials) {
            electrical        += trial.electrical_rejected;
            site_rejected     += trial.site_rejected;
            resource_rejected += trial.resource_rejected;
          }
          out.Key("graph_electrical_rejected");
          out.Uint64(electrical);
          out.Key("graph_site_rejected");
          out.Uint64(site_rejected);
          out.Key("graph_resource_rejected");
          out.Uint64(resource_rejected);
          if (!selected.trials.empty()) {
            const auto& trial = selected.trials.front();
            out.Key("graph_seed_margin_ps");
            if (trial.best_input_margin_ps) {
              out.Double(*trial.best_input_margin_ps);
            } else {
              out.Null();
            }
            out.Key("graph_seed_wire_um");
            if (trial.best_wire_length_um) {
              out.Double(*trial.best_wire_length_um);
            } else {
              out.Null();
            }
          }
          out.Key("graph_unsupported_examples");
          out.StartArray();
          for (const auto& reason : selected.unsupported_examples) {
            out.String(reason.c_str());
          }
          out.EndArray();
          if (selected.best) {
            out.Key("graph_best_buffer_count");
            out.Uint64(selected.best->solution.candidate.buffers.size());
            out.Key("graph_best_buffers");
            out.StartArray();
            for (const auto& cell : selected.best->solution.candidate.buffers) {
              out.StartObject();
              out.Key("name");
              out.String(cell.name.c_str());
              out.Key("master");
              out.String(cell.model.cell.c_str());
              out.Key("body_um");
              out.StartArray();
              for (double value : {cell.body.xl, cell.body.yl, cell.body.xh, cell.body.yh}) {
                out.Double(value);
              }
              out.EndArray();
              out.EndObject();
            }
            out.EndArray();
            out.Key("graph_best_sites");
            out.StartArray();
            for (const auto& site : selected.best->solution.sites) {
              out.StartObject();
              out.Key("row");
              out.String(site.row.c_str());
              out.Key("first");
              out.Uint64(site.first);
              out.Key("count");
              out.Uint64(site.count);
              out.EndObject();
            }
            out.EndArray();
            out.Key("graph_best_wire_um");
            out.Double(selected.best->solution.timing.wire_length_um);
            out.Key("graph_driver_policies");
            out.StartObject();
            out.Key("characterized");
            out.Bool(timing.driver_options.characterized_boundary);
            out.Key("bounded");
            out.Bool(timing.driver_options.bounded_boundary);
            out.EndObject();
            out.Key("graph_best_margin_ps");
            if (selected.best->solution.worst_input_margin_ps) {
              out.Double(*selected.best->solution.worst_input_margin_ps);
            } else {
              out.Null();
            }
          }
        }
      }
      out.EndObject();
    }
    out.EndArray();
    if (!allocation_cohort.empty()) {
      check(!allocation_nets.empty(), "negotiation cohort has no nets");
      const b::Allocation_state original(*allocator);
      auto                      result = b::negotiate(*library, original, allocation_nets, {}, {}, allocation_settings);
      out.Key("negotiation");
      out.StartObject();
      out.Key("scope");
      out.String(
          "Simultaneous finite virtual proposals, explicitly all critical for this diagnostic; no production window/LR "
          "classification, graph commit or whole-design timing acceptance");
      out.Key("cohort");
      out.String(allocation_cohort.c_str());
      out.Key("status");
      out.String(result.status == b::Allocation_status::settled             ? "settled"
                 : result.status == b::Allocation_status::incomplete_models ? "incomplete_models"
                                                                            : "budget_exhausted");
      out.Key("seconds");
      out.Double(result.seconds);
      out.Key("evaluated");
      out.Uint64(result.evaluated);
      out.Key("waveform_fits");
      out.Uint64(result.waveform_fits);
      out.Key("unsupported");
      out.Uint64(result.unsupported);
      out.Key("rejected");
      out.Uint64(result.rejected);
      out.Key("input_search_complete");
      out.Bool(result.input_search_complete);
      out.Key("missing_gap_entries");
      out.Uint64(result.missing_gap_entries);
      out.Key("maximum_pull_fraction");
      out.Double(allocation_settings.maximum_pull_fraction);
      out.Key("original_reservation_owners");
      out.Uint64(original.sites.reservations().size());
      out.Key("reserved_owners");
      out.Uint64(result.proposal.sites.reservations().size());
      out.Key("rounds");
      out.StartArray();
      for (const auto& round : result.rounds) {
        out.StartObject();
        out.Key("round");
        out.Uint64(round.round);
        out.Key("allocated");
        out.Uint64(round.allocated);
        out.Key("unmet");
        out.Uint64(round.unmet);
        out.Key("requested_overlap_sites");
        out.Uint64(round.requested_overlap_sites);
        out.Key("requested_resource_excess");
        out.Uint64(round.requested_resource_excess);
        out.EndObject();
      }
      out.EndArray();
      out.Key("nets");
      out.StartArray();
      for (size_t i = 0; i < result.nets.size(); ++i) {
        const auto& report = result.nets[i];
        const auto& net    = allocation_nets[i];
        out.StartObject();
        out.Key("owner");
        out.String(report.owner.c_str());
        out.Key("classification");
        out.String("critical_diagnostic");
        out.Key("context_present");
        out.Bool(bool(net.context));
        out.Key("input_search_complete");
        out.Bool(net.input_search_complete);
        out.Key("alternatives");
        out.Uint64(net.candidates.size());
        out.Key("candidate");
        if (report.candidate) {
          out.Uint64(*report.candidate);
        } else {
          out.Null();
        }
        out.Key("reason");
        out.String(report.reason.c_str());
        out.Key("mandatory_in_supplied_set");
        out.Bool(report.mandatory);
        out.Key("congestion_charge");
        out.Double(report.congestion_charge);
        out.Key("buffers");
        out.StartArray();
        if (report.candidate) {
          for (const auto& cell : net.candidates[*report.candidate].candidate.buffers) {
            out.StartObject();
            out.Key("name");
            out.String(cell.name.c_str());
            out.Key("master");
            out.String(cell.model.cell.c_str());
            out.Key("body_um");
            out.StartArray();
            for (double v : {cell.body.xl, cell.body.yl, cell.body.xh, cell.body.yh}) {
              out.Double(v);
            }
            out.EndArray();
            out.EndObject();
          }
        }
        out.EndArray();
        out.Key("sites");
        out.StartArray();
        const auto spans = result.proposal.sites.reservations().find(net.owner);
        if (spans != result.proposal.sites.reservations().end()) {
          for (const auto& span : spans->second) {
            out.StartObject();
            out.Key("row");
            out.String(span.row.c_str());
            out.Key("first");
            out.Uint64(span.first);
            out.Key("count");
            out.Uint64(span.count);
            out.EndObject();
          }
        }
        out.EndArray();
        if (report.timing) {
          out.Key("electrically_feasible");
          out.Bool(report.timing->electrically_feasible);
          out.Key("resources_feasible");
          out.Bool(report.timing->resources_feasible);
          out.Key("meets_required");
          if (report.timing->meets_required) {
            out.Bool(*report.timing->meets_required);
          } else {
            out.Null();
          }
          out.Key("root_required_ps");
          out.StartArray();
          for (auto v : report.timing->root_required_ps) {
            if (v) {
              out.Double(*v);
            } else {
              out.Null();
            }
          }
          out.EndArray();
          out.Key("area_um2");
          out.Double(report.timing->area_um2);
          out.Key("wire_length_um");
          out.Double(report.timing->wire_length_um);
        }
        out.EndObject();
      }
      out.EndArray();
      out.Key("pulls");
      out.StartArray();
      for (const auto& pull : result.pulls) {
        out.StartObject();
        out.Key("owner");
        out.String(pull.owner.c_str());
        out.Key("driver_stage");
        out.Uint64(pull.driver_stage);
        out.Key("buffer");
        out.Uint64(pull.buffer);
        out.Key("target_um");
        out.StartArray();
        out.Double(pull.target.x);
        out.Double(pull.target.y);
        out.EndArray();
        out.Key("fraction");
        out.Double(pull.fraction);
        out.EndObject();
      }
      out.EndArray();
      if (validate_gap_forces) {
#if DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
        const auto                 started = std::chrono::steady_clock::now();
        dp::PinTiming              base;
        std::map<std::string, int> drivers;
        const auto                 pin = [&](const std::string& instance, b::Point p) {
          const auto found = force_cell_index.find(instance);
          size_t     node  = 0;
          if (found == force_cell_index.end()) {
            check(instance == "PIN", "force endpoint absent from exact original pose inventory");
            node = force_design.node_x.size();
            force_design.node_x.push_back(p.x * pool->dbu);
            force_design.node_y.push_back(p.y * pool->dbu);
            force_design.node_size_x.push_back(0);
            force_design.node_size_y.push_back(0);
          } else {
            node = found->second;
          }
          const int id = force_design.pin2node.size();
          force_design.pin2node.push_back(node);
          force_design.pin_offset_x.push_back(p.x * pool->dbu - force_design.node_x[node]);
          force_design.pin_offset_y.push_back(p.y * pool->dbu - force_design.node_y[node]);
          force_design.flat_net2pin.push_back(id);
          base.driver.push_back(-1);
          base.weight.push_back(0);
          return id;
        };
        for (const auto& net : allocation_nets) {
          if (!net.context) {
            continue;
          }
          const auto& context = *net.context;
          const int   source  = pin(context.driver.name, context.driver.output);
          drivers.emplace(net.owner, source);
          for (const auto& sink : context.sinks) {
            const auto colon    = sink.name.rfind(':');
            const auto instance = colon == std::string::npos ? std::string("PIN") : sink.name.substr(0, colon);
            const int  target   = pin(instance, sink.position);
            base.driver[target] = source;
            base.weight[target] = 1. / context.sinks.size();
          }
          force_design.flat_net2pin_start.push_back(force_design.flat_net2pin.size());
        }
        force_design.build_maps();
        force_design.pin_timing = base;
        force_design.validate();
        const auto bound
            = b::bind_gap_forces(force_design, base, drivers, result.pulls, pool->dbu, allocation_settings.maximum_pull_fraction);
        force_design.pin_timing  = bound.timing;
        const double gamma       = pool->dbu;
        const auto   gradient    = dp::detail::pin_timing_force(force_design, gamma);
        double       max_error   = 0;
        size_t       differences = 0;
        if (bound.supported_api) {
          std::set<int> nodes;
          for (const auto& [owner, source] : drivers) {
            nodes.insert(force_design.pin2node[source]);
          }
          for (int node : nodes) {
            for (bool horizontal : {true, false}) {
              auto&        coordinate = horizontal ? force_design.node_x[node] : force_design.node_y[node];
              const double saved = coordinate, epsilon = .01;
              coordinate            = saved + epsilon;
              const double high     = dp::detail::pin_timing_force(force_design, gamma).value;
              coordinate            = saved - epsilon;
              const double low      = dp::detail::pin_timing_force(force_design, gamma).value;
              coordinate            = saved;
              const double analytic = horizontal ? gradient.x[node] : gradient.y[node];
              const double error    = std::abs((high - low) / (2 * epsilon) - analytic);
              check(std::isfinite(error), "nonfinite real-cohort force difference");
              max_error = std::max(max_error, error);
              ++differences;
            }
          }
          check(max_error <= 2e-6, "real-cohort gap-force finite difference failed");
        }
        out.Key("gap_force_validation");
        out.StartObject();
        out.Key("scope");
        out.String(
            "Gradient-only diagnostic on exact full component poses/ownership and sampled original pins, uniform per-net sink "
            "weights; no full LR classification, density solve, physical movement or timing acceptance");
        out.Key("supported_api");
        out.Bool(bound.supported_api);
        out.Key("native_api");
        out.Int(DREAMPLACE_NATIVE_PIN_TIMING_API);
        out.Key("component_nodes");
        out.Uint64(force_cell_index.size());
        out.Key("sampled_pins");
        out.Uint64(force_design.pin2node.size());
        out.Key("applied");
        out.Uint64(bound.applied);
        out.Key("virtual_driver");
        out.Uint64(bound.virtual_driver);
        out.Key("unbound_driver");
        out.Uint64(bound.unbound_driver);
        out.Key("fixed_driver");
        out.Uint64(bound.fixed_driver);
        out.Key("no_active_force");
        out.Uint64(bound.no_active_force);
        out.Key("suppressed_pairs");
        out.Uint64(bound.suppressed_pairs);
        out.Key("objective");
        out.Double(gradient.value);
        out.Key("finite_difference_checks");
        out.Uint64(differences);
        out.Key("maximum_gradient_error");
        out.Double(max_error);
        out.Key("seconds");
        out.Double(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        out.EndObject();
#else
        out.Key("gap_force_validation");
        out.StartObject();
        out.Key("supported_api");
        out.Bool(false);
        out.Key("scope");
        out.String("Published dependency lacks the native gap-force API; no force was applied");
        out.EndObject();
#endif
      }
      if (validate_density_reservations) {
        out.Key("density_reservation_validation");
        out.StartObject();
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1
        const auto started = std::chrono::steady_clock::now();
        force_design.build_maps();
        const auto bound = b::bind_reservation_density(force_design, {}, allocation_nets, result, pool->dbu, pool->dbu);
        check(bound.supported_api, "owned density reservation API unavailable");
        force_design.density_reservations = bound.proposal.footprints;
        std::vector<double> exact_map, smooth_map;
        dp::detail::reservation_density_map(force_design, 32, 32, true, 1, exact_map);
        dp::detail::reservation_density_map(force_design, 32, 32, false, 1, smooth_map);
        const double deposited = std::accumulate(exact_map.begin(), exact_map.end(), 0.);
        check(std::abs(deposited - bound.virtual_area) <= 1e-8 * std::max(1., bound.virtual_area),
              "exact reservation map lost physical area");
        out.Key("supported_api");
        out.Bool(true);
        out.Key("component_nodes");
        out.Uint64(force_cell_index.size());
        out.Key("virtual_buffers");
        out.Uint64(bound.virtual_buffers);
        out.Key("materialized_buffers");
        out.Uint64(bound.materialized_buffers);
        out.Key("replaced_nets");
        out.Uint64(bound.replaced);
        out.Key("unmet_nets");
        out.Uint64(bound.unmet);
        out.Key("virtual_area_um2");
        out.Double(bound.virtual_area / pool->dbu / pool->dbu);
        out.Key("exact_deposited_area_um2");
        out.Double(deposited / pool->dbu / pool->dbu);
        out.Key("smooth_deposited_area_um2");
        out.Double(std::accumulate(smooth_map.begin(), smooth_map.end(), 0.) / pool->dbu / pool->dbu);
        out.Key("footprints");
        out.StartArray();
        for (const auto& r : bound.proposal.footprints) {
          out.StartObject();
          out.Key("owner");
          out.String(r.owner.c_str());
          out.Key("body_um");
          out.StartArray();
          for (double v : {r.x, r.y, r.x + r.width, r.y + r.height}) {
            out.Double(v / pool->dbu);
          }
          out.EndArray();
          out.EndObject();
        }
        out.EndArray();
        out.Key("seconds");
        out.Double(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        out.Key("scope");
        out.String(
            "Actual negotiated physical footprints on full original GP20 component poses; native exact/smoothed charge deposition "
            "only. No density solve, physical movement, graph edit, production checkpoint activation or timing acceptance.");
#else
        out.Key("supported_api");
        out.Bool(false);
        out.Key("scope");
        out.String("Published dependency lacks reservation density; no charge was applied");
#endif
        out.EndObject();
      }
      if (input.HasMember("joint_checkpoint_validation")) {
        out.Key("joint_checkpoint_validation");
        out.StartObject();
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 2 && DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
        const auto  started = std::chrono::steady_clock::now();
        const auto& spec    = input["joint_checkpoint_validation"];
        if (spec.HasMember("generate_native_candidates")) {
          check(spec["generate_native_candidates"].IsBool(), "native candidate generation must be boolean");
        }
        rapidjson::Document config;
        config.Parse(read(string(spec, "native_config")).c_str());
        check(!config.HasParseError() && config.IsObject(), "invalid native joint replay config");
        const auto& units = field(config, "sdc_units");
        const auto  constraints
            = livehd::placement::late::read_saved_sdc(read(string(config, "sdc")),
                                                      joint_frame->design,
                                                      {number(units, "time_ps"), number(units, "capacitance_ff")});
        const auto          rc = livehd::physical::deserialize_parasitics_exact(read(string(config, "parasitics")));
        rapidjson::Document poses;
        poses.Parse(read(string(spec, "pin_locations")).c_str());
        check(!poses.HasParseError() && poses.IsObject(), "invalid joint replay pin locations");
        std::map<std::string, std::pair<double, double>> locations;
        for (auto it = poses.MemberBegin(); it != poses.MemberEnd(); ++it) {
          check(it->value.IsArray() && it->value.Size() == 2 && it->value[0].IsNumber() && it->value[1].IsNumber(),
                "invalid joint replay pin point");
          locations.emplace(
              it->name.GetString(),
              std::make_pair(spec.HasMember("generate_native_candidates") && spec["generate_native_candidates"].GetBool()
                                 ? it->value[0].GetDouble()
                                 : std::round(it->value[0].GetDouble()),
                             spec.HasMember("generate_native_candidates") && spec["generate_native_candidates"].GetBool()
                                 ? it->value[1].GetDouble()
                                 : std::round(it->value[1].GetDouble())));
        }
        hhds::GraphLibrary                      graphs;
        const auto                              graph  = livehd::physical::mapped_graph(graphs, *joint_frame, *joint_technology);
        auto                                    models = std::make_shared<livehd::liberty::timing::Library>(*library);
        livehd::placement::late::Driver_options driver;
        if (config.HasMember("characterized_driver_boundary")) {
          check(config["characterized_driver_boundary"].IsBool(), "characterized boundary policy must be boolean");
          driver.characterized_boundary = config["characterized_driver_boundary"].GetBool();
        }
        if (config.HasMember("bounded_driver_boundary")) {
          check(config["bounded_driver_boundary"].IsBool(), "bounded boundary policy must be boolean");
          driver.bounded_boundary = config["bounded_driver_boundary"].GetBool();
        }
        livehd::placement::late::Proxy proxy(*graph, models, constraints);
        const auto                     native_start = std::chrono::steady_clock::now();
        const auto                     timing = proxy.evaluate(&rc, nullptr, {.capture_feasible_graph = true, .driver = driver});
        const double native_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - native_start).count();
        check(timing.certified && timing.achievable_period_ps.has_value(), "joint replay lacks complete native timing coverage");
        livehd::placement::timeclosure::Pin_weight_bank bank;
        const auto                                      lr_start = std::chrono::steady_clock::now();
        const auto                                      weights  = bank.update(timing);
        const double lr_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - lr_start).count();
        dp::Design   design     = force_design;
        design.node_x.resize(force_cell_index.size());
        design.node_y.resize(force_cell_index.size());
        design.node_size_x.resize(force_cell_index.size());
        design.node_size_y.resize(force_cell_index.size());
        design.pin2node.clear();
        design.pin2net.clear();
        design.pin_offset_x.clear();
        design.pin_offset_y.clear();
        design.flat_net2pin.clear();
        design.flat_net2pin_start = {0};
        design.net_weights.clear();
        design.net_mask.clear();
        design.pin_timing.reset();
        design.density_reservations.clear();
        std::map<std::string, int>    physical_pins, drivers;
        std::map<std::string, size_t> io_nodes;
        for (const auto& net : rc.nets) {
          const auto bind_pin = [&](const std::string& name) {
            check(locations.contains(name), "full joint replay RC pin has no geometry");
            const auto colon = name.rfind(':');
            size_t     node  = 0;
            if (colon != std::string::npos) {
              const auto found = force_cell_index.find(name.substr(0, colon));
              check(found != force_cell_index.end(), "full joint replay pin has no original instance");
              node = found->second;
            } else {
              auto [found, inserted] = io_nodes.emplace(name, design.node_x.size());
              node                   = found->second;
              if (inserted) {
                design.node_x.push_back(locations.at(name).first);
                design.node_y.push_back(locations.at(name).second);
                design.node_size_x.push_back(0);
                design.node_size_y.push_back(0);
              }
            }
            const int pin = design.pin2node.size();
            check(physical_pins.emplace(name, pin).second, "duplicate full joint replay physical pin");
            design.pin2node.push_back(node);
            design.pin_offset_x.push_back(locations.at(name).first - design.node_x[node]);
            design.pin_offset_y.push_back(locations.at(name).second - design.node_y[node]);
            design.flat_net2pin.push_back(pin);
            return pin;
          };
          check(!net.driver.empty(), "joint RC net has no driver");
          const int source = bind_pin(net.driver);
          drivers.emplace(net.driver, source);
          size_t pins = 1;
          for (const auto& node : net.nodes) {
            if (!node.pin.empty() && node.pin != net.driver) {
              bind_pin(node.pin);
              ++pins;
            }
          }
          design.flat_net2pin_start.push_back(design.flat_net2pin.size());
          design.net_weights.push_back(pins >= 2 ? 1 : 0);
          design.net_mask.push_back(pins >= 2);
        }
        design.build_maps();
        design.validate();
        dp::PinTiming base;
        base.driver.assign(design.pin2node.size(), -1);
        base.weight.assign(design.pin2node.size(), 0);
        for (const auto& [name, w] : weights.pins) {
          check(physical_pins.contains(name) && drivers.contains(w.driver), "joint native weight lacks full physical binding");
          const int pin    = physical_pins.at(name);
          base.driver[pin] = drivers.at(w.driver);
          base.weight[pin] = w.weight;
        }
        b::Checkpoint_problem problem;
        problem.sites = pool->legal;
        problem.nets  = allocation_nets;
        b::Checkpoint_settings settings;
        settings.start               = 0;
        settings.native_units_per_um = settings.site_dbu_per_um = pool->dbu;
        settings.allocation                                     = allocation_settings;
        settings.generate = [](const b::Checkpoint_input&) { return b::Checkpoint_problem{}; };
        const b::Checkpoint_input frame{design, timing, weights, rc, locations, 20};
        if (spec.HasMember("generate_native_candidates") && spec["generate_native_candidates"].GetBool()) {
          b::Producer_settings policy;
          policy.max_seconds            = 30;
          policy.max_nets               = 32;
          policy.max_fanout             = 4;
          policy.options                = option_settings;
          policy.search.max_assignments = 256;
          const auto& wire              = field(spec, "directional_rc");
          check(wire.IsArray() && wire.Size() == 4, "native generation needs explicit directional technology RC");
          for (unsigned k = 0; k < 4; ++k) {
            check(wire[k].IsNumber(), "native directional RC must be numeric");
            policy.directional_rc[k] = wire[k].GetDouble();
          }
          std::vector<b::Site_rect> opaque_macros;
          for (const auto& cell : joint_frame->components) {
            if (!joint_technology->macros.at(cell.macro).klass.starts_with("CORE")) {
              opaque_macros.push_back(
                  {cell.place.x, cell.place.y, cell.place.x + cell.place.width, cell.place.y + cell.place.height});
            }
          }
          auto generated = b::generate_checkpoint(frame,
                                                  *models,
                                                  driver,
                                                  *joint_technology,
                                                  *joint_frame,
                                                  joint_masks,
                                                  opaque_macros,
                                                  physical_models,
                                                  policy);
          check(generated.supported_api, "native generator unavailable");
          problem = std::move(generated.problem);
          out.Key("native_generation");
          out.StartObject();
          out.Key("seconds");
          out.Double(generated.seconds);
          out.Key("demand_seconds");
          out.Double(generated.demand_seconds);
          out.Key("sites_seconds");
          out.Double(generated.sites_seconds);
          out.Key("search_seconds");
          out.Double(generated.search_seconds);
          out.Key("critical_nets");
          out.Uint64(generated.critical_nets);
          out.Key("window_nets");
          out.Uint64(generated.window_nets);
          out.Key("outside_nets");
          out.Uint64(generated.outside_nets);
          out.Key("omitted_nets");
          out.Uint64(generated.omitted_nets);
          out.Key("missing_contexts");
          out.Uint64(generated.missing_contexts);
          out.Key("budget_exhausted");
          out.Bool(generated.budget_exhausted);
          out.Key("nets");
          out.StartArray();
          for (const auto& net : generated.nets) {
            out.StartObject();
            out.Key("owner");
            out.String(net.owner.c_str());
            out.Key("reason");
            out.String(net.reason.c_str());
            out.Key("candidates");
            out.Uint64(net.candidates);
            out.Key("options");
            out.Uint64(net.options);
            out.Key("assignments");
            out.Uint64(net.assignments);
            out.EndObject();
          }
          out.EndArray();
          out.EndObject();
        }

        const auto   prepare_start   = std::chrono::steady_clock::now();
        const auto   joint           = b::prepare_checkpoint(frame, problem, {}, *models, base, drivers, driver, settings);
        const double prepare_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - prepare_start).count();
        check(joint.supported_api, "joint replay unavailable on owned API");
        out.Key("continuous_terminal_geometry");
        out.Bool(spec.HasMember("generate_native_candidates") && spec["generate_native_candidates"].GetBool());
        out.Key("supported_api");
        out.Bool(true);
        out.Key("native_certified");
        out.Bool(timing.certified);
        out.Key("native_timing_seconds");
        out.Double(native_seconds);
        out.Key("lr_weight_seconds");
        out.Double(lr_seconds);
        out.Key("joint_prepare_seconds");
        out.Double(prepare_seconds);
        out.Key("native_visited_pins");
        out.Uint64(timing.visited_pins);
        out.Key("native_evaluated_arcs");
        out.Uint64(timing.evaluated_arcs);
        const size_t allocated = std::count_if(joint.negotiation->nets.begin(), joint.negotiation->nets.end(), [](const auto& n) {
          return bool(n.candidate);
        });
        out.Key("allocated_nets");
        out.Uint64(allocated);
        out.Key("unmet_nets");
        out.Uint64(joint.negotiation->nets.size() - allocated);
        out.Key("allocation_status");
        out.String(joint.negotiation->status == b::Allocation_status::settled             ? "settled"
                   : joint.negotiation->status == b::Allocation_status::incomplete_models ? "incomplete_models"
                                                                                          : "budget_exhausted");

        out.Key("native_period_ps");
        out.Double(*timing.achievable_period_ps);
        out.Key("physical_pins");
        out.Uint64(design.pin2node.size());
        out.Key("weighted_pins");
        out.Uint64(weights.pins.size());
        out.Key("critical_nets");
        out.Uint64(joint.critical_nets);
        out.Key("window_nets");
        out.Uint64(joint.window_nets);
        out.Key("outside_nets");
        out.Uint64(joint.outside_nets);
        out.Key("missing_contexts");
        out.Uint64(joint.missing_contexts);
        out.Key("virtual_buffers");
        out.Uint64(joint.density.virtual_buffers);
        out.Key("reserved_area_um2");
        out.Double(joint.density.virtual_area / pool->dbu / pool->dbu);
        out.Key("gap_pulls");
        out.Uint64(joint.forces.applied);
        out.Key("unbound_pulls");
        out.Uint64(joint.forces.unbound_driver);
        out.Key("input_search_complete");
        out.Bool(joint.negotiation->input_search_complete);
        out.Key("footprints");
        out.StartArray();
        for (size_t i = 0; i < joint.negotiation->nets.size(); ++i) {
          const auto& n = joint.negotiation->nets[i];
          if (!n.candidate) {
            continue;
          }
          const auto& candidate = problem.nets[i].candidates[*n.candidate];
          for (const auto& cell : candidate.candidate.buffers) {
            out.StartObject();
            out.Key("owner");
            out.String(cell.name.c_str());
            out.Key("net");
            out.String(n.owner.c_str());
            out.Key("master");
            out.String(cell.model.cell.c_str());
            out.Key("body_um");
            out.StartArray();
            for (double v : {cell.body.xl, cell.body.yl, cell.body.xh, cell.body.yh}) {
              out.Double(v);
            }
            out.EndArray();
            out.EndObject();
          }
        }
        out.EndArray();
        out.Key("sites");
        out.StartArray();
        for (const auto& [owner, spans] : joint.proposal->allocation.sites.reservations()) {
          for (const auto& span : spans) {
            out.StartObject();
            out.Key("owner");
            out.String(owner.c_str());
            out.Key("row");
            out.String(span.row.c_str());
            out.Key("first");
            out.Uint64(span.first);
            out.Key("count");
            out.Uint64(span.count);
            out.EndObject();
          }
        }
        out.EndArray();
        out.Key("seconds");
        out.Double(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
        out.Key("scope");
        out.String(
            spec.HasMember("generate_native_candidates") && spec["generate_native_candidates"].GetBool()
                ? "Fresh native critical/window generation on the original graph/saved RC, continuous pin geometry and "
                  "conservative integer-DBU occupancy. One topology with bounded site/cell search, opaque macros, empty history; "
                  "no GP solve, graph edit, continuous/Steiner or routed acceptance."
                : "Single native joint-checkpoint replay on original graph/saved wire RC and rounded endpoint geometry, with fresh "
                  "LR/window/source context and empty history. No GP solve, graph edit, continuous/Steiner or routed acceptance.");
#else
        out.Key("supported_api");
        out.Bool(false);
#endif
        out.EndObject();
      }
      out.Key("gap_history");
      out.StartArray();
      for (const auto& [gap, price] : result.proposal.history.gaps) {
        out.StartObject();
        out.Key("row");
        out.String(gap.row.c_str());
        out.Key("first");
        out.Uint64(gap.first);
        out.Key("count");
        out.Uint64(gap.count);
        out.Key("price");
        out.Double(price);
        out.EndObject();
      }
      out.EndArray();
      out.EndObject();
    }
    out.EndObject();
    std::cout << buffer.GetString() << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "geometry probe failed: " << e.what() << '\n';
    return 1;
  }
}
