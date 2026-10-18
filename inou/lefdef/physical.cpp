// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>

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
      c.tech.macros.at(c.macro).blocked_layers.insert(layer);
    }
  }
  return 0;
}
struct DefContext {
  Floorplan   fp;
  std::string error;
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
int blockage_cb(defrCallbackType_e, defiBlockage*, defiUserData data) {
  static_cast<DefContext*>(data)->error = "DEF BLOCKAGES require row-segment support and are not yet supported";
  return 1;
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
Floorplan read_def(const std::string& path) {
  std::scoped_lock lock(reader_mutex);
  auto             file = open_file(path);
  DefContext       c;
  defrInit();
  defrSetUnitsCbk(def_units_cb);
  defrSetDesignCbk(def_design_cb);
  defrSetDieAreaCbk(die_cb);
  defrSetRowCbk(row_cb);
  defrSetComponentCbk(component_cb);
  defrSetPinCbk(terminal_cb);
  defrSetNetCbk(net_cb);
  defrSetBlockageCbk(blockage_cb);
  const int rc = defrRead(file.get(), path.c_str(), &c, 1);
  defrClear();
  if (rc || !c.error.empty()) {
    throw std::runtime_error("DEF read failed: " + path + ": " + c.error);
  }
  return std::move(c.fp);
}
void write_def(const Floorplan& fp, const std::string& path) {
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
  out << "END NETS\nEND DESIGN\n";
  out.close();
  if (!out) {
    throw std::runtime_error("failed writing DEF: " + path);
  }
}
void verify_def_roundtrip(const Floorplan& a, const Floorplan& b) {
  if (a.design != b.design || a.dbu != b.dbu || a.die != b.die || a.rows != b.rows || a.terminals != b.terminals || a.nets != b.nets
      || a.components.size() != b.components.size()) {
    const auto  field = a.design != b.design         ? "design"
                        : a.dbu != b.dbu             ? "units"
                        : a.die != b.die             ? "die"
                        : a.rows != b.rows           ? "rows"
                        : a.terminals != b.terminals ? "terminals"
                        : a.nets != b.nets           ? "nets"
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
}  // namespace livehd::physical
