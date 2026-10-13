// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// THE MEMORY SLOT GATE, built directly as an LGraph.
//
// WHY NOT AN RTL FIXTURE.  The shapes these pin are legal at the graph level
// but the front end folds or canonicalizes them away: a narrow constant-zero
// Concat lane beside a wide write-enable kept disappearing into `get_mask`,
// and three successive `.v` rewrites each lost the lane to a different fold.
// The D4 revision f6ad1fe29 solved the same problem the same way -- its
// `pass_single_edge_test.cpp` builds memories by hand -- and this file adapts
// that approach to master's schema (uniform `type` 0/1, no per-read mask) and
// HHDS pin API.  Historical presence is reuse material, not test credit: every
// assertion here runs against the CURRENT pass.
#include <memory>
#include <string>
#include <vector>

#include "cell.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "pass_single_edge.hpp"

using namespace livehd;
namespace gu = livehd::graph_util;

namespace {

constexpr int kBits  = 16;  // data width
constexpr int kDepth = 4;
constexpr int kWen   = 16;  // one enable lane per bit -- a WIDE vector
constexpr int kLane  = 5;   // the narrow Concat lane the shared zero drives

int mpid(int block, int off) { return block * static_cast<int>(Ntype::Memory_port_stride) + off; }

void drive(const hhds::Node_class& n, int raw_pid, const hhds::Pin_class& dpin) {
  n.create_sink_pin(static_cast<hhds::Port_id>(raw_pid)).connect_driver(dpin);
}

// master's HHDS has no `Node_class::inp_edges`; `gu::inp_sink_drivers` is the
// current spelling and is what the pass itself uses.
hhds::Pin_class driver_at(const hhds::Node_class& n, int raw_pid) {
  for (const auto& e : gu::inp_sink_drivers(n)) {
    if (static_cast<int>(e.get_port_id()) == raw_pid) {
      return e.driver;
    }
  }
  return {};
}

single_edge::Result run(hhds::Graph* g) {
  single_edge::Options o;
  o.quiet = true;
  return single_edge::normalize(g, {}, o);
}

struct Fixture {
  std::shared_ptr<hhds::Graph> g;
  hhds::Node_class             mem;
  hhds::Node_class             concat;   // invalid when `with_concat` is false
  hhds::Pin_class              shared_zero;
};

// A one-write / one-read ASYNC-read memory on the reference clock, a negedge
// flop to force P=2, and -- optionally -- a Concat whose NARROW lane is driven
// by the SAME canonical zero pin the gate would restamp.
Fixture build(hhds::GraphLibrary& lib, const std::string& mod, bool with_enable, bool with_concat) {
  auto gio = lib.create_io(mod);
  int  pos = 0;
  gio->add_input("clk", pos++);
  gio->set_bits("clk", 1);
  gio->set_unsign("clk", true);
  gio->add_input("we", pos++);
  gio->set_bits("we", kWen);
  gio->set_unsign("we", true);
  for (const char* n : {"wa", "ra"}) {
    gio->add_input(n, pos++);
    gio->set_bits(n, 2);
    gio->set_unsign(n, true);
  }
  gio->add_input("d", pos++);
  gio->set_bits("d", kBits);
  gio->set_unsign("d", true);
  gio->add_output("q", pos++);
  gio->set_bits("q", kBits);
  gio->set_unsign("q", true);
  gio->add_output("fq", pos++);
  gio->set_bits("fq", kBits);
  gio->set_unsign("fq", true);
  if (with_concat) {
    gio->add_output("y", pos++);
    gio->set_bits("y", kBits + kLane);
    gio->set_unsign("y", true);
  }

  Fixture f;
  f.g       = gio->create_graph();
  auto* g   = f.g.get();
  auto  clk = g->get_input_pin("clk");

  // THE SHARED CANONICAL ZERO.  `create_constant` deduplicates by value, so
  // this is the same pin the gate would mint for a quiet arm.
  f.shared_zero = gu::create_const(*g, *Dlop::create_integer(0));

  f.mem = gu::create_typed_node(*g, Ntype_op::Memory);
  drive(f.mem, 1, gu::create_const(*g, *Dlop::create_integer(kBits)));   // bits
  drive(f.mem, 5, f.shared_zero);                                        // fwd = 0
  drive(f.mem, 6, gu::create_const(*g, *Dlop::create_integer(1)));       // posclk: RISING
  drive(f.mem, 7, f.shared_zero);                                        // type 0: async read
  drive(f.mem, 8, gu::create_const(*g, *Dlop::create_integer(kWen)));    // wensize
  drive(f.mem, 9, gu::create_const(*g, *Dlop::create_integer(kDepth)));  // size
  // write port 0
  drive(f.mem, mpid(0, 0), g->get_input_pin("wa"));
  drive(f.mem, mpid(0, 2), clk);
  drive(f.mem, mpid(0, 3), g->get_input_pin("d"));
  if (with_enable) {
    drive(f.mem, mpid(0, 4), g->get_input_pin("we"));
  }
  drive(f.mem, mpid(0, 10), f.shared_zero);  // rdport = 0 -> a WRITE port
  // async read port 1
  drive(f.mem, mpid(1, 0), g->get_input_pin("ra"));
  drive(f.mem, mpid(1, 10), gu::create_const(*g, *Dlop::create_integer(1)));

  auto dout = f.mem.create_driver_pin(1);
  gu::set_bits(dout, kBits);
  gu::set_unsign(dout);
  dout.connect_sink(g->get_output_pin("q"));

  // a NEGEDGE flop, so the design needs P=2 at all
  auto fl = gu::create_typed_node(*g, Ntype_op::Flop);
  clk.connect_sink(gu::setup_sink_by_name(fl, "clock_pin"));
  gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(fl, "posclk"));
  dout.connect_sink(gu::setup_sink_by_name(fl, "din"));
  auto fq = fl.create_driver_pin(0);
  gu::set_bits(fq, kBits);
  gu::set_unsign(fq);
  fq.connect_sink(g->get_output_pin("fq"));

  if (with_concat) {
    // `{ q[kBits-1:0], 0[kLane-1:0] }` -- lane 1 is the NARROW window the
    // shared zero drives.  Concat takes (driver, window) pairs.
    f.concat = gu::create_typed_node(*g, Ntype_op::Concat);
    drive(f.concat, 0, dout);
    drive(f.concat, 1, gu::create_const(*g, *Dlop::create_integer(kBits)));
    drive(f.concat, 2, f.shared_zero);
    drive(f.concat, 3, gu::create_const(*g, *Dlop::create_integer(kLane)));
    auto cq = f.concat.create_driver_pin(0);
    gu::set_bits(cq, kBits + kLane);
    gu::set_unsign(cq);
    cq.connect_sink(g->get_output_pin("y"));
  }
  return f;
}

// ---------------------------------------------------------------------------

// THE ALIAS PROPERTY, on a lane the front end cannot fold away.
TEST(SingleEdgeMemoryGate, NarrowConcatLaneOfTheSharedZeroIsUntouched) {
  hhds::GraphLibrary lib;
  auto               f = build(lib, "concat_zero_mem", /*with_enable=*/true, /*with_concat=*/true);

  const auto before_bits   = gu::bits_of(f.shared_zero);
  const auto before_unsign = livehd::graph_util::is_unsign(f.shared_zero);

  auto r = run(f.g.get());
  ASSERT_TRUE(r.applied) << "the fixture did not lower: " << r.reason;
  ASSERT_EQ(r.mem_ports_slotted, 1) << "the write port was not gated, so nothing was exercised";

  // the CANONICAL constant's own metadata
  EXPECT_EQ(gu::bits_of(f.shared_zero), before_bits)
      << "the shared zero's width changed: the gate restamped a canonical constant";
  EXPECT_EQ(livehd::graph_util::is_unsign(f.shared_zero), before_unsign)
      << "the shared zero's signedness changed";
  // …and the narrow lane still reads THAT pin, with its window intact
  auto lane_drv = driver_at(f.concat, 2);
  ASSERT_FALSE(lane_drv.is_invalid()) << "the narrow Concat lane lost its driver";
  EXPECT_TRUE(lane_drv.is_const()) << "the narrow Concat lane is no longer driven by a constant";
  EXPECT_EQ(gu::const_of(lane_drv).to_just_i64(), 0) << "the narrow Concat lane's constant is no longer zero";
  EXPECT_LE(gu::bits_of(lane_drv), kLane)
      << "the narrow Concat lane's driver is now WIDER than its " << kLane << "-bit window";
  auto window = driver_at(f.concat, 3);
  ASSERT_TRUE(window.is_const());
  EXPECT_EQ(gu::const_of(window).to_just_i64(), kLane) << "the lane window itself was altered";
}

// EVERY HIGH LANE SURVIVES the active step: the gated enable is the ORIGINAL
// vector ANDed with the mask, at the full `wensize`.
TEST(SingleEdgeMemoryGate, ActiveStepKeepsTheWholeEnableVector) {
  hhds::GraphLibrary lib;
  auto               f  = build(lib, "high_lane_mem", /*with_enable=*/true, /*with_concat=*/false);
  auto               we = f.g->get_input_pin("we");

  auto r = run(f.g.get());
  ASSERT_TRUE(r.applied) << r.reason;
  ASSERT_EQ(r.mem_ports_slotted, 1);

  auto gated = driver_at(f.mem, mpid(0, 4));
  ASSERT_FALSE(gated.is_invalid()) << "the write port lost its enable";
  EXPECT_EQ(gu::bits_of(gated), kWen) << "the gated enable is not the full lane width";
  auto gn = gated.get_master_node();
  EXPECT_EQ(gu::type_op_of(gn), Ntype_op::And) << "the gate is not an AND of the enable with a mask";

  bool keeps_original = false, has_mask = false;
  for (const auto& e : gu::inp_sink_drivers(gn)) {
    if (e.driver.get_class_index() == we.get_class_index()) {
      keeps_original = true;
    } else if (!e.driver.is_invalid() && !e.driver.is_const()
               && gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Sext) {
      has_mask = true;
      EXPECT_EQ(gu::bits_of(e.driver), kWen) << "the slot mask is not the full lane width";
    }
  }
  EXPECT_TRUE(keeps_original) << "the ORIGINAL enable vector is not an operand -- high lanes were dropped";
  EXPECT_TRUE(has_mask) << "no sign-extended slot mask drives the gate";
}

// THE QUIET STEP: the mask is a sign-extension of a ONE-BIT slot predicate, so
// when the predicate is low every one of the `wensize` lanes is zero.  No wide
// constant is minted for it.
TEST(SingleEdgeMemoryGate, QuietStepMaskIsAWidthCorrectSignExtension) {
  hhds::GraphLibrary lib;
  auto               f = build(lib, "quiet_mem", /*with_enable=*/true, /*with_concat=*/false);

  auto r = run(f.g.get());
  ASSERT_TRUE(r.applied) << r.reason;

  auto gated = driver_at(f.mem, mpid(0, 4));
  ASSERT_FALSE(gated.is_invalid());
  hhds::Pin_class mask;
  for (const auto& e : gu::inp_sink_drivers(gated.get_master_node())) {
    if (!e.driver.is_invalid() && !e.driver.is_const()
        && gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Sext) {
      mask = e.driver;
    }
  }
  ASSERT_FALSE(mask.is_invalid()) << "no sign-extended mask";
  EXPECT_EQ(gu::bits_of(mask), kWen) << "a quiet step would not zero every lane";
  auto src = driver_at(mask.get_master_node(), 0);
  ASSERT_FALSE(src.is_invalid());
  EXPECT_EQ(gu::bits_of(src), 1) << "the slot predicate is not one bit, so the mask is not all-ones/all-zeros";
}

// AN ABSENT ENABLE MEANS ALL LANES WRITE -- the native contract
// (`Slop<wensize>::create_integer(-1)` in both pinned old and master
// `emit_wen`).  The gated enable is then the mask itself, at full width.
TEST(SingleEdgeMemoryGate, AbsentEnableWritesEveryLaneOnTheActiveStep) {
  hhds::GraphLibrary lib;
  auto               f = build(lib, "absent_en_mem", /*with_enable=*/false, /*with_concat=*/false);

  auto r = run(f.g.get());
  ASSERT_TRUE(r.applied) << "an absent enable was refused instead of defaulting to all lanes: " << r.reason;
  ASSERT_EQ(r.mem_ports_slotted, 1);

  auto gated = driver_at(f.mem, mpid(0, 4));
  ASSERT_FALSE(gated.is_invalid()) << "no enable was installed for the absent-enable port";
  EXPECT_EQ(gu::bits_of(gated), kWen) << "the all-lanes default is not the full lane width";
  EXPECT_EQ(gu::type_op_of(gated.get_master_node()), Ntype_op::Sext)
      << "the all-lanes default should be the slot mask itself, with no AND and no wide constant";
}

}  // namespace
