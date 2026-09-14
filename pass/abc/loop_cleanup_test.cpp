// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "loop_cleanup.hpp"

#include <cstdlib>

#include "abc_map.hpp"
#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass_color.hpp"
#include "pass_liberty.hpp"
#include "pass_partition.hpp"
#include "query.hpp"

namespace gu  = livehd::graph_util;
namespace abc = livehd::abc;

namespace {
struct Design {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> body, top;
  hhds::Node_class             independent, carried;

  explicit Design(bool parallel_data = false) {
    auto bio = lib.create_io("body");
    bio->add_input("a", 1);
    bio->add_input("idx", 2);
    bio->add_output("y", 3);
    bio->add_output("z", 4);
    for (auto name : {"a", "idx", "y", "z"}) {
      bio->set_bits(name, 4);
      bio->set_unsign(name, true);
    }
    body   = bio->create_graph();
    auto a = body->get_input_pin("a"), idx = body->get_input_pin("idx");
    gu::set_ubits(a, 4);
    gu::set_ubits(idx, 4);
    auto x = gu::create_typed_node(*body, Ntype_op::Xor);
    a.connect_sink(x.create_sink_pin(0));
    idx.connect_sink(x.create_sink_pin(0));
    auto xp = x.create_driver_pin(0);
    gu::set_ubits(xp, 4);
    auto sum = gu::create_typed_node(*body, Ntype_op::Sum);
    a.connect_sink(sum.create_sink_pin(0));
    xp.connect_sink(sum.create_sink_pin(0));
    auto y = sum.create_driver_pin(0);
    gu::set_ubits(y, 4);
    y.connect_sink(body->get_output_pin("y"));
    auto mask = gu::create_typed_node(*body, Ntype_op::And);
    a.connect_sink(mask.create_sink_pin(0));
    idx.connect_sink(mask.create_sink_pin(0));
    auto z = mask.create_driver_pin(0);
    gu::set_ubits(z, 4);
    z.connect_sink(body->get_output_pin("z"));
    if (parallel_data) {
      bio->add_input("data", 5);
      bio->set_bits("data", 4);
      bio->set_unsign("data", true);
      auto data = body->get_input_pin("data");
      gu::set_ubits(data, 4);
      auto independent_xor = gu::create_typed_node(*body, Ntype_op::Xor);
      data.connect_sink(independent_xor.create_sink_pin(0));
      idx.connect_sink(independent_xor.create_sink_pin(0));
      auto value = independent_xor.create_driver_pin(0);
      gu::set_ubits(value, 4);
      value.connect_sink(sum.create_sink_pin(0));
    }

    auto tio = lib.create_io("top");
    tio->add_input("x", 1);
    tio->set_bits("x", 4);
    tio->set_unsign("x", true);
    for (int i = 0; i < 4; ++i) {
      const auto name = "o" + std::to_string(i);
      tio->add_output(name, i + 2);
      tio->set_bits(name, 4);
      tio->set_unsign(name, true);
    }
    top        = tio->create_graph();
    auto input = top->get_input_pin("x");
    gu::set_ubits(input, 4);
    hhds::Subnode_loop loop;
    loop.first       = 1;
    loop.step        = 1;
    loop.count       = 3;
    loop.index_input = 2;
    for (int i = 0; i < 2; ++i) {
      auto n = gu::create_typed_node(*top, Ntype_op::Sub);
      n.set_subnode(bio, loop);
      n.set_name(i ? "carried" : "independent");
      input.connect_sink(n.create_sink_pin(1));
      for (int j = 0; j < 2; ++j) {
        auto out = n.create_driver_pin(j + 3);
        gu::set_ubits(out, 4);
        out.connect_sink(top->get_output_pin("o" + std::to_string(i * 2 + j)));
      }
      if (i) {
        n.create_driver_pin(3).connect_sink(n.create_sink_pin(1));
        carried = n;
      } else {
        independent = n;
      }
      if (parallel_data) {
        input.connect_sink(n.create_sink_pin(5));
      }
    }
  }
  auto graphs() const { return std::vector<std::shared_ptr<hhds::Graph>>{top, body}; }
};
}  // namespace

TEST(LoopCleanup, SameDefinitionCanBeIndependentAndCarried) {
  for (bool unroll : {false, true}) {
    Design                d;
    abc::Loop_preparation prep;
    ASSERT_TRUE(abc::prepare_loop_bodies(d.graphs(), unroll, prep));
    EXPECT_EQ(prep.independent, 1u);
    EXPECT_EQ(prep.carried, 1u);
    EXPECT_EQ(prep.expanded, unroll ? 1u : 0u);
    EXPECT_TRUE(prep.preserved_defs.contains(d.body->get_gid()));
    size_t compact = 0;
    for (auto n : d.top->body().nodes()) {
      if (n.is_loop_subnode()) {
        n.subnode_group().validate();
        ++compact;
      }
    }
    EXPECT_EQ(compact, unroll ? 1u : 2u);
  }
}

TEST(LoopCleanup, ActivationRecurrenceCountsAsCarry) {
  for (bool chained : {false, true}) {
    Design d;
    auto   io = d.body->get_io();
    io->add_input("active", 5);
    io->add_output("next_active", 6);
    io->set_bits("active", 1);
    io->set_bits("next_active", 1);
    auto active = d.body->get_input_pin("active");
    gu::set_ubits(active, 1);
    active.connect_sink(d.body->get_output_pin("next_active"));
    for (auto n : {d.independent, d.carried}) {
      auto loop             = *n.subnode_loop();
      loop.activation_input = 5;
      if (chained) {
        loop.next_active_output = 6;
      }
      n.set_subnode(io, loop);
      gu::create_const(*d.top, *Dlop::create_integer(1)).connect_sink(n.create_sink_pin(5));
    }
    abc::Loop_preparation prep;
    ASSERT_TRUE(abc::prepare_loop_bodies(d.graphs(), true, prep));
    EXPECT_EQ(prep.independent, chained ? 0u : 1u);
    EXPECT_EQ(prep.carried, chained ? 2u : 1u);
    EXPECT_EQ(prep.expanded, chained ? 2u : 1u);
  }
}

TEST(LoopCleanup, SynthColorsWholeBodyDespiteArithmeticCutsAndSizeLimits) {
  for (auto mode : {"cones", "synth", "pipe"}) {
    Design   d;
    Eprp_var var({
        {       "alg", "synth"},
        { "synth_alg",    mode},
        {       "top",   "top"},
        {    "min_ge",     "0"},
        {    "max_ge",     "1"},
        {  "max_gate",     "1"},
        {"continuous",  "true"}
    });
    for (auto g : d.graphs()) {
      var.add(g);
    }
    Pass_color::color(var);
    int color = 0;
    for (auto n : d.body->body().nodes()) {
      if (!livehd::color::is_partitionable(n)) {
        continue;
      }
      const int c = gu::node_color_of(n);
      ASSERT_NE(c, 0);
      if (color) {
        EXPECT_EQ(c, color);
      }
      color = c;
    }
    EXPECT_NE(color, 0);
    EXPECT_TRUE(d.independent.is_loop_subnode());
    EXPECT_TRUE(d.carried.is_loop_subnode());
  }
}

TEST(LoopCleanup, MappingThenStitchingPreservesIndependentAndCarriedResults) {
  const auto models_path = std::string(std::getenv("TEST_TMPDIR")) + "/loop-models";
  Eprp_var   model_var;
  Pass_liberty::setup();
  Pass::eprp.run_method_now("pass.liberty",
                            model_var,
                            {
                                {"files", "inou/prp/tests/abc/test.lib"},
                                {  "out",                   models_path}
  });
  auto& models = livehd::Hhds_graph_library::instance(models_path);
  for (bool unroll : {false, true}) {
    for (auto flatten : {livehd::partition::Flatten_mode::off, livehd::partition::Flatten_mode::on}) {
      SCOPED_TRACE(unroll);
      SCOPED_TRACE(static_cast<int>(flatten));
      Design ref(true), d(true);
      ASSERT_TRUE(gu::materialize_occurrences_all(ref.graphs(), "test"));
      abc::Loop_preparation prep;
      ASSERT_TRUE(abc::prepare_loop_bodies(d.graphs(), unroll, prep));
      EXPECT_EQ(prep.shared_bodies.size(), unroll ? 1u : 0u);
      auto graphs = d.graphs();
      graphs.insert(graphs.end(), prep.shared_bodies.begin(), prep.shared_bodies.end());
      hhds::GraphLibrary mapped_lib;
      abc::Map_options   options;
      options.library      = "inou/prp/tests/abc/test.lib";
      options.map_register = false;
      abc::Mapper mapper(options);
      mapper.set_outlib(&mapped_lib);
      ASSERT_TRUE(Pass_partition::build_decomposition(
          graphs,
          &mapped_lib,
          "top",
          false,
          [&](const livehd::partition::Region_body& rb) { mapper.map_region(rb); },
          flatten,
          false,
          {},
          64,
          prep.preserved_defs));
      mapper.stop();
      std::vector<std::shared_ptr<hhds::Graph>> mapped_graphs;
      size_t                                    compact = 0;
      for (auto gid : mapped_lib.all_gids()) {
        auto g = mapped_lib.get_graph(gid);
        if (!g) {
          continue;
        }
        mapped_graphs.push_back(g);
        for (auto n : g->body().nodes()) {
          if (n.is_loop_subnode()) {
            n.subnode_group().validate();
            ++compact;
          }
        }
      }
      EXPECT_EQ(compact, unroll ? 1u : 2u);
      ASSERT_TRUE(gu::materialize_occurrences_all(mapped_graphs, "test"));
      auto                                         mapped = mapped_lib.find_io("top")->get_graph();
      absl::flat_hash_map<hhds::Gid, hhds::Graph*> sub_lib;
      for (auto g : ref.graphs()) {
        sub_lib[g->get_gid()] = g.get();
      }
      for (auto g : mapped_graphs) {
        sub_lib[g->get_gid()] = g.get();
      }
      for (auto gid : models.all_gids()) {
        if (auto g = models.get_graph(gid)) {
          sub_lib[gid] = g.get();
        }
      }
      livehd::lec::Lec_options proof_options;
      proof_options.engine      = "ind";
      proof_options.timeout     = 20;
      proof_options.min_timeout = 1;
      auto proof                = livehd::lec::prove_equal(ref.top.get(), mapped.get(), proof_options, &sub_lib);
      EXPECT_EQ(proof.verdict, livehd::lec::Verdict::Proven) << proof.detail << " " << proof.witness;
    }
  }
}
