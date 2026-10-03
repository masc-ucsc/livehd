// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "loop_hoist.hpp"

#include <string>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace {
namespace gu = livehd::graph_util;

struct Fixture {
  std::shared_ptr<hhds::Graph> parent;
  std::shared_ptr<hhds::Graph> body;
  hhds::Node_class             loop;
  hhds::Node_class             plain;  // an ordinary instance of the same body, when requested
};

size_t count_op(const std::shared_ptr<hhds::Graph>& g, Ntype_op op) {
  size_t n = 0;
  for (auto node : g->body().nodes()) {
    if (gu::type_op_of(node) == op) {
      ++n;
    }
  }
  return n;
}

// A compact loop over an 8-bit carry whose body is
//   t = Not(inv)            invariant: reads only an invariant input
//   m = And(t, i)           varies with the ordinal
//   acc_out = Xor(m, acc_in) varies with the carry
// so exactly one node, the Not, has the same value in every iteration.
Fixture make(std::string_view tag, bool with_plain_call) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_loop_hoist_") + std::string(tag));

  auto body_io = lib.create_io(std::string(tag) + "_body");
  body_io->add_input("inv", 0);
  body_io->add_input("i", 1);
  body_io->add_input("acc_in", 2);
  body_io->add_output("acc_out", 3);
  for (const auto* name : {"inv", "acc_in", "acc_out"}) {
    body_io->set_bits(name, 8);
    body_io->set_unsign(name, true);
  }
  body_io->set_bits("i", 4);
  auto body = body_io->create_graph();
  auto t    = gu::create_typed_node(*body, Ntype_op::Not);
  body->get_input_pin("inv").connect_sink(t.create_sink_pin(0));
  gu::set_ubits(t.create_driver_pin(0), 8);
  auto m = gu::create_typed_node(*body, Ntype_op::And);
  t.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(m, "as"));
  body->get_input_pin("i").connect_sink(gu::setup_sink_by_name(m, "as"));
  gu::set_ubits(m.create_driver_pin(0), 8);
  auto x = gu::create_typed_node(*body, Ntype_op::Xor);
  m.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(x, "as"));
  body->get_input_pin("acc_in").connect_sink(gu::setup_sink_by_name(x, "as"));
  gu::set_ubits(x.create_driver_pin(0), 8);
  x.create_driver_pin(0).connect_sink(body->get_output_pin("acc_out"));

  auto parent_io = lib.create_io(std::string(tag) + "_parent");
  parent_io->add_input("seed", 0);
  parent_io->add_input("x", 1);
  parent_io->add_output("result", 2);
  parent_io->add_output("plain_result", 3);
  parent_io->set_bits("seed", 8);
  parent_io->set_bits("x", 8);
  parent_io->set_bits("result", 8);
  parent_io->set_bits("plain_result", 8);
  auto parent = parent_io->create_graph();

  auto loop = gu::create_typed_node(*parent, Ntype_op::Sub);
  loop.set_name("u_loop");
  hhds::Subnode_loop desc;
  desc.count       = 4;
  desc.index_input = hhds::Port_id{1};
  loop.set_subnode(body_io, desc);
  parent->get_input_pin("x").connect_sink(loop.create_sink_pin(0));
  parent->get_input_pin("seed").connect_sink(loop.create_sink_pin(2));
  auto out = loop.create_driver_pin(3);
  gu::set_ubits(out, 8);
  out.connect_sink(parent->get_output_pin("result"));
  out.connect_sink(loop.create_sink_pin(2));  // the carry: acc_out -> acc_in
  loop.subnode_group().validate();

  hhds::Node_class plain;
  if (with_plain_call) {
    plain = gu::create_typed_node(*parent, Ntype_op::Sub);
    plain.set_name("u_plain");
    plain.set_subnode(body_io);
    parent->get_input_pin("x").connect_sink(plain.create_sink_pin(0));
    parent->get_input_pin("seed").connect_sink(plain.create_sink_pin(1));
    parent->get_input_pin("seed").connect_sink(plain.create_sink_pin(2));
    auto pout = plain.create_driver_pin(3);
    gu::set_ubits(pout, 8);
    pout.connect_sink(parent->get_output_pin("plain_result"));
  }
  return {parent, body, loop, plain};
}
}  // namespace

// The Not reads only `inv`, so it moves to the parent once; the And reads the
// ordinal and the Xor the carry, so both stay. The body gains one `__hoist0`
// input the parent drives from its own Not of `x`, and a second sweep moves
// nothing more.
TEST(LoopHoist, InvariantConeMovesToEveryParentOnce) {
  auto f = make("basic", false);
  ASSERT_EQ(count_op(f.body, Ntype_op::Not), 1u);
  ASSERT_EQ(count_op(f.parent, Ntype_op::Not), 0u);

  const auto stats = livehd::cprop::hoist_loop_invariants({f.parent, f.body});
  EXPECT_EQ(stats.bodies, 1u);
  EXPECT_EQ(stats.hoisted_values, 1u);
  EXPECT_EQ(stats.removed_nodes, 1u);
  EXPECT_EQ(stats.instances, 1u);

  EXPECT_EQ(count_op(f.body, Ntype_op::Not), 0u) << "the invariant Not left the body";
  EXPECT_EQ(count_op(f.body, Ntype_op::And), 1u) << "the ordinal-dependent And stays";
  EXPECT_EQ(count_op(f.body, Ntype_op::Xor), 1u) << "the carry-dependent Xor stays";
  EXPECT_EQ(count_op(f.parent, Ntype_op::Not), 1u) << "the parent computes it once";

  auto io = f.body->get_io();
  ASSERT_TRUE(io);
  EXPECT_EQ(io->get_input_pin_decls().size(), 4u);
  const auto& added = io->get_input_pin_decls().back();
  EXPECT_EQ(added.name, "__hoist0");
  EXPECT_EQ(added.port_id, hhds::Port_id{4}) << "past every existing input and output port";
  EXPECT_EQ(added.bits, 8u);
  EXPECT_TRUE(added.unsign);

  // The body's And now reads the new input where the Not used to be.
  auto hoisted_in = f.body->get_input_pin("__hoist0");
  ASSERT_FALSE(hoisted_in.is_invalid());
  bool feeds_and = false;
  for (const auto& e : hoisted_in.out_edges()) {
    feeds_and |= gu::type_op_of(e.sink.get_master_node()) == Ntype_op::And;
  }
  EXPECT_TRUE(feeds_and);

  // The instance drives the new port from the parent's clone, which reads `x`.
  auto sp = f.loop.try_get_sink_pin(hhds::Port_id{4});
  ASSERT_FALSE(sp.is_invalid());
  auto drv = sp.get_driver_pin();
  ASSERT_EQ(gu::type_op_of(drv.get_master_node()), Ntype_op::Not);
  bool reads_x = false;
  for (auto spin : drv.get_master_node().inp_sorted_pins()) {
    for (auto d : spin.get_driver_pins()) {
      reads_x |= d == f.parent->get_input_pin("x");
    }
  }
  EXPECT_TRUE(reads_x);
  EXPECT_EQ(gu::bits_of(drv), 8);

  const auto again = livehd::cprop::hoist_loop_invariants({f.parent, f.body});
  EXPECT_EQ(again.hoisted_values, 0u) << "idempotent";
  f.loop.subnode_group().validate();
}

// Two things that are invariant but not worth a port: a cone that only re-wires
// an invariant (a slice) gains nothing in any backend, and a value of unknown
// width cannot become a declared port (a 0-width port takes the body off the
// simulator's inline evaluator). Both stay in the body.
TEST(LoopHoist, WiringOnlyAndUnsizedConesStay) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_loop_hoist_wiring");
  auto  body_io = lib.create_io("wiring_body");
  body_io->add_input("inv", 0);
  body_io->add_input("i", 1);
  body_io->add_input("acc_in", 2);
  body_io->add_output("acc_out", 3);
  body_io->set_bits("inv", 8);
  body_io->set_bits("i", 4);
  body_io->set_bits("acc_in", 8);
  body_io->set_bits("acc_out", 8);
  auto body = body_io->create_graph();
  // slice = inv#[0..4]: wiring only, sized
  auto slice = gu::create_get_mask(*body, body->get_input_pin("inv"), 0, 4);
  gu::set_ubits(slice.create_driver_pin(0), 4);
  // bare = Not(inv): computes, but its width is unknown
  auto bare = gu::create_typed_node(*body, Ntype_op::Not);
  body->get_input_pin("inv").connect_sink(bare.create_sink_pin(0));
  auto m = gu::create_typed_node(*body, Ntype_op::And);
  slice.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(m, "as"));
  bare.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(m, "as"));
  body->get_input_pin("i").connect_sink(gu::setup_sink_by_name(m, "as"));
  gu::set_ubits(m.create_driver_pin(0), 8);
  auto x = gu::create_typed_node(*body, Ntype_op::Xor);
  m.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(x, "as"));
  body->get_input_pin("acc_in").connect_sink(gu::setup_sink_by_name(x, "as"));
  gu::set_ubits(x.create_driver_pin(0), 8);
  x.create_driver_pin(0).connect_sink(body->get_output_pin("acc_out"));

  auto parent_io = lib.create_io("wiring_parent");
  parent_io->add_input("seed", 0);
  parent_io->add_input("x", 1);
  parent_io->add_output("result", 2);
  parent_io->set_bits("seed", 8);
  parent_io->set_bits("x", 8);
  parent_io->set_bits("result", 8);
  auto parent = parent_io->create_graph();
  auto loop   = gu::create_typed_node(*parent, Ntype_op::Sub);
  hhds::Subnode_loop desc;
  desc.count       = 4;
  desc.index_input = hhds::Port_id{1};
  loop.set_subnode(body_io, desc);
  parent->get_input_pin("x").connect_sink(loop.create_sink_pin(0));
  parent->get_input_pin("seed").connect_sink(loop.create_sink_pin(2));
  auto out = loop.create_driver_pin(3);
  gu::set_ubits(out, 8);
  out.connect_sink(parent->get_output_pin("result"));
  out.connect_sink(loop.create_sink_pin(2));
  loop.subnode_group().validate();

  const auto stats = livehd::cprop::hoist_loop_invariants({parent, body});
  EXPECT_EQ(stats.hoisted_values, 0u);
  EXPECT_EQ(count_op(body, Ntype_op::Get_mask), 1u);
  EXPECT_EQ(count_op(body, Ntype_op::Not), 1u);
  EXPECT_EQ(body_io->get_input_pin_decls().size(), 3u);
}

// A body that is also called as an ordinary Sub has no invariant inputs for
// that call, so it is left alone entirely.
TEST(LoopHoist, BodyWithAPlainCallIsLeftAlone) {
  auto       f     = make("mixed", true);
  const auto stats = livehd::cprop::hoist_loop_invariants({f.parent, f.body});
  EXPECT_EQ(stats.hoisted_values, 0u);
  EXPECT_EQ(count_op(f.body, Ntype_op::Not), 1u);
  EXPECT_EQ(f.body->get_io()->get_input_pin_decls().size(), 3u);
}
