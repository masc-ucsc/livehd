// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sdc_subset.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace livehd::placement::late {
namespace {
struct Word {
  std::string value;
  bool        command = false;
};
[[noreturn]] void fail(const std::string& message) { throw std::invalid_argument("saved-SDC subset: " + message); }
class Words {
  std::string_view text;
  size_t           at        = 0;
  bool             data_list = false;
  std::string      group(char open, char close) {
    const size_t first = ++at;
    unsigned     depth = 1;
    while (at < text.size()) {
      if (text[at] == '\\') {
        at += 2;
        continue;
      }
      if (open == '[' && text[at] == '{') {
        group('{', '}');
        continue;
      }
      if (text[at] == open) {
        ++depth;
      }
      if (text[at] == close && --depth == 0) {
        const auto value = std::string(text.substr(first, at - first));
        ++at;
        return value;
      }
      ++at;
    }
    fail("unterminated group");
  }

public:
  explicit Words(std::string_view source, bool list = false) : text(source), data_list(list) {}
  std::vector<Word> parse() {
    std::vector<Word> result;
    while (at < text.size()) {
      if (std::isspace(static_cast<unsigned char>(text[at]))) {
        ++at;
        continue;
      }
      Word word;
      if (text[at] == '{') {
        word.value = group('{', '}');
      } else if (text[at] == '[' && !data_list) {
        word.command = true;
        word.value   = group('[', ']');
      } else {
        const bool quoted = text[at] == '"';
        if (quoted) {
          ++at;
        }
        bool closed = !quoted;
        while (at < text.size()) {
          char c = text[at++];
          if (quoted && c == '"') {
            closed = true;
            break;
          }
          if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
            --at;
            break;
          }
          if (c == '\\') {
            if (at >= text.size()) {
              fail("dangling escape");
            }
            c = text[at++];
            if (std::string_view("ntrvfxu01234567").find(c) != std::string_view::npos) {
              fail("unsupported Tcl escape");
            }
          } else if (!data_list && (c == '$' || c == '[' || c == '{' || c == '}' || c == ']')) {
            fail("unsupported substitution or concatenated Tcl word");
          }
          word.value += c;
        }
        if (!closed) {
          fail("unterminated quoted word");
        }
      }
      if (at < text.size() && !std::isspace(static_cast<unsigned char>(text[at]))) {
        fail("concatenated Tcl groups are unsupported");
      }
      result.push_back(std::move(word));
    }
    return result;
  }
};
double numeric(const Word& word) {
  if (word.command) {
    fail("numeric command substitution is unsupported");
  }
  char*        end   = nullptr;
  const double value = std::strtod(word.value.c_str(), &end);
  if (end == word.value.c_str() || *end || !std::isfinite(value)) {
    fail("invalid numeric literal: " + word.value);
  }
  return value;
}
std::string literal(const Word& word) {
  if (word.command) {
    fail("expected a literal");
  }
  return word.value;
}
std::vector<std::string> query(const Word& word, std::string_view kind) {
  if (!word.command) {
    fail("expected exact get_" + std::string(kind) + " selector");
  }
  const auto words = Words(word.value).parse();
  if (words.size() != 2 || words[0].command || words[0].value != "get_" + std::string(kind) || words[1].command) {
    fail("unsupported selector: " + word.value);
  }
  std::vector<std::string> result;
  for (const auto& item : Words(words[1].value, true).parse()) {
    auto name = literal(item);
    if (name.empty() || name.find('*') != name.npos || name.find('?') != name.npos) {
      fail("wildcard/empty selector");
    }
    if (kind == "pins") {
      const auto slash = name.rfind('/');
      if (slash == name.npos) {
        fail("cell pin selector requires an instance path");
      }
      name[slash] = ':';
    }
    result.push_back(std::move(name));
  }
  if (result.empty()) {
    fail("empty selector");
  }
  return result;
}
std::vector<std::string> physical(const Word& word) {
  const auto words = word.command ? Words(word.value).parse() : std::vector<Word>{};
  if (words.empty()) {
    fail("expected physical selector");
  }
  return query(word, words[0].value == "get_ports" ? "ports" : "pins");
}
std::string one(const std::vector<std::string>& names) {
  if (names.size() != 1) {
    fail("expected one clock/source");
  }
  return names.front();
}
std::vector<std::string> commands(std::string_view input) {
  std::string text;
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '\\' && i + 1 < input.size() && input[i + 1] == '\n') {
      text += ' ';
      ++i;
    } else {
      text += input[i];
    }
  }
  std::vector<std::string> result;
  size_t                   start  = 0;
  unsigned                 braces = 0, brackets = 0;
  bool                     quoted = false, beginning = true;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (beginning && c == '#') {
      while (i < text.size() && text[i] != '\n') {
        ++i;
      }
      start = i + 1;
      continue;
    }
    if (c == '\\') {
      ++i;
      beginning = false;
      continue;
    }
    if (!std::isspace(static_cast<unsigned char>(c))) {
      beginning = false;
    }
    if (!braces && c == '"') {
      quoted = !quoted;
    }
    if (!quoted && c == '{') {
      ++braces;
    }
    if (!quoted && c == '}') {
      if (!braces) {
        fail("unmatched brace");
      }
      --braces;
    }
    if (!braces && !quoted && c == '[') {
      ++brackets;
    }
    if (!braces && !quoted && c == ']') {
      if (!brackets) {
        fail("unmatched bracket");
      }
      --brackets;
    }
    if (!braces && !brackets && !quoted && (c == '\n' || c == ';')) {
      result.push_back(text.substr(start, i - start));
      start     = i + 1;
      beginning = true;
    }
  }
  if (braces || brackets || quoted) {
    fail("unterminated statement");
  }
  if (start < text.size()) {
    result.push_back(text.substr(start));
  }
  return result;
}
}  // namespace

Constraints read_saved_sdc(std::string_view text, std::string_view design, Sdc_units units) {
  if (!(units.time_ps > 0 && units.capacitance_ff > 0) || !std::isfinite(units.time_ps) || !std::isfinite(units.capacitance_ff)) {
    fail("units must be finite positive ps/fF scales");
  }
  Constraints                         result;
  std::map<std::string, Pair>         loads;
  std::map<std::string, Driving_cell> drivers;
  bool                                saw_design = false;
  for (const auto& statement : commands(text)) {
    const auto words = Words(statement).parse();
    if (words.empty()) {
      continue;
    }
    const auto command = literal(words.front());
    if (command == "current_design") {
      if (words.size() != 2 || literal(words[1]) != design || saw_design) {
        fail("current_design does not match the mapped design");
      }
      saw_design = true;
      continue;
    }
    if (command == "set_clock_groups") {
      bool                                  async = false;
      std::vector<std::vector<std::string>> groups;
      for (size_t i = 1; i < words.size(); ++i) {
        const auto option = literal(words[i]);
        if (option == "-asynchronous") {
          async = true;
          continue;
        }
        if (++i == words.size()) {
          fail("missing clock-group value");
        }
        if (option == "-group") {
          groups.push_back(query(words[i], "clocks"));
        } else if (option == "-name") {
          literal(words[i]);
        } else {
          fail("unsupported clock-group option: " + option);
        }
      }
      if (!async || groups.size() < 2) {
        fail("only explicit asynchronous clock groups are supported");
      }
      for (size_t a = 0; a < groups.size(); ++a) {
        for (size_t b = a + 1; b < groups.size(); ++b) {
          for (const auto& first : groups[a]) {
            for (const auto& second : groups[b]) {
              result.asynchronous.emplace(first, second);
            }
          }
        }
      }
      continue;
    }
    std::map<std::string, Word> options;
    std::set<std::string>       flags;
    std::vector<Word>           values;
    for (size_t i = 1; i < words.size(); ++i) {
      if (!words[i].command && !words[i].value.empty() && words[i].value.front() == '-' && words[i].value.size() > 1
          && !std::isdigit(static_cast<unsigned char>(words[i].value[1])) && words[i].value[1] != '.') {
        const auto option = words[i].value;
        if (option == "-add_delay" || option == "-pin_load") {
          flags.insert(option);
        } else {
          if (++i == words.size() || !options.emplace(option, words[i]).second) {
            fail("missing/duplicate option: " + option);
          }
        }
      } else {
        values.push_back(words[i]);
      }
    }
    const auto allow = [&](std::initializer_list<std::string> names, std::initializer_list<std::string> switches) {
      const std::set<std::string> allowed(names), allowed_flags(switches);
      for (const auto& [option, value] : options) {
        (void)value;
        if (!allowed.contains(option)) {
          fail(command + " unsupported option: " + option);
        }
      }
      for (const auto& flag : flags) {
        if (!allowed_flags.contains(flag)) {
          fail(command + " unsupported flag: " + flag);
        }
      }
    };
    const auto value = [&](const std::string& option) -> const Word& {
      const auto found = options.find(option);
      if (found == options.end()) {
        fail(command + " missing option: " + option);
      }
      return found->second;
    };
    if (command == "create_clock") {
      allow({"-name", "-period", "-waveform"}, {});
      if (values.size() != 1) {
        fail("clock requires a physical selector");
      }
      Ideal_clock clock;
      clock.period_ps  = numeric(value("-period")) * units.time_ps;
      const auto roots = physical(values[0]);
      clock.roots.insert(roots.begin(), roots.end());
      if (options.contains("-waveform")) {
        const auto edges = Words(literal(value("-waveform"))).parse();
        if (edges.size() != 2) {
          fail("clock waveform needs rise/fall edges");
        }
        clock.phase_ps       = numeric(edges[0]) * units.time_ps;
        const double falling = numeric(edges[1]) * units.time_ps;
        if (!(falling > clock.phase_ps && falling < clock.phase_ps + clock.period_ps)) {
          fail("invalid clock waveform");
        }
      }
      if (!result.clocks.emplace(literal(value("-name")), clock).second) {
        fail("duplicate clock");
      }
    } else if (command == "create_generated_clock") {
      allow({"-name", "-source", "-divide_by"}, {});
      if (values.size() != 1 || numeric(value("-divide_by")) != 1) {
        fail("only same-waveform divide-by-one generated clocks are supported");
      }
      const auto               source = one(physical(value("-source")));
      std::vector<std::string> owners;
      for (const auto& [name, clock] : result.clocks) {
        if (clock.roots.contains(source)) {
          owners.push_back(name);
        }
      }
      for (const auto& [name, alias] : result.clock_aliases) {
        if (alias.roots.contains(source)) {
          owners.push_back(name);
        }
      }
      if (owners.size() != 1) {
        fail("generated-clock source is not a unique declared clock root");
      }
      const auto roots = physical(values[0]);
      if (!result.clock_aliases
               .emplace(literal(value("-name")),
                        Clock_alias{
                            owners[0],
                            {roots.begin(), roots.end()}
      })
               .second) {
        fail("duplicate clock alias");
      }
    } else if (command == "set_input_delay" || command == "set_output_delay") {
      allow({"-clock"}, {"-add_delay"});
      if (values.size() != 2) {
        fail("IO delay needs a number and exact port selector");
      }
      const double       delay = numeric(values[0]) * units.time_ps;
      const auto         clock = one(query(value("-clock"), "clocks"));
      const Clock_system system({}, result.clocks, result.clock_aliases, result.asynchronous);
      const double       period = system.clocks[system.find(clock)].model.period_ps;
      for (const auto& port : query(values[1], "ports")) {
        if (command == "set_input_delay") {
          if (result.inputs.contains(port)) {
            fail("multiple input-delay references are unsupported: " + port);
          }
          auto& input      = result.inputs[port];
          input.arrival_ps = {delay, delay};
          input.clock      = clock;
        } else {
          if (result.outputs.contains(port)) {
            fail("multiple output-delay references are unsupported: " + port);
          }
          result.outputs[port] = {
              {period - delay, period - delay},
              {},
              clock
          };
        }
      }
    } else if (command == "set_load") {
      allow({}, {"-pin_load"});
      if (values.size() != 2 || !flags.contains("-pin_load")) {
        fail("only explicit pin loads are supported");
      }
      const double cap = numeric(values[0]) * units.capacitance_ff;
      if (cap < 0) {
        fail("negative pin load");
      }
      for (const auto& port : query(values[1], "ports")) {
        loads[port] = {cap, cap};
      }
    } else if (command == "set_driving_cell") {
      allow({"-lib_cell", "-pin", "-input_transition_rise", "-input_transition_fall"}, {});
      if (values.size() != 1) {
        fail("driving cell requires exact port selectors");
      }
      const Driving_cell driver{
          literal(value("-lib_cell")),
          literal(value("-pin")),
          "",
          {numeric(value("-input_transition_rise")) * units.time_ps, numeric(value("-input_transition_fall")) * units.time_ps}
      };
      for (const auto& port : query(values[0], "ports")) {
        drivers[port] = driver;
      }
    } else {
      fail("unsupported command: " + command);
    }
  }
  if (!saw_design) {
    fail("missing current_design");
  }
  for (const auto& [port, load] : loads) {
    if (!result.outputs.contains(port)) {
      fail("load without an output-delay boundary: " + port);
    }
    result.outputs[port].load_ff = load;
  }
  for (const auto& [port, driver] : drivers) {
    if (!result.inputs.contains(port)) {
      fail("driving cell without an input-delay boundary: " + port);
    }
    result.inputs[port].driving_cell = driver;
  }
  Clock_system({}, result.clocks, result.clock_aliases, result.asynchronous);
  result.complete = true;
  return result;
}
}  // namespace livehd::placement::late
