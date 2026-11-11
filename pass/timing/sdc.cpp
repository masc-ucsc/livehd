// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sdc.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace livehd::timing::fast {
namespace {

struct Unsupported : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// What a Tcl word or bracketed command evaluates to: a list of strings, or a symbolic port selection.
struct Value {
  std::vector<std::string>      list;
  std::optional<Port_selection> selection;
  [[nodiscard]] std::string     text() const { return list.empty() ? std::string() : list.front(); }
};

bool glob(std::string_view pattern, std::string_view text) {
  size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = t;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      t = ++mark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
}

std::string base_of(std::string_view name) {
  if (!name.empty() && name.back() == ']') {
    if (const auto open = name.rfind('['); open != std::string_view::npos) {
      return std::string(name.substr(0, open));
    }
  }
  return std::string(name);
}

bool number(std::string_view text, double& out) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  if (!text.empty() && text.front() == '+') {
    text.remove_prefix(1);
  }
  const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
  return !text.empty() && r.ec == std::errc() && r.ptr == text.data() + text.size();
}

// Arithmetic for [expr ...]: + - * / and parentheses over numbers.
class Arithmetic {
public:
  explicit Arithmetic(std::string_view text) : s_(text) {}
  double run() {
    const double v = sum();
    skip();
    if (i_ != s_.size()) {
      throw Unsupported("expr: unexpected '" + std::string(s_.substr(i_)) + "'");
    }
    return v;
  }

private:
  void skip() {
    while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) {
      ++i_;
    }
  }
  double sum() {
    double v = product();
    for (skip(); i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-'); skip()) {
      const char op = s_[i_++];
      const double r = product();
      v              = op == '+' ? v + r : v - r;
    }
    return v;
  }
  double product() {
    double v = atom();
    for (skip(); i_ < s_.size() && (s_[i_] == '*' || s_[i_] == '/'); skip()) {
      const char op = s_[i_++];
      const double r = atom();
      v              = op == '*' ? v * r : v / r;
    }
    return v;
  }
  double atom() {
    skip();
    if (i_ < s_.size() && s_[i_] == '(') {
      ++i_;
      const double v = sum();
      skip();
      if (i_ >= s_.size() || s_[i_++] != ')') {
        throw Unsupported("expr: missing ')'");
      }
      return v;
    }
    if (i_ < s_.size() && s_[i_] == '-') {
      ++i_;
      return -atom();
    }
    const size_t begin = i_;
    while (i_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[i_])) || s_[i_] == '.' || s_[i_] == '_')) {
      ++i_;
    }
    // exponents: 1e-3
    if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+') && i_ > begin && (s_[i_ - 1] == 'e' || s_[i_ - 1] == 'E')) {
      ++i_;
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) {
        ++i_;
      }
    }
    double v = 0;
    if (!number(s_.substr(begin, i_ - begin), v)) {
      throw Unsupported("expr: '" + std::string(s_.substr(begin, i_ - begin)) + "' is not a number");
    }
    return v;
  }
  std::string_view s_;
  size_t           i_ = 0;
};

class Reader {
public:
  Reader(Sdc& sdc, Sdc_units units) : sdc_(sdc), units_(units) {}

  void run(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
      const auto command = next_command(text, i);
      if (!command.empty()) {
        execute(command);
      }
    }
  }

private:
  // Reads one command: up to a newline or ';' at depth zero, joining backslash-newline and open groups.
  static std::string next_command(std::string_view text, size_t& i) {
    std::string out;
    int         brace = 0, bracket = 0;
    bool        quote = false;
    while (i < text.size()) {
      const char c = text[i];
      if (c == '\\' && i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '\r')) {
        i += 2;
        if (i < text.size() && text[i - 1] == '\r' && text[i] == '\n') {
          ++i;
        }
        out += ' ';
        continue;
      }
      if (brace == 0 && bracket == 0 && !quote && (c == '\n' || c == ';')) {
        ++i;
        break;
      }
      if (c == '#' && brace == 0 && bracket == 0 && !quote && out.find_first_not_of(" \t\r") == std::string::npos) {
        while (i < text.size() && text[i] != '\n') {
          ++i;
        }
        continue;
      }
      if (c == '{' && !quote) {
        ++brace;
      } else if (c == '}' && !quote && brace > 0) {
        --brace;
      } else if (c == '[' && brace == 0 && !quote) {
        ++bracket;
      } else if (c == ']' && brace == 0 && !quote && bracket > 0) {
        --bracket;
      } else if (c == '"' && brace == 0) {
        quote = !quote;
      }
      out += c;
      ++i;
    }
    const auto first = out.find_first_not_of(" \t\r");
    return first == std::string::npos ? std::string() : out.substr(first);
  }

  // Splits a command into raw words (still with their braces, quotes and brackets).
  static std::vector<std::string> words_of(std::string_view s) {
    std::vector<std::string> words;
    size_t                   i = 0;
    while (i < s.size()) {
      while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
      }
      if (i >= s.size()) {
        break;
      }
      const size_t begin = i;
      int          brace = 0, bracket = 0;
      bool         quote = false;
      for (; i < s.size(); ++i) {
        const char c = s[i];
        if (!brace && !bracket && !quote && std::isspace(static_cast<unsigned char>(c))) {
          break;
        }
        if (c == '{' && !quote) {
          ++brace;
        } else if (c == '}' && !quote && brace > 0) {
          --brace;
        } else if (c == '[' && !brace && !quote) {
          ++bracket;
        } else if (c == ']' && !brace && !quote && bracket > 0) {
          --bracket;
        } else if (c == '"' && !brace) {
          quote = !quote;
        }
      }
      words.emplace_back(s.substr(begin, i - begin));
    }
    return words;
  }

  std::string substitute_variables(std::string_view s) const {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
      if (s[i] != '$') {
        out += s[i];
        continue;
      }
      size_t      j = i + 1;
      std::string name;
      if (j < s.size() && s[j] == '{') {
        const auto close = s.find('}', j);
        if (close == std::string_view::npos) {
          throw Unsupported("unterminated ${");
        }
        name = std::string(s.substr(j + 1, close - j - 1));
        j    = close + 1;
      } else {
        for (; j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_' || s[j] == ':'); ++j) {
          name += s[j];
        }
      }
      const auto it = variables_.find(name);
      if (it == variables_.end()) {
        throw Unsupported("variable '" + name + "' is not set");
      }
      if (it->second.selection) {
        throw Unsupported("a port selection inside a string");
      }
      out += it->second.text();
      i = j - 1;
    }
    return out;
  }

  Value evaluate(const std::string& word) {
    if (word.size() >= 2 && word.front() == '{' && word.back() == '}') {
      Value v;
      v.list = split_list(word.substr(1, word.size() - 2));
      return v;
    }
    if (word.size() >= 2 && word.front() == '[' && word.back() == ']') {
      return bracket(word.substr(1, word.size() - 2));
    }
    if (word.front() == '$' && word.find_first_of("[ \t\"") == std::string::npos) {
      const auto name = word.size() > 1 && word[1] == '{' ? word.substr(2, word.size() - 3) : word.substr(1);
      const auto it   = variables_.find(name);
      if (it == variables_.end()) {
        throw Unsupported("variable '" + name + "' is not set");
      }
      return it->second;
    }
    std::string text = word;
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"') {
      text = text.substr(1, text.size() - 2);
    }
    Value v;
    v.list.push_back(substitute_variables(text));
    return v;
  }

  static std::vector<std::string> split_list(std::string_view s) {
    std::vector<std::string> out;
    for (auto& w : words_of(s)) {
      if (w.size() >= 2 && w.front() == '{' && w.back() == '}') {
        w = w.substr(1, w.size() - 2);
      }
      out.push_back(std::move(w));
    }
    return out;
  }

  Value bracket(const std::string& command) {
    const auto raw = words_of(command);
    if (raw.empty()) {
      return {};
    }
    const auto& name = raw[0];
    Value       out;
    if (name == "get_ports" || name == "get_clocks") {
      Port_selection selection;
      for (size_t i = 1; i < raw.size(); ++i) {
        if (raw[i] == "-quiet") {
          continue;
        }
        if (raw[i].front() == '-') {
          throw Unsupported(name + " " + raw[i]);
        }
        const auto v = evaluate(raw[i]);
        if (v.selection) {
          throw Unsupported(name + " of a selection");
        }
        for (const auto& item : v.list) {
          for (const auto& p : split_list(item)) {
            selection.patterns.push_back(p);
          }
        }
      }
      if (name == "get_clocks") {
        out.list = selection.patterns;
      } else {
        out.selection = std::move(selection);
      }
      return out;
    }
    if (name == "all_inputs" || name == "all_outputs") {
      Port_selection selection;
      selection.kind = name == "all_inputs" ? Port_selection::Kind::all_inputs : Port_selection::Kind::all_outputs;
      for (size_t i = 1; i < raw.size(); ++i) {
        if (raw[i] == "-no_clocks") {
          selection.no_clocks = true;
        } else {
          throw Unsupported(name + " " + raw[i]);
        }
      }
      out.selection = std::move(selection);
      return out;
    }
    if (name == "expr") {
      std::string text;
      for (size_t i = 1; i < raw.size(); ++i) {
        auto w = raw[i];
        if (w.size() >= 2 && w.front() == '{' && w.back() == '}') {
          w = w.substr(1, w.size() - 2);
        }
        text += (i > 1 ? " " : "") + substitute_variables(w);
      }
      const double v = Arithmetic(text).run();
      char         buffer[64];
      const auto   r = std::to_chars(buffer, buffer + sizeof buffer, v);
      out.list.emplace_back(buffer, r.ptr);
      return out;
    }
    if (name == "lsearch") {
      // lsearch -inline -all -not -exact LIST ITEM: LIST without ITEM.
      bool inline_ = false, all = false, not_ = false, exact = false;
      std::vector<Value> args;
      for (size_t i = 1; i < raw.size(); ++i) {
        if (raw[i] == "-inline") {
          inline_ = true;
        } else if (raw[i] == "-all") {
          all = true;
        } else if (raw[i] == "-not") {
          not_ = true;
        } else if (raw[i] == "-exact") {
          exact = true;
        } else if (raw[i].front() == '-') {
          throw Unsupported("lsearch " + raw[i]);
        } else {
          args.push_back(evaluate(raw[i]));
        }
      }
      if (!(inline_ && all && not_ && exact) || args.size() != 2) {
        throw Unsupported("lsearch form");
      }
      Value result = args[0];
      for (const auto& item : args[1].selection ? args[1].selection->patterns : args[1].list) {
        if (result.selection) {
          result.selection->excluded.push_back(item);
        } else {
          std::erase(result.list, item);
        }
      }
      return result;
    }
    throw Unsupported("command '" + name + "' in brackets");
  }

  bool time(const Value& v, double& out) const {
    if (v.selection || !number(v.text(), out)) {
      return false;
    }
    out *= units_.time_ps;
    return true;
  }

  void report(const std::string& what) {
    if (reported_.insert(what).second) {
      sdc_.unsupported.push_back(what);
    }
  }

  void execute(const std::string& command) {
    const auto raw = words_of(command);
    if (raw.empty()) {
      return;
    }
    const std::string& name = raw[0];
    static const std::set<std::string> ignored{"current_design", "set_propagated_clock", "set_units",  "set_ideal_network",
                                               "set_ideal_latency", "set_ideal_transition", "set_wire_load_mode"};
    static const std::set<std::string> control{"foreach", "for", "while", "if", "switch", "proc", "catch", "unset", "error"};
    try {
      if (name == "set") {
        if (raw.size() != 3) {
          throw Unsupported("set form");
        }
        if (raw[1].starts_with("::") || raw[1].starts_with("sta_")) {
          return;  // tool variable
        }
        variables_[raw[1]] = evaluate(raw[2]);
      } else if (ignored.contains(name)) {
        return;
      } else if (control.contains(name)) {
        report(name + " (a Tcl control structure, skipped)");
      } else if (name == "create_clock") {
        create_clock(raw);
      } else if (name == "set_clock_uncertainty" || name == "set_clock_transition") {
        clock_value(raw);
      } else if (name == "set_input_delay" || name == "set_output_delay") {
        delay(raw);
      } else if (name == "set_input_transition") {
        double v = 0;
        if (raw.size() != 3 || !time(evaluate(raw[1]), v)) {
          throw Unsupported("set_input_transition form");
        }
        sdc_.input_transition_ps = static_cast<float>(v);
      } else if (name == "set_load") {
        load(raw);
      } else if (name == "set_false_path") {
        false_path(raw);
      } else {
        report(name);
      }
    } catch (const Unsupported& e) {
      report(name + " (" + e.what() + ")");
    }
  }

  void create_clock(const std::vector<std::string>& raw) {
    Sdc::Clock clock;
    bool       have_period = false;
    for (size_t i = 1; i < raw.size(); ++i) {
      if (raw[i] == "-name" && i + 1 < raw.size()) {
        clock.name = evaluate(raw[++i]).text();
      } else if (raw[i] == "-period" && i + 1 < raw.size()) {
        double v = 0;
        if (!time(evaluate(raw[++i]), v) || v <= 0) {
          throw Unsupported("create_clock -period");
        }
        clock.period_ps = static_cast<float>(v);
        have_period     = true;
      } else if (raw[i].front() == '-') {
        throw Unsupported("create_clock " + raw[i]);
      } else {
        const auto v = evaluate(raw[i]);
        if (!v.selection || v.selection->kind != Port_selection::Kind::names || v.selection->patterns.size() != 1) {
          throw Unsupported("create_clock source must be one port");
        }
        clock.port = v.selection->patterns.front();
      }
    }
    if (!have_period || clock.port.empty()) {
      throw Unsupported("create_clock needs -period and a port (virtual clocks are not modelled)");
    }
    if (clock.name.empty()) {
      clock.name = clock.port;
    }
    sdc_.clocks.push_back(std::move(clock));
  }

  void clock_value(const std::vector<std::string>& raw) {
    double                   value = 0;
    bool                     have  = false;
    std::vector<std::string> clocks;
    for (size_t i = 1; i < raw.size(); ++i) {
      if (raw[i].front() == '-' && !have) {
        if (raw[i] == "-rise" || raw[i] == "-fall" || raw[i] == "-min" || raw[i] == "-max" || raw[i] == "-setup") {
          continue;
        }
        throw Unsupported(raw[0] + " " + raw[i]);
      }
      const auto v = evaluate(raw[i]);
      if (!have && !v.selection && time(v, value)) {
        have = true;
      } else if (!v.selection) {
        clocks.insert(clocks.end(), v.list.begin(), v.list.end());
      } else {
        throw Unsupported(raw[0] + " target");
      }
    }
    if (!have) {
      throw Unsupported(raw[0] + " without a value");
    }
    for (auto& clock : sdc_.clocks) {
      if (clocks.empty() || std::ranges::find(clocks, clock.name) != clocks.end()) {
        (raw[0] == "set_clock_uncertainty" ? clock.uncertainty_ps : clock.slew_ps) = static_cast<float>(value);
      }
    }
  }

  void delay(const std::vector<std::string>& raw) {
    Sdc::Delay delay;
    delay.input = raw[0] == "set_input_delay";
    bool have = false, min_only = false, max_flag = false, min_flag = false;
    for (size_t i = 1; i < raw.size(); ++i) {
      if (raw[i] == "-clock" && i + 1 < raw.size()) {
        const auto v = evaluate(raw[++i]);
        if (v.selection) {
          throw Unsupported(raw[0] + " -clock selection");
        }
        delay.clock = v.text();
      } else if (raw[i] == "-max") {
        max_flag = true;
      } else if (raw[i] == "-min") {
        min_flag = true;
      } else if (raw[i] == "-add_delay" || raw[i] == "-network_latency_included" || raw[i] == "-source_latency_included") {
        continue;
      } else if (raw[i].front() == '-' && raw[i].size() > 1 && std::isalpha(static_cast<unsigned char>(raw[i][1]))) {
        throw Unsupported(raw[0] + " " + raw[i]);
      } else {
        const auto v = evaluate(raw[i]);
        double     d = 0;
        if (!have && !v.selection && time(v, d)) {
          delay.delay_ps = static_cast<float>(d);
          have           = true;
        } else if (v.selection) {
          delay.ports = *v.selection;
        } else {
          delay.ports.patterns.insert(delay.ports.patterns.end(), v.list.begin(), v.list.end());
        }
      }
    }
    min_only = min_flag && !max_flag;
    if (!have) {
      throw Unsupported(raw[0] + " without a value");
    }
    if (!min_only) {
      sdc_.delays.push_back(std::move(delay));
    }
  }

  void load(const std::vector<std::string>& raw) {
    Sdc::Load load;
    bool      have = false;
    for (size_t i = 1; i < raw.size(); ++i) {
      if (raw[i].front() == '-' && raw[i].size() > 1 && std::isalpha(static_cast<unsigned char>(raw[i][1]))) {
        if (raw[i] == "-pin_load" || raw[i] == "-max" || raw[i] == "-min") {
          continue;
        }
        throw Unsupported("set_load " + raw[i]);
      }
      const auto v = evaluate(raw[i]);
      double     d = 0;
      if (!have && !v.selection && number(v.text(), d)) {
        load.load_ff = static_cast<float>(d * units_.capacitance_ff);
        have         = true;
      } else if (v.selection) {
        load.ports = *v.selection;
      } else {
        throw Unsupported("set_load target");
      }
    }
    if (!have) {
      throw Unsupported("set_load without a value");
    }
    sdc_.loads.push_back(std::move(load));
  }

  void false_path(const std::vector<std::string>& raw) {
    Sdc::False_path path;
    bool            has_from = false, has_to = false;
    for (size_t i = 1; i < raw.size(); ++i) {
      if ((raw[i] == "-from" || raw[i] == "-to") && i + 1 < raw.size()) {
        const bool from = raw[i] == "-from";
        const auto v    = evaluate(raw[++i]);
        if (!v.selection) {
          throw Unsupported("set_false_path " + raw[i - 1] + " target is not a port selection");
        }
        (from ? path.from : path.to) = *v.selection;
        (from ? has_from : has_to)   = true;
      } else {
        throw Unsupported("set_false_path " + raw[i]);
      }
    }
    if (has_from == has_to) {
      throw Unsupported(has_from ? "set_false_path with -from and -to" : "set_false_path without -from or -to");
    }
    sdc_.false_paths.push_back(std::move(path));
  }

  Sdc&                         sdc_;
  Sdc_units                    units_;
  std::map<std::string, Value> variables_;
  std::set<std::string>        reported_;
};

}  // namespace

Sdc read_sdc(std::string_view text, Sdc_units units) {
  Sdc sdc;
  Reader(sdc, units).run(text);
  return sdc;
}

Constraints resolve(const Sdc& sdc, const Netlist& netlist) {
  Constraints out;
  out.unsupported = sdc.unsupported;
  std::set<std::string> clock_ports;
  for (const auto& clock : sdc.clocks) {
    out.clocks.push_back({clock.name, clock.port, clock.period_ps, clock.uncertainty_ps.value_or(0.f), clock.slew_ps.value_or(0.f)});
    clock_ports.insert(clock.port);
  }
  if (sdc.input_transition_ps) {
    out.default_input_slew_ps = *sdc.input_transition_ps;
  }
  const auto is_clock_port = [&](const Port& port) {
    return clock_ports.contains(port.name) || clock_ports.contains(base_of(port.name));
  };
  std::set<std::string> missed;
  // The ports of one direction a selection names.
  const auto select = [&](const Port_selection& selection, bool input) {
    std::vector<const Port*> found;
    for (const auto& port : netlist.ports()) {
      if (port.input != input) {
        continue;
      }
      const auto base = base_of(port.name);
      bool       hit  = false;
      if (selection.kind == Port_selection::Kind::names) {
        hit = std::ranges::any_of(selection.patterns, [&](const auto& p) { return glob(p, port.name) || glob(p, base); });
      } else if ((selection.kind == Port_selection::Kind::all_inputs) == input) {
        hit = !(selection.no_clocks && is_clock_port(port))
              && std::ranges::none_of(selection.excluded, [&](const auto& e) { return e == port.name || e == base; });
      }
      if (hit) {
        found.push_back(&port);
      }
    }
    if (selection.kind == Port_selection::Kind::names) {
      for (const auto& p : selection.patterns) {
        const bool any = std::ranges::any_of(netlist.ports(), [&](const Port& port) {
          return port.input == input && (glob(p, port.name) || glob(p, base_of(port.name)));
        });
        const bool other = std::ranges::any_of(netlist.ports(), [&](const Port& port) {
          return port.input != input && (glob(p, port.name) || glob(p, base_of(port.name)));
        });
        if (!any && !other) {
          missed.insert(p);
        }
      }
    }
    return found;
  };
  std::map<std::string, float> loads;
  for (const auto& l : sdc.loads) {
    for (const auto* port : select(l.ports, false)) {
      loads[port->name] = l.load_ff;
    }
  }
  const auto first_clock = out.clocks.empty() ? std::string() : out.clocks.front().name;
  for (const auto& d : sdc.delays) {
    for (const auto* port : select(d.ports, d.input)) {
      if (d.input) {
        if (!is_clock_port(*port)) {
          out.inputs.push_back({port->name, d.clock.empty() ? first_clock : d.clock, d.delay_ps, out.default_input_slew_ps});
        }
      } else {
        const auto it = loads.find(port->name);
        out.outputs.push_back({port->name, d.clock.empty() ? first_clock : d.clock, d.delay_ps, it == loads.end() ? 0.f : it->second});
      }
    }
  }
  for (const auto& path : sdc.false_paths) {
    for (const auto* port : select(path.from, true)) {
      if (!is_clock_port(*port)) {
        out.cut_pins.push_back(netlist.pin(port->cell, 0));
      }
    }
    for (const auto* port : select(path.to, false)) {
      out.excluded_endpoints.push_back(netlist.pin(port->cell, 0));
    }
  }
  for (const auto& p : missed) {
    out.unsupported.push_back("port pattern '" + p + "' names no port of the netlist");
  }
  return out;
}

}  // namespace livehd::timing::fast
