// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Read-only rectangular routing dump for independent OpenDB shape comparisons.
#include "inou/lefdef/routing_geometry.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

#include "rapidjson/ostreamwrapper.h"
#include "rapidjson/writer.h"

namespace ph = livehd::physical;
int main(int argc, char** argv) {
  try {
    std::vector<std::string> lefs;
    std::string              def, output;
    for (int i = 1; i < argc; ++i) {
      const std::string key = argv[i];
      if (i + 1 == argc) {
        throw std::invalid_argument("missing argument value");
      }
      if (key == "--lef") {
        lefs.emplace_back(argv[++i]);
      } else if (key == "--def") {
        def = argv[++i];
      } else if (key == "--output") {
        output = argv[++i];
      } else {
        throw std::invalid_argument("unknown argument: " + key);
      }
    }
    if (lefs.empty() || def.empty() || output.empty()) {
      throw std::invalid_argument("usage: routing_geometry --lef tech.lef --lef cells.lef --def design.def --output shapes.json");
    }
    const auto    tech = ph::read_lef(lefs);
    const auto    fp   = ph::read_def(def);
    std::ofstream out(output);
    if (!out) {
      throw std::runtime_error("cannot open output: " + output);
    }
    rapidjson::OStreamWrapper                    stream(out);
    rapidjson::Writer<rapidjson::OStreamWrapper> w(stream);
    const auto                                   shapes = [&](const std::vector<ph::Shape>& geometry) {
      w.StartArray();
      for (const auto& s : geometry) {
        w.StartArray();
        w.String(s.layer.c_str());
        w.Int64(std::llround(s.rect.xl));
        w.Int64(std::llround(s.rect.yl));
        w.Int64(std::llround(s.rect.xh));
        w.Int64(std::llround(s.rect.yh));
        w.EndArray();
      }
      w.EndArray();
    };
    w.StartObject();
    w.Key("dbu");
    w.Int64(fp.dbu);
    w.Key("vias");
    w.StartObject();
    for (const auto& via : fp.vias) {
      w.Key(via.name.c_str());
      shapes(ph::via_geometry(tech, fp, {via.name}));
    }
    w.EndObject();
    const auto nets = [&](const char* key, const std::vector<ph::Net>& list, bool special) {
      w.Key(key);
      w.StartObject();
      for (const auto& net : list) {
        if (net.wires.empty()) {
          continue;
        }
        w.Key(net.name.c_str());
        shapes(ph::route_geometry(tech, fp, net, special));
      }
      w.EndObject();
    };
    nets("nets", fp.nets, false);
    nets("special_nets", fp.special_nets, true);
    w.EndObject();
    out.close();
    if (!out) {
      throw std::runtime_error("cannot finish output: " + output);
    }
  } catch (const std::exception& e) {
    std::cerr << "routing_geometry: " << e.what() << '\n';
    return 1;
  }
}
