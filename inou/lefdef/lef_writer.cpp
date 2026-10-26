// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lef_writer.hpp"

#include <format>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace livehd::physical {
namespace {
// Exact decimal microns: every LEF DATABASE MICRONS value (100..20000) divides
// a power of ten, so v DBU is v * scale / unit with `digits` decimals.
class Microns {
public:
  Microns(int64_t dbu, const std::string& where) : dbu_(dbu), where_(where) {
    int64_t unit = 1;
    for (int digits = 0; dbu > 0 && digits <= 9; ++digits, unit *= 10) {
      if (unit % dbu == 0) {
        scale_  = unit / dbu;
        unit_   = unit;
        digits_ = size_t(digits);
        return;
      }
    }
    throw std::invalid_argument("LEF writer: DBU " + std::to_string(dbu) + " has no exact decimal micron form");
  }
  std::string operator()(double microns) const {
    const auto value = rounded_dbu(microns * dbu_, 1e-6);
    if (!value) {
      throw std::invalid_argument(where_ + ": coordinate " + std::format("{}", microns) + " is not an integral DBU");
    }
    const auto scaled    = *value * scale_;
    const auto magnitude = scaled < 0 ? -scaled : scaled;
    auto       text      = std::string(scaled < 0 ? "-" : "") + std::to_string(magnitude / unit_);
    if (const auto fraction = magnitude % unit_) {
      auto digits = std::to_string(fraction);
      digits.insert(0, digits_ - digits.size(), '0');
      digits.erase(digits.find_last_not_of('0') + 1);
      text += "." + digits;
    }
    return text;
  }
  void        set_where(const std::string& where) { where_ = where; }
  const auto& where() const { return where_; }

private:
  int64_t     dbu_, scale_ = 1, unit_ = 1;
  size_t      digits_ = 0;
  std::string where_;
};
// LEF words: nonempty, single-space separated, free of statement and comment
// delimiters. Names (macro, pin, layer, site) are one word.
const std::string& lef_text(const std::string& text, bool name, const std::string& where) {
  bool ok = !text.empty() && text.front() != ' ' && text.back() != ' ' && text.find("  ") == std::string::npos;
  for (char ch : text) {
    ok = ok && ch != ';' && ch != '"' && ch != '#' && ch != '\n' && ch != '\r' && ch != '\t' && !(name && ch == ' ');
  }
  if (!ok) {
    throw std::invalid_argument(where + ": '" + text + "' is not a LEF " + (name ? "name" : "keyword"));
  }
  return text;
}
void write_rects(std::string& out, const std::vector<Shape>& shapes, const Microns& um, std::string_view indent) {
  const std::string* layer = nullptr;
  for (const auto& shape : shapes) {
    const auto& r = shape.rect;
    if (r.empty()) {
      throw std::invalid_argument(um.where() + ": degenerate rectangle on " + shape.layer);
    }
    if (!layer || *layer != shape.layer) {
      layer  = &shape.layer;
      out   += std::string(indent) + "LAYER " + lef_text(shape.layer, true, um.where()) + " ;\n";
    }
    out += std::string(indent) + "  RECT " + um(r.xl) + " " + um(r.yl) + " " + um(r.xh) + " " + um(r.yh) + " ;\n";
  }
}
}  // namespace
std::string write_lef_macros(const std::map<std::string, Macro>& macros, int64_t dbu) {
  Microns     um(dbu, "LEF writer");
  std::string out = "VERSION 5.8 ;\nBUSBITCHARS \"[]\" ;\nDIVIDERCHAR \"/\" ;\n";
  for (const auto& [name, m] : macros) {
    um.set_where("LEF macro " + name);
    lef_text(name, true, um.where());
    if (!m.conservative_obstructions.empty()) {
      throw std::invalid_argument(um.where() + ": polygon obstructions have no rectangular LEF form");
    }
    std::set<std::string> obstruction_layers;
    for (const auto& shape : m.obstructions) {
      obstruction_layers.insert(shape.layer);
    }
    if (obstruction_layers != m.blocked_layers) {
      throw std::invalid_argument(um.where() + ": OBS layers must match the obstruction rectangles");
    }
    if (!(m.width > 0) || !(m.height > 0)) {
      throw std::invalid_argument(um.where() + ": SIZE must be positive");
    }
    out += "MACRO " + name + "\n";
    if (!m.klass.empty()) {
      out += "  CLASS " + lef_text(m.klass, false, um.where()) + " ;\n";
    }
    out += "  ORIGIN " + um(m.origin_x) + " " + um(m.origin_y) + " ;\n";
    out += "  SIZE " + um(m.width) + " BY " + um(m.height) + " ;\n";
    if (m.symmetry_x || m.symmetry_y) {
      out += std::string("  SYMMETRY") + (m.symmetry_x ? " X" : "") + (m.symmetry_y ? " Y" : "") + " ;\n";
    }
    if (!m.site.empty()) {
      out += "  SITE " + lef_text(m.site, true, um.where()) + " ;\n";
    }
    for (const auto& [pin_name, pin] : m.pins) {
      lef_text(pin_name, true, um.where());
      if (pin.shapes.empty()) {
        throw std::invalid_argument(um.where() + ": pin " + pin_name + " has no geometry");
      }
      out += "  PIN " + pin_name + "\n";
      if (!pin.direction.empty()) {
        out += "    DIRECTION " + lef_text(pin.direction, false, um.where()) + " ;\n";
      }
      if (!pin.use.empty()) {
        out += "    USE " + lef_text(pin.use, true, um.where()) + " ;\n";
      }
      out += "    PORT\n";
      write_rects(out, pin.shapes, um, "      ");
      out += "    END\n  END " + pin_name + "\n";
    }
    if (!m.obstructions.empty()) {
      out += "  OBS\n";
      write_rects(out, m.obstructions, um, "    ");
      out += "  END\n";
    }
    out += "END " + name + "\n";
  }
  out += "END LIBRARY\n";
  return out;
}
}  // namespace livehd::physical
