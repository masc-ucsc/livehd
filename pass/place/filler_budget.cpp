// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "filler_budget.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace livehd::placement {
Filler_budget relative_filler_budget(const DreamPlace::native::Design& d, const DreamPlace::native::Options& o) {
  Filler_budget r;
  r.fraction = o.filler_area_fraction;
  d.validate();
  const auto&  l     = d.layout;
  const double width = l.xh - l.xl, height = l.yh - l.yl;
  double       area = 0, fixed = 0, macros = 0;
  r.nonmacro_area = width * height;
  for (size_t i = 0; i < d.node_x.size(); ++i) {
    const bool   macro = !d.node_macro.empty() && d.node_macro[i];
    const double px = d.node_pad_x.empty() ? 0 : d.node_pad_x[i], py = d.node_pad_y.empty() ? 0 : d.node_pad_y[i];
    const double physical = std::max(0., d.node_size_x[i] - 2 * px) * std::max(0., d.node_size_y[i] - 2 * py);
    const double clipped  = std::max(0., std::min(l.xh, d.node_x[i] + d.node_size_x[i]) - std::max(l.xl, d.node_x[i]))
                           * std::max(0., std::min(l.yh, d.node_y[i] + d.node_size_y[i]) - std::max(l.yl, d.node_y[i]));
    if (macro) {
      r.nonmacro_area -= physical;
    } else if (i >= size_t(d.num_movable)) {
      r.nonmacro_area -= clipped;
    }
    if (i < size_t(d.num_movable)) {
      area += d.node_size_x[i] * d.node_size_y[i];
      if (macro) {
        macros += d.node_size_x[i] * d.node_size_y[i];
      }
    } else {
      fixed += clipped;
    }
  }
  r.nonmacro_area     = std::max(0., r.nonmacro_area);
  double materialized = 0;
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1
  for (const auto& reservation : d.density_reservations) {
    if (reservation.materialized_node >= 0) {
      materialized += d.node_size_x.at(reservation.materialized_node) * d.node_size_y.at(reservation.materialized_node);
    }
  }
#endif
  r.available_area = o.filler_cell_space ? (1 - o.target_density) * r.nonmacro_area
                                         : std::max(0.,
                                                    o.target_density * (width * height - fixed) - area + materialized
                                                        + (o.scale_macro_density ? (1 - o.target_density) * macros : 0));
  r.requested_area = o.add_fillers && d.num_movable ? r.available_area * r.fraction : 0;
  if (!std::isfinite(r.requested_area) || r.requested_area < 0) {
    throw std::invalid_argument("invalid relative filler area budget");
  }
  return r;
}
}  // namespace livehd::placement
