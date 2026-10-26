// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "producer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

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
#if DREAMPLACE_NATIVE_DENSITY_RESERVATION_API >= 1 && DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
struct Environment {
  livehd::physical::Technology tech;
  livehd::physical::Floorplan  frame;
  b::Producer_settings         settings;
  Environment(const Fixture& f) {
    namespace p = livehd::physical;
    tech.dbu = frame.dbu = 1000;
    frame.die            = {0, 0, 10000, 2000};
    tech.sites["core"]   = {1, 1, true};
    frame.rows           = {
        { "r", "core", 0,    0, 10, 1000,  p::Orientation::N},
        {"r2", "core", 0, 1000, 10, 1000, p::Orientation::FS}
    };
    for (const auto& master : {"D", "S", "B"}) {
      p::Macro macro;
      macro.width = macro.height = 1;
      macro.klass                = "CORE";
      macro.site                 = "core";
      macro.symmetry_x           = true;
      macro.pins["A"]            = {"INPUT", "SIGNAL", {{"M1", {0, .49, .02, .51}}}};
      macro.pins["Y"]            = {"OUTPUT", "SIGNAL", {{"M1", {.98, .49, 1, .51}}}};
      tech.macros[master]        = macro;
    }
    for (const auto& c : f.rc.cells) {
      p::Component cell;
      cell.name         = c.name;
      cell.macro        = c.master;
      cell.place.x      = c.x;
      cell.place.y      = c.y;
      cell.place.status = p::Status::PLACED;
      frame.components.push_back(cell);
    }
    settings.max_seconds                    = 2;
    settings.options.max_attachment_dbu     = 5000;
    settings.options.max_options_per_vertex = 4;
    settings.search.max_assignments         = 256;
  }
  auto generate(const Fixture& f) const {
    const auto model = b::physical_buffer_model(tech, *f.timing_settings.models, {"B", "A", "Y"});
    if (!model.model) {
      throw std::runtime_error(model.reason);
    }
    return b::generate_checkpoint(f.input(),
                                  *f.timing_settings.models,
                                  f.timing_settings.driver,
                                  tech,
                                  frame,
                                  {},
                                  {},
                                  {*model.model},
                                  settings);
  }
};
TEST(CheckpointProducer, DiscoversFreshCriticalOwnersAndRecordsUnsupportedContexts) {
  Fixture    f;
  const auto r = b::discover_checkpoint_demand(f.input(), f.timing_settings.driver, 1000, 32);
  ASSERT_TRUE(r.supported_api);
  EXPECT_EQ(r.critical_nets, 3u);
  EXPECT_EQ(r.nets.size(), 3u);
  EXPECT_EQ(r.missing_contexts, 2u);
  const auto found = std::find_if(r.nets.begin(), r.nets.end(), [](const auto& n) { return n.owner == "u:Y"; });
  ASSERT_NE(found, r.nets.end());
  ASSERT_TRUE(found->context);
  EXPECT_EQ(found->context->driver.model.cell, "D");
  EXPECT_EQ(found->context->driver.model.input, "A");
  EXPECT_DOUBLE_EQ(found->context->driver.input_arrival_ps[0], 10);
  const auto bounded = b::discover_checkpoint_demand(f.input(), f.timing_settings.driver, 1000, 1);
  EXPECT_EQ(bounded.nets.size(), 1u);
  EXPECT_EQ(bounded.omitted_nets, 2u);
}
TEST(CheckpointProducer, FreshSelectedCandidatesReachTheJointReservationPath) {
  Fixture     f;
  Environment env(f);
  auto        r = env.generate(f);
  ASSERT_TRUE(r.supported_api);
  ASSERT_EQ(r.problem.nets.size(), 3u);
  const auto joint = b::prepare_checkpoint(f.input(),
                                           r.problem,
                                           {
  },
                                           *f.timing_settings.models,
                                           f.base,
                                           {{"u:Y", 2}},
                                           f.timing_settings.driver,
                                           f.settings);
  EXPECT_EQ(joint.critical_nets, 3u);
  EXPECT_EQ(joint.missing_contexts, 2u);
  EXPECT_GE(joint.density.virtual_buffers, 1u);
  EXPECT_FALSE(joint.negotiation->input_search_complete);
  EXPECT_TRUE(f.physical.density_reservations.empty());
}
TEST(CheckpointProducer, ContinuousPinGeometryIsPreservedThroughExplicitRcAttachments) {
  Fixture f;
  f.physical.pin_offset_x[2] += .25;
  f.physical.pin_offset_x[3] += .5;
  f.refresh(f.physical);
  Environment env(f);
  auto        r     = env.generate(f);
  const auto  found = std::find_if(r.problem.nets.begin(), r.problem.nets.end(), [](const auto& n) { return n.owner == "u:Y"; });
  ASSERT_NE(found, r.problem.nets.end());
  ASSERT_TRUE(found->context);
  ASSERT_FALSE(found->candidates.empty());
  EXPECT_DOUBLE_EQ(found->context->driver.output.x, 3.00025);
  EXPECT_DOUBLE_EQ(found->context->sinks[0].position.x, 8.0005);
  const auto& direct = found->candidates[0].candidate.trees[0];
  EXPECT_EQ(direct.positions[direct.driver_node], found->context->driver.output);
  bool exact = false;
  for (size_t i = 0; i < direct.rc.nodes.size(); ++i) {
    if (direct.rc.nodes[i].pin == "v:A") {
      exact = direct.positions[i] == found->context->sinks[0].position;
    }
  }
  EXPECT_TRUE(exact);
}
TEST(CheckpointProducer, RejectsChangedFixedGeometryAndInvalidSelectionBounds) {
  Fixture     f;
  Environment env(f);
  env.frame.components[0].place.status = livehd::physical::Status::FIXED;
  ++env.frame.components[0].place.x;
  EXPECT_THROW(env.generate(f), std::invalid_argument);
  EXPECT_THROW(b::discover_checkpoint_demand(f.input(), f.timing_settings.driver, 1000, 0), std::invalid_argument);
}
TEST(CheckpointProducer, ActualPublicPlacementUsesFreshGenerationAtEveryCheckpoint) {
  Fixture     f;
  Environment env(f);
  env.frame.design = "joint_checkpoint";
  env.frame.nets   = {
      {"n0", {{"PIN", "a"}, {"u", "A"}}},
      {"n1",   {{"u", "Y"}, {"v", "A"}}},
      {"n2", {{"v", "Y"}, {"PIN", "z"}}},
      {"nc",           {{"PIN", "clk"}}}
  };
  for (const auto& [name, direction, x] : std::vector<std::tuple<std::string, std::string, int64_t>>{
           {  "a",  "INPUT",     0},
           {  "z", "OUTPUT", 10000},
           {"clk",  "INPUT",     0}
  }) {
    livehd::physical::Terminal t;
    t.name      = name;
    t.net       = name;
    t.direction = direction;
    t.x         = x;
    t.y         = 500;
    t.status    = livehd::physical::Status::FIXED;
    env.frame.terminals.push_back(t);
  }
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "generated_public_place";
  std::filesystem::create_directories(root);
  const auto lef = root / "cells.lef", lib = root / "cells.lib", def = root / "frame.def";
  {
    std::ofstream file(lib);
    file << f.liberty;
  }
  {
    std::ofstream file(lef);
    file << "VERSION 5.8 ;\nUNITS DATABASE MICRONS 1000 ; END UNITS\nMANUFACTURINGGRID 0.001 ;\nLAYER M1 TYPE ROUTING ; DIRECTION "
            "HORIZONTAL ; PITCH 0.1 ; WIDTH 0.05 ; END M1\nSITE core CLASS CORE ; SYMMETRY X Y ; SIZE 1 BY 1 ; END core\n";
    for (const auto& name : {"D", "S", "B"}) {
      file << "MACRO " << name
           << "\nCLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ; SYMMETRY X Y ; SITE core ;\nPIN A DIRECTION INPUT ; USE SIGNAL ; PORT "
              "LAYER M1 ; RECT 0 0.49 0.02 0.51 ; END END A\nPIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.98 0.49 "
              "1 0.51 ; END END Y\nEND "
           << name << "\n";
    }
    file << "END LIBRARY\n";
  }
  livehd::physical::write_def(env.frame, def.string());
  env.tech         = livehd::physical::read_lef({lef.string()});
  const auto model = b::physical_buffer_model(env.tech, *f.timing_settings.models, {"B", "A", "Y"});
  ASSERT_TRUE(model.model);
  env.settings.sites.occupy_movable = false;
  auto options   = livehd::placement::buffering::test_fixture::public_placement_options(f, lef, lib, def, root / "run", env.frame);
  auto buffering = std::make_shared<b::Checkpoint_settings>(f.settings);
  options.native_gp_buffering = buffering;
  size_t calls = 0, previous_seen = 0;
  buffering->generate = [&](const b::Checkpoint_input& input) {
    ++calls;
    previous_seen  += input.previous != nullptr;
    auto generated  = b::generate_checkpoint(input,
                                            *f.timing_settings.models,
                                            f.timing_settings.driver,
                                            env.tech,
                                            env.frame,
                                             {},
                                             {},
                                             {*model.model},
                                            env.settings);
    EXPECT_EQ(generated.critical_nets, 3u);
    EXPECT_EQ(generated.missing_contexts, 2u);
    return std::move(generated.problem);
  };
  hhds::GraphLibrary placed;
  const auto         result = livehd::placement::place(*f.graph, placed, options);
  EXPECT_EQ(calls, 3u);
  EXPECT_EQ(previous_seen, 2u);
  EXPECT_EQ(result.floorplan.components.size(), 2u);
  std::ifstream journal(root / "run/native_gp_buffering.jsonl");
  ASSERT_TRUE(journal);
  size_t updates = 0;
  for (std::string line; std::getline(journal, line);) {
    rapidjson::Document entry;
    entry.Parse(line.c_str());
    ASSERT_FALSE(entry.HasParseError());
    EXPECT_GE(entry["virtual_buffers"].GetUint64(), 1u);
    EXPECT_EQ(entry["critical_nets"].GetUint64(), 3u);
    ++updates;
  }
  EXPECT_EQ(updates, 3u);
}
#else
TEST(CheckpointProducer, PublishedDependencyReportsUnsupported) {
  Fixture f;
  EXPECT_FALSE(b::discover_checkpoint_demand(f.input(), f.timing_settings.driver, 1000, 32).supported_api);
}
#endif
}  // namespace
