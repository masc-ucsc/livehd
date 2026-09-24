// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass.abc end to end on the shared synthesis features it drives: loop bodies
// (pass/synth loop_cleanup) and enclosed arithmetic modules (pass/synth
// ware_module), each mapped by the ABC backend and checked with LEC.
#include <cstdlib>

#include "abc_map.hpp"
#include "color_common.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "loop_cleanup.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "pass.hpp"
#include "pass_color.hpp"
#include "pass_liberty.hpp"
#include "pass_partition.hpp"
#include "query.hpp"
#include "ware_module.hpp"

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
    a.connect_sink(gu::setup_sink_pid(x, 0));
    idx.connect_sink(gu::setup_sink_pid(x, 0));
    auto xp = x.create_driver_pin(0);
    gu::set_ubits(xp, 4);
    auto sum = gu::create_typed_node(*body, Ntype_op::Sum);
    a.connect_sink(gu::setup_sink_pid(sum, 0));
    xp.connect_sink(gu::setup_sink_pid(sum, 0));
    auto y = sum.create_driver_pin(0);
    gu::set_ubits(y, 4);
    y.connect_sink(body->get_output_pin("y"));
    auto mask = gu::create_typed_node(*body, Ntype_op::And);
    a.connect_sink(gu::setup_sink_pid(mask, 0));
    idx.connect_sink(gu::setup_sink_pid(mask, 0));
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
      data.connect_sink(gu::setup_sink_pid(independent_xor, 0));
      idx.connect_sink(gu::setup_sink_pid(independent_xor, 0));
      auto value = independent_xor.create_driver_pin(0);
      gu::set_ubits(value, 4);
      value.connect_sink(gu::setup_sink_pid(sum, 0));
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
      input.connect_sink(gu::setup_sink_pid(n, 1));
      for (int j = 0; j < 2; ++j) {
        auto out = n.create_driver_pin(j + 3);
        gu::set_ubits(out, 4);
        out.connect_sink(top->get_output_pin("o" + std::to_string(i * 2 + j)));
      }
      if (i) {
        n.create_driver_pin(3).connect_sink(gu::setup_sink_pid(n, 1));
        carried = n;
      } else {
        independent = n;
      }
      if (parallel_data) {
        input.connect_sink(gu::setup_sink_pid(n, 5));
      }
    }
  }
  auto graphs() const { return std::vector<std::shared_ptr<hhds::Graph>>{top, body}; }
};
}  // namespace

TEST(LoopCleanup, SameDefinitionCanBeIndependentAndCarried) {
  for (bool unroll : {false, true}) {
    Design                d;
    livehd::synth::Loop_preparation prep;
    ASSERT_TRUE(livehd::synth::prepare_loop_bodies(d.graphs(), unroll, prep));
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
      gu::create_const(*d.top, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(n, 5));
    }
    livehd::synth::Loop_preparation prep;
    ASSERT_TRUE(livehd::synth::prepare_loop_bodies(d.graphs(), true, prep));
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
        {      "mode",    mode},
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
  if (!Pass::eprp.get_method("pass.liberty")) {
    Pass_liberty::setup();
  }
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
      livehd::synth::Loop_preparation prep;
      ASSERT_TRUE(livehd::synth::prepare_loop_bodies(d.graphs(), unroll, prep));
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

TEST(WareModule, NarySumPreservesAllPortsAndSharesEqualRealizations) {
  hhds::GraphLibrary lib;
  auto               io = lib.create_io("top");
  for (int i = 0; i < 4; ++i) {
    const auto name = std::to_string(i);
    io->add_input(name, i + 1);
    io->set_bits(name, 3 + i * 5);
    io->set_unsign(name, i % 2 == 0);
  }
  for (int copy = 0; copy < 2; ++copy) {
    io->add_output("y" + std::to_string(copy), 5 + copy * 2);
    io->add_output("z" + std::to_string(copy), 6 + copy * 2);
    io->set_bits("y" + std::to_string(copy), 21);
    io->set_bits("z" + std::to_string(copy), 11);
  }
  auto g = io->create_graph();
  for (int copy = 0; copy < 2; ++copy) {
    auto sum = gu::create_typed_node(*g, Ntype_op::Sum);
    for (int i = 0; i < 4; ++i) {
      auto pin = g->get_input_pin(std::to_string(i));
      gu::set_bits(pin, 3 + i * 5);
      i % 2 == 0 ? gu::set_unsign(pin) : gu::set_sign(pin);
      pin.connect_sink(gu::setup_sink_pid(sum, i == 3 ? 1 : 0));
    }
    auto a = sum.create_driver_pin(0), b = sum.create_driver_pin(1);
    gu::set_ubits(a, 21);
    gu::set_ubits(b, 11);
    // Both Sum outputs are retained, including different result widths.
    a.connect_sink(g->get_output_pin("y" + std::to_string(copy)));
    b.connect_sink(g->get_output_pin("z" + std::to_string(copy)));
  }
  auto modules = livehd::synth::build_ware_modules({g});
  ASSERT_EQ(modules.size(), 1u);
  auto child = modules.front();
  EXPECT_EQ(child->get_io()->get_input_pin_decls().size(), 4u);
  EXPECT_EQ(child->get_io()->get_output_pin_decls().size(), 2u);
  int sums = 0;
  for (auto n : child->body().nodes()) {
    if (gu::type_op_of(n) != Ntype_op::Sum) {
      continue;
    }
    ++sums;
    EXPECT_EQ(n.inp_pins_snapshot().size(), 4u);
    EXPECT_EQ(gu::bits_of(n.create_driver_pin(0)), 21);
    EXPECT_EQ(gu::bits_of(n.create_driver_pin(1)), 11);
  }
  EXPECT_EQ(sums, 1);
  int instances = 0;
  for (auto n : g->body().nodes()) {
    EXPECT_NE(gu::type_op_of(n), Ntype_op::Sum);
    if (gu::type_op_of(n) == Ntype_op::Sub) {
      ++instances;
      EXPECT_EQ(n.get_subnode_gid(), child->get_gid());
    }
  }
  EXPECT_EQ(instances, 2);

  // Exercise the actual mapper, not just extraction: output 1 used to be
  // declared but never bit-blasted. Compare both realized widths by SMT.
  hhds::GraphLibrary mapped_lib;
  auto               mapped_io = mapped_lib.create_io(child->get_name());
  for (const auto& d : child->get_io()->get_input_pin_decls()) {
    mapped_io->add_input(d.name, d.port_id);
    mapped_io->set_bits(d.name, d.bits);
    mapped_io->set_unsign(d.name, d.unsign);
  }
  for (const auto& d : child->get_io()->get_output_pin_decls()) {
    mapped_io->add_output(d.name, d.port_id);
    mapped_io->set_bits(d.name, d.bits);
    mapped_io->set_unsign(d.name, d.unsign);
  }
  auto mapped = mapped_io->create_graph();
  for (const auto& d : mapped_io->get_input_pin_decls()) {
    auto pin = mapped->get_input_pin(d.name);
    gu::set_bits(pin, d.bits);
    d.unsign ? gu::set_unsign(pin) : gu::set_sign(pin);
  }
  std::vector<hhds::Node_class> nodes;
  for (auto n : child->body().nodes()) {
    nodes.push_back(n);
  }
  livehd::partition::Region_body rb;
  rb.src         = child.get();
  rb.body        = mapped.get();
  rb.module_name = std::string(child->get_name());
  rb.nodes       = nodes;
  for (const auto& d : child->get_io()->get_input_pin_decls()) {
    rb.inputs.push_back({d.name, child->get_input_pin(d.name), static_cast<int>(d.bits), !d.unsign});
  }
  for (const auto& d : child->get_io()->get_output_pin_decls()) {
    // One driver per sink pin: a graph output is driven by exactly one pin.
    auto out_drv = child->get_output_pin(d.name).get_driver_pin();
    ASSERT_FALSE(out_drv.is_invalid());
    rb.outputs.push_back({d.name, out_drv, static_cast<int>(d.bits), !d.unsign});
  }
  abc::Map_options mapping;
  mapping.library      = "inou/prp/tests/abc/test.lib";
  mapping.map_register = false;
  abc::Mapper mapper(mapping);
  mapper.set_outlib(&mapped_lib);
  mapper.map_region(rb);
  mapper.stop();
  ASSERT_EQ(mapper.qor().size(), 1u);
  EXPECT_GT(mapper.qor().front().gates, 0);

  const auto models_path = std::string(std::getenv("TEST_TMPDIR")) + "/ware-models";
  Eprp_var   model_var;
  if (!Pass::eprp.get_method("pass.liberty")) {
    Pass_liberty::setup();
  }
  Pass::eprp.run_method_now("pass.liberty",
                            model_var,
                            {
                                {"files", mapping.library},
                                {  "out",     models_path}
  });
  auto&                                        models = livehd::Hhds_graph_library::instance(models_path);
  absl::flat_hash_map<hhds::Gid, hhds::Graph*> sub_lib;
  for (auto gid : models.all_gids()) {
    auto body = models.get_graph(gid);
    if (body) {
      sub_lib[gid] = body.get();
    }
  }
  livehd::lec::Lec_options proof_options;
  proof_options.engine      = "ind";
  proof_options.timeout     = 20;
  proof_options.min_timeout = 1;
  auto proof                = livehd::lec::prove_equal(child.get(), mapped.get(), proof_options, &sub_lib);
  ASSERT_EQ(proof.verdict, livehd::lec::Verdict::Proven) << proof.detail << " " << proof.witness;
  EXPECT_EQ(proof.detail.find("width/sign reconciled"), std::string::npos) << proof.detail;
  // Negative control: the second output must actually be checked.
  auto second = mapped->get_output_pin("o1");
  gu::drop_drivers(second);
  gu::create_const(*mapped, *Dlop::create_integer(0)).connect_sink(second);
  auto wrong = livehd::lec::prove_equal(child.get(), mapped.get(), proof_options, &sub_lib);
  EXPECT_EQ(wrong.verdict, livehd::lec::Verdict::Refuted) << wrong.detail;
}

TEST(WareModule, PolicyFallbackAndExplicitFalse) {
  hhds::GraphLibrary lib;
  auto               g      = lib.create_io("policy")->create_graph();
  auto               policy = livehd::synth::ware_policy(*g, {false, true, false});
  EXPECT_FALSE(policy.arith);
  EXPECT_TRUE(policy.cmp);
  EXPECT_FALSE(policy.shift);
  g->get_input_node()
      .attr(livehd::attrs::coloring_info)
      .set(R"({"params":{"stop_arith":true,"ware_arith":false,"ware_cmp":false,"ware_shift":true}})");
  policy = livehd::synth::ware_policy(*g);
  EXPECT_FALSE(policy.arith);
  EXPECT_FALSE(policy.cmp);
  EXPECT_TRUE(policy.shift);
}
