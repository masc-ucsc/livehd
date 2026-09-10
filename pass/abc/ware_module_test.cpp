// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "ware_module.hpp"

#include <cstdlib>

#include "abc_map.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass.hpp"
#include "pass_liberty.hpp"
#include "query.hpp"

namespace gu  = livehd::graph_util;
namespace abc = livehd::abc;

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
      pin.connect_sink(sum.create_sink_pin(i == 3 ? 1 : 0));
    }
    auto a = sum.create_driver_pin(0), b = sum.create_driver_pin(1);
    gu::set_ubits(a, 21);
    gu::set_ubits(b, 11);
    // Both Sum outputs are retained, including different result widths.
    a.connect_sink(g->get_output_pin("y" + std::to_string(copy)));
    b.connect_sink(g->get_output_pin("z" + std::to_string(copy)));
  }
  auto modules = abc::build_ware_modules({g});
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
    EXPECT_EQ(n.inp_edges().size(), 4u);
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
    auto edges = child->get_output_pin(d.name).inp_edges();
    ASSERT_EQ(edges.size(), 1u);
    rb.outputs.push_back({d.name, edges[0].driver, static_cast<int>(d.bits), !d.unsign});
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
  Pass_liberty::setup();
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
  for (auto e : second.inp_edges()) {
    e.del_edge();
  }
  gu::create_const(*mapped, *Dlop::create_integer(0)).connect_sink(second);
  auto wrong = livehd::lec::prove_equal(child.get(), mapped.get(), proof_options, &sub_lib);
  EXPECT_EQ(wrong.verdict, livehd::lec::Verdict::Refuted) << wrong.detail;
}

TEST(WareModule, PolicyFallbackAndExplicitFalse) {
  hhds::GraphLibrary lib;
  auto               g      = lib.create_io("policy")->create_graph();
  auto               policy = abc::ware_policy(*g, {false, true, false});
  EXPECT_FALSE(policy.arith);
  EXPECT_TRUE(policy.cmp);
  EXPECT_FALSE(policy.shift);
  g->get_input_node()
      .attr(livehd::attrs::coloring_info)
      .set(R"({"params":{"stop_arith":true,"ware_arith":false,"ware_cmp":false,"ware_shift":true}})");
  policy = abc::ware_policy(*g);
  EXPECT_FALSE(policy.arith);
  EXPECT_FALSE(policy.cmp);
  EXPECT_TRUE(policy.shift);
}
