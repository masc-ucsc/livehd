// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "area_fit.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>

#include "node_util.hpp"
#include "place_test_fixture.hpp"
#include "rapidjson/document.h"

namespace pl = livehd::placement;
namespace ph = livehd::physical;

namespace {
// 100 rows x 1000 sites of 0.2 x 2 um (DBU 1000): a 200 x 200 um core at (10, 10) um in a 220 um die.
struct Frame {
  ph::Technology tech;
  ph::Floorplan  fp;
  Frame() {
    tech.dbu        = 1000;
    tech.sites["s"] = {.width = .2, .height = 2};
    ph::Macro cell;
    cell.width         = 1;
    cell.height        = 2;
    cell.klass         = "CORE";
    tech.macros["INV"] = cell;
    ph::Macro ram;
    ram.width          = 40;
    ram.height         = 60;
    ram.klass          = "BLOCK";
    tech.macros["RAM"] = ram;
    ph::Layer m2;
    m2.direction        = "HORIZONTAL";
    m2.pitch_y          = .4;
    ph::Layer m3        = m2;
    m3.direction        = "VERTICAL";
    m3.pitch_x          = .4;
    tech.layers["M1"]   = m2;
    tech.layers["M2"]   = m2;
    tech.layers["M3"]   = m3;
    tech.routing_layers = {"M1", "M2", "M3"};
    fp.design           = "top";
    fp.dbu              = 1000;
    fp.die              = {0, 0, 220000, 220000};
    for (int r = 0; r < 100; ++r) {
      fp.rows.push_back(
          {"ROW_" + std::to_string(r), "s", 10000, 10000 + r * 2000, 1000, 200, r % 2 ? ph::Orientation::FS : ph::Orientation::N});
    }
  }
  void add(const std::string& name, const std::string& macro, int64_t x, int64_t y, ph::Status status = ph::Status::PLACED) {
    ph::Component c;
    c.name         = name;
    c.macro        = macro;
    c.place.x      = x;
    c.place.y      = y;
    c.place.status = status;
    fp.components.push_back(c);
  }
  void pin(const std::string& name, int64_t x, int64_t y) {
    ph::Terminal t;
    t.name   = name;
    t.net    = name;
    t.x      = x;
    t.y      = y;
    t.status = ph::Status::FIXED;
    fp.terminals.push_back(t);
  }
};
}  // namespace

TEST(AreaFit, CoreIsTheRowBoundingBox) {
  Frame      f;
  const auto core = pl::area_fit_core(f.tech, f.fp);
  EXPECT_EQ(core.site, 200);
  EXPECT_EQ(core.row, 2000);
  EXPECT_DOUBLE_EQ(core.rect.xl, 10000);
  EXPECT_DOUBLE_EQ(core.rect.yl, 10000);
  EXPECT_DOUBLE_EQ(core.rect.xh, 210000);
  EXPECT_DOUBLE_EQ(core.rect.yh, 210000);
}

TEST(AreaFit, AreasSplitCellsFromMacros) {
  Frame f;
  f.add("u0", "INV", 10000, 10000);
  f.add("u1", "INV", 11000, 10000);
  f.add("m0", "RAM", 50000, 50000);
  f.fp.components.back().place.orientation = ph::Orientation::E;  // swapped: 60 x 40 um
  const auto areas                         = pl::area_fit_areas(f.tech, f.fp);
  EXPECT_DOUBLE_EQ(areas.cells, 2 * 1000.0 * 2000.0);
  EXPECT_DOUBLE_EQ(areas.macros, 40000.0 * 60000.0);
  EXPECT_EQ(areas.max_macro_w, 60000);
  EXPECT_EQ(areas.max_macro_h, 40000);
}

TEST(AreaFit, RegionKeepsAspectSnapsAndCentersAtTheAnchor) {
  Frame      f;
  const auto core   = pl::area_fit_core(f.tech, f.fp);
  const auto center = pl::area_fit_anchor(f.fp, core, "center");
  EXPECT_DOUBLE_EQ(center.first, 110000);
  // a quarter of the area: half of each side, centered
  const auto r = pl::area_fit_region(f.tech, f.fp, core, {}, .25, center);
  EXPECT_DOUBLE_EQ(r.xh - r.xl, 100000);
  EXPECT_DOUBLE_EQ(r.yh - r.yl, 100000);
  EXPECT_DOUBLE_EQ(r.xl, 60000);
  EXPECT_DOUBLE_EQ(r.yl, 60000);
  // sides round up to whole sites/rows: the area never falls under the fraction
  const auto odd = pl::area_fit_region(f.tech, f.fp, core, {}, .3, center);
  EXPECT_GE((odd.xh - odd.xl) * (odd.yh - odd.yl), .3 * 200000.0 * 200000.0);
  EXPECT_DOUBLE_EQ(std::fmod(odd.xl - core.rect.xl, 200), 0);
  EXPECT_DOUBLE_EQ(std::fmod(odd.yl - core.rect.yl, 2000), 0);
  EXPECT_TRUE(core.rect.contains(odd));
}

TEST(AreaFit, IoAnchorSlidesTheRegionToThePins) {
  Frame f;
  for (int i = 0; i < 10; ++i) {
    f.pin("p" + std::to_string(i), 0, 20000 + i * 20000);  // all pins on the left die edge
  }
  const auto core   = pl::area_fit_core(f.tech, f.fp);
  const auto anchor = pl::area_fit_anchor(f.fp, core, "io");
  EXPECT_DOUBLE_EQ(anchor.first, 0);
  const auto r = pl::area_fit_region(f.tech, f.fp, core, {}, .25, anchor);
  EXPECT_DOUBLE_EQ(r.xl, core.rect.xl);  // abuts the left core edge
  EXPECT_DOUBLE_EQ(r.xh - r.xl, 100000);
  // no placed pins: the core center
  Frame      g;
  const auto c = pl::area_fit_anchor(g.fp, core, "io");
  EXPECT_DOUBLE_EQ(c.first, 110000);
  EXPECT_DOUBLE_EQ(c.second, 110000);
}

TEST(AreaFit, RegionHoldsTheLargestMacroAndCoversFixedComponents) {
  Frame f;
  f.add("m0", "RAM", 10000, 10000);
  const auto core  = pl::area_fit_core(f.tech, f.fp);
  auto       areas = pl::area_fit_areas(f.tech, f.fp);
  // 1% of the area would be 20 x 20 um: at least the 40 x 60 um macro
  const auto small = pl::area_fit_region(f.tech, f.fp, core, areas, .01, {110000, 110000});
  EXPECT_GE(small.xh - small.xl, 40000);
  EXPECT_GE(small.yh - small.yl, 60000);
  // a FIXED macro at the lower-left corner pulls the centered region over it
  f.fp.components.back().place.status = ph::Status::FIXED;
  const auto grown                    = pl::area_fit_region(f.tech, f.fp, core, areas, .25, {110000, 110000});
  EXPECT_DOUBLE_EQ(grown.xl, 10000);
  EXPECT_DOUBLE_EQ(grown.yl, 10000);
  EXPECT_DOUBLE_EQ(grown.xh, 160000);
  EXPECT_DOUBLE_EQ(grown.yh, 160000);
}

TEST(AreaFit, FenceClipsRowsToWholeSites) {
  Frame      f;
  const auto fenced = pl::area_fit_fence(f.fp, {60100, 60000, 160000, 160000});
  ASSERT_EQ(fenced.rows.size(), 50u);
  EXPECT_EQ(fenced.rows.front().y, 60000);
  EXPECT_EQ(fenced.rows.front().x, 60200);  // the first whole site inside
  EXPECT_EQ(fenced.rows.front().count, 499);
  EXPECT_EQ(fenced.rows.front().orientation, f.fp.rows[25].orientation);  // rows keep their flip
  EXPECT_EQ(fenced.die.xh, f.fp.die.xh);                                  // the die stays
  EXPECT_THROW(pl::area_fit_fence(f.fp, {0, 0, 5000, 5000}), std::runtime_error);
}

TEST(AreaFit, RoutableComparesAgainstTheReference) {
  pl::Area_fit_options o;
  pl::Area_fit_route   ref;
  ref.oracle          = "legroute";
  ref.signals         = 10000;
  ref.overflow_tracks = 1000;
  ref.unrouted        = 2;
  auto probe          = ref;
  EXPECT_TRUE(pl::area_fit_routable(probe, &ref, o));
  probe.overflow_tracks = 1000 * 1.05 + 10;  // slack plus a floor of 0.001 x 10000 signals
  EXPECT_TRUE(pl::area_fit_routable(probe, &ref, o));
  probe.overflow_tracks += 1;
  EXPECT_FALSE(pl::area_fit_routable(probe, &ref, o));
  probe.overflow_tracks = 0;
  probe.unrouted        = 13;
  EXPECT_FALSE(pl::area_fit_routable(probe, &ref, o));
  // without a reference: absolute
  probe.unrouted = 0;
  EXPECT_TRUE(pl::area_fit_routable(probe, nullptr, o));
  pl::Area_fit_route rudy;
  rudy.oracle    = "rudy";
  rudy.overflow  = .01;
  auto worse     = rudy;
  worse.overflow = .02;
  EXPECT_FALSE(pl::area_fit_routable(worse, &rudy, o));
  EXPECT_TRUE(pl::area_fit_routable(rudy, &rudy, o));
}

TEST(AreaFit, RudyGrowsWithCompaction) {
  // the same 200 two-pin nets spread over the core, then squeezed into a corner
  const auto measure = [](int64_t span) {
    Frame f;
    for (int i = 0; i < 200; ++i) {
      const int64_t x = 10000 + (i * 7919) % span, y = 10000 + (i * 104729) % span;
      f.add("a" + std::to_string(i), "INV", x, y);
      f.add("b" + std::to_string(i), "INV", 10000 + (x * 3) % span, 10000 + (y * 5) % span);
      ph::Net net;
      net.name        = "n" + std::to_string(i);
      net.connections = {
          {"a" + std::to_string(i), "A"},
          {"b" + std::to_string(i), "Y"}
      };
      f.fp.nets.push_back(net);
    }
    pl::Area_fit_options o;
    o.oracle = "rudy";
    return pl::area_fit_route(f.tech, f.fp, o);
  };
  const auto spread = measure(190000), packed = measure(20000);
  EXPECT_EQ(spread.oracle, "rudy");
  EXPECT_EQ(spread.signals, 200u);
  EXPECT_GT(packed.peak, spread.peak);
}

TEST(AreaFit, RudyRejectsDemandWithoutHorizontalVerticalOrEitherCapacity) {
  pl::Area_fit_options options;
  options.oracle = "rudy";
  for (const int missing : {1, 2, 3}) {
    SCOPED_TRACE(missing);
    Frame f;
    f.add("a", "INV", 10000, 10000);
    f.add("b", "INV", 100000, 100000);
    ph::Net net;
    net.name        = "n";
    net.connections = {
        {"a", "Y"},
        {"b", "A"}
    };
    f.fp.nets.push_back(net);
    const auto valid = pl::area_fit_route(f.tech, f.fp, options);
    ASSERT_TRUE(valid.capacity_valid);
    if (missing & 1) {
      f.tech.layers.at("M2").pitch_y = 0;
    }
    if (missing & 2) {
      f.tech.layers.at("M3").pitch_x = 0;
    }
    const auto invalid = pl::area_fit_route(f.tech, f.fp, options);
    EXPECT_EQ(invalid.signals, 1u);
    EXPECT_GT(invalid.overflow_edges, 0u);
    EXPECT_FALSE(invalid.capacity_valid);
    EXPECT_FALSE(pl::area_fit_routable(invalid, nullptr, options));
    EXPECT_FALSE(pl::area_fit_routable(invalid, &invalid, options));
    EXPECT_FALSE(pl::area_fit_routable(valid, &invalid, options));
    EXPECT_THROW(pl::validate_area_fit_final_route(invalid, valid, options), std::runtime_error);
    rapidjson::Document report;
    report.Parse(pl::area_fit_route_json(invalid).c_str());
    ASSERT_FALSE(report.HasParseError());
    EXPECT_FALSE(report["capacity_valid"].GetBool());
    EXPECT_TRUE(std::isfinite(report["overflow"].GetDouble()));
    EXPECT_TRUE(std::isfinite(report["peak"].GetDouble()));
  }
  // No nets means no demand: missing tracks alone must not invent a failure.
  Frame empty;
  empty.tech.routing_layers = {"M1"};
  const auto no_demand      = pl::area_fit_route(empty.tech, empty.fp, options);
  EXPECT_TRUE(no_demand.capacity_valid);
  EXPECT_TRUE(pl::area_fit_routable(no_demand, nullptr, options));
}

TEST(AreaFit, FinalPlacementIsCheckedAgainstFrozenReferenceTolerance) {
  pl::Area_fit_options options;
  pl::Area_fit_route   reference;
  reference.oracle            = "legroute";
  reference.signals           = 10000;
  reference.overflow_tracks   = 1000;
  reference.unrouted          = 2;
  auto final_route            = reference;
  final_route.overflow_tracks = 1060;  // 5% relative slack plus the ten-track floor
  final_route.unrouted        = 12;
  EXPECT_NO_THROW(pl::validate_area_fit_final_route(final_route, reference, options));
  ++final_route.overflow_tracks;
  EXPECT_THROW(pl::validate_area_fit_final_route(final_route, reference, options), std::runtime_error);
  final_route.overflow_tracks = 1000;
  ++final_route.unrouted;
  EXPECT_THROW(pl::validate_area_fit_final_route(final_route, reference, options), std::runtime_error);
  final_route              = reference;
  reference.capacity_valid = false;
  EXPECT_THROW(pl::validate_area_fit_final_route(final_route, reference, options), std::runtime_error);
}

TEST(AreaFit, OptionsAreValidated) {
  pl::Area_fit_options o;
  EXPECT_NO_THROW(pl::validate_area_fit_options(o));
  o.anchor = "corner";
  EXPECT_THROW(pl::validate_area_fit_options(o), std::invalid_argument);
  o        = {};
  o.oracle = "fastroute";
  EXPECT_THROW(pl::validate_area_fit_options(o), std::invalid_argument);
  o              = {};
  o.min_fraction = 0;
  EXPECT_THROW(pl::validate_area_fit_options(o), std::invalid_argument);
  o           = {};
  o.tolerance = 1;
  EXPECT_THROW(pl::validate_area_fit_options(o), std::invalid_argument);
}

namespace {
// A chain of inverters between pins on the left die edge of a sparsely used imported core.
struct Chain {
  hhds::GraphLibrary           source;
  std::shared_ptr<hhds::Graph> graph;
  ph::Floorplan                frame;
  explicit Chain(int cells) {
    namespace gu = livehd::graph_util;
    auto io      = source.create_io("chain");
    auto inv     = source.create_io("sky130_fd_sc_hd__inv_1");
    inv->add_input("A", 0);
    inv->add_output("Y", 0);
    constexpr int lanes = 8;
    for (int i = 0; i < lanes; ++i) {
      io->add_input("a" + std::to_string(i), i);
      io->set_bits("a" + std::to_string(i), 1);
      io->add_output("y" + std::to_string(i), i);
      io->set_bits("y" + std::to_string(i), 1);
    }
    graph = io->create_graph();
    for (int i = 0; i < lanes; ++i) {
      auto signal = graph->get_input_pin("a" + std::to_string(i));
      gu::set_bits(signal, 1);
      for (int k = 0; k < cells / lanes; ++k) {
        auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
        node.set_subnode(inv);
        node.set_name(std::format("inv{}_{}", i, k));
        node.create_sink_pin("A").connect_driver(signal);
        signal = node.create_driver_pin("Y");
        gu::set_bits(signal, 1);
      }
      graph->get_output_pin("y" + std::to_string(i)).connect_driver(signal);
    }
    // 40 rows x 200 sites (92 x 108.8 um) at a 10 um margin; the IO on the left die edge
    const auto tech = ph::read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
    frame.design    = "chain";
    frame.dbu       = tech.dbu;
    const auto site = tech.sites.begin()->first;
    frame.die       = {0, 0, double(20000 + 200 * 460), double(20000 + 40 * 2720)};
    for (int r = 0; r < 40; ++r) {
      frame.rows.push_back({"ROW_" + std::to_string(r),
                            site,
                            10000,
                            10000 + r * 2720,
                            200,
                            460,
                            r % 2 ? Ann_place::Orientation::FS : Ann_place::Orientation::N});
    }
    for (int i = 0; i < lanes; ++i) {
      for (const auto* side : {"a", "y"}) {
        const auto name = side + std::to_string(i);
        frame.terminals.push_back({name,
                                   name,
                                   *side == 'a' ? "INPUT" : "OUTPUT",
                                   0,
                                   int64_t(20000 + (2 * i + (*side == 'y')) * 6000),
                                   Ann_place::Orientation::N,
                                   Ann_place::Status::FIXED,
                                   {}});
      }
    }
  }
};
}  // namespace

TEST(AreaFit, AutoPlacesAnImportedCoreInASmallerRoutableRegion) {
  Chain      chain(160);
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "area_fit";
  std::filesystem::create_directories(root);
  ph::write_def(chain.frame, (root / "frame.def").string());
  auto options                  = pl::plain_options();
  options.optimization_pending  = false;
  options.lefs                  = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.floorplan             = (root / "frame.def").string();
  options.iterations            = 300;
  options.density               = .6;
  options.area_fit.enabled      = true;
  options.area_fit.oracle       = "rudy";  // deterministic and instant on a toy; legroute is the production oracle
  options.area_fit.min_fraction = .2;
  options.area_fit.probes       = 3;
  options.output_dir            = (root / "auto").string();
  hhds::GraphLibrary placed_library;
  const auto         result = pl::place(*chain.graph, placed_library, options);
  // the result keeps the real core's rows; every cell sits inside the chosen region, inside the core
  EXPECT_EQ(result.floorplan.rows, chain.frame.rows);
  std::ifstream       report(options.output_dir + "/area_fit.json");
  const std::string   text{std::istreambuf_iterator<char>(report), {}};
  rapidjson::Document json;
  json.Parse(text.c_str());
  ASSERT_FALSE(json.HasParseError()) << text;
  const double fraction = json["fraction"].GetDouble();
  EXPECT_LT(fraction, 1);
  EXPECT_GE(fraction, .2 - 1e-9);
  EXPECT_GE(json["probes"].GetUint(), 2u);
  EXPECT_STREQ(json["route"]["oracle"].GetString(), "rudy");
  const auto& region = json["region_um"].GetArray();
  const auto  tech   = ph::read_lef(options.lefs);
  ASSERT_EQ(result.floorplan.components.size(), 160u);
  for (const auto& c : result.floorplan.components) {
    const auto& m = tech.macros.at(c.macro);
    EXPECT_GE(c.place.x, region[0].GetDouble() * tech.dbu - 1) << c.name;
    EXPECT_GE(c.place.y, region[1].GetDouble() * tech.dbu - 1) << c.name;
    EXPECT_LE(c.place.x + m.width * tech.dbu, region[2].GetDouble() * tech.dbu + 1) << c.name;
    EXPECT_LE(c.place.y + m.height * tech.dbu, region[3].GetDouble() * tech.dbu + 1) << c.name;
  }
  // pins on the left edge: the region abuts the left core edge
  EXPECT_DOUBLE_EQ(region[0].GetDouble(), 10);
  EXPECT_NE(result.qor.find("\"area_fit\""), std::string::npos);
  EXPECT_NO_THROW(pl::check_legality(tech, result.floorplan));
  // off (the default): the whole core, no search report
  auto fixed             = options;
  fixed.area_fit.enabled = false;
  fixed.output_dir       = (root / "fixed").string();
  hhds::GraphLibrary fixed_library;
  const auto         whole = pl::place(*chain.graph, fixed_library, fixed);
  EXPECT_FALSE(std::filesystem::exists(fixed.output_dir + "/area_fit.json"));
  EXPECT_EQ(whole.qor.find("\"area_fit\""), std::string::npos);
}
