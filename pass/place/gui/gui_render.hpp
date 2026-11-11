// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Software rasterizer for the placement GUI: the same layers as the page, drawn to a PNG without a browser.
// Used by the `screenshot` command, GET /screenshot.png, replay video frames and tests.
#include <set>
#include <string>

#include "gui_session.hpp"

namespace livehd::placement::gui {

struct Render_options {
  int  width = 1000, height = 720;
  bool comb = true, regs = true, macros = true, io = true, regions = false, centroids = true, pins = true;
  bool has_view = false;  // otherwise: fit the die
  Box  view;              // world (DBU) rectangle to show
};

// Draws `frame` of `scene`; `pinned` are solver node indices. Returns the PNG file bytes.
std::string render_png(const Scene& scene, const Frame& frame, const std::set<int>& pinned, const Render_options&);
// Encodes RGB (3 bytes per pixel, row-major) as an uncompressed-deflate PNG.
std::string encode_png(int width, int height, const std::string& rgb);
// Parses "comb,regs,macros,io,regions,centroids,pins" (names present = on, "-name" = off; "cells" = comb and regs);
// unknown names are ignored.
Render_options parse_layers(const std::string& csv, Render_options base = {});

}  // namespace livehd::placement::gui
