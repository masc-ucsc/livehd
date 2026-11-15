// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Per-net FLUTE tree length of a routed-geometry JSON (placement_metrics --route-geometry): the
// denominator of the routed detour ratio. Output: one JSON array entry per net, in DBU; null for a
// disabled net or one with fewer than two pins.
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "dreamplace/native/rudy.hpp"
#include "dreamplace/native/rudy_tree.hpp"
#include "pass/place/steiner_topology.hpp"
#include "rapidjson/document.h"

namespace dp = DreamPlace::native;
namespace {
using Value = rapidjson::Value;
double number(const Value& value) {
  if (!value.IsNumber()) {
    throw std::invalid_argument("expected numeric geometry value");
  }
  return value.GetDouble();
}
const Value& field(const Value& value, const char* key) {
  if (!value.IsObject() || !value.HasMember(key)) {
    throw std::invalid_argument(std::string("missing geometry field: ") + key);
  }
  return value[key];
}
bool flag(const Value& value, const char* key) {
  if (!value.HasMember(key)) {
    return false;
  }
  if (!value[key].IsBool()) {
    throw std::invalid_argument(std::string("expected boolean geometry field: ") + key);
  }
  return value[key].GetBool();
}
}  // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      throw std::invalid_argument("usage: flute_lengths geometry.json flute.json");
    }
    std::ifstream       input(argv[1]);
    const std::string   text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    if (!input || doc.HasParseError()) {
      throw std::invalid_argument("invalid geometry JSON");
    }
    const auto& nets = field(doc, "nets");
    if (!nets.IsArray()) {
      throw std::invalid_argument("nets must be an array");
    }
    dp::RudyOptions options;
    options.degree     = 4096;
    options.initialize = livehd::placement::initialize_flute_tables;
    std::ofstream output(argv[2]);
    output << std::setprecision(17) << '[';
    bool first = true;
    for (const auto& net : nets.GetArray()) {
      if (!first) {
        output << ',';
      }
      first            = false;
      const auto& list = field(net, "pins");
      if (!list.IsArray()) {
        throw std::invalid_argument("pins must be an array");
      }
      std::vector<std::pair<double, double>> pins;
      for (const auto& pin : list.GetArray()) {
        if (!pin.IsArray() || pin.Size() != 2) {
          throw std::invalid_argument("pin must contain two coordinates");
        }
        pins.emplace_back(number(pin[0]), number(pin[1]));
      }
      if ((!net.HasMember("enabled") || flag(net, "enabled")) && pins.size() >= 2) {
        if (pins.size() > size_t(options.degree)) {
          throw std::invalid_argument("detour FLUTE degree exceeds 4096");
        }
        double length = 0;
        for (const auto& edge : dp::detail::route_tree(pins, options)) {
          length += edge.xh - edge.xl + edge.yh - edge.yl;
        }
        output << length;
      } else {
        output << "null";
      }
    }
    output << "]\n";
    if (!output) {
      throw std::runtime_error("cannot write FLUTE lengths");
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
