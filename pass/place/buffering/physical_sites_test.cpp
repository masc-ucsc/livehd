// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_sites.hpp"

#include <algorithm>
#include <random>
#include <set>
#include <tuple>

#include "graph_search.hpp"
#include "gtest/gtest.h"

namespace b = livehd::placement::buffering;
namespace p = livehd::physical;
namespace m = livehd::liberty::timing;
namespace {
m::Table table(double base, double cap) {
  return {
      m::Variable::input_slew,
      m::Variable::output_load,
      {0, 100},
      {0, 100},
      {base, base + 100 * cap, base, base + 100 * cap}
  };
}
struct Fixture {
  p::Technology tech;
  m::Library    lib;
  p::Floorplan  frame;
  Fixture() {
    tech.dbu           = 10;
    tech.sites["site"] = {1, 2, true};
    frame.dbu          = 10;
    frame.die          = {0, 0, 100, 60};
    frame.rows         = {
        {"lo", "site", 0,  0, 8, 10,  p::Orientation::N},
        {"hi", "site", 0, 20, 8, 10, p::Orientation::FS}
    };
    for (auto [name, cap] : std::vector<std::pair<std::string, double>>{
             { "D", 1},
             {"B1", 1},
             {"B2", 2},
             { "S", 4}
    }) {
      m::Cell cell;
      cell.name              = name;
      auto& in               = cell.pins["A"];
      in.name                = "A";
      in.direction           = "input";
      in.capacitance_ff      = {cap, cap};
      in.max_transition_ps   = 1000;
      auto& out              = cell.pins["Y"];
      out.name               = "Y";
      out.direction          = "output";
      out.function           = "A";
      out.max_capacitance_ff = 100;
      out.max_transition_ps  = 1000;
      m::Arc arc;
      arc.related_pin = "A";
      arc.type        = "combinational";
      arc.sense       = m::Sense::positive;
      for (unsigned t = 0; t < 2; ++t) {
        arc.delay[t] = table(1, .1);
        arc.slew[t]  = table(1, .2);
      }
      out.arcs.push_back(arc);
      lib.cells.emplace(name, std::move(cell));
      p::Macro macro;
      macro.width       = 2;
      macro.height      = 2;
      macro.site        = "site";
      macro.klass       = "CORE";
      macro.symmetry_x  = true;
      macro.pins["A"]   = {"INPUT", "SIGNAL", {{"M1", {.2, .4, .3, .6}}}};
      macro.pins["Y"]   = {"OUTPUT", "SIGNAL", {{"M1", {1.4, 1.4, 1.6, 1.6}}}};
      tech.macros[name] = macro;
    }
    p::Macro obstacle;
    obstacle.width          = 1.2;
    obstacle.height         = 4;
    obstacle.klass          = "BLOCK";
    tech.macros["obstacle"] = obstacle;
    auto component          = [](std::string name, std::string master, int64_t x, int64_t y, p::Status status) {
      p::Component c;
      c.name         = name;
      c.macro        = master;
      c.place.x      = x;
      c.place.y      = y;
      c.place.status = status;
      return c;
    };
    frame.components = {component("d", "D", 30, 0, p::Status::PLACED), component("macro", "obstacle", 60, 0, p::Status::FIXED)};
    frame.blockages.push_back({true, "", {{20, 0, 30, 20}}});
  }
  b::Physical_sites environment() const {
    return b::build_physical_sites(tech,
                                   frame,
                                   {
                                       {0, 20, 10, 40}
    });
  }
  b::Physical_buffer_model model(std::string name = "B1") const {
    auto result = b::physical_buffer_model(tech, lib, {name, "A", "Y"});
    if (!result.model) {
      throw std::runtime_error(result.reason);
    }
    return *result.model;
  }
};
using Signature = std::tuple<std::string, uint64_t, std::string>;
std::set<Signature> signature(const std::vector<b::Tree_option>& options) {
  std::set<Signature> result;
  for (const auto& o : options) {
    result.emplace(o.site.row, o.site.first, o.buffer.model.cell);
  }
  return result;
}
// Independent dense sampling oracle for these half-DBU pin centers and integer
// obstacles. Every closed rectilinear segment crossing is seen at step .25.
bool route_clear(b::Point a, b::Point z, const std::vector<b::Site_rect>& masks) {
  std::vector<b::Point> ends{
      {z.x, a.y},
      z
  };
  for (auto end : ends) {
    const int count = int(std::ceil(4 * (std::abs(a.x - end.x) + std::abs(a.y - end.y))));
    for (int i = 0; i <= count; ++i) {
      const double   t = count ? double(i) / count : 0;
      const b::Point q{a.x + (end.x - a.x) * t, a.y + (end.y - a.y) * t};
      for (auto r : masks) {
        if (q.x >= r.xl && q.x <= r.xh && q.y >= r.yl && q.y <= r.yh) {
          return false;
        }
      }
    }
    a = end;
  }
  return true;
}
}  // namespace
TEST(PhysicalBufferSites, OriginalRowsAllOccupancySoftBlockagesAndExtraMasks) {
  Fixture f;
  auto    e = f.environment();
  EXPECT_TRUE(e.occupies_movable);
  EXPECT_EQ(e.occupied_components, 2);
  EXPECT_EQ(e.placement_masks, 2);
  EXPECT_EQ(e.legal.at("lo").gaps,
            (std::vector<b::Site_span>{
                {"lo", 0, 2},
                {"lo", 5, 1}
  }));
  EXPECT_EQ(e.legal.at("hi").gaps,
            (std::vector<b::Site_span>{
                {"hi", 1, 5}
  }));
  b::Physical_site_settings virtual_settings;
  virtual_settings.occupy_movable = false;
  auto virtual_e                  = b::build_physical_sites(f.tech, f.frame, {}, virtual_settings);
  EXPECT_FALSE(virtual_e.occupies_movable);
  EXPECT_EQ(virtual_e.skipped_movable, 1);
  EXPECT_EQ(virtual_e.occupied_components, 1);
  EXPECT_GT(b::buffer_slots(virtual_e.legal, 2), b::buffer_slots(e.legal, 2));
}
TEST(PhysicalBufferSites, MacroHaloAndMarginAreExplicitAndUnionSafe) {
  Fixture                   f;
  const auto                original = f.environment();
  b::Physical_site_settings settings;
  settings.macro_halo_dbu          = 10;
  settings.legalization_margin_dbu = 1;
  auto e                           = b::build_physical_sites(f.tech,
                                   f.frame,
                                                             {
                                       {60, 0, 72, 40},
                                       {60, 0, 72, 40}
  },
                                   settings);
  EXPECT_LT(b::buffer_slots(e.legal, 1), b::buffer_slots(original.legal, 1));
  auto once = b::build_physical_sites(f.tech,
                                      f.frame,
                                      {
                                          {60, 0, 72, 40}
  },
                                      settings);
  for (const auto& [name, row] : e.legal) {
    EXPECT_EQ(row.gaps, once.legal.at(name).gaps);
  }
}
TEST(PhysicalBufferSites, ActualPinOriginAndMirroredRowGeometry) {
  Fixture f;
  f.frame.components.clear();
  f.frame.blockages.clear();
  auto& master    = f.tech.macros.at("B1");
  master.origin_x = .1;
  master.origin_y = .2;
  auto model      = f.model();
  EXPECT_NEAR(model.input_offset_dbu.x, 3.5, 1e-12);
  EXPECT_NEAR(model.input_offset_dbu.y, 7, 1e-12);
  auto                    e = b::build_physical_sites(f.tech, f.frame);
  b::Site_reservations    reservations(e.legal);
  b::Site_option_settings settings;
  settings.max_attachment_dbu     = 50;
  settings.max_options_per_vertex = 100;
  auto result                     = b::physical_site_options(e,
                                         reservations,
                                         "net",
                                                             {
                                             {35, 20}
  },
                                                             {model},
                                                             {},
                                         settings);
  ASSERT_EQ(result.status, b::Site_option_status::complete);
  ASSERT_GT(result.emitted, 0);
  bool n = false, fs = false;
  for (const auto& option : result.options[0]) {
    if (option.site.row == "lo") {
      n = true;
      EXPECT_NEAR(option.buffer.input.y, .7, 1e-12);
    }
    if (option.site.row == "hi") {
      fs = true;
      EXPECT_NEAR(option.buffer.input.y, 3.3, 1e-12);
    }
    EXPECT_EQ(option.site.count, 2);
    EXPECT_EQ(option.buffer.demand[0].units, 2);
  }
  EXPECT_TRUE(n);
  EXPECT_TRUE(fs);
}
TEST(PhysicalBufferSites, MirroringAndSiteCompatibilityCannotBeInvented) {
  Fixture f;
  f.frame.components.clear();
  f.frame.blockages.clear();
  auto                 e = b::build_physical_sites(f.tech, f.frame);
  b::Site_reservations reservations(e.legal);
  auto                 model = f.model();
  model.mirror_x             = false;
  b::Site_option_settings settings;
  settings.max_attachment_dbu     = 100;
  settings.max_options_per_vertex = 100;
  auto result                     = b::physical_site_options(e,
                                         reservations,
                                         "net",
                                                             {
                                             {35, 20}
  },
                                                             {model},
                                                             {},
                                         settings);
  ASSERT_GT(result.emitted, 0);
  for (const auto& o : result.options[0]) {
    EXPECT_EQ(o.site.row, "lo");
  }
  model.site = "different";
  result     = b::physical_site_options(e,
                                    reservations,
                                    "net",
                                        {
                                        {35, 20}
  },
                                        {model},
                                        {},
                                    settings);
  EXPECT_EQ(result.emitted, 0);
  model                     = f.model();
  model.height_dbu          = 10;
  model.input_offset_dbu.y  = 5;
  model.output_offset_dbu.y = 5;
  result                    = b::physical_site_options(e,
                                    reservations,
                                    "net",
                                                       {
                                        {35, 20}
  },
                                                       {model},
                                                       {},
                                    settings);
  EXPECT_EQ(result.emitted, 0);
}
TEST(PhysicalBufferSites, OtherOwnersAreUnavailableAndOwnFootprintIsReusable) {
  Fixture              f;
  auto                 e = f.environment();
  b::Site_reservations reservations(e.legal);
  ASSERT_EQ(reservations
                .replace("owner",
                         {
                             {"lo", 0, 2}
  })
                .status,
            b::Reservation_status::reserved);
  b::Site_option_settings settings;
  settings.max_attachment_dbu     = 30;
  settings.max_options_per_vertex = 100;
  auto own                        = b::physical_site_options(e,
                                      reservations,
                                      "owner",
                                                             {
                                          {5, 5}
  },
                                                             {f.model()},
                                                             {},
                                      settings);
  auto other                      = b::physical_site_options(e,
                                        reservations,
                                        "other",
                                                             {
                                            {5, 5}
  },
                                                             {f.model()},
                                                             {},
                                        settings);
  EXPECT_GT(own.emitted, other.emitted);
  EXPECT_EQ(own.capacity.at("sites:lo"), 3);
  EXPECT_EQ(other.capacity.at("sites:lo"), 1);
  EXPECT_EQ(reservations.reservations().size(), 1);
}
TEST(PhysicalBufferSites, RoutingMasksAreDistinctAndCheckBothActualAttachmentPaths) {
  Fixture f;
  f.frame.components.clear();
  f.frame.blockages.clear();
  auto                    e = b::build_physical_sites(f.tech, f.frame);
  b::Site_reservations    reservations(e.legal);
  b::Site_option_settings settings;
  settings.max_attachment_dbu     = 60;
  settings.max_options_per_vertex = 100;
  auto open                       = b::physical_site_options(e,
                                       reservations,
                                       "net",
                                                             {
                                           {35, 5}
  },
                                                             {f.model()},
                                                             {},
                                       settings);
  auto blocked                    = b::physical_site_options(e,
                                          reservations,
                                          "net",
                                                             {
                                              {35, 5}
  },
                                                             {f.model()},
                                                             {{34, 0, 36, 10}},
                                          settings);
  EXPECT_GT(open.emitted, 0);
  EXPECT_EQ(blocked.emitted, 0);
  EXPECT_GT(blocked.routing_rejected, 0);
  // A placement body never implicitly becomes an opaque routing obstruction.
  EXPECT_TRUE(e.legal.size());
  EXPECT_TRUE(reservations.reservations().empty());
}
TEST(PhysicalBufferSites, FiniteStartsMatchIndependentDenseRoutingAndSiteOracle) {
  std::mt19937 random(1829);
  for (unsigned trial = 0; trial < 100; ++trial) {
    Fixture f;
    f.frame.components.clear();
    f.frame.blockages.clear();
    std::vector<b::Site_rect> placement, routing;
    for (unsigned i = 0; i < trial % 4; ++i) {
      int64_t x = random() % 75, y = random() % 35;
      placement.push_back({x, y, x + 5, y + 5});
    }
    for (unsigned i = 0; i < trial % 3; ++i) {
      int64_t x = random() % 75, y = random() % 35;
      routing.push_back({x, y, x + 3, y + 3});
    }
    auto                                        e = b::build_physical_sites(f.tech, f.frame, placement);
    b::Site_reservations                        reservations(e.legal);
    const std::vector<b::Physical_buffer_model> models{f.model(), f.model("B2")};
    b::Site_option_settings                     settings;
    settings.max_attachment_dbu     = 15 + random() % 40;
    settings.max_options_per_vertex = 1000;
    const b::Route_point at{int64_t(random() % 80), int64_t(random() % 40)};
    const auto           result = b::physical_site_options(e, reservations, "net", {at}, models, routing, settings);
    ASSERT_EQ(result.status, b::Site_option_status::complete);
    std::set<Signature> expected;
    for (const auto& [name, row] : e.legal) {
      for (auto gap : row.gaps) {
        for (uint64_t c = gap.first; c + 2 <= gap.first + gap.count; ++c) {
          for (const auto& model : models) {
            b::Point input = model.input_offset_dbu, output = model.output_offset_dbu;
            if (e.rows.at(name).orientation == p::Orientation::FS) {
              input.y  = 20 - input.y;
              output.y = 20 - output.y;
            }
            input.x   += row.row.body.xl + int64_t(c) * 10;
            output.x  += row.row.body.xl + int64_t(c) * 10;
            input.y   += row.row.body.yl;
            output.y  += row.row.body.yl;
            auto dist  = [&](b::Point pin) { return std::abs(pin.x - at.x) + std::abs(pin.y - at.y); };
            if (dist(input) <= settings.max_attachment_dbu && dist(output) <= settings.max_attachment_dbu
                && route_clear({double(at.x), double(at.y)}, input, routing)
                && route_clear(output, {double(at.x), double(at.y)}, routing)) {
              expected.emplace(name, c, model.ports.cell);
            }
          }
        }
      }
    }
    EXPECT_EQ(signature(result.options[0]), expected) << trial;
  }
}
TEST(PhysicalBufferSites, BoundsReturnPartialOptionsWithoutCommittingReservations) {
  Fixture                 f;
  auto                    e = f.environment();
  b::Site_reservations    reservations(e.legal);
  b::Site_option_settings settings;
  settings.max_attachment_dbu   = 100;
  settings.max_candidate_checks = 0;
  auto result                   = b::physical_site_options(e,
                                         reservations,
                                         "net",
                                                           {
                                             {5, 5}
  },
                                                           {f.model()},
                                                           {},
                                         settings);
  EXPECT_EQ(result.status, b::Site_option_status::budget_exhausted);
  EXPECT_EQ(result.emitted, 0);
  settings.max_candidate_checks   = 1000;
  settings.max_options_per_vertex = 1;
  result                          = b::physical_site_options(e,
                                    reservations,
                                    "net",
                                                             {
                                        {5, 5}
  },
                                                             {f.model()},
                                                             {},
                                    settings);
  EXPECT_EQ(result.status, b::Site_option_status::budget_exhausted);
  EXPECT_EQ(result.emitted, 1);
  EXPECT_TRUE(reservations.reservations().empty());
}
TEST(PhysicalBufferSites, MissingPhysicalOrElectricalModelsRemainUnsupported) {
  Fixture f;
  EXPECT_FALSE(b::physical_buffer_model(f.tech, f.lib, {"missing", "A", "Y"}).model);
  auto saved                           = f.tech.macros.at("B1");
  f.tech.macros.at("B1").virtual_proxy = true;
  EXPECT_FALSE(b::physical_buffer_model(f.tech, f.lib, {"B1", "A", "Y"}).model);
  f.tech.macros.at("B1") = saved;
  f.tech.macros.at("B1").pins.erase("Y");
  EXPECT_FALSE(b::physical_buffer_model(f.tech, f.lib, {"B1", "A", "Y"}).model);
  f.tech.macros.at("B1")                     = saved;
  f.lib.cells.at("B1").pins.at("Y").function = "!A";
  EXPECT_FALSE(b::physical_buffer_model(f.tech, f.lib, {"B1", "A", "Y"}).model);
  f.lib.cells.at("B1").pins.at("Y").function = "A";
  f.lib.cells.at("B1").pins.at("A").capacitance_ff[0].reset();
  EXPECT_FALSE(b::physical_buffer_model(f.tech, f.lib, {"B1", "A", "Y"}).model);
}
TEST(PhysicalBufferSites, InvalidRowsStaleBlockagesUnknownBodiesAndOverflowReject) {
  Fixture f;
  auto    frame = f.frame;
  frame.dbu++;
  EXPECT_THROW(b::build_physical_sites(f.tech, frame), std::invalid_argument);
  frame = f.frame;
  frame.rows[0].step++;
  EXPECT_THROW(b::build_physical_sites(f.tech, frame), std::invalid_argument);
  frame                     = f.frame;
  frame.rows[0].orientation = p::Orientation::E;
  EXPECT_THROW(b::build_physical_sites(f.tech, frame), std::invalid_argument);
  frame               = f.frame;
  frame.rows[0].count = INT64_MAX;
  EXPECT_THROW(b::build_physical_sites(f.tech, frame), std::invalid_argument);
  frame                            = f.frame;
  frame.components[0].place.status = p::Status::UNPLACED;
  EXPECT_THROW(b::build_physical_sites(f.tech, frame), std::invalid_argument);
  frame                           = f.frame;
  frame.components[0].place.width = 1;
  EXPECT_THROW(b::build_physical_sites(f.tech, frame), std::invalid_argument);
  frame                        = f.frame;
  frame.blockages[0].component = "d";
  EXPECT_THROW(b::build_physical_sites(f.tech, frame), std::invalid_argument);
  frame                     = f.frame;
  frame.components[0].macro = "missing";
  EXPECT_THROW(b::build_physical_sites(f.tech, frame), std::invalid_argument);
}
TEST(PhysicalBufferSites, RealFootprintsAndLibraryOptionsFeedNativeSharedSearch) {
  Fixture f;
  f.frame.components.resize(1);
  f.frame.blockages.clear();
  f.frame.rows[0].count                             = 10;
  f.frame.rows[1].count                             = 10;
  f.tech.macros.at("D").pins.at("Y").shapes[0].rect = {.4, .4, .6, .6};
  f.tech.macros.at("S").pins.at("A").shapes[0].rect = {.4, .4, .6, .6};
  p::Component sink;
  sink.name         = "s";
  sink.macro        = "S";
  sink.place.x      = 70;
  sink.place.y      = 20;
  sink.place.status = p::Status::PLACED;
  f.frame.components.push_back(sink);
  f.lib.cells.at("D").pins.at("Y").max_capacitance_ff = 1;
  auto                 e                              = b::build_physical_sites(f.tech, f.frame);
  b::Site_reservations reservations(e.legal);
  b::Visibility_graph  graph;
  graph.vertices = {
      {35,  5},
      {35, 25},
      {75, 25}
  };
  graph.edges = {
      {0, 1, 20},
      {1, 2, 40}
  };
  graph.terminals = {0, 2};
  b::Site_option_settings settings;
  settings.max_attachment_dbu     = 30;
  settings.max_options_per_vertex = 1000;
  auto options = b::physical_site_options(e, reservations, "net", graph.vertices, {f.model(), f.model("B2")}, {}, settings);
  ASSERT_EQ(options.status, b::Site_option_status::complete);
  ASSERT_GT(options.options[0].size(), 0);
  options.options[1].clear();
  options.options[2].clear();
  b::Context context;
  context.driver = {
      "d",
      {"D", "A", "Y"},
      {3.5, .5},
      {0, 0},
      {1, 1}
  };
  context.core  = {0, 0, 10, 6};
  context.sinks = {
      {"s", "S", "A", {7.5, 2.5}, {100, 100}, {.5, .5}}
  };
  context.capacity = options.capacity;
  b::Graph_search_settings search;
  search.tree.dbu_per_um  = 10;
  search.tree.max_seconds = 10;
  search.max_seconds      = 10;
  auto result             = b::search_graph(f.lib, context, graph, options.options, {0, 0, 0, 0}, reservations, "net", search);
  ASSERT_EQ(result.status, b::Graph_search_status::complete);
  ASSERT_TRUE(result.best);
  ASSERT_EQ(result.best->solution.candidate.buffers.size(), 1);
  EXPECT_EQ(result.best->solution.candidate.buffers[0].model.cell, "B1");
  EXPECT_TRUE(result.best->solution.timing.feasible());
  EXPECT_TRUE(reservations.reservations().empty());
}

TEST(PhysicalBufferSites, LargeCoordinatesCannotSilentlyMoveFractionalPhysicalPins) {
  Fixture       f;
  const int64_t offset = (int64_t{1} << 53) - 100;
  f.frame.die          = {double(offset), 0, double(offset + 100), 60};
  for (auto& row : f.frame.rows) {
    row.x += offset;
  }
  for (auto& c : f.frame.components) {
    c.place.x += offset;
  }
  for (auto& blockage : f.frame.blockages) {
    for (auto& r : blockage.rectangles) {
      r.xl += offset;
      r.xh += offset;
    }
  }
  auto                    e = b::build_physical_sites(f.tech, f.frame);
  b::Site_reservations    reservations(e.legal);
  b::Site_option_settings settings;
  settings.max_attachment_dbu = 30;
  EXPECT_THROW(b::physical_site_options(e,
                                        reservations,
                                        "net",
                                        {
                                            {offset + 5, 5}
  },
                                        {f.model()},
                                        {},
                                        settings),
               std::invalid_argument);
}

TEST(PhysicalBufferSites, PerVertexLimitsRetainEvidenceForEveryRequestedAnchor) {
  Fixture                 f;
  auto                    e = f.environment();
  b::Site_reservations    reservations(e.legal);
  b::Site_option_settings settings;
  settings.max_attachment_dbu     = 100;
  settings.max_options_per_vertex = 1;
  auto result                     = b::physical_site_options(e,
                                         reservations,
                                         "net",
                                                             {
                                             { 5,  5},
                                             {45, 25}
  },
                                                             {f.model()},
                                                             {},
                                         settings);
  EXPECT_EQ(result.status, b::Site_option_status::budget_exhausted);
  ASSERT_EQ(result.options.size(), 2);
  ASSERT_EQ(result.options[0].size(), 1);
  ASSERT_EQ(result.options[1].size(), 1);
  EXPECT_NE(result.options[0][0].buffer.name, result.options[1][0].buffer.name);
  EXPECT_EQ(result.emitted, 2);
  EXPECT_TRUE(reservations.reservations().empty());
}

TEST(PhysicalBufferSites, PerModelBoundsExposeBothLibraryTypesWithoutAnOptimumClaim) {
  Fixture                 f;
  auto                    e = f.environment();
  b::Site_reservations    reservations(e.legal);
  b::Site_option_settings settings;
  settings.max_attachment_dbu               = 100;
  settings.max_options_per_vertex           = 4;
  settings.max_options_per_model_per_vertex = 2;
  auto result                               = b::physical_site_options(e,
                                         reservations,
                                         "net",
                                                                       {
                                             {5, 5}
  },
                                                                       {f.model(), f.model("B2")},
                                                                       {},
                                         settings);
  EXPECT_EQ(result.status, b::Site_option_status::budget_exhausted);
  ASSERT_EQ(result.options[0].size(), 4);
  std::map<std::string, size_t> counts;
  for (const auto& option : result.options[0]) {
    ++counts[option.buffer.model.cell];
  }
  EXPECT_EQ(counts["B1"], 2);
  EXPECT_EQ(counts["B2"], 2);
  EXPECT_TRUE(reservations.reservations().empty());
}
TEST(PhysicalBufferSites, FractionalSiteWidthReservesEveryIntersectedWholeSite) {
  Fixture f;
  f.tech.macros.at("B1").width = 1.5;
  auto model                   = f.model();
  EXPECT_EQ(model.width_dbu, 15);
  auto                    e = f.environment();
  b::Site_reservations    reservations(e.legal);
  b::Site_option_settings settings;
  settings.max_attachment_dbu     = 100;
  settings.max_options_per_vertex = 100;
  auto result                     = b::physical_site_options(e,
                                         reservations,
                                         "net",
                                                             {
                                             {5, 5}
  },
                                                             {model},
                                                             {},
                                         settings);
  ASSERT_EQ(result.status, b::Site_option_status::complete);
  ASSERT_EQ(result.emitted, 5);
  for (const auto& option : result.options[0]) {
    EXPECT_EQ(option.site.count, 2);
    EXPECT_NEAR(option.buffer.body.xh - option.buffer.body.xl, 1.5, 1e-12);
  }
}
