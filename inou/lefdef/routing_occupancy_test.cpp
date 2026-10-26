// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_occupancy.hpp"

#include <limits>
#include <random>
#include <tuple>

#include "gtest/gtest.h"
#include "routing_geometry.hpp"

namespace livehd::physical {
TEST(RoutingOccupancy, RouteProvenanceSurvivesRunsCopiesAndOverlays) {
  Routing_occupancy index(64);
  const auto        pin  = index.insert({
      {"M1", {0, 0, 18, 100}},
      "VDD"
  });
  const auto        wire = index.insert_run(
      {
          {"M1", {0, 0, 18, 100}},
          "VDD",
          true
  },
      36,
      0,
      8);
  EXPECT_FALSE(index.at(pin).routed);
  for (size_t i = 0; i < 8; ++i) {
    EXPECT_TRUE(index.at(wire + i).routed);
  }
  EXPECT_TRUE(index.query("M1", {0, 0, 400, 100}, 0, "VDD").empty());
  auto              copy = index;
  Routing_occupancy overlay(std::make_shared<const Routing_occupancy>(std::move(copy)));
  EXPECT_FALSE(overlay.at(pin).routed);
  EXPECT_TRUE(overlay.at(wire + 7).routed);
  EXPECT_EQ(overlay.at(wire + 7).net, "VDD");
  EXPECT_TRUE(overlay.query("M1", {0, 0, 400, 100}, 0, "VDD").empty());
}

TEST(RoutingOccupancy, ArithmeticRunsMatchExpandedIdsAndQueries) {
  Routing_occupancy compact(64), expanded(64);
  const auto        add_run = [&](Rect first, int64_t dx, int64_t dy, size_t count, std::string owner) {
    const auto start = expanded.size();
    EXPECT_EQ(compact.insert_run(
                  {
                      {"V1", first},
                      owner
    },
                  dx,
                  dy,
                  count),
              start);
    for (size_t i = 0; i < count; ++i) {
      Rect r{first.xl + double(i) * dx, first.yl + double(i) * dy, first.xh + double(i) * dx, first.yh + double(i) * dy};
      EXPECT_EQ(expanded.insert({
                    {"V1", r},
                    owner
      }),
                start + i);
    }
  };
  add_run({-900, -100, -882, -82}, 36, 0, 100, "VDD");
  EXPECT_EQ(compact.insert({
                {"V1", {10, 10, 20, 20}},
                "signal"
  }),
            expanded.insert({{"V1", {10, 10, 20, 20}}, "signal"}));
  add_run({400, 800, 418, 818}, 0, -36, 90, "VSS");
  add_run({900, 900, 918, 918}, -36, -18, 100, "VDD");
  add_run({0, 0, 18, 18}, 0, 0, 7, "VSS");
  add_run({0, -700, 18, -682}, 36, 0, 1, "single");
  EXPECT_EQ(compact.size(), expanded.size());
  for (size_t i = 0; i < expanded.size(); ++i) {
    EXPECT_EQ(compact.at(i).shape, expanded.at(i).shape);
    EXPECT_EQ(compact.at(i).net, expanded.at(i).net);
  }
  EXPECT_THROW(compact.at(compact.size()), std::out_of_range);
  std::mt19937                       rng(2097);
  std::uniform_int_distribution<int> point(-3000, 3000), extent(1, 300);
  for (int i = 0; i < 1000; ++i) {
    const int  x = point(rng), y = point(rng);
    const Rect q{double(x), double(y), double(x + extent(rng)), double(y + extent(rng))};
    for (const std::string owner : {"", "VDD", "VSS", "other"}) {
      EXPECT_EQ(compact.query("V1", q, i % 73, owner), expanded.query("V1", q, i % 73, owner));
    }
  }
  EXPECT_EQ(compact.query("V1", {-5000, -5000, 5000, 5000}, 0), expanded.query("V1", {-5000, -5000, 5000, 5000}, 0));
  auto copy = compact;
  copy.insert_run(
      {
          {"V1", {0, 0, 18, 18}},
          "extra"
  },
      36,
      0,
      10);
  EXPECT_EQ(compact.size(), expanded.size());
  Routing_occupancy overlay(std::make_shared<const Routing_occupancy>(compact));
  EXPECT_EQ(overlay.insert_run(
                {
                    {"V1", {0, 0, 18, 18}},
                    "extra"
  },
                36,
                0,
                10),
            compact.size());
  EXPECT_EQ(overlay.query("V1", {-5000, -5000, 5000, 5000}, 0), copy.query("V1", {-5000, -5000, 5000, 5000}, 0));
  const auto before = compact.size();
  EXPECT_THROW(compact.insert_run(
                   {
                       {"V1", {0, 0, 18, 18}},
                       "bad"
  },
                   36,
                   0,
                   0),
               std::invalid_argument);
  EXPECT_THROW(compact.insert_run(
                   {
                       {"V1", {0, 0, 18, 18}},
                       "bad"
  },
                   std::numeric_limits<int64_t>::min(),
                   0,
                   2),
               std::invalid_argument);
  EXPECT_THROW(compact.insert_run(
                   {
                       {"V1", {0, 0, 18, 18}},
                       "bad"
  },
                   36,
                   0,
                   std::numeric_limits<size_t>::max()),
               std::length_error);
  EXPECT_EQ(compact.size(), before);
}
TEST(RoutingOccupancy, RunAfterOrdinaryRecordsSurvivesMoveAndTouchesExactBoundary) {
  Routing_occupancy source(1000);
  source.insert({
      {"M1", {0, 0, 10, 10}},
      "ordinary"
  });
  const double limit = std::numeric_limits<int32_t>::max();
  EXPECT_EQ(source.insert_run(
                {
                    {"V1", {limit - 100, 0, limit - 90, 10}},
                    "VDD"
  },
                -30,
                0,
                3),
            1);
  Routing_occupancy moved(std::move(source));
  EXPECT_EQ(source.size(), 0);
  EXPECT_EQ(source.insert_run(
                {
                    {"V1", {0, 0, 10, 10}},
                    "new"
  },
                30,
                0,
                3),
            0);
  EXPECT_EQ(moved.size(), 4);
  EXPECT_EQ(moved.query("V1", {limit - 130, 10, limit - 129, 11}, 0), (std::vector<size_t>{2}));
  EXPECT_TRUE(moved.query("V1", {limit - 119, 0, limit - 101, 10}, 0).empty());
  EXPECT_EQ(moved.at(3).shape,
            (Shape{
                "V1",
                {limit - 160, 0, limit - 150, 10}
  }));
  Routing_occupancy assigned(64);
  assigned = std::move(moved);
  EXPECT_EQ(assigned.size(), 4);
  EXPECT_EQ(moved.size(), 0);
  EXPECT_EQ(moved.insert({
                {"M2", {0, 0, 10, 10}},
                "reuse"
  }),
            0);
}
TEST(RoutingOccupancy, QueriesMatchBruteForceIncludingNegativeCoordinatesAndLargeShapes) {
  Routing_occupancy                  index(64);
  std::mt19937                       rng(417);
  std::uniform_int_distribution<int> coordinate(-2000, 2000), extent(1, 150);
  for (int i = 0; i < 600; ++i) {
    const int x = coordinate(rng), y = coordinate(rng);
    index.insert({
        {i % 2 ? "M2" : "M3", {double(x), double(y), double(x + extent(rng)), double(y + extent(rng))}},
        i % 3 ? "a" : "b"
    });
  }
  index.insert({
      {"M2", {-100000, -100000, 100000, 100000}},
      {}
  });
  for (int i = 0; i < 200; ++i) {
    const int           x = coordinate(rng), y = coordinate(rng), margin = i % 60;
    const Rect          rect{double(x), double(y), double(x + extent(rng)), double(y + extent(rng))};
    const std::string   layer = i % 2 ? "M2" : "M3", ignore = i % 3 ? "a" : "";
    std::vector<size_t> expected;
    for (size_t j = 0; j < index.size(); ++j) {
      const auto& s = index.at(j);
      if (s.shape.layer == layer && (ignore.empty() || s.net != ignore) && s.shape.rect.xl <= rect.xh + margin
          && s.shape.rect.xh >= rect.xl - margin && s.shape.rect.yl <= rect.yh + margin && s.shape.rect.yh >= rect.yl - margin) {
        expected.push_back(j);
      }
    }
    EXPECT_EQ(index.query(layer, rect, margin, ignore), expected);
  }
  EXPECT_EQ(index.query("M2", {-200000, -200000, 200000, 200000}, 0).size(), 301);
}
TEST(RoutingOccupancy, DenseGridPreservesExactShapeIdsAndOwnership) {
  Routing_occupancy index(1000);
  constexpr int     side = 256;
  for (int x = 0; x < side; ++x) {
    for (int y = 0; y < side; ++y) {
      const double xl = (x - side / 2) * 1000 + 100;
      const double yl = (y - side / 2) * 1000 + 100;
      EXPECT_EQ(index.insert({
                    {"M2", {xl, yl, xl + 18, yl + 18}},
                    "supply"
      }),
                size_t(x * side + y));
    }
  }
  for (int x = 0; x < side; x += 7) {
    for (int y = 0; y < side; y += 11) {
      const double xl = (x - side / 2) * 1000 + 100;
      const double yl = (y - side / 2) * 1000 + 100;
      EXPECT_EQ(index.query("M2", {xl, yl, xl + 18, yl + 18}, 0), (std::vector<size_t>{size_t(x * side + y)}));
      EXPECT_TRUE(index.query("M2", {xl, yl, xl + 18, yl + 18}, 0, "supply").empty());
    }
  }
}
TEST(RoutingOccupancy, LongRailsAndBroadObstructionsMatchBruteForce) {
  Routing_occupancy index(64);
  for (int i = -40; i < 40; ++i) {
    const double p = i * 64;
    index.insert({
        {"M2", {-100000, p, 100000, p + 64}},
        i % 2 ? "VDD" : "VSS"
    });
    index.insert({
        {"M2", {p, -100000, p + 64, 100000}},
        i % 2 ? "VDD" : "VSS"
    });
  }
  index.insert({
      {"M2", {-100000, -100000, 100000, 100000}},
      {}
  });
  index.insert({
      {"M2", {-32, -32, 32, 32}},
      "signal"
  });
  for (const auto& rect : std::vector<Rect>{
           {    -64,     -64,       0,       0},
           {      0,       0,      64,      64},
           {-100001, -100001, -100000, -100000},
           {-200000, -200000,  200000,  200000},
           {      0, -100000,       1,  100000}
  }) {
    for (int margin : {0, 1, 64, 2000}) {
      for (const std::string ignore : {"", "VDD", "unknown"}) {
        std::vector<size_t> expected;
        for (size_t id = 0; id < index.size(); ++id) {
          const auto  shape = index.at(id);
          const auto& r     = shape.shape.rect;
          if ((ignore.empty() || shape.net != ignore) && r.xl <= rect.xh + margin && r.xh >= rect.xl - margin
              && r.yl <= rect.yh + margin && r.yh >= rect.yl - margin) {
            expected.push_back(id);
          }
        }
        EXPECT_EQ(index.query("M2", rect, margin, ignore), expected);
      }
    }
  }
}
TEST(RoutingOccupancy, CompactRecordsRetainExactCoordinatesAndOwningNames) {
  Routing_occupancy index(64);
  const double      limit = std::numeric_limits<int32_t>::max();
  const std::string layer(80, 'L'), owner(100, 'n');
  const Rect        rect{-limit, limit - 1, -limit + 1, limit};
  index.insert({
      {layer, rect},
      owner
  });
  auto saved = index.at(0);
  for (int i = 0; i < 5000; ++i) {
    index.insert({
        {"M2", {double(i), 0, double(i + 1), 1}},
        std::to_string(i)
    });
  }
  EXPECT_EQ(saved.shape.rect, rect);
  EXPECT_EQ(saved.shape.layer, layer);
  EXPECT_EQ(saved.net, owner);
  EXPECT_EQ(index.at(0).shape.rect, rect);
  EXPECT_EQ(index.query(layer, rect, 0), (std::vector<size_t>{0}));
  EXPECT_TRUE(index.query(layer, rect, 0, owner).empty());
  EXPECT_EQ(index.query(layer, rect, 0, "unknown"), (std::vector<size_t>{0}));
  auto copy = index;
  index.insert({
      {"M2", {0, 2, 1, 3}},
      "later"
  });
  EXPECT_EQ(copy.size(), 5001);
  EXPECT_EQ(copy.at(0).net, owner);
  EXPECT_THROW(copy.at(copy.size()), std::out_of_range);
  EXPECT_THROW(index.insert({
                   {layer, {-limit - 1, 0, 1, 1}},
                   owner
  }),
               std::invalid_argument);
}
TEST(RoutingOccupancy, MovedIndexRetainsIdsAndSourceCanBeReused) {
  Routing_occupancy source(64);
  source.insert({
      {"M2", {0, 0, 10, 10}},
      "original"
  });
  auto destination = std::move(source);
  ASSERT_EQ(destination.size(), 1);
  EXPECT_EQ(destination.at(0).net, "original");
  EXPECT_EQ(source.insert({
                {"M3", {20, 20, 30, 30}},
                "new"
  }),
            0);
  EXPECT_EQ(source.size(), 1);
  EXPECT_EQ(source.query("M3", {20, 20, 30, 30}, 0), (std::vector<size_t>{0}));
  EXPECT_EQ(source.at(0).net, "new");
  EXPECT_EQ(destination.query("M2", {0, 0, 10, 10}, 0), (std::vector<size_t>{0}));
}
TEST(RoutingOccupancy, ConstructionCheckpointCancelsWithoutChangingFrame) {
  Technology tech;
  tech.layers["M2"] = {};
  Floorplan fp;
  for (int i = 0; i < 2000; ++i) {
    Terminal pin;
    pin.name   = std::to_string(i);
    pin.status = Status::PLACED;
    pin.x      = i * 20;
    pin.shapes = {
        {"M2", {0, 0, 10, 10}}
    };
    fp.terminals.push_back(std::move(pin));
  }
  const auto original = fp.terminals;
  int        calls    = 0;
  EXPECT_THROW(routing_occupancy(tech,
                                 fp,
                                 1000,
                                 [&] {
                                   if (++calls == 2) {
                                     throw std::runtime_error("cancel index");
                                   }
                                 }),
               std::runtime_error);
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(fp.terminals, original);
  EXPECT_EQ(routing_occupancy(tech, fp, 1000).size(), 2000);
}
TEST(RoutingOccupancy, InvalidProposalDoesNotPartiallyOccupyGrid) {
  Routing_occupancy index(10);
  index.insert({
      {"M2", {0, 0, 18, 100}},
      "existing"
  });
  const std::vector<Occupied_shape> invalid = {
      {{"M2", {30, 0, 48, 100}}, "trial"},
      {{"M2", {60, 0, 60, 100}}, "trial"}
  };
  EXPECT_THROW(index.insert_many(invalid), std::invalid_argument);
  EXPECT_EQ(index.size(), 1);
  EXPECT_EQ(index.query("M2", {18, 0, 20, 100}, 0).size(), 1);
  EXPECT_TRUE(index.query("M2", {0, 0, 18, 100}, 0, "existing").empty());
  EXPECT_THROW(index.query("M2", {0, 0, 18, 100}, -1), std::invalid_argument);
}
TEST(RoutingOccupancy, OverflowingProposalDoesNotPartiallyOccupyOverlay) {
  auto                 base = std::make_shared<Routing_occupancy>(10);
  const Occupied_shape existing{
      {"V1", {0, 0, 18, 18}},
      "VDD"
  };
  // A duplicate progression exercises logical-ID exhaustion without allocating
  // enormous storage. The overlay must reject the entire batch before insertion.
  base->insert_run(existing, 0, 0, std::numeric_limits<size_t>::max() - 1);
  Routing_occupancy    overlay(base);
  const Occupied_shape trial{
      {"M2", {30, 0, 48, 100}},
      "trial"
  };
  EXPECT_THROW(overlay.insert_many({trial, trial}), std::length_error);
  EXPECT_EQ(overlay.size(), base->size());
  EXPECT_TRUE(overlay.query("M2", {0, 0, 100, 100}, 0).empty());
  EXPECT_EQ(overlay.insert(trial), base->size());
  EXPECT_EQ(overlay.at(base->size()).shape, trial.shape);
  EXPECT_THROW(overlay.insert_many({trial}), std::length_error);
  EXPECT_NO_THROW(overlay.insert_many({}));
}
TEST(RoutingOccupancy, SeedsMirroredPinsObstructionsAndRoutedMetalWithOwnership) {
  Technology tech;
  tech.layers["M1"]       = {};
  tech.layers["M2"].width = .018;
  auto& m                 = tech.macros["cell"];
  m.width                 = .1;
  m.height                = .2;
  m.klass                 = "CORE";
  m.pins["A"]             = {"INPUT", "SIGNAL", {{"M1", {.01, .02, .03, .06}}}};
  m.obstructions          = {
      {"M2", {.04, .04, .06, .08}}
  };
  Floorplan fp;
  Ann_place loc(100, 200, 100, 200);
  loc.status      = Status::FIXED;
  loc.orientation = Orientation::FS;
  fp.components.push_back({"u0", "cell", loc});
  Net net;
  net.name        = "a";
  net.connections = {
      {"u0", "A"}
  };
  net.wires = {
      {"FIXED", {{"M2", {}, {}, {Route_point{500, 500, 0}, Route_point{600, 500, 0}}}}}
  };
  fp.nets.push_back(net);
  const auto index = routing_occupancy(tech, fp, 64);
  const auto pin   = index.query("M1", {110, 340, 130, 380}, 0);
  ASSERT_EQ(pin.size(), 1);
  EXPECT_EQ(index.at(pin[0]).net, "a");
  EXPECT_EQ(index.at(pin[0]).shape.rect, (Rect{110, 340, 130, 380}));
  EXPECT_TRUE(index.query("M1", {110, 340, 130, 380}, 0, "a").empty());
  EXPECT_EQ(index.query("M2", {140, 320, 160, 360}, 0, "a").size(), 1);
  EXPECT_EQ(index.query("M2", {500, 491, 600, 509}, 0).size(), 1);
  EXPECT_TRUE(index.query("M2", {500, 491, 600, 509}, 0, "a").empty());
}
TEST(RoutingOccupancy, PolygonPinPlaceholderBecomesUnownedBlockage) {
  Technology tech;
  tech.layers["M1"] = {};
  auto& m           = tech.macros["cell"];
  m.width           = .1;
  m.height          = .2;
  m.klass           = "CORE";
  m.pins["A"]       = {"INPUT", "SIGNAL", {{"M1", {.01, .02, .01, .02}}}};
  Floorplan fp;
  Ann_place loc(100, 200, 100, 200);
  loc.status = Status::PLACED;
  fp.components.push_back({"u0", "cell", loc});
  const auto index = routing_occupancy(tech, fp, 64);
  ASSERT_EQ(index.size(), 1);
  EXPECT_EQ(index.at(0).shape.rect, (Rect{100, 200, 200, 400}));
  EXPECT_TRUE(index.at(0).net.empty());
}
TEST(RoutingOccupancy, SharedBasePreservesIdsOwnershipAndTrialIsolation) {
  Routing_occupancy original(64);
  original.insert({
      {"M2", {-100000, 0, 100000, 18}},
      "VDD"
  });
  original.insert({
      {"M1", {0, 0, 10, 10}},
      {}
  });
  const auto        base = std::make_shared<const Routing_occupancy>(std::move(original));
  Routing_occupancy a(base), b(base);
  EXPECT_EQ(a.insert({
                {"M2", {0, 0, 18, 100}},
                "signal"
  }),
            2);
  EXPECT_EQ(b.insert({
                {"M3", {20, 0, 38, 100}},
                "VDD"
  }),
            2);
  EXPECT_EQ(base->size(), 2);
  EXPECT_EQ(a.query("M2", {0, 0, 18, 18}, 0), (std::vector<size_t>{0, 2}));
  EXPECT_EQ(a.query("M2", {0, 0, 18, 18}, 0, "VDD"), (std::vector<size_t>{2}));
  EXPECT_EQ(a.query("M2", {0, 0, 18, 18}, 0, "signal"), (std::vector<size_t>{0}));
  EXPECT_EQ(b.query("M2", {0, 0, 18, 18}, 0), (std::vector<size_t>{0}));
  EXPECT_TRUE(a.query("M3", {20, 0, 38, 100}, 0).empty());
  EXPECT_TRUE(b.query("M3", {20, 0, 38, 100}, 0, "VDD").empty());
  EXPECT_EQ(a.query("M1", {0, 0, 10, 10}, 0, "VDD"), (std::vector<size_t>{1}));
  auto copy = a;
  copy.insert({
      {"M2", {30, 0, 48, 100}},
      "other"
  });
  EXPECT_EQ(a.size(), 3);
  EXPECT_EQ(copy.size(), 4);
  const auto        second_base = std::make_shared<const Routing_occupancy>(std::move(copy));
  Routing_occupancy nested(second_base);
  EXPECT_EQ(nested.insert({
                {"M2", {60, 0, 78, 100}},
                "nested"
  }),
            4);
  EXPECT_EQ(nested.query("M2", {-100001, -1, 100001, 101}, 0), (std::vector<size_t>{0, 2, 3, 4}));
  EXPECT_EQ(nested.at(4).net, "nested");
  EXPECT_EQ(nested.at(0).net, "VDD");
  EXPECT_THROW(nested.at(5), std::out_of_range);
  EXPECT_THROW(nested.query("absent", {0, 0, 10, 10}, -1), std::invalid_argument);
  EXPECT_THROW(nested.insert_many({
                   {{"M2", {90, 0, 108, 100}}, "trial"},
                   {    {"M2", {0, 0, 0, 10}},   "bad"}
  }),
               std::invalid_argument);
  EXPECT_EQ(nested.size(), 5);
}
TEST(RoutingOccupancy, PowerGridRunCompressionPreservesDecoderOrderAndCutGaps) {
  Technology tech;
  tech.layers["M1"].width         = .018;
  tech.layers["M2"].width         = .020;
  tech.cut_layers["V1"].width     = .018;
  tech.via_rules["rule"].generate = true;
  Floorplan     fp;
  Generated_via g;
  g.rule        = "rule";
  g.layers      = {"M1", "V1", "M2"};
  g.cut_size    = {18, 18};
  g.cut_spacing = {18, 18};
  g.enclosure   = {0, 0, 2, 0};
  g.row_col     = {3, 40};
  Via via;
  via.name      = "array";
  via.generated = g;
  fp.vias.push_back(via);
  Net net;
  net.name = "VDD";
  net.use  = "POWER";
  net.wires.push_back({"FIXED", {{"M1", 18, {}, {Route_point{0, 0, 0}, Route_via{"array"}}}}});
  fp.special_nets.push_back(net);
  Routing_power_grid pg(tech, fp, 64);
  auto               compact = pg.overlay(tech, fp, 64);
  Routing_occupancy  expanded(64);
  for (const auto& shape : route_geometry(tech, fp, net, true)) {
    expanded.insert({shape, net.name});
  }
  ASSERT_EQ(compact.size(), expanded.size());
  for (size_t id = 0; id < expanded.size(); ++id) {
    EXPECT_EQ(compact.at(id).shape, expanded.at(id).shape);
  }
  for (int x = -1000; x < 1000; x += 7) {
    for (int y = -100; y < 100; y += 11) {
      const Rect q{double(x), double(y), double(x + 1), double(y + 1)};
      EXPECT_EQ(compact.query("V1", q, 0), expanded.query("V1", q, 0));
      EXPECT_EQ(compact.query("V1", q, 18), expanded.query("V1", q, 18));
    }
  }
}
TEST(RoutingOccupancy, PowerGridReuseMatchesRebuildAndRefreshesPinOwnership) {
  Technology tech;
  tech.layers["M1"]       = {};
  tech.layers["M2"].width = .018;
  auto& master            = tech.macros["cell"];
  master.width = master.height = .1;
  master.pins["A"]             = {"INPUT", "SIGNAL", {{"M1", {.01, .02, .03, .04}}}};
  master.pins["VDD"]           = {"INOUT", "POWER", {{"M1", {0, 0, .01, .01}}}};
  Floorplan frame;
  Ann_place place(100, 200, 100, 100);
  place.status = Status::FIXED;
  frame.components.push_back({"u", "cell", place});
  frame.nets.push_back({"old", {{"u", "A"}}, {}, "SIGNAL"});
  frame.special_nets.push_back(
      {"VDD", {{"*", "VDD"}}, {{"FIXED", {{"M2", 20, {}, {Route_point{0, 1000, 0}, Route_point{1000, 1000, 0}}}}}}, "POWER"});
  const Routing_power_grid seed(tech, frame, 64);
  const auto               canonical = [](const Routing_occupancy& index) {
    std::vector<std::tuple<std::string, std::string, double, double, double, double, bool>> shapes;
    for (size_t i = 0; i < index.size(); ++i) {
      const auto s = index.at(i);
      shapes.emplace_back(s.shape.layer, s.net, s.shape.rect.xl, s.shape.rect.yl, s.shape.rect.xh, s.shape.rect.yh, s.routed);
    }
    std::sort(shapes.begin(), shapes.end());
    return shapes;
  };
  EXPECT_EQ(canonical(routing_occupancy(tech, frame, 64)), canonical(routing_occupancy(tech, frame, 64, {}, &seed)));
  frame.nets.front().wires = {
      {"FIXED", {{"M2", {}, {}, {Route_point{500, 500, 0}, Route_point{600, 500, 0}}}}}
  };
  auto static_frame = frame;
  static_frame.nets.front().wires.clear();
  const auto expected_static = canonical(routing_occupancy(tech, static_frame, 64));
  EXPECT_EQ(expected_static, canonical(routing_occupancy(tech, frame, 64, {}, nullptr, Signal_wires::exclude)));
  EXPECT_EQ(expected_static, canonical(routing_occupancy(tech, frame, 64, {}, &seed, Signal_wires::exclude)));
  EXPECT_GT(routing_occupancy(tech, frame, 64).size(), expected_static.size());
  EXPECT_EQ(frame.nets.front().wires.size(), 1);
  {
    auto selected_frame            = frame;
    auto selected_tech             = tech;
    selected_tech.cut_layers["V1"] = {};
    selected_frame.vias.push_back({
        "pair",
        {{"M1", {-10, -10, 10, 10}}, {"V1", {-5, -5, 5, 5}}, {"M2", {-12, -8, 12, 8}}},
        {}
    });
    selected_frame.special_nets.front().wires.front().paths.front().steps.push_back(Route_via{"pair"});
    const auto all = canonical(routing_occupancy(selected_tech, selected_frame, 64));
    for (const std::string layer : {"M1", "M2", "V1"}) {
      auto expected = all;
      std::erase_if(expected, [&](const auto& shape) { return std::get<0>(shape) != layer; });
      const auto selected = routing_occupancy(selected_tech, selected_frame, 64, {}, nullptr, Signal_wires::include, {layer});
      EXPECT_EQ(canonical(selected), expected);
      EXPECT_LT(selected.size(), all.size());
    }
    EXPECT_THROW(routing_occupancy(tech, frame, 64, {}, &seed, Signal_wires::include, {"M2"}), std::invalid_argument);
  }
  frame.nets.front().name = "new";
  frame.components.push_back({"buffer", "cell", Ann_place(300, 200, 100, 100)});
  frame.components.back().place.status = Status::FIXED;
  frame.special_nets.front().connections.push_back({"buffer", "VDD"});
  const auto trial = routing_occupancy(tech, frame, 64, {}, &seed);
  EXPECT_EQ(canonical(routing_occupancy(tech, frame, 64)), canonical(trial));
  EXPECT_TRUE(trial.query("M1", {110, 220, 130, 240}, 0, "new").empty());
  EXPECT_EQ(trial.query("M1", {110, 220, 130, 240}, 0, "old").size(), 1);
  EXPECT_TRUE(trial.query("M2", {0, 990, 1000, 1010}, 0, "VDD").empty());
  auto changed                                                   = frame;
  changed.special_nets.front().wires.front().paths.front().width = 22;
  EXPECT_THROW(routing_occupancy(tech, changed, 64, {}, &seed), std::invalid_argument);
  changed = frame;
  changed.vias.push_back({"new_via", {}, {}});
  EXPECT_THROW(routing_occupancy(tech, changed, 64, {}, &seed), std::invalid_argument);
  changed                                = frame;
  changed.nondefault_rules["wide"]["M2"] = 24;
  EXPECT_THROW(routing_occupancy(tech, changed, 64, {}, &seed), std::invalid_argument);
  changed = frame;
  ++changed.dbu;
  EXPECT_THROW(routing_occupancy(tech, changed, 64, {}, &seed), std::invalid_argument);
  EXPECT_THROW(routing_occupancy(tech, frame, 65, {}, &seed), std::invalid_argument);
  auto other_tech = tech;
  EXPECT_THROW(routing_occupancy(other_tech, frame, 64, {}, &seed), std::invalid_argument);
  int checks = 0;
  EXPECT_THROW((Routing_power_grid(tech,
                                   frame,
                                   64,
                                   [&] {
                                     if (++checks == 2) {
                                       throw std::runtime_error("cancel");
                                     }
                                   })),
               std::runtime_error);
  EXPECT_EQ(checks, 2);
  EXPECT_EQ(canonical(routing_occupancy(tech, frame, 64)), canonical(routing_occupancy(tech, frame, 64, {}, &seed)));
}
TEST(RoutingOccupancy, PowerGridRejectsUnknownPathAndViaLayers) {
  Technology tech;
  tech.layers["M1"].width = .018;
  tech.layers["M2"].width = .018;
  tech.cut_layers["V1"]   = {};
  Floorplan frame;
  frame.special_nets.push_back(
      {"VDD", {}, {{"FIXED", {{"missing", 20, {}, {Route_point{0, 100, 0}, Route_point{200, 100, 0}}}}}}, "POWER"});
  EXPECT_THROW((Routing_power_grid(tech, frame, 64)), std::runtime_error);
  auto& path = frame.special_nets.front().wires.front().paths.front();
  path.layer = "M2";
  path.steps.push_back(Route_via{"pair"});
  frame.vias.push_back({
      "pair",
      {{"M1", {-10, -10, 10, 10}}, {"unknown_cut", {-5, -5, 5, 5}}, {"M2", {-12, -8, 12, 8}}},
      {}
  });
  EXPECT_THROW((Routing_power_grid(tech, frame, 64)), std::runtime_error);
  frame.vias.front().shapes[1].layer = "V1";
  const Routing_power_grid seed(tech, frame, 64);
  const auto               indexed = seed.overlay(tech, frame, 64);
  ASSERT_EQ(indexed.size(), 4);
  EXPECT_EQ(indexed.query("V1", {195, 95, 205, 105}, 0).size(), 1);
  EXPECT_EQ(indexed.query("M1", {190, 90, 210, 110}, 0).size(), 1);
}
}  // namespace livehd::physical
