// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Diagnostic access enumeration on a complete physical frame, not a pre-router.
#include "inou/lefdef/pin_access.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

#include "inou/lefdef/routing_occupancy.hpp"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

int main(int argc, char** argv) {
  namespace ph = livehd::physical;
  try {
    std::vector<std::string> lefs;
    std::string              def;
    std::vector<std::string> instances;
    auto signal_wires = ph::Signal_wires::include;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (++i == argc) {
        throw std::invalid_argument("missing argument value");
      }
      if (key == "--lef") {
        lefs.emplace_back(argv[i]);
      } else if (key == "--def") {
        def = argv[i];
      } else if (key == "--instance") {
        instances.emplace_back(argv[i]);
      } else if (key == "--signal-wires") {
        const std::string mode = argv[i];
        if (mode != "include" && mode != "exclude") {
          throw std::invalid_argument("--signal-wires requires include or exclude");
        }
        signal_wires = mode == "include" ? ph::Signal_wires::include : ph::Signal_wires::exclude;
      } else {
        throw std::invalid_argument("unknown argument: " + key);
      }
    }
    if (lefs.empty() || def.empty() || instances.empty()) {
      throw std::invalid_argument("pin_access requires --lef, --def, --instance");
    }
    const auto tech   = ph::read_lef(lefs);
    const auto fp     = ph::read_def(def);
    std::set<std::string> requested(instances.begin(), instances.end());
    if (requested.size() != instances.size() || requested.contains("")) {
      throw std::invalid_argument("empty or duplicate instance");
    }
    std::map<std::string, const ph::Component*> targets;
    std::map<std::pair<std::string, std::string>, std::string> pin_nets;
    for (const auto& cell : fp.components) {
      if (requested.contains(cell.name)) { targets.emplace(cell.name, &cell); }
    }
    if (targets.size() != requested.size()) { throw std::invalid_argument("unknown instance"); }
    for (const auto& net : fp.nets) {
      for (const auto& connection : net.connections) {
        if (requested.contains(connection.instance)) {
          pin_nets[{connection.instance, connection.pin}] = net.name;
        }
      }
    }
    const auto occupancy = ph::routing_occupancy(tech, fp, fp.dbu, {}, nullptr, signal_wires);
    const ph::Signal_metal_rules               rules(tech, fp);
    rapidjson::StringBuffer                    buffer;
    rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    if (instances.size() > 1) {
      w.StartObject();
      w.Key("instances");
      w.StartObject();
    }
    for (const auto& instance : instances) {
      const auto* target = targets.at(instance);
      if (instances.size() > 1) { w.Key(instance.c_str()); }
      w.StartObject();
      w.Key("instance");
      w.String(instance.c_str());
      w.Key("scope");
      w.String("M1 pin/M2 escape candidates; full-route acceptance remains separate");
      w.Key("signal_wires");
      w.String(signal_wires == ph::Signal_wires::include ? "included" : "excluded for diagnostic baseline");
      w.Key("obstacles");
      w.Uint64(occupancy.size());
      w.Key("pins");
      w.StartObject();
      for (const auto& [name, pin] : tech.macros.at(target->macro).pins) {
        if (!pin.use.empty() && pin.use != "SIGNAL") {
          continue;
        }
        const auto found = pin_nets.find({instance, name});
        const std::string own_net = found == pin_nets.end() ? std::string{} : found->second;
        const auto result = ph::enumerate_m1_pin_access(tech, fp, *target, name, occupancy, rules, 10000, own_net);
        w.Key(name.c_str());
        w.StartObject();
        w.Key("examined");
        w.Uint64(result.examined);
        w.Key("exhausted");
        w.Bool(result.exhausted);
        w.Key("points");
        w.StartArray();
        for (const auto& p : result.points) {
          w.StartObject();
          w.Key("x");
          w.Int64(p.x);
          w.Key("y");
          w.Int64(p.y);
          w.Key("via");
          w.String(p.via.c_str());
          w.Key("escape");
          w.StartArray();
          for (auto v : {p.escape.rect.xl, p.escape.rect.yl, p.escape.rect.xh, p.escape.rect.yh}) {
            w.Int64(std::llround(v));
          }
          w.EndArray();
          w.EndObject();
        }
        w.EndArray();
        w.Key("rejected");
        w.StartObject();
        for (const auto& [reason, count] : result.rejected) {
          w.Key(reason.c_str());
          w.Uint64(count);
        }
        w.EndObject();
        w.EndObject();
      }
      w.EndObject();
      w.EndObject();
    }
    if (instances.size() > 1) {
      w.EndObject();
      w.EndObject();
    }
    std::cout << buffer.GetString() << '\n';
  } catch (const std::exception& e) {
    std::cerr << "pin_access: " << e.what() << '\n';
    return 1;
  }
}
