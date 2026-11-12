// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sdc.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <regex>
#include <set>
#include <unordered_map>

#include "sdc_tcl.hpp"

namespace livehd::timing::fast {
namespace {

using Kind = Sdc_object::Kind;
using tcl::Error;
using Words = tcl::Interp::Words;

// An object in a Tcl value: a mark, a letter for the kind, a colon and the name. `[current_design]` is 'D'.
constexpr char mark = '\x1f';

char letter_of(Kind kind) {
  switch (kind) {
    case Kind::port : return 'P';
    case Kind::pin  : return 'I';
    case Kind::net  : return 'N';
    case Kind::clock: return 'C';
    case Kind::cell : return 'L';
  }
  return '?';
}
const char* word_of(Kind kind) {
  switch (kind) {
    case Kind::port : return "port";
    case Kind::pin  : return "pin";
    case Kind::net  : return "net";
    case Kind::clock: return "clock";
    case Kind::cell : return "cell";
  }
  return "object";
}
std::string handle(Kind kind, std::string_view name) {
  std::string out{mark, letter_of(kind), ':'};
  out += name;
  return out;
}
bool is_handle(std::string_view text) { return text.size() >= 3 && text[0] == mark && text[2] == ':'; }
bool is_design(std::string_view text) { return is_handle(text) && text[1] == 'D'; }
std::optional<Sdc_object> object_of(std::string_view text) {
  if (!is_handle(text)) {
    return std::nullopt;
  }
  for (const auto kind : {Kind::port, Kind::pin, Kind::net, Kind::clock, Kind::cell}) {
    if (text[1] == letter_of(kind)) {
      return Sdc_object{kind, std::string(text.substr(3))};
    }
  }
  return std::nullopt;
}
std::string plain_of(std::string_view text) { return std::string(is_handle(text) ? text.substr(3) : text); }

// A name pattern of an object query: `*` and `?` only (brackets are bus bits, not character classes); a
// backslash takes the next character as it is.
bool wild(std::string_view pattern) {
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '\\') {
      ++i;
    } else if (pattern[i] == '*' || pattern[i] == '?') {
      return true;
    }
  }
  return false;
}
std::string unescaped(std::string_view pattern) {
  std::string out;
  for (size_t i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == '\\' && i + 1 < pattern.size()) {
      ++i;
    }
    out += pattern[i];
  }
  return out;
}
bool name_match(std::string_view pattern, std::string_view text, bool nocase = false) {
  const auto eq = [&](char a, char b) {
    return nocase ? std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)) : a == b;
  };
  size_t p = 0, t = 0, star = std::string_view::npos, at = 0;
  while (t < text.size()) {
    if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      at   = t;
    } else if (p + 1 < pattern.size() && pattern[p] == '\\' && eq(pattern[p + 1], text[t])) {
      p += 2;
      ++t;
    } else if (p < pattern.size() && pattern[p] != '\\' && (pattern[p] == '?' || eq(pattern[p], text[t]))) {
      ++p;
      ++t;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      t = ++at;
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

// A regular expression of an object query, matched against the whole name. When it is a plain name once its
// escapes are removed (the usual case: a generated, escaped instance path), `literal` is that name.
struct Name_regex {
  std::optional<std::string> literal;
  std::string                prefix;  // the leading literal part, to skip most names without running the expression
  std::regex                 re;
  bool                       nocase = false;
  explicit Name_regex(std::string pattern, bool ignore_case) : nocase(ignore_case) {
    if (pattern.starts_with("^")) {
      pattern.erase(0, 1);
    }
    if (pattern.ends_with("$") && !pattern.ends_with("\\$")) {
      pattern.pop_back();
    }
    std::string text;
    bool        plain = true;
    for (size_t i = 0; i < pattern.size() && plain; ++i) {
      const char c = pattern[i];
      if (c == '\\' && i + 1 < pattern.size() && !std::isalnum(static_cast<unsigned char>(pattern[i + 1]))) {
        text += pattern[++i];
      } else if (std::string_view(".*+?()[]{}|^$\\").find(c) != std::string_view::npos) {
        plain = false;
      } else {
        text += c;
      }
    }
    if (plain && !nocase) {
      literal = std::move(text);
      return;
    }
    prefix = nocase ? std::string() : text;
    try {
      re = std::regex(pattern, nocase ? std::regex::ECMAScript | std::regex::icase : std::regex::ECMAScript);
    } catch (const std::regex_error&) {
      throw Error("regular expression \"" + pattern + "\" is not supported");
    }
  }
  [[nodiscard]] bool matches(const std::string& name) const {
    if (literal) {
      return name == *literal;
    }
    return name.starts_with(prefix) && std::regex_match(name, re);
  }
};

// Options of an SDC command: flags, options with a value, and what is left. A unique prefix of an option
// name is accepted, as the tools do; a negative number is not an option.
struct Options {
  std::set<std::string>                            flags;
  std::vector<std::pair<std::string, std::string>> values;  // in order, an option may repeat
  std::vector<std::string>                         rest;
  [[nodiscard]] bool                               flag(const std::string& name) const { return flags.contains(name); }
  [[nodiscard]] const std::string*                 value(const std::string& name) const {
    const std::string* found = nullptr;
    for (const auto& [key, v] : values) {
      if (key == name) {
        found = &v;
      }
    }
    return found;
  }
};

Options parse(const Words& w, std::initializer_list<const char*> flags, std::initializer_list<const char*> keys) {
  Options out;
  for (size_t i = 1; i < w.size(); ++i) {
    const auto& word = w[i];
    if (word.size() < 2 || word[0] != '-' || std::isdigit(static_cast<unsigned char>(word[1])) || word[1] == '.') {
      out.rest.push_back(word);
      continue;
    }
    const char* found   = nullptr;
    bool        is_flag = false, ambiguous = false;
    for (const auto* set : {&flags, &keys}) {
      for (const char* name : *set) {
        if (word == name) {
          found     = name;
          is_flag   = set == &flags;
          ambiguous = false;
          goto done;
        }
        if (std::string_view(name).starts_with(word)) {
          ambiguous = found != nullptr;
          found     = name;
          is_flag   = set == &flags;
        }
      }
    }
  done:
    if (found == nullptr || ambiguous) {
      throw Error(w[0] + " " + word + (ambiguous ? " is ambiguous" : " is not supported"));
    }
    if (is_flag) {
      out.flags.insert(found);
    } else if (i + 1 >= w.size()) {
      throw Error(w[0] + " " + word + " needs a value");
    } else {
      out.values.emplace_back(found, w[++i]);
    }
  }
  return out;
}

// The design of a constraint file read alone: every name exists.
class Unknown_design final : public Sdc_design {
public:
  void ports(const std::function<void(std::string_view, bool)>&) const override {}
  void pins(const std::function<void(std::string_view, std::string_view)>&) const override {}
  bool has_pin(std::string_view, std::string_view) const override { return true; }
  void nets(const std::function<void(std::string_view)>&) const override {}
  bool has_net(std::string_view) const override { return true; }
  void cells(const std::function<void(std::string_view)>&) const override {}
  bool has_cell(std::string_view) const override { return true; }
  [[nodiscard]] bool known() const override { return false; }
};

class Reader {
public:
  Reader(Sdc& sdc, Sdc_units units, const Sdc_design& design)
      : sdc_(sdc)
      , units_(units)
      , design_(design)
      , interp_([this](Words& w, std::string& result) { return command(w, result); }, [](std::string_view text) { return plain_of(text); }) {
    design_.ports([&](std::string_view name, bool input) {
      port_index_.emplace(std::string(name), ports_.size());
      bus_[base_of(name)].push_back(ports_.size());
      ports_.push_back({std::string(name), input});
    });
  }

  void run(std::string_view text) {
    interp_.run(text, [&](const std::string& name, const std::string& message) {
      report(name.empty() ? message : name + " (" + message + ")");
      sdc_.commands.push_back({interp_.line(), {}, name, message});
    });
  }

private:
  struct Port {
    std::string name;
    bool        input = true;
  };

  void report(const std::string& what) {
    // An object handle prints as its name.
    std::string text;
    for (size_t i = 0; i < what.size(); ++i) {
      if (what[i] == mark && i + 2 < what.size() && what[i + 2] == ':') {
        i += 2;
        continue;
      }
      text += what[i] == '\n' ? ' ' : what[i];
    }
    if (reported_.insert(text).second) {
      sdc_.unsupported.push_back(std::move(text));
    }
  }

  double time(const std::string& text, const std::string& what) const {
    double v = 0;
    if (!tcl::to_number(text, v)) {
      throw Error(what + ": \"" + plain_of(text) + "\" is not a number");
    }
    return v * units_.time_ps;
  }

  // ---- object queries ----

  std::vector<std::string> match_ports(const std::string& pattern, bool regexp, bool nocase, int direction = 0) const {
    std::vector<std::string> out;
    const auto               take = [&](size_t i) {
      if (direction == 0 || (direction > 0) == ports_[i].input) {
        out.push_back(ports_[i].name);
      }
    };
    if (!design_.known()) {
      out.push_back(pattern);
      return out;
    }
    if (regexp) {
      const Name_regex re(pattern, nocase);
      for (size_t i = 0; i < ports_.size(); ++i) {
        if (re.matches(ports_[i].name) || re.matches(base_of(ports_[i].name))) {
          take(i);
        }
      }
      return out;
    }
    if (!wild(pattern) && !nocase) {
      const auto name = unescaped(pattern);
      if (const auto it = port_index_.find(name); it != port_index_.end()) {
        take(it->second);
      } else if (const auto bits = bus_.find(name); bits != bus_.end()) {
        for (const auto i : bits->second) {
          take(i);
        }
      }
      return out;
    }
    for (size_t i = 0; i < ports_.size(); ++i) {
      if (name_match(pattern, ports_[i].name, nocase) || name_match(pattern, base_of(ports_[i].name), nocase)) {
        take(i);
      }
    }
    return out;
  }

  std::vector<std::string> match_pins(const std::string& pattern, bool regexp, bool nocase) const {
    std::vector<std::string> out;
    if (!design_.known()) {
      out.push_back(pattern);
      return out;
    }
    const auto exact = [&](const std::string& name) {
      const auto slash = name.rfind('/');
      if (slash == std::string::npos ? design_.has_pin({}, name)
                                     : design_.has_pin(std::string_view(name).substr(0, slash), std::string_view(name).substr(slash + 1))) {
        out.push_back(name);
      }
    };
    std::optional<Name_regex> re;
    if (regexp) {
      re.emplace(pattern, nocase);
      if (re->literal) {
        exact(*re->literal);
        return out;
      }
    } else if (!wild(pattern) && !nocase) {
      exact(unescaped(pattern));
      return out;
    }
    std::string name;
    design_.pins([&](std::string_view instance, std::string_view pin) {
      name.assign(instance);
      name += '/';
      name += pin;
      if (re ? re->matches(name) : name_match(pattern, name, nocase)) {
        out.push_back(name);
      }
    });
    return out;
  }

  template <class Has, class Each>
  std::vector<std::string> match_names(const std::string& pattern, bool regexp, bool nocase, const Has& has, const Each& each) const {
    std::vector<std::string> out;
    if (!design_.known()) {
      out.push_back(pattern);
      return out;
    }
    std::optional<Name_regex> re;
    if (regexp) {
      re.emplace(pattern, nocase);
    }
    const auto plain = unescaped(pattern);
    if (const auto* literal = re ? (re->literal ? &*re->literal : nullptr) : (!wild(pattern) && !nocase ? &plain : nullptr)) {
      if (has(*literal)) {
        out.push_back(*literal);
      }
      return out;
    }
    std::string name;
    each([&](std::string_view candidate) {
      name.assign(candidate);
      if (re ? re->matches(name) : name_match(pattern, name, nocase)) {
        out.push_back(name);
      }
    });
    return out;
  }

  std::vector<std::string> match_clocks(const std::string& pattern, bool regexp, bool nocase) const {
    std::vector<std::string> out;
    std::optional<Name_regex> re;
    if (regexp) {
      re.emplace(pattern, nocase);
    }
    for (const auto& clock : sdc_.clocks) {
      if (re ? re->matches(clock.name) : name_match(pattern, clock.name, nocase)) {
        out.push_back(clock.name);
      }
    }
    return out;
  }

  std::vector<std::string> match(Kind kind, const std::string& pattern, bool regexp = false, bool nocase = false) const {
    switch (kind) {
      case Kind::port : return match_ports(pattern, regexp, nocase);
      case Kind::pin  : return match_pins(pattern, regexp, nocase);
      case Kind::clock: return match_clocks(pattern, regexp, nocase);
      case Kind::net  :
        return match_names(
            pattern, regexp, nocase, [&](const std::string& n) { return design_.has_net(n); },
            [&](const auto& visit) { design_.nets(visit); });
      case Kind::cell :
        return match_names(
            pattern, regexp, nocase, [&](const std::string& n) { return design_.has_cell(n); },
            [&](const auto& visit) { design_.cells(visit); });
    }
    return {};
  }

  // get_ports, get_pins, get_nets, get_clocks, get_cells.
  std::string query(Words& w, Kind kind) {
    const auto o = parse(w, {"-quiet", "-regexp", "-nocase", "-hierarchical", "-exact"}, {"-filter", "-of_objects", "-hsc"});
    if (o.value("-filter") != nullptr || o.value("-of_objects") != nullptr) {
      throw Error(w[0] + (o.value("-filter") != nullptr ? " -filter" : " -of_objects") + " is not supported");
    }
    if (const auto* separator = o.value("-hsc"); separator != nullptr && *separator != "/") {
      throw Error(w[0] + " -hsc " + *separator + " is not supported");
    }
    std::vector<std::string> out;
    std::set<std::string>    seen;
    for (const auto& argument : o.rest) {
      // The patterns are a list, and a backslash in a pattern (an escaped bracket, a regular expression) is
      // kept through the split, as the tools keep it.
      std::string kept;
      for (const char c : argument) {
        kept += c;
        if (c == '\\') {
          kept += c;
        }
      }
      for (const auto& pattern : tcl::list_split(kept)) {
        if (const auto object = object_of(pattern)) {
          if (object->kind != kind) {
            throw Error(w[0] + ": " + object->name + " is a " + word_of(object->kind));
          }
          if (seen.insert(object->name).second) {
            out.push_back(handle(kind, object->name));
          }
          continue;
        }
        auto names = match(kind, pattern, o.flag("-regexp"), o.flag("-nocase"));
        if (names.empty() && !o.flag("-quiet")) {
          if (design_.keep_unmatched() && !o.flag("-regexp") && !wild(pattern) && (kind == Kind::port || kind == Kind::pin || kind == Kind::clock)) {
            names.push_back(unescaped(pattern));
          } else {
            report(std::string(word_of(kind)) + " pattern '" + pattern + "' names no " + word_of(kind) + " of the design");
          }
        }
        for (const auto& name : names) {
          if (seen.insert(name).second) {
            out.push_back(handle(kind, name));
          }
        }
      }
    }
    return tcl::list_join(out);
  }

  [[nodiscard]] std::string all_text(bool inputs, bool no_clocks) const {
    if (!design_.known()) {
      return handle(Kind::port, inputs ? "<all_inputs>" : "<all_outputs>") + " ";
    }
    std::set<std::string> clocks;
    if (no_clocks) {
      for (const auto& clock : sdc_.clocks) {
        for (const auto& source : clock.sources) {
          if (source.kind == Kind::port) {
            clocks.insert(source.name);
          }
        }
      }
    }
    std::vector<std::string> out;
    for (const auto& port : ports_) {
      if (port.input == inputs && !clocks.contains(port.name)) {
        out.push_back(handle(Kind::port, port.name));
      }
    }
    return out.empty() ? std::string() : tcl::list_join(out) + " ";
  }

  std::string all_ports(Words& w, bool inputs) {
    const auto o = parse(w, {"-no_clocks"}, {});
    if (!o.rest.empty()) {
      throw Error(w[0] + ": unexpected \"" + plain_of(o.rest.front()) + "\"");
    }
    return all_text(inputs, o.flag("-no_clocks"));
  }

  // The objects an argument names. A plain name is looked up as each allowed kind, in order.
  std::vector<Sdc_object> objects(const std::string& argument, std::initializer_list<Kind> allowed, const std::string& what) {
    std::vector<Sdc_object> out;
    for (const auto& element : tcl::list_split(argument)) {
      if (is_design(element)) {
        throw Error(what + ": the design is not a valid object here");
      }
      if (auto object = object_of(element)) {
        if (std::find(allowed.begin(), allowed.end(), object->kind) == allowed.end()) {
          throw Error(what + ": " + object->name + " is a " + word_of(object->kind));
        }
        out.push_back(std::move(*object));
        continue;
      }
      bool found = false;
      for (const auto kind : allowed) {
        if (kind == Kind::pin && element.find('/') == std::string::npos) {
          continue;
        }
        const auto names = match(kind, element);
        for (const auto& name : names) {
          out.push_back({kind, name});
        }
        if (!names.empty()) {
          found = true;
          break;
        }
      }
      if (!found) {
        report(what + ": '" + element + "' names nothing in the design");
      }
    }
    return out;
  }

  Sdc::Clock* clock(const std::string& name) {
    for (auto& c : sdc_.clocks) {
      if (c.name == name) {
        return &c;
      }
    }
    return nullptr;
  }
  std::vector<std::string> clocks_of(const std::string& argument, const std::string& what) {
    std::vector<std::string> out;
    for (const auto& object : objects(argument, {Kind::clock}, what)) {
      out.push_back(object.name);
    }
    return out;
  }

  // ---- SDC commands ----

  void add_clock(Sdc::Clock clock_data, bool add) {
    // A clock made on a source takes it over, unless -add; a clock of the same name is replaced.
    for (auto& other : sdc_.clocks) {
      if (!add && other.name != clock_data.name) {
        std::erase_if(other.sources, [&](const Sdc_object& s) { return std::find(clock_data.sources.begin(), clock_data.sources.end(), s) != clock_data.sources.end(); });
      }
    }
    if (auto* existing = clock(clock_data.name)) {
      *existing = std::move(clock_data);
    } else {
      sdc_.clocks.push_back(std::move(clock_data));
    }
  }

  std::string create_clock(Words& w) {
    const auto o = parse(w, {"-add"}, {"-name", "-period", "-waveform", "-comment"});
    Sdc::Clock c;
    const auto* period = o.value("-period");
    if (period == nullptr) {
      throw Error("create_clock needs -period");
    }
    c.period_ps = time(*period, "create_clock -period");
    if (!(c.period_ps > 0)) {
      throw Error("create_clock -period must be positive");
    }
    c.fall_ps = c.period_ps / 2;
    if (const auto* waveform = o.value("-waveform")) {
      const auto edges = tcl::list_split(*waveform);
      if (edges.size() != 2) {
        throw Error("create_clock -waveform with " + std::to_string(edges.size()) + " edges is not supported");
      }
      c.rise_ps = time(edges[0], "create_clock -waveform");
      c.fall_ps = time(edges[1], "create_clock -waveform");
      c.waveform_given = true;
    }
    for (const auto& argument : o.rest) {
      for (auto& object : objects(argument, {Kind::port, Kind::pin}, "create_clock")) {
        c.sources.push_back(std::move(object));
      }
    }
    if (const auto* name = o.value("-name")) {
      c.name = *name;
    } else if (!c.sources.empty()) {
      c.name = c.sources.front().name;
    } else {
      throw Error("create_clock needs -name or a source that is in the design");
    }
    add_clock(std::move(c), o.flag("-add"));
    return {};
  }

  std::string create_generated_clock(Words& w) {
    const auto o = parse(w, {"-add", "-invert", "-combinational"},
                         {"-name", "-source", "-master_clock", "-divide_by", "-multiply_by", "-edges", "-edge_shift", "-duty_cycle", "-comment"});
    if (o.value("-edge_shift") != nullptr) {
      throw Error("create_generated_clock -edge_shift is not supported");
    }
    const auto* source = o.value("-source");
    if (source == nullptr) {
      throw Error("create_generated_clock needs -source");
    }
    const auto        from   = objects(*source, {Kind::port, Kind::pin}, "create_generated_clock -source");
    const Sdc::Clock* master = nullptr;
    if (const auto* name = o.value("-master_clock")) {
      const auto names = clocks_of(*name, "create_generated_clock -master_clock");
      master           = names.empty() ? nullptr : clock(names.front());
    } else {
      for (const auto& candidate : sdc_.clocks) {
        for (const auto& s : from) {
          if (std::find(candidate.sources.begin(), candidate.sources.end(), s) != candidate.sources.end()) {
            master = &candidate;
          }
        }
      }
      if (master == nullptr) {
        // The source is somewhere inside the network of its master: with one clock so far there is no doubt.
        const Sdc::Clock* only  = nullptr;
        size_t            count = 0;
        for (const auto& candidate : sdc_.clocks) {
          if (!candidate.generated) {
            only = &candidate;
            ++count;
          }
        }
        master = count == 1 ? only : nullptr;
      }
    }
    if (master == nullptr) {
      throw Error("create_generated_clock: no master clock is found on the source (name it with -master_clock)");
    }
    Sdc::Clock c;
    c.generated = true;
    c.master    = master->name;
    c.slew_ps   = master->slew_ps;
    const double T = master->period_ps, rise = master->rise_ps, fall = master->fall_ps;
    const auto   count = [&](const char* option) -> double {
      const auto* text = o.value(option);
      double      v    = 1;
      if (text != nullptr && (!tcl::to_number(*text, v) || v < 1)) {
        throw Error(std::string("create_generated_clock ") + option + " expects a positive number");
      }
      return v;
    };
    if (const auto* edges = o.value("-edges")) {
      const auto list = tcl::list_split(*edges);
      if (list.size() != 3) {
        throw Error("create_generated_clock -edges expects three edges");
      }
      // Edge 1 is the first rising edge of the master, 2 its falling edge, 3 the next rising edge, ...
      double at[3];
      for (size_t i = 0; i < 3; ++i) {
        double k = 0;
        if (!tcl::to_number(list[i], k) || k < 1) {
          throw Error("create_generated_clock -edges expects edge numbers from 1");
        }
        const auto n = static_cast<long long>(k);
        at[i]        = n % 2 == 1 ? rise + static_cast<double>((n - 1) / 2) * T : fall + static_cast<double>((n - 2) / 2) * T;
      }
      if (!(at[2] > at[0])) {
        throw Error("create_generated_clock -edges must increase");
      }
      c.period_ps = at[2] - at[0];
      c.rise_ps   = at[0];
      c.fall_ps   = at[1];
    } else {
      const double divide = count("-divide_by"), multiply = count("-multiply_by");
      c.period_ps = T * divide / multiply;
      c.rise_ps   = rise;
      c.fall_ps   = divide == 1 && multiply == 1 ? fall : rise + c.period_ps / 2;
      if (o.flag("-invert")) {
        if (divide != 1 || multiply != 1) {
          throw Error("create_generated_clock -invert with a division or multiplication is not supported");
        }
        std::swap(c.rise_ps, c.fall_ps);
      }
    }
    for (const auto& argument : o.rest) {
      for (auto& object : objects(argument, {Kind::port, Kind::pin}, "create_generated_clock")) {
        c.sources.push_back(std::move(object));
      }
    }
    if (const auto* name = o.value("-name")) {
      c.name = *name;
    } else if (!c.sources.empty()) {
      c.name = c.sources.front().name;
    } else {
      throw Error("create_generated_clock needs -name or a target that is in the design");
    }
    add_clock(std::move(c), o.flag("-add"));
    return {};
  }

  std::string set_clock_groups(Words& w) {
    const auto o = parse(w, {"-asynchronous", "-logically_exclusive", "-physically_exclusive", "-allow_paths"}, {"-name", "-group", "-comment"});
    if (o.flag("-allow_paths")) {
      throw Error("set_clock_groups -allow_paths is not supported");
    }
    if (!o.rest.empty()) {
      throw Error("set_clock_groups: unexpected \"" + plain_of(o.rest.front()) + "\"");
    }
    Sdc::Clock_group group;
    for (const auto& [key, value] : o.values) {
      if (key == "-group") {
        group.groups.push_back(clocks_of(value, "set_clock_groups -group"));
      }
    }
    if (group.groups.empty()) {
      throw Error("set_clock_groups needs -group");
    }
    sdc_.clock_groups.push_back(std::move(group));
    return {};
  }

  std::string set_clock_uncertainty(Words& w) {
    const auto o = parse(w, {"-setup", "-hold", "-rise", "-fall"}, {"-from", "-to", "-rise_from", "-fall_from", "-rise_to", "-fall_to"});
    if (!o.values.empty()) {
      throw Error("set_clock_uncertainty between two clocks (" + o.values.front().first + ") is not supported");
    }
    if (o.rest.empty()) {
      throw Error("set_clock_uncertainty needs a value");
    }
    const double value = time(o.rest[0], "set_clock_uncertainty");
    if (o.flag("-hold") && !o.flag("-setup")) {
      report("set_clock_uncertainty -hold (the hold uncertainty is not modelled)");
      return {};
    }
    std::vector<std::string> names;
    for (size_t i = 1; i < o.rest.size(); ++i) {
      for (const auto& object : objects(o.rest[i], {Kind::clock, Kind::port, Kind::pin}, "set_clock_uncertainty")) {
        if (object.kind != Kind::clock) {
          throw Error("set_clock_uncertainty on a pin is not supported");
        }
        names.push_back(object.name);
      }
    }
    for (auto& c : sdc_.clocks) {
      if (o.rest.size() == 1 || std::find(names.begin(), names.end(), c.name) != names.end()) {
        c.uncertainty_ps = static_cast<float>(value);
      }
    }
    return {};
  }

  std::string set_clock_transition(Words& w) {
    const auto o = parse(w, {"-rise", "-fall", "-min", "-max"}, {});
    if (o.rest.size() != 2) {
      throw Error("set_clock_transition expects a value and clocks");
    }
    const double value     = time(o.rest[0], "set_clock_transition");
    const bool   only_min  = o.flag("-min") && !o.flag("-max");
    for (const auto& name : clocks_of(o.rest[1], "set_clock_transition")) {
      auto* c = clock(name);
      if (c == nullptr) {
        continue;  // a name kept for a consumer that resolves clocks itself
      }
      // The late transition is the largest of what is given; a -min value alone only stands until a late one.
      if (only_min) {
        if (!c->slew_ps) {
          c->slew_ps = static_cast<float>(value);
          min_only_slew_.insert(name);
        }
      } else if (!c->slew_ps || min_only_slew_.erase(name) != 0) {
        c->slew_ps = static_cast<float>(value);
      } else {
        c->slew_ps = std::max(*c->slew_ps, static_cast<float>(value));
      }
    }
    return {};
  }

  std::string io_delay(Words& w) {
    const bool input = w[0] == "set_input_delay";
    const auto o     = parse(w,
                             {"-clock_fall", "-rise", "-fall", "-max", "-min", "-add_delay", "-network_latency_included", "-source_latency_included"},
                             {"-clock", "-reference_pin"});
    for (const char* option : {"-clock_fall", "-rise", "-fall"}) {
      if (o.flag(option)) {
        throw Error(w[0] + " " + option + " is not supported");
      }
    }
    if (o.value("-reference_pin") != nullptr) {
      throw Error(w[0] + " -reference_pin is not supported");
    }
    if (o.rest.size() != 2) {
      throw Error(w[0] + " expects a delay and ports");
    }
    Sdc::Delay d;
    d.input    = input;
    d.delay_ps = static_cast<float>(time(o.rest[0], w[0]));
    d.max      = o.flag("-max") || !o.flag("-min");
    d.min      = o.flag("-min") || !o.flag("-max");
    if (const auto* name = o.value("-clock")) {
      const auto names = clocks_of(*name, w[0] + " -clock");
      if (names.size() != 1) {
        throw Error(w[0] + " -clock expects one clock");
      }
      d.clock = names.front();
    }
    for (auto& object : objects(o.rest[1], {Kind::port, Kind::pin}, w[0])) {
      if (object.kind != Kind::port) {
        throw Error(w[0] + " on a pin is not supported");
      }
      d.port = std::move(object);
      sdc_.delays.push_back(d);
    }
    return {};
  }

  std::string set_input_transition(Words& w) {
    const auto o = parse(w, {"-rise", "-fall", "-min", "-max"}, {"-clock"});
    if (o.rest.size() != 2) {
      throw Error("set_input_transition expects a value and ports");
    }
    const double value = time(o.rest[0], "set_input_transition");
    if (o.flag("-min") && !o.flag("-max")) {
      return {};  // the early transition is not modelled apart from the late one
    }
    for (auto& object : objects(o.rest[1], {Kind::port}, "set_input_transition")) {
      sdc_.transitions.push_back({std::move(object), static_cast<float>(value)});
    }
    return {};
  }

  std::string set_load(Words& w) {
    const auto o = parse(w, {"-pin_load", "-wire_load", "-min", "-max", "-rise", "-fall", "-subtract_pin_load"}, {"-corner"});
    if (o.flag("-subtract_pin_load")) {
      throw Error("set_load -subtract_pin_load is not supported");
    }
    if (o.rest.size() != 2) {
      throw Error("set_load expects a capacitance and ports");
    }
    double value = 0;
    if (!tcl::to_number(o.rest[0], value)) {
      throw Error("set_load: \"" + plain_of(o.rest[0]) + "\" is not a number");
    }
    if (o.flag("-min") && !o.flag("-max")) {
      return {};
    }
    for (auto& object : objects(o.rest[1], {Kind::port, Kind::net}, "set_load")) {
      if (object.kind != Kind::port) {
        throw Error("set_load on a net is not supported");
      }
      sdc_.loads.push_back({std::move(object), static_cast<float>(value * units_.capacitance_ff)});
    }
    return {};
  }

  std::string set_driving_cell(Words& w) {
    const auto o = parse(w, {"-rise", "-fall", "-min", "-max", "-dont_scale", "-no_design_rule"},
                         {"-lib_cell", "-cell", "-library", "-pin", "-from_pin", "-input_transition_rise", "-input_transition_fall", "-multiply_by", "-clock"});
    const auto* cell = o.value("-lib_cell") != nullptr ? o.value("-lib_cell") : o.value("-cell");
    if (cell == nullptr) {
      throw Error("set_driving_cell needs -lib_cell");
    }
    if (o.rest.size() != 1) {
      throw Error("set_driving_cell expects ports");
    }
    Sdc::Drive drive;
    drive.cell = plain_of(*cell);
    if (const auto slash = drive.cell.rfind('/'); slash != std::string::npos) {
      drive.cell.erase(0, slash + 1);  // library/cell
    }
    if (const auto* pin = o.value("-pin")) {
      drive.pin = plain_of(*pin);
    }
    for (const char* option : {"-input_transition_rise", "-input_transition_fall"}) {
      if (const auto* text = o.value(option)) {
        drive.input_slew_ps = std::max(drive.input_slew_ps, static_cast<float>(time(*text, std::string("set_driving_cell ") + option)));
      }
    }
    for (auto& object : objects(o.rest[0], {Kind::port}, "set_driving_cell")) {
      drive.port = std::move(object);
      sdc_.drives.push_back(drive);
    }
    return {};
  }

  std::string set_drive(Words& w) {
    const auto o = parse(w, {"-rise", "-fall", "-min", "-max"}, {});
    double     value = 0;
    if (o.rest.empty() || !tcl::to_number(o.rest[0], value)) {
      throw Error("set_drive expects a resistance");
    }
    if (value != 0) {
      report("set_drive (a drive resistance is not modelled; set_driving_cell is)");
    }
    return {};
  }

  std::string set_max_fanout(Words& w) {
    const auto o = parse(w, {}, {});
    double     value = 0;
    if (o.rest.size() != 2 || !tcl::to_number(o.rest[0], value) || value <= 0) {
      throw Error("set_max_fanout expects a positive limit and the design or ports");
    }
    bool design = false;
    for (const auto& element : tcl::list_split(o.rest[1])) {
      design = design || is_design(element);
    }
    if (!design) {
      report("set_max_fanout on ports (only the limit of the design is checked)");
      return {};
    }
    sdc_.max_fanout = sdc_.max_fanout ? std::min(*sdc_.max_fanout, static_cast<float>(value)) : static_cast<float>(value);
    return {};
  }

  std::string set_false_path(Words& w) {
    const auto o = parse(w, {"-setup", "-hold", "-rise", "-fall", "-reset_path"},
                         {"-from", "-to", "-through", "-rise_from", "-fall_from", "-rise_to", "-fall_to", "-rise_through", "-fall_through", "-comment"});
    for (const char* option : {"-rise", "-fall"}) {
      if (o.flag(option)) {
        throw Error(std::string("set_false_path ") + option + " is not supported");
      }
    }
    if (!o.rest.empty()) {
      throw Error("set_false_path: unexpected \"" + plain_of(o.rest.front()) + "\"");
    }
    Sdc::Exception e;
    e.setup            = o.flag("-setup") || !o.flag("-hold");
    e.hold             = o.flag("-hold") || !o.flag("-setup");
    size_t throughs    = 0;
    bool   given[3]    = {false, false, false};
    for (const auto& [key, value] : o.values) {
      if (key == "-from") {
        given[0] = true;
        for (auto& object : objects(value, {Kind::clock, Kind::port, Kind::pin, Kind::cell}, "set_false_path -from")) {
          e.from.push_back(std::move(object));
        }
      } else if (key == "-to") {
        given[2] = true;
        for (auto& object : objects(value, {Kind::clock, Kind::port, Kind::pin, Kind::cell}, "set_false_path -to")) {
          e.to.push_back(std::move(object));
        }
      } else if (key == "-through") {
        given[1] = true;
        if (++throughs > 1) {
          throw Error("set_false_path with more than one -through is not supported");
        }
        for (auto& object : objects(value, {Kind::port, Kind::pin, Kind::net, Kind::cell}, "set_false_path -through")) {
          e.through.push_back(std::move(object));
        }
      } else if (key != "-comment") {
        throw Error("set_false_path " + key + " is not supported");
      }
    }
    if (!given[0] && !given[1] && !given[2]) {
      throw Error("set_false_path needs -from, -through or -to");
    }
    // A list that was given and names nothing here (a port of another partition) is an exception this design
    // does not need: it must not widen into an exception with fewer conditions.
    if ((given[0] && e.from.empty()) || (given[1] && e.through.empty()) || (given[2] && e.to.empty())) {
      return {};
    }
    sdc_.false_paths.push_back(std::move(e));
    return {};
  }

  std::string set_max_delay(Words& w) {
    const auto o = parse(w, {"-rise", "-fall", "-ignore_clock_latency", "-reset_path", "-probe"},
                         {"-from", "-to", "-through", "-rise_from", "-fall_from", "-rise_to", "-fall_to", "-rise_through", "-fall_through", "-comment"});
    for (const char* option : {"-rise", "-fall", "-probe"}) {
      if (o.flag(option)) {
        throw Error(std::string("set_max_delay ") + option + " is not supported");
      }
    }
    if (o.rest.size() != 1) {
      throw Error("set_max_delay expects one delay");
    }
    Sdc::Path_delay d;
    d.delay_ps = static_cast<float>(time(o.rest[0], "set_max_delay"));
    bool from = false, to = false;
    for (const auto& [key, value] : o.values) {
      if (key == "-from") {
        from = true;
        for (auto& object : objects(value, {Kind::clock, Kind::port, Kind::pin, Kind::cell}, "set_max_delay -from")) {
          d.from.push_back(std::move(object));
        }
      } else if (key == "-to") {
        to = true;
        for (auto& object : objects(value, {Kind::clock, Kind::port, Kind::pin, Kind::cell}, "set_max_delay -to")) {
          d.to.push_back(std::move(object));
        }
      } else if (key != "-comment") {
        throw Error("set_max_delay " + key + " is not supported");
      }
    }
    // A list that names nothing here must not widen the constraint to every start or end point.
    if ((from && d.from.empty()) || (to && d.to.empty())) {
      return {};
    }
    sdc_.max_delays.push_back(std::move(d));
    return {};
  }

  std::string set_disable_timing(Words& w) {
    const auto o = parse(w, {}, {"-from", "-to"});
    if (!o.values.empty()) {
      throw Error("set_disable_timing -from/-to is not supported");
    }
    for (const auto& argument : o.rest) {
      for (auto& object : objects(argument, {Kind::port, Kind::pin, Kind::cell}, "set_disable_timing")) {
        if (object.kind == Kind::cell) {
          throw Error("set_disable_timing on a cell is not supported");
        }
        sdc_.disabled.push_back(std::move(object));
      }
    }
    return {};
  }

  // A word of a command as the flat text of `Sdc::commands`.
  static std::string quoted(std::string_view text) {
    if (text.empty()) {
      return "{}";
    }
    for (const char c : text) {
      if (c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '"' || c == '$' || c == '[' || c == ']') {
        return "{" + std::string(text) + "}";
      }
    }
    return std::string(text);
  }
  // The value of [all_inputs] or [all_outputs] ends in a space, which a list does not see and a variable
  // keeps: a word that is still that value (and still what the selector gives now) stays the selector in the
  // flat text. A consumer keeps its own meaning of it, and the line stays short on a design with many ports.
  [[nodiscard]] std::string whole_selector(const std::string& word) const {
    if (word.empty() || word.back() != ' ') {
      return {};
    }
    if (word == all_text(true, false)) {
      return "[all_inputs]";
    }
    if (word == all_text(true, true)) {
      return "[all_inputs -no_clocks]";
    }
    return word == all_text(false, false) ? "[all_outputs]" : std::string();
  }
  [[nodiscard]] std::string flat_word(const std::string& word, bool bare_clock) const {
    if (word.find(mark) == std::string::npos) {
      return quoted(word);
    }
    std::string out;
    if (auto whole = whole_selector(word); !whole.empty()) {
      return whole;
    }
    const auto elements = tcl::list_split(word);
    for (size_t i = 0; i < elements.size();) {
      if (!out.empty()) {
        out += ' ';
      }
      if (is_design(elements[i])) {
        out += "[current_design]";
        ++i;
        continue;
      }
      const auto first = object_of(elements[i]);
      if (!first) {
        out += quoted(plain_of(elements[i]));
        ++i;
        continue;
      }
      if (bare_clock && first->kind == Kind::clock && elements.size() == 1) {
        return first->name;
      }
      out += std::string("[get_") + word_of(first->kind) + "s {";
      bool any = false;
      for (; i < elements.size(); ++i) {
        const auto object = object_of(elements[i]);
        if (!object || object->kind != first->kind) {
          break;
        }
        out += any ? " " : "";
        any  = true;
        for (const char c : object->name) {
          if (c == ' ' || c == '\t' || c == '{' || c == '}' || c == '\\') {
            out += '\\';
          }
          out += c;
        }
      }
      out += "}]";
    }
    return out;
  }
  void record(const Words& w) {
    const bool  clock = w[0] == "create_clock" || w[0] == "create_generated_clock";
    std::string text  = w[0];
    for (size_t i = 1; i < w.size(); ++i) {
      if (w[i].empty()) {
        if (clock) {
          continue;  // a source that is not in the design: the clock is still made
        }
        return;  // an object list that came out empty: nothing here to constrain
      }
      text += ' ';
      text += flat_word(w[i], w[i - 1] == "-clock" || w[i - 1] == "-master_clock");
    }
    sdc_.commands.push_back({interp_.line(), std::move(text), w[0], {}});
  }

  // The SDC commands. Returns false for a name that is not one. An SDC command that fails (an option that is
  // not modelled, a query that cannot be answered) is listed and returns nothing; the script goes on.
  bool command(Words& w, std::string& result) {
    const std::string name = w[0];
    // No effect on what the engine computes: the design is already selected, the clocks are ideal, wire load
    // models are not used, and the `ideal` commands are accepted without effect, as OpenSTA does.
    static const std::set<std::string_view> ignored{
        "set_propagated_clock", "set_units", "set_cmd_units", "set_ideal_network", "set_ideal_latency", "set_ideal_transition", "set_ideal_net",
        "set_wire_load_mode", "set_wire_load_model", "set_wire_load_selection_group", "set_wire_load_min_block_size", "set_operating_conditions",
        "group_path", "set_dont_touch", "set_dont_use", "set_hierarchy_separator", "set_max_area", "set_voltage", "set_max_dynamic_power",
        "set_max_leakage_power"};
    // Constraints that change timing and are not modelled: listed, once each.
    static const std::set<std::string_view> unmodelled{
        "set_multicycle_path", "set_min_delay", "set_data_check", "set_timing_derate", "set_case_analysis", "set_logic_zero",
        "set_logic_one", "set_logic_dc", "set_clock_latency", "set_clock_sense", "set_sense", "set_clock_gating_check", "set_min_pulse_width",
        "set_max_transition", "set_max_capacitance", "set_min_capacitance", "set_max_time_borrow", "set_resistance", "set_fanout_load",
        "set_port_fanout_number", "set_input_noise"};
    static const std::set<std::string_view> queries{"get_ports", "get_port", "get_pins", "get_pin", "get_nets", "get_net", "get_clocks", "get_clock",
                                                    "get_cells", "get_cell", "get_lib_cells", "get_lib_cell", "get_libs", "get_lib_pins", "all_inputs",
                                                    "all_outputs", "all_clocks", "current_design"};
    static const std::set<std::string_view> modelled{"create_clock", "create_generated_clock", "set_clock_groups", "set_clock_uncertainty",
                                                     "set_clock_transition", "set_input_delay", "set_output_delay", "set_input_transition", "set_load",
                                                     "set_driving_cell", "set_drive", "set_max_fanout", "set_false_path", "set_max_delay",
                                                     "set_disable_timing"};
    const bool query_command = queries.contains(name);
    if (!query_command && !modelled.contains(name) && !ignored.contains(name) && !unmodelled.contains(name)) {
      return false;
    }
    if (!query_command) {
      record(w);
    }
    result.clear();
    try {
      if (name == "get_ports" || name == "get_port") {
        result = query(w, Kind::port);
      } else if (name == "get_pins" || name == "get_pin") {
        result = query(w, Kind::pin);
      } else if (name == "get_nets" || name == "get_net") {
        result = query(w, Kind::net);
      } else if (name == "get_clocks" || name == "get_clock") {
        result = query(w, Kind::clock);
      } else if (name == "get_cells" || name == "get_cell") {
        result = query(w, Kind::cell);
      } else if (name == "get_lib_cells" || name == "get_lib_cell" || name == "get_libs" || name == "get_lib_pins") {
        result = w.size() > 1 ? w.back() : std::string();
      } else if (name == "all_inputs" || name == "all_outputs") {
        result = all_ports(w, name == "all_inputs");
      } else if (name == "all_clocks") {
        std::vector<std::string> out;
        for (const auto& c : sdc_.clocks) {
          out.push_back(handle(Kind::clock, c.name));
        }
        result = tcl::list_join(out);
      } else if (name == "current_design") {
        result = std::string{mark, 'D', ':'} + "design";
      } else if (name == "create_clock") {
        result = create_clock(w);
      } else if (name == "create_generated_clock") {
        result = create_generated_clock(w);
      } else if (name == "set_clock_groups") {
        result = set_clock_groups(w);
      } else if (name == "set_clock_uncertainty") {
        result = set_clock_uncertainty(w);
      } else if (name == "set_clock_transition") {
        result = set_clock_transition(w);
      } else if (name == "set_input_delay" || name == "set_output_delay") {
        result = io_delay(w);
      } else if (name == "set_input_transition") {
        result = set_input_transition(w);
      } else if (name == "set_load") {
        result = set_load(w);
      } else if (name == "set_driving_cell") {
        result = set_driving_cell(w);
      } else if (name == "set_drive") {
        result = set_drive(w);
      } else if (name == "set_max_fanout") {
        result = set_max_fanout(w);
      } else if (name == "set_false_path") {
        result = set_false_path(w);
      } else if (name == "set_max_delay") {
        result = set_max_delay(w);
      } else if (name == "set_disable_timing") {
        result = set_disable_timing(w);
      } else if (unmodelled.contains(name)) {
        report(name + " (not modelled)");
      }
    } catch (const Error& e) {
      const std::string what = e.what();
      report(what.starts_with(name) ? what : name + " (" + what + ")");
      if (query_command) {
        sdc_.commands.push_back({interp_.line(), {}, name, what});
      }
      result.clear();
    }
    return true;
  }

  Sdc&                                                 sdc_;
  Sdc_units                                            units_;
  const Sdc_design&                                    design_;
  tcl::Interp                                          interp_;
  std::vector<Port>                                    ports_;
  std::unordered_map<std::string, size_t>              port_index_;
  std::unordered_map<std::string, std::vector<size_t>> bus_;
  std::set<std::string>                                reported_, min_only_slew_;
};

}  // namespace

Sdc read_sdc(std::string_view text, Sdc_units units, const Sdc_design& design) {
  Sdc sdc;
  Reader(sdc, units, design).run(text);
  return sdc;
}

Sdc read_sdc(std::string_view text, Sdc_units units) { return read_sdc(text, units, Unknown_design()); }

Constraints read_constraints(std::string_view text, Sdc_units units, const Netlist& netlist, const Design_names* names) {
  return resolve(read_sdc(text, units, Netlist_design(netlist, names)), netlist, names);
}

}  // namespace livehd::timing::fast
