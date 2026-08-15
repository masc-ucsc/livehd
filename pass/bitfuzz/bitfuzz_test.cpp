//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bitfuzz.hpp"

#include <format>
#include <memory>
#include <string>

#include "cell.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

namespace {

void quiet_diag() {
  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_jsonl_path("off");
  sink.set_human_stderr(false);
}

livehd::bitfuzz::Options wires_opts() {
  livehd::bitfuzz::Options o;
  o.mode = livehd::bitfuzz::Mode::Wires;
  return o;
}

// Every finding on one line, so a failing expectation says WHICH pin misbehaved
// instead of just a count.
std::string describe(const livehd::bitfuzz::Stats& st) {
  std::string s;
  for (const auto& f : st.findings) {
    s += std::format(" [{} {} {}: {}->{} bits]", f.op, f.pin, f.kind, f.was_bits, f.now_bits);
  }
  return s.empty() ? std::string{" <no findings>"} : s;
}

// An honest combinational design: two sized inputs into an And, whose width the
// front end declared exactly as wide as inference would derive it. Stripping
// the annotation must give it back unchanged and flag nothing.
TEST(Bitfuzz, RecoversHonestCombinational) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
  auto  gio = lib.create_io("bf_honest");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_input("b", 2);
  gio->set_bits("b", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto op = gu::create_typed_node(*g, Ntype_op::And, 8);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(1));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  quiet_diag();
  auto st = livehd::bitfuzz::fuzz(g, wires_opts());

  EXPECT_EQ(st.cleared, 1) << "the And driver pin is the only eligible pin";
  EXPECT_EQ(st.wider, 0) << "an honest declaration must not need widening:" << describe(st);
  EXPECT_EQ(st.unrecovered, 0) << describe(st);
  EXPECT_GT(gu::bits_of(op.create_driver_pin(0)), 0) << "bitfuzz must leave every pin sized";
  livehd::diag::sink().clear();
}

// Graph IO is never stripped: the port widths are the source RTL interface and
// a LEC comparison is only well defined while both sides keep them.
TEST(Bitfuzz, KeepsGraphIoWidths) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
  auto  gio = lib.create_io("bf_io");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_output("o", 2);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto op = gu::create_typed_node(*g, Ntype_op::Not, 8);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  quiet_diag();
  auto st = livehd::bitfuzz::fuzz(g, wires_opts());

  // A port's declared width lives on the GraphIO decl, not the pin attr
  // (node_util.hpp bits_of(pin, gio, name)), so read it from there.
  EXPECT_EQ(gio->get_bits("a"), 8u) << "an input port width must survive the fuzz";
  EXPECT_EQ(gio->get_bits("o"), 8u) << "an output port width must survive the fuzz";
  for (const auto& f : st.findings) {
    EXPECT_NE(f.op, "IO") << "no graph-IO pin may ever be stripped";
  }
  livehd::diag::sink().clear();
}

// THE target bug class. A Sum of two 8-bit inputs genuinely needs 9 bits; a
// front end that declares the sum 8 bits is using the annotation to truncate.
// Inference sees no Get_mask, so it recovers 9 and bitfuzz reports `wider`.
TEST(Bitfuzz, FlagsImplicitTruncationAsWider) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
  auto  gio = lib.create_io("bf_trunc");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_input("b", 2);
  gio->set_bits("b", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  // Both operands on `as` (pid 0, a multi-driver sink) so this is a+b, which
  // over [0..127] spans [0..254] and genuinely needs 9 bits.
  auto op = gu::create_typed_node(*g, Ntype_op::Sum, 8);  // declared narrower than a+b needs
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(0));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  quiet_diag();
  auto st = livehd::bitfuzz::fuzz(g, wires_opts());

  EXPECT_EQ(st.wider, 1) << "an 8-bit-declared sum of two 8-bit values must be flagged as implicitly truncating";
  ASSERT_FALSE(st.findings.empty());
  EXPECT_EQ(st.findings.front().kind, "wider");
  EXPECT_GT(st.findings.front().now_bits, st.findings.front().was_bits);
  livehd::diag::sink().clear();
}

// An EXPLICIT narrowing is the shape the ruling demands: the same sum, masked
// back to 8 bits by a Get_mask cell. Nothing may be flagged -- the cell carries
// the semantics, so the annotation is genuinely redundant.
TEST(Bitfuzz, ExplicitGetMaskIsClean) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
  auto  gio = lib.create_io("bf_masked");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_input("b", 2);
  gio->set_bits("b", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto sum = gu::create_typed_node(*g, Ntype_op::Sum, 9);
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sum.create_sink_pin(0));

  // 9, not 8: every LGraph value is SIGNED and unsigned is just the
  // non-negative subset, so a mask of 0xff yields the range [0..255], whose
  // signed width is 9. Declaring 8 here would itself be an implicit truncation.
  auto mask = gu::create_typed_node(*g, Ntype_op::Get_mask, 9);
  gu::setup_sink_by_name(mask, "a").connect_driver(sum.create_driver_pin(0));
  gu::setup_sink_by_name(mask, "mask").connect_driver(gu::create_const(*g, *Dlop::get_mask_value(8)));
  mask.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  quiet_diag();
  auto st = livehd::bitfuzz::fuzz(g, wires_opts());

  EXPECT_EQ(st.wider, 0) << "an explicitly masked cone must recover exactly; nothing is doing hidden truncation:"
                         << describe(st);
  EXPECT_EQ(st.unrecovered, 0) << describe(st);
  livehd::diag::sink().clear();
}

// mode=wires must not touch register q pins.
TEST(Bitfuzz, WiresModeKeepsRegisterWidths) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
  auto  gio = lib.create_io("bf_reg_wires");
  gio->add_input("clk", 1);
  gio->set_bits("clk", 1);
  gio->add_input("d", 2);
  gio->set_bits("d", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto ff = gu::create_typed_node(*g, Ntype_op::Flop);
  gu::set_bits(ff.create_driver_pin(0), 8);
  g->get_input_pin("d").connect_sink(gu::setup_sink_by_name(ff, "din"));
  g->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(ff, "clock_pin"));
  ff.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  quiet_diag();
  auto st = livehd::bitfuzz::fuzz(g, wires_opts());

  EXPECT_EQ(st.cleared_state, 0) << "mode=wires must leave state alone";
  EXPECT_EQ(gu::bits_of(ff.create_driver_pin(0)), 8);
  livehd::diag::sink().clear();
}

// mode=all clears the flop q too. This one IS recoverable: the flop's width
// comes straight from its `din` driver, which is a sized graph input.
TEST(Bitfuzz, AllModeRecoversRegisterFromDin) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
  auto  gio = lib.create_io("bf_reg_all");
  gio->add_input("clk", 1);
  gio->set_bits("clk", 1);
  gio->add_input("d", 2);
  gio->set_bits("d", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto ff = gu::create_typed_node(*g, Ntype_op::Flop);
  gu::set_bits(ff.create_driver_pin(0), 8);
  g->get_input_pin("d").connect_sink(gu::setup_sink_by_name(ff, "din"));
  g->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(ff, "clock_pin"));
  ff.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  livehd::bitfuzz::Options o;
  o.mode = livehd::bitfuzz::Mode::All;

  quiet_diag();
  auto st = livehd::bitfuzz::fuzz(g, o);

  EXPECT_EQ(st.cleared_state, 1) << "mode=all must clear the flop q pin";
  EXPECT_EQ(st.unrecovered, 0) << "a flop fed by a sized input is recoverable from its din cone:" << describe(st);
  EXPECT_GT(gu::bits_of(ff.create_driver_pin(0)), 0);
  livehd::diag::sink().clear();
}

// repair contract: whatever inference could not bound gets its front-end
// annotation back, so no pin is ever left width-less for cgen to trip over.
TEST(Bitfuzz, RepairLeavesNoUnsizedPin) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
  auto  gio = lib.create_io("bf_repair");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_input("b", 2);
  gio->set_bits("b", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  // Div has NO bitwidth inference rule (bitwidth.cpp has no Ntype_op::Div
  // branch), so its width is unrecoverable by construction -- exactly the case
  // repair exists for.
  auto op = gu::create_typed_node(*g, Ntype_op::Div, 8);
  g->get_input_pin("a").connect_sink(gu::setup_sink_by_name(op, "a"));
  g->get_input_pin("b").connect_sink(gu::setup_sink_by_name(op, "b"));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  quiet_diag();
  auto st = livehd::bitfuzz::fuzz(g, wires_opts());

  EXPECT_EQ(st.repaired, 1) << "an unbounded pin must be repaired";
  EXPECT_EQ(st.no_rule, 1) << "Div must be reported as a missing inference rule, not a translation bug";
  EXPECT_EQ(gu::bits_of(op.create_driver_pin(0)), 8) << "repair must restore the original width";
  livehd::diag::sink().clear();
}

// mode=off is a strict no-op.
TEST(Bitfuzz, OffModeIsNoOp) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
  auto  gio = lib.create_io("bf_off");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_output("o", 2);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto op = gu::create_typed_node(*g, Ntype_op::Not, 8);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  quiet_diag();
  livehd::bitfuzz::Options o;  // Mode::Off by default
  auto                     st = livehd::bitfuzz::fuzz(g, o);

  EXPECT_EQ(st.cleared, 0);
  EXPECT_EQ(gu::bits_of(op.create_driver_pin(0)), 8);
  livehd::diag::sink().clear();
}

// Seeded register selection is a pure function of (seed, pin), so the same seed
// must clear the same set on every run.
TEST(Bitfuzz, SeededSelectionIsReproducible) {
  auto build = [](const char* name) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitfuzz_test");
    auto  gio = lib.create_io(name);
    gio->add_input("clk", 1);
    gio->set_bits("clk", 1);
    gio->add_input("d", 2);
    gio->set_bits("d", 8);
    gio->add_output("o", 3);
    gio->set_bits("o", 8);
    auto g = gio->create_graph();
    for (int i = 0; i < 8; ++i) {
      auto ff = gu::create_typed_node(*g, Ntype_op::Flop);
      gu::set_sbits(ff.create_driver_pin(0), 8);
      g->get_input_pin("d").connect_sink(gu::setup_sink_by_name(ff, "din"));
      g->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(ff, "clock_pin"));
      if (i == 0) {
        ff.create_driver_pin(0).connect_sink(g->get_output_pin("o"));
      }
    }
    return g;
  };

  livehd::bitfuzz::Options o;
  o.mode    = livehd::bitfuzz::Mode::All;
  o.seed    = 42;
  o.reg_pct = 50;

  quiet_diag();
  auto st1 = livehd::bitfuzz::fuzz(build("bf_seed1"), o);
  auto st2 = livehd::bitfuzz::fuzz(build("bf_seed2"), o);

  EXPECT_EQ(st1.cleared_state, st2.cleared_state) << "the same seed must select the same register subset";
  livehd::diag::sink().clear();
}

}  // namespace
