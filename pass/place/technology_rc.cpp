// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "technology_rc.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace livehd::placement {
namespace {
using Rc = Layer_rc;
std::string text(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot read technology RC input: " + path);
  }
  std::string result;
  std::array<char, 65536> buffer;
  while (in.read(buffer.data(), buffer.size()) || in.gcount()) {
    result.append(buffer.data(), static_cast<size_t>(in.gcount()));
  }
  if (!in.eof()) {
    throw std::runtime_error("incomplete technology RC input: " + path);
  }
  return result;
}
std::string unquote(std::string value) {
  while (!value.empty() && (value.front() == '"' || value.front() == '{')) {
    value.erase(value.begin());
  }
  while (!value.empty() && (value.back() == '"' || value.back() == '}')) {
    value.pop_back();
  }
  return value;
}
double number(const std::string& value) {
  char*      end    = nullptr;
  const auto result = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || *end || !std::isfinite(result) || result < 0) {
    throw std::runtime_error("technology RC requires a finite nonnegative numeric literal: " + value);
  }
  return result;
}
double scale(std::string value, bool resistance) {
  value = unquote(value);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
  char*  end        = nullptr;
  double multiplier = std::strtod(value.c_str(), &end);
  if (end == value.c_str()) {
    multiplier = 1;
    end        = value.data();
  }
  if (!std::isfinite(multiplier) || multiplier <= 0) {
    throw std::runtime_error("invalid technology RC units: " + value);
  }
  const std::string unit = end;
  const std::map<std::string,double> units = resistance ? std::map<std::string,double>{{"ohm",1},{"kohm",1000},{"mohm",1000000}}
                                                       : std::map<std::string,double>{{"ff",1},{"pf",1000},{"nf",1e6},{"uf",1e9},{"f",1e15}};
  const auto it = units.find(unit);
  if (it == units.end()) {
    throw std::runtime_error("unsupported technology RC units: " + value);
  }
  return multiplier * it->second;
}
struct Units {
  double r = 0, c = 0, distance_um = 1;
};
Units liberty_units(const std::string& path) {
  Units result;
  if (path.empty()) {
    return result;
  }
  auto             header = text(path);
  const std::regex first_cell(R"rc(\bcell\s*\()rc"), resistance(R"rc(pulling_resistance_unit\s*:\s*"([^"]+)")rc"),
      capacitance(R"rc(capacitive_load_unit\s*\(\s*([0-9.eE+\-]+)\s*,\s*"?([a-zA-Z]+)"?\s*\))rc");
  std::smatch match;
  if (std::regex_search(header, match, first_cell)) {
    header.resize(match.position());
  }
  if (std::regex_search(header, match, resistance)) {
    result.r = scale(match[1], true);
  }
  if (std::regex_search(header, match, capacitance)) {
    result.c = number(match[1]) * scale(match[2], false);
  }
  return result;
}
bool valid(const Rc& rc) { return rc.r >= 0 && rc.c >= 0 && std::isfinite(rc.r) && std::isfinite(rc.c); }
struct Parsed_rc {
  Route_technology_rc table;
  std::optional<Rc>   signal;
};
Parsed_rc parse_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file, bool detailed) {
  Parsed_rc parsed;
  auto&     layers    = parsed.table.layers;
  parsed.table.source = rc_file.empty() ? "LEF" : rc_file;
  for (const auto& [name, via] : tech.vias) {
    if (std::isfinite(via.resistance) && via.resistance > 0) {
      parsed.table.via_resistance_ohm[name] = via.resistance;
    }
  }
  for (const auto& [name, l] : tech.layers) {
    if (l.width > 0 && l.resistance > 0 && (l.capacitance > 0 || l.edge_cap > 0)) {
      layers[name] = {l.resistance / l.width, (l.capacitance * l.width + 2 * l.edge_cap) * 1000};
    }
  }
  if (!rc_file.empty()) {
    auto               units = liberty_units(liberty);
    auto               data  = std::regex_replace(text(rc_file), std::regex("\\\\\r?\n"), " ");
    std::istringstream lines(data);
    std::string        line;
    std::optional<Rc>  signal;
    unsigned           line_number = 0;
    while (std::getline(lines, line)) {
      ++line_number;
      line.erase(line.find('#') == std::string::npos ? line.size() : line.find('#'));
      std::istringstream       input(line);
      std::vector<std::string> words;
      for (std::string word; input >> word;) {
        words.push_back(unquote(word));
      }
      if (words.empty()) {
        continue;
      }
      const auto& command = words.front();
      if (command != "set_wire_rc" && command != "set_layer_rc" && command != "set_cmd_units" && command != "set_units") {
        throw std::runtime_error(rc_file + ":" + std::to_string(line_number)
                                 + ": unsupported technology RC data command: " + command);
      }
      std::map<std::string, std::string> args;
      for (size_t i = 1; i < words.size(); ++i) {
        const auto key = words[i];
        if (key == "-signal" || key == "-clock") {
          args[key] = "true";
        } else {
          if (++i == words.size()) {
            throw std::runtime_error("missing technology RC option value: " + key);
          }
          args[key] = words[i];
        }
      }
      const std::vector<std::string> allowed
          = command == "set_cmd_units" || command == "set_units"
                ? std::vector<std::string>{"-resistance", "-capacitance", "-distance", "-time", "-power", "-voltage", "-current"}
                : std::vector<std::string>{"-signal", "-clock", "-resistance", "-capacitance", "-layer", "-via"};
      for (const auto& [key, value] : args) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
          throw std::runtime_error(rc_file + ":" + std::to_string(line_number) + ": unsupported technology RC option: " + key);
        }
      }
      if (command == "set_cmd_units" || command == "set_units") {
        if (args.contains("-resistance")) {
          units.r = scale(args.at("-resistance"), true);
        }
        if (args.contains("-capacitance")) {
          units.c = scale(args.at("-capacitance"), false);
        }
        if (args.contains("-distance")) {
          const std::map<std::string, double> distances{
              {"nm", .001},
              {"um",    1},
              {"mm", 1000},
              { "m",  1e6}
          };
          const auto it = distances.find(args.at("-distance"));
          if (it == distances.end()) {
            throw std::runtime_error("unsupported technology RC distance units");
          }
          units.distance_um = it->second;
        }
        continue;
      }
      if (command == "set_layer_rc" && args.contains("-via")) {
        if (detailed) {
          if (args.size() != 2 || !args.contains("-resistance") || !tech.cut_layers.contains(args.at("-via"))) {
            throw std::runtime_error("technology RC requires a known cut layer and its via resistance");
          }
          if (!(units.r > 0)) {
            throw std::runtime_error("technology via RC needs resistance units");
          }
          // A via is a lumped resistor; distance units do not scale it.
          const auto resistance = number(args.at("-resistance")) * units.r;
          if (!std::isfinite(resistance)) {
            throw std::runtime_error("unrepresentable via resistance");
          }
          parsed.table.cut_resistance_ohm[args.at("-via")] = resistance;
        }
        continue;
      }
      if (command == "set_wire_rc" && args.contains("-clock") && !args.contains("-signal")) {
        continue;
      }
      Rc rc;
      if (args.contains("-layer") && command == "set_wire_rc") {
        const auto it = layers.find(args.at("-layer"));
        if (it == layers.end()) {
          throw std::runtime_error("technology RC layer has no characterized RC: " + args.at("-layer"));
        }
        rc = it->second;
      }
      if (args.contains("-resistance")) {
        if (!(units.r > 0)) {
          throw std::runtime_error("technology RC needs resistance units in set_cmd_units or the main Liberty header");
        }
        rc.r = number(args.at("-resistance")) * units.r / units.distance_um;
      }
      if (args.contains("-capacitance")) {
        if (!(units.c > 0)) {
          throw std::runtime_error("technology RC needs capacitance units in set_cmd_units or the main Liberty header");
        }
        rc.c = number(args.at("-capacitance")) * units.c / units.distance_um;
      }
      if (!valid(rc)) {
        throw std::runtime_error(rc_file + ":" + std::to_string(line_number) + ": incomplete technology wire RC");
      }
      if (command == "set_wire_rc") {
        signal = rc;
      } else if (args.contains("-layer") && tech.layers.contains(args.at("-layer"))) {
        layers[args.at("-layer")] = rc;
      } else {
        throw std::runtime_error("technology RC refers to an unknown routing layer");
      }
    }
    parsed.signal = signal;
  }
  return parsed;
}
}  // namespace
Route_technology_rc technology_route_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file) {
  return parse_rc(tech, liberty, rc_file, true).table;
}
Wire_rc technology_wire_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file,
                           double resistance, double capacitance, double rc_pessimism) {
  if (!std::isfinite(rc_pessimism) || rc_pessimism < 1) {
    throw std::invalid_argument("rc_pessimism must be finite and at least 1");
  }
  const auto pessimize = [factor = std::sqrt(rc_pessimism)](Wire_rc wire) {
    wire.horizontal_r *= factor;
    wire.horizontal_c *= factor;
    wire.vertical_r   *= factor;
    wire.vertical_c   *= factor;
    if (!valid({wire.horizontal_r, wire.horizontal_c}) || !valid({wire.vertical_r, wire.vertical_c})) {
      throw std::invalid_argument("rc_pessimism overflows wire RC coefficients");
    }
    return wire;
  };
  if (resistance >= 0 && capacitance >= 0) {
    if (!valid({resistance, capacitance})) {
      throw std::runtime_error("invalid explicit wire RC");
    }
    return pessimize({resistance, capacitance, resistance, capacitance, "explicit wire_rc"});
  }
  const auto  parsed = parse_rc(tech, liberty, rc_file, false);
  const auto& layers = parsed.table.layers;
  if (parsed.signal) {
    const auto& signal = *parsed.signal;
    return pessimize({signal.r, signal.c, signal.r, signal.c, rc_file});
  }
  std::optional<Rc> horizontal;
  for (const auto& name : tech.routing_layers) {
    const auto it = layers.find(name);
    if (it == layers.end()) {
      continue;
    }
    const auto& l = tech.layers.at(name);
    if (!horizontal && l.direction == "HORIZONTAL") {
      horizontal = it->second;
    } else if (horizontal && l.direction == "VERTICAL") {
      return pessimize({horizontal->r, horizontal->c, it->second.r, it->second.c, rc_file.empty() ? "LEF" : rc_file});
    }
  }
  throw std::runtime_error(
      "technology has no characterized signal or horizontal/vertical wire RC; supply synth.place.tech_rc or synth.place.wire_rc");
}
}  // namespace livehd::placement
