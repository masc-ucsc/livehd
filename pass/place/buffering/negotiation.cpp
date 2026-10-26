// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "negotiation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "budget.hpp"
#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
using detail::Clock;
using detail::elapsed;
void add(uint64_t& total, uint64_t units) {
  require(total <= UINT64_MAX - units, "negotiation resource count overflow");
  total += units;
}
Gap_key gap_for(const Legal_sites& geometry, const Site_span& span) {
  const auto& row = geometry.at(span.row);
  for (const auto& gap : row.gaps) {
    if (span.first >= gap.first && span.first + span.count <= gap.first + gap.count) {
      return {gap.row, gap.first, gap.count};
    }
  }
  throw std::invalid_argument("allocation span not in a legal gap");
}
struct Prepared {
  size_t                          index = 0;
  Evaluation                      timing;
  std::optional<double>           margin;
  std::map<Gap_key, uint64_t>     gaps;
  std::map<std::string, uint64_t> resources;
  std::vector<Gap_key>            buffer_gaps;
};
struct Usage {
  std::map<Gap_key, uint64_t>     gaps;
  std::map<std::string, uint64_t> resources;
};
Usage usage(const Allocation_state& state) {
  Usage result;
  for (const auto& [owner, spans] : state.sites.reservations()) {
    for (const auto& span : spans) {
      add(result.gaps[gap_for(state.sites.geometry(), span)], span.count);
    }
  }
  for (const auto& [owner, demand] : state.resources) {
    for (const auto& [name, units] : demand) {
      add(result.resources[name], units);
    }
  }
  return result;
}
double charge(const Prepared& choice, const Usage& occupied, const Congestion_history& history,
              const std::map<std::string, uint64_t>& capacities, const Allocation_settings& settings) {
  long double cost = 0;
  for (const auto& [gap, units] : choice.gaps) {
    const auto used   = occupied.gaps.find(gap);
    const auto prior  = history.gaps.find(gap);
    cost             += static_cast<long double>(units) / gap.count
            * ((prior == history.gaps.end() ? 0 : prior->second)
               + settings.present_price * static_cast<long double>(used == occupied.gaps.end() ? 0 : used->second) / gap.count);
  }
  for (const auto& [name, units] : choice.resources) {
    if (units) {
      const auto capacity = capacities.at(name);
      require(capacity > 0, "priced demand has zero resource capacity");
      const auto used   = occupied.resources.find(name);
      const auto prior  = history.resources.find(name);
      cost             += static_cast<long double>(units) / capacity
              * ((prior == history.resources.end() ? 0 : prior->second)
                 + settings.present_price * static_cast<long double>(used == occupied.resources.end() ? 0 : used->second)
                       / capacity);
    }
  }
  require(std::isfinite(cost) && cost <= std::numeric_limits<double>::max(), "nonfinite negotiation congestion charge");
  return double(cost);
}
Usage without(Usage occupied, const Allocation_state& state, const std::string& owner) {
  if (auto it = state.sites.reservations().find(owner); it != state.sites.reservations().end()) {
    for (auto span : it->second) {
      occupied.gaps[gap_for(state.sites.geometry(), span)] -= span.count;
    }
  }
  if (auto it = state.resources.find(owner); it != state.resources.end()) {
    for (const auto& [name, units] : it->second) {
      occupied.resources[name] -= units;
    }
  }
  return occupied;
}
uint64_t overlaps(const std::vector<std::pair<Site_span, std::string>>& spans, std::map<Gap_key, uint64_t>& excess,
                  const Legal_sites& geometry) {
  std::map<Gap_key, std::map<uint64_t, int64_t>> events;
  for (const auto& [span, owner] : spans) {
    auto& changes = events[gap_for(geometry, span)];
    ++changes[span.first];
    --changes[span.first + span.count];
  }
  uint64_t total = 0;
  for (const auto& [gap, changes] : events) {
    int64_t  active   = 0;
    uint64_t previous = 0;
    for (const auto& [position, change] : changes) {
      if (active > 1) {
        require(position - previous <= UINT64_MAX / uint64_t(active - 1), "negotiated overlap count overflow");
        const auto units = (position - previous) * uint64_t(active - 1);
        add(excess[gap], units);
        add(total, units);
      }
      active   += change;
      previous  = position;
    }
  }
  return total;
}
struct Quality {
  std::array<size_t, 3>      unmet{};
  std::optional<long double> worst;
  long double                area = 0, wire = 0;
};
bool better(const Quality& next, const Quality& prior) {
  if (next.unmet != prior.unmet) {
    return next.unmet < prior.unmet;
  }
  if (next.worst != prior.worst) {
    return next.worst.value_or(0) > prior.worst.value_or(0);
  }
  if (next.area != prior.area) {
    return next.area < prior.area;
  }
  return next.wire < prior.wire;
}
std::optional<Point> entry(Point from, Point to, Rect gap) {
  const auto inside = [&](Point p) { return p.x >= gap.xl && p.x <= gap.xh && p.y >= gap.yl && p.y <= gap.yh; };
  if (inside(from)) {
    return from;
  }
  if (from.x == to.x && from.x >= gap.xl && from.x <= gap.xh) {
    const double value = to.y > from.y ? gap.yl : gap.yh;
    if (value >= std::min(from.y, to.y) && value <= std::max(from.y, to.y)) {
      return Point{from.x, value};
    }
  }
  if (from.y == to.y && from.y >= gap.yl && from.y <= gap.yh) {
    const double value = to.x > from.x ? gap.xl : gap.xh;
    if (value >= std::min(from.x, to.x) && value <= std::max(from.x, to.x)) {
      return Point{value, from.y};
    }
  }
  return {};
}
void finalize(Allocation_result& result, const Allocation_state& original, const std::vector<Allocation_net>& nets,
              const std::vector<std::vector<Prepared>>& prepared, const std::map<std::string, uint64_t>& capacities,
              const Allocation_settings& settings) {
  // Charges use one final price epoch, even if a better physical assignment
  // was retained from an earlier round. Prices never select the hard period.
  const auto final_usage = usage(result.proposal);
  for (size_t index = 0; index < nets.size(); ++index) {
    if (result.nets[index].candidate) {
      const auto selected = *result.nets[index].candidate;
      const auto it       = std::find_if(prepared[index].begin(), prepared[index].end(), [&](const auto& choice) {
        return choice.index == selected;
      });
      result.nets[index].congestion_charge
          = charge(*it, without(final_usage, result.proposal, nets[index].owner), result.proposal.history, capacities, settings);
      const auto& alternative   = nets[index].candidates[selected];
      double      fraction_left = settings.maximum_pull_fraction;
      for (size_t buffer_index = 0; buffer_index < alternative.candidate.buffers.size(); ++buffer_index) {
        const auto&                   gap  = it->buffer_gaps[buffer_index];
        const auto                    body = site_body(original.sites.geometry(), {gap.row, gap.first, gap.count});
        const auto                    box  = micron_rect(body, settings.dbu_per_um);
        std::optional<Gap_entry_pull> pull;
        for (const auto& tree : alternative.candidate.trees) {
          for (const auto& terminal : tree.terminals) {
            if (terminal.buffer && terminal.index == buffer_index) {
              std::vector<size_t> parent(tree.rc.nodes.size(), tree.rc.nodes.size()), order{tree.driver_node};
              parent[tree.driver_node] = tree.driver_node;
              std::vector<std::vector<size_t>> adjacency(tree.rc.nodes.size());
              for (auto edge : tree.rc.edges) {
                adjacency[edge.from].push_back(edge.to);
                adjacency[edge.to].push_back(edge.from);
              }
              for (size_t scan = 0; scan < order.size(); ++scan) {
                for (auto next : adjacency[order[scan]]) {
                  if (parent[next] == parent.size()) {
                    parent[next] = order[scan];
                    order.push_back(next);
                  }
                }
              }
              std::vector<size_t> path;
              for (size_t at = terminal.node;; at = parent[at]) {
                path.push_back(at);
                if (at == tree.driver_node) {
                  break;
                }
              }
              std::reverse(path.begin(), path.end());
              if (auto point = entry(tree.positions[path.front()], tree.positions[path.front()], box)) {
                pull = Gap_entry_pull{nets[index].owner, tree.driver_stage, buffer_index, *point, 0, nets[index].classification};
              }
              for (size_t segment = 1; segment < path.size(); ++segment) {
                if (pull) {
                  break;
                }
                auto point = entry(tree.positions[path[segment - 1]], tree.positions[path[segment]], box);
                if (point) {
                  pull = Gap_entry_pull{nets[index].owner, tree.driver_stage, buffer_index, *point, 0, nets[index].classification};
                  break;
                }
                if (tree.positions[path[segment - 1]].x != tree.positions[path[segment]].x
                    && tree.positions[path[segment - 1]].y != tree.positions[path[segment]].y) {
                  break;
                }
              }
            }
          }
        }
        if (settings.maximum_pull_fraction == 0) {
          continue;
        }
        if (pull) {
          const auto remaining  = alternative.candidate.buffers.size() - buffer_index;
          pull->fraction        = fraction_left / double(remaining);
          fraction_left        -= pull->fraction;
          result.pulls.push_back(*pull);
        } else {
          ++result.missing_gap_entries;
        }
      }
    }
  }
}
}  // namespace
Allocation_result negotiate(const liberty::timing::Library& library, const Allocation_state& original,
                            const std::vector<Allocation_net>& nets, const std::map<std::string, uint64_t>& capacities,
                            const std::set<std::string>& retired, const Allocation_settings& settings) {
  const auto started = Clock::now();
  require(std::isfinite(settings.max_seconds) && settings.max_seconds > 0 && std::isfinite(settings.dbu_per_um)
              && settings.dbu_per_um > 0 && std::isfinite(settings.present_price) && settings.present_price >= 0
              && std::isfinite(settings.history_increment) && settings.history_increment >= 0
              && std::isfinite(settings.maximum_price) && settings.maximum_price >= 0
              && std::isfinite(settings.area_normalization_um2) && settings.area_normalization_um2 > 0
              && std::isfinite(settings.outside_congestion_weight) && settings.outside_congestion_weight >= 0
              && std::isfinite(settings.maximum_pull_fraction) && settings.maximum_pull_fraction >= 0
              && settings.maximum_pull_fraction <= 1,
          "invalid negotiation settings");
  Allocation_result result(original);
  if (result.proposal.history.geometry_epoch != settings.geometry_epoch) {
    result.proposal.history = {settings.geometry_epoch, {}, {}};
    result.history_rebased  = true;
  }
  for (const auto& [key, price] : result.proposal.history.gaps) {
    require(std::isfinite(price) && price >= 0 && price <= settings.maximum_price, "invalid gap history price");
  }
  for (const auto& [key, price] : result.proposal.history.resources) {
    require(std::isfinite(price) && price >= 0 && price <= settings.maximum_price, "invalid resource history price");
  }
  std::set<std::string> names;
  for (const auto& net : nets) {
    require(!net.owner.empty() && names.emplace(net.owner).second && !retired.contains(net.owner) && std::isfinite(net.severity)
                && (net.classification == Net_class::critical || net.classification == Net_class::window
                    || net.classification == Net_class::outside_window),
            "invalid/duplicate/retired allocation owner");
  }
  for (const auto& owner : retired) {
    require(!owner.empty(), "empty retired owner");
    result.proposal.sites.release(owner);
    result.proposal.resources.erase(owner);
  }
  auto                               baseline = result.proposal;
  std::vector<std::vector<Prepared>> prepared(nets.size());
  result.nets.resize(nets.size());
  for (size_t index = 0; index < nets.size(); ++index) {
    result.nets[index].owner     = nets[index].owner;
    result.nets[index].reason    = "not evaluated before work budget; prior reservation retained";
    result.input_search_complete = result.input_search_complete && nets[index].input_search_complete;
  }
  bool       missing_models = false;
  size_t     considered     = 0;
  const auto finish         = [&](Allocation_status status) {
    finalize(result, original, nets, prepared, capacities, settings);
    result.status  = status;
    result.seconds = elapsed(started);
    return result;
  };
  // A single scratch allocator validates geometry without a deep copy for
  // every candidate. Nothing can mutate original's ownership or geometry.
  Site_reservations geometry_only(original.sites.geometry());
  for (size_t index = 0; index < nets.size(); ++index) {
    const auto& net = nets[index];
    if (!net.context) {
      missing_models = true;
      ++result.unsupported;
      result.nets[index].reason = "missing native source context; prior reservation retained";
      continue;
    }
    bool unbuffered = false;
    for (size_t candidate_index = 0; candidate_index < net.candidates.size(); ++candidate_index) {
      if (considered >= settings.max_candidates || elapsed(started) >= settings.max_seconds) {
        return finish(Allocation_status::budget_exhausted);
      }
      ++considered;
      const auto& alternative = net.candidates[candidate_index];
      require(alternative.sites.size() == alternative.candidate.buffers.size(), "buffer/span inventory differs");
      auto context               = *net.context;
      context.max_waveform_fits  = std::min(context.max_waveform_fits, settings.max_waveform_fits - result.waveform_fits);
      auto timing                = evaluate(library, context, alternative.candidate);
      result.waveform_fits      += timing.waveform_fits;
      if (timing.status == Status::budget_exhausted) {
        return finish(Allocation_status::budget_exhausted);
      }
      if (timing.status == Status::unsupported) {
        ++result.unsupported;
        missing_models = true;
        continue;
      }
      ++result.evaluated;
      if (!std::isfinite(timing.area_um2) || !std::isfinite(timing.wire_length_um) || !std::isfinite(timing.multiplier_cost)) {
        ++result.unsupported;
        missing_models = true;
        continue;
      }
      if (!timing.feasible() || (net.classification == Net_class::outside_window && !timing.meets_required.value_or(true))) {
        ++result.rejected;
        continue;
      }
      if (geometry_only.check(net.owner, alternative.sites).status != Reservation_status::reserved) {
        ++result.rejected;
        continue;
      }
      Prepared choice;
      choice.index  = candidate_index;
      choice.timing = std::move(timing);
      std::vector<bool> matched(alternative.sites.size());
      bool              bound = true;
      for (const auto& buffer : alternative.candidate.buffers) {
        size_t found = alternative.sites.size();
        for (size_t span_index = 0; span_index < alternative.sites.size(); ++span_index) {
          if (body_within_sites(buffer.body,
                                site_body(original.sites.geometry(), alternative.sites[span_index]),
                                settings.dbu_per_um)) {
            found = span_index;
          }
        }
        if (found == alternative.sites.size() || matched[found]) {
          bound = false;
          break;
        }
        matched[found] = true;
        choice.buffer_gaps.push_back(gap_for(original.sites.geometry(), alternative.sites[found]));
      }
      if (!bound) {
        ++result.rejected;
        continue;
      }
      for (const auto& span : alternative.sites) {
        add(choice.gaps[gap_for(original.sites.geometry(), span)], span.count);
      }
      bool unknown = false, overflowed = false;
      for (const auto& [name, units] : choice.timing.demand) {
        if (name.starts_with("sites:")) {
          continue;  // exact physical spans carry row demand
        }
        if (!capacities.contains(name)) {
          unknown = true;
          break;
        }
        add(choice.resources[name], units);
      }
      for (const auto& use : alternative.resources) {
        require(!use.resource.empty(), "empty shared crossing resource");
        if (!capacities.contains(use.resource)) {
          unknown = true;
          break;
        }
        add(choice.resources[use.resource], use.units);
      }
      if (unknown) {
        ++result.unsupported;
        missing_models = true;
        continue;
      }
      for (const auto& [name, units] : choice.resources) {
        if (units > capacities.at(name)) {
          overflowed = true;
        }
      }
      if (overflowed) {
        ++result.rejected;
        continue;
      }
      choice.margin = worst_input_margin(choice.timing, context.driver);
      unbuffered    = unbuffered || alternative.candidate.buffers.empty();
      prepared[index].push_back(std::move(choice));
    }
    result.nets[index].mandatory = !unbuffered;
    if (!prepared[index].empty()) {
      baseline.sites.release(net.owner);
      baseline.resources.erase(net.owner);
    } else {
      result.nets[index].reason = "no admitted local candidate; prior reservation retained";
      if (net.candidates.empty()) {
        missing_models = true;
      }
    }
  }
  const auto protected_usage = usage(baseline);
  for (const auto& [name, units] : protected_usage.resources) {
    require(capacities.contains(name) && units <= capacities.at(name), "protected shared resource exceeds supplied capacity");
  }
  if (elapsed(started) >= settings.max_seconds) {
    return finish(Allocation_status::budget_exhausted);
  }
  auto                               previous = baseline;
  std::vector<std::optional<size_t>> prior_choices(nets.size());
  std::vector<bool>                  unmet(nets.size(), false);
  std::optional<Quality>             best_quality;
  bool                               settled = false;
  for (size_t round = 0; round < settings.max_rounds; ++round) {
    if (elapsed(started) >= settings.max_seconds) {
      return finish(Allocation_status::budget_exhausted);
    }
    auto allocated                              = baseline;
    allocated.history                           = result.proposal.history;
    auto                               occupied = usage(previous);
    std::vector<std::optional<size_t>> requests(nets.size()), selected(nets.size());
    const auto rank = [&](size_t net_index, const Prepared& lhs, const Prepared& rhs, const Usage& current) {
      if (lhs.index == rhs.index) {
        return false;
      }
      const auto& net = nets[net_index];
      double      a   = charge(lhs, current, allocated.history, capacities, settings),
             b        = charge(rhs, current, allocated.history, capacities, settings);
      if (net.classification != Net_class::outside_window) {
        if (lhs.margin != rhs.margin) {
          return lhs.margin.value_or(0) > rhs.margin.value_or(0);
        }
      } else {
        const auto x = static_cast<long double>(lhs.timing.area_um2) / settings.area_normalization_um2
                       + static_cast<long double>(settings.outside_congestion_weight) * a;
        const auto y = static_cast<long double>(rhs.timing.area_um2) / settings.area_normalization_um2
                       + static_cast<long double>(settings.outside_congestion_weight) * b;
        if (x != y) {
          return x < y;
        }
      }
      if (a != b) {
        return a < b;
      }
      if (lhs.timing.area_um2 != rhs.timing.area_um2) {
        return lhs.timing.area_um2 < rhs.timing.area_um2;
      }
      if (lhs.timing.wire_length_um != rhs.timing.wire_length_um) {
        return lhs.timing.wire_length_um < rhs.timing.wire_length_um;
      }
      if (prior_choices[net_index] == lhs.index) {
        return true;
      }
      if (prior_choices[net_index] == rhs.index) {
        return false;
      }
      return lhs.index < rhs.index;
    };
    for (size_t index = 0; index < nets.size(); ++index) {
      if (!prepared[index].empty()) {
        const auto current = without(occupied, previous, nets[index].owner);
        size_t     best    = 0;
        for (size_t i = 1; i < prepared[index].size(); ++i) {
          if (rank(index, prepared[index][i], prepared[index][best], current)) {
            best = i;
          }
        }
        requests[index] = best;
      }
    }
    std::vector<size_t> order(nets.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](size_t lhs, size_t rhs) {
      if (nets[lhs].classification != nets[rhs].classification) {
        return nets[lhs].classification < nets[rhs].classification;
      }
      if (result.nets[lhs].mandatory != result.nets[rhs].mandatory) {
        return result.nets[lhs].mandatory;
      }
      if (unmet[lhs] != unmet[rhs]) {
        return bool(unmet[lhs]);
      }
      if (nets[lhs].severity != nets[rhs].severity) {
        return nets[lhs].severity > nets[rhs].severity;
      }
      return nets[lhs].owner < nets[rhs].owner;
    });
    Quality quality;
    size_t  assigned = 0;
    for (auto index : order) {
      const auto&         net = nets[index];
      std::vector<size_t> alternatives(prepared[index].size());
      std::iota(alternatives.begin(), alternatives.end(), 0);
      const auto current = usage(allocated);
      std::stable_sort(alternatives.begin(), alternatives.end(), [&](size_t lhs, size_t rhs) {
        return rank(index, prepared[index][lhs], prepared[index][rhs], current);
      });
      for (auto which : alternatives) {
        if (elapsed(started) >= settings.max_seconds) {
          return finish(Allocation_status::budget_exhausted);
        }
        const auto& choice = prepared[index][which];
        bool        fits   = true;
        for (const auto& [name, units] : choice.resources) {
          const auto used = current.resources.contains(name) ? current.resources.at(name) : 0;
          if (units > capacities.at(name) - used) {
            fits = false;
          }
        }
        if (!fits) {
          continue;
        }
        if (allocated.sites.replace(net.owner, net.candidates[choice.index].sites).status != Reservation_status::reserved) {
          continue;
        }
        allocated.resources[net.owner] = choice.resources;
        selected[index]                = which;
        ++assigned;
        quality.area += choice.timing.area_um2;
        quality.wire += choice.timing.wire_length_um;
        if (net.classification != Net_class::outside_window && choice.margin) {
          const auto normalized = static_cast<long double>(*choice.margin) / net.context->normalization_period_ps;
          quality.worst         = std::min(quality.worst.value_or(normalized), normalized);
        }
        break;
      }
      if (!selected[index]) {
        ++quality.unmet[size_t(net.classification)];
      }
      unmet[index] = !selected[index];
    }
    std::vector<std::pair<Site_span, std::string>> demand;
    Usage                                          request_usage = protected_usage;
    for (const auto& [owner, spans] : baseline.sites.reservations()) {
      for (auto span : spans) {
        demand.push_back({span, owner});
      }
    }
    for (size_t index = 0; index < nets.size(); ++index) {
      if (requests[index]) {
        const auto& choice = prepared[index][*requests[index]];
        for (auto span : nets[index].candidates[choice.index].sites) {
          demand.push_back({span, nets[index].owner});
        }
        for (const auto& [name, units] : choice.resources) {
          add(request_usage.resources[name], units);
        }
      }
    }
    std::map<Gap_key, uint64_t> excess;
    const auto                  overlap         = overlaps(demand, excess, original.sites.geometry());
    uint64_t                    resource_excess = 0;
    auto                        history         = allocated.history;
    for (const auto& [gap, units] : excess) {
      history.gaps[gap]
          = std::min(settings.maximum_price, history.gaps[gap] + settings.history_increment * double(units) / gap.count);
    }
    for (const auto& [name, units] : request_usage.resources) {
      if (units > capacities.at(name)) {
        const auto extra = units - capacities.at(name);
        add(resource_excess, extra);
        // Zero capacity can never admit positive demand, including protected use.
        require(capacities.at(name) > 0, "positive request on zero capacity");
        history.resources[name]
            = std::min(settings.maximum_price,
                       history.resources[name] + settings.history_increment * double(extra) / capacities.at(name));
      }
    }
    result.rounds.push_back({round, assigned, nets.size() - assigned, overlap, resource_excess});
    bool                               stable = true;
    std::vector<std::optional<size_t>> next_choices(nets.size());
    for (size_t index = 0; index < nets.size(); ++index) {
      if (selected[index]) {
        next_choices[index] = prepared[index][*selected[index]].index;
      }
      stable = stable && next_choices[index] == prior_choices[index];
    }
    if (!best_quality || better(quality, *best_quality)) {
      best_quality    = quality;
      result.proposal = allocated;
      for (size_t index = 0; index < nets.size(); ++index) {
        auto& report     = result.nets[index];
        report.candidate = next_choices[index];
        report.timing.reset();
        if (selected[index]) {
          report.timing = prepared[index][*selected[index]].timing;
          report.reason = "allocated under local native/physical/resource constraints";
        } else if (!prepared[index].empty()) {
          report.reason = "shared capacity unavailable after priority allocation";
        }
      }
    }
    result.proposal.history = history;
    previous                = std::move(allocated);
    previous.history        = history;
    prior_choices           = std::move(next_choices);
    bool retained_matches   = true;
    for (size_t index = 0; index < nets.size(); ++index) {
      retained_matches = retained_matches && result.nets[index].candidate == prior_choices[index];
    }
    if (assigned == nets.size() && retained_matches && (stable || (overlap == 0 && resource_excess == 0))) {
      settled = true;
      break;
    }
  }
  return finish(missing_models ? Allocation_status::incomplete_models
                : settled      ? Allocation_status::settled
                               : Allocation_status::budget_exhausted);
}
}  // namespace livehd::placement::buffering
