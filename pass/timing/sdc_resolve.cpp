// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// The typed SDC of a design as the constraints of the fast timing engine: objects to pins, clocks to source
// pins and to the relations between them, exceptions to cut pins, excluded endpoints and launch groups.
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "sdc.hpp"

namespace livehd::timing::fast {
namespace {
constexpr float inf = std::numeric_limits<float>::infinity();
using Kind          = Sdc_object::Kind;

// From a rising edge of the launch clock: the time to the first later rising edge of the capture clock (setup),
// and to the last capture edge that is not later (hold; never positive), the tightest over the common period.
void relation(double launch_period, double launch_rise, double capture_period, double capture_rise, float& setup, float& hold) {
  size_t edges = 1;
  for (; edges < 1000; ++edges) {
    const double ratio = static_cast<double>(edges) * launch_period / capture_period;
    if (std::fabs(ratio - std::round(ratio)) < 1e-6 * std::max(1.0, ratio)) {
      break;
    }
  }
  double tightest = std::numeric_limits<double>::infinity(), closest = -std::numeric_limits<double>::infinity();
  for (size_t i = 0; i < edges; ++i) {
    const double launch = launch_rise + static_cast<double>(i) * launch_period;
    double       next   = capture_rise + (std::floor((launch - capture_rise) / capture_period + 1e-9) + 1) * capture_period;
    if (next - launch < 1e-6 * capture_period) {
      next += capture_period;
    }
    tightest = std::min(tightest, next - launch);
    closest  = std::max(closest, std::min(0.0, next - capture_period - launch));
  }
  setup = static_cast<float>(tightest);
  hold  = static_cast<float>(closest);
}

// The setup relation given for some endpoints: +inf for a false path, a delay for set_max_delay. A false path
// wins over a delay, and of two delays the smaller.
using Relations = std::map<uint32_t, float>;
void merge(Relations& into, uint32_t key, float value) {
  const auto [it, fresh] = into.emplace(key, value);
  if (!fresh) {
    it->second = value == inf || it->second == inf ? inf : std::min(it->second, value);
  }
}

// What a start point launches into: its relation to every capture clock, and the endpoints that have a relation
// of their own for it.
struct Launch {
  std::vector<float>                      setup, hold;
  std::vector<std::pair<uint32_t, float>> relations;
  auto                                    operator<=>(const Launch&) const = default;
};

class Resolver {
public:
  Resolver(const Sdc& sdc, const Netlist& netlist, const Design_names* names) : sdc_(sdc), netlist_(netlist), types_(netlist.types()), names_(names) {
    out_.unsupported = sdc.unsupported;
    noted_.insert(sdc.unsupported.begin(), sdc.unsupported.end());
    for (const auto& port : netlist_.ports()) {
      ports_.emplace(port.name, &port);
    }
  }

  Constraints run() {
    clocks();
    relations();
    outputs();
    exceptions();
    path_delays();
    groups();
    emit_outputs();
    for (const auto& object : sdc_.disabled) {
      if (const auto pin = pin_of(object); pin != no_index) {
        out_.cut_pins.push_back(pin);
      } else {
        missing(object, "set_disable_timing");
      }
    }
    out_.max_fanout = sdc_.max_fanout.value_or(0.f);
    const auto unique = [](std::vector<uint32_t>& v) {
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    unique(out_.cut_pins);
    unique(out_.excluded_endpoints);
    return std::move(out_);
  }

private:
  void note(std::string text) {
    if (noted_.insert(text).second) {
      out_.unsupported.push_back(std::move(text));
    }
  }
  void missing(const Sdc_object& object, std::string_view what) {
    note(std::format("{}: {} '{}' is not in the netlist", what, object.kind == Kind::pin ? "pin" : object.kind == Kind::net ? "net" : "object", object.name));
  }

  [[nodiscard]] uint32_t pin_of(const Sdc_object& object) const {
    switch (object.kind) {
      case Kind::port: {
        const auto it = ports_.find(object.name);
        return it == ports_.end() ? no_index : netlist_.pin(it->second->cell, 0u);
      }
      case Kind::pin: {
        const auto slash = object.name.rfind('/');
        if (names_ == nullptr || slash == std::string::npos) {
          return no_index;
        }
        const auto cell = names_->cell(std::string_view(object.name).substr(0, slash));
        return cell == no_index ? no_index : netlist_.pin(cell, std::string_view(object.name).substr(slash + 1));
      }
      case Kind::net: {
        if (const auto it = ports_.find(object.name); it != ports_.end()) {
          const auto pin = netlist_.pin(it->second->cell, 0u);
          if (it->second->input) {
            return pin;
          }
          const auto net = netlist_.net_of(pin);
          return net == no_index ? no_index : netlist_.driver_of(net);
        }
        return names_ == nullptr ? no_index : names_->net_driver(object.name);
      }
      default: return no_index;
    }
  }
  [[nodiscard]] uint32_t clock_of(const std::string& name) const {
    for (uint32_t i = 0; i < sdc_.clocks.size(); ++i) {
      if (sdc_.clocks[i].name == name) {
        return i;
      }
    }
    return no_index;
  }
  // The clock pins of a sequential cell, and its checked data pins.
  [[nodiscard]] bool launches(uint32_t pin) const {
    const auto& type = types_.type(netlist_.type_of(netlist_.cell_of(pin)));
    return netlist_.pin_type(pin).clock && type.sequential && !type.clock_gate;
  }
  [[nodiscard]] bool checked(uint32_t pin) const {
    const auto cell = netlist_.cell_of(pin);
    if (netlist_.type_of(cell) == Cell_types::output_port_type) {
      return true;
    }
    for (const auto& arc : types_.arcs_into(netlist_.type_of(cell), netlist_.local_of(pin))) {
      if (arc.kind == Arc_kind::setup_rise) {
        return true;
      }
    }
    return false;
  }

  void clocks() {
    for (const auto& clock : sdc_.clocks) {
      Clock c{clock.name, "", static_cast<float>(clock.period_ps), clock.uncertainty_ps.value_or(0.f), clock.slew_ps.value_or(0.f), {}, 0};
      for (const auto& source : clock.sources) {
        if (const auto pin = pin_of(source); pin != no_index) {
          c.source_pins.push_back(pin);
          clock_sources_.insert(pin);
        } else {
          note(std::format("clock {}: its source '{}' is not in the netlist", clock.name, source.name));
        }
      }
      if (!clock.sources.empty() && c.source_pins.empty()) {
        note(std::format("clock {} has no source in the netlist: it clocks nothing and only references IO delays", clock.name));
      }
      out_.clocks.push_back(std::move(c));
    }
  }

  void relations() {
    const size_t n = sdc_.clocks.size();
    setup_.assign(n * n, 0.f);
    hold_.assign(n * n, 0.f);
    for (size_t l = 0; l < n; ++l) {
      for (size_t c = 0; c < n; ++c) {
        relation(sdc_.clocks[l].period_ps, sdc_.clocks[l].rise_ps, sdc_.clocks[c].period_ps, sdc_.clocks[c].rise_ps, setup_[l * n + c], hold_[l * n + c]);
      }
    }
    for (const auto& set : sdc_.clock_groups) {
      std::vector<int> group(n, -1);
      for (size_t g = 0; g < set.groups.size(); ++g) {
        for (const auto& name : set.groups[g]) {
          if (const auto c = clock_of(name); c != no_index) {
            group[c] = static_cast<int>(g);
          }
        }
      }
      for (size_t a = 0; a < n; ++a) {
        for (size_t b = 0; b < n; ++b) {
          // With one group the clocks that are not named are the other side.
          const bool apart = set.groups.size() == 1 ? (group[a] == 0) != (group[b] == 0) : group[a] >= 0 && group[b] >= 0 && group[a] != group[b];
          if (apart) {
            setup_[a * n + b] = hold_[a * n + b] = inf;
          }
        }
      }
    }
  }

  struct Points {
    std::vector<uint32_t> clocks, pins;
    bool                  given = false;
    [[nodiscard]] bool    empty() const { return clocks.empty() && pins.empty(); }
  };

  Points starts(const std::vector<Sdc_object>& objects, std::vector<uint32_t>& through) {
    Points p;
    p.given = !objects.empty();
    for (const auto& object : objects) {
      if (object.kind == Kind::clock) {
        if (const auto c = clock_of(object.name); c != no_index) {
          p.clocks.push_back(c);
        }
        continue;
      }
      if (object.kind == Kind::cell) {
        const auto cell = names_ == nullptr ? no_index : names_->cell(object.name);
        if (cell == no_index) {
          missing(object, command_ + " -from");
          continue;
        }
        for (uint32_t local = 0; local < types_.type(netlist_.type_of(cell)).pin_count; ++local) {
          if (launches(netlist_.pin(cell, local))) {
            p.pins.push_back(netlist_.pin(cell, local));
          }
        }
        continue;
      }
      const auto pin = pin_of(object);
      if (pin == no_index) {
        missing(object, command_ + " -from");
      } else if (object.kind == Kind::port) {
        // A clock port keeps its arrival: it is the clock, not a data start. An output port starts nothing.
        if (ports_.at(object.name)->input && !clock_sources_.contains(pin)) {
          p.pins.push_back(pin);
        }
      } else if (launches(pin)) {
        p.pins.push_back(pin);
      } else {
        // Not a start point: every path through the pin, which is what the tools do with it too.
        through.push_back(pin);
      }
    }
    return p;
  }

  Points ends(const std::vector<Sdc_object>& objects) {
    Points p;
    p.given = !objects.empty();
    for (const auto& object : objects) {
      if (object.kind == Kind::clock) {
        if (const auto c = clock_of(object.name); c != no_index) {
          p.clocks.push_back(c);
        }
        continue;
      }
      if (object.kind == Kind::cell) {
        const auto cell = names_ == nullptr ? no_index : names_->cell(object.name);
        if (cell == no_index) {
          missing(object, command_ + " -to");
          continue;
        }
        for (uint32_t local = 0; local < types_.type(netlist_.type_of(cell)).pin_count; ++local) {
          if (checked(netlist_.pin(cell, local))) {
            p.pins.push_back(netlist_.pin(cell, local));
          }
        }
        continue;
      }
      const auto pin = pin_of(object);
      if (pin == no_index) {
        missing(object, command_ + " -to");
      } else if (object.kind == Kind::port ? !ports_.at(object.name)->input : checked(pin)) {
        p.pins.push_back(pin);
      } else if (object.kind == Kind::pin) {
        note(std::format("{} -to: pin '{}' is not an endpoint", command_, object.name));
      }
    }
    return p;
  }

  // The start points (input port pins, flip-flop clock pins) with a path to one of `targets`. False when the
  // cone is too large to walk.
  bool starts_behind(const std::vector<uint32_t>& targets, std::unordered_set<uint32_t>& found) const {
    std::vector<uint32_t>        work(targets.begin(), targets.end());
    std::unordered_set<uint32_t> seen(targets.begin(), targets.end());
    while (!work.empty()) {
      if (seen.size() > cone_limit) {
        return false;
      }
      const auto pin = work.back();
      work.pop_back();
      const auto visit = [&](uint32_t next) {
        if (seen.insert(next).second) {
          work.push_back(next);
        }
      };
      const auto net = netlist_.net_of(pin);
      if (net != no_index && !netlist_.is_driver(pin)) {
        visit(netlist_.driver_of(net));
        continue;
      }
      const auto cell = netlist_.cell_of(pin);
      const auto type = netlist_.type_of(cell);
      if (type == Cell_types::input_port_type) {
        found.insert(pin);
        continue;
      }
      if (netlist_.untimed(pin)) {
        continue;  // nothing is launched from a cell that is not timed
      }
      for (const auto& arc : types_.arcs_into(type, netlist_.local_of(pin))) {
        if (arc.kind == Arc_kind::launch_rise) {
          found.insert(netlist_.pin(cell, arc.from));
        } else if (arc.kind == Arc_kind::combinational) {
          visit(netlist_.pin(cell, arc.from));
        }
      }
    }
    return true;
  }

  // The endpoints (checked data pins, output port pins) reached from `sources`. False when the cone is too large.
  bool ends_ahead(const std::vector<uint32_t>& sources, std::unordered_set<uint32_t>& found) const {
    std::vector<uint32_t>        work(sources.begin(), sources.end());
    std::unordered_set<uint32_t> seen(sources.begin(), sources.end());
    while (!work.empty()) {
      if (seen.size() > cone_limit) {
        return false;
      }
      const auto pin = work.back();
      work.pop_back();
      const auto visit = [&](uint32_t next) {
        if (seen.insert(next).second) {
          work.push_back(next);
        }
      };
      const auto net = netlist_.net_of(pin);
      if (net != no_index && netlist_.is_driver(pin)) {
        for (const auto sink : netlist_.sinks_of(net)) {
          visit(sink);
        }
        continue;
      }
      const auto cell  = netlist_.cell_of(pin);
      const auto type  = netlist_.type_of(cell);
      const auto local = netlist_.local_of(pin);
      if (netlist_.untimed(pin)) {
        continue;
      }
      if (checked(pin)) {
        found.insert(pin);
      }
      for (const auto& arc : types_.arcs(type)) {
        if (arc.from == local && (arc.kind == Arc_kind::combinational || arc.kind == Arc_kind::launch_rise)) {
          visit(netlist_.pin(cell, arc.to));
        }
      }
    }
    return true;
  }

  void exceptions() {
    const size_t n = sdc_.clocks.size();
    for (const auto& e : sdc_.false_paths) {
      std::vector<uint32_t> through;
      for (const auto& object : e.through) {
        if (object.kind == Kind::cell) {
          const auto cell = names_ == nullptr ? no_index : names_->cell(object.name);
          if (cell == no_index) {
            missing(object, "set_false_path -through");
            continue;
          }
          for (uint32_t local = 0; local < types_.type(netlist_.type_of(cell)).pin_count; ++local) {
            if (types_.pin(netlist_.type_of(cell), local).direction == Direction::output) {
              through.push_back(netlist_.pin(cell, local));
            }
          }
        } else if (const auto pin = pin_of(object); pin != no_index) {
          through.push_back(pin);
        } else {
          missing(object, "set_false_path -through");
        }
      }
      const bool through_given = !e.through.empty();
      size_t     from_through  = through.size();
      const auto from          = starts(e.from, through);
      from_through             = through.size() - from_through;  // -from pins that are not start points
      const auto to            = ends(e.to);
      // A list that names nothing in this netlist is an exception the netlist does not need; it must not turn
      // into an exception with fewer conditions.
      if ((from.given && from.empty() && from_through == 0) || (to.given && to.empty()) || (through_given && through.size() == from_through)) {
        continue;
      }
      if (!e.setup || !e.hold) {
        if (!from.pins.empty() || !to.pins.empty() || !through.empty()) {
          note("set_false_path -setup or -hold on ports or pins (only between clocks)");
          continue;
        }
      }
      const auto block = [&](size_t launch, size_t capture) {
        if (e.setup) {
          setup_[launch * n + capture] = inf;
        }
        if (e.hold) {
          hold_[launch * n + capture] = inf;
        }
      };
      if (!through.empty()) {
        if ((from.given && !from.empty()) || to.given) {
          note("set_false_path -through together with -from or -to (not modelled: the paths stay timed)");
          continue;
        }
        out_.cut_pins.insert(out_.cut_pins.end(), through.begin(), through.end());
        continue;
      }
      if (!to.given) {
        for (const auto l : from.clocks) {
          for (size_t c = 0; c < n; ++c) {
            block(l, c);
          }
        }
        out_.cut_pins.insert(out_.cut_pins.end(), from.pins.begin(), from.pins.end());
        continue;
      }
      if (!from.given) {
        for (const auto c : to.clocks) {
          for (size_t l = 0; l < n; ++l) {
            block(l, c);
          }
          merge(any_to_, c, inf);
        }
        out_.excluded_endpoints.insert(out_.excluded_endpoints.end(), to.pins.begin(), to.pins.end());
        continue;
      }
      for (const auto l : from.clocks) {
        for (const auto c : to.clocks) {
          block(l, c);
        }
        for (const auto pin : to.pins) {
          merge(clock_relations_[l], pin, inf);
        }
      }
      if (from.pins.empty()) {
        continue;
      }
      // From some start points to some end points. When nothing else reaches those end points, or those start
      // points reach nothing else, the exception is a plain exclusion or a plain cut; otherwise the start points
      // launch in a group of their own.
      if (to.clocks.empty()) {
        std::unordered_set<uint32_t> behind, ahead;
        if (starts_behind(to.pins, behind) && std::ranges::all_of(behind, [&](uint32_t s) { return std::ranges::find(from.pins, s) != from.pins.end(); })) {
          out_.excluded_endpoints.insert(out_.excluded_endpoints.end(), to.pins.begin(), to.pins.end());
          continue;
        }
        if (ends_ahead(from.pins, ahead) && std::ranges::all_of(ahead, [&](uint32_t t) { return std::ranges::find(to.pins, t) != to.pins.end(); })) {
          out_.cut_pins.insert(out_.cut_pins.end(), from.pins.begin(), from.pins.end());
          continue;
        }
      }
      for (const auto pin : from.pins) {
        if (netlist_.type_of(netlist_.cell_of(pin)) != Cell_types::input_port_type) {
          note("set_false_path from a register to some of the endpoints it reaches (not modelled: the paths stay timed)");
          continue;
        }
        for (const auto end : to.pins) {
          merge(start_relations_[pin], end, inf);
        }
        for (const auto c : to.clocks) {
          merge(start_clocks_[pin], c, inf);
        }
        start_relations_[pin];
      }
    }
  }

  // set_max_delay: the paths it names may take that long from their launch edge. Between clocks it is the
  // relation of the two; from or to points it is a relation of those endpoints, for every group or for the
  // group of the start points. An output port without an output delay is an endpoint of such a path too.
  void path_delays() {
    const size_t n = sdc_.clocks.size();
    command_       = "set_max_delay";
    delayed_.assign(n * n, 0);
    const auto between = [&](size_t launch, size_t capture, float v) {
      auto& relation = setup_[launch * n + capture];
      if (relation != inf) {
        relation                        = delayed_[launch * n + capture] ? std::min(relation, v) : v;
        delayed_[launch * n + capture] = 1;
      }
    };
    const auto relate = [&](Relations& into, uint32_t pin, float v) {
      merge(into, pin, v);
      if (netlist_.type_of(netlist_.cell_of(pin)) == Cell_types::output_port_type) {
        delay_ports_.insert(pin);
      }
    };
    const auto is_port = [&](uint32_t pin) { return netlist_.type_of(netlist_.cell_of(pin)) == Cell_types::input_port_type; };
    // The output ports no clock checks: reached by a delay that names no end point.
    std::optional<std::vector<uint32_t>> open_outputs;
    const auto                           unclocked = [&]() -> const std::vector<uint32_t>& {
      if (!open_outputs) {
        open_outputs.emplace();
        for (const auto& port : netlist_.ports()) {
          if (const auto it = outs_.find(port.name); !port.input && (it == outs_.end() || !it->second.delay)) {
            open_outputs->push_back(netlist_.pin(port.cell, 0u));
          }
        }
      }
      return *open_outputs;
    };
    for (const auto& d : sdc_.max_delays) {
      std::vector<uint32_t> through;
      const auto            from = starts(d.from, through);
      const auto            to   = ends(d.to);
      const float           v    = d.delay_ps;
      if (!through.empty()) {
        note("set_max_delay -from a pin that is not a start point (not modelled: the paths keep their clock relation)");
        continue;
      }
      if ((from.given && from.empty()) || (to.given && to.empty())) {
        continue;  // names nothing in this netlist
      }
      if (!from.given && !to.given) {
        note("set_max_delay without -from or -to (not modelled)");
        continue;
      }
      bool registers = false;
      if (!to.given) {
        for (const auto l : from.clocks) {
          for (size_t c = 0; c < n; ++c) {
            between(l, c, v);
          }
          for (const auto out : unclocked()) {
            relate(clock_relations_[l], out, v);
          }
        }
        for (const auto pin : from.pins) {
          if (!is_port(pin)) {
            registers = true;
            continue;
          }
          merge(start_all_, pin, v);
          for (const auto out : unclocked()) {
            relate(start_relations_[pin], out, v);
          }
        }
      } else if (!from.given) {
        for (const auto c : to.clocks) {
          for (size_t l = 0; l < n; ++l) {
            between(l, c, v);
          }
          merge(any_to_, c, v);
        }
        for (const auto pin : to.pins) {
          relate(end_all_, pin, v);
        }
      } else {
        for (const auto l : from.clocks) {
          for (const auto c : to.clocks) {
            between(l, c, v);
          }
          for (const auto pin : to.pins) {
            relate(clock_relations_[l], pin, v);
          }
        }
        // From some start points to some end points: when nothing else reaches those end points the delay is
        // theirs for every group; otherwise the start points launch in a group of their own.
        std::unordered_set<uint32_t> behind;
        if (!from.pins.empty() && to.clocks.empty() && starts_behind(to.pins, behind)
            && std::ranges::all_of(behind, [&](uint32_t s) { return std::ranges::find(from.pins, s) != from.pins.end(); })) {
          for (const auto pin : to.pins) {
            relate(end_all_, pin, v);
          }
          continue;
        }
        for (const auto pin : from.pins) {
          if (!is_port(pin)) {
            registers = true;
            continue;
          }
          for (const auto end : to.pins) {
            relate(start_relations_[pin], end, v);
          }
          for (const auto c : to.clocks) {
            merge(start_clocks_[pin], c, v);
          }
          start_relations_[pin];
        }
      }
      if (registers) {
        note("set_max_delay from a register to some of the endpoints it reaches (not modelled: the paths keep their clock relation)");
      }
    }
    command_ = "set_false_path";
  }

  void groups() {
    const size_t n = sdc_.clocks.size();
    std::map<Launch, uint32_t> ids;
    std::vector<Launch>        launches;
    bool                       overflow = false;
    const auto                 id_of    = [&](Launch launch, uint32_t fallback) {
      if (const auto it = ids.find(launch); it != ids.end()) {
        return it->second;
      }
      if (launches.size() >= 32) {
        overflow = true;
        return fallback;
      }
      const auto id = static_cast<uint32_t>(launches.size());
      ids.emplace(launch, id);
      launches.push_back(std::move(launch));
      return id;
    };
    const auto of_clock = [&](size_t l) {
      Launch launch;
      launch.setup.assign(setup_.begin() + static_cast<long>(l * n), setup_.begin() + static_cast<long>((l + 1) * n));
      launch.hold.assign(hold_.begin() + static_cast<long>(l * n), hold_.begin() + static_cast<long>((l + 1) * n));
      if (const auto it = clock_relations_.find(static_cast<uint32_t>(l)); it != clock_relations_.end()) {
        launch.relations.assign(it->second.begin(), it->second.end());
      }
      return launch;
    };
    // A clock with a network launches the flip-flops it reaches. A virtual clock, or one made on an output port
    // (a forwarded clock), launches only the input ports whose delay names it: its group is made then.
    for (size_t l = 0; l < n; ++l) {
      const bool network = std::ranges::any_of(out_.clocks[l].source_pins, [&](uint32_t pin) {
        return netlist_.type_of(netlist_.cell_of(pin)) != Cell_types::output_port_type;
      });
      if (network) {
        out_.clocks[l].group = id_of(of_clock(l), 0);
      }
    }
    // A port without an input delay launches against every capture clock as that clock itself would.
    Launch any;
    for (size_t c = 0; c < n; ++c) {
      const auto given = any_to_.find(static_cast<uint32_t>(c));
      any.setup.push_back(given == any_to_.end() ? static_cast<float>(sdc_.clocks[c].period_ps) : given->second);
      any.hold.push_back(given != any_to_.end() && given->second == inf ? inf : 0.f);
    }

    // The input ports: the delays (the last one given for each analysis), transitions and driving cells.
    struct Input {
      std::optional<float> late, early, slew;
      std::string          clock;
      const Sdc::Drive*    drive = nullptr;
      bool                 two_clocks = false;
    };
    std::map<std::string, Input> inputs;
    for (const auto& d : sdc_.delays) {
      if (!d.input) {
        continue;
      }
      const auto port = ports_.find(d.port.name);
      if (port == ports_.end()) {
        missing(d.port, "set_input_delay");
        continue;
      }
      if (!port->second->input) {
        continue;  // [all_inputs] of another direction: nothing to launch
      }
      auto& in = inputs[d.port.name];
      if ((in.late || in.early) && in.clock != d.clock) {
        in.two_clocks = true;
      }
      in.clock = d.clock;
      if (d.max) {
        in.late = d.delay_ps;
      }
      if (d.min) {
        in.early = d.delay_ps;
      }
    }
    for (const auto& t : sdc_.transitions) {
      if (const auto port = ports_.find(t.port.name); port != ports_.end() && port->second->input) {
        inputs[t.port.name].slew = t.slew_ps;
      }
    }
    for (const auto& drive : sdc_.drives) {
      if (const auto port = ports_.find(drive.port.name); port != ports_.end() && port->second->input) {
        inputs[drive.port.name].drive = &drive;
      }
    }
    for (const auto& [pin, relations] : start_relations_) {
      (void)relations;
      inputs[netlist_.ports()[port_index(pin)].name];
    }
    for (const auto& [pin, delay] : start_all_) {
      (void)delay;
      inputs[netlist_.ports()[port_index(pin)].name];
    }
    for (const auto& [name, in] : inputs) {
      const auto pin = netlist_.pin(ports_.at(name)->cell, 0u);
      if (clock_sources_.contains(pin)) {
        continue;  // the clock itself
      }
      if (in.two_clocks) {
        note(std::format("set_input_delay: port '{}' has delays against more than one clock (the last one is used)", name));
      }
      Input_delay entry;
      entry.port  = name;
      entry.clock = in.clock;
      if (in.late || in.early) {
        entry.late           = in.late.has_value();
        entry.early          = in.early.has_value();
        entry.delay_ps       = in.late.value_or(in.early.value_or(0.f));
        entry.early_delay_ps = in.early.value_or(0.f);
        entry.early_set      = in.early.has_value() && in.late.has_value() && *in.early != *in.late;
      }
      entry.slew_ps = in.slew.value_or(out_.default_input_slew_ps);
      if (in.drive != nullptr) {
        const auto type = types_.find(in.drive->cell);
        uint32_t   local = no_index;
        if (type != no_index) {
          for (uint32_t i = 0; i < types_.type(type).pin_count && local == no_index; ++i) {
            const auto& pin_type = types_.pin(type, i);
            if (in.drive->pin.empty() ? pin_type.direction == Direction::output : pin_type.name == in.drive->pin) {
              local = i;
            }
          }
        }
        if (type == no_index) {
          note(std::format("set_driving_cell: cell '{}' is not in the library (the ports are driven ideally)", in.drive->cell));
        } else if (local == no_index) {
          note(std::format("set_driving_cell: cell '{}' has no pin '{}' (the ports are driven ideally)", in.drive->cell, in.drive->pin));
        } else {
          entry.drive_type = type;
          entry.drive_pin  = local;
          entry.slew_ps    = in.drive->input_slew_ps;
        }
      }
      // The launch group: the port's clock, less what its own exceptions remove.
      const auto clock = in.clock.empty() ? ((in.late || in.early) && n != 0 ? 0u : no_index) : clock_of(in.clock);
      Launch     launch = clock == no_index ? any : of_clock(clock);
      if (const auto it = start_all_.find(pin); it != start_all_.end()) {
        for (auto& relation : launch.setup) {
          relation = relation == inf ? inf : it->second;
        }
      }
      if (const auto it = start_clocks_.find(pin); it != start_clocks_.end()) {
        for (const auto& [c, v] : it->second) {
          if (v == inf) {
            launch.setup[c] = launch.hold[c] = inf;
          } else if (launch.setup[c] != inf) {
            launch.setup[c] = v;
          }
        }
      }
      if (const auto it = start_relations_.find(pin); it != start_relations_.end() && !it->second.empty()) {
        Relations merged(launch.relations.begin(), launch.relations.end());
        for (const auto& [end, v] : it->second) {
          merge(merged, end, v);
        }
        launch.relations.assign(merged.begin(), merged.end());
      }
      entry.group = id_of(std::move(launch), clock == no_index ? 0 : out_.clocks[clock].group);
      out_.inputs.push_back(std::move(entry));
    }
    // A clock without a network has the group its input ports made, if any.
    for (size_t l = 0; l < n; ++l) {
      if (const auto it = ids.find(of_clock(l)); it != ids.end()) {
        out_.clocks[l].group = it->second;
      }
    }
    // Only a port that has no entry needs the default group.
    size_t plain_inputs = 0;
    for (const auto& port : netlist_.ports()) {
      plain_inputs += port.input && !clock_sources_.contains(netlist_.pin(port.cell, 0u)) && !inputs.contains(port.name);
    }
    if (plain_inputs != 0) {
      out_.default_input_group = id_of(any, 0);
    }
    if (overflow) {
      note("more than 32 launch groups: the false paths of some start points are not modelled");
    }
    out_.groups = static_cast<uint32_t>(std::max<size_t>(1, launches.size()));
    if (n == 0) {
      out_.default_input_group = 0;
      return;
    }
    out_.setup_relation_ps.assign(out_.groups * n, 0.f);
    out_.hold_relation_ps.assign(out_.groups * n, 0.f);
    for (size_t g = 0; g < launches.size(); ++g) {
      std::copy(launches[g].setup.begin(), launches[g].setup.end(), out_.setup_relation_ps.begin() + static_cast<long>(g * n));
      std::copy(launches[g].hold.begin(), launches[g].hold.end(), out_.hold_relation_ps.begin() + static_cast<long>(g * n));
      for (const auto& [pin, v] : launches[g].relations) {
        out_.endpoint_relations.push_back({pin, static_cast<uint32_t>(g), v});
      }
    }
    for (const auto& [pin, v] : end_all_) {
      out_.endpoint_relations.push_back({pin, no_index, v});
    }
  }

  [[nodiscard]] size_t port_index(uint32_t pin) const {
    const auto cell = netlist_.cell_of(pin);
    for (size_t i = 0; i < netlist_.ports().size(); ++i) {
      if (netlist_.ports()[i].cell == cell) {
        return i;
      }
    }
    return 0;
  }

  void outputs() {
    bool min_only = false;
    for (const auto& d : sdc_.delays) {
      if (d.input) {
        continue;
      }
      const auto port = ports_.find(d.port.name);
      if (port == ports_.end()) {
        missing(d.port, "set_output_delay");
        continue;
      }
      if (port->second->input) {
        continue;
      }
      if (!d.max) {
        min_only = true;
        continue;
      }
      auto& o = outs_[d.port.name];
      o.delay = d.delay_ps;
      o.clock = d.clock;
    }
    if (min_only) {
      note("set_output_delay -min (hold is not checked at output ports)");
    }
    for (const auto& l : sdc_.loads) {
      const auto port = ports_.find(l.port.name);
      if (port == ports_.end()) {
        missing(l.port, "set_load");
      } else if (port->second->input) {
        input_load_ = true;
      } else {
        outs_[l.port.name].load = l.load_ff;
      }
    }
    if (input_load_) {
      note("set_load on an input port (not modelled)");
    }
  }

  void emit_outputs() {
    // An output port a set_max_delay reaches is an endpoint without an output delay too.
    for (const auto pin : delay_ports_) {
      outs_[netlist_.ports()[port_index(pin)].name].path_delay = true;
    }
    for (const auto& [name, o] : outs_) {
      Output_delay entry{name, o.clock, o.delay.value_or(0.f), o.load.value_or(0.f), o.delay.has_value() || o.path_delay, o.delay.has_value()};
      out_.outputs.push_back(std::move(entry));
    }
  }

  static constexpr size_t cone_limit = 200'000;

  const Sdc&                                               sdc_;
  const Netlist&                                           netlist_;
  const Cell_types&                                        types_;
  const Design_names*                                      names_;
  Constraints                                              out_;
  std::set<std::string>                                    noted_;
  std::unordered_map<std::string_view, const Port*>        ports_;
  std::unordered_set<uint32_t>                             clock_sources_;
  std::vector<float>                                       setup_, hold_;  // [launch clock][capture clock]
  std::vector<uint8_t>                                     delayed_;       // a relation of setup_ a set_max_delay gave
  Relations                                                any_to_;        // capture clock: the relation for every start
  // Endpoints with a relation of their own: for a launch clock, for an input port, for every start.
  std::map<uint32_t, Relations>                            clock_relations_, start_relations_;
  Relations                                                end_all_;
  std::map<uint32_t, Relations>                            start_clocks_;  // input port: capture clock: relation
  Relations                                                start_all_;     // input port: its delay to every endpoint
  struct Output {
    std::optional<float> delay, load;
    std::string          clock;
    bool                 path_delay = false;
  };
  std::map<std::string, Output>                            outs_;
  std::set<uint32_t>                                       delay_ports_;  // output ports a set_max_delay reaches
  std::string                                              command_ = "set_false_path";
  bool                                                     input_load_ = false;
};

}  // namespace

void Netlist_design::ports(const std::function<void(std::string_view, bool)>& visit) const {
  for (const auto& port : netlist_.ports()) {
    visit(port.name, port.input);
  }
}

void Netlist_design::pins(const std::function<void(std::string_view, std::string_view)>& visit) const {
  if (names_ == nullptr) {
    return;
  }
  const auto& types = netlist_.types();
  names_->cells([&](std::string_view name, uint32_t cell) {
    const auto type = netlist_.type_of(cell);
    for (uint32_t local = 0; local < types.type(type).pin_count; ++local) {
      visit(name, types.pin(type, local).name);
    }
  });
}

bool Netlist_design::has_pin(std::string_view instance, std::string_view pin) const {
  if (names_ == nullptr) {
    return false;
  }
  const auto cell = names_->cell(instance);
  return cell != no_index && netlist_.pin(cell, pin) != no_index;
}

void Netlist_design::nets(const std::function<void(std::string_view)>& visit) const {
  for (const auto& port : netlist_.ports()) {
    visit(port.name);
  }
  if (names_ != nullptr) {
    names_->nets([&](std::string_view name, uint32_t) { visit(name); });
  }
}

bool Netlist_design::has_net(std::string_view name) const {
  for (const auto& port : netlist_.ports()) {
    if (port.name == name) {
      return true;
    }
  }
  return names_ != nullptr && names_->net_driver(name) != no_index;
}

void Netlist_design::cells(const std::function<void(std::string_view)>& visit) const {
  if (names_ != nullptr) {
    names_->cells([&](std::string_view name, uint32_t) { visit(name); });
  }
}

bool Netlist_design::has_cell(std::string_view name) const { return names_ != nullptr && names_->cell(name) != no_index; }

Constraints resolve(const Sdc& sdc, const Netlist& netlist, const Design_names* names) {
  if (sdc.clocks.empty() && !sdc.max_delays.empty()) {
    // A block constrained by set_max_delay alone has no clock to report its endpoints under: a virtual one,
    // named after the command, with the longest of the delays as its period.
    Sdc        with = sdc;
    Sdc::Clock clock;
    clock.name = "set_max_delay";
    for (const auto& d : sdc.max_delays) {
      clock.period_ps = std::max<double>(clock.period_ps, d.delay_ps);
    }
    clock.period_ps = clock.period_ps > 0 ? clock.period_ps : 1.0;
    clock.fall_ps   = clock.period_ps / 2;
    with.clocks.push_back(std::move(clock));
    return Resolver(with, netlist, names).run();
  }
  return Resolver(sdc, netlist, names).run();
}

}  // namespace livehd::timing::fast
