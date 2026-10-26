// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "tree_search.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "budget.hpp"
#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
using detail::finite;
double distance(Point a, Point b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
using Children = std::vector<std::vector<size_t>>;
Children validate(const Context& context, const Routing_tree& tree) {
  const auto n = tree.vertices.size();
  require(n && n <= std::numeric_limits<unsigned>::max() / 4, "invalid/beyond-index-limit routing tree");
  require(!context.sinks.empty() && tree.vertices[0].position == context.driver.output,
          "routing tree root differs from driver or has no sinks");
  require(tree.segments.size() == n - 1, "routing tree must have n-1 segments");
  for (auto value : tree.attachment_rc) {
    require(std::isfinite(value) && value >= 0, "invalid attachment wire RC");
  }
  Children                      children(n);
  std::vector<size_t>           parents(n), sinks(context.sinks.size());
  std::map<std::string, size_t> names;
  for (size_t i = 0; i < n; ++i) {
    const auto& vertex = tree.vertices[i];
    require(finite(vertex.position), "nonfinite routing vertex");
    for (auto s : vertex.sinks) {
      require(s < context.sinks.size() && vertex.position == context.sinks[s].position, "invalid or misplaced routing-tree sink");
      ++sinks[s];
    }
    for (const auto& option : vertex.options) {
      const auto& b                = option.buffer;
      const auto [found, inserted] = names.emplace(b.name, i);
      require(!b.name.empty() && (inserted || found->second == i) && b.name != context.driver.name,
              "routing-tree buffer identity is shared by different vertices");
      require(finite(b.input) && finite(b.output) && finite(Point{b.body.xl, b.body.yl}) && finite(Point{b.body.xh, b.body.yh})
                  && b.body.xl < b.body.xh && b.body.yl < b.body.yh && b.input.x >= b.body.xl && b.input.x <= b.body.xh
                  && b.input.y >= b.body.yl && b.input.y <= b.body.yh && b.output.x >= b.body.xl && b.output.x <= b.body.xh
                  && b.output.y >= b.body.yl && b.output.y <= b.body.yh,
              "invalid tree buffer footprint/pins");
    }
  }
  for (size_t i = 0; i < tree.segments.size(); ++i) {
    const auto& e = tree.segments[i];
    require(e.parent < n && e.child < n && e.child && e.parent != e.child, "invalid routing-tree edge");
    require(++parents[e.child] == 1, "routing tree has multiple parents");
    require(std::isfinite(e.resistance_ohm) && e.resistance_ohm >= 0 && std::isfinite(e.capacitance_ff) && e.capacitance_ff >= 0
                && std::isfinite(e.length_um) && e.length_um >= 0,
            "invalid routing segment RC/length");
    const auto length = distance(tree.vertices[e.parent].position, tree.vertices[e.child].position);
    require(std::isfinite(length) && e.length_um + 1e-9 * std::max(1., length) >= length,
            "routing segment is shorter than its geometry");
    children[e.parent].push_back(i);
  }
  std::vector<size_t> order{0};
  for (size_t i = 0; i < order.size(); ++i) {
    const auto at = order[i];
    require(!children[at].empty() || !tree.vertices[at].sinks.empty(), "unloaded routing-tree leaf");
    for (auto e : children[at]) {
      order.push_back(tree.segments[e].child);
    }
    require(order.size() <= n, "cycle in routing tree");
  }
  require(order.size() == n && std::all_of(sinks.begin(), sinks.end(), [](auto count) { return count == 1; }),
          "disconnected routing tree or missing/duplicated sinks");
  return children;
}
size_t node(Wire_tree& tree, Point position, std::string pin) {
  const auto at = tree.rc.nodes.size();
  require(at < std::numeric_limits<unsigned>::max(), "expanded routing tree node-index overflow");
  tree.rc.nodes.push_back({std::move(pin), 0});
  tree.positions.push_back(position);
  return at;
}
size_t wire(Wire_tree& tree, size_t from, Point to, std::string pin, double r, double c, double length, bool allowed) {
  const auto at                       = node(tree, to, std::move(pin));
  tree.rc.nodes[from].capacitance_ff += c * .5;
  tree.rc.nodes[at].capacitance_ff   += c * .5;
  tree.rc.edges.push_back({unsigned(from), unsigned(at), r});
  tree.rc.wire_length_um += length;
  tree.routing_allowed    = tree.routing_allowed && allowed;
  return at;
}
size_t attach(Wire_tree& tree, size_t from, Point to, std::string pin, const Routing_tree& route, bool allowed) {
  const auto a  = tree.positions[from];
  const auto dx = std::abs(a.x - to.x), dy = std::abs(a.y - to.y);
  // Separate directional pi segments retain their actual RC distribution.
  auto       bend = from;
  if (dx && dy) {
    bend = wire(tree, from, {to.x, a.y}, "", dx * route.attachment_rc[0], dx * route.attachment_rc[1], dx, allowed);
  }
  const double r = (bend == from ? dx * route.attachment_rc[0] : 0) + dy * route.attachment_rc[2];
  const double c = (bend == from ? dx * route.attachment_rc[1] : 0) + dy * route.attachment_rc[3];
  return wire(tree, bend, to, std::move(pin), r, c, bend == from ? dx + dy : dy, allowed);
}
Candidate expand(const Context& c, const Routing_tree& route, const Children& children, const std::vector<size_t>& choices) {
  require(choices.size() == route.vertices.size(), "tree choice count differs from vertex count");
  for (size_t i = 0; i < choices.size(); ++i) {
    require(choices[i] <= route.vertices[i].options.size(), "invalid tree buffer choice");
  }
  Candidate  result;
  const auto stage = [&](const std::string& pin, Point position) {
    Wire_tree tree;
    tree.driver_stage = result.trees.size();
    tree.rc.driver    = pin;
    node(tree, position, pin);
    result.trees.push_back(std::move(tree));
    return result.trees.size() - 1;
  };
  stage(c.driver.name + ":" + c.driver.model.output, c.driver.output);
  struct Work {
    size_t vertex, stage, node;
  };
  std::vector<Work> work{
      {0, 0, 0}
  };
  for (size_t at = 0; at < work.size(); ++at) {
    const auto  item          = work[at];
    const auto& vertex        = route.vertices[item.vertex];
    auto        current_stage = item.stage, current_node = item.node;
    if (choices[item.vertex]) {
      const auto& option = vertex.options[choices[item.vertex] - 1];
      const auto& b      = option.buffer;
      const auto  index  = result.buffers.size();
      const auto  input  = attach(result.trees[current_stage],
                                current_node,
                                b.input,
                                b.name + ":" + b.model.input,
                                route,
                                option.attachment_routing_allowed);
      result.trees[current_stage].terminals.push_back({input, true, index});
      result.buffers.push_back(b);
      current_stage = stage(b.name + ":" + b.model.output, b.output);
      current_node  = attach(result.trees[current_stage], 0, vertex.position, "", route, option.attachment_routing_allowed);
    }
    for (auto s : vertex.sinks) {
      const auto terminal = wire(result.trees[current_stage], current_node, c.sinks[s].position, c.sinks[s].name, 0, 0, 0, true);
      result.trees[current_stage].terminals.push_back({terminal, false, s});
    }
    for (auto index : children[item.vertex]) {
      const auto& e     = route.segments[index];
      const auto  child = wire(result.trees[current_stage],
                              current_node,
                              route.vertices[e.child].position,
                              "",
                              e.resistance_ohm,
                              e.capacitance_ff,
                              e.length_um,
                              e.routing_allowed);
      work.push_back({e.child, current_stage, child});
    }
  }
  return result;
}
bool subset(const std::vector<Site_span>& a, const std::vector<Site_span>& b) {
  for (const auto& s : a) {
    auto cursor = s.first;
    for (const auto& t : b) {
      if (s.row != t.row || t.first > cursor || t.first + t.count <= cursor) {
        continue;
      }
      cursor = t.first + t.count;
      if (cursor >= s.first + s.count) {
        break;
      }
    }
    if (cursor < s.first + s.count) {
      return false;
    }
  }
  return true;
}
}  // namespace

bool dominates_tree_solution(const Tree_solution& a, const Tree_solution& b, const Context& c) {
  const auto& x = a.timing;
  const auto& y = b.timing;
  if (x.area_um2 > y.area_um2 || x.wire_length_um > y.wire_length_um || x.multiplier_cost > y.multiplier_cost
      || !subset(a.sites, b.sites)) {
    return false;
  }
  for (unsigned t = 0; t < 2; ++t) {
    if (x.root_required_ps[t].has_value() != y.root_required_ps[t].has_value()) {
      return false;
    }
    if (x.root_required_ps[t] && *x.root_required_ps[t] < *y.root_required_ps[t]) {
      return false;
    }
  }
  for (const auto& sink : c.sinks) {
    const auto& xs = x.pins.at(sink.name);
    const auto& ys = y.pins.at(sink.name);
    for (unsigned t = 0; t < 2; ++t) {
      if (xs.arrival_ps[t] > ys.arrival_ps[t] || xs.slew_ps[t] > ys.slew_ps[t]) {
        return false;
      }
    }
  }
  for (const auto& [resource, units] : x.demand) {
    const auto found = y.demand.find(resource);
    if (found == y.demand.end() || units > found->second) {
      return false;
    }
  }
  return true;
}
bool prefer_tree_solution(const Tree_solution& a, const Tree_solution& b, Tree_selection selection) {
  const auto& x = a.timing;
  const auto& y = b.timing;
  if (selection == Tree_selection::critical) {
    if (a.worst_input_margin_ps != b.worst_input_margin_ps) {
      return a.worst_input_margin_ps.value_or(0) > b.worst_input_margin_ps.value_or(0);
    }
    if (x.multiplier_cost != y.multiplier_cost) {
      return x.multiplier_cost < y.multiplier_cost;
    }
  }
  if (x.area_um2 != y.area_um2) {
    return x.area_um2 < y.area_um2;
  }
  if (x.wire_length_um != y.wire_length_um) {
    return x.wire_length_um < y.wire_length_um;
  }
  if (x.multiplier_cost != y.multiplier_cost) {
    return x.multiplier_cost < y.multiplier_cost;
  }
  return a.choices < b.choices;
}
bool replaces_best_tree_solution(const Tree_solution& candidate, const Tree_solution& best, const Context& c,
                                 Tree_selection selection) {
  const bool old_dominates = dominates_tree_solution(best, candidate, c);
  const bool new_dominates = dominates_tree_solution(candidate, best, c);
  return (new_dominates && !old_dominates)
         || (!(old_dominates && !new_dominates) && prefer_tree_solution(candidate, best, selection));
}

Rect micron_rect(const Site_rect& r, double dbu_per_um) {
  return {double(r.xl) / dbu_per_um, double(r.yl) / dbu_per_um, double(r.xh) / dbu_per_um, double(r.yh) / dbu_per_um};
}
bool body_within_sites(const Rect& b, const Site_rect& sites, double dbu_per_um) {
  constexpr auto limit = detail::exact_dbu_limit;
  require(sites.xl >= -limit && sites.xh <= limit && sites.yl >= -limit && sites.yh <= limit,
          "site footprint exceeds exact double DBU range");
  const auto s = micron_rect(sites, dbu_per_um);
  return b.xl >= s.xl && b.xh <= s.xh && b.yl >= s.yl && b.yh <= s.yh;
}
std::optional<double> worst_input_margin(const Evaluation& timing, const Driver& driver) {
  std::optional<double> worst;
  for (unsigned t = 0; t < 2; ++t) {
    if (timing.root_required_ps[t]) {
      const double margin = *timing.root_required_ps[t] - driver.input_arrival_ps[t];
      worst               = std::min(worst.value_or(margin), margin);
    }
  }
  return worst;
}

Candidate expand_tree(const Context& c, const Routing_tree& route, const std::vector<size_t>& choices) {
  return expand(c, route, validate(c, route), choices);
}

Tree_search_result search_tree(const liberty::timing::Library& lib, const Context& c, const Routing_tree& route,
                               const Site_reservations& reservations, const std::string& owner,
                               const Tree_search_settings& settings) {
  require(!owner.empty() && settings.max_frontier && std::isfinite(settings.dbu_per_um) && settings.dbu_per_um > 0
              && std::isfinite(settings.max_seconds) && settings.max_seconds > 0,
          "invalid tree-search settings/owner");
  require(settings.selection == Tree_selection::critical || settings.selection == Tree_selection::outside_window,
          "invalid tree-selection policy");
  Tree_search_result result;
  if (route.vertices.size() > settings.max_topology_vertices) {
    result.status = Tree_search_status::budget_exhausted;
    return result;
  }
  const auto            start    = detail::Clock::now();
  const auto            children = validate(c, route);
  std::vector<Site_row> rows;
  for (const auto& [name, row] : reservations.geometry()) {
    rows.push_back(row.row);
  }
  const auto full = build_legal_sites(rows, {});
  for (const auto& vertex : route.vertices) {
    for (const auto& option : vertex.options) {
      require(body_within_sites(option.buffer.body, site_body(full, option.site), settings.dbu_per_um),
              "buffer footprint exceeds its reserved site span");
    }
  }
  std::vector<size_t> choices(route.vertices.size());
  for (;;) {
    if (result.assignments >= settings.max_assignments || detail::elapsed(start) >= settings.max_seconds) {
      result.status = Tree_search_status::budget_exhausted;
      return result;
    }
    ++result.assignments;
    std::vector<Site_span> spans;
    for (size_t i = 0; i < choices.size(); ++i) {
      if (choices[i]) {
        spans.push_back(route.vertices[i].options[choices[i] - 1].site);
      }
    }
    std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) {
      return a.row == b.row ? a.first < b.first : a.row < b.row;
    });
    bool overlap = false;
    for (size_t i = 1; i < spans.size(); ++i) {
      overlap = overlap || spans_overlap(spans[i - 1], spans[i]);
    }
    if (overlap || reservations.check(owner, spans).status != Reservation_status::reserved) {
      ++result.site_rejected;
    } else {
      auto candidate             = expand(c, route, children, choices);
      auto context               = c;
      context.max_waveform_fits  = std::min(context.max_waveform_fits, settings.max_waveform_fits - result.waveform_fits);
      auto timing                = evaluate(lib, context, candidate);
      result.waveform_fits      += timing.waveform_fits;
      if (timing.status == Status::budget_exhausted) {
        result.status = Tree_search_status::budget_exhausted;
        return result;
      }
      if (timing.status == Status::unsupported) {
        ++result.unsupported;
        if (result.unsupported_examples.size() < 8) {
          result.unsupported_examples.push_back(timing.reason);
        }
      } else {
        ++result.evaluated;
        if (!timing.electrically_feasible) {
          ++result.electrical_rejected;
        }
        if (!timing.resources_feasible) {
          ++result.resource_rejected;
        }
        const bool required = settings.selection == Tree_selection::outside_window && !timing.meets_required.value_or(true);
        if (required) {
          ++result.required_rejected;
        }
        if (timing.feasible() && !required) {
          const auto    margin = worst_input_margin(timing, c.driver);
          Tree_solution solution{choices, std::move(candidate), std::move(timing), std::move(spans), margin};
          if (!result.best || replaces_best_tree_solution(solution, *result.best, c, settings.selection)) {
            result.best = solution;
          }
          if (!admit_tree_frontier(result.frontier, std::move(solution), settings.max_frontier, c, std::identity{})) {
            result.status = Tree_search_status::budget_exhausted;
            return result;
          }
          result.peak_frontier = std::max(result.peak_frontier, result.frontier.size());
          if (settings.prefer_unbuffered_if_feasible && result.assignments == 1) {
            result.status = Tree_search_status::unbuffered_retained;
            return result;
          }
        }
      }
    }
    size_t digit = choices.size();
    while (digit) {
      --digit;
      if (choices[digit] < route.vertices[digit].options.size()) {
        ++choices[digit];
        break;
      }
      choices[digit] = 0;
    }
    if (!digit && choices[0] == 0) {
      result.assignment_space_exhausted = true;
      result.status                     = result.unsupported ? Tree_search_status::incomplete_models : Tree_search_status::complete;
      return result;
    }
  }
}
}  // namespace livehd::placement::buffering
