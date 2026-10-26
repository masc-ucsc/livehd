// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "preroute_demand.hpp"

#include <algorithm>
#include <stdexcept>

namespace livehd::placement {
namespace dp = DreamPlace::native;
Preroute_demand::Preroute_demand(const dp::Design& design, dp::RudyOptions options, const Route_capacity_grid& grid,
                                 const std::vector<size_t>& realized_nets)
    : design_(design), options_(std::move(options)) {
  design_.validate();
  const auto& l = design_.layout;
  if (l.xl != grid.bounds.xl || l.yl != grid.bounds.yl || l.xh != grid.bounds.xh || l.yh != grid.bounds.yh || options_.bins_x <= 0
      || options_.bins_y <= 0 || size_t(options_.bins_x) != grid.bins_x || size_t(options_.bins_y) != grid.bins_y) {
    throw std::invalid_argument("RSMT demand and route capacity grids differ");
  }
  if (options_.model != dp::RudyModel::rsmt || !options_.macro_blockages || !options_.pin_access) {
    throw std::invalid_argument("pre-route demand requires obstacle-aware RSMT and pin access");
  }
  for (const auto net : realized_nets) {
    if (net >= design_.net_mask.size()) {
      throw std::invalid_argument("realized pre-route demand net is out of range");
    }
    design_.net_mask[net] = 0;
  }
  // Timing weights may increase routing pressure, but cannot discount a live
  // physical net below one net's demand. Exclusions belong in net_mask.
  for (size_t n = 0; n < design_.net_weights.size(); ++n) {
    if (design_.net_mask[n]) {
      design_.net_weights[n] = std::max(1., design_.net_weights[n]);
    }
  }
}
bool Preroute_demand::prepare() {
  try {
    cursor_.emplace(dp::RudyMaskSnapshot(design_, options_));
    return true;
  } catch (const std::exception&) {
    cursor_.reset();
    return false;
  }
}
Preroute_demand_trial Preroute_demand::without(size_t net) {
  if (net >= design_.net_mask.size() || !design_.net_mask[net]) {
    throw std::invalid_argument("pre-route demand candidate is masked or already accepted");
  }
  // Restore on success and exceptions (e.g. missing FLUTE LUTs); callers can
  // explore another route without inheriting a failed candidate's removal.
  struct Restore {
    std::uint8_t& mask;
    std::uint8_t  prior;
    ~Restore() { mask = prior; }
  } restore{design_.net_mask[net], design_.net_mask[net]};
  design_.net_mask[net]     = 0;
  auto                  map = cursor_ ? cursor_->without(net) : dp::rudy(design_, options_);
  Preroute_demand_trial trial;
  trial.owner                         = this;
  trial.epoch                         = epoch_;
  trial.net                           = net;
  trial.remaining.horizontal          = std::move(map.horizontal);
  trial.remaining.vertical            = std::move(map.vertical);
  trial.remaining.horizontal_capacity = options_.horizontal_capacity;
  trial.remaining.vertical_capacity   = options_.vertical_capacity;
  trial.remaining.pin_utilization     = std::move(map.pin_utilization);
  trial.remaining.blocked_pins        = map.blocked_pins;
  trial.remaining.unroutable_nets     = map.unroutable_nets;
  return trial;
}
void Preroute_demand::commit(const Preroute_demand_trial& trial) {
  if (trial.owner != this || trial.epoch != epoch_ || trial.net >= design_.net_mask.size() || !design_.net_mask[trial.net]) {
    throw std::invalid_argument("foreign or stale demand trial");
  }
  if (cursor_) {
    cursor_->exclude(trial.net);
  }
  design_.net_mask[trial.net] = 0;
  ++epoch_;
}
}  // namespace livehd::placement
