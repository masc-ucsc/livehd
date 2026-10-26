// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "route_store.hpp"

#include <stdexcept>

#include "graph/attrs.hpp"

namespace livehd::physical {
namespace {
using Records = attrs::route_records_v1_t::value_type;
using Strings = attrs::route_strings_v1_t::value_type;
enum Kind : int64_t { header, connection, wire, path, point, via, rect, rule };
void require(bool condition) {
  if (!condition) {
    throw std::invalid_argument("invalid persistent signal route");
  }
}
struct Payload {
  Records                        records;
  Strings                        strings;
  std::map<std::string, int64_t> ids;
  int64_t                        intern(const std::string& text) {
    const auto [it, inserted] = ids.emplace(text, strings.size());
    if (inserted) {
      strings.push_back(text);
    }
    return it->second;
  }
};
void validate(const Net& net) {
  require(!net.name.empty() && net.connections.size() >= 2 && !net.wires.empty());
  for (const auto& w : net.wires) {
    require(w.type == "FIXED" && !w.paths.empty());
    for (const auto& p : w.paths) {
      require(!p.layer.empty() && !p.steps.empty() && std::holds_alternative<Route_point>(p.steps.front()));
      require(!p.width || *p.width > 0);
    }
  }
}
}  // namespace
void clear_route(const hhds::Pin_class& driver) {
  require(driver.is_driver());
  driver.attr(attrs::route_records_v1).del();
  driver.attr(attrs::route_strings_v1).del();
}
void store_route(const hhds::Pin_class& driver, int64_t dbu, const Net& net, const Route_width_rules& rules,
                 const std::string& context) {
  require(driver.is_driver() && dbu > 0 && !context.empty());
  validate(net);
  Payload data;
  data.records.push_back({header, dbu, data.intern(context), data.intern(net.name), data.intern(net.use), 1});
  for (const auto& c : net.connections) {
    data.records.push_back({connection, data.intern(c.instance), data.intern(c.pin)});
  }
  for (const auto& w : net.wires) {
    data.records.push_back({wire, data.intern(w.type)});
    for (const auto& p : w.paths) {
      data.records.push_back(
          {path, data.intern(p.layer), p.width.has_value(), p.width.value_or(0), data.intern(p.shape), data.intern(p.taper_rule)});
      for (const auto& step : p.steps) {
        if (const auto* v = std::get_if<Route_point>(&step)) {
          require(!v->extension || *v->extension >= 0);
          data.records.push_back({point, v->x, v->y, v->extension.has_value(), v->extension.value_or(0)});
        } else if (const auto* vcut = std::get_if<Route_via>(&step)) {
          require(static_cast<int64_t>(vcut->orientation) <= 7);
          data.records.push_back({via, data.intern(vcut->name), static_cast<int64_t>(vcut->orientation)});
        } else {
          const auto& patch = std::get<Route_rect>(step);
          data.records.push_back({rect, patch.xl, patch.yl, patch.xh, patch.yh});
        }
      }
    }
  }
  for (const auto& [name, widths] : rules) {
    require(!name.empty() && !widths.empty());
    for (const auto& [layer, width] : widths) {
      require(!layer.empty() && width > 0);
      data.records.push_back({rule, data.intern(name), data.intern(layer), width});
    }
  }
  // All ordinary validation failures occur before touching either attribute.
  // Clear old records first so allocation failure cannot expose mixed generations.
  driver.attr(attrs::route_records_v1).del();
  driver.attr(attrs::route_strings_v1).set(std::move(data.strings));
  driver.attr(attrs::route_records_v1).set(std::move(data.records));
}
std::optional<Stored_route> load_route(const hhds::Pin_class& driver, int64_t dbu, const Net& current, const std::string& context) {
  require(driver.is_driver() && dbu > 0 && !context.empty());
  const auto records_attr = driver.attr(attrs::route_records_v1);
  if (!records_attr.has()) {
    return {};
  }
  const auto strings_attr = driver.attr(attrs::route_strings_v1);
  require(strings_attr.has());
  const auto& records = records_attr.get();
  const auto& strings = strings_attr.get();
  const auto  text    = [&](int64_t id) -> const std::string& {
    require(id >= 0 && static_cast<uint64_t>(id) < strings.size());
    return strings[id];
  };
  require(!records.empty() && records[0][0] == header && records[0][5] == 1);
  const auto& h = records[0];
  if (h[1] != dbu || text(h[2]) != context) {
    return {};
  }
  Stored_route result;
  result.net.name = text(h[3]);
  result.net.use  = text(h[4]);
  for (size_t i = 1; i < records.size(); ++i) {
    const auto& r = records[i];
    switch (r[0]) {
      case connection:
        require(result.net.wires.empty());
        result.net.connections.push_back({text(r[1]), text(r[2])});
        break;
      case wire: result.net.wires.push_back({text(r[1]), {}}); break;
      case path:
        require(!result.net.wires.empty() && (r[2] == 0 || r[2] == 1));
        result.net.wires.back().paths.push_back(
            {text(r[1]), r[2] ? std::optional<int64_t>(r[3]) : std::nullopt, text(r[4]), {}, text(r[5])});
        break;
      case rule:
        require(r[3] > 0 && !text(r[1]).empty() && !text(r[2]).empty());
        require(result.rules[text(r[1])].emplace(text(r[2]), r[3]).second);
        break;
      default: {
        require(!result.net.wires.empty() && !result.net.wires.back().paths.empty());
        auto& steps = result.net.wires.back().paths.back().steps;
        if (r[0] == point) {
          require((r[3] == 0 || r[3] == 1) && (!r[3] || r[4] >= 0));
          steps.push_back(Route_point{r[1], r[2], r[3] ? std::optional<int64_t>(r[4]) : std::nullopt});
        } else if (r[0] == via) {
          require(r[2] >= 0 && r[2] <= 7);
          steps.push_back(Route_via{text(r[1]), static_cast<Orientation>(r[2])});
        } else if (r[0] == rect) {
          steps.push_back(Route_rect{r[1], r[2], r[3], r[4]});
        } else {
          require(false);
        }
        break;
      }
    }
  }
  validate(result.net);
  if (result.net.name != current.name || result.net.connections != current.connections || result.net.use != current.use) {
    return {};
  }
  return result;
}
}  // namespace livehd::physical
