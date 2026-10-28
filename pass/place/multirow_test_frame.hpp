// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// The frame shared by multirow_test and multirow_eco_test: 100 DBU per micron,
// one-micron sites and rows (N/FS alternating by default), a single-row "cell"
// and "tap", and the anchor prepass controls over one density bin with no
// padding (the padding cases set their own).
#include <map>
#include <string>

#include "inou/lefdef/physical.hpp"
#include "multirow.hpp"

namespace livehd::placement::test {
struct Multirow_frame {
  physical::Technology                     tech;
  physical::Floorplan                      fp;
  physical::Row_access_convention          access;
  std::map<std::string, Multirow_template> templates;
  explicit Multirow_frame(int width = 12, int height = 2, bool flip = true) {
    tech.dbu = fp.dbu  = 100;
    tech.sites["core"] = {1, 1, true};
    macro("cell", "CORE", 1, 1);
    macro("tap", "CORE WELLTAP", 1, 1);
    fp.die = {0, 0, width * 100., height * 100.};
    for (int y = 0; y < height; ++y) {
      fp.rows.push_back({"r" + std::to_string(y),
                         "core",
                         0,
                         y * 100,
                         width,
                         100,
                         flip && y % 2 ? physical::Orientation::FS : physical::Orientation::N});
    }
  }
  Multirow_frame(const Multirow_frame&) = delete;  // controls point into the frame
  void macro(const std::string& name, const std::string& klass, double width, double height, bool symmetry_y = false) {
    auto& m      = tech.macros[name];
    m.width      = width;
    m.height     = height;
    m.klass      = klass;
    m.site       = "core";
    m.symmetry_x = true;
    m.symmetry_y = symmetry_y;
  }
  // A two-row CORE master with the row-access template t0 (bottom half horizontal).
  void two_row(const std::string& name, double width) {
    macro(name, "CORE", width, 2, true);
    templates[name] = {{physical::Row_access::horizontal, physical::Row_access::vertical}};
  }
  // PG rails on M1 (pg_rails.hpp): the one-row "cell" has VSS (GROUND) on its
  // bottom edge and VDD (POWER) on its top, so an N row has VSS below and an
  // FS row VDD below. Without this call no master has rails and parity is
  // unknown (unchecked).
  void pg_rails() {
    tech.routing_layers = {"M1"};
    auto& cell          = tech.macros.at("cell");
    cell.pins["VSS"]    = {"INOUT", "GROUND", {{"M1", {0, -.05, 1, .05}}}};
    cell.pins["VDD"]    = {"INOUT", "POWER", {{"M1", {0, .95, 1, 1.05}}}};
  }
  // Rail template t of a two-row master: VSS on its outer edges and VDD in
  // the middle for t0 (fits N starts), the reverse for t1 (FS starts).
  void rails(const std::string& name, int t) {
    auto&      m     = tech.macros.at(name);
    const auto outer = [&](double y) {
      return physical::Shape{
          "M1",
          {0, y - .05, m.width, y + .05}
      };
    };
    m.pins[t ? "VDD" : "VSS"] = {
        "INOUT",
        t ? "POWER" : "GROUND",
        {outer(0), outer(2)}
    };
    m.pins[t ? "VSS" : "VDD"] = {"INOUT", t ? "GROUND" : "POWER", {outer(1)}};
  }
  // A two-row master in t0 and its t1 rail twin `name`_t1, each the other's twin.
  void twin_pair(const std::string& name, double width) {
    for (const auto& [master, t] : {
             std::pair{        name, 0},
             {name + "_t1", 1}
    }) {
      two_row(master, width);
      rails(master, t);
    }
    templates[name].rail_twin         = name + "_t1";
    templates[name + "_t1"].rail_twin = name;
  }
  void controls(Multirow_options& options) {
    options.access                  = &access;
    options.templates               = &templates;
    options.occupancy.bins_x        = 1;
    options.occupancy.bins_y        = 1;
    options.occupancy.max_density   = 1;
    options.occupancy.padding_sites = 0;
  }
};
}  // namespace livehd::placement::test
