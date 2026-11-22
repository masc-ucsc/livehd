// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "technology_rc.hpp"
#include "steiner_net.hpp"

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
#include <unordered_map>
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
Units liberty_units(const std::string& path, std::optional<std::string_view> contents) {
  Units result;
  if (path.empty()) {
    return result;
  }
  const auto       owned = contents ? std::string{} : text(path);
  auto             view  = contents.value_or(owned);
  const std::regex first_cell(R"rc(\bcell\s*\()rc"), resistance(R"rc(pulling_resistance_unit\s*:\s*"([^"]+)")rc"),
      capacitance(R"rc(capacitive_load_unit\s*\(\s*([0-9.eE+\-]+)\s*,\s*"?([a-zA-Z]+)"?\s*\))rc");
  std::match_results<std::string_view::const_iterator> cell_match;
  if (std::regex_search(view.begin(), view.end(), cell_match, first_cell)) {
    view = view.substr(0, cell_match.position());
  }
  const std::string header(view);
  std::smatch       match;
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
  std::optional<Rc>   signal, clock;
};
Parsed_rc parse_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file, bool detailed,
                   std::optional<std::string_view> contents = std::nullopt) {
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
    auto               units = liberty_units(liberty, contents);
    auto               data  = std::regex_replace(text(rc_file), std::regex("\\\\\r?\n"), " ");
    std::istringstream lines(data);
    std::string        line;
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
        if (args.contains("-signal") || !args.contains("-clock")) {
          parsed.signal = rc;
        }
        if (args.contains("-clock") || !args.contains("-signal")) {
          parsed.clock = rc;
        }
      } else if (args.contains("-layer") && tech.layers.contains(args.at("-layer"))) {
        layers[args.at("-layer")] = rc;
        parsed.table.characterized.insert(args.at("-layer"));
      } else {
        throw std::runtime_error("technology RC refers to an unknown routing layer");
      }
    }
  }
  return parsed;
}
}  // namespace
Route_technology_rc technology_route_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file) {
  return parse_rc(tech, liberty, rc_file, true).table;
}
namespace {
constexpr double kDoubleSpacedCapacitance = 0.90;
// The layers named by a clock_layers specification, in routing order.
std::vector<std::string> clock_layer_names(const physical::Technology& tech, const Route_technology_rc& table,
                                           std::string_view spec) {
  const auto& order = tech.routing_layers;
  const auto  index = [&](const std::string& name) {
    const auto it = std::ranges::find(order, name);
    if (it == order.end()) {
      throw std::runtime_error("clock_layers names a layer that is not a routing layer: " + name);
    }
    return size_t(it - order.begin());
  };
  std::vector<std::string> names;
  if (spec == "auto") {
    for (auto it = order.rbegin(); it != order.rend() && names.size() < 2; ++it) {
      if (table.characterized.contains(*it)) {
        names.push_back(*it);
      }
    }
    std::ranges::reverse(names);
    return names;
  }
  const auto split = [](std::string_view text, char separator) {
    std::vector<std::string> parts;
    for (;;) {
      const auto cut = text.find(separator);
      parts.emplace_back(text.substr(0, cut));
      if (cut == std::string_view::npos) {
        break;
      }
      text.remove_prefix(cut + 1);
    }
    return parts;
  };
  for (const auto& item : split(spec, ',')) {
    const auto range = split(item, '-');
    if (range.size() == 1) {
      index(range.front());
      names.push_back(range.front());
    } else if (range.size() == 2) {
      auto lo = index(range[0]), hi = index(range[1]);
      if (lo > hi) {
        std::swap(lo, hi);
      }
      names.insert(names.end(), order.begin() + lo, order.begin() + hi + 1);
    } else {
      throw std::runtime_error("clock_layers needs layer names, ranges like M4-M7 or auto, got " + std::string(spec));
    }
  }
  return names;
}
// Direction-averaged RC of the layers; a direction without a layer takes the other one's average.
std::optional<Wire_rc> clock_layer_rc(const physical::Technology& tech, const Parsed_rc& parsed, std::string_view spec,
                                      const std::string& source) {
  if (spec.empty()) {
    return std::nullopt;
  }
  const auto names = clock_layer_names(tech, parsed.table, spec);
  if (names.empty()) {
    return std::nullopt;
  }
  std::array<std::pair<double, double>, 2> sum{};
  std::array<unsigned, 2>                  count{};
  for (const auto& name : names) {
    const auto rc = parsed.table.layers.find(name);
    if (rc == parsed.table.layers.end() || !(rc->second.r >= 0) || !(rc->second.c >= 0)) {
      throw std::runtime_error("clock_layers names a layer with no characterized RC: " + name);
    }
    const size_t side  = tech.layers.at(name).direction == "VERTICAL" ? 1 : 0;
    sum[side].first   += rc->second.r;
    sum[side].second  += rc->second.c;
    ++count[side];
  }
  const auto mean = [&](size_t side) {
    return count[side] > 0 ? std::pair{sum[side].first / count[side], sum[side].second / count[side]} : std::pair{-1., -1.};
  };
  auto h = mean(0), v = mean(1);
  if (h.first < 0) {
    h = v;
  }
  if (v.first < 0) {
    v = h;
  }
  std::string used;
  for (const auto& name : names) {
    used += (used.empty() ? "" : ",") + name;
  }
  // The hand-off routes the trunks with twice the track spacing, and the layer tables are for the default spacing:
  // less coupling to the neighbours, less capacitance. Against the global-route estimate of 10,105 coralnpu sinks on
  // ASAP7, the M6 and M7 average gives a mean error of 62 ps (all pessimistic) and the same resistance with 10% less
  // capacitance 9 ps (median +0.8 ps); 5% less gives 33 ps and 15% less 30 ps. The factor is that measurement.
  return Wire_rc{h.first, h.second * kDoubleSpacedCapacitance, v.first, v.second * kDoubleSpacedCapacitance,
                 "clock layers " + used + " of " + source};
}
}  // namespace
Net_wire_rc technology_net_wire_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file,
                                   double resistance, double capacitance, double rc_pessimism,
                                   std::optional<std::string_view> liberty_text, std::string_view clock_layers) {
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
    const auto wire = pessimize({resistance, capacitance, resistance, capacitance, "explicit wire_rc"});
    return {wire, wire};
  }
  const auto  parsed     = parse_rc(tech, liberty, rc_file, false, liberty_text);
  const auto& layers     = parsed.table.layers;
  const auto  with_clock = [&](const Wire_rc& signal_wire) -> Net_wire_rc {
    if (auto layered = clock_layer_rc(tech, parsed, clock_layers, rc_file.empty() ? "LEF" : rc_file)) {
      return {signal_wire, pessimize(*layered)};
    }
    if (parsed.clock) {
      const auto& clock = *parsed.clock;
      return {signal_wire, pessimize({clock.r, clock.c, clock.r, clock.c, rc_file})};
    }
    return {signal_wire, signal_wire};
  };
  if (parsed.signal) {
    const auto& signal = *parsed.signal;
    return with_clock(pessimize({signal.r, signal.c, signal.r, signal.c, rc_file}));
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
      return with_clock(pessimize({horizontal->r, horizontal->c, it->second.r, it->second.c, rc_file.empty() ? "LEF" : rc_file}));
    }
  }
  throw std::runtime_error("technology has no characterized signal or horizontal/vertical wire RC; supply synth.wire_rc");
}
Wire_rc technology_wire_rc(const physical::Technology& tech, const std::string& liberty, const std::string& rc_file,
                           double resistance, double capacitance, double rc_pessimism,
                           std::optional<std::string_view> liberty_text) {
  return technology_net_wire_rc(tech, liberty, rc_file, resistance, capacitance, rc_pessimism, liberty_text).signal;
}
std::pair<double, double> placement_pin_offset(const physical::Macro& macro, const std::string& name, physical::Orientation o, int64_t dbu) {
  auto it = macro.pins.find(name);
  if ((it == macro.pins.end() || it->second.shapes.empty()) && physical::soft_block(macro)) {
    // A soft block pin without LEF geometry connects at the block center.
    const auto [x, y] = physical::oriented_center(macro, {0, 0, macro.width, macro.height}, o);
    return {x * dbu, y * dbu};
  }
  if (it == macro.pins.end() || it->second.shapes.empty()) {
    throw std::runtime_error("missing LEF pin geometry: " + name);
  }
  const auto [x, y] = physical::oriented_center(macro, it->second.shapes.front().rect, o);
  return {x * dbu, y * dbu};
}

// Placement RC uses driver-aware Steiner trees, with half each wire's
// capacitance on each end. Coincident pins attach to a shared junction through
// 1e-9 ohm edges. If driver-aware construction exceeds its work bound, spatial
// FLUTE groups are joined by representative trees and any cycles are removed.
physical::Rc_net steiner_rc(std::string driver, const std::vector<std::pair<std::string, std::pair<double, double>>>& pins,
                      const Wire_rc& wire, double dbu, bool* approximate) {
  const auto driver_pin = std::find_if(pins.begin(), pins.end(), [&](const auto& pin) { return pin.first == driver; });
  if (driver_pin == pins.end()) {
    throw std::invalid_argument("Steiner RC requires a driver pin");
  }
  std::vector<std::pair<double, double>> xy;
  xy.reserve(pins.size());
  for (const auto& pin : pins) {
    xy.push_back(pin.second);
  }
  const auto net = steiner_net(xy, static_cast<size_t>(driver_pin - pins.begin()), per_um(wire), dbu, approximate);
  physical::Rc_net rc;
  rc.driver         = std::move(driver);
  rc.wire_length_um = net.wire_length_um;
  rc.nodes.reserve(net.node_cap_ff.size());
  for (size_t i = 0; i < net.node_cap_ff.size(); ++i) {
    rc.nodes.push_back({i < pins.size() ? Pin_label(pins[i].first) : Pin_label(), net.node_cap_ff[i]});
  }
  rc.edges.reserve(net.edges.size());
  for (const auto& edge : net.edges) {
    rc.edges.push_back({edge.from, edge.to, edge.resistance_ohm});
  }
  return rc;
}
physical::Parasitics estimate_floorplan_rc(const physical::Floorplan& fp, const physical::Technology& tech,
                                          const Net_wire_rc& wire) {
  return estimate_floorplan_rc(fp, tech, wire, {});
}

physical::Parasitics estimate_floorplan_rc(const physical::Floorplan& fp, const physical::Technology& tech,
                                          const Net_wire_rc& wire, const std::map<std::string, Wire_rc>& driver_rc, std::optional<std::span<const size_t>> subset) {
  std::unordered_map<std::string_view, const physical::Component*> instances;
  for (const auto& c : fp.components) {
    instances.emplace(c.name.view(), &c);
  }
  std::unordered_map<std::string_view, const physical::Terminal*> ports;
  for (const auto& t : fp.terminals) {
    ports.emplace(t.name, &t);
  }
  physical::Parasitics result;
  result.identity = wire.signal.source;
  const size_t count=subset?subset->size():fp.nets.size();
  for (size_t i=0;i<count;++i) {
    const auto& net=fp.nets.at(subset?(*subset)[i]:i);
    if (net.connections.size() < 2 || net.use == "POWER" || net.use == "GROUND") {
      continue;
    }
    std::vector<std::pair<std::string, std::pair<double, double>>> pins;
    std::string                                                    driver;
    for (const auto& conn : net.connections) {
      double x, y;
      bool   output;
      if (conn.instance == "PIN") {
        const auto& t = *ports.at(conn.pin.view());
        if (t.status == physical::Status::UNPLACED) {
          throw std::invalid_argument("missing placement for port " + t.name);
        }
        x      = t.x;
        y      = t.y;
        output = t.direction == "INPUT";
      } else {
        const auto& c      = *instances.at(conn.instance.view());
        const auto& master = tech.macros.at(c.macro);
        if (c.place.status == physical::Status::UNPLACED) {
          throw std::invalid_argument("missing placement for cell " + c.name);
        }
        const auto offset = placement::placement_pin_offset(master, conn.pin, c.place.orientation, fp.dbu);
        x                 = c.place.x + offset.first;
        y                 = c.place.y + offset.second;
        output            = master.pins.at(conn.pin).direction == "OUTPUT";
      }
      const auto label = physical::connection_label(conn);
      pins.emplace_back(label, std::pair{x, y});
      if (output) {
        if (!driver.empty()) {
          throw std::invalid_argument("multiple physical drivers on " + net.name);
        }
        driver = label;
      }
    }
    if (driver.empty()) {
      throw std::invalid_argument("no physical driver on " + net.name);
    }
    const auto override = driver_rc.find(driver);
    const auto& rc = override != driver_rc.end() ? override->second : (net.use == "CLOCK" ? wire.clock : wire.signal);
    result.nets.push_back(placement::steiner_rc(driver, pins, rc, fp.dbu));
  }
  return result;
}
}  // namespace livehd::placement
