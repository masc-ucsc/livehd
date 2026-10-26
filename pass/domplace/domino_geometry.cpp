// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_geometry.hpp"

#include <algorithm>
#include <format>
#include <numeric>
#include <set>
#include <stdexcept>

#include "inou/lefdef/pin_access.hpp"
#include "inou/lefdef/routing_rules.hpp"

namespace livehd::domplace {
namespace {
namespace ph = physical;
using ph::Orientation;
using ph::Row_access;
constexpr int64_t     kMaxPeriod    = 64;    // site x-phases and start rows enumerated by one check
constexpr size_t      kAccessBudget = 4096;  // enumerate_pin_access work per pin and placement
constexpr const char* kCheckMaster  = "__lhd_domino_access_check";
constexpr std::array  kOrientations{Orientation::N, Orientation::FS, Orientation::FN, Orientation::S};
[[noreturn]] void     refuse(const std::string& why) { throw std::invalid_argument("domino geometry: " + why); }
int64_t               to_dbu(double um, int64_t dbu, const std::string& what) {
  if (const auto value = ph::rounded_dbu(um * dbu, 1e-6)) {
    return *value;
  }
  refuse(what + " is not an integral DBU");
}
bool    is_pg(const ph::Pin& p) { return p.use == "POWER" || p.use == "GROUND"; }
int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }  // b > 0
int64_t ceil_div(int64_t a, int64_t b) { return -floor_div(-a, b); }
int64_t floor_mod(int64_t a, int64_t b) { return a - floor_div(a, b) * b; }
// LEF track lattice of a layer along x (vertical wires) or y, in DBU.
struct Lattice {
  int64_t offset = 0, pitch = 0, width = 0;
  int64_t at_or_above(int64_t v) const { return offset + ceil_div(v - offset, pitch) * pitch; }
};
Lattice lattice(const ph::Technology& tech, const std::string& name, bool x, const char* role) {
  const auto it = tech.layers.find(name);
  if (name.empty() || it == tech.layers.end()) {
    refuse(std::format("unknown {} layer '{}'", role, name));
  }
  const auto& l = it->second;
  Lattice     result{to_dbu(x ? l.offset_x : l.offset_y, tech.dbu, name + " offset"),
                 to_dbu(x ? l.pitch_x : l.pitch_y, tech.dbu, name + " pitch"),
                 to_dbu(l.width, tech.dbu, name + " width")};
  if (result.pitch <= 0 || result.width <= 0) {
    refuse(std::format("{} layer {} needs a positive pitch and width", role, name));
  }
  return result;
}
// One PG edge rail of the reference cell in rail-local DBU: y about the row
// edge, xl as the overhang before x = 0 and xh as the overhang past the width.
struct Edge_rail {
  std::string            pin, use;
  std::vector<ph::Shape> shapes;
};
struct Reference {
  std::string              site;
  int64_t                  site_width = 0, row_height = 0, grid = 1, area = 0;  // DBU, area in DBU^2
  std::array<Edge_rail, 2> rails;                                               // bottom (y = 0) and top (y = H) edges
  int64_t                  rail_reach = 0;                                      // farthest rail extent from its edge
};
Reference reference(const Geometry_inputs& in) {
  if (!in.tech) {
    refuse("no technology");
  }
  const auto& tech = *in.tech;
  const auto  it   = tech.macros.find(in.inverter);
  if (it == tech.macros.end()) {
    refuse("reference inverter is absent from LEF: " + in.inverter);
  }
  const auto& m    = it->second;
  const auto  site = tech.sites.find(m.site);
  if (m.klass != "CORE" || m.virtual_proxy || site == tech.sites.end()) {
    refuse("reference inverter must be a CORE cell on a LEF SITE: " + in.inverter);
  }
  const auto grid = ph::manufacturing_grid_dbu(tech, tech.dbu);
  if (!grid) {
    refuse("unsupported manufacturing grid");
  }
  Reference r;
  r.site           = m.site;
  r.site_width     = to_dbu(site->second.width, tech.dbu, "site width");
  r.row_height     = to_dbu(site->second.height, tech.dbu, "site height");
  r.grid           = *grid;
  const auto width = to_dbu(m.width, tech.dbu, in.inverter + " width");
  if (r.site_width <= 0 || width <= 0 || to_dbu(m.height, tech.dbu, in.inverter + " height") != r.row_height) {
    refuse("reference inverter must be one site row high: " + in.inverter);
  }
  r.area = width * r.row_height;
  for (const auto& [name, pin] : m.pins) {
    if (!is_pg(pin)) {
      continue;
    }
    for (const auto& shape : pin.shapes) {
      const auto local = shape.rect.translated(m.origin_x, m.origin_y);
      const auto what  = in.inverter + "/" + name;
      const auto xl = to_dbu(local.xl, tech.dbu, what), xh = to_dbu(local.xh, tech.dbu, what);
      const auto yl = to_dbu(local.yl, tech.dbu, what), yh = to_dbu(local.yh, tech.dbu, what);
      // Edge rails span the cell and are centered on a row edge; wells are not rails.
      if (xl > 0 || xh < width || (yl + yh != 0 && yl + yh != 2 * r.row_height)) {
        continue;
      }
      const int  edge   = yl + yh ? 1 : 0;
      const auto center = edge * r.row_height;
      auto&      rail   = r.rails[edge];
      if (!rail.pin.empty() && rail.pin != name) {
        refuse("two PG pins share one rail edge of " + in.inverter);
      }
      rail.pin = name;
      rail.use = pin.use;
      rail.shapes.push_back({
          shape.layer,
          {double(xl), double(yl - center), double(xh - width), double(yh - center)}
      });
      r.rail_reach = std::max({r.rail_reach, center - yl, yh - center});
    }
  }
  if (r.rails[0].pin.empty() || r.rails[1].pin.empty() || r.rails[0].pin == r.rails[1].pin) {
    refuse("reference inverter needs distinct PG rails on both row edges: " + in.inverter);
  }
  return r;
}
ph::Rect microns(const ph::Rect& r, int64_t dbu) {
  const double d = double(dbu);
  return {r.xl / d, r.yl / d, r.xh / d, r.yh / d};
}
// The inverter's rails as in local row 0 and in their FS image in local row 1
// (y -> 2H - y), stretched to the cell width: bottom polarity at 0 and 2H.
void add_pg_pins(ph::Macro& m, const Reference& r, int64_t width, int64_t dbu) {
  const auto h = r.row_height;
  for (int edge = 0; edge < 2; ++edge) {
    const auto& rail = r.rails[edge];
    auto&       pin  = m.pins[rail.pin];
    pin.direction    = "INOUT";
    pin.use          = rail.use;
    for (int row = 0; row < 2; ++row) {
      for (const auto& shape : rail.shapes) {
        const auto&     s    = shape.rect;
        const auto      y    = edge * h;
        const auto      rect = row == 0 ? ph::Rect{s.xl, y + s.yl, width + s.xh, y + s.yh}
                                        : ph::Rect{s.xl, 2 * h - y - s.yh, width + s.xh, 2 * h - y - s.yl};
        const ph::Shape placed{shape.layer, microns(rect, dbu)};
        if (std::find(pin.shapes.begin(), pin.shapes.end(), placed) == pin.shapes.end()) {
          pin.shapes.push_back(placed);
        }
      }
    }
  }
}
// Pin-layer spacing bound, as the router's rule model computes it.
int64_t pin_clearance(const Geometry_inputs& in) {
  ph::Floorplan frame;
  frame.dbu            = in.tech->dbu;
  const auto clearance = ph::Signal_metal_rules(*in.tech, frame).clearance(in.access.pin);
  if (!clearance) {
    refuse("unsupported spacing rules on pin layer " + in.access.pin);
  }
  return *clearance;
}
// Site x-phases of a vertical lattice: lattice positions in the local frame of
// a cell placed k sites from the lattice origin, k < period. A mirrored (FN, S)
// cell is a whole number of sites wide, so its local positions are those of
// the offset's negation; the two sets differ unless the offset is 0 or half
// of gcd(site width, pitch).
std::pair<int64_t, int64_t> phase_range(const Lattice& v, int64_t site_width, int64_t period) {
  int64_t lo = v.pitch, hi = 0;
  for (int64_t k = 0; k < period; ++k) {
    for (const auto offset : {v.offset, -v.offset}) {  // N/FS, then FN/S
      const auto phase = floor_mod(offset - k * site_width, v.pitch);
      lo               = std::min(lo, phase);
      hi               = std::max(hi, phase);
    }
  }
  return {lo, hi};
}
int64_t x_period(int64_t site_width, std::initializer_list<int64_t> pitches) {
  int64_t span = site_width;
  for (auto pitch : pitches) {
    span = std::lcm(span, pitch);
    if (span / site_width > kMaxPeriod) {
      return 0;
    }
  }
  return span / site_width;
}
// Signal pins whose shapes are pin-layer DBU rectangles inside the outline,
// each within one local row, with shapes in both rows. Failures name the rest.
std::set<std::string> outline_clean_pins(const ph::Macro& m, const std::string& layer, int64_t dbu, int64_t w, int64_t h,
                                         std::vector<std::string>& failures) {
  std::set<std::string> clean;
  for (const auto& [name, p] : m.pins) {
    if (is_pg(p)) {
      continue;
    }
    std::array<bool, 2> in_row{};
    const auto          before = failures.size();
    for (const auto& shape : p.shapes) {
      const auto r
          = ph::snapped_dbu(ph::Rect{shape.rect.xl * dbu, shape.rect.yl * dbu, shape.rect.xh * dbu, shape.rect.yh * dbu}, 1e-6);
      if (shape.layer != layer || !r) {
        failures.push_back(std::format("{}: shape must be {} geometry on integral DBU", name, layer));
      } else if (r->xl < 0 || r->yl < 0 || r->xh > w || r->yh > 2 * h) {
        failures.push_back(std::format("{}: shape outside the {} x {} cell outline", name, w, 2 * h));
      } else if (r->yl < h && r->yh > h) {
        failures.push_back(name + ": shape crosses the middle rail");
      } else {
        in_row[r->yl >= h] = true;
      }
    }
    for (int row = 0; failures.size() == before && row < 2; ++row) {
      if (!in_row[row]) {
        failures.push_back(std::format("{}: no shape in local row {}", name, row));
      }
    }
    if (failures.size() == before) {
      clean.insert(name);
    }
  }
  return clean;
}
struct Frame_tracks {
  const char*        axis;
  const std::string& layer;
  const Lattice&     lattice;
};
// Check frame over die whose tracks are the given LEF lattices.
ph::Floorplan lattice_frame(int64_t dbu, const ph::Rect& die, std::initializer_list<Frame_tracks> tracks) {
  ph::Floorplan frame;
  frame.dbu = dbu;
  frame.die = die;
  for (const auto& t : tracks) {
    const bool x     = std::string_view(t.axis) == "X";
    const auto start = t.lattice.at_or_above(int64_t(x ? die.xl : die.yl));
    frame.tracks.push_back({t.axis, t.layer, start, (int64_t(x ? die.xh : die.yh) - start) / t.lattice.pitch + 1, t.lattice.pitch});
  }
  return frame;
}
// The PG pin with a placed rail centered on y spanning [xl, xh]; empty if none.
std::string rail_on_edge(const ph::Macro& m, const std::map<std::string, std::vector<ph::Rect>>& placed, double y, double xl,
                         double xh) {
  for (const auto& [name, p] : m.pins) {
    if (!is_pg(p)) {
      continue;
    }
    for (const auto& r : placed.at(name)) {
      if (r.yl + r.yh == 2 * y && r.xl <= xl && r.xh >= xh) {
        return name;
      }
    }
  }
  return {};
}
// Stand-in vertical access: a pin strip inside the row band that holds a
// vertical-layer wire on a track and spans at least one escape pitch.
bool vertical_access(const std::vector<ph::Rect>& pin, const ph::Rect& band, const Lattice& v, int64_t escape_pitch) {
  const auto half = (v.width + 1) / 2;
  return std::any_of(pin.begin(), pin.end(), [&](const ph::Rect& r) {
    return band.contains(r) && r.yh - r.yl >= escape_pitch && v.at_or_above(int64_t(r.xl) + half) + half <= r.xh;
  });
}
// Why enumerate_pin_access found no point: empty after recording the reasons
// in unsupported when the engine refused only technology rules it lacks.
std::optional<std::string> horizontal_refusal(const ph::Pin_access_result& access, std::set<std::string>& unsupported) {
  const auto& rejected = access.rejected;
  if (!rejected.empty()
      && std::all_of(rejected.begin(), rejected.end(), [](const auto& r) { return r.first.starts_with("unsupported"); })) {
    for (const auto& [reason, count] : rejected) {
      (void)count;
      unsupported.insert(reason);
    }
    return std::nullopt;
  }
  std::string reasons;
  for (const auto& [reason, count] : rejected) {
    reasons += std::format("{}{} x{}", reasons.empty() ? "" : ", ", reason, count);
  }
  return reasons.empty() ? "no candidate" : reasons;
}
}  // namespace
ph::Route_policy domino_route_policy(const ph::Technology& tech, std::string* source) {
  try {
    const auto& profile = ph::resolve_tech_profile(tech);
    if (source) {
      *source = "tech-profile:" + profile.name;
    }
    return profile.route;
  } catch (const std::invalid_argument&) {
    // No registered profile: derive the policy from the LEF stack order below.
  }
  const auto& stack = tech.routing_layers;
  if (stack.size() < 3) {
    refuse("a LEF routing stack of at least three layers is needed without a technology profile");
  }
  ph::Route_policy route;
  route.access.pin    = stack[0];
  route.access.escape = stack[1];
  route.branch_layer  = stack[2];
  for (const auto& [name, via] : tech.vias) {
    (void)name;
    std::set<std::string> metals, cuts;
    for (const auto& shape : via.shapes) {
      (tech.cut_layers.contains(shape.layer) ? cuts : metals).insert(shape.layer);
    }
    if (cuts.size() == 1 && metals == std::set<std::string>{stack[0], stack[1]}) {
      route.access.cut = *cuts.begin();
      break;
    }
  }
  if (route.access.cut.empty()) {
    refuse(std::format("no LEF via joins {} and {}", stack[0], stack[1]));
  }
  if (source) {
    *source = "lef-stack";
  }
  return route;
}
Geometry_inputs geometry_inputs(const ph::Technology& tech, std::string inverter, const ph::Route_policy& route,
                                ph::Row_access_convention convention) {
  return {&tech, std::move(inverter), route.access, route.branch_layer, std::move(convention)};
}
std::array<Row_access, 2> template_halves(const ph::Row_access_convention& convention) {
  return {convention.at(0), convention.at(1)};
}
Domino_variant_geometry generate_domino_macro(const usyn::Physical_cell_interface& pins, const usyn::Gate_formula::Metrics& pdn,
                                              const Domino_reference& reference_cells, size_t variant, const Geometry_inputs& in,
                                              std::optional<int64_t> width_override) {
  const auto  ref  = reference(in);
  const auto& tech = *in.tech;
  const auto  dbu  = tech.dbu;
  if (reference_cells.inverter.name != in.inverter) {
    refuse("the reference inverter " + reference_cells.inverter.name + " is not the geometry inverter " + in.inverter);
  }
  if (variant >= reference_cells.registers.size()) {
    refuse(std::format("no register for drive variant {} of {}", variant, pins.name));
  }
  const auto& register_cell = reference_cells.registers[variant].name;
  const auto  reg           = tech.macros.find(register_cell);
  if (reg == tech.macros.end()) {
    refuse("register is absent from LEF: " + register_cell);
  }
  const auto proxy_devices = int64_t(pdn.transistors) + reference_cells.aux_nonregister;
  if (proxy_devices == 0 || pins.name.empty() || pins.rails.empty()) {
    refuse("a domino master needs a name, rails and proxy devices: " + pins.name);
  }
  Domino_variant_geometry g;
  g.name           = pins.name;
  g.halves         = template_halves(in.row_access);
  const auto sw    = ref.site_width;
  const auto h     = ref.row_height;
  const auto a_reg
      = to_dbu(reg->second.width, dbu, register_cell + " width") * to_dbu(reg->second.height, dbu, register_cell + " height");
  // 2 * A_domino is integral in DBU^2 (x is a multiple of 1/2), so the site
  // count is exact: 35.0336 um^2 on sky130 is 14 two-row sites, never 15.
  const auto   twice = proxy_devices * ref.area + 2 * a_reg;
  const double dbu2  = double(dbu) * double(dbu);
  g.x                = proxy_devices / 2.0;
  g.a_not_um2        = ref.area / dbu2;
  g.a_register_um2   = a_reg / dbu2;
  g.a_estimate_um2   = twice / (2 * dbu2);
  g.w_area           = ceil_div(twice, 2 * (2 * h * sw)) * sw;
  // Pin slots: vertical-layer tracks wide enough for every site x-phase.
  const auto v       = lattice(tech, in.vertical_layer, true, "vertical-access");
  const auto pin     = lattice(tech, in.access.pin, true, "pin");
  const auto period  = x_period(sw, {v.pitch});
  if (!period) {
    refuse("too many site x-phases on " + in.vertical_layer);
  }
  const auto phases = phase_range(v, sw, period);
  const auto lo = phases.first, hi = phases.second;
  const auto clearance = pin_clearance(in);
  const auto slot      = v.pitch * ceil_div(std::max(pin.pitch, hi - lo + pin.width + clearance), v.pitch);
  const auto count     = int64_t(pins.rails.size()) + 2;
  g.w_pin              = (count + 1) * slot;
  g.width              = width_override ? *width_override : ceil_div(std::max(g.w_area, g.w_pin), sw) * sw;
  g.height             = 2 * h;
  if (g.width <= 0 || g.width % sw) {
    refuse(std::format("width {} of {} is not a positive whole number of sites", g.width, pins.name));
  }
  const auto grid_down = [&](int64_t x) { return floor_div(x, ref.grid) * ref.grid; };
  const auto grid_up   = [&](int64_t x) { return ceil_div(x, ref.grid) * ref.grid; };
  const auto first     = lo + std::max<int64_t>(0, ceil_div(slot / 2 - lo, v.pitch)) * v.pitch;
  const auto strip     = [&](int64_t s) {
    const auto track = first + s * slot;
    return std::pair{grid_down(track - (pin.width + 1) / 2), grid_up(track + hi - lo + (pin.width + 1) / 2)};
  };
  int64_t slots = 0;
  while (strip(slots).second <= g.width - strip(0).first) {
    ++slots;
  }
  // Rows keep the strips clear of the rail bands by the pin-layer spacing.
  const auto margin = grid_up(ref.rail_reach + clearance);
  if (h - 2 * margin <= 0) {
    refuse("rail bands leave no pin room in a row of " + ref.site);
  }
  auto& m      = g.macro;
  m.width      = double(g.width) / dbu;
  m.height     = double(g.height) / dbu;
  m.site       = ref.site;
  m.klass      = "CORE";
  m.symmetry_x = m.symmetry_y = true;
  std::vector<std::pair<std::string, std::string>> signals;
  for (const auto& rail : pins.rails) {
    signals.emplace_back(rail, "INPUT");
  }
  signals.emplace_back(pins.clock, "INPUT");
  signals.emplace_back(pins.output, "OUTPUT");
  const int first_row = g.halves[0] == Row_access::horizontal || g.halves[1] != Row_access::horizontal ? 0 : 1;
  for (size_t i = 0; i < signals.size(); ++i) {
    const auto& [name, direction] = signals[i];
    // Spread pins over the free slots; a narrow override overflows the outline.
    const auto s                  = slots >= count ? int64_t(2 * i + 1) * slots / (2 * count) : int64_t(i);
    const auto [xl, xh]           = strip(s);
    ph::Pin p{direction, name == pins.clock ? "CLOCK" : "SIGNAL", {}};
    for (int row : {first_row, 1 - first_row}) {
      p.shapes.push_back(
          {in.access.pin, microns({double(xl), double(row * h + margin), double(xh), double((row + 1) * h - margin)}, dbu)});
    }
    if (name.empty() || !m.pins.emplace(name, std::move(p)).second) {
      refuse("duplicate or empty pin name in " + pins.name);
    }
  }
  if (m.pins.contains(ref.rails[0].pin) || m.pins.contains(ref.rails[1].pin)) {
    refuse("signal pin named like a PG pin in " + pins.name);
  }
  add_pg_pins(m, ref, g.width, dbu);
  g.a_footprint_um2 = double(g.width) * double(g.height) / dbu2;
  return g;
}
Access_check check_domino_access(const Geometry_inputs& in, const ph::Macro& m, const std::array<Row_access, 2>& halves) {
  Access_check out;
  const auto   ref  = reference(in);
  const auto   dbu  = in.tech->dbu;
  const auto   sw   = ref.site_width;
  const auto   h    = ref.row_height;
  const auto   fail = [&](std::string why) { out.failures.push_back(std::move(why)); };
  const auto   w    = ph::rounded_dbu(m.width * dbu, 1e-6);
  if (!w || ph::rounded_dbu(m.height * dbu, 1e-6) != 2 * h || *w <= 0 || *w % sw || m.origin_x != 0 || m.origin_y != 0) {
    fail("macro: outline must be whole sites wide, two site rows high, with ORIGIN 0 0");
  }
  if (m.klass != "CORE" || m.site != ref.site) {
    fail("macro: must be CLASS CORE on SITE " + ref.site);
  }
  if (!m.symmetry_x) {
    fail("macro: SYMMETRY X missing (FS starts unavailable; generated rows would all be N)");
  }
  if (!m.symmetry_y) {
    fail("macro: SYMMETRY Y missing");
  }
  if (!out.failures.empty()) {
    return out;
  }
  const auto v      = lattice(*in.tech, in.vertical_layer, true, "vertical-access");
  const auto pin    = lattice(*in.tech, in.access.pin, true, "pin");
  const auto escape = lattice(*in.tech, in.access.escape, false, "escape");
  out.x_phases      = size_t(x_period(sw, {v.pitch, pin.pitch}));
  // Start rows: both parities and every escape-track y-phase of the rows.
  const auto starts = std::lcm(int64_t(2), std::lcm(h, escape.pitch) / h);
  if (!out.x_phases || starts > kMaxPeriod) {
    fail("macro: track lattice phases exceed the enumeration bound");
    return out;
  }
  const auto signal_pins = outline_clean_pins(m, in.access.pin, dbu, *w, h, out.failures);
  auto       tech        = *in.tech;
  tech.macros.insert_or_assign(kCheckMaster, m);
  // Frame with LEF-pitch tracks; the first core row and site sit at the origin.
  const auto                   margin = 2 * (*w + std::max({v.pitch, pin.pitch, escape.pitch}));
  const ph::Rect               die{double(-margin),
                     double(-margin),
                     double(int64_t(out.x_phases) * sw + *w + margin),
                     double((starts + 2) * h + margin)};
  const auto                   frame = lattice_frame(dbu,
                                   die,
                                                     {
                                       {"Y",  in.access.escape, escape},
                                       {"X",     in.access.pin,    pin},
                                       {"X", in.vertical_layer,      v}
  });
  const ph::Signal_metal_rules rules(tech, frame);
  std::set<std::string>        unsupported;
  for (int64_t start = 0; start < starts; ++start) {
    for (auto o : kOrientations) {
      Access_case c{size_t(start), o};
      c.compatible      = ph::row_access_matches(in.row_access, size_t(start), o, halves);
      c.pg_rows_match   = c.compatible;
      const auto before = out.failures.size();
      for (int64_t k = 0; c.compatible && k < int64_t(out.x_phases); ++k) {
        ph::Component cell{"u", kCheckMaster, Ann_place(double(k * sw), double(start * h), double(*w), double(2 * h))};
        cell.place.orientation = o;
        cell.place.status      = ph::Status::PLACED;
        const auto where       = std::format("start row {} {} x-phase {}", start, ph::orientation_name(o), k);
        std::map<std::string, std::vector<ph::Rect>> placed;
        std::vector<ph::Shape>                       obstacles;
        const auto                                   place = [&](const ph::Shape& shape) {
          const auto r = ph::snapped_dbu(ph::placed_rect(m, cell.place, dbu, shape.rect), 1e-6);
          if (!r) {
            fail(std::format("macro: {}: {} geometry is not on integral DBU", where, shape.layer));
          }
          return r.value_or(ph::Rect{});
        };
        for (const auto& [name, p] : m.pins) {
          for (const auto& shape : p.shapes) {
            placed[name].push_back(place(shape));
            if (is_pg(p)) {
              obstacles.push_back({shape.layer, placed[name].back()});
            }
          }
        }
        for (const auto& shape : m.obstructions) {
          obstacles.push_back({shape.layer, place(shape)});
        }
        // PG: a rail spanning the cell on each of the three row edges.
        for (int64_t edge = start; edge <= start + 2; ++edge) {
          const auto rail = rail_on_edge(m, placed, double(edge * h), cell.place.x, cell.place.x + *w);
          if (rail.empty()) {
            fail(std::format("PG: {}: no rail on core row edge {}", where, edge));
          }
          c.pg_rows_match = c.pg_rows_match && rail == ref.rails[edge % 2].pin;
        }
        for (const auto& name : signal_pins) {
          std::optional<ph::Pin_access_result> access;
          for (size_t half = 0; half < 2; ++half) {
            const auto     row = int64_t(ph::placed_row_of_half(size_t(start), 2, half, o));
            const ph::Rect band{die.xl, double(row * h), die.xh, double((row + 1) * h)};
            if (halves[half] == Row_access::vertical) {
              if (!vertical_access(placed[name], band, v, escape.pitch)) {
                fail(std::format("{}: {}: no vertical access in core row {} (off {} tracks or shorter than one {} pitch)",
                                 name,
                                 where,
                                 row,
                                 in.vertical_layer,
                                 in.access.escape));
              }
              continue;
            }
            if (!access) {
              access = ph::enumerate_pin_access(tech, frame, cell, name, obstacles, rules, kAccessBudget, in.access);
            }
            if (std::none_of(access->points.begin(), access->points.end(), [&](const auto& p) {
                  return p.y >= band.yl && p.y <= band.yh;
                })) {
              if (const auto why = horizontal_refusal(*access, unsupported)) {
                fail(std::format("{}: {}: no horizontal access in core row {} ({})", name, where, row, *why));
              }
            }
          }
        }
      }
      c.ok = c.compatible && out.failures.size() == before;
      out.cases.push_back(c);
    }
  }
  for (int parity = 0; parity < 2; ++parity) {
    if (std::none_of(out.cases.begin(), out.cases.end(), [&](const auto& c) {
          return c.compatible && int(c.start_row % 2) == parity;
        })) {
      fail(std::format("macro: no orientation fits {} start rows of the row-access convention", parity ? "odd" : "even"));
    }
  }
  out.unsupported.assign(unsupported.begin(), unsupported.end());
  out.ok = out.failures.empty() && out.unsupported.empty();
  return out;
}
}  // namespace livehd::domplace
