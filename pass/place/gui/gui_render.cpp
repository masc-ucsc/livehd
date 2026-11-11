// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gui_render.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace livehd::placement::gui {

namespace {
struct Rgb {
  std::uint8_t r, g, b;
};
constexpr Rgb kBg{16, 19, 26}, kDie{11, 14, 20}, kLine{44, 52, 70}, kCell{91, 155, 255}, kFixed{107, 114, 133}, kMacro{240, 164, 60},
    kMacroFixed{185, 120, 42}, kIo{63, 207, 130}, kRegion{74, 83, 106}, kPin{255, 255, 255}, kCentroid{255, 92, 138}, kSeq{210, 140, 255};

struct Canvas {
  int         w, h;
  std::string px;
  Canvas(int width, int height, Rgb bg) : w(width), h(height), px(size_t(width) * size_t(height) * 3, 0) {
    for (size_t i = 0; i < px.size(); i += 3) {
      px[i] = char(bg.r);
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
  std::string body = std::string(type, 4) + data;
  out += body;
  be32(out, crc32(body));
}
}  // namespace

std::string encode_png(int width, int height, const std::string& rgb) {
  std::string raw;
  raw.reserve(size_t(height) * (size_t(width) * 3 + 1));
  for (int y = 0; y < height; ++y) {
    raw += char(0);
    raw.append(rgb, size_t(y) * size_t(width) * 3, size_t(width) * 3);
  }
  std::string z = "\x78\x01";
  for (size_t at = 0; at < raw.size() || at == 0;) {
    const size_t n     = std::min<size_t>(65535, raw.size() - at);
    const bool   final = at + n >= raw.size();
    z += char(final ? 1 : 0);
    z += char(n & 0xff);
    z += char((n >> 8) & 0xff);
    z += char(~n & 0xff);
    z += char((~n >> 8) & 0xff);
    z.append(raw, at, n);
    at += n;
    if (final) {
      break;
    }
  }
  std::uint32_t a = 1, b = 0;
  for (const unsigned char c : raw) {
    a = (a + c) % 65521;
    b = (b + a) % 65521;
  }
  be32(z, (b << 16) | a);
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
    } else if (item == "regions") {
      o.regions = on;
    } else if (item == "centroids") {
      o.centroids = on;
    } else if (item == "pins") {
      o.pins = on;
    }
  }
  return o;
}

std::string render_png(const Scene& scene, const Frame& frame, const std::set<int>& pinned, const Render_options& opt) {
  const int w = std::clamp(opt.width, 64, 4096), h = std::clamp(opt.height, 64, 4096);
  Canvas    c(w, h, kBg);
  const Box view = opt.has_view ? opt.view : scene.die;
  const double vw = std::max(1.0, view.xh - view.xl), vh = std::max(1.0, view.yh - view.yl);
  const int    pad = 12;
  const double s   = std::min((w - 2.0 * pad) / vw, (h - 2.0 * pad) / vh);
  const double ox  = (w - vw * s) / 2 - view.xl * s, oy = (h + vh * s) / 2 + view.yl * s;
  const auto   sx = [&](double x) { return int(std::lround(x * s + ox)); };
  const auto   sy = [&](double y) { return int(std::lround(oy - y * s)); };
  c.fill(sx(scene.die.xl), sy(scene.die.yh), sx(scene.die.xh), sy(scene.die.yl), kDie);
  c.outline(sx(scene.die.xl), sy(scene.die.yh), sx(scene.die.xh), sy(scene.die.yl), kLine);
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
      const int   x1 = std::max(sx(frame.x[size_t(i)] + nd.w), x0 + 5) + 2, y0 = std::min(sy(frame.y[size_t(i)] + nd.h), y1 - 5) - 2;
      c.outline(x0, y0, x1, y1, kPin);
    }
  }
  return encode_png(w, h, c.px);
}

}  // namespace livehd::placement::gui
