// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "placement_session.hpp"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace livehd::placement {
namespace dp = DreamPlace::native;
dp::Result Placement_session::advance(dp::Design& design, const dp::Options& options, const std::vector<std::string>& identities,
                                      std::string phase, unsigned minimum_steps,
                                      const std::function<std::vector<std::string>()>& updated_identities, bool macro_replan,
                                      bool recover) {
  if (identities.size() != design.node_x.size()
      || std::unordered_set<std::string>(identities.begin(), identities.end()).size() != identities.size()) {
    throw std::invalid_argument("placement session requires unique physical node identities");
  }
  auto controls                 = options;
  controls.min_slice_iterations = int(minimum_steps);
  dp::GpRebind binding;
  const bool   warm   = live();
  const int    before = iteration();
  if (warm) {
    binding.allow_fixed_macro_move = macro_replan;
    const auto& old                = current.physical.layout;
    binding.renormalize_density
        = recover || design.layout.xh - design.layout.xl > old.xh - old.xl || design.layout.yh - design.layout.yl > old.yh - old.yl;
    binding.restart_wirelength      = recover;
    binding.reset_routing_inflation = recover;
    std::unordered_map<std::string, int> previous;
    for (size_t i = 0; i < current.identities.size(); ++i) {
      previous.emplace(current.identities[i], int(i));
    }
    for (const auto& name : identities) {
      const auto found = previous.find(name);
      binding.previous_node.push_back(found == previous.end() ? -1 : found->second);
    }
  }
  auto candidate_names = identities;
  if (controls.checkpoint) {
    const auto checkpoint = controls.checkpoint;
    controls.checkpoint   = [&, checkpoint](const dp::Design& physical, int iteration) {
      auto update = checkpoint(physical, iteration);
      if (update.replacement) {
        if (!updated_identities) {
          throw std::invalid_argument("placement session topology update requires physical identity publication");
        }
        auto next_names = updated_identities();
        if (next_names.size() != update.replacement->node_x.size() || update.previous_node.size() != next_names.size()
            || std::unordered_set<std::string>(next_names.begin(), next_names.end()).size() != next_names.size()) {
          throw std::invalid_argument("placement session received invalid replacement identities");
        }
        for (size_t i = 0; i < next_names.size(); ++i) {
          const int previous = update.previous_node[i];
          if (previous < -1
              || (previous >= 0
                  && (size_t(previous) >= candidate_names.size() || next_names[i] != candidate_names[size_t(previous)]))) {
            throw std::invalid_argument("placement session replacement changed a retained node identity");
          }
        }
        candidate_names = std::move(next_names);
      }
      return update;
    };
  }
  auto       candidate = design;
  auto       optimizer = current.optimizer;
  const auto result    = dp::place(candidate, controls, &optimizer, &binding);
  if (optimizer.signature && optimizer.physical.node_x.size() != candidate_names.size()) {
    throw std::runtime_error("placement session topology callback must publish physical identities before continuation");
  }
  design                = std::move(candidate);
  current.optimizer     = std::move(optimizer);
  current.identities    = std::move(candidate_names);
  current.physical      = design;
  starts               += !warm && live();
  spent                += result.iterations;
  const size_t fillers  = live() ? current.optimizer.x.size() - current.optimizer.physical.node_x.size() : 0;
  events.push_back({std::move(phase),
                    before,
                    iteration(),
                    fillers,
                    warm,
                    false,
                    current.optimizer.density_weight,
                    binding.renormalize_density,
                    binding.allow_fixed_macro_move,
                    binding.restart_wirelength,
                    current.optimizer.wirelength_epoch,
                    binding.reset_routing_inflation});
  return result;
}
size_t Placement_session::seed_movable(dp::Design& design, const std::vector<std::string>& identities) const {
  if (!live()) {
    return 0;
  }
  design.validate();
  if (identities.size() != design.node_x.size()) {
    throw std::invalid_argument("placement session seed identity count mismatch");
  }
  std::unordered_map<std::string, size_t> previous;
  for (size_t i = 0; i < current.identities.size(); ++i) {
    previous.emplace(current.identities[i], i);
  }
  size_t      seeded = 0;
  const auto& old    = current.physical;
  for (int i = 0; i < design.num_movable; ++i) {
    const auto found = previous.find(identities[size_t(i)]);
    if (found == previous.end() || found->second >= size_t(old.num_movable)) {
      continue;
    }
    const size_t from = found->second;
    if ((!design.node_macro.empty() && design.node_macro[i]) || (!old.node_macro.empty() && old.node_macro[from])) {
      continue;
    }
    // Spread retained cell centers into the expanded core before the short
    // timed slice. Same-core retries retain centers; fixed bodies and fresh
    // IO edge seeds stay. The next GP slice relaxes this initial proposal.
    const double sx = (design.layout.xh - design.layout.xl) / (old.layout.xh - old.layout.xl);
    const double sy = (design.layout.yh - design.layout.yl) / (old.layout.yh - old.layout.yl);
    const double x
        = design.layout.xl + (old.node_x[from] + old.node_size_x[from] / 2 - old.layout.xl) * sx - design.node_size_x[i] / 2;
    const double y
        = design.layout.yl + (old.node_y[from] + old.node_size_y[from] / 2 - old.layout.yl) * sy - design.node_size_y[i] / 2;
    design.node_x[i] = std::clamp(x, design.layout.xl, design.layout.xh - design.node_size_x[i]);
    design.node_y[i] = std::clamp(y, design.layout.yl, design.layout.yh - design.node_size_y[i]);
    ++seeded;
  }
  return seeded;
}
void Placement_session::synchronize(dp::Design& design, const dp::Options& options, const std::vector<std::string>& identities,
                                    std::string phase) {
  if (!live()) {
    return;
  }
  auto controls           = options;
  controls.max_iterations = controls.min_slice_iterations = 0;
  controls.legalize = controls.detailed_place = false;
  controls.before_legalize                    = {};
  controls.after_macro_legalize               = {};
  controls.observer                           = {};
  advance(design, controls, identities, std::move(phase));
}
void Placement_session::restore(const Snapshot& accepted) {
  const int before     = iteration();
  current              = accepted;
  const size_t fillers = live() ? current.optimizer.x.size() - current.optimizer.physical.node_x.size() : 0;
  events.push_back({"restore",
                    before,
                    iteration(),
                    fillers,
                    false,
                    true,
                    current.optimizer.density_weight,
                    false,
                    false,
                    false,
                    current.optimizer.wirelength_epoch});
}
void Placement_session::write(const std::string& path) const {
  if (path.empty()) {
    return;
  }
  std::ofstream out(path);
  out << "{\"cold_starts\":" << starts << ",\"spent_iterations\":" << spent << ",\"selected_iteration\":" << iteration()
      << ",\"events\":[";
  for (size_t i = 0; i < events.size(); ++i) {
    const auto& e = events[i];
    out << (i ? "," : "") << "{\"phase\":" << std::quoted(e.phase) << ",\"begin\":" << e.begin << ",\"end\":" << e.end
        << ",\"fillers\":" << e.fillers << ",\"rebind\":" << (e.rebind ? "true" : "false")
        << ",\"restore\":" << (e.restore ? "true" : "false") << ",\"density_weight\":" << e.density_weight
        << ",\"density_reset\":" << (e.density_reset ? "true" : "false")
        << ",\"macro_replan\":" << (e.macro_replan ? "true" : "false")
        << ",\"wirelength_restart\":" << (e.wirelength_restart ? "true" : "false") << ",\"wirelength_epoch\":" << e.wirelength_epoch
        << ",\"routing_inflation_reset\":" << (e.routing_inflation_reset ? "true" : "false") << '}';
  }
  out << "]}\n";
  if (!out) {
    throw std::runtime_error("cannot write placement session report");
  }
}
}  // namespace livehd::placement
