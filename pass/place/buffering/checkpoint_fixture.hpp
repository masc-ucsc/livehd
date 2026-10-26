// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "checkpoint.hpp"
#include "node_util.hpp"
#include "pass/place/forces/gp_timing.hpp"
#include "pass/place/place.hpp"

// Two-cell native timing/buffering fixture shared by the checkpoint and
// producer tests: u (D) drives v (S) across a 10x2 um frame, with one
// prospective B buffer at site r/5 for the u:Y net.
namespace livehd::placement::buffering::test_fixture {
namespace b    = livehd::placement::buffering;
namespace dp   = DreamPlace::native;
namespace tc   = livehd::placement::timeclosure;
namespace late = livehd::placement::late;
namespace m    = livehd::liberty::timing;
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
// Public placement options for one fixed 12-iteration final GP of the
// fixture design with native timing; the caller adds native buffering.
inline livehd::placement::Options public_placement_options(Fixture& f, const std::filesystem::path& lef,
                                                           const std::filesystem::path& lib, const std::filesystem::path& def,
                                                           const std::filesystem::path&       output,
                                                           const livehd::physical::Floorplan& frame) {
  livehd::placement::Options o;
  o.lefs       = {lef.string()};
  o.liberty    = lib.string();
  o.floorplan  = def.string();
  o.output_dir = output.string();
  o.fp_place = o.final_cells = o.macro_snap = false;
  o.fp_pin_adjust = o.macro_pin_adjust = false;
  o.halo_x = o.halo_y      = 0;
  o.optimization_pending   = true;
  o.preserve_imported_ties = true;
  o.threads                = 1;
  o.iterations = o.min_iterations = 12;
  o.bins                          = 4;
  o.density                       = .5;
  o.post_density_factor           = 0;
  o.routability                   = false;
  o.resistance_ohm_um = o.capacitance_ff_um = 0;
  o.restart_seed                            = std::make_shared<livehd::physical::Floorplan>(frame);
  f.timing_settings.start                   = 0;
  f.timing_settings.continuing_rollback     = true;
  o.native_gp_timing                        = std::make_shared<tc::Gp_timing_settings>(f.timing_settings);
  return o;
}
}  // namespace livehd::placement::buffering::test_fixture
