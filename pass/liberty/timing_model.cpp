// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "timing_model.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace livehd::liberty::timing {
namespace {
struct Unsupported : std::invalid_argument {
  using std::invalid_argument::invalid_argument;
};
struct Token {
  std::string text;
  bool        quoted = false;
};
class Lexer {
  std::string_view text;
  size_t           position = 0;

public:
  explicit Lexer(std::string_view source) : text(source) {}
  size_t offset() const { return position; }
  Token  next() {
    while (position < text.size()) {
      const auto c = text[position];
      if (std::isspace(static_cast<unsigned char>(c)) || (c == '\\' && position + 1 < text.size() && text[position + 1] == '\n')) {
        ++position;
      } else if (text.substr(position, 2) == "//") {
        const auto end = text.find('\n', position + 2);
        position       = end == text.npos ? text.size() : end;
      } else if (text.substr(position, 2) == "/*") {
        const auto end = text.find("*/", position + 2);
        if (end == text.npos) {
          throw std::invalid_argument("unterminated Liberty comment");
        }
        position = end + 2;
      } else {
        break;
      }
    }
    if (position == text.size()) {
      return {};
    }
    const auto begin = position++;
    const char c     = text[begin];
    if (c == '"') {
      std::string value;
      while (position < text.size()) {
        const char ch = text[position++];
        if (ch == '"') {
          return {std::move(value), true};
        }
        if (ch == '\\' && position < text.size()) {
          const char escaped = text[position++];
          if (escaped != '\n') {
            value.push_back(escaped);
          }
        } else {
          value.push_back(ch);
        }
      }
      throw std::invalid_argument("unterminated Liberty string");
    }
    if (std::string_view("{}():;,").find(c) != std::string_view::npos) {
      return {std::string(1, c)};
    }
    while (position < text.size() && !std::isspace(static_cast<unsigned char>(text[position]))
           && std::string_view("{}():;,\"").find(text[position]) == std::string_view::npos) {
      if (text.substr(position, 2) == "/*" || text.substr(position, 2) == "//") {
        break;
      }
      ++position;
    }
    return {std::string(text.substr(begin, position - begin))};
  }
};
struct Group {
  std::string                                     kind;
  std::vector<std::string>                        arguments;
  std::map<std::string, std::vector<std::string>> attributes;
  std::vector<Group>                              groups;
};
class Parser {
  Lexer lexer;
  Token token;
  void  advance() { token = lexer.next(); }
  bool  is(std::string_view value) const { return !token.quoted && token.text == value; }
  void  require(std::string_view value) {
    if (!is(value)) {
      throw std::invalid_argument("Liberty expected '" + std::string(value) + "', got '" + token.text + "'");
    }
    advance();
  }
  std::vector<std::string> arguments() {
    require("(");
    std::vector<std::string> result;
    std::string              value;
    int                      depth = 0;
    while (!token.text.empty() || token.quoted) {
      if (depth == 0 && (is(",") || is(")"))) {
        if (!value.empty()) {
          result.push_back(std::move(value));
        }
        value.clear();
        if (is(")")) {
          advance();
          return result;
        }
        advance();
        continue;
      }
      if (is("(")) {
        ++depth;
      }
      if (is(")")) {
        --depth;
      }
      if (!value.empty()) {
        value += ' ';
      }
      value += token.text;
      advance();
    }
    throw std::invalid_argument("unterminated Liberty argument list");
  }
  void body(Group& owner, bool nested) {
    while (!token.text.empty() || token.quoted) {
      if (is("}")) {
        if (!nested) {
          throw std::invalid_argument("unexpected Liberty closing brace");
        }
        advance();
        return;
      }
      if (is(";")) {
        advance();
        continue;
      }
      if (!token.quoted && std::string_view("{():,").find(token.text) != std::string_view::npos) {
        throw std::invalid_argument("invalid Liberty attribute/group name: " + token.text);
      }
      const auto key = token.text;
      advance();
      if (is(":")) {
        advance();
        // Liberty simple attributes have one scalar/string value. Several
        // shipped ASAP7 libraries omit its trailing semicolon (notably area).
        // Do not consume the following group while looking for that semicolon.
        if ((!token.quoted && token.text.empty()) || is("}") || is("{") || is(";")) {
          throw std::invalid_argument("missing Liberty attribute value: " + key);
        }
        std::string value = token.text;
        advance();
        owner.attributes[key] = {std::move(value)};
        if (is(";")) {
          advance();
        }
      } else if (is("(")) {
        auto args = arguments();
        if (is("{")) {
          advance();
          Group child{key, std::move(args)};
          body(child, true);
          owner.groups.push_back(std::move(child));
        } else {
          owner.attributes[key] = std::move(args);
          if (is(";")) {
            advance();
          }
        }
      } else {
        throw std::invalid_argument("Liberty attribute/group has no ':' or '(': " + key);
      }
    }
    if (nested) {
      throw std::invalid_argument("unterminated Liberty group: " + owner.kind);
    }
  }

public:
  explicit Parser(std::string_view source) : lexer(source) { advance(); }
  Group parse() {
    Group root;
    try {
      body(root, false);
    } catch (const std::invalid_argument& error) {
      throw std::invalid_argument(std::string(error.what()) + " near byte " + std::to_string(lexer.offset()));
    }
    return root;
  }
};
std::string attr(const Group& group, const std::string& key, std::string fallback = {}) {
  const auto it = group.attributes.find(key);
  return it == group.attributes.end() || it->second.empty() ? fallback : it->second.front();
}
double numeric(std::string_view text) {
  if (text.starts_with('+')) {
    text.remove_prefix(1);
  }
  double value            = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) {
    throw std::invalid_argument("invalid Liberty number: " + std::string(text));
  }
  return value;
}
std::optional<double> number(const Group& group, const std::string& key, double scale = 1) {
  if (!group.attributes.contains(key)) {
    return {};
  }
  const auto value = numeric(attr(group, key)) * scale;
  if (!std::isfinite(value)) {
    throw std::invalid_argument("Liberty unit conversion overflow: " + key);
  }
  return value;
}
std::vector<double> numbers(const std::vector<std::string>& arguments) {
  std::vector<double> result;
  for (auto argument : arguments) {
    std::replace(argument.begin(), argument.end(), ',', ' ');
    std::istringstream input(argument);
    std::string        token;
    while (input >> token) {
      result.push_back(numeric(token));
    }
  }
  return result;
}
std::string group_name(const Group& group) {
  if (group.arguments.size() != 1 || group.arguments.front().empty()) {
    throw std::invalid_argument("Liberty group requires one name: " + group.kind);
  }
  return group.arguments.front();
}
double time_scale(const Group& group) {
  const auto unit         = attr(group, "time_unit", "1ns");
  double     value        = 0;
  const auto [end, error] = std::from_chars(unit.data(), unit.data() + unit.size(), value);
  std::string suffix(end, unit.data() + unit.size());
  std::erase_if(suffix, [](unsigned char c) { return std::isspace(c); });
  const std::map<std::string, double> scales{
      {"fs", .001},
      {"ps",    1},
      {"ns", 1000},
      {"us",  1e6},
      {"ms",  1e9},
      { "s", 1e12}
  };
  if (error != std::errc{} || !std::isfinite(value) || value <= 0 || !scales.contains(suffix)) {
    throw std::invalid_argument("invalid Liberty time_unit: " + unit);
  }
  return value * scales.at(suffix);
}
double cap_scale(const Group& group) {
  const auto entry = group.attributes.find("capacitive_load_unit");
  if (entry == group.attributes.end()) {
    return 1000;
  }
  if (entry->second.size() != 2) {
    throw std::invalid_argument("invalid capacitive_load_unit");
  }
  const auto&                         args = entry->second;
  const std::map<std::string, double> scales{
      {"af", .001},
      {"ff",    1},
      {"pf", 1000},
      {"nf",  1e6},
      {"uf",  1e9},
      { "f", 1e15}
  };
  const auto value = numeric(args[0]);
  if (value <= 0 || !scales.contains(args[1])) {
    throw std::invalid_argument("invalid capacitive_load_unit");
  }
  return value * scales.at(args[1]);
}
Variable variable(const std::string& name) {
  if (name.empty()) {
    return Variable::none;
  }
  if (name == "input_net_transition" || name == "input_transition_time") {
    return Variable::input_slew;
  }
  if (name == "total_output_net_capacitance") {
    return Variable::output_load;
  }
  if (name == "related_pin_transition") {
    return Variable::related_slew;
  }
  if (name == "constrained_pin_transition") {
    return Variable::constrained_slew;
  }
  throw Unsupported("unsupported NLDM axis variable: " + name);
}
struct Units {
  double                              time = 1000, cap = 1000;
  std::map<std::string, const Group*> templates, types;
};
Table table(const Group& group, const Units& units) {
  const auto name  = group_name(group);
  const auto found = units.templates.find(name);
  if (found == units.templates.end() && name != "scalar") {
    throw Unsupported("missing NLDM template: " + name);
  }
  const Group empty;
  const auto& model = found == units.templates.end() ? empty : *found->second;
  if (model.attributes.contains("variable_3") || group.attributes.contains("index_3")) {
    throw Unsupported("three-dimensional NLDM tables are unsupported");
  }
  Table      result;
  const auto axis = [&](const char* v, const char* indices, Variable& kind, std::vector<double>& coordinates) {
    kind               = variable(attr(model, v));
    const auto& source = group.attributes.contains(indices) ? group : model;
    if (const auto it = source.attributes.find(indices); it != source.attributes.end()) {
      coordinates = numbers(it->second);
    }
    const double scale = kind == Variable::output_load ? units.cap : units.time;
    for (auto& coordinate : coordinates) {
      coordinate *= scale;
    }
  };
  axis("variable_1", "index_1", result.x_variable, result.x);
  axis("variable_2", "index_2", result.y_variable, result.y);
  const auto values = group.attributes.find("values");
  if (values == group.attributes.end()) {
    throw std::invalid_argument("NLDM table has no values");
  }
  result.values = numbers(values->second);
  for (auto& value : result.values) {
    value *= units.time;
  }
  result.validate();
  return result;
}
Thresholds thresholds(const Group& library) {
  Thresholds result;
  for (size_t i = 0; i < 2; ++i) {
    const std::string transition = i == 0 ? "rise" : "fall";
    result.input[i]              = number(library, "input_threshold_pct_" + transition, .01).value_or(.5);
    result.output[i]             = number(library, "output_threshold_pct_" + transition, .01).value_or(.5);
    result.lower[i]              = number(library, "slew_lower_threshold_pct_" + transition, .01).value_or(.2);
    result.upper[i]              = number(library, "slew_upper_threshold_pct_" + transition, .01).value_or(.8);
  }
  result.slew_derate = number(library, "slew_derate_from_library").value_or(1);
  result.validate();
  return result;
}
Arc arc(const Group& group, const Units& units) {
  Arc result;
  result.related_pin = attr(group, "related_pin", attr(group, "related_bus_pins"));
  result.type        = attr(group, "timing_type", "combinational");
  result.when        = attr(group, "when");
  const auto sense   = attr(group, "timing_sense");
  if (sense == "positive_unate") {
    result.sense = Sense::positive;
  } else if (sense == "negative_unate") {
    result.sense = Sense::negative;
  } else if (sense == "non_unate") {
    result.sense = Sense::non_unate;
  }
  for (const auto& child : group.groups) {
    std::optional<Table>* target = nullptr;
    if (child.kind == "cell_rise") {
      target = &result.delay[0];
    } else if (child.kind == "cell_fall") {
      target = &result.delay[1];
    } else if (child.kind == "rise_transition") {
      target = &result.slew[0];
    } else if (child.kind == "fall_transition") {
      target = &result.slew[1];
    } else if (child.kind == "rise_constraint") {
      target = &result.constraint[0];
    } else if (child.kind == "fall_constraint") {
      target = &result.constraint[1];
    }
    if (!target) {
      continue;
    }
    if (*target) {
      throw std::invalid_argument("duplicate NLDM table: " + child.kind);
    }
    try {
      *target = table(child, units);
    } catch (const Unsupported& error) {
      result.unsupported.push_back(child.kind + ": " + error.what());
    }
  }
  return result;
}
Pin pin(const Group& group, const Group& library, const Units& units) {
  Pin result;
  result.name         = group_name(group);
  result.direction    = attr(group, "direction");
  result.function     = attr(group, "function");
  result.clock        = attr(group, "clock") == "true";
  result.three_state  = group.attributes.contains("three_state");
  const auto common   = number(group, "capacitance", units.cap);
  const auto fallback = number(library, "default_" + result.direction + "_pin_cap", units.cap);
  for (size_t i = 0; i < 2; ++i) {
    result.capacitance_ff[i] = number(group, i == 0 ? "rise_capacitance" : "fall_capacitance", units.cap);
    if (!result.capacitance_ff[i]) {
      result.capacitance_ff[i] = common ? common : fallback;
    }
    if (result.capacitance_ff[i] && *result.capacitance_ff[i] < 0) {
      throw std::invalid_argument("negative Liberty pin capacitance");
    }
  }
  result.max_capacitance_ff = number(group, "max_capacitance", units.cap);
  result.max_transition_ps  = number(group, "max_transition", units.time);
  if (!result.max_transition_ps) {
    result.max_transition_ps = number(library, "default_max_transition", units.time);
  }
  if ((result.max_capacitance_ff && *result.max_capacitance_ff < 0)
      || (result.max_transition_ps && *result.max_transition_ps < 0)) {
    throw std::invalid_argument("negative Liberty electrical limit");
  }
  for (const auto& child : group.groups) {
    if (child.kind == "timing") {
      auto               model = arc(child, units);
      std::istringstream related(model.related_pin);
      std::string        name;
      bool               any = false;
      while (related >> name) {
        auto copy        = model;
        copy.related_pin = name;
        result.arcs.push_back(std::move(copy));
        any = true;
      }
      if (!any) {
        result.arcs.push_back(std::move(model));
      }
    }
  }
  return result;
}
std::pair<int, int> bus_range(const Group& group, const Units& units) {
  const auto type = units.types.find(attr(group, "bus_type"));
  if (type == units.types.end()) {
    throw std::invalid_argument("missing bus type: " + attr(group, "bus_type"));
  }
  const auto& model = *type->second;
  const auto  width = number(model, "bit_width").value_or(0);
  const auto  from = number(model, "bit_from").value_or(width - 1), to = number(model, "bit_to").value_or(0);
  if (width < 1 || width > 100000 || from != std::floor(from) || to != std::floor(to) || std::abs(from - to) + 1 != width
      || from < 0 || to < 0 || from > 1000000 || to > 1000000) {
    throw std::invalid_argument("invalid Liberty bus range");
  }
  return {static_cast<int>(from), static_cast<int>(to)};
}
double coordinate(Variable kind, const Query& query) {
  switch (kind) {
    case Variable::none            : return 0;
    case Variable::input_slew      : return query.input_slew_ps;
    case Variable::output_load     : return query.output_load_ff;
    case Variable::related_slew    : return query.related_slew_ps;
    case Variable::constrained_slew: return query.constrained_slew_ps;
  }
  throw std::invalid_argument("invalid NLDM variable");
}
struct Interval {
  size_t lo = 0, hi = 0;
  double fraction = 0;
};
Interval interval(const std::vector<double>& axis, double value) {
  if (!std::isfinite(value) || value < 0) {
    throw std::invalid_argument("NLDM query must be finite and nonnegative");
  }
  if (axis.size() < 2) {
    return {};
  }
  const auto   upper = std::upper_bound(axis.begin(), axis.end(), value);
  const size_t hi = std::clamp<size_t>(static_cast<size_t>(upper - axis.begin()), 1, axis.size() - 1), lo = hi - 1;
  return {lo, hi, (value - axis[lo]) / (axis[hi] - axis[lo])};
}
}  // namespace

void Table::validate() const {
  const auto axis = [](Variable kind, const std::vector<double>& data) {
    if ((kind == Variable::none) != data.empty()) {
      throw std::invalid_argument("NLDM axis has missing variable or indices");
    }
    for (size_t i = 0; i < data.size(); ++i) {
      if (!std::isfinite(data[i]) || data[i] < 0 || (i && data[i] <= data[i - 1])) {
        throw std::invalid_argument("NLDM indices must be finite, nonnegative and strictly increasing");
      }
    }
  };
  axis(x_variable, x);
  axis(y_variable, y);
  if (x_variable == Variable::none && y_variable != Variable::none) {
    throw std::invalid_argument("NLDM variable_2 without variable_1");
  }
  if (x_variable != Variable::none && x_variable == y_variable) {
    throw std::invalid_argument("duplicate NLDM axis variable");
  }
  if (values.size() != std::max<size_t>(1, x.size()) * std::max<size_t>(1, y.size())) {
    throw std::invalid_argument("NLDM value count does not match table axes");
  }
  for (auto value : values) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("nonfinite NLDM value");
    }
  }
}
double Table::lookup(const Query& query) const {
  const auto nx = std::max<size_t>(1, x.size()), ny = std::max<size_t>(1, y.size());
  if (values.size() != nx * ny) {
    throw std::invalid_argument("invalid NLDM table dimensions");
  }
  const auto a = interval(x, coordinate(x_variable, query)), b = interval(y, coordinate(y_variable, query));
  const auto low  = std::lerp(values[a.lo * ny + b.lo], values[a.lo * ny + b.hi], b.fraction);
  const auto high = std::lerp(values[a.hi * ny + b.lo], values[a.hi * ny + b.hi], b.fraction);
  return std::lerp(low, high, a.fraction);
}
Table Table::output_load_slice(const Query& fixed) const {
  Table result;
  if (x_variable == Variable::output_load) {
    result.x = x;
  } else if (y_variable == Variable::output_load) {
    result.x = y;
  } else {
    result.values = {lookup(fixed)};
    return result;
  }
  result.x_variable = Variable::output_load;
  auto query        = fixed;
  for (double cap : result.x) {
    query.output_load_ff = cap;
    result.values.push_back(lookup(query));
  }
  return result;
}
void Thresholds::validate() const {
  if (!std::isfinite(slew_derate) || slew_derate <= 0) {
    throw std::invalid_argument("invalid Liberty slew derate");
  }
  for (size_t i = 0; i < 2; ++i) {
    for (auto value : {input[i], output[i], lower[i], upper[i]}) {
      if (!std::isfinite(value) || value < 0 || value > 1) {
        throw std::invalid_argument("invalid Liberty threshold");
      }
    }
    if (lower[i] >= upper[i]) {
      throw std::invalid_argument("invalid Liberty slew threshold range");
    }
  }
}
Library parse(std::string_view text) {
  const auto syntax = Parser(text).parse();
  Library    result;
  bool       first = true;
  for (const auto& library : syntax.groups) {
    if (library.kind != "library") {
      throw std::invalid_argument("top-level Liberty group is not a library");
    }
    const auto library_name = group_name(library);
    Units      units{time_scale(library), cap_scale(library)};
    const auto levels = thresholds(library);
    if (first) {
      result.default_thresholds = levels;
      first                     = false;
    }
    for (const auto& group : library.groups) {
      if (group.kind == "lu_table_template") {
        if (!units.templates.emplace(group_name(group), &group).second) {
          throw std::invalid_argument("duplicate NLDM template");
        }
      } else if (group.kind == "type") {
        if (!units.types.emplace(group_name(group), &group).second) {
          throw std::invalid_argument("duplicate Liberty bus type");
        }
      }
    }
    for (const auto& group : library.groups) {
      if (group.kind != "cell") {
        continue;
      }
      Cell cell;
      cell.name       = group_name(group);
      cell.library    = library_name;
      cell.thresholds = levels;
      cell.area       = number(group, "area").value_or(0);
      cell.clock_gate = !attr(group, "clock_gating_integrated_cell").empty();
      if (attr(library, "delay_model", "table_lookup") != "table_lookup") {
        cell.unsupported.push_back("library delay_model is not table_lookup");
      }
      auto local_units = units;
      for (const auto& child : group.groups) {
        if (child.kind == "ff" || child.kind == "ff_bank") {
          cell.sequential = true;
        }
        if (child.kind == "latch" || child.kind == "latch_bank") {
          cell.sequential = cell.latch = true;
        }
        if (child.kind == "memory") {
          cell.sequential = cell.memory = true;
        }
        if (child.kind == "type") {
          local_units.types[group_name(child)] = &child;
        }
      }
      for (const auto& child : group.groups) {
        if (child.kind != "pin" && child.kind != "bus") {
          continue;
        }
        auto model = pin(child, library, local_units);
        if (child.kind == "bus") {
          try {
            const auto [from, to] = bus_range(child, local_units);
            bool explicit_members = false;
            for (const auto& member_group : child.groups) {
              if (member_group.kind != "pin") {
                continue;
              }
              explicit_members  = true;
              const auto name   = group_name(member_group);
              const auto prefix = model.name + "[";
              if (!name.starts_with(prefix) || !name.ends_with(']')) {
                throw Unsupported("unsupported Liberty bus member name: " + name);
              }
              const auto bit = numeric(std::string_view(name).substr(prefix.size(), name.size() - prefix.size() - 1));
              if (bit != std::floor(bit) || bit < std::min(from, to) || bit > std::max(from, to)) {
                throw std::invalid_argument("Liberty bus member outside declared type");
              }
              Group inherited     = child;
              inherited.kind      = "pin";
              inherited.arguments = {name};
              std::erase_if(inherited.groups, [](const auto& g) { return g.kind == "pin"; });
              for (const auto& [key, attribute] : member_group.attributes) {
                inherited.attributes[key] = attribute;
              }
              for (const auto& local : member_group.groups) {
                if (local.kind == "timing") {
                  std::erase_if(inherited.groups, [&](const auto& g) {
                    return g.kind == "timing" && attr(g, "related_pin") == attr(local, "related_pin")
                           && attr(g, "timing_type", "combinational") == attr(local, "timing_type", "combinational")
                           && attr(g, "when") == attr(local, "when");
                  });
                }
                inherited.groups.push_back(local);
              }
              auto member = pin(inherited, library, local_units);
              if (!cell.pins.emplace(name, std::move(member)).second) {
                throw std::invalid_argument("duplicate Liberty bus member");
              }
            }
            if (!explicit_members) {
              for (int bit = std::min(from, to); bit <= std::max(from, to); ++bit) {
                auto member = model;
                member.name = model.name + "[" + std::to_string(bit) + "]";
                if (!cell.pins.emplace(member.name, std::move(member)).second) {
                  throw std::invalid_argument("duplicate Liberty bus bit");
                }
              }
            }
          } catch (const std::invalid_argument& error) {
            cell.unsupported.push_back(error.what());
          }
        } else if (!cell.pins.emplace(model.name, std::move(model)).second) {
          throw std::invalid_argument("duplicate Liberty pin");
        }
      }
      if (!result.cells.emplace(cell.name, std::move(cell)).second) {
        throw std::invalid_argument("duplicate Liberty cell name across libraries/corners");
      }
    }
  }
  if (syntax.groups.empty()) {
    throw std::invalid_argument("no Liberty library group");
  }
  return result;
}
}  // namespace livehd::liberty::timing
