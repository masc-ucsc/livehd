// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Read-only common HPWL and reset-cut export from physical connectivity.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "pass/place/prects_networks.hpp"
#include "pass/place/track_capacity.hpp"
#include "rapidjson/document.h"
#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

namespace ph = livehd::physical;
namespace pl = livehd::placement;

struct Displacement {
  size_t      instances = 0, moved = 0, orientations = 0, statuses = 0, fixed_moved = 0;
  long double total = 0, maximum = 0;

  void observe(const ph::Component& before, const ph::Component& after, int64_t dbu) {
    const auto distance = (std::abs(static_cast<long double>(after.place.x) - before.place.x)
                           + std::abs(static_cast<long double>(after.place.y) - before.place.y))
                          / dbu;
    const bool rotated  = before.place.orientation != after.place.orientation;
    ++instances;
    moved        += distance != 0;
    orientations += rotated;
    statuses     += before.place.status != after.place.status;
    fixed_moved
        += (before.place.status == ph::Status::FIXED || before.place.status == ph::Status::LOCKED) && (distance != 0 || rotated);
    total   += distance;
    maximum  = std::max(maximum, distance);
  }
};

int main(int argc, char** argv) {
  try {
    std::string              definition, reference_def, liberty, controls_path, output, snapshot_path, uroute_path;
    std::vector<std::string> lefs;
    bool                     check_native = false;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (key == "--check-native") {
        check_native = true;
        continue;
      }
      if (++i == argc) {
        throw std::invalid_argument("missing value for " + key);
      }
      if (key == "--def") {
        definition = argv[i];
      } else if (key == "--reference-def") {
        reference_def = argv[i];
      } else if (key == "--liberty") {
        liberty = argv[i];
      } else if (key == "--controls") {
        controls_path = argv[i];
      } else if (key == "--output") {
        output = argv[i];
      } else if (key == "--snapshot") {
        snapshot_path = argv[i];
      } else if (key == "--uroute-geometry") {
        uroute_path = argv[i];
      } else if (key == "--lef") {
        lefs.emplace_back(argv[i]);
      } else {
        throw std::invalid_argument("unknown argument: " + key);
      }
    }
    if (definition.empty() || liberty.empty() || controls_path.empty() || output.empty() || lefs.empty()) {
      throw std::invalid_argument(
          "usage: placement_metrics --def DEF --liberty LIB --controls QOR --lef LEF [--lef LEF] --output JSON [--check-native] "
          "[--reference-def BEFORE_DEF]");
    }
    std::ifstream       input(controls_path);
    const std::string   text{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document metadata;
    metadata.Parse(text.c_str());
    if (metadata.HasParseError() || !metadata.IsObject() || !metadata.HasMember("prects_controls")) {
      throw std::invalid_argument("controls must contain native prects_controls metadata");
    }
    const auto& saved = metadata["prects_controls"];
    if (!saved.IsObject()) {
      throw std::invalid_argument("prects_controls must be an object");
    }
    if (metadata.HasMember("hpwl_um") && !metadata["hpwl_um"].IsNumber()) {
      throw std::invalid_argument("hpwl_um must be a number");
    }
    auto       roles = pl::prects_library(liberty);
    const auto load  = [&](const char* key, std::set<std::string>& values) {
      if (saved.HasMember(key)) {
        if (!saved[key].IsArray()) {
          throw std::invalid_argument(std::string(key) + " must be an array of strings");
        }
        for (const auto& value : saved[key].GetArray()) {
          if (!value.IsString()) {
            throw std::invalid_argument(std::string(key) + " must be an array of strings");
          }
          values.emplace(value.GetString());
        }
      }
    };
    load("ports", roles.ports);
    load("pins", roles.pins);
    load("reset_ports", roles.reset_ports);
    load("reset_pins", roles.reset_pins);
    if (saved.HasMember("override") && !saved["override"].IsBool()) {
      throw std::invalid_argument("override must be a boolean");
    }
    roles.override_controls = saved.HasMember("override") && saved["override"].GetBool();
    if (roles.override_controls) {
      throw std::invalid_argument("explicit control overrides do not export enough clock-role metadata for this scorer");
    }
    // Graph-derived clock/reset roles are disjoint in this exported metadata.
    // Liberty and LEF clock sinks also seed the original network classifier.
    for (const auto& name : roles.ports) {
      if (!roles.reset_ports.contains(name)) {
        roles.clock_ports.insert(name);
      }
    }
    for (const auto& name : roles.pins) {
      if (!roles.reset_pins.contains(name)) {
        roles.clock_pins.insert(name);
      }
    }
    const auto tech = ph::read_lef(lefs);
    const auto fp   = ph::read_def_snapshot(definition);
    if (fp.dbu != tech.dbu) {
      throw std::invalid_argument("LEF/DEF units differ");
    }
    const auto policy  = pl::prects_networks(tech, fp, roles);
    const auto ignored = std::count(policy.ignored.begin(), policy.ignored.end(), true);
    if (check_native) {
      if (!saved.HasMember("ignored_nets") || !saved["ignored_nets"].IsInt64() || !metadata.HasMember("hpwl_um")
          || !metadata["hpwl_um"].IsNumber()) {
        throw std::invalid_argument("native verification needs ignored_nets and hpwl_um");
      }
      if (ignored != saved["ignored_nets"].GetInt64()) {
        throw std::runtime_error("reconstructed control-net census differs from native placement");
      }
    }
    std::unordered_map<std::string_view, const ph::Component*> cells;
    std::unordered_map<std::string_view, const ph::Terminal*>  terminals;
    for (const auto& cell : fp.components) {
      cells.emplace(cell.name, &cell);
    }
    for (const auto& terminal : fp.terminals) {
      terminals.emplace(terminal.name, &terminal);
    }
    std::array<Displacement, 4> displacements;
    if (!reference_def.empty()) {
      const auto before = ph::read_def_snapshot(reference_def);
      if (before.dbu != fp.dbu || fp.dbu <= 0 || before.design != fp.design) {
        throw std::invalid_argument("displacement comparison requires matching DEF design and units");
      }
      if (before.components.size() != fp.components.size() || cells.size() != fp.components.size()) {
        throw std::invalid_argument("displacement comparison instance census differs");
      }
      std::set<std::string_view> seen;
      for (const auto& old : before.components) {
        const auto found = cells.find(old.name);
        if (!seen.insert(old.name).second || found == cells.end() || found->second->macro != old.macro) {
          throw std::invalid_argument("displacement comparison instance/master differs: " + old.name);
        }
        const auto& cell = *found->second;
        if (old.place.status == ph::Status::UNPLACED || cell.place.status == ph::Status::UNPLACED) {
          throw std::invalid_argument("displacement comparison requires placed instances: " + old.name);
        }
        const auto&  master        = tech.macros.at(cell.macro);
        const bool   physical_only = ph::placement_mask(cell, tech) != ph::Placement_mask::none || master.klass == "CORE SPACER";
        const size_t category      = physical_only ? 3 : master.klass.starts_with("CORE") ? 1 : 2;
        displacements[0].observe(old, cell, fp.dbu);
        displacements[category].observe(old, cell, fp.dbu);
      }
    }
    double                   all_hpwl = 0, data_hpwl = 0, clock_excluded_hpwl = 0;
    std::vector<std::string> clock_nets, reset_nets;
    struct Snapshot_pin {
      size_t node;
      double x, y;
    };
    std::vector<std::vector<Snapshot_pin>> snapshot_nets;
    std::vector<std::vector<Snapshot_pin>> physical_nets;
    for (size_t i = 0; i < fp.nets.size(); ++i) {
      const auto&               net   = fp.nets[i];
      bool                      reset = false;
      double                    xl = std::numeric_limits<double>::infinity(), yl = xl, xh = -xl, yh = -xl;
      std::vector<Snapshot_pin> pins;
      for (const auto& pin : net.connections) {
        const auto label  = ph::connection_label(pin);
        reset            |= policy.timing_cuts.contains(label);
        double x, y;
        size_t node;
        if (pin.instance == "PIN") {
          const auto& terminal = *terminals.at(pin.pin);
          x                    = terminal.x;
          y                    = terminal.y;
          node                 = fp.components.size() + (&terminal - fp.terminals.data());
        } else {
          const auto& cell   = *cells.at(pin.instance);
          const auto& master = tech.macros.at(cell.macro);
          const auto& shapes = master.pins.at(pin.pin).shapes;
          if (shapes.empty()) {
            throw std::runtime_error("missing LEF pin geometry: " + label);
          }
          const auto point = ph::oriented_center(master, shapes.front().rect, cell.place.orientation);
          x                = cell.place.x + point.first * fp.dbu;
          y                = cell.place.y + point.second * fp.dbu;
          node             = &cell - fp.components.data();
        }
        if (!snapshot_path.empty() || !uroute_path.empty()) {
          pins.push_back({node, x, y});
        }
        xl = std::min(xl, x);
        xh = std::max(xh, x);
        yl = std::min(yl, y);
        yh = std::max(yh, y);
      }
      const bool clock = policy.ignored[i] && !reset;
      if (!uroute_path.empty()) {
        physical_nets.push_back(pins);
      }
      if (clock) {
        clock_nets.push_back(net.name);
      }
      if (reset) {
        reset_nets.push_back(net.name);
      }
      if (net.connections.size() < 2) {
        continue;
      }
      const double length  = (xh - xl + yh - yl) / fp.dbu;
      all_hpwl            += length;
      if (!policy.ignored[i]) {
        data_hpwl += length;
      }
      if (!clock) {
        clock_excluded_hpwl += length;
        if (!snapshot_path.empty()) {
          snapshot_nets.push_back(std::move(pins));
        }
      }
    }
    if (check_native && std::abs(data_hpwl - metadata["hpwl_um"].GetDouble()) > 1e-3) {
      throw std::runtime_error("reconstructed data HPWL differs from native placement");
    }
    if (!snapshot_path.empty() || !uroute_path.empty()) {
      if (fp.rows.empty()) {
        throw std::invalid_argument("routing snapshot needs core rows");
      }
      const auto& row     = fp.rows.front();
      const auto  rh      = std::llround(tech.sites.at(row.site).height * fp.dbu);
      double      core_xl = std::numeric_limits<double>::infinity(), core_yl = core_xl;
      double      core_xh = -core_xl, core_yh = -core_xl;
      for (const auto& r : fp.rows) {
        if (r.count <= 0 || r.step <= 0) {
          throw std::invalid_argument("routing snapshot needs nonempty rows with positive step");
        }
        const auto& site = tech.sites.at(r.site);
        core_xl          = std::min(core_xl, double(r.x));
        core_yl          = std::min(core_yl, double(r.y));
        core_xh          = std::max(core_xh, double(r.x) + double(r.count - 1) * r.step + site.width * fp.dbu);
        core_yh          = std::max(core_yh, double(r.y) + site.height * fp.dbu);
      }
      // LEF and DEF units were checked equal above.
      const auto [capacity_h, capacity_v] = pl::lef_track_capacity(tech);
      if (!(capacity_h > 0 && capacity_v > 0)) {
        throw std::invalid_argument("routing snapshot needs horizontal and vertical pitches");
      }
      if (!uroute_path.empty()) {
        std::ofstream                                geometry(uroute_path);
        rapidjson::OStreamWrapper                    stream(geometry);
        rapidjson::Writer<rapidjson::OStreamWrapper> json(stream);
        const auto                                   number = [&](const char* key, double value) {
          json.Key(key);
          json.Double(value);
        };
        json.StartObject();
        json.Key("geometry_version");
        json.Int(1);
        json.Key("top");
        json.String(fp.design.c_str());
        number("dbu", fp.dbu);
        number("site_width", row.step);
        number("row_height", rh);
        number("capacity_h", capacity_h);
        number("capacity_v", capacity_v);
        const auto box = [&](const char* key, double xl, double yl, double xh, double yh) {
          json.Key(key);
          json.StartArray();
          for (double value : {xl, yl, xh, yh}) {
            json.Double(value);
          }
          json.EndArray();
        };
        box("core", core_xl, core_yl, core_xh, core_yh);
        box("die", fp.die.xl, fp.die.yl, fp.die.xh, fp.die.yh);
        // Placement constraints are provenance, not physical macro footprints
        // or cell density. Keep them without changing the four feature maps.
        json.Key("blockages");
        json.StartArray();
        for (const auto& b : fp.snapshot_blockages) {
          json.StartObject();
          json.Key("kind");
          json.String(b.placement ? "placement" : "routing");
          json.Key("layer");
          json.String(b.layer.c_str());
          json.Key("component");
          json.String(b.component.c_str());
          const auto flag = [&](const char* key, bool value) {
            json.Key(key);
            json.Bool(value);
          };
          flag("soft", b.soft);
          flag("slots", b.slots);
          flag("fills", b.fills);
          flag("pushdown", b.pushdown);
          flag("except_pg", b.except_pg);
          number("partial_density", b.partial_density);
          number("spacing", b.spacing);
          number("design_rule_width", b.design_rule_width);
          number("mask", b.mask);
          json.Key("rectangles");
          json.StartArray();
          for (const auto& rect : b.rectangles) {
            json.StartArray();
            for (double value : {rect.xl, rect.yl, rect.xh, rect.yh}) {
              json.Double(value);
            }
            json.EndArray();
          }
          json.EndArray();
          json.Key("polygons");
          json.StartArray();
          for (const auto& polygon : b.polygons) {
            json.StartArray();
            for (const auto& [x, y] : polygon) {
              json.StartArray();
              json.Int64(x);
              json.Int64(y);
              json.EndArray();
            }
            json.EndArray();
          }
          json.EndArray();
          json.EndObject();
        }
        json.EndArray();
        json.Key("layers");
        json.StartArray();
        for (const auto& name : tech.routing_layers) {
          const auto& layer = tech.layers.at(name);
          json.StartObject();
          json.Key("name");
          json.String(name.c_str());
          json.Key("direction");
          json.String(layer.direction.c_str());
          number("pitch_x", layer.pitch_x * fp.dbu);
          number("pitch_y", layer.pitch_y * fp.dbu);
          number("width", layer.width * fp.dbu);
          json.EndObject();
        }
        json.EndArray();
        json.Key("nodes");
        json.StartArray();
        for (const auto& cell : fp.components) {
          const auto& master = tech.macros.at(cell.macro);
          double      width = master.width * fp.dbu, height = master.height * fp.dbu;
          if (cell.place.orientation == ph::Orientation::E || cell.place.orientation == ph::Orientation::W
              || cell.place.orientation == ph::Orientation::FE || cell.place.orientation == ph::Orientation::FW) {
            std::swap(width, height);
          }
          json.StartObject();
          json.Key("name");
          json.String(cell.name.c_str());
          json.Key("master");
          json.String(cell.macro.c_str());
          number("x", cell.place.x);
          number("y", cell.place.y);
          number("width", width);
          number("height", height);
          const auto [blocked_h, blocked_v]
              = master.klass.starts_with("CORE") ? std::pair{0., 0.} : pl::lef_track_capacity(tech, master.blocked_layers);
          number("blocked_h", blocked_h);
          number("blocked_v", blocked_v);
          json.Key("macro");
          json.Bool(!master.klass.starts_with("CORE"));
          json.Key("mask");
          json.Bool(ph::placement_mask(cell, tech) != ph::Placement_mask::none || master.klass == "CORE SPACER");
          json.EndObject();
        }
        for (const auto& terminal : fp.terminals) {
          json.StartObject();
          json.Key("name");
          json.String(terminal.name.c_str());
          json.Key("master");
          json.String("");
          number("x", terminal.x);
          number("y", terminal.y);
          number("width", 0);
          number("height", 0);
          json.Key("macro");
          json.Bool(false);
          json.Key("mask");
          json.Bool(false);
          json.EndObject();
        }
        json.EndArray();
        json.Key("nets");
        json.StartArray();
        for (size_t i = 0; i < fp.nets.size(); ++i) {
          json.StartObject();
          json.Key("name");
          json.String(fp.nets[i].name.c_str());
          json.Key("enabled");
          json.Bool(!policy.ignored[i]);
          json.Key("pins");
          json.StartArray();
          for (const auto& pin : physical_nets[i]) {
            json.StartArray();
            json.Double(pin.x);
            json.Double(pin.y);
            json.EndArray();
          }
          json.EndArray();
          json.Key("pin_nodes");
          json.StartArray();
          for (const auto& pin : physical_nets[i]) {
            json.Uint64(pin.node);
          }
          json.EndArray();
          json.EndObject();
        }
        json.EndArray();
        json.EndObject();
        geometry << '\n';
        if (!geometry) {
          throw std::runtime_error("cannot write URoute physical geometry");
        }
      }
      if (!snapshot_path.empty()) {
        std::ofstream snapshot(snapshot_path);
        snapshot << std::setprecision(17) << core_xl << ' ' << core_yl << ' ' << core_xh << ' ' << core_yh << ' ' << row.step << ' '
                 << rh << ' ' << capacity_h << ' ' << capacity_v << " 64\n"
                 << fp.components.size() + fp.terminals.size() << '\n';
        for (const auto& cell : fp.components) {
          const auto& master    = tech.macros.at(cell.macro);
          const bool  macro     = !master.klass.starts_with("CORE");
          const auto [blocked_h, blocked_v] = macro ? pl::lef_track_capacity(tech, master.blocked_layers) : std::pair{0., 0.};
          double width = master.width * fp.dbu, height = master.height * fp.dbu;
          if (cell.place.orientation == ph::Orientation::E || cell.place.orientation == ph::Orientation::W
              || cell.place.orientation == ph::Orientation::FE || cell.place.orientation == ph::Orientation::FW) {
            std::swap(width, height);
          }
          snapshot << cell.place.x << ' ' << cell.place.y << ' ' << width << ' ' << height << ' ' << macro << ' ' << blocked_h
                   << ' ' << blocked_v << '\n';
        }
        for (const auto& terminal : fp.terminals) {
          snapshot << terminal.x << ' ' << terminal.y << " 0 0 0 0 0\n";
        }
        snapshot << snapshot_nets.size() << '\n';
        for (const auto& net : snapshot_nets) {
          snapshot << net.size();
          for (const auto& pin : net) {
            snapshot << ' ' << pin.node << ' ' << pin.x << ' ' << pin.y;
          }
          snapshot << '\n';
        }
        if (!snapshot) {
          throw std::runtime_error("cannot write physical routing snapshot");
        }
      }
    }
    std::ofstream file(output);
    if (!file) {
      throw std::runtime_error("cannot write output");
    }
    rapidjson::OStreamWrapper                    stream(file);
    rapidjson::Writer<rapidjson::OStreamWrapper> json(stream);
    json.StartObject();
    const auto number = [&](const char* key, double value) {
      json.Key(key);
      json.Double(value);
    };
    number("clock_excluded_hpwl_um", clock_excluded_hpwl);
    number("data_hpwl_um", data_hpwl);
    number("all_hpwl_um", all_hpwl);
    if (metadata.HasMember("hpwl_um")) {
      number("native_data_hpwl_difference_um", data_hpwl - metadata["hpwl_um"].GetDouble());
    }
    json.Key("control_net_census");
    json.Int64(ignored);
    json.Key("nets");
    json.Uint64(fp.nets.size());
    json.Key("instances");
    json.Uint64(fp.components.size());
    const auto names = [&](const char* key, const std::vector<std::string>& values) {
      json.Key(key);
      json.StartArray();
      for (const auto& value : values) {
        json.String(value.c_str());
      }
      json.EndArray();
    };
    names("clock_nets", clock_nets);
    names("reset_cut_nets", reset_nets);
    if (!reference_def.empty()) {
      json.Key("displacement");
      json.StartObject();
      json.Key("metric");
      json.String("instance-origin Manhattan distance in microns; orientation and status changes counted separately");
      const std::array<const char*, 4> categories{"all_instances", "standard_cells", "macros", "physical_only"};
      for (size_t i = 0; i < categories.size(); ++i) {
        const auto& d = displacements[i];
        json.Key(categories[i]);
        json.StartObject();
        for (const auto& [key, value] : std::array<std::pair<const char*, size_t>, 5>{
                 {{"instances", d.instances},
                  {"moved", d.moved},
                  {"orientation_changes", d.orientations},
                  {"status_changes", d.statuses},
                  {"fixed_instances_moved", d.fixed_moved}}
        }) {
          json.Key(key);
          json.Uint64(value);
        }
        number("total_um", static_cast<double>(d.total));
        json.Key("mean_um");
        if (d.instances) {
          json.Double(static_cast<double>(d.total / d.instances));
        } else {
          json.Null();
        }
        json.Key("maximum_um");
        if (d.instances) {
          json.Double(static_cast<double>(d.maximum));
        } else {
          json.Null();
        }
        json.EndObject();
      }
      json.EndObject();
    }
    json.Key("policy");
    json.String(
        "Unweighted HPWL excludes clock nets, includes reset wiring. Reset-cut nets are excluded from timing, not physical "
        "connectivity.");
    json.EndObject();
    file << '\n';
    if (!file) {
      throw std::runtime_error("output failed");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
