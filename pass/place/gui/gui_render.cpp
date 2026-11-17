// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>

#include "rapidjson/document.h"

namespace livehd::placement::gui {

namespace {
struct Rgb {
  std::uint8_t r, g, b;
};
constexpr Rgb kBg{16, 19, 26}, kDie{11, 14, 20}, kLine{44, 52, 70}, kCell{91, 155, 255}, kFixed{107, 114, 133},
    kMacro{240, 164, 60}, kMacroFixed{185, 120, 42}, kIo{63, 207, 130}, kRegion{74, 83, 106}, kPin{255, 255, 255},
    kCentroid{255, 92, 138}, kSeq{210, 140, 255};

struct Canvas {
  int         w, h;
  std::string px;
  Canvas(int width, int height, Rgb bg) : w(width), h(height), px(size_t(width) * size_t(height) * 3, 0) {
    for (size_t i = 0; i < px.size(); i += 3) {
      px[i]     = char(bg.r);
      px[i + 1] = char(bg.g);
      px[i + 2] = char(bg.b);
    }
  }
  void set(int x, int y, Rgb c, double alpha = 1) {
    if (x < 0 || y < 0 || x >= w || y >= h) {
      return;
    }
    auto* p = reinterpret_cast<std::uint8_t*>(px.data()) + (size_t(y) * size_t(w) + size_t(x)) * 3;
    p[0]    = std::uint8_t(p[0] + (c.r - p[0]) * alpha);
    p[1]    = std::uint8_t(p[1] + (c.g - p[1]) * alpha);
    p[2]    = std::uint8_t(p[2] + (c.b - p[2]) * alpha);
  }
  void fill(int x0, int y0, int x1, int y1, Rgb c, double alpha = 1) {
    for (int y = std::max(0, y0); y <= std::min(h - 1, y1); ++y) {
      for (int x = std::max(0, x0); x <= std::min(w - 1, x1); ++x) {
        set(x, y, c, alpha);
      }
    }
  }
  void outline(int x0, int y0, int x1, int y1, Rgb c) {
    for (int x = x0; x <= x1; ++x) {
      set(x, y0, c);
      set(x, y1, c);
    }
    for (int y = y0; y <= y1; ++y) {
      set(x0, y, c);
      set(x1, y, c);
    }
  }
};

std::uint32_t crc32(const std::string& data, std::uint32_t crc = 0) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? 0xedb88320U ^ (c >> 1) : c >> 1;
      }
      t[n] = c;
    }
    return t;
  }();
  crc = ~crc;
  for (const unsigned char byte : data) {
    crc = table[(crc ^ byte) & 0xff] ^ (crc >> 8);
  }
  return ~crc;
}
void be32(std::string& s, std::uint32_t v) {
  for (int i = 3; i >= 0; --i) {
    s += char((v >> (8 * i)) & 0xff);
  }
}
void chunk(std::string& out, const char* type, const std::string& data) {
  be32(out, std::uint32_t(data.size()));
  std::string body  = std::string(type, 4) + data;
  out              += body;
  be32(out, crc32(body));
}
}  // namespace

namespace {
// A minimal zlib stream with no library behind it (two zlib implementations in one static link, LLVM's zlib-ng and a
// separate zlib, collide at link time): one deflate block with the fixed Huffman code, literals and run matches at
// distance 1. Fed PNG's Sub-filtered rows, flat parts of a floorplan picture are runs of zeros and shrink to a few percent.
class Bit_writer {
public:
  void bits(std::uint32_t value, int count) {  // LSB first
    for (int i = 0; i < count; ++i) {
      acc_ |= ((value >> i) & 1U) << used_;
      if (++used_ == 8) {
        out_ += char(acc_);
        acc_ = used_ = 0;
      }
    }
  }
  void code(std::uint32_t value, int count) {  // Huffman codes go out MSB first
    for (int i = count - 1; i >= 0; --i) {
      bits((value >> i) & 1U, 1);
    }
  }
  std::string finish() {
    if (used_) {
      out_ += char(acc_);
    }
    return std::move(out_);
  }

private:
  std::string   out_;
  std::uint32_t acc_ = 0;
  int           used_ = 0;
};
void fixed_symbol(Bit_writer& w, unsigned sym) {
  if (sym < 144) {
    w.code(0x30 + sym, 8);
  } else if (sym < 256) {
    w.code(0x190 + (sym - 144), 9);
  } else if (sym < 280) {
    w.code(sym - 256, 7);
  } else {
    w.code(0xC0 + (sym - 280), 8);
  }
}
std::string zlib_fixed(const std::string& raw, size_t row_bytes) {
  static constexpr std::array<unsigned, 30> dist_base{1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static constexpr std::array<int, 30>      dist_extra{0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
  static constexpr std::array<unsigned, 29> base{3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static constexpr std::array<int, 29>      extra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
  Bit_writer                                w;
  w.bits(1, 1);  // the only block
  w.bits(1, 2);  // fixed Huffman
  for (size_t i = 0; i < raw.size();) {
    // Two places to copy from: the byte before (a run) and the same place one row up (an unchanged stretch of picture).
    size_t run = 0, same = 0;
    if (i > 0) {
      while (i + run < raw.size() && run < 258 && raw[i + run] == raw[i - 1]) {
        ++run;
      }
    }
    if (row_bytes <= 32768 && i >= row_bytes) {
      while (i + same < raw.size() && same < 258 && raw[i + same] == raw[i + same - row_bytes]) {
        ++same;
      }
    }
    const size_t length = std::max(run, same);
    if (length >= 3) {
      const size_t distance = same > run ? row_bytes : 1;
      size_t       k = base.size() - 1;
      while (base[k] > length) {
        --k;
      }
      fixed_symbol(w, unsigned(257 + k));
      w.bits(unsigned(length - base[k]), extra[k]);
      size_t d = dist_base.size() - 1;
      while (dist_base[d] > distance) {
        --d;
      }
      w.code(unsigned(d), 5);
      w.bits(unsigned(distance - dist_base[d]), dist_extra[d]);
      i += length;
    } else {
      fixed_symbol(w, static_cast<unsigned char>(raw[i]));
      ++i;
    }
  }
  fixed_symbol(w, 256);
  std::string z = "\x78\x01" + w.finish();
  std::uint32_t a = 1, b = 0;
  for (const unsigned char c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  be32(z, (b << 16) | a);
  return z;
}
}  // namespace

std::string encode_png(int width, int height, const std::string& rgb) {
  // PNG filter 1 (Sub): each byte minus the same channel of the pixel to its left. A flat area becomes zeros.
  std::string raw;
  raw.reserve(size_t(height) * (size_t(width) * 3 + 1));
  for (int y = 0; y < height; ++y) {
    raw += char(1);
    const auto* row = reinterpret_cast<const unsigned char*>(rgb.data()) + size_t(y) * size_t(width) * 3;
    for (size_t i = 0; i < size_t(width) * 3; ++i) {
      raw += char(static_cast<unsigned char>(row[i] - (i >= 3 ? row[i - 3] : 0)));
    }
  }
  const std::string z = zlib_fixed(raw, size_t(width) * 3 + 1);
  std::string out = "\x89PNG\r\n\x1a\n";
  std::string ihdr;
  be32(ihdr, std::uint32_t(width));
  be32(ihdr, std::uint32_t(height));
  ihdr += char(8);  // bit depth
  ihdr += char(2);  // truecolor
  ihdr += char(0);
  ihdr += char(0);
  ihdr += char(0);
  chunk(out, "IHDR", ihdr);
  chunk(out, "IDAT", z);
  chunk(out, "IEND", "");
  return out;
}

Render_options parse_layers(const std::string& csv, Render_options o) {
  std::stringstream ss(csv);
  std::string       item;
  while (std::getline(ss, item, ',')) {
    bool on = true;
    if (!item.empty() && item[0] == '-') {
      on   = false;
      item = item.substr(1);
    }
    if (item == "cells") {
      o.comb = o.regs = on;
    } else if (item == "comb") {
      o.comb = on;
    } else if (item == "regs") {
      o.regs = on;
    } else if (item == "macros") {
      o.macros = on;
    } else if (item == "io") {
      o.io = on;
    } else if (item == "chunks") {
      o.chunks = on;
    } else if (item == "regions") {
      o.regions = on;
    } else if (item == "centroids") {
      o.centroids = on;
    } else if (item == "pin_sides") {
      o.pin_sides = on;
    } else if (item == "congestion") {
      o.congestion = on;
    } else if (item == "pins") {
      o.pins = on;
    }
  }
  return o;
}

std::string render_png(const Scene& scene, const Frame& frame, const std::set<int>& pinned, const Render_options& opt,
                       const Grid* map) {
  const int    w = std::clamp(opt.width, 64, 4096), h = std::clamp(opt.height, 64, 4096);
  Canvas       c(w, h, kBg);
  const Box    view = opt.has_view ? opt.view : scene.die;
  const double vw = std::max(1.0, view.xh - view.xl), vh = std::max(1.0, view.yh - view.yl);
  const int    pad = 12;
  const double s   = std::min((w - 2.0 * pad) / vw, (h - 2.0 * pad) / vh);
  const double ox = (w - vw * s) / 2 - view.xl * s, oy = (h + vh * s) / 2 + view.yl * s;
  const auto   sx = [&](double x) { return int(std::lround(x * s + ox)); };
  const auto   sy = [&](double y) { return int(std::lround(oy - y * s)); };
  c.fill(sx(scene.die.xl), sy(scene.die.yh), sx(scene.die.xh), sy(scene.die.yl), kDie);
  if (opt.congestion && map && map->bx > 0 && map->by > 0 && map->v.size() == size_t(map->bx) * map->by) {
    const auto dx = (scene.die.xh - scene.die.xl) / map->bx, dy = (scene.die.yh - scene.die.yl) / map->by;
    for (int i = 0; i < map->bx; ++i) {
      for (int j = 0; j < map->by; ++j) {
        const auto use = map->v[size_t(i) * map->by + j];
        if (use <= 0.05) {
          continue;
        }
        c.fill(sx(scene.die.xl + i * dx),
               sy(scene.die.yl + (j + 1) * dy),
               sx(scene.die.xl + (i + 1) * dx),
               sy(scene.die.yl + j * dy),
               use > 1 ? Rgb{255, 60, 60} : Rgb{255, 170, 40},
               .1 + .55 * std::min(1., use / std::max(1., map->peak)));
      }
    }
  }
  for (const auto& b : scene.exclusions) {
    c.fill(sx(b.xl), sy(b.yh), sx(b.xh), sy(b.yl), kBg);
  }
  for (const auto& ring : scene.outline) {
    for (size_t i = 0; i < ring.size(); ++i) {
      const auto& a = ring[i];
      const auto& b = ring[(i + 1) % ring.size()];
      c.fill(sx(std::min(a[0], b[0])), sy(std::max(a[1], b[1])), sx(std::max(a[0], b[0])), sy(std::min(a[1], b[1])), kLine);
    }
  }
  if (opt.regions) {
    for (const auto& r : scene.regions) {
      c.outline(sx(r.xl), sy(r.yh), sx(r.xh), sy(r.yl), kRegion);
    }
  }
  const size_t n = std::min(scene.nodes.size(), frame.x.size());
  if (opt.comb || opt.regs) {
    for (size_t i = 0; i < n; ++i) {
      const auto& nd = scene.nodes[i];
      if (nd.kind != Kind::cell || (nd.seq ? !opt.regs : !opt.comb)) {
        continue;
      }
      const int x0 = sx(frame.x[i]), y1 = sy(frame.y[i]), x1 = sx(frame.x[i] + nd.w), y0 = sy(frame.y[i] + nd.h);
      const Rgb col = nd.seq ? kSeq : nd.movable ? kCell : kFixed;
      if (x1 - x0 >= 2 || y1 - y0 >= 2) {
        c.fill(x0, y0, x1, y1, col);
      } else {
        c.fill(x0, y0, x0 + 1, y0 + 1, col);
      }
    }
  }
  if (opt.macros) {
    for (size_t i = 0; i < n; ++i) {
      const auto& nd = scene.nodes[i];
      if (nd.kind != Kind::macro && nd.kind != Kind::fixed && nd.kind != Kind::blockage) {
        continue;
      }
      const int x0 = sx(frame.x[i]), y1 = sy(frame.y[i]), x1 = sx(frame.x[i] + nd.w), y0 = sy(frame.y[i] + nd.h);
      const Rgb col = nd.kind == Kind::blockage ? kRegion : nd.movable ? kMacro : kMacroFixed;
      c.fill(x0, y0, x1, y1, col, nd.kind == Kind::blockage ? 0.25 : 0.35);
      c.outline(x0, y0, x1, y1, col);
    }
  }
  if (opt.pin_sides) {
    for (size_t i = 0; i < n; ++i) {
      for (const auto& pin : scene.nodes[i].signal_pins) {
        const auto x = sx(frame.x[i] + pin.x), y = sy(frame.y[i] + pin.y);
        c.fill(x - 1, y - 1, x + 1, y + 1, kIo);
      }
    }
  }
  if (opt.io) {
    for (size_t i = 0; i < n; ++i) {
      if (scene.nodes[i].kind == Kind::io) {
        const int x = sx(frame.x[i]), y = sy(frame.y[i]);
        c.fill(x - 2, y - 2, x + 2, y + 2, kIo);
      }
    }
  }
  if (opt.centroids) {
    for (const auto& cen : scene.centroids) {
      const int x = sx(cen.x), y = sy(cen.y), r = std::max(4, int(cen.radius * s));
      for (int a = 0; a < 360; a += 3) {
        c.set(x + int(std::lround(r * std::cos(a * M_PI / 180))), y + int(std::lround(r * std::sin(a * M_PI / 180))), kCentroid);
      }
      c.fill(x - 5, y, x + 5, y, kCentroid);
      c.fill(x, y - 5, x, y + 5, kCentroid);
    }
  }
  if (opt.pins) {
    for (const int i : pinned) {
      if (i < 0 || size_t(i) >= n) {
        continue;
      }
      const auto& nd = scene.nodes[size_t(i)];
      const int   x0 = sx(frame.x[size_t(i)]) - 2, y1 = sy(frame.y[size_t(i)]) + 2;
      const int x1 = std::max(sx(frame.x[size_t(i)] + nd.w), x0 + 5) + 2, y0 = std::min(sy(frame.y[size_t(i)] + nd.h), y1 - 5) - 2;
      c.outline(x0, y0, x1, y1, kPin);
    }
  }
  if (opt.chunks && !scene.chunk_plan.empty()) {
    rapidjson::Document plan;
    plan.Parse(scene.chunk_plan.c_str());
    if (plan.IsObject() && plan.HasMember("chunks") && plan["chunks"].IsArray()) {
      constexpr Rgb edge{100, 211, 224};
      for (const auto& part : plan["chunks"].GetArray()) {
        if (!part.IsObject() || !part.HasMember("box_dbu")) {
          continue;
        }
        const auto& b = part["box_dbu"];
        if (!b.IsArray() || b.Size() != 4 || !std::all_of(b.Begin(), b.End(), [](const auto& v) { return v.IsNumber(); })) {
          continue;
        }
        const int left = sx(b[0].GetDouble()), right = sx(b[2].GetDouble()), top = sy(b[3].GetDouble()),
                  bottom = sy(b[1].GetDouble());
        // Clip the strokes, not the rectangle: zoomed screenshots must not invent boundaries at the viewport.
        c.fill(left, top, right, top + 1, edge);
        c.fill(left, bottom - 1, right, bottom, edge);
        c.fill(left, top, left + 1, bottom, edge);
        c.fill(right - 1, top, right, bottom, edge);
      }
    }
  }
  return encode_png(w, h, c.px);
}

}  // namespace livehd::placement::gui
