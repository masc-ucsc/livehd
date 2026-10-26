// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <tuple>

#include "third_party/misc/def/def/defrReader.hpp"
#include "third_party/misc/lef/lef/lefrReader.hpp"

namespace livehd::physical {
namespace {
using namespace LefDefParser;
// The Si2 readers have process-global callback/configuration state.
std::mutex reader_mutex;
struct FileCloser {
  void operator()(FILE* file) const noexcept { std::fclose(file); }
};
using File = std::unique_ptr<FILE, FileCloser>;
File open_file(const std::string& path) {
  File file(std::fopen(path.c_str(), "r"));
  if (!file) {
    throw std::runtime_error("cannot open physical input: " + path);
  }
  return file;
}
struct LefContext {
  Technology  tech;
  std::string macro, error;
};
int units_cb(lefrCallbackType_e, lefiUnits* units, lefiUserData data) {
  auto& c = *static_cast<LefContext*>(data);
  if (units->hasDatabase()) {
    c.tech.dbu = std::llround(units->databaseNumber());
  }
  return 0;
}
int manufacturing_grid_cb(lefrCallbackType_e, double grid, lefiUserData data) {
  auto& c = *static_cast<LefContext*>(data);
  if (!std::isfinite(grid) || grid <= 0 || (c.tech.manufacturing_grid > 0 && std::abs(c.tech.manufacturing_grid - grid) > 1e-12)) {
    c.error = "invalid or conflicting LEF MANUFACTURINGGRID";
    return 1;
  }
  c.tech.manufacturing_grid = grid;
  return 0;
}
int site_cb(lefrCallbackType_e, lefiSite* site, lefiUserData data) {
  auto& c                    = *static_cast<LefContext*>(data);
  c.tech.sites[site->name()] = {site->sizeX(), site->sizeY(), site->hasXSymmetry() != 0};
  return 0;
}
int layer_cb(lefrCallbackType_e, lefiLayer* layer, lefiUserData data) {
  if (!layer->hasType() || std::string_view(layer->type()) != "ROUTING") {
    return 0;
  }
  auto& tech = static_cast<LefContext*>(data)->tech;
  if (!tech.layers.contains(layer->name())) {
    tech.routing_layers.emplace_back(layer->name());
  }
  auto& l = tech.layers[layer->name()];
  if (layer->hasWidth()) {
    l.width = layer->width();
  }
  if (layer->hasXYPitch()) {
    l.pitch_x = layer->pitchX();
    l.pitch_y = layer->pitchY();
  } else if (layer->hasPitch()) {
    l.pitch_x = l.pitch_y = layer->pitch();
  }
  if (layer->hasXYOffset()) {
    l.offset_x   = layer->offsetX();
    l.offset_y   = layer->offsetY();
    l.has_offset = true;
  } else if (layer->hasOffset()) {
    l.offset_x = l.offset_y = layer->offset();
    l.has_offset            = true;
  }
  if (layer->hasResistance()) {
    l.resistance = layer->resistance();
  }
  if (layer->hasCapacitance()) {
    l.capacitance = layer->capacitance();
  }
  if (layer->hasEdgeCap()) {
    l.edge_cap = layer->edgeCap();
  }
  if (layer->hasDirection()) {
    l.direction = layer->direction();
  }
  return 0;
}
int macro_begin_cb(lefrCallbackType_e, const char* name, lefiUserData data) {
  auto& c = *static_cast<LefContext*>(data);
  c.macro = name;
  if (c.tech.macros.contains(name)) {
    c.error = "duplicate LEF macro: " + c.macro;
    return 1;
  }
  c.tech.macros.emplace(name, Macro{});
  return 0;
}
int macro_cb(lefrCallbackType_e, lefiMacro* macro, lefiUserData data) {
  auto& c = *static_cast<LefContext*>(data);
  auto& m = c.tech.macros.at(c.macro);
  if (!macro->hasSize()) {
    c.error = "LEF macro has no SIZE: " + c.macro;
    return 1;
  }
  m.width    = macro->sizeX();
  m.height   = macro->sizeY();
  m.origin_x = macro->originX();
  m.origin_y = macro->originY();
  if (macro->hasSiteName()) {
    m.site = macro->siteName();
  }
  if (macro->hasClass()) {
    m.klass = macro->macroClass();
  }
  m.symmetry_x = macro->hasXSymmetry();
  m.symmetry_y = macro->hasYSymmetry();
  return 0;
}
int pin_cb(lefrCallbackType_e, lefiPin* pin, lefiUserData data) {
  auto& c = *static_cast<LefContext*>(data);
  auto& p = c.tech.macros.at(c.macro).pins[pin->name()];
  if (pin->hasDirection()) {
    p.direction = pin->direction();
  }
  if (pin->hasUse()) {
    p.use = pin->use();
  }
  for (int port = 0; port < pin->numPorts(); ++port) {
    auto*       geometry = pin->port(port);
    std::string layer;
    for (int i = 0; i < geometry->numItems(); ++i) {
      if (geometry->itemType(i) == lefiGeomLayerE) {
        layer = geometry->getLayer(i);
      } else if (geometry->itemType(i) == lefiGeomRectE) {
        auto* r = geometry->getRect(i);
        p.shapes.push_back({
            layer,
            {r->xl, r->yl, r->xh, r->yh}
        });
      } else if (geometry->itemType(i) == lefiGeomPolygonE) {
        auto* polygon = geometry->getPolygon(i);
        // Use an interior vertex as the pin access point; retain a zero-size
        // rectangle instead of a bounding box whose center may be outside.
        if (polygon->numPoints > 0) {
          p.shapes.push_back({
              layer,
              {polygon->x[0], polygon->y[0], polygon->x[0], polygon->y[0]}
          });
        }
      } else if (geometry->itemType(i) == lefiGeomRectIterE || geometry->itemType(i) == lefiGeomPolygonIterE) {
        c.error = "unsupported iterated LEF pin geometry: " + c.macro + "/" + pin->name();
        return 1;
      }
    }
  }
  return 0;
}
int obstruction_cb(lefrCallbackType_e, lefiObstruction* obs, lefiUserData data) {
  auto&       c        = *static_cast<LefContext*>(data);
  auto*       geometry = obs->geometries();
  std::string layer;
  for (int i = 0; i < geometry->numItems(); ++i) {
    if (geometry->itemType(i) == lefiGeomLayerE) {
      layer = geometry->getLayer(i);
    } else if (!layer.empty()
               && (geometry->itemType(i) == lefiGeomRectE || geometry->itemType(i) == lefiGeomPolygonE
                   || geometry->itemType(i) == lefiGeomRectIterE || geometry->itemType(i) == lefiGeomPolygonIterE)) {
      auto& macro = c.tech.macros.at(c.macro);
      macro.blocked_layers.insert(layer);
      if (geometry->itemType(i) == lefiGeomRectE) {
        const auto* rect = geometry->getRect(i);
        macro.obstructions.push_back({
            layer,
            {rect->xl, rect->yl, rect->xh, rect->yh}
        });
      } else {
        // Nonrectangular/iterated OBS remain a conservative full-cell block.
        macro.conservative_obstructions.insert(layer);
      }
    }
  }
  return 0;
}
struct DefContext {
  Floorplan   fp;
  std::string error;
  bool        snapshot = false;
};
int def_units_cb(defrCallbackType_e, double units, defiUserData data) {
  static_cast<DefContext*>(data)->fp.dbu = std::llround(units);
  return 0;
}
int def_design_cb(defrCallbackType_e, const char* name, defiUserData data) {
  static_cast<DefContext*>(data)->fp.design = name;
  return 0;
}
int die_cb(defrCallbackType_e, defiBox* box, defiUserData data) {
  auto& c = *static_cast<DefContext*>(data);
  if (box->getPoint().numPoints != 2 && box->getPoint().numPoints != 4) {
    c.error = "only rectangular DEF DIEAREA is supported";
    return 1;
  }
  auto points = box->getPoint();
  c.fp.die    = {static_cast<double>(box->xl()),
                 static_cast<double>(box->yl()),
                 static_cast<double>(box->xh()),
                 static_cast<double>(box->yh())};
  for (int i = 0; i < points.numPoints; ++i) {
    if ((points.x[i] != c.fp.die.xl && points.x[i] != c.fp.die.xh) || (points.y[i] != c.fp.die.yl && points.y[i] != c.fp.die.yh)) {
      c.error = "nonrectangular DEF DIEAREA is unsupported";
      return 1;
    }
  }
  return 0;
}
int row_cb(defrCallbackType_e, defiRow* row, defiUserData data) {
  auto& c = *static_cast<DefContext*>(data);
  if (!row->hasDoStep() || row->yNum() != 1) {
    c.error = "DEF ROW must use DO count BY 1";
    return 1;
  }
  c.fp.rows.push_back({row->name(),
                       row->macro(),
                       std::llround(row->x()),
                       std::llround(row->y()),
                       std::llround(row->xNum()),
                       std::llround(row->xStep()),
                       parse_orientation(row->orientStr())});
  return 0;
}
int track_cb(defrCallbackType_e, defiTrack* track, defiUserData data) {
  auto& fp = static_cast<DefContext*>(data)->fp;
  for (int i = 0; i < track->numLayers(); ++i) {
    fp.tracks.push_back({track->macro(),
                         track->layer(i),
                         std::llround(track->x()),
                         std::llround(track->xNum()),
                         std::llround(track->xStep()),
                         track->firstTrackMask(),
                         track->sameMask() != 0});
  }
  return 0;
}
int component_cb(defrCallbackType_e, defiComponent* component, defiUserData data) {
  auto&     c = *static_cast<DefContext*>(data);
  Ann_place p;
  p.x           = component->placementX();
  p.y           = component->placementY();
  p.orientation = parse_orientation(component->placementOrientStr());
  p.status      = component->isFixed() || component->isCover() ? Status::FIXED
                  : component->isPlaced()                      ? Status::PLACED
                                                               : Status::UNPLACED;
  c.fp.components.push_back({component->id(), component->name(), p});
  return 0;
}
int terminal_cb(defrCallbackType_e, defiPin* pin, defiUserData data) {
  auto&    c = *static_cast<DefContext*>(data);
  Terminal p;
  p.name = pin->pinName();
  p.net  = pin->netName();
  if (pin->hasDirection()) {
    p.direction = pin->direction();
  }
  const auto placement = [&](const auto* item) {
    if (item->hasPlacement()) {
      p.x           = item->placementX();
      p.y           = item->placementY();
      p.orientation = parse_orientation(item->orientStr());
      p.status      = item->isFixed() || item->isCover() ? Status::FIXED : item->isPlaced() ? Status::PLACED : Status::UNPLACED;
    }
  };
  const auto shapes = [&](const auto* item) {
    for (int i = 0; i < item->numLayer(); ++i) {
      int xl, yl, xh, yh;
      item->bounds(i, &xl, &yl, &xh, &yh);
      p.shapes.push_back({
          item->layer(i),
          {double(xl), double(yl), double(xh), double(yh)}
      });
    }
  };
  placement(pin);
  shapes(pin);
  if (pin->numPorts() > 1 || (pin->numPorts() && pin->numLayer())) {
    c.error = "DEF PIN with multiple ports or mixed direct/PORT geometry is unsupported";
    return 1;
  }
  if (pin->numPorts() == 1) {
    const auto* port = pin->pinPort(0);
    if (port->numPolygons() || port->numVias()) {
      c.error = "DEF PIN PORT requires rectangular LAYER shapes";
      return 1;
    }
    // OpenROAD writes one PORT per ordinary IO. Its placement and rectangles
    // have the same representation as direct PIN geometry in our model.
    placement(port);
    shapes(port);
  }
  c.fp.terminals.push_back(std::move(p));
  return 0;
}
int net_cb(defrCallbackType_e, defiNet* net, defiUserData data) {
  Net n;
  n.name = net->name();
  for (int i = 0; i < net->numConnections(); ++i) {
    n.connections.push_back({net->instance(i), net->pin(i)});
  }
  static_cast<DefContext*>(data)->fp.nets.push_back(std::move(n));
  return 0;
}
int blockage_snapshot_cb(defrCallbackType_e, defiBlockage* input, defiUserData data) {
  auto&    c = *static_cast<DefContext*>(data);
  Blockage b;
  b.placement = input->hasPlacement();
  if (bool(input->hasLayer()) == b.placement) {
    c.error = "DEF blockage must specify exactly one of LAYER or PLACEMENT";
    return 1;
  }
  if (input->hasLayer()) {
    b.layer = input->layerName();
  }
  if (input->hasComponent()) {
    b.component = b.placement ? input->placementComponentName() : input->layerComponentName();
  }
  b.soft      = input->hasSoft();
  b.slots     = input->hasSlots();
  b.fills     = input->hasFills();
  b.pushdown  = input->hasPushdown();
  b.except_pg = input->hasExceptpgnet();
  if (input->hasPartial()) {
    b.partial_density = input->placementMaxDensity();
  }
  if (input->hasSpacing()) {
    b.spacing = input->minSpacing();
  }
  if (input->hasDesignRuleWidth()) {
    b.design_rule_width = input->designRuleWidth();
  }
  if (input->hasMask()) {
    b.mask = input->mask();
  }
  for (int i = 0; i < input->numRectangles(); ++i) {
    b.rectangles.push_back({double(input->xl(i)), double(input->yl(i)), double(input->xh(i)), double(input->yh(i))});
  }
  for (int i = 0; i < input->numPolygons(); ++i) {
    const auto points  = input->getPolygon(i);
    auto&      polygon = b.polygons.emplace_back();
    for (int j = 0; j < points.numPoints; ++j) {
      polygon.emplace_back(points.x[j], points.y[j]);
    }
  }
  c.fp.snapshot_blockages.push_back(std::move(b));
  return 0;
}
int blockage_cb(defrCallbackType_e, defiBlockage* blockage, defiUserData data) {
  auto& c = *static_cast<DefContext*>(data);
  if (c.snapshot) {
    return blockage_snapshot_cb(defrBlockageCbkType, blockage, data);
  }
  if (!blockage->hasPlacement() || blockage->hasLayer() || blockage->hasPartial() || blockage->hasPushdown() || blockage->hasSlots()
      || blockage->hasFills() || blockage->hasExceptpgnet() || blockage->hasSpacing() || blockage->hasDesignRuleWidth()
      || blockage->hasMask() || blockage->numPolygons() || !blockage->numRectangles()) {
    c.error = "DEF BLOCKAGES support only hard/soft rectangular PLACEMENT constraints";
    return 1;
  }
  Placement_blockage item;
  item.soft = blockage->hasSoft();
  if (blockage->hasComponent()) {
    item.component = blockage->placementComponentName();
  }
  for (int i = 0; i < blockage->numRectangles(); ++i) {
    const Rect rect{double(blockage->xl(i)), double(blockage->yl(i)), double(blockage->xh(i)), double(blockage->yh(i))};
    if (rect.xh <= rect.xl || rect.yh <= rect.yl) {
      c.error = "DEF placement blockage needs positive rectangular area";
      return 1;
    }
    item.rectangles.push_back(rect);
  }
  c.fp.blockages.push_back(std::move(item));
  return 0;
}
void validate_blockages(const Floorplan& fp) {
  if (!fp.snapshot_blockages.empty()) {
    throw std::invalid_argument("placement cannot consume read-only DEF snapshot blockages");
  }
  for (const auto& blockage : fp.blockages) {
    if (blockage.rectangles.empty()) {
      throw std::invalid_argument("placement blockage needs rectangles");
    }
    if (!blockage.component.empty()
        && std::none_of(fp.components.begin(), fp.components.end(), [&](const auto& c) { return c.name == blockage.component; })) {
      throw std::invalid_argument("placement blockage references absent component: " + blockage.component);
    }
    for (const auto& r : blockage.rectangles) {
      const auto coordinate = [](double x) { return std::isfinite(x) && x >= INT_MIN && x <= INT_MAX && std::trunc(x) == x; };
      if (!coordinate(r.xl) || !coordinate(r.yl) || !coordinate(r.xh) || !coordinate(r.yh) || r.xh <= r.xl || r.yh <= r.yl) {
        throw std::invalid_argument("placement blockage needs positive integer-DBU rectangles");
      }
    }
  }
}
std::string identifier(const std::string& text) {
  // The text is already DEF-escaped (the reader retains escapes): copy `\x` pairs as-is and escape only bare specials.
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = text[i];
    if (c == '\\') {
      out += '\\';
      // A trailing lone backslash would escape the following separator, so escape it itself.
      out += i + 1 < text.size() ? text[++i] : '\\';
      continue;
    }
    if (c <= ' ' || c == '(' || c == ')' || c == ';' || c == '+') {
      out += '\\';
    }
    out += char(c);
  }
  if (out.empty()) {
    throw std::runtime_error("empty DEF identifier");
  }
  return out;
}
const char* status_name(Status status) { return status == Status::FIXED || status == Status::LOCKED ? "FIXED" : "PLACED"; }
}  // namespace
std::string orientation_name(Orientation o) {
  static constexpr const char* names[] = {"N", "W", "S", "E", "FN", "FW", "FS", "FE"};
  return names[static_cast<unsigned>(o)];
}
Orientation parse_orientation(const std::string& name) {
  for (unsigned i = 0; i < 8; ++i) {
    if (orientation_name(static_cast<Orientation>(i)) == name) {
      return static_cast<Orientation>(i);
    }
  }
  if (name.empty()) {
    return Orientation::N;
  }
  throw std::runtime_error("unsupported orientation: " + name);
}
std::pair<double, double> orient_point(double x, double y, double w, double h, Orientation o) {
  switch (o) {
    case Orientation::N : return {x, y};
    case Orientation::S : return {w - x, h - y};
    case Orientation::FN: return {w - x, y};
    case Orientation::FS: return {x, h - y};
    case Orientation::W : return {h - y, x};
    case Orientation::E : return {y, w - x};
    case Orientation::FW: return {h - y, w - x};
    case Orientation::FE: return {y, x};
  }
  throw std::runtime_error("invalid orientation");
}
Technology read_lef(const std::vector<std::string>& paths) {
  std::scoped_lock lock(reader_mutex);
  LefContext       c;
  if (paths.empty()) {
    throw std::runtime_error("no LEF inputs");
  }
  for (const auto& path : paths) {
    auto file = open_file(path);
    lefrInit();
    lefrSetUnitsCbk(units_cb);
    lefrSetManufacturingCbk(manufacturing_grid_cb);
    lefrSetSiteCbk(site_cb);
    lefrSetLayerCbk(layer_cb);
    lefrSetMacroBeginCbk(macro_begin_cb);
    lefrSetMacroCbk(macro_cb);
    lefrSetPinCbk(pin_cb);
    lefrSetObstructionCbk(obstruction_cb);
    const int rc = lefrRead(file.get(), path.c_str(), &c);
    lefrClear();
    if (rc || !c.error.empty()) {
      throw std::runtime_error("LEF read failed: " + path + ": " + c.error);
    }
  }
  if (c.tech.dbu <= 0 || c.tech.sites.empty() || c.tech.macros.empty()) {
    throw std::runtime_error("LEF needs units, SITE and MACRO geometry");
  }
  return std::move(c.tech);
}
namespace {
Floorplan read_def_impl(const std::string& path, bool snapshot) {
  std::scoped_lock lock(reader_mutex);
  auto             file = open_file(path);
  DefContext       c;
  c.snapshot = snapshot;
  defrInit();
  defrSetUnitsCbk(def_units_cb);
  defrSetDesignCbk(def_design_cb);
  defrSetDieAreaCbk(die_cb);
  defrSetRowCbk(row_cb);
  defrSetTrackCbk(track_cb);
  defrSetComponentCbk(component_cb);
  defrSetPinCbk(terminal_cb);
  defrSetNetCbk(net_cb);
  defrSetBlockageCbk(blockage_cb);
  const int rc = defrRead(file.get(), path.c_str(), &c, 1);
  defrClear();
  if (rc || !c.error.empty()) {
    throw std::runtime_error("DEF read failed: " + path + ": " + c.error);
  }
  if (!snapshot) {
    validate_blockages(c.fp);
  }
  return std::move(c.fp);
}
}  // namespace
Floorplan read_def(const std::string& path) { return read_def_impl(path, false); }
Floorplan read_def_snapshot(const std::string& path) { return read_def_impl(path, true); }
void      write_def(const Floorplan& fp, const std::string& path) {
  if (!fp.snapshot_blockages.empty()) {
    throw std::runtime_error("placement DEF writer cannot discard snapshot blockages");
  }
  validate_blockages(fp);
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("cannot write DEF: " + path);
  }
  out << "VERSION 5.8 ;\nDIVIDERCHAR \".\" ;\nBUSBITCHARS \"[]\" ;\nDESIGN " << identifier(fp.design)
      << " ;\nUNITS DISTANCE MICRONS " << fp.dbu << " ;\nDIEAREA ( " << std::llround(fp.die.xl) << ' ' << std::llround(fp.die.yl)
      << " ) ( " << std::llround(fp.die.xh) << ' ' << std::llround(fp.die.yh) << " ) ;\n";
  for (const auto& row : fp.rows) {
    out << "ROW " << identifier(row.name) << ' ' << identifier(row.site) << ' ' << row.x << ' ' << row.y << ' '
        << orientation_name(row.orientation) << " DO " << row.count << " BY 1 STEP " << row.step << " 0 ;\n";
  }
  for (const auto& track : fp.tracks) {
    out << "TRACKS " << track.axis << ' ' << track.start << " DO " << track.count << " STEP " << track.step;
    if (track.mask) {
      out << " MASK " << track.mask << (track.same_mask ? " SAMEMASK" : "");
    }
    out << " LAYER " << identifier(track.layer) << " ;\n";
  }
  out << "COMPONENTS " << fp.components.size() << " ;\n";
  for (const auto& c : fp.components) {
    out << "- " << identifier(c.name) << ' ' << identifier(c.macro);
    if (c.place.status == Status::UNPLACED) {
      out << " + UNPLACED";
    } else {
      out << " + " << status_name(c.place.status) << " ( " << c.place.x << ' ' << c.place.y << " ) "
          << orientation_name(c.place.orientation);
    }
    out << " ;\n";
  }
  out << "END COMPONENTS\nPINS " << fp.terminals.size() << " ;\n";
  for (const auto& p : fp.terminals) {
    out << "- " << identifier(p.name) << " + NET " << identifier(p.net) << " + DIRECTION " << p.direction << " + USE SIGNAL";
    for (const auto& shape : p.shapes) {
      out << " + LAYER " << identifier(shape.layer) << " ( " << std::llround(shape.rect.xl) << ' ' << std::llround(shape.rect.yl)
          << " ) ( " << std::llround(shape.rect.xh) << ' ' << std::llround(shape.rect.yh) << " )";
    }
    out << " + " << status_name(p.status) << " ( " << p.x << ' ' << p.y << " ) " << orientation_name(p.orientation) << " ;\n";
  }
  out << "END PINS\nNETS " << fp.nets.size() << " ;\n";
  for (const auto& net : fp.nets) {
    out << "- " << identifier(net.name);
    for (const auto& c : net.connections) {
      out << " ( " << identifier(c.instance) << ' ' << identifier(c.pin) << " )";
    }
    out << " ;\n";
  }
  out << "END NETS\n";
  if (!fp.blockages.empty()) {
    out << "BLOCKAGES " << fp.blockages.size() << " ;\n";
    for (const auto& blockage : fp.blockages) {
      out << "- PLACEMENT";
      if (blockage.soft) {
        out << " + SOFT";
      }
      if (!blockage.component.empty()) {
        out << " + COMPONENT " << identifier(blockage.component);
      }
      for (const auto& r : blockage.rectangles) {
        out << " RECT ( " << std::llround(r.xl) << ' ' << std::llround(r.yl) << " ) ( " << std::llround(r.xh) << ' '
            << std::llround(r.yh) << " )";
      }
      out << " ;\n";
    }
    out << "END BLOCKAGES\n";
  }
  out << "END DESIGN\n";
  out.close();
  if (!out) {
    throw std::runtime_error("failed writing DEF: " + path);
  }
}
void verify_def_roundtrip(const Floorplan& a, const Floorplan& b) {
  if (a.design != b.design || a.dbu != b.dbu || a.die != b.die || a.rows != b.rows || a.tracks != b.tracks
      || a.terminals != b.terminals || a.nets != b.nets || a.blockages != b.blockages
      || a.snapshot_blockages != b.snapshot_blockages || a.components.size() != b.components.size()) {
    const auto  field = a.design != b.design                           ? "design"
                        : a.dbu != b.dbu                               ? "units"
                        : a.die != b.die                               ? "die"
                        : a.rows != b.rows                             ? "rows"
                        : a.tracks != b.tracks                         ? "tracks"
                        : a.terminals != b.terminals                   ? "terminals"
                        : a.nets != b.nets                             ? "nets"
                        : a.blockages != b.blockages                   ? "blockages"
                        : a.snapshot_blockages != b.snapshot_blockages ? "snapshot blockages"
                                                                       : "component count";
    std::string detail;
    if (a.nets != b.nets) {
      detail = " (nets " + std::to_string(a.nets.size()) + " -> " + std::to_string(b.nets.size()) + ")";
      for (size_t i = 0; i < std::min(a.nets.size(), b.nets.size()); ++i) {
        if (a.nets[i] != b.nets[i]) {
          detail += " at " + std::to_string(i) + ": " + a.nets[i].name + " -> " + b.nets[i].name + " connections "
                    + std::to_string(a.nets[i].connections.size()) + " -> " + std::to_string(b.nets[i].connections.size());
          for (size_t j = 0; j < std::min(a.nets[i].connections.size(), b.nets[i].connections.size()); ++j) {
            if (a.nets[i].connections[j] != b.nets[i].connections[j]) {
              detail += " endpoint " + a.nets[i].connections[j].instance + ":" + a.nets[i].connections[j].pin + " -> "
                        + b.nets[i].connections[j].instance + ":" + b.nets[i].connections[j].pin;
              break;
            }
          }
          break;
        }
      }
    }
    throw std::runtime_error(std::string("DEF roundtrip changed floorplan/connectivity: ") + field + detail);
  }
  for (size_t i = 0; i < a.components.size(); ++i) {
    auto c        = a.components[i];
    c.place.width = c.place.height = 0;
    if (c.place.status == Status::LOCKED) {
      c.place.status = Status::FIXED;
    }
    if (c != b.components[i]) {
      throw std::runtime_error("DEF roundtrip changed component: " + c.name);
    }
  }
}
namespace {
// Sweep large physical-only obstacle inventories without subtracting every
// previously accepted rectangle. Fixed bodies own overlap, then hard masks,
// then soft masks. The small-input path below retains its historical partition.
std::vector<Placement_exclusion> sweep_exclusions(const Floorplan& fp, const Rect& domain, const std::vector<Rect>& occupied) {
  struct Event {
    double y, xl, xh;
    size_t kind;
    int    delta;
  };
  std::vector<Event>  events;
  std::vector<double> xs;
  const auto          add = [&](const Rect& r, size_t kind) {
    const Rect c{std::max(r.xl, domain.xl), std::max(r.yl, domain.yl), std::min(r.xh, domain.xh), std::min(r.yh, domain.yh)};
    if (c.xl < c.xh && c.yl < c.yh) {
      events.push_back({c.yl, c.xl, c.xh, kind, 1});
      events.push_back({c.yh, c.xl, c.xh, kind, -1});
      xs.push_back(c.xl);
      xs.push_back(c.xh);
    }
  };
  for (const auto& b : fp.blockages) {
    for (const auto& r : b.rectangles) {
      add(r, b.soft ? 1 : 0);
    }
  }
  for (const auto& r : occupied) {
    add(r, 2);
  }
  if (events.empty()) {
    return {};
  }
  std::sort(xs.begin(), xs.end());
  xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
  std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
  struct Node {
    std::array<int, 3> cover{};
    unsigned           any = 0;
  };
  std::vector<Node> tree(4 * xs.size());
  const auto        update = [&](auto&& self, size_t n, size_t l, size_t h, size_t ql, size_t qh, size_t kind, int delta) -> void {
    if (qh <= l || h <= ql) {
      return;
    }
    auto& t = tree[n];
    if (ql <= l && h <= qh) {
      t.cover[kind] += delta;
    } else {
      const auto mid = (l + h) / 2;
      self(self, 2 * n, l, mid, ql, qh, kind, delta);
      self(self, 2 * n + 1, mid, h, ql, qh, kind, delta);
    }
    t.any = h - l > 1 ? tree[2 * n].any | tree[2 * n + 1].any : 0;
    for (size_t k = 0; k < 3; ++k) {
      if (t.cover[k] > 0) {
        t.any |= 1U << k;
      }
    }
  };
  struct Span {
    double         xl, xh;
    Placement_mask kind;
  };
  std::vector<Span> spans;
  const auto        emit = [&](size_t l, size_t h, Placement_mask kind) {
    if (!spans.empty() && spans.back().xh == xs[l] && spans.back().kind == kind) {
      spans.back().xh = xs[h];
    } else {
      spans.push_back({xs[l], xs[h], kind});
    }
  };
  const auto collect = [&](auto&& self, size_t n, size_t l, size_t h, std::array<int, 3> cover) -> void {
    const auto& t = tree[n];
    for (size_t k = 0; k < 3; ++k) {
      cover[k] += t.cover[k];
    }
    if (cover[2] || (!(cover[0] || cover[1]) && !(t.any & 3))) {
      return;
    }
    if (cover[0] && !(t.any & 4)) {
      emit(l, h, Placement_mask::hard);
      return;
    }
    if (cover[1] && !(t.any & 5)) {
      emit(l, h, Placement_mask::soft);
      return;
    }
    if (h - l > 1) {
      const auto mid = (l + h) / 2;
      self(self, 2 * n, l, mid, cover);
      self(self, 2 * n + 1, mid, h, cover);
    }
  };
  using Key = std::tuple<double, double, Placement_mask>;
  std::map<Key, size_t>            previous;
  std::vector<Placement_exclusion> result;
  for (size_t i = 0; i < events.size();) {
    const double y = events[i].y;
    do {
      const auto& e = events[i++];
      const auto  l = std::lower_bound(xs.begin(), xs.end(), e.xl) - xs.begin();
      const auto  h = std::lower_bound(xs.begin(), xs.end(), e.xh) - xs.begin();
      update(update, 1, 0, xs.size() - 1, l, h, e.kind, e.delta);
    } while (i < events.size() && events[i].y == y);
    if (i == events.size()) {
      break;
    }
    spans.clear();
    collect(collect, 1, 0, xs.size() - 1, {});
    std::map<Key, size_t> next;
    for (const auto& s : spans) {
      const Key  key{s.xl, s.xh, s.kind};
      const auto it = previous.find(key);
      if (it != previous.end() && result[it->second].rect.yh == y) {
        result[it->second].rect.yh = events[i].y;
        next.emplace(key, it->second);
      } else {
        next.emplace(key, result.size());
        result.push_back({
            {s.xl, y, s.xh, events[i].y},
            s.kind
        });
      }
    }
    previous = std::move(next);
  }
  std::stable_partition(result.begin(), result.end(), [](const auto& e) { return e.kind == Placement_mask::hard; });
  return result;
}
}  // namespace
std::vector<Placement_exclusion> placement_exclusions(const Floorplan& fp, const Rect& domain,
                                                      const std::vector<Rect>& fixed_occupancy) {
  validate_blockages(fp);
  if (!std::isfinite(domain.xl) || !std::isfinite(domain.yl) || !std::isfinite(domain.xh) || !std::isfinite(domain.yh)
      || domain.xh <= domain.xl || domain.yh <= domain.yl) {
    throw std::invalid_argument("invalid placement exclusion domain");
  }
  const auto clip = [&](const Rect& r) {
    return Rect{std::max(r.xl, domain.xl), std::max(r.yl, domain.yl), std::min(r.xh, domain.xh), std::min(r.yh, domain.yh)};
  };
  std::vector<Rect> occupied;
  for (const auto& r : fixed_occupancy) {
    if (!std::isfinite(r.xl) || !std::isfinite(r.yl) || !std::isfinite(r.xh) || !std::isfinite(r.yh) || r.xh < r.xl
        || r.yh < r.yl) {
      throw std::invalid_argument("invalid fixed placement occupancy");
    }
    const auto c = clip(r);
    if (c.xh > c.xl && c.yh > c.yl) {
      occupied.push_back(c);
    }
  }
  for (const auto& blockage : fp.blockages) {
    if (!blockage.component.empty()) {
      const auto component
          = std::find_if(fp.components.begin(), fp.components.end(), [&](const auto& c) { return c.name == blockage.component; });
      if (component->place.status != Status::FIXED && component->place.status != Status::LOCKED) {
        throw std::invalid_argument("placement blockage associated with movable component: " + blockage.component);
      }
    }
  }
  size_t rectangles = occupied.size();
  for (const auto& blockage : fp.blockages) {
    rectangles += blockage.rectangles.size();
  }
  if (rectangles > 1024) {
    return sweep_exclusions(fp, domain, occupied);
  }
  std::vector<Placement_exclusion> result;
  // Hard geometry owns overlap with soft geometry. Each accepted piece is
  // subtracted from subsequent rectangles, so density sees a union once.
  for (bool soft : {false, true}) {
    for (const auto& blockage : fp.blockages) {
      if (blockage.soft != soft) {
        continue;
      }
      for (const auto& original : blockage.rectangles) {
        const auto        clipped = clip(original);
        std::vector<Rect> pieces;
        if (clipped.xh > clipped.xl && clipped.yh > clipped.yl) {
          pieces.push_back(clipped);
        }
        for (const auto& obstacle : occupied) {
          std::vector<Rect> next;
          for (const auto& r : pieces) {
            const Rect overlap{std::max(r.xl, obstacle.xl),
                               std::max(r.yl, obstacle.yl),
                               std::min(r.xh, obstacle.xh),
                               std::min(r.yh, obstacle.yh)};
            if (overlap.xh <= overlap.xl || overlap.yh <= overlap.yl) {
              next.push_back(r);
              continue;
            }
            for (const Rect piece : {
                     Rect{      r.xl,       r.yl, overlap.xl,       r.yh},
                     Rect{overlap.xh,       r.yl,       r.xh,       r.yh},
                     Rect{overlap.xl,       r.yl, overlap.xh, overlap.yl},
                     Rect{overlap.xl, overlap.yh, overlap.xh,       r.yh}
            }) {
              if (piece.xh > piece.xl && piece.yh > piece.yl) {
                next.push_back(piece);
              }
            }
          }
          pieces = std::move(next);
          if (pieces.empty()) {
            break;
          }
        }
        for (const auto& r : pieces) {
          result.push_back({r, soft ? Placement_mask::soft : Placement_mask::hard});
          occupied.push_back(r);
        }
      }
    }
  }
  return result;
}
Floorplan rectangular_placement_frame(const Floorplan& original, const Technology& technology) {
  validate_blockages(original);
  if (original.rows.empty() || original.dbu != technology.dbu) {
    throw std::invalid_argument("placement frame needs original rows and matching LEF/DEF units");
  }
  const auto& first = original.rows.front();
  const auto  site  = technology.sites.find(first.site);
  if (site == technology.sites.end()) {
    throw std::invalid_argument("placement frame SITE missing from LEF: " + first.site);
  }
  const auto width = site->second.width * technology.dbu, height = site->second.height * technology.dbu;
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 || width > INT_MAX || height > INT_MAX) {
    throw std::invalid_argument("placement frame needs finite positive site dimensions in DEF range");
  }
  const int64_t sw = std::llround(width);
  const int64_t rh = std::llround(height);
  if (sw <= 0 || rh <= 0) {
    throw std::invalid_argument("placement frame needs positive site dimensions");
  }
  int64_t                                    xl = first.x, yl = first.y, xh = first.x, yh = first.y;
  std::map<int64_t, std::vector<const Row*>> levels;
  for (const auto& row : original.rows) {
    if (row.site != first.site || row.step != sw || row.count <= 0 || row.count > INT_MAX / sw
        || (row.orientation != Orientation::N && row.orientation != Orientation::FS) || row.x < original.die.xl
        || row.y < original.die.yl || row.x + row.count * sw > original.die.xh || row.y + rh > original.die.yh) {
      throw std::invalid_argument("placement frame requires positive on-grid N/FS row segments within DIEAREA");
    }
    xl = std::min(xl, row.x);
    yl = std::min(yl, row.y);
    xh = std::max(xh, row.x + row.count * sw);
    yh = std::max(yh, row.y + rh);
    levels[row.y].push_back(&row);
  }
  if ((xh - xl) % sw || (yh - yl) % rh || (yh - yl) / rh > 1000000) {
    throw std::invalid_argument("placement frame row bounds are off-grid or exceed the supported row count");
  }
  for (const auto& row : original.rows) {
    if ((row.x - xl) % sw || (row.y - yl) % rh) {
      throw std::invalid_argument("placement frame row segments are off the common site lattice");
    }
  }
  Floorplan result = original;
  result.rows.clear();
  Placement_blockage                            missing;
  std::map<std::pair<int64_t, int64_t>, size_t> previous;
  for (int64_t y = yl; y < yh; y += rh) {
    auto& segments = levels[y];
    std::sort(segments.begin(), segments.end(), [](const auto* a, const auto* b) { return a->x < b->x; });
    const auto                               orientation = segments.empty() ? Orientation::N : segments.front()->orientation;
    int64_t                                  cursor      = xl;
    std::vector<std::pair<int64_t, int64_t>> gaps;
    for (const auto* row : segments) {
      if (row->orientation != orientation || row->x < cursor) {
        throw std::invalid_argument("placement frame has overlapping or differently oriented row segments");
      }
      if (row->x > cursor) {
        gaps.emplace_back(cursor, row->x);
      }
      cursor = row->x + row->count * sw;
    }
    if (cursor < xh) {
      gaps.emplace_back(cursor, xh);
    }
    std::map<std::pair<int64_t, int64_t>, size_t> current;
    for (const auto& gap : gaps) {
      const auto   found = previous.find(gap);
      const size_t index = found == previous.end() ? missing.rectangles.size() : found->second;
      if (found == previous.end()) {
        missing.rectangles.push_back({double(gap.first), double(y), double(gap.second), double(y + rh)});
      } else {
        missing.rectangles[index].yh = y + rh;
      }
      current.emplace(gap, index);
    }
    previous = std::move(current);
    result.rows.push_back(
        {"__lhd_rect_row_" + std::to_string(result.rows.size()), first.site, xl, y, (xh - xl) / sw, sw, orientation});
  }
  if (!missing.rectangles.empty()) {
    result.blockages.push_back(std::move(missing));
  }
  return result;
}
}  // namespace livehd::physical
