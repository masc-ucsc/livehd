// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_sites.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
using detail::Clock;
using detail::elapsed;
using detail::finite;
using detail::require;
constexpr int64_t exact_limit = detail::exact_dbu_limit;
double            manhattan(Point lhs, Point rhs) { return std::abs(lhs.x - rhs.x) + std::abs(lhs.y - rhs.y); }
Point             pin_position(int64_t x, int64_t y, Point offset) {
  const long double px = static_cast<long double>(x) + offset.x, py = static_cast<long double>(y) + offset.y;
  const Point       result{double(px), double(py)};
  require(std::isfinite(result.x) && std::isfinite(result.y) && std::abs(static_cast<long double>(result.x) - px) <= 1e-6
              && std::abs(static_cast<long double>(result.y) - py) <= 1e-6,
          "physical buffer pin loses DBU precision");
  return result;
}
Point microns(Point dbu_point, int64_t scale) {
  const Point result{dbu_point.x / scale, dbu_point.y / scale};
  require(std::isfinite(result.x) && std::isfinite(result.y)
              && std::abs(static_cast<long double>(result.x) * scale - dbu_point.x) <= 1e-6
              && std::abs(static_cast<long double>(result.y) * scale - dbu_point.y) <= 1e-6,
          "physical buffer conversion loses DBU precision");
  return result;
}
int64_t dimension(double microns, int64_t dbu) {
  const long double value = static_cast<long double>(microns) * dbu;
  require(std::isfinite(value) && value > 0 && value <= exact_limit, "invalid physical site/master dimension");
  const auto rounded = std::llround(value);
  require(rounded > 0 && std::abs(value - rounded) <= 1e-6, "physical dimension is not integer DBU");
  return rounded;
}
void coordinate(int64_t value) {
  require(value >= -exact_limit && value <= exact_limit, "physical site coordinate outside exact DBU range");
}
Site_rect grow(Site_rect r, int64_t margin) {
  require(margin >= 0 && r.xl < r.xh && r.yl < r.yh, "invalid physical placement mask/halo");
  coordinate(r.xl);
  coordinate(r.yl);
  coordinate(r.xh);
  coordinate(r.yh);
  require(r.xl >= -exact_limit + margin && r.yl >= -exact_limit + margin && r.xh <= exact_limit - margin
              && r.yh <= exact_limit - margin,
          "physical halo coordinate overflow");
  return {r.xl - margin, r.yl - margin, r.xh + margin, r.yh + margin};
}
Site_rect mask(physical::Rect rect) {
  require(std::isfinite(rect.xl) && std::isfinite(rect.yl) && std::isfinite(rect.xh) && std::isfinite(rect.yh) && rect.xl < rect.xh
              && rect.yl < rect.yh && rect.xl >= -double(exact_limit) && rect.yl >= -double(exact_limit)
              && rect.xh <= double(exact_limit) && rect.yh <= double(exact_limit),
          "invalid physical blockage rectangle");
  return {int64_t(std::floor(rect.xl)), int64_t(std::floor(rect.yl)), int64_t(std::ceil(rect.xh)), int64_t(std::ceil(rect.yh))};
}
}  // namespace
Physical_sites build_physical_sites(const physical::Technology& tech, const physical::Floorplan& frame,
                                    const std::vector<Site_rect>& extra, const Physical_site_settings& settings) {
  require(tech.dbu > 0 && tech.dbu == frame.dbu, "physical site LEF/DEF units differ");
  require(settings.macro_halo_dbu >= 0 && settings.legalization_margin_dbu >= 0 && settings.macro_halo_dbu <= exact_limit
              && settings.legalization_margin_dbu <= exact_limit,
          "invalid physical site halo/margin");
  auto                   die = mask(frame.die);
  std::vector<Site_row>  rows;
  std::vector<Site_rect> exclusions;
  Physical_sites         result;
  result.dbu              = tech.dbu;
  result.occupies_movable = settings.occupy_movable;
  for (const auto& row : frame.rows) {
    const auto site = tech.sites.find(row.site);
    require(site != tech.sites.end(), "physical row SITE missing from technology");
    const auto width = dimension(site->second.width, tech.dbu), height = dimension(site->second.height, tech.dbu);
    require(!row.name.empty() && row.count > 0 && row.step == width
                && (row.orientation == physical::Orientation::N || row.orientation == physical::Orientation::FS),
            "unsupported physical placement row");
    coordinate(row.x);
    coordinate(row.y);
    require(row.count <= exact_limit / width && row.x <= exact_limit - row.count * width && row.y <= exact_limit - height,
            "physical row coordinate overflow");
    Site_rect body{row.x, row.y, row.x + row.count * width, row.y + height};
    require(body.xl >= die.xl && body.yl >= die.yl && body.xh <= die.xh && body.yh <= die.yh, "physical row outside die");
    require(result.rows.emplace(row.name, row).second, "duplicate physical row name");
    result.rows_by_y[row.y].push_back(row.name);
    rows.push_back({row.name, body, width});
  }
  require(!rows.empty(), "physical sites require original placement rows");
  std::map<std::string, const physical::Component*> components;
  for (const auto& cell : frame.components) {
    require(!cell.name.empty() && components.emplace(cell.name, &cell).second, "duplicate physical component identity");
    const auto model = tech.macros.find(cell.macro);
    require(model != tech.macros.end() && !model->second.virtual_proxy, "physical component master missing or virtual");
    const auto& macro = model->second;
    int64_t     width = dimension(macro.width, tech.dbu), height = dimension(macro.height, tech.dbu);
    using O = physical::Orientation;
    switch (cell.place.orientation) {
      case O::N:
      case O::S:
      case O::FN:
      case O::FS: break;
      case O::W :
      case O::E :
      case O::FW:
      case O::FE: std::swap(width, height); break;
      default   : throw std::invalid_argument("invalid component orientation");
    }
    if (!settings.occupy_movable && cell.place.status != physical::Status::FIXED && cell.place.status != physical::Status::LOCKED) {
      ++result.skipped_movable;
      continue;
    }
    require(cell.place.status == physical::Status::PLACED || cell.place.status == physical::Status::FIXED
                || cell.place.status == physical::Status::LOCKED,
            "physical occupancy includes an unplaced/invalid component");
    coordinate(cell.place.x);
    coordinate(cell.place.y);
    require(cell.place.x <= exact_limit - width && cell.place.y <= exact_limit - height, "physical component body overflow");
    require((cell.place.width == 0 || cell.place.width == width) && (cell.place.height == 0 || cell.place.height == height),
            "physical component body disagrees with LEF orientation");
    auto body = Site_rect{cell.place.x, cell.place.y, cell.place.x + width, cell.place.y + height};
    exclusions.push_back(grow(body, macro.klass.starts_with("CORE") ? 0 : settings.macro_halo_dbu));
    ++result.occupied_components;
  }
  for (const auto& blockage : frame.blockages) {
    if (!blockage.component.empty()) {
      auto it = components.find(blockage.component);
      require(it != components.end(), "placement blockage component missing");
      auto status = it->second->place.status;
      require(status == physical::Status::FIXED || status == physical::Status::LOCKED,
              "placement blockage follows a movable component");
    }
    for (auto r : blockage.rectangles) {
      exclusions.push_back(mask(r));
      ++result.placement_masks;
    }
  }
  for (auto r : extra) {
    exclusions.push_back(grow(r, 0));
    ++result.placement_masks;
  }
  result.legal = build_legal_sites(rows, exclusions, 0, settings.legalization_margin_dbu);
  return result;
}
Physical_model_result physical_buffer_model(const physical::Technology& tech, const liberty::timing::Library& library,
                                            const Cell_ports& ports) {
  const auto fail = [](std::string reason) { return Physical_model_result{{}, std::move(reason)}; };
  const auto lm = tech.macros.find(ports.cell), unused = tech.macros.end();
  if (lm == unused || lm->second.virtual_proxy || !lm->second.klass.starts_with("CORE")) {
    return fail("buffer has no real CORE LEF master");
  }
  const auto& macro = lm->second;
  // The evaluator's stage model, plus no other output arc and a finite load.
  std::string why;
  const auto  stage = stage_model(library, ports, true, &why);
  if (!stage) {
    return fail("buffer: " + why);
  }
  if (stage->output->arcs.size() != 1) {
    return fail("buffer needs one unconditional timing arc");
  }
  for (const auto& cap : stage->input->capacitance_ff) {
    if (!std::isfinite(*cap) || *cap < 0) {
      return fail("buffer lacks capacitance/NLDM tables");
    }
  }
  if (!tech.sites.contains(macro.site)) {
    return fail("buffer SITE is missing");
  }
  Physical_buffer_model result;
  result.ports    = ports;
  result.site     = macro.site;
  result.mirror_x = macro.symmetry_x;
  try {
    require(tech.dbu > 0, "invalid technology units");
    result.width_dbu  = dimension(macro.width, tech.dbu);
    result.height_dbu = dimension(macro.height, tech.dbu);
  } catch (const std::invalid_argument& error) {
    return fail(error.what());
  }
  for (auto item : {
           std::pair{ ports.input,  &result.input_offset_dbu},
           std::pair{ports.output, &result.output_offset_dbu}
  }) {
    const auto pin = macro.pins.find(item.first);
    if (pin == macro.pins.end() || pin->second.shapes.empty()) {
      return fail("buffer LEF pin geometry missing");
    }
    const auto& r = pin->second.shapes[0].rect;
    if (!std::isfinite(r.xl) || !std::isfinite(r.yl) || !std::isfinite(r.xh) || !std::isfinite(r.yh) || r.xl >= r.xh
        || r.yl >= r.yh) {
      return fail("invalid buffer pin shape");
    }
    const bool input = item.first == ports.input;
    if (pin->second.direction != (input ? "INPUT" : "OUTPUT") && pin->second.direction != (input ? "input" : "output")) {
      return fail("buffer LEF pin direction differs from timing");
    }
    *item.second = {((r.xl + r.xh) * .5 + macro.origin_x) * tech.dbu, ((r.yl + r.yh) * .5 + macro.origin_y) * tech.dbu};
    const auto p = *item.second;
    if (!finite(p) || p.x < 0 || p.y < 0 || p.x > result.width_dbu || p.y > result.height_dbu) {
      return fail("buffer pin centroid outside body");
    }
  }
  return {result, {}};
}
Site_option_result physical_site_options(const Physical_sites& physical, const Site_reservations& reservations,
                                         const std::string& owner, const std::vector<Route_point>& vertices,
                                         const std::vector<Physical_buffer_model>& models, const std::vector<Site_rect>& routing,
                                         const Site_option_settings& settings) {
  const auto start = Clock::now();
  require(physical.dbu > 0 && !owner.empty() && settings.max_attachment_dbu >= 0 && settings.max_attachment_dbu <= exact_limit
              && std::isfinite(settings.max_seconds) && settings.max_seconds > 0,
          "invalid physical site option settings");
  require(physical.legal == reservations.geometry(), "reservation geometry differs from physical snapshot");
  for (auto r : routing) {
    grow(r, 0);
  }
  int64_t max_height = 0;
  for (const auto& model : models) {
    require(model.width_dbu > 0 && model.width_dbu <= exact_limit && model.height_dbu > 0 && model.height_dbu <= exact_limit
                && !model.site.empty() && !model.ports.cell.empty() && !model.ports.input.empty() && !model.ports.output.empty()
                && finite(model.input_offset_dbu) && finite(model.output_offset_dbu) && model.input_offset_dbu.x >= 0
                && model.input_offset_dbu.x <= model.width_dbu && model.input_offset_dbu.y >= 0
                && model.input_offset_dbu.y <= model.height_dbu && model.output_offset_dbu.x >= 0
                && model.output_offset_dbu.x <= model.width_dbu && model.output_offset_dbu.y >= 0
                && model.output_offset_dbu.y <= model.height_dbu,
            "invalid physical buffer model");
    max_height = std::max(max_height, model.height_dbu);
  }
  const auto         available = reservations.available_sites(owner);
  Site_option_result result;
  result.options.resize(vertices.size());
  const auto finish = [&](Site_option_status status) {
    result.status  = status;
    result.seconds = elapsed(start);
    return result;
  };
  for (const auto& [name, row] : available) {
    for (auto gap : row.gaps) {
      result.capacity["sites:" + name] += gap.count;
    }
  }
  const auto radius           = static_cast<long double>(settings.max_attachment_dbu);
  const auto enumerate_vertex = [&](size_t vertex) {
    std::vector<size_t> model_emitted(models.size());
    auto                anchor = vertices[vertex];
    coordinate(anchor.x);
    coordinate(anchor.y);
    const long double low  = static_cast<long double>(anchor.y) - radius - max_height,
                      high = static_cast<long double>(anchor.y) + radius;
    auto row               = physical.rows_by_y.lower_bound(low <= -exact_limit ? -exact_limit : int64_t(std::floor(low)));
    for (; row != physical.rows_by_y.end() && row->first <= high; ++row) {
      for (const auto& name : row->second) {
        if (elapsed(start) >= settings.max_seconds) {
          return false;
        }
        const auto& meta     = physical.rows.at(name);
        const auto& free     = available.at(name);
        const auto& geometry = free.row;
        for (const auto& model : models) {
          if (model.site != meta.site || model.height_dbu != geometry.body.yh - geometry.body.yl
              || (meta.orientation == physical::Orientation::FS && !model.mirror_x)) {
            continue;
          }
          const auto model_index = size_t(&model - models.data());
          if (model_emitted[model_index] >= settings.max_options_per_model_per_vertex) {
            result.status = Site_option_status::budget_exhausted;
            continue;
          }
          auto input = model.input_offset_dbu, output = model.output_offset_dbu;
          if (meta.orientation == physical::Orientation::FS) {
            input.y  = model.height_dbu - input.y;
            output.y = model.height_dbu - output.y;
          }
          long double left = -exact_limit, right = exact_limit;
          for (auto pin : {input, output}) {
            const auto slack = radius - std::abs(static_cast<long double>(anchor.y) - meta.y - pin.y);
            if (slack < 0) {
              left  = 1;
              right = 0;
              break;
            }
            left  = std::max(left, static_cast<long double>(anchor.x) - pin.x - slack);
            right = std::min(right, static_cast<long double>(anchor.x) - pin.x + slack);
          }
          if (left > right) {
            continue;
          }
          const auto width = uint64_t(model.width_dbu / geometry.pitch + (model.width_dbu % geometry.pitch != 0));
          for (auto gap : free.gaps) {
            if (model_emitted[model_index] >= settings.max_options_per_model_per_vertex) {
              result.status = Site_option_status::budget_exhausted;
              break;
            }
            if (gap.count < width) {
              continue;
            }
            const long double lo
                = std::max(left, static_cast<long double>(geometry.body.xl) + static_cast<long double>(gap.first) * geometry.pitch);
            const long double hi = std::min(right,
                                            static_cast<long double>(geometry.body.xl)
                                                + static_cast<long double>(gap.first + gap.count - width) * geometry.pitch);
            if (lo > hi) {
              continue;
            }
            const auto first = uint64_t(std::max<long double>(gap.first, std::ceil((lo - geometry.body.xl) / geometry.pitch)));
            const auto last  = uint64_t(
                std::min<long double>(gap.first + gap.count - width, std::floor((hi - geometry.body.xl) / geometry.pitch)));
            if (first > last) {
              continue;
            }
            for (uint64_t column = first;; ++column) {
              if (model_emitted[model_index] >= settings.max_options_per_model_per_vertex) {
                result.status = Site_option_status::budget_exhausted;
                break;
              }
              if (result.candidate_checks >= settings.max_candidate_checks || elapsed(start) >= settings.max_seconds) {
                return false;
              }
              ++result.candidate_checks;
              const auto  x = geometry.body.xl + int64_t(column) * geometry.pitch;
              const auto  y = geometry.body.yl;
              const auto  a = pin_position(x, y, input), b = pin_position(x, y, output);
              const Point at{double(anchor.x), double(anchor.y)};
              if (manhattan(at, a) <= settings.max_attachment_dbu && manhattan(at, b) <= settings.max_attachment_dbu) {
                if (!detail::rectilinear_clear(at, a, routing) || !detail::rectilinear_clear(b, at, routing)) {
                  ++result.routing_rejected;
                } else {
                  if (result.options[vertex].size() >= settings.max_options_per_vertex) {
                    result.status = Site_option_status::budget_exhausted;
                    return true;
                  }
                  const double dbu = double(physical.dbu);
                  Buffer       buffer{
                      owner + "__vb_" + std::to_string(vertex),
                      model.ports,
                      microns(a, physical.dbu),
                      microns(b, physical.dbu),
                            {double(x) / dbu, double(y) / dbu, double(x + model.width_dbu) / dbu, double(y + model.height_dbu) / dbu},
                      true,
                            {{"sites:" + name, width}}
                  };
                  result.options[vertex].push_back({
                      std::move(buffer),
                      {name, column, width},
                      true
                  });
                  ++result.emitted;
                  ++model_emitted[model_index];
                }
              }
              if (column == last) {
                break;
              }
            }
          }
        }
      }
    }

    return true;
  };
  for (size_t vertex = 0; vertex < vertices.size(); ++vertex) {
    if (!enumerate_vertex(vertex)) {
      return finish(Site_option_status::budget_exhausted);
    }
  }
  return finish(result.status);
}
}  // namespace livehd::placement::buffering
