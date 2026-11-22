// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

#include "clock_spef.hpp"
#include "core/file_utils.hpp"
#include "pass/place/technology_rc.hpp"
#include "rapidjson/document.h"

namespace {
using namespace livehd;
struct RC {
  double r = 0, c = 0;
};
RC total(const physical::Rc_net& net) {
  RC rc;
  for (const auto& e : net.edges) {
    rc.r += e.resistance_ohm;
  }
  for (const auto& n : net.nodes) {
    rc.c += n.capacitance_ff;
  }
  return rc;
}
void fit(const char* def, const char* spef, const char* manifest, const char* liberty, const char* rc,
         const std::vector<std::string>& lefs, const std::string& profile) {
  const auto          contents = file_utils::read_file(manifest);
  rapidjson::Document doc;
  if (!contents || doc.Parse(contents->c_str()).HasParseError() || !doc.IsObject() || !doc.HasMember("networks")
      || !doc["networks"].IsArray()) {
    throw std::invalid_argument("invalid PCRS manifest");
  }
  std::map<std::string, bool> drivers;
  for (const auto& network : doc["networks"].GetArray()) {
    if (!network.IsObject() || !network.HasMember("branches") || !network["branches"].IsArray()) {
      throw std::invalid_argument("missing PCRS branches");
    }
    for (const auto& branch : network["branches"].GetArray()) {
      if (!branch.IsObject() || !branch.HasMember("instance") || !branch["instance"].IsString() || !branch.HasMember("leaf")
          || !branch["leaf"].IsBool() || !drivers.emplace(branch["instance"].GetString(), branch["leaf"].GetBool()).second) {
        throw std::invalid_argument("invalid or duplicate PCRS branch");
      }
    }
  }
  if (drivers.empty()) {
    throw std::invalid_argument("no native branches to fit");
  }
  auto                                               fp   = physical::read_def_snapshot(def);
  const auto                                         tech = physical::read_lef(lefs);
  std::map<std::string_view, const physical::Macro*> masters;
  for (const auto& c : fp.components) {
    if (drivers.contains(c.name.str())) {
      masters.emplace(c.name.view(), &tech.macros.at(c.macro));
    }
  }
  std::set<std::string> selected;
  std::erase_if(fp.nets, [&](const auto& net) {
    for (const auto& pin : net.connections) {
      const auto cell = masters.find(pin.instance.view());
      if (cell != masters.end() && cell->second->pins.at(pin.pin).direction == "OUTPUT") {
        selected.insert(net.name.str());
        return false;
      }
    }
    return true;
  });
  if (selected.size() != drivers.size()) {
    throw std::invalid_argument("routed DEF does not match native branch inventory");
  }
  auto wire                      = placement::technology_net_wire_rc(tech, liberty, rc, -1, -1, 1);
  wire.signal                    = wire.clock;  // Select clock-layer RC independently of DEF USE annotations.
  const auto              model  = placement::estimate_floorplan_rc(fp, tech, wire);
  const auto              actual = physical::clock_report_spef(spef, fp, tech, &selected);
  std::map<Pin_label, RC> measured;
  for (const auto& net : actual.nets) {
    measured.emplace(net.driver, total(net));
  }
  if (model.nets.size() != drivers.size() || measured.size() != drivers.size()) {
    throw std::invalid_argument("incomplete extracted clock data");
  }
  std::cout << "driver\tgroup\tsteiner_um\tpredicted_ohm\textracted_ohm\tpredicted_ff\textracted_ff\n" << std::setprecision(12);
  struct Group {
    size_t count = 0;
    RC     estimated, actual;
  };
  std::map<std::string, Group> groups;
  for (const auto& net : model.nets) {
    const auto& instance  = net.driver.instance().str();
    const auto  predicted = total(net), routed = measured.at(net.driver);
    const auto  kind  = drivers.at(instance) ? "leaf" : "trunk";
    auto&       group = groups[kind];
    ++group.count;
    group.estimated.r += predicted.r;
    group.estimated.c += predicted.c;
    group.actual.r    += routed.r;
    group.actual.c    += routed.c;
    std::cout << net.driver.str() << '\t' << kind << '\t' << net.wire_length_um << '\t' << predicted.r << '\t' << routed.r << '\t'
              << predicted.c << '\t' << routed.c << '\n';
  }
  if (!profile.empty()) {
    for (const auto* kind : {"leaf", "trunk"}) {
      const auto found = groups.find(kind);
      if (found == groups.end()) {  // a tree of leaf drivers only has no trunk group
        throw std::invalid_argument(std::string("no routed ") + kind + " branch to fit");
      }
      const auto& g = found->second;
      if (g.estimated.r <= 0 || g.estimated.c <= 0 || g.actual.r <= 0 || g.actual.c <= 0) {
        throw std::invalid_argument(std::string("no positive RC totals for ") + kind);
      }
    }
    std::ofstream out;
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out.open(profile);
    out << std::setprecision(17) << "{";
    for (const auto& [kind, g] : groups) {
      out << (kind == "leaf" ? "\n" : ",\n") << "  \"" << kind << "\": {\"count\": " << g.count
          << ", \"ohm_scale\": " << g.actual.r / g.estimated.r << ", \"ff_scale\": " << g.actual.c / g.estimated.c << "}";
    }
    out << "\n}\n";
    out.close();
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    std::string profile;
    if (argc > 2 && std::string_view(argv[1]) == "--profile") {
      profile  = argv[2];
      argv    += 2;
      argc    -= 2;
    }
    if (argc < 7) {
      throw std::invalid_argument(
          "usage: clock_rc_fit [--profile output.json] routed.def routed.spef pcrs_clock.json library.lib setRC.tcl tech.lef "
          "[cell.lef ...]");
    }
    fit(argv[1], argv[2], argv[3], argv[4], argv[5], {argv + 6, argv + argc}, profile);
    return std::cout.good() ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "clock_rc_fit: " << e.what() << '\n';
    return 1;
  }
}
