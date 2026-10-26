// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "density.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
using detail::require;
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1
std::vector<Site_span> canonical(std::vector<Site_span> spans) {
  std::sort(spans.begin(), spans.end(), [](const auto& a, const auto& b) {
    return std::tie(a.row, a.first, a.count) < std::tie(b.row, b.first, b.count);
  });
  std::vector<Site_span> result;
  for (const auto& span : spans) {
    require(span.count && span.first <= std::numeric_limits<uint64_t>::max() - span.count, "invalid density site span");
    if (!result.empty() && result.back().row == span.row && span.first <= result.back().first + result.back().count) {
      result.back().count = std::max(result.back().first + result.back().count, span.first + span.count) - result.back().first;
    } else {
      result.push_back(span);
    }
  }
  return result;
}
#endif
}  // namespace
Density_binding_result bind_reservation_density(const DreamPlace::native::Design& design, const Density_reservation_state& original,
                                                const std::vector<Allocation_net>& nets, const Allocation_result& allocation,
                                                double units, double site_units, const std::map<std::string, int>& materialized,
                                                const std::set<std::string>& retired) {
  require(std::isfinite(units) && units > 0 && std::isfinite(site_units) && site_units > 0, "invalid reservation density units");
  Density_binding_result result;
  result.proposal = original;
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1
  result.supported_api = true;
  require(nets.size() == allocation.nets.size(), "density allocation inventory differs");
  std::map<std::string, Density_footprint> footprints;
  for (const auto& r : original.footprints) {
    require(!r.owner.empty() && footprints.emplace(r.owner, r).second, "duplicate original density identity");
  }
  std::set<std::string> managed;
  for (const auto& [owner, buffers] : original.owners) {
    require(!owner.empty(), "empty density net owner");
    for (const auto& name : buffers) {
      require(footprints.contains(name) && managed.insert(name).second, "invalid density ownership ledger");
    }
  }
  const auto release = [&](const std::string& owner) {
    const auto old = result.proposal.owners.find(owner);
    if (old != result.proposal.owners.end()) {
      for (const auto& name : old->second) {
        footprints.erase(name);
      }
      result.proposal.owners.erase(old);
    }
  };
  for (const auto& owner : retired) {
    require(!owner.empty() && !allocation.proposal.sites.reservations().contains(owner),
            "retired density owner still reserves sites");
    release(owner);
    ++result.retired;
  }
  std::set<std::string> owners;
  for (size_t i = 0; i < nets.size(); ++i) {
    const auto& net      = nets[i];
    const auto& selected = allocation.nets[i];
    require(!net.owner.empty() && owners.insert(net.owner).second && !retired.contains(net.owner) && selected.owner == net.owner,
            "density net identity/order differs");
    if (!selected.candidate) {
      ++result.unmet;
      continue;
    }
    require(*selected.candidate < net.candidates.size() && selected.timing && selected.timing->feasible(),
            "density candidate was not admitted");
    require(net.classification != Net_class::outside_window || selected.timing->meets_required.value_or(true),
            "outside-window density candidate violates required time");
    const auto& chosen   = net.candidates[*selected.candidate];
    const auto  reserved = allocation.proposal.sites.reservations().find(net.owner);
    const auto  spans    = reserved == allocation.proposal.sites.reservations().end() ? std::vector<Site_span>{} : reserved->second;
    require(canonical(spans) == canonical(chosen.sites), "density candidate differs from negotiated sites");
    release(net.owner);
    std::vector<std::string> names;
    for (const auto& buffer : chosen.candidate.buffers) {
      require(!buffer.name.empty() && buffer.placement_allowed, "invalid density buffer identity/placement");
      const auto& box = buffer.body;
      require(std::isfinite(box.xl) && std::isfinite(box.yl) && std::isfinite(box.xh) && std::isfinite(box.yh) && box.xh > box.xl
                  && box.yh > box.yl,
              "invalid density buffer body");
      bool covered = false;
      for (const auto& span : spans) {
        const auto   site = site_body(allocation.proposal.sites.geometry(), span);
        const double eps  = 1e-8;
        covered           = covered
                  || (box.xl * site_units >= site.xl - eps && box.yl * site_units >= site.yl - eps
                      && box.xh * site_units <= site.xh + eps && box.yh * site_units <= site.yh + eps);
      }
      require(covered, "density buffer body lies outside negotiated sites");
      Density_footprint r{buffer.name, box.xl * units, box.yl * units, (box.xh - box.xl) * units, (box.yh - box.yl) * units, -1};
      require(footprints.emplace(r.owner, r).second, "density buffers collide with protected identities");
      names.push_back(r.owner);
    }
    result.proposal.owners[net.owner] = std::move(names);
    ++result.replaced;
  }
  for (const auto& [name, node] : materialized) {
    require(footprints.contains(name) && node >= 0, "unknown/stale materialized density identity");
  }
  result.proposal.footprints.clear();
  for (auto& [name, r] : footprints) {
    const auto bound = materialized.find(name);
    require(r.materialized_node < 0 || bound != materialized.end(), "materialized density requires fresh physical binding");
    if (bound != materialized.end()) {
      r.materialized_node = bound->second;
    }
    result.proposal.footprints.push_back(r);
  }
  auto checked                 = design;
  checked.density_reservations = result.proposal.footprints;
  checked.validate();
  const auto charge           = DreamPlace::native::reservation_charge(checked);
  result.virtual_buffers      = charge.virtual_count;
  result.materialized_buffers = charge.materialized_count;
  result.virtual_area         = charge.virtual_area;
  result.materialized_area    = charge.materialized_area;
#else
  (void)design;
  (void)nets;
  (void)allocation;
  (void)materialized;
  (void)retired;
#endif
  return result;
}
}  // namespace livehd::placement::buffering
