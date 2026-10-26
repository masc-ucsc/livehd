// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "checkpoint.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "checkpoint_fixture.hpp"
#include "gtest/gtest.h"
#include "inou/lefdef/mapped_graph.hpp"
#include "rapidjson/document.h"

namespace b    = livehd::placement::buffering;
namespace dp   = DreamPlace::native;
namespace tc   = livehd::placement::timeclosure;
namespace late = livehd::placement::late;
namespace m    = livehd::liberty::timing;
namespace {
using livehd::placement::buffering::test_fixture::Fixture;
TEST(JointCheckpoint, InvalidSettings) {
  b::Checkpoint_settings s;
  EXPECT_THROW(s.validate(), std::invalid_argument);
}
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1 && DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
TEST(JointCheckpoint, RefreshesSourceWindowAndLrThenPairsPayloads) {
  Fixture f;
  auto    p = f.problem(f.locations);
  auto    r = b::prepare_checkpoint(f.input(),
                                 p,
                                    {
  },
                                 *f.timing_settings.models,
                                 f.base,
                                    {{"u:Y", 2}},
                                 f.timing_settings.driver,
                                 f.settings);
  ASSERT_TRUE(r.supported_api);
  ASSERT_TRUE(r.proposal);
  ASSERT_TRUE(r.negotiation->nets[0].candidate);
  EXPECT_EQ(r.critical_nets, 1u);
  EXPECT_EQ(r.outside_nets, 0u);
  EXPECT_EQ(r.density.virtual_buffers, 1u);
  EXPECT_EQ(r.forces.applied, 1u);
  EXPECT_DOUBLE_EQ(r.density.virtual_area, 1000000);
  EXPECT_EQ(r.proposal->allocation.sites.reservations().at("u:Y")[0].first, 5u);
  EXPECT_DOUBLE_EQ(r.negotiation->nets[0].timing->pins.at("v:A").arrival_ps[0], 16);
  EXPECT_DOUBLE_EQ(p.nets[0].context->driver.input_arrival_ps[0], 999);
  EXPECT_TRUE(f.physical.density_reservations.empty());
}
TEST(JointCheckpoint, UnmetOwnersKeepBothReservationAndPullUntilRetired) {
  Fixture f;
  auto    p                       = f.problem(f.locations);
  auto    r                       = b::prepare_checkpoint(f.input(),
                                 p,
                                                          {
  },
                                 *f.timing_settings.models,
                                 f.base,
                                                          {{"u:Y", 2}},
                                 f.timing_settings.driver,
                                 f.settings);
  auto    state                   = r.proposal;
  f.physical.density_reservations = state->density.footprints;
  p.nets[0].context.reset();
  auto kept = b::prepare_checkpoint(f.input(2),
                                    p,
                                    state,
                                    *f.timing_settings.models,
                                    f.base,
                                    {
                                        {"u:Y", 2}
  },
                                    f.timing_settings.driver,
                                    f.settings);
  EXPECT_EQ(kept.density.unmet, 1u);
  EXPECT_EQ(kept.density.virtual_buffers, 1u);
  EXPECT_EQ(kept.forces.applied, 1u);
  p.nets.clear();
  p.retired    = {"u:Y"};
  auto cleared = b::prepare_checkpoint(f.input(3),
                                       p,
                                       kept.proposal,
                                       *f.timing_settings.models,
                                       f.base,
                                       {
                                           {"u:Y", 2}
  },
                                       f.timing_settings.driver,
                                       f.settings);
  EXPECT_TRUE(cleared.proposal->density.footprints.empty());
  EXPECT_TRUE(cleared.proposal->pulls.empty());
  EXPECT_EQ(cleared.forces.applied, 0u);
}
TEST(JointCheckpoint, ProtectedGeometryFailureAndEpochChangeAreAtomic) {
  Fixture    f;
  auto       p                    = f.problem(f.locations);
  auto       r                    = b::prepare_checkpoint(f.input(),
                                 p,
                                                          {
  },
                                 *f.timing_settings.models,
                                 f.base,
                                                          {{"u:Y", 2}},
                                 f.timing_settings.driver,
                                 f.settings);
  auto       state                = r.proposal;
  const auto saved                = state->density;
  f.physical.density_reservations = saved.footprints;
  p.sites                         = b::build_legal_sites(
      {
          {"r", {0, 0, 10000, 1000}, 1000}
  },
      {{5000, 0, 6000, 1000}});
  EXPECT_THROW(b::prepare_checkpoint(f.input(2),
                                     p,
                                     state,
                                     *f.timing_settings.models,
                                     f.base,
                                     {
                                         {"u:Y", 2}
  },
                                     f.timing_settings.driver,
                                     f.settings),
               std::invalid_argument);
  EXPECT_EQ(state->density, saved);
  p = f.problem(f.locations);
  ++f.timing.topology_generation;
  EXPECT_THROW(b::prepare_checkpoint(f.input(2),
                                     p,
                                     state,
                                     *f.timing_settings.models,
                                     f.base,
                                     {
                                         {"u:Y", 2}
  },
                                     f.timing_settings.driver,
                                     f.settings),
               std::invalid_argument);
}
TEST(JointCheckpoint, RejectsOmittedSinkAndWrongMasterBeforeCommit) {
  Fixture f;
  auto    p = f.problem(f.locations);
  p.nets[0].context->sinks.clear();
  EXPECT_THROW(b::prepare_checkpoint(f.input(),
                                     p,
                                     {
  },
                                     *f.timing_settings.models,
                                     f.base,
                                     {{"u:Y", 2}},
                                     f.timing_settings.driver,
                                     f.settings),
               std::invalid_argument);
  p                                    = f.problem(f.locations);
  p.nets[0].context->driver.model.cell = "B";
  EXPECT_THROW(b::prepare_checkpoint(f.input(),
                                     p,
                                     {
  },
                                     *f.timing_settings.models,
                                     f.base,
                                     {{"u:Y", 2}},
                                     f.timing_settings.driver,
                                     f.settings),
               std::invalid_argument);
}
TEST(JointCheckpoint, ActualNativeGpRefreshesBothPayloadsThroughTimingSession) {
  Fixture                            f;
  tc::Gp_timing_session              session(*f.graph, f.timing_settings, f.policy());
  std::optional<b::Checkpoint_state> state;
  size_t                             calls = 0;
  dp::Options                        o;
  o.max_iterations = o.min_iterations = 12;
  o.bins_x                            = 16;
  o.bins_y                            = 4;
  o.target_density                    = .5;
  o.legalize = o.detailed_place = false;
  o.checkpoint_interval         = 4;
  o.checkpoint_rollback         = true;
  o.wirelength_gamma_start = o.wirelength_gamma_end = 1;
  o.checkpoint                                      = [&](const dp::Design& d, int iteration) {
    f.refresh(d);
    std::optional<b::Checkpoint_result> prepared;
    auto                                feedback = session.update(f.rc,
                                   f.locations,
                                   iteration,
                                   dp::density_overflow(d, 16, 4, .5),
                                                                                                       {},
                                   [&](const late::Result& timing, const tc::Pin_weights& w) {
                                     const b::Checkpoint_input input{d, timing, w, f.rc, f.locations, iteration};
                                     prepared = b::prepare_checkpoint(input,
                                                                      f.settings.generate(input),
                                                                      state,
                                                                      *f.timing_settings.models,
                                                                      f.pin_weights(w),
                                                                                                                                          {
                                                                          {"u:Y", 2}
                                     },
                                                                      f.timing_settings.driver,
                                                                      f.settings);
                                   });
    (void)feedback;
    EXPECT_TRUE(prepared->supported_api);
    ++calls;
    state = prepared->proposal;
    dp::GpUpdate u;
    u.pin_timing           = prepared->forces.timing;
    u.density_reservations = prepared->density.proposal.footprints;
    u.action               = dp::GpCheckpointAction::retain;
    return u;
  };
  auto result = dp::place(f.physical, o);
  EXPECT_EQ(calls, 3u);
  ASSERT_EQ(f.physical.density_reservations.size(), 1u);
  EXPECT_EQ(f.physical.density_reservations, state->density.footprints);
  EXPECT_EQ(f.physical.pin_timing->gap_pulls.size(), 1u);
  EXPECT_EQ(result.iterations, 12);
  EXPECT_NEAR(result.filler_area - result.effective_filler_area, 1000000, 1e-6);
}
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 2
TEST(JointCheckpoint, PublicLiveHdPlacementCommitsTheJointPayload) {
  Fixture f;
  f.timing_settings.startup_coefficient = .025;
  const auto root                       = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "joint_public_place";
  std::filesystem::create_directories(root);
  const auto lef = root / "cells.lef", lib = root / "cells.lib", def = root / "original.def";
  {
    std::ofstream out(lib);
    out << f.liberty;
  }
  {
    std::ofstream out(lef);
    out << R"(VERSION 5.8 ;
UNITS DATABASE MICRONS 1000 ; END UNITS
MANUFACTURINGGRID 0.001 ;
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.1 ; WIDTH 0.05 ; END M1
SITE core CLASS CORE ; SYMMETRY Y ; SIZE 1 BY 1 ; END core
)";
    for (const auto& name : {"D", "B", "S"}) {
      out << "MACRO " << name << R"(
CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ; SYMMETRY X Y ; SITE core ;
PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT -0.01 0.49 0.01 0.51 ; END END A
PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.99 0.49 1.01 0.51 ; END END Y
END )" << name
          << '\n';
    }
    out << "END LIBRARY\n";
  }
  livehd::physical::Floorplan frame;
  frame.design = "joint_checkpoint";
  frame.dbu    = 1000;
  frame.die    = {0, 0, 10000, 2000};
  frame.rows   = {
      {"r0", "core", 0,    0, 10, 1000, livehd::physical::Orientation::N},
      {"r1", "core", 0, 1000, 10, 1000, livehd::physical::Orientation::N}
  };
  for (auto [name, master, x] : std::vector<std::tuple<std::string, std::string, int>>{
           {"u", "D", 2000},
           {"v", "S", 8000}
  }) {
    livehd::physical::Component c;
    c.name              = name;
    c.macro             = master;
    c.place             = Ann_place(x, 0, 1000, 1000);
    c.place.status      = livehd::physical::Status::PLACED;
    c.place.orientation = livehd::physical::Orientation::N;
    frame.components.push_back(c);
  }
  for (auto [name, x, direction] : std::vector<std::tuple<std::string, int, std::string>>{
           {  "a",     0,  "INPUT"},
           {"clk",     0,  "INPUT"},
           {  "z", 10000, "OUTPUT"}
  }) {
    livehd::physical::Terminal p;
    p.name      = name;
    p.net       = name == "a" ? "n0" : name == "z" ? "n2" : "nc";
    p.x         = x;
    p.y         = 500;
    p.direction = direction;
    p.status    = livehd::physical::Status::FIXED;
    frame.terminals.push_back(p);
  }
  frame.nets = {
      {"n0", {{"PIN", "a"}, {"u", "A"}}},
      {"n1",   {{"u", "Y"}, {"v", "A"}}},
      {"n2", {{"v", "Y"}, {"PIN", "z"}}},
      {"nc",           {{"PIN", "clk"}}}
  };
  livehd::physical::write_def(frame, def.string());
  auto               technology = livehd::physical::read_lef({lef.string()});
  hhds::GraphLibrary source, placed;
  auto               graph = livehd::physical::mapped_graph(source, frame, technology);
  auto o = livehd::placement::buffering::test_fixture::public_placement_options(f, lef, lib, def, root / "run", frame);
  o.native_gp_buffering = std::make_shared<b::Checkpoint_settings>(f.settings);
  const auto result     = livehd::placement::place(*graph, placed, o);
  EXPECT_EQ(result.floorplan.components.size(), 2u);
  {
    std::ifstream report(root / "run/native_gp_startup.json");
    ASSERT_TRUE(report);
    const std::string   text((std::istreambuf_iterator<char>(report)), {});
    rapidjson::Document record;
    record.Parse(text.c_str());
    ASSERT_FALSE(record.HasParseError());
    EXPECT_DOUBLE_EQ(record["coefficient"].GetDouble(), .025);
    EXPECT_GT(record["selected_paths"].GetUint64(), 0u);
    EXPECT_GT(record["weighted_pins"].GetUint64(), 0u);
  }

  std::ifstream in(root / "run/native_gp_buffering.jsonl");
  ASSERT_TRUE(in);
  size_t rows = 0;
  for (std::string line; std::getline(in, line);) {
    rapidjson::Document record;
    record.Parse(line.c_str());
    ASSERT_FALSE(record.HasParseError());
    EXPECT_EQ(record["virtual_buffers"].GetUint64(), 1u);
    EXPECT_DOUBLE_EQ(record["reserved_area_um2"].GetDouble(), 1);
    EXPECT_EQ(record["critical_nets"].GetUint64(), 1u);
    EXPECT_FALSE(record["graph_edits"].GetBool());
    ++rows;
  }
  EXPECT_EQ(rows, 3u);
  auto bad      = std::make_shared<b::Checkpoint_settings>(f.settings);
  bad->generate = [&](const b::Checkpoint_input& input) {
    auto problem                    = f.problem(input.locations);
    problem.materialized["repair0"] = 0;
    return problem;
  };
  o.native_gp_buffering = bad;
  o.output_dir          = (root / "bad_identity").string();
  hhds::GraphLibrary rejected;
  try {
    livehd::placement::place(*graph, rejected, o);
    FAIL() << "mismatched materialized identity was accepted";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("actual physical instance identity"), std::string::npos);
  }
}
#endif
#else
TEST(JointCheckpoint, PublishedDependencyReportsUnsupported) {
  Fixture f;
  auto    r = b::prepare_checkpoint(f.input(),
                                 f.problem(f.locations),
                                    {},
                                 *f.timing_settings.models,
                                 f.base,
                                    {},
                                 f.timing_settings.driver,
                                 f.settings);
  EXPECT_FALSE(r.supported_api);
  EXPECT_FALSE(r.proposal);
  EXPECT_TRUE(f.physical.pin2node.size() == 6);
}
#endif
}  // namespace
