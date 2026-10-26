// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "producer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

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
  livehd::placement::Options options;
  options.lefs       = {lef.string()};
  options.liberty    = lib.string();
  options.floorplan  = def.string();
  options.output_dir = (root / "run").string();
  options.fp_place = options.final_cells = options.macro_snap = false;
  options.fp_pin_adjust = options.macro_pin_adjust = false;
  options.halo_x = options.halo_y = 0;
  options.optimization_pending    = true;
  options.preserve_imported_ties  = true;
  options.threads                 = 1;
  options.iterations = options.min_iterations = 12;
  options.bins                                = 4;
  options.density                             = .5;
  options.post_density_factor                 = 0;
  options.routability                         = false;
  options.resistance_ohm_um = options.capacitance_ff_um = 0;
  options.restart_seed                                  = std::make_shared<livehd::physical::Floorplan>(env.frame);
  f.timing_settings.start                               = 0;
  f.timing_settings.continuing_rollback                 = true;
  options.native_gp_timing                              = std::make_shared<tc::Gp_timing_settings>(f.timing_settings);
  auto buffering                                        = std::make_shared<b::Checkpoint_settings>(f.settings);
  options.native_gp_buffering                           = buffering;
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
