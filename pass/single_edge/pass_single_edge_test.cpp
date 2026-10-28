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

// Replace whatever drives one of the memory's sinks. `build` has already wired
// the scalar policy pins, so a test that wants a different `type` or `rdport`
// has to displace the constant rather than add a second driver.
void repin(const Fixture& f, int raw_pid, int64_t value) {
  auto sink = f.mem.create_sink_pin(static_cast<hhds::Port_id>(raw_pid));
  sink.del_sink();
  gu::create_const(*f.g, *Dlop::create_integer(value)).connect_sink(sink);
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

// STRUCTURE ONLY.  The next three tests inspect the graph the pass LEAVES; they
// do not execute a memory transition, and they are not dynamic acceptance.
// They also run BEFORE bitwidth/cprop, so a width that is correct here can
// still be narrowed on the way to emission -- which is exactly what happened to
// the first version of this gate.  The emitted VALUE is checked separately by
// `formal/lean/Futamura_Projection_Scripts/slot_mask_check.lean` against a real certificate.
//
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
               && gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Get_mask) {
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
TEST(SingleEdgeMemoryGate, QuietStepMaskIsAWidthCorrectWindowOverASignExtension) {
  hhds::GraphLibrary lib;
  auto               f = build(lib, "quiet_mem", /*with_enable=*/true, /*with_concat=*/false);

  auto r = run(f.g.get());
  ASSERT_TRUE(r.applied) << r.reason;

  auto gated = driver_at(f.mem, mpid(0, 4));
  ASSERT_FALSE(gated.is_invalid());
  hhds::Pin_class mask;
  for (const auto& e : gu::inp_sink_drivers(gated.get_master_node())) {
    if (!e.driver.is_invalid() && !e.driver.is_const()
        && gu::type_op_of(e.driver.get_master_node()) == Ntype_op::Get_mask) {
      mask = e.driver;
    }
  }
  ASSERT_FALSE(mask.is_invalid()) << "no Get_mask-widened slot mask";
  EXPECT_EQ(gu::bits_of(mask), kWen) << "a quiet step would not zero every lane";
  // the mask's VALUE source is a Sext of a one-bit predicate
  auto sx = driver_at(mask.get_master_node(), Ntype::get_sink_pid(Ntype_op::Get_mask, "a"));
  ASSERT_FALSE(sx.is_invalid()) << "the mask has no value operand";
  EXPECT_EQ(gu::type_op_of(sx.get_master_node()), Ntype_op::Sext) << "the mask is not a sign-extended predicate";
  // …and the window really covers every lane
  auto hi = driver_at(mask.get_master_node(), Ntype::get_sink_pid(Ntype_op::Get_mask, "hi"));
  ASSERT_TRUE(hi.is_const()) << "the mask window has no constant high endpoint";
  EXPECT_EQ(gu::const_of(hi).to_just_i64(), kWen) << "the mask window does not reach the top lane";
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
  EXPECT_EQ(gu::type_op_of(gated.get_master_node()), Ntype_op::Get_mask)
      << "the all-lanes default should be the slot mask itself, with no AND";
}

}  // namespace

// ---------------------------------------------------------------------------
// A GATED write clock: `clk & enl`, with the enable latch on the opposite
// phase. `gate_only` leaves the port's own enable off, so the absent-enable
// path is exercised too.
namespace {
struct Gated {
  Fixture          f;
  hhds::Node_class latch;
  hhds::Pin_class  arm;
};

// `invert` builds `~clk & enl` instead, which gates the FALLING root edge.
Gated build_gated(hhds::GraphLibrary& lib, const std::string& mod, bool with_enable, bool invert,
                  bool negedge_mem = false) {
  Gated   r{build(lib, mod, with_enable, /*with_concat=*/false), {}, {}};
  auto*   g   = r.f.g.get();
  auto    clk = g->get_input_pin("clk");

  // enl: transparent while clk is LOW, so it is shut at the rising edge.
  auto notn = gu::create_typed_node(*g, Ntype_op::Not);
  clk.connect_sink(notn.create_sink_pin(0));
  auto nclk = notn.create_driver_pin(0);
  gu::set_bits(nclk, 1);
  gu::set_unsign(nclk);

  auto lat = gu::create_typed_node(*g, Ntype_op::Latch);
  lat.attr(hhds::attrs::name).set(std::string{"enl"});
  nclk.connect_sink(gu::setup_sink_by_name(lat, "enable"));
  auto arm = g->get_input_pin("we");            // a real pin, reused as the gate arm
  arm.connect_sink(gu::setup_sink_by_name(lat, "din"));
  auto lq = lat.create_driver_pin(0);
  gu::set_bits(lq, 1);
  gu::set_unsign(lq);

  hhds::Pin_class clk_op = clk;
  if (invert) {
    auto ni = gu::create_typed_node(*g, Ntype_op::Not);
    clk.connect_sink(ni.create_sink_pin(0));
    clk_op = ni.create_driver_pin(0);
    gu::set_bits(clk_op, 1);
    gu::set_unsign(clk_op);
  }
  auto gate = gu::create_typed_node(*g, Ntype_op::And);
  clk_op.connect_sink(livehd::graph_util::setup_sink_pid(gate, 0));
  lq.connect_sink(livehd::graph_util::setup_sink_pid(gate, 0));
  auto gq = gate.create_driver_pin(0);
  gu::set_bits(gq, 1);
  gu::set_unsign(gq);

  if (negedge_mem) {
    // posclk = 0: the CELL commits on the falling edge. With an INVERTED gate
    // that is a double inversion, so `commit_class_of_clock` reports an
    // effective ROOT-RISING class and the falling-edge refusal does NOT fire.
    // That is precisely the combination the inverted-gate refusal exists for:
    // rebinding to the root here would silently move the committing edge.
    auto pk = r.f.mem.create_sink_pin(6);
    pk.del_sink();
    gu::create_const(*g, *Dlop::create_integer(0)).connect_sink(pk);
  }
  // Move the write port onto the GATED clock.
  auto wclk = r.f.mem.create_sink_pin(mpid(0, 2));
  wclk.del_sink();
  gq.connect_sink(wclk);

  r.latch = lat;
  r.arm   = arm;
  return r;
}

// A cheap structural fingerprint: node count plus, per node, its op and the
// (sink pid -> driver) pairs it carries.
std::string fingerprint(hhds::Graph* g) {
  std::vector<std::string> rows;
  for (auto n : g->body().nodes()) {
    std::string row = std::string{Ntype::get_name(gu::type_op_of(n))};
    for (const auto& sd : gu::inp_sink_drivers(n)) {
      row += ":" + std::to_string(static_cast<int>(sd.sink.get_port_id())) + "->"
             + std::to_string(static_cast<uint64_t>(sd.driver.is_invalid() ? 0 : sd.driver.get_class_index().value));
    }
    rows.push_back(row);
  }
  std::sort(rows.begin(), rows.end());
  std::string out = std::to_string(rows.size());
  for (const auto& x : rows) {
    out += "|" + x;
  }
  return out;
}

single_edge::Result run_dry(hhds::Graph* g) {
  single_edge::Options o;
  o.quiet   = true;
  o.dry_run = true;
  return single_edge::normalize(g, {}, o);
}

// ---------------------------------------------------------------------------
// MIXED read timing: the cell-global `type` has no scalar answer, so the PORTS
// decide.
//
// `sync_read` is read at two slots as "this memory has a read register, which
// would commit on every sub-step" and the design is refused. Reading the mixed
// sentinel as "nonzero, therefore registered" refuses a cell whose read ports
// all say asynchronous -- a design with no read register at all.
//
// This is the only input that distinguishes the two readings, and no RTL route
// builds it: the reader's script runs `memory -nomap -nordff`, so a
// synchronous read port is never formed and `type` is never 3. Built by hand
// for exactly that reason.
TEST(SingleEdgeMemoryGate, MixedCellWhoseReadPortsAreAllAsyncHasNoReadRegister) {
  hhds::GraphLibrary lib;
  auto               f = build(lib, "mixed_rd_async", /*with_enable=*/true, /*with_concat=*/false);
  repin(f, 7, Ntype::Memory_type_mixed);                 // the sentinel
  repin(f, mpid(1, 10), Ntype::Memory_rdport_async);     // ... but the one read port is async
  const auto r = run(f.g.get());
  EXPECT_EQ(r.reason.find("synchronous read port"), std::string::npos)
      << "the pins say asynchronous; the sentinel is not a scalar answer. reason: " << r.reason;
}

// The other direction, so the first test cannot be satisfied by ignoring the
// sentinel altogether: a mixed cell with a REGISTERED read port is still
// refused at two slots.
TEST(SingleEdgeMemoryGate, MixedCellWithARegisteredReadPortIsStillRefusedAtTwoSlots) {
  hhds::GraphLibrary lib;
  auto               f = build(lib, "mixed_rd_sync", /*with_enable=*/true, /*with_concat=*/false);
  repin(f, 7, Ntype::Memory_type_mixed);
  repin(f, mpid(1, 10), Ntype::Memory_rdport_sync);
  const auto r = run(f.g.get());
  EXPECT_TRUE(r.error) << r.reason;
  EXPECT_NE(r.reason.find("synchronous read port"), std::string::npos) << r.reason;
}

}  // namespace

// PLANNING MUST NOT TOUCH THE GRAPH. `dry_run` returns before the rewrite and
// promises exactly that, and `proof_prep` relies on it. An earlier revision of
// the gated-memory path built the gate's AND cone while still PLANNING, so a
// dry run -- and any plan that was refused later -- left nodes and edges
// behind.
TEST(SingleEdgeMemoryGate, DryRunLeavesAGatedMemoryGraphUntouched) {
  hhds::GraphLibrary lib;
  auto               gz     = build_gated(lib, "dry_gated_mem", /*with_enable=*/true, /*invert=*/false);
  const auto         before = fingerprint(gz.f.g.get());

  auto r = run_dry(gz.f.g.get());
  EXPECT_TRUE(r.applied) << r.reason << " (the plan itself must still succeed)";
  EXPECT_EQ(fingerprint(gz.f.g.get()), before)
      << "a dry run mutated the graph: planning allocated the gate cone instead of freezing its operands";
}

// The same promise for a plan that is REFUSED after the gate is resolved: an
// inverted gate is declined, and nothing may be left behind on the way out.
TEST(SingleEdgeMemoryGate, ARefusedInvertedGateLeavesTheGraphUntouched) {
  hhds::GraphLibrary lib;
  auto               gz     = build_gated(lib, "inv_gated_mem", /*with_enable=*/true, /*invert=*/true,
                                          /*negedge_mem=*/true);
  const auto         before = fingerprint(gz.f.g.get());

  auto r = run(gz.f.g.get());
  EXPECT_FALSE(r.applied) << "`~clk & en` gates the FALLING root edge; rebinding it to the root would move the "
                             "committing edge, and this pass does not retype a memory's polarity";
  EXPECT_NE(r.reason.find("INVERTED"), std::string::npos)
      << "expected the inverted-gate refusal; got: " << r.reason;
  EXPECT_EQ(fingerprint(gz.f.g.get()), before) << "a refused plan left nodes or edges behind";
}

// THE GATE REACHES EVERY LANE, and the clock stops being derived.
TEST(SingleEdgeMemoryGate, AGatedWriteClockFoldsIntoTheFullWidthEnable) {
  hhds::GraphLibrary lib;
  auto               gz = build_gated(lib, "gated_mem", /*with_enable=*/true, /*invert=*/false);

  auto r = run(gz.f.g.get());
  ASSERT_TRUE(r.applied) << r.reason;
  EXPECT_EQ(r.mem_ports_ungated, 1) << "the gated write clock was not rebound";

  auto gated = driver_at(gz.f.mem, mpid(0, 4));
  ASSERT_FALSE(gated.is_invalid());
  EXPECT_EQ(gu::bits_of(gated), kWen) << "the gated enable is not the full lane width -- a high lane would be lost";

  // The write clock must no longer be the AND: a derived clock survives the
  // lowering otherwise, and the encoder refuses one.
  auto wclk = driver_at(gz.f.mem, mpid(0, 2));
  ASSERT_FALSE(wclk.is_invalid());
  EXPECT_NE(gu::type_op_of(wclk.get_master_node()), Ntype_op::And)
      << "the memory still commits on the gated net; folding alone leaves a derived clock";
}

// ABSENT ENABLE is the native all-lanes contract, and the gate must still reach
// every one of those lanes.
TEST(SingleEdgeMemoryGate, AGatedWriteClockWithNoPortEnableStillGatesEveryLane) {
  hhds::GraphLibrary lib;
  auto               gz = build_gated(lib, "gated_mem_noen", /*with_enable=*/false, /*invert=*/false);

  auto r = run(gz.f.g.get());
  ASSERT_TRUE(r.applied) << r.reason;
  auto gated = driver_at(gz.f.mem, mpid(0, 4));
  ASSERT_FALSE(gated.is_invalid()) << "the absent enable must become the width-correct gated mask";
  EXPECT_EQ(gu::bits_of(gated), kWen);
}
