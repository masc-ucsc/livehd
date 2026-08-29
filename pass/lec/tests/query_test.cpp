// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Milestone-1 acceptance: the L0 encoder + L1 query prove tiny combinational
// modules equal / different, with verdicts that match the known ground truth
// (and, in the lec.cross path, lgcheck). Graphs are built programmatically so
// the test needs no reader.

#include "query.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "cell.hpp"
#include "encode.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"

using namespace livehd;
using livehd::lec::Verdict;

namespace {

// Build `out = a <op> b` over two `bits`-bit unsigned inputs. For reduce-style
// ops (And/Or/Xor) and Sum-add, both operands land on the multi-driver sink
// "as"; `swap` flips the edge order (to exercise commutativity in the equal case).
std::shared_ptr<hhds::Graph> build_binop(hhds::GraphLibrary& lib, const std::string& mod, Ntype_op op, int bits,
                                         bool swap = false) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits);
  gio->set_unsign("a", true);
  gio->add_input("b", 1);
  gio->set_bits("b", bits);
  gio->set_unsign("b", true);
  gio->add_output("out", 2);
  const int out_bits
      = op == Ntype_op::Sum ? bits + 1 : ((op == Ntype_op::EQ || op == Ntype_op::LT || op == Ntype_op::GT) ? 1 : bits);
  gio->set_bits("out", out_bits);
  gio->set_unsign("out", true);

  auto g    = gio->create_graph();
  auto node = graph_util::create_typed_node(*g, op);

  auto sink_a = graph_util::setup_sink_by_name(node, "as");
  auto a      = g->get_input_pin("a");
  auto b      = g->get_input_pin("b");
  if (swap) {
    b.connect_sink(sink_a);
    a.connect_sink(sink_a);
  } else {
    a.connect_sink(sink_a);
    b.connect_sink(sink_a);
  }

  auto dpin = node.create_driver_pin(0);
  graph_util::set_ubits(dpin, out_bits);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

// Build `out = a + const` (exercises the Nconst / constant path).
std::shared_ptr<hhds::Graph> build_add_const(hhds::GraphLibrary& lib, const std::string& mod, int64_t k, int bits) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits);
  gio->set_unsign("a", true);
  gio->add_output("out", 1);
  gio->set_bits("out", bits + 1);
  gio->set_unsign("out", true);

  auto g    = gio->create_graph();
  auto node = graph_util::create_typed_node(*g, Ntype_op::Sum);

  auto sink_a = graph_util::setup_sink_by_name(node, "as");
  g->get_input_pin("a").connect_sink(sink_a);
  auto cval = Dlop::create_integer(k);
  auto cpin = graph_util::create_const(*g, *cval);
  cpin.connect_sink(sink_a);

  auto dpin = node.create_driver_pin(0);
  graph_util::set_ubits(dpin, bits + 1);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

// Build a 4-bit Concat lane from an 8-bit input. The Concat cell contract is
// `value mod 2^window`; `pretruncate` spells that mask explicitly on one side.
std::shared_ptr<hhds::Graph> build_overwide_concat(hhds::GraphLibrary& lib, const std::string& mod, bool pretruncate) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", 8);
  gio->set_unsign("a", true);
  gio->add_output("out", 1);
  gio->set_bits("out", 4);  // unsigned u4
  gio->set_unsign("out", true);

  auto g    = gio->create_graph();
  auto lane = g->get_input_pin("a");
  if (pretruncate) {
    auto mask = graph_util::create_typed_node(*g, Ntype_op::Get_mask, 4);
    lane.connect_sink(mask.create_sink_pin(0));
    graph_util::create_const(*g, *Dlop::create_integer(15)).connect_sink(mask.create_sink_pin(2));
    lane = mask.create_driver_pin(0);
    graph_util::set_ubits(lane, 4);
  }

  auto concat = graph_util::create_typed_node(*g, Ntype_op::Concat, 4);
  lane.connect_sink(concat.create_sink_pin(0));
  graph_util::create_const(*g, *Dlop::create_integer(4)).connect_sink(concat.create_sink_pin(1));
  auto out = concat.create_driver_pin(0);
  graph_util::set_ubits(out, 4);
  out.connect_sink(g->get_output_pin("out"));
  return g;
}

std::shared_ptr<hhds::Graph> build_active_loop(hhds::GraphLibrary& lib, uint64_t count = 3) {
  auto body_io = lib.create_io("active_body");
  body_io->add_input("carry", 0);
  body_io->add_input("active", 1);
  body_io->add_output("next_carry", 2);
  body_io->add_output("next_active", 3);
  body_io->set_bits("carry", 9);
  body_io->set_bits("active", 1);
  body_io->set_bits("next_carry", 9);
  body_io->set_bits("next_active", 1);
  body_io->set_unsign("carry", true);
  body_io->set_unsign("active", true);
  body_io->set_unsign("next_carry", true);
  body_io->set_unsign("next_active", true);
  auto body = body_io->create_graph();

  auto sum = graph_util::create_typed_node(*body, Ntype_op::Sum, 9);
  body->get_input_pin("carry").connect_sink(sum.create_sink_pin(0));
  graph_util::create_const(*body, *Dlop::create_integer(1)).connect_sink(sum.create_sink_pin(0));

  // The lifted body itself preserves the carry while inactive; the occurrence
  // realization additionally inserts the inter-ordinal bypass required by the
  // compact call-binding contract.
  auto mux = graph_util::create_typed_node(*body, Ntype_op::Mux, 9);
  body->get_input_pin("active").connect_sink(mux.create_sink_pin(0));
  body->get_input_pin("carry").connect_sink(mux.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(mux.create_sink_pin(2));
  auto next_carry = mux.create_driver_pin(0);
  graph_util::set_bits(next_carry, 9);
  graph_util::set_unsign(next_carry);
  next_carry.connect_sink(body->get_output_pin("next_carry"));
  graph_util::create_const(*body, *Dlop::create_integer(0)).connect_sink(body->get_output_pin("next_active"));

  auto top_io = lib.create_io("active_top");
  top_io->add_input("seed", 0);
  top_io->add_input("enable", 1);
  top_io->add_output("result", 2);
  top_io->set_bits("seed", 9);
  top_io->set_bits("enable", 1);
  top_io->set_bits("result", 9);
  top_io->set_unsign("seed", true);
  top_io->set_unsign("enable", true);
  top_io->set_unsign("result", true);
  auto top  = top_io->create_graph();
  auto call = graph_util::create_typed_node(*top, Ntype_op::Sub);
  call.set_subnode(body_io,
                   hhds::Subnode_loop{
                       .first              = 0,
                       .step               = 1,
                       .count              = count,
                       .index_input        = std::nullopt,
                       .activation_input   = 1,
                       .next_active_output = 3,
                   });
  top->get_input_pin("seed").connect_sink(call.create_sink_pin(0));
  top->get_input_pin("enable").connect_sink(call.create_sink_pin(1));
  call.create_driver_pin(2).connect_sink(call.create_sink_pin(0));
  call.create_driver_pin(2).connect_sink(top->get_output_pin("result"));
  call.subnode_group().validate();
  return top;
}

std::shared_ptr<hhds::Graph> build_indexed_carry_loop(hhds::GraphLibrary& lib, int64_t first, uint64_t count,
                                                      bool observe_plain_output = false) {
  auto body_io = lib.create_io("indexed_body");
  body_io->add_input("index", 0);
  body_io->add_input("x", 1);
  body_io->add_input("carry", 2);
  body_io->add_output("next_carry", 3);
  for (const auto name : {"index", "x", "carry", "next_carry"}) {
    body_io->set_bits(name, 17);
    body_io->set_unsign(name, true);
  }
  if (observe_plain_output) {
    body_io->add_output("body_value", 4);
    body_io->set_bits("body_value", 17);
    body_io->set_unsign("body_value", true);
  }
  auto body = body_io->create_graph();
  auto sum  = graph_util::create_typed_node(*body, Ntype_op::Sum, 17);
  body->get_input_pin("index").connect_sink(sum.create_sink_pin(0));
  body->get_input_pin("x").connect_sink(sum.create_sink_pin(0));
  body->get_input_pin("carry").connect_sink(sum.create_sink_pin(0));
  auto sum_out = sum.create_driver_pin(0);
  graph_util::set_bits(sum_out, 17);
  graph_util::set_unsign(sum_out);
  sum_out.connect_sink(body->get_output_pin("next_carry"));
  if (observe_plain_output) {
    body->get_input_pin("x").connect_sink(body->get_output_pin("body_value"));
  }

  auto top_io = lib.create_io("indexed_top");
  top_io->add_input("x", 0);
  top_io->add_output("result", 1);
  top_io->set_bits("x", 17);
  top_io->set_bits("result", 17);
  top_io->set_unsign("x", true);
  top_io->set_unsign("result", true);
  if (observe_plain_output) {
    top_io->add_output("observed", 2);
    top_io->set_bits("observed", 17);
    top_io->set_unsign("observed", true);
  }
  auto top  = top_io->create_graph();
  auto loop = graph_util::create_typed_node(*top, Ntype_op::Sub);
  loop.set_name("loop_site");
  loop.set_subnode(body_io,
                   hhds::Subnode_loop{
                       .first              = first,
                       .step               = 1,
                       .count              = count,
                       .index_input        = 0,
                       .activation_input   = std::nullopt,
                       .next_active_output = std::nullopt,
                   });
  top->get_input_pin("x").connect_sink(loop.create_sink_pin(1));
  graph_util::create_const(*top, *Dlop::create_integer(0)).connect_sink(loop.create_sink_pin(2));
  auto result = loop.create_driver_pin(3);
  graph_util::set_bits(result, 17);
  graph_util::set_unsign(result);
  result.connect_sink(loop.create_sink_pin(2));
  result.connect_sink(top->get_output_pin("result"));
  if (observe_plain_output) {
    auto observed = loop.create_driver_pin(4);
    graph_util::set_bits(observed, 17);
    graph_util::set_unsign(observed);
    observed.connect_sink(top->get_output_pin("observed"));
  }
  loop.subnode_group().validate();
  return top;
}

}  // namespace

TEST(LecNames, PyropeQuotedStateMatchesDirectRtlName) {
  EXPECT_EQ(lec::canon_flop_name("`msg_port_enabled.e0`"), lec::canon_flop_name("msg_port_enabled.e0"));
  EXPECT_EQ(lec::canon_flop_name("top.`state.part`"), lec::canon_flop_name("top.state.part"));
  EXPECT_EQ(lec::canon_flop_name("csrMod\\_Mhpmevent10_0"), lec::canon_flop_name("csrMod_Mhpmevent10_0"));
}

TEST(CombEquiv, AndCommutativeProven) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::And, 4, false);
  auto               impl = build_binop(lib, "impl", Ntype_op::And, 4, true);  // b & a

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
}

TEST(CombEquiv, AndVsOrRefuted) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::And, 4);
  auto               impl = build_binop(lib, "impl", Ntype_op::Or, 4);

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Refuted) << r.detail;
}

TEST(CombEquiv, SumCommutativeProven) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::Sum, 4, false);
  auto               impl = build_binop(lib, "impl", Ntype_op::Sum, 4, true);  // b + a

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
}

TEST(CombEquiv, AddConstEqualProven) {
  hhds::GraphLibrary lib;
  auto               ref  = build_add_const(lib, "ref", 1, 4);
  auto               impl = build_add_const(lib, "impl", 1, 4);

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
}

TEST(CombEquiv, AddConstOffByOneRefuted) {
  hhds::GraphLibrary lib;
  auto               ref  = build_add_const(lib, "ref", 1, 4);
  auto               impl = build_add_const(lib, "impl", 2, 4);  // off by one

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Refuted) << r.detail;
}

TEST(CombEquiv, ConcatTruncatesOverwideLaneToDeclaredWindow) {
  hhds::GraphLibrary lib;
  auto               ref  = build_overwide_concat(lib, "ref", false);
  auto               impl = build_overwide_concat(lib, "impl", true);

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
}

TEST(CombEquiv, EngineBmcRefutes) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::And, 4);
  auto               impl = build_binop(lib, "impl", Ntype_op::Or, 4);

  lec::Lec_options o;
  o.engine = "bmc";
  auto r   = lec::prove_equal(ref.get(), impl.get(), o);
  EXPECT_EQ(r.verdict, Verdict::Refuted) << r.detail;
}

TEST(CombEquiv, NativeActivationLoopMatchesPrivatePhysicalRealization) {
  hhds::GraphLibrary compact_lib;
  auto               compact = build_active_loop(compact_lib);

  hhds::GraphLibrary physical_lib;
  ASSERT_TRUE(physical_lib.copy_from(compact_lib, "active_body"));
  ASSERT_TRUE(physical_lib.copy_from(compact_lib, "active_top"));
  auto physical = physical_lib.find_io("active_top")->get_graph();
  ASSERT_EQ(graph_util::materialize_occurrences(physical.get(), "test"), 1);

  lec::Lec_options options;
  options.engine = "ind";
  auto result    = lec::prove_equal(compact.get(), physical.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
}

TEST(CombEquiv, ProvenBodyAndMatchedDescriptorUseCompactLoopCertificate) {
  constexpr uint64_t kPastMaterializationCap = (1u << 20) + 1;
  hhds::GraphLibrary ref_lib;
  hhds::GraphLibrary impl_lib;
  auto               ref  = build_active_loop(ref_lib, kPastMaterializationCap);
  auto               impl = build_active_loop(impl_lib, kPastMaterializationCap);

  lec::Lec_options body_options;
  body_options.engine = "ind";
  auto body_result    = lec::prove_equal(ref_lib.find_io("active_body")->get_graph().get(),
                                      impl_lib.find_io("active_body")->get_graph().get(),
                                      body_options);
  ASSERT_EQ(body_result.verdict, Verdict::Proven) << body_result.detail;

  lec::Lec_options top_options;
  top_options.engine   = "ind";
  top_options.collapse = {"active_body"};  // the discharged body theorem
  auto result          = lec::prove_equal(ref.get(), impl.get(), top_options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
  EXPECT_NE(result.detail.find("loop certificate: 1 matched compact recurrence"), std::string::npos) << result.detail;
  ASSERT_EQ(result.loop_certificates.size(), 1);
  EXPECT_NE(result.loop_certificates.front().find("P0=descriptor-exact"), std::string::npos);
  EXPECT_NE(result.loop_certificates.front().find("P4/P5=ordinal-recurrence"), std::string::npos);
}

TEST(CombEquiv, IndexedCarryLoopCertificateOmitsNoDescriptorObligation) {
  hhds::GraphLibrary ref_lib;
  hhds::GraphLibrary impl_lib;
  auto               ref  = build_indexed_carry_loop(ref_lib, 0, 4);
  auto               impl = build_indexed_carry_loop(impl_lib, 0, 4);

  lec::Lec_options options;
  options.engine   = "ind";
  options.collapse = {"indexed_body"};
  auto result      = lec::prove_equal(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
  EXPECT_NE(result.detail.find("loop certificate: 1 matched compact recurrence"), std::string::npos) << result.detail;
  ASSERT_EQ(result.loop_certificates.size(), 1);
  EXPECT_NE(result.loop_certificates.front().find("body=indexed_body"), std::string::npos);
}

TEST(CombEquiv, IndexedCarryDescriptorMismatchFallsBackAndRefutes) {
  hhds::GraphLibrary ref_lib;
  hhds::GraphLibrary impl_lib;
  auto               ref  = build_indexed_carry_loop(ref_lib, 0, 4);
  auto               impl = build_indexed_carry_loop(impl_lib, 1, 4);

  lec::Lec_options options;
  options.engine   = "ind";
  options.collapse = {"indexed_body"};
  auto result      = lec::prove_equal(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Refuted) << result.detail;
  EXPECT_EQ(result.detail.find("loop certificate:"), std::string::npos) << result.detail;
  EXPECT_TRUE(result.loop_certificates.empty());
}

TEST(CombEquiv, ZeroCountObservedPlainOutputIsRejectedBeforeCertification) {
  hhds::GraphLibrary lib;
  EXPECT_THROW((void)build_indexed_carry_loop(lib, 0, 0, true), std::logic_error);
}

// --- worker-pipe framing -----------------------------------------------------
//
// The soundness contract of frame_blob/unframe_blob: a blob the pipe truncated
// must FAIL to unframe, so the parent tags the worker "no result" (Unknown)
// instead of reading a half-parsed Query_result whose trailing soundness
// qualifiers (bounded / unsupported / nothing_compared) silently default to
// "nothing to worry about". Before framing, a child SIGKILLed mid-write handed
// the parent an UNBOUNDED Proven for a bounded claim.
TEST(WorkerFrame, RoundTripsAnyPayload) {
  for (const auto& payload : std::vector<std::string>{{}, {"x"}, std::string(9000, '\x01')}) {
    const std::string framed = livehd::lec::frame_blob(payload);
    EXPECT_EQ(framed.size(), payload.size() + sizeof(uint64_t));
    std::string_view got;
    ASSERT_TRUE(livehd::lec::unframe_blob(framed, got));
    EXPECT_EQ(std::string(got), payload);
  }
}

TEST(WorkerFrame, TruncationIsNotAResult) {
  const std::string payload(4096, '\x7f');  // > PIPE_BUF: a real short-write shape
  const std::string framed = livehd::lec::frame_blob(payload);
  std::string_view  got;
  // Every proper prefix must be rejected: a header cut mid-length, a header with
  // no payload, and a payload cut at any point (including one byte short).
  for (size_t n = 0; n < framed.size(); ++n) {
    EXPECT_FALSE(livehd::lec::unframe_blob(std::string_view{framed}.substr(0, n), got))
        << "a " << n << "-byte prefix of a " << framed.size() << "-byte frame was accepted";
  }
  // Trailing garbage (two children's writes interleaved) is not a result either.
  EXPECT_FALSE(livehd::lec::unframe_blob(framed + "junk", got));
  EXPECT_TRUE(livehd::lec::unframe_blob(framed, got));
}
