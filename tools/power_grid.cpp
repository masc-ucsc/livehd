// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Audit native PG planning and experimental PG DEF export.
#include "pass/place/power_grid.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

int main(int argc, char** argv) {
  try {
    std::vector<std::string>                   lefs;
    std::vector<livehd::placement::Pg_channel> channels;
    std::string                                def, output, def_output, report_output, profile = "auto";
    bool                                       reserve_signals = false;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (i + 1 == argc) {
        throw std::invalid_argument("missing argument value");
      }
      const std::string value = argv[++i];
      if (key == "--lef") {
        lefs.push_back(value);
      } else if (key == "--def") {
        def = value;
      } else if (key == "--output") {
        output = value;
      } else if (key == "--def-output") {
        def_output = value;
      } else if (key == "--report") {
        report_output = value;
      } else if (key == "--channel-skip" || key == "--channel-gap") {
        auto parsed = livehd::placement::parse_pg_channels(std::string(key == "--channel-skip" ? "skip," : "gap,") + value);
        if (parsed.size() != 1) {
          throw std::invalid_argument("one channel expected per audit argument");
        }
        channels.push_back(std::move(parsed.front()));
      } else if (key == "--reserve-signals") {
        if (value != "true" && value != "false") {
          throw std::invalid_argument("--reserve-signals requires true or false");
        }
        reserve_signals = value == "true";
      } else if (key == "--profile") {
        profile = value;
      } else {
        throw std::invalid_argument("unknown argument: " + key);
      }
    }
    if (lefs.empty() || def.empty() || output.empty()) {
      throw std::invalid_argument("power_grid requires --lef, --def and --output (JSON metal plan only)");
    }
    if (!report_output.empty() && def_output.empty()) {
      throw std::invalid_argument("--report requires --def-output");
    }
    std::set<std::filesystem::path> inputs, outputs;
    inputs.insert(std::filesystem::weakly_canonical(def));
    for (const auto& lef : lefs) {
      inputs.insert(std::filesystem::weakly_canonical(lef));
    }
    for (const auto& name : {output, def_output, report_output}) {
      if (name.empty()) {
        continue;
      }
      const auto path = std::filesystem::weakly_canonical(name);
      if (inputs.contains(path) || !outputs.insert(path).second) {
        throw std::invalid_argument("power grid outputs must be distinct from each other and inputs");
      }
    }
    const auto tech = livehd::physical::read_lef(lefs);
    const auto fp   = livehd::physical::read_def(def);
    auto       p    = livehd::placement::power_grid_profile(profile, tech);
    p.channels      = channels;
    if (reserve_signals) {
      p = livehd::placement::reserve_power_grid_channels(tech, fp, p);
    }
    const auto plan = livehd::placement::plan_power_grid(tech, fp, p);
    if (!def_output.empty()) {
      livehd::placement::Pg_channel_stats stats;
      const auto grid = livehd::placement::make_power_grid(tech, fp, p, report_output.empty() ? nullptr : &stats, reserve_signals);
      livehd::physical::write_def(grid, def_output);
      livehd::physical::verify_def_roundtrip(grid, livehd::physical::read_def(def_output));
      if (!report_output.empty()) {
        livehd::placement::write_power_grid_report(grid, p, report_output, &stats);
      }
    }
    std::ofstream out(output);
    if (!out) {
      throw std::runtime_error("cannot open output: " + output);
    }
    rapidjson::OStreamWrapper                    stream(out);
    rapidjson::Writer<rapidjson::OStreamWrapper> w(stream);
    w.StartObject();
    w.Key("stage");
    w.String("metal_plan_without_vias_or_trimming");
    w.Key("exported_pg_def");
    w.String(def_output.c_str());
    w.Key("reserve_signals");
    w.Bool(reserve_signals);
    w.Key("automatic_channel_count");
    w.Uint64(p.channels.size() - channels.size());
    w.Key("profile");
    w.String(p.name.c_str());
    w.Key("reference_revision");
    w.String(p.reference_revision.c_str());
    w.Key("dbu");
    w.Int64(fp.dbu);
    w.Key("channel_removed_planned_stripe_length_um");
    w.StartObject();
    for (const auto& [layer, length] : plan.channel_removed_length_um) {
      w.Key(layer.c_str());
      w.Double(length);
    }
    w.EndObject();
    w.Key("metal");
    w.StartArray();
    for (const auto& metal : plan.metal) {
      w.StartArray();
      w.String(metal.net.c_str());
      w.String(metal.layer.c_str());
      w.Int64(std::llround(metal.rect.xl));
      w.Int64(std::llround(metal.rect.yl));
      w.Int64(std::llround(metal.rect.xh));
      w.Int64(std::llround(metal.rect.yh));
      w.Bool(metal.followpin);
      w.EndArray();
    }
    w.EndArray();
    w.EndObject();
    out.close();
    if (!out) {
      throw std::runtime_error("cannot finish output: " + output);
    }
  } catch (const std::exception& e) {
    std::cerr << "power_grid: " << e.what() << '\n';
    return 1;
  }
}
