// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "producer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "budget.hpp"
#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
using detail::Clock;
using detail::elapsed;
Route_point lattice(Point p, double units) {
  const double x = p.x * units, y = p.y * units;
  require(std::isfinite(x) && std::isfinite(y) && std::abs(x) < double(int64_t(1) << 52) && std::abs(y) < double(int64_t(1) << 52),
          "producer terminal exceeds exact DBU range");
  return {std::llround(x), std::llround(y)};
}
// Add exact source/sink leaves outside the lattice topology. Zero-length
// attachments remain valid identity-preserving tree edges.
void terminals(Routing_tree& tree, const Context& c, const std::vector<Site_rect>& masks, double units) {
  tree.vertices.insert(tree.vertices.begin(), {c.driver.output, {}, {}});
  for (auto& e : tree.segments) {
    ++e.parent;
    ++e.child;
  }
  const auto append = [&](size_t parent, size_t child) {
    const auto   a = tree.vertices[parent].position, z = tree.vertices[child].position;
    const double dx = std::abs(a.x - z.x), dy = std::abs(a.y - z.y);
    tree.segments.push_back({parent,
                             child,
                             dx * tree.attachment_rc[0] + dy * tree.attachment_rc[2],
                             dx * tree.attachment_rc[1] + dy * tree.attachment_rc[3],
                             dx + dy,
                             rectilinear_clear(a, z, masks, units)});
  };
  append(0, 1);
  const auto size = tree.vertices.size();
  for (size_t v = 1; v < size; ++v) {
    auto sinks = std::move(tree.vertices[v].sinks);
    tree.vertices[v].sinks.clear();
    for (auto sink : sinks) {
      const auto child = tree.vertices.size();
      tree.vertices.push_back({c.sinks.at(sink).position, {sink}, {}});
      append(v, child);
    }
  }
}
}  // namespace
Producer_result generate_checkpoint(const Checkpoint_input& input, const liberty::timing::Library& library,
                                    const late::Driver_options& driver, const physical::Technology& tech,
                                    const physical::Floorplan& original, const std::vector<Site_rect>& extra_masks,
                                    const std::vector<Site_rect>& routing_masks, const std::vector<Physical_buffer_model>& models,
                                    const Producer_settings& settings) {
  require(settings.max_nets > 0 && settings.max_fanout > 0 && settings.max_option_vertices > 0
              && std::isfinite(settings.max_seconds) && settings.max_seconds > 0 && original.dbu > 0 && original.dbu == tech.dbu
              && !models.empty(),
          "invalid native checkpoint producer settings/models/units");
  for (auto v : settings.directional_rc) {
    require(std::isfinite(v) && v >= 0, "invalid producer technology RC");
  }
  const auto      begin = Clock::now();
  const double    units = original.dbu;
  Producer_result result;
  const auto      demand  = discover_checkpoint_demand(input, driver, units, settings.max_nets);
  result.demand_seconds   = elapsed(begin);
  result.supported_api    = demand.supported_api;
  result.critical_nets    = demand.critical_nets;
  result.window_nets      = demand.window_nets;
  result.outside_nets     = demand.outside_nets;
  result.omitted_nets     = demand.omitted_nets;
  result.missing_contexts = demand.missing_contexts;
  result.problem.nets     = demand.nets;
  if (!result.supported_api) {
    result.seconds = elapsed(begin);
    return result;
  }
  for (const auto& model : models) {
    const auto checked = physical_buffer_model(tech, library, model.ports);
    require(bool(checked.model), "producer buffer lacks a supported physical/timing model");
    const auto& canonical = *checked.model;
    require(model.site == canonical.site && model.width_dbu == canonical.width_dbu && model.height_dbu == canonical.height_dbu
                && model.input_offset_dbu == canonical.input_offset_dbu && model.output_offset_dbu == canonical.output_offset_dbu
                && model.mirror_x == canonical.mirror_x,
            "producer buffer footprint/pins differ from physical master");
  }
  const auto                                               sites_begin = Clock::now();
  auto                                                     current     = original;
  std::map<std::string, const physical::Parasitics::Cell*> poses;
  for (const auto& c : input.parasitics.cells) {
    require(poses.emplace(c.name, &c).second, "duplicate current producer pose");
  }
  require(poses.size() == current.components.size(), "producer pose inventory differs from original signal frame");
  for (auto& c : current.components) {
    require(poses.contains(c.name), "producer current pose lacks original cell");
    const auto& p = *poses.at(c.name);
    require(p.master == c.macro && p.orientation < 8, "producer current master/orientation differs from original frame");
    if (c.place.status == physical::Status::FIXED || c.place.status == physical::Status::LOCKED) {
      require(c.place.x == p.x && c.place.y == p.y && unsigned(c.place.orientation) == p.orientation,
              "producer fixed geometry changed");
    }
    c.place.x           = p.x;
    c.place.y           = p.y;
    c.place.width       = p.width;
    c.place.height      = p.height;
    c.place.orientation = static_cast<physical::Orientation>(p.orientation);
  }
  auto site_settings = settings.sites;
  // RC component origins are rounded to DBU; protect their uncertainty.
  if (site_settings.occupy_movable) {
    site_settings.legalization_margin_dbu = std::max<int64_t>(1, site_settings.legalization_margin_dbu);
  }
  const auto sites              = build_physical_sites(tech, current, extra_masks, site_settings);
  result.problem.sites          = sites.legal;
  result.problem.geometry_epoch = input.previous ? input.previous->allocation.history.geometry_epoch : 0;
  if (input.previous && input.previous->allocation.sites.geometry() != sites.legal) {
    require(result.problem.geometry_epoch < std::numeric_limits<uint64_t>::max(), "producer geometry epoch exhausted");
    ++result.problem.geometry_epoch;
  }

  Site_reservations ledger(sites.legal);
  if (input.previous) {
    for (const auto& [owner, spans] : input.previous->allocation.sites.reservations()) {
      require(ledger.replace(owner, spans).status == Reservation_status::reserved,
              "producer geometry invalidates protected reservation");
    }
  }
  result.sites_seconds    = elapsed(sites_begin);
  const auto search_begin = Clock::now();
  const auto remaining    = [&] { return std::max(0., settings.max_seconds - elapsed(begin)); };
  for (auto& net : result.problem.nets) {
    Producer_net trace;
    trace.owner     = net.owner;
    const auto save = [&](const char* reason) {
      trace.reason = reason;
      result.nets.push_back(trace);
    };
    if (!net.context) {
      save("unsupported_native_source_context");
      continue;
    }
    if (net.context->sinks.size() > settings.max_fanout) {
      save("fanout_bound");
      continue;
    }
    if (remaining() <= 0) {
      result.budget_exhausted = true;
      save("producer_budget_exhausted");
      continue;
    }
    auto&            context = *net.context;
    Geometry_request request;
    const auto&      box = input.design.layout;
    const auto       lo = lattice({box.xl / units, box.yl / units}, units), hi = lattice({box.xh / units, box.yh / units}, units);
    request.core = {lo.x, lo.y, hi.x, hi.y};
    request.terminals.push_back(lattice(context.driver.output, units));
    for (const auto& sink : context.sinks) {
      request.terminals.push_back(lattice(sink.position, units));
    }
    request.routing_exclusions    = routing_masks;
    auto geometry_settings        = settings.geometry;
    geometry_settings.max_seconds = std::min(geometry_settings.max_seconds, remaining());
    const auto geometry           = build_visibility_graph(request, geometry_settings);
    if (!geometry.graph) {
      result.budget_exhausted = true;
      save("geometry_budget_exhausted");
      continue;
    }
    if (remaining() <= 0) {
      result.budget_exhausted = true;
      save("producer_budget_exhausted");
      continue;
    }
    auto route_settings        = settings.route;
    route_settings.max_seconds = std::min(route_settings.max_seconds, remaining());
    auto route                 = shortest_path_tree(*geometry.graph, units, settings.directional_rc, route_settings);
    if (!route.tree) {
      result.budget_exhausted |= route.status == Route_status::budget_exhausted;
      save("routing_disconnected_or_budget");
      continue;
    }
    const size_t             count = std::min(settings.max_option_vertices, route.tree->vertices.size());
    std::vector<Route_point> anchors;
    std::vector<size_t>      chosen;
    for (size_t i = 0; i < count; ++i) {
      const size_t vertex = count == 1 ? 0 : i * (route.tree->vertices.size() - 1) / (count - 1);
      chosen.push_back(vertex);
      anchors.push_back(lattice(route.tree->vertices[vertex].position, units));
    }
    if (remaining() <= 0) {
      result.budget_exhausted = true;
      save("producer_budget_exhausted");
      continue;
    }
    auto option_settings         = settings.options;
    option_settings.max_seconds  = std::min(option_settings.max_seconds, remaining());
    auto options                 = physical_site_options(sites, ledger, net.owner, anchors, models, routing_masks, option_settings);
    trace.options                = options.emitted;
    result.budget_exhausted     |= options.status == Site_option_status::budget_exhausted;
    for (size_t i = 0; i < chosen.size(); ++i) {
      route.tree->vertices[chosen[i]].options = std::move(options.options[i]);
    }
    context.capacity = std::move(options.capacity);
    terminals(*route.tree, context, routing_masks, units);
    // Preserve the unbuffered seed even if bounded timing search finds no fit.
    net.candidates.push_back({expand_tree(context, *route.tree, std::vector<size_t>(route.tree->vertices.size())), {}, {}});
    if (remaining() <= 0) {
      result.budget_exhausted = true;
      trace.candidates        = net.candidates.size();
      save("producer_budget_exhausted");
      continue;
    }
    auto search_settings        = settings.search;
    search_settings.dbu_per_um  = units;
    search_settings.max_seconds = std::min(search_settings.max_seconds, remaining());
    search_settings.selection
        = net.classification == Net_class::outside_window ? Tree_selection::outside_window : Tree_selection::critical;
    const auto searched      = search_tree(library, context, *route.tree, ledger, net.owner, search_settings);
    trace.assignments        = searched.assignments;
    result.budget_exhausted |= searched.status == Tree_search_status::budget_exhausted;
    if (searched.best) {
      net.candidates.push_back({searched.best->candidate, searched.best->sites, {}});
    }
    for (const auto& candidate : searched.frontier) {
      if (!searched.best || candidate.choices != searched.best->choices) {
        net.candidates.push_back({candidate.candidate, candidate.sites, {}});
      }
    }
    trace.candidates = net.candidates.size();
    save(searched.status == Tree_search_status::incomplete_models ? "incomplete_models" : "partial_single_topology_search");
  }
  result.search_seconds    = elapsed(search_begin);
  result.seconds           = elapsed(begin);
  result.budget_exhausted |= result.seconds >= settings.max_seconds;
  return result;
}
}  // namespace livehd::placement::buffering
