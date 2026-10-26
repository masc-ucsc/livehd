// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "checkpoint.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "gtest/gtest.h"
#include "inou/lefdef/mapped_graph.hpp"
#include "node_util.hpp"
#include "pass/place/forces/gp_timing.hpp"
#include "pass/place/place.hpp"
#include "rapidjson/document.h"

namespace b    = livehd::placement::buffering;
namespace dp   = DreamPlace::native;
namespace tc   = livehd::placement::timeclosure;
namespace late = livehd::placement::late;
namespace m    = livehd::liberty::timing;
namespace {
struct Fixture {
  hhds::GraphLibrary                               owner;
  std::shared_ptr<hhds::Graph>                     graph;
  tc::Gp_timing_settings                           timing_settings;
  b::Checkpoint_settings                           settings;
  dp::Design                                       physical;
  livehd::physical::Parasitics                     rc;
  std::map<std::string, std::pair<double, double>> locations;
  late::Result                                     timing;
  std::string                                      liberty;
  tc::Pin_weights                                  weights;
  b::Gap_force_payload                             base;
  Fixture() {
    const std::string text = R"(library(test){time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:0;
cell(D){pin(A){direction:input;capacitance:1;}pin(Y){direction:output;function:A;max_capacitance:1;
timing(){related_pin:A;timing_sense:positive_unate;cell_rise(scalar){values("5");}cell_fall(scalar){values("7");}rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}}
cell(B){pin(A){direction:input;capacitance:1;}pin(Y){direction:output;function:A;max_capacitance:100;
timing(){related_pin:A;timing_sense:positive_unate;cell_rise(scalar){values("1");}cell_fall(scalar){values("1");}rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}}
cell(S){pin(A){direction:input;capacitance:4;}pin(Y){direction:output;function:A;max_capacitance:100;
timing(){related_pin:A;timing_sense:positive_unate;cell_rise(scalar){values("3");}cell_fall(scalar){values("2.5");}rise_transition(scalar){values("1");}fall_transition(scalar){values("1");}}}}})";
    liberty                = text;
    timing_settings.models = std::make_shared<m::Library>(m::parse(text));
    auto io                = owner.create_io("joint_checkpoint");
    io->add_input("a", 0);
    io->add_input("clk", 1);
    io->add_output("z", 0);
    for (const auto& n : {"a", "clk", "z"}) {
      io->set_bits(n, 1);
    }
    graph           = io->create_graph();
    const auto cell = [&](const char* name, const char* master) {
      auto body = owner.create_io(master);
      body->add_input("A", 0);
      body->add_output("Y", 0);
      body->set_bits("A", 1);
      body->set_bits("Y", 1);
      auto n = livehd::graph_util::create_typed_node(*graph, Ntype_op::Sub);
      n.set_name(name);
      n.set_subnode(body);
      return n;
    };
    auto u = cell("u", "D"), v = cell("v", "S");
    u.create_sink_pin("A").connect_driver(graph->get_input_pin("a"));
    v.create_sink_pin("A").connect_driver(u.create_driver_pin("Y"));
    graph->get_output_pin("z").connect_driver(v.create_driver_pin("Y"));
    timing_settings.constraints.clocks["main"] = {.period_ps = 100, .roots = {"clk"}};
    timing_settings.constraints.inputs["a"]    = {
        {10, 10},
        { 1,  1},
        std::nullopt,
        "main"
    };
    timing_settings.constraints.outputs["z"] = {
        {100, 100},
        {  0,   0},
        "main"
    };
    timing_settings.constraints.complete = true;
    timing_settings.correction_max_age   = 1;
    timing_settings.interval             = 4;
    physical.layout                      = {0, 0, 10000, 2000, 1000, 1000};
    physical.num_movable                 = 2;
    physical.node_x                      = {2000, 8000, 0, 10000};
    physical.node_y                      = {0, 0, 500, 500};
    physical.node_size_x                 = {1000, 1000, 0, 0};
    physical.node_size_y                 = {1000, 1000, 0, 0};
    physical.node_macro                  = {0, 0, 0, 0};
    physical.pin2node                    = {2, 0, 0, 1, 1, 3};
    physical.pin_offset_x                = {0, 0, 1000, 0, 1000, 0};
    physical.pin_offset_y                = {0, 500, 500, 500, 500, 0};
    physical.flat_net2pin                = {0, 1, 2, 3, 4, 5};
    physical.flat_net2pin_start          = {0, 2, 4, 6};
    physical.build_maps();
    physical.validate();
    settings.start                  = 0;
    settings.allocation.max_seconds = 1;
    settings.generate               = [this](const b::Checkpoint_input& input) { return problem(input.locations); };
    refresh(physical);
#if DREAMPLACE_NATIVE_PIN_TIMING_API >= 3
    late::Proxy proxy(*graph, timing_settings.models, timing_settings.constraints);
    timing = proxy.evaluate(&rc, nullptr, {.capture_feasible_graph = true});
    tc::Pin_weight_bank bank;
    weights = bank.update(timing);
    base    = pin_weights(weights);
#endif
  }
  livehd::placement::Rc_policy policy() const {
    return {
        {0, 0, 0, 0},
        1000,
        "ideal_hermetic"
    };
  }
  void refresh(const dp::Design& d) {
    static const std::vector<std::string> names = {"a", "u:A", "u:Y", "v:A", "v:Y", "z"};
    locations.clear();
    for (size_t p = 0; p < names.size(); ++p) {
      const int n         = d.pin2node[p];
      locations[names[p]] = {d.node_x[n] + d.pin_offset_x[p], d.node_y[n] + d.pin_offset_y[p]};
    }
    rc.nets = {
        {  "a",   {{"a", 0}, {"u:A", 0}}, {{0, 1, 0}}},
        {"u:Y", {{"u:Y", 0}, {"v:A", 0}}, {{0, 1, 0}}},
        {"v:Y",   {{"v:Y", 0}, {"z", 0}}, {{0, 1, 0}}}
    };
    rc.cells = {
        {"u", int64_t(d.node_x[0]), int64_t(d.node_y[0]), 0, "D", 1000, 1000},
        {"v", int64_t(d.node_x[1]), int64_t(d.node_y[1]), 0, "S", 1000, 1000}
    };
  }
  b::Checkpoint_problem problem(const std::map<std::string, std::pair<double, double>>& pins) const {
    b::Checkpoint_problem result;
    result.sites = b::build_legal_sites(
        {
            {"r", {0, 0, 10000, 1000}, 1000}
    },
        {});
    b::Allocation_net net;
    net.owner          = "u:Y";
    net.classification = b::Net_class::outside_window;
    b::Context context;
    context.core                    = {0, 0, 10, 2};
    context.normalization_period_ps = 999;
    const auto pos = [&](const std::string& n) { return b::Point{pins.at(n).first / 1000, pins.at(n).second / 1000}; };
    context.driver = {
        "u",
        {"D", "A", "Y"},
        pos("u:Y"),
        {999, 999},
        {999, 999}
    };
    context.sinks = {
        {"v:A", "S", "A", pos("v:A"), {-999, -999}, {99, 99}}
    };
    context.capacity["sites:r"] = 10;
    net.context                 = context;
    b::Routing_tree tree;
    tree.vertices = {
        {pos("u:Y"),  {}, {}},
        {   {6, .5},  {}, {}},
        {pos("v:A"), {0}, {}}
    };
    const auto length = [](b::Point a, b::Point z) { return std::abs(a.x - z.x) + std::abs(a.y - z.y); };
    tree.segments     = {
        {0, 1, 0, 0, length(tree.vertices[0].position, tree.vertices[1].position), true},
        {1, 2, 0, 0, length(tree.vertices[1].position, tree.vertices[2].position), true}
    };
    b::Buffer buffer{
        "repair0",
        {"B", "A", "Y"},
        {5, .5},
        {6, .5},
        {5, 0, 6, 1},
        true,
        {{"sites:r", 1}}
    };
    tree.vertices[1].options = {
        {buffer, {"r", 5, 1}, true}
    };
    net.candidates = {
        {b::expand_tree(context, tree, {0, 1, 0}), {{"r", 5, 1}}, {}}
    };
    result.nets = {net};
    return result;
  }
  b::Gap_force_payload pin_weights(const tc::Pin_weights& fresh) const {
    b::Gap_force_payload p;
    p.driver.assign(6, -1);
    p.weight.assign(6, 0);
    const std::vector<std::string>   names   = {"a", "u:A", "u:Y", "v:A", "v:Y", "z"};
    const std::map<std::string, int> drivers = {
        {  "a", 0},
        {"u:Y", 2},
        {"v:Y", 4}
    };
    for (size_t i = 0; i < names.size(); ++i) {
      if (fresh.pins.contains(names[i])) {
        const auto& w = fresh.pins.at(names[i]);
        p.driver[i]   = drivers.at(w.driver);
        p.weight[i]   = w.weight;
      }
    }
    return p;
  }
  b::Checkpoint_input input(int iteration = 1) const { return {physical, timing, weights, rc, locations, iteration}; }
};
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
  auto                       technology = livehd::physical::read_lef({lef.string()});
  hhds::GraphLibrary         source, placed;
  auto                       graph = livehd::physical::mapped_graph(source, frame, technology);
  livehd::placement::Options o;
  o.lefs       = {lef.string()};
  o.liberty    = lib.string();
  o.floorplan  = def.string();
  o.output_dir = (root / "run").string();
  o.fp_place = o.final_cells = o.macro_snap = false;
  o.fp_pin_adjust = o.macro_pin_adjust = false;
  o.halo_x = o.halo_y      = 0;
  o.optimization_pending   = true;
  o.preserve_imported_ties = true;
  o.threads                = 1;
  o.iterations = o.min_iterations       = 12;
  o.bins                                = 4;
  o.density                             = .5;
  o.post_density_factor                 = 0;
  o.routability                         = false;
  o.resistance_ohm_um                   = 0;
  o.capacitance_ff_um                   = 0;
  o.restart_seed                        = std::make_shared<livehd::physical::Floorplan>(frame);
  f.timing_settings.start               = 0;
  f.timing_settings.continuing_rollback = true;
  o.native_gp_timing                    = std::make_shared<tc::Gp_timing_settings>(f.timing_settings);
  o.native_gp_buffering                 = std::make_shared<b::Checkpoint_settings>(f.settings);
  const auto result                     = livehd::placement::place(*graph, placed, o);
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
