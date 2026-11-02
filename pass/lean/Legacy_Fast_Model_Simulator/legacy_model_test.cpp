// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "legacy_model.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "emit_design_cert.hpp"
#include "emit_legacy_fast_model.hpp"
#include "gtest/gtest.h"
#include "lean_format.hpp"
#include "design_scan.hpp"
#include "graph_library_singleton.hpp"
#include "node_util.hpp"
#include "design_io_meta.hpp"
namespace lean_export {
namespace {
using livehd::graph_util::create_typed_node;
using livehd::graph_util::set_bits;
PinRef     input(uint32_t id, uint32_t width) { return {PinKind::Input, id, 0, width, 0, {}}; }
PinRef     constant(std::string value, uint32_t width) { return {PinKind::Constant, 3, 0, 0, width, std::move(value), width}; }
DesignScan binary(ScanOp op, uint32_t width) {
  DesignScan d;
  d.name   = "owned";
  d.inputs = {
      {"a", 2000000000, 8, {}},
      {"b", 2000000001, 8, {}}
  };
  d.nodes = {
      {10, op, width, false, {{0, 0, input(2000000000, 8)}, {1, 1, input(2000000001, 8)}}}
  };
  d.outputs = {
      {"y", 0, width, PinRef{PinKind::Node, 10, 0, width, 0, {}}}
  };
  return d;
}
TEST(LegacyModel, UsesOwnedCertificateRatherThanScanNodeSemantics) {
  auto d = binary(ScanOp::Sum, 8);
  auto c = build_certificate(d, {});
  d.nodes.clear();
  c.nodes.front().op.kind = Operation::Xor;
  std::ostringstream os;
  emit_legacy_model(d, c, {}, os);
  EXPECT_NE(os.str().find("^^^"), std::string::npos);
  EXPECT_NE(os.str().find("LGraphOp.Op_Xor"), std::string::npos);
  EXPECT_EQ(os.str().find("LGraphOp.Op_Sum"), std::string::npos);
}
// Two clock domains: `_next`/`_step` take a `<Top>_edges` record, and each flop
// is guarded by its OWN domain's edge. Off its edge a flop holds, except that an
// asserted asynchronous reset still resets it (DesignSemantics.srcFlopNext). One
// domain emits no edges record and no extra argument, i.e. exactly what it did
// before multi-domain support.
DesignScan two_domain_counter() {
  DesignScan d;
  d.name   = "twoclk";
  d.inputs = {
      {"rst", 2000000000, 1, {}},
      {  "a", 2000000001, 8, {}}
  };
  d.clocks = {
      {"k_a", "clk_a", true, false},
      {"k_b", "clk_b", true, false}
  };
  Flop qa;
  qa.id            = 10;
  qa.width         = 8;
  qa.raw_name      = "qa";
  qa.din           = input(2000000001, 8);
  qa.reset         = input(2000000000, 1);
  qa.asynchronous  = true;
  qa.initial       = "5";
  qa.reset_input   = 2000000000;
  qa.clock_ordinal = 0;
  Flop qb;
  qb.id            = 11;
  qb.width         = 8;
  qb.raw_name      = "qb";
  qb.din           = input(2000000001, 8);
  qb.clock_ordinal = 1;
  d.flops          = {qa, qb};
  d.outputs        = {
      {"ya", 0, 8, PinRef{PinKind::Flop, 10, 0, 8, 0, {}}},
      {"yb", 1, 8, PinRef{PinKind::Flop, 11, 0, 8, 0, {}}}
  };
  return d;
}
TEST(LegacyModel, TwoClockDomainsGuardEachFlopByItsOwnEdge) {
  auto              d = two_domain_counter();
  const auto        c = build_certificate(d, {});
  LegacyEmitOptions o;
  o.emit_cert        = true;
  o.emit_fast_bridge = true;
  o.cert_wf          = LeanCertWFMode::Chunked;
  std::ostringstream os;
  ASSERT_NO_THROW(emit_legacy_model(d, c, o, os));
  const auto text = os.str();
  EXPECT_NE(text.find("structure twoclk_edges where\n  edge_clk_a : Bool\n  edge_clk_b : Bool\n"), std::string::npos);
  EXPECT_NE(text.find("def twoclk_next (e : twoclk_edges) (i : twoclk_in) (s : twoclk_state)"), std::string::npos);
  EXPECT_NE(text.find("def twoclk_nextStateFromCert (e : twoclk_edges)"), std::string::npos);
  EXPECT_NE(text.find("theorem twoclk_next_refines_fast (e : twoclk_edges)"), std::string::npos);
  // qa (async, domain a): reset still acts off-edge; qb (sync, domain b): holds.
  EXPECT_NE(text.find(":= if e.edge_clk_a then flop_next"), std::string::npos);
  EXPECT_NE(text.find(" false ((bv_zext"), std::string::npos) << "the async off-edge form forces the enable false";
  EXPECT_NE(text.find(":= if e.edge_clk_b then flop_next"), std::string::npos);
  EXPECT_NE(text.find(" else s.st_qb\n"), std::string::npos);
  EXPECT_EQ(text.find("by sorry"), std::string::npos);
  if (const char* destination = std::getenv("LHD_LEAN_MULTICLOCK_OUTPUT")) {
    const auto path = std::filesystem::path(destination) / "twoclk_Lgraph.lean";
    write_atomic(path.string(), [&](std::ostream& stream) { stream << text; });
  }
  d.clocks.pop_back();
  for (auto& f : d.flops) {
    f.clock_ordinal = 0;
  }
  std::ostringstream one;
  ASSERT_NO_THROW(emit_legacy_model(d, build_certificate(d, {}), o, one));
  EXPECT_EQ(one.str().find("_edges"), std::string::npos);
  EXPECT_EQ(one.str().find("(e : "), std::string::npos);
  EXPECT_EQ(one.str().find("if e."), std::string::npos);
}
// A memory on more than one domain (DCERT3 commit table, decision #9): both
// legacy emitters take its next image from `legacy_memory_next`, which is the
// certificate's `srcMemNext`. Here the memory is on domains 0 and 2 of three, so
// domain 1 must not appear in any condition.
TEST(LegacyModel, CommitTableMemoryNextIsTheCertificateRule) {
  LegacyNames n;
  n.edges = {"edge_a", "edge_b", "edge_c"};
  MemoryDriver m;
  m.next_img     = 30;
  m.clock        = 0;
  m.commits      = {{uint64_t{1} << 0, 31}, {uint64_t{1} << 2, 32}};
  const auto img = [](uint32_t id) { return "img" + std::to_string(id); };
  EXPECT_EQ(legacy_memory_next(m, n, img, "held"),
            "if e.edge_a && e.edge_c then img30 else if e.edge_a && !e.edge_c then img31 "
            "else if !e.edge_a && e.edge_c then img32 else held");
  // No commit table: the one-domain rule on the memory's clock.
  m.commits.clear();
  EXPECT_EQ(legacy_memory_next(m, n, img, "held"), "if e.edge_a then img30 else held");
  // One domain in the design: no edges record, no guard.
  n.edges.clear();
  EXPECT_EQ(legacy_memory_next(m, n, img, "held"), "img30");
}
// A whole-array memory (decision #2): the fast model writes `mem_load` and
// `mem_to_bus` (Translation/WholeArray), and the bridge proves them with
// `mem_load_bridge` / `mem_to_bus_bridge` (Translation/WholeArrayBridge).
TEST(LegacyModel, WholeArrayMemoryUsesLoadAndToBus) {
  DesignScan d;
  d.name   = "wa";
  d.inputs = {
      {"upd", 2000000000, 24, {}},
      {"rst", 2000000001,  1, {}}
  };
  Memory m;
  m.id          = 10;
  m.bits        = 8;
  m.addr_width  = 2;
  m.size        = 3;
  m.whole_array = true;
  m.update      = input(2000000000, 24);
  m.bulk_reset  = input(2000000001, 1);
  m.ports.push_back({0, 0, constant("1", 2), {}, constant("1", 1), std::nullopt});
  m.read_ports = {0};
  m.fwd_rows   = {{}};
  d.memories.emplace(10, m);
  d.nodes.push_back({10, ScanOp::Memory, 0, false, {}});
  d.nodes.push_back({10, ScanOp::Memory, 0, false, {}});
  d.nodes.back().memory_read = kMemoryReadAll;
  d.outputs                  = {
      {  "q", 0,  8,                  PinRef{PinKind::Memory, 10, 0, 8, 0, {}}},
      {"all", 1, 24, PinRef{PinKind::Memory, 10, kMemoryReadAllPid, 24, 0, {}}}
  };
  const auto c = build_certificate(d, {});
  for (const bool bridge : {false, true}) {
    LegacyEmitOptions o;
    o.emit_cert        = bridge;
    o.emit_fast_bridge = bridge;
    std::ostringstream out;
    ASSERT_NO_THROW(emit_legacy_model(d, c, o, out));
    const auto text = out.str();
    EXPECT_NE(text.find(bridge ? "import LeanSemanticPrimitives.Translation.WholeArrayBridge"
                               : "import LeanSemanticPrimitives.Translation.WholeArray\n"),
              std::string::npos);
    EXPECT_NE(text.find("then mem_load "), std::string::npos);
    EXPECT_NE(text.find("(mem_to_bus "), std::string::npos);
    if (bridge) {
      EXPECT_NE(text.find("mem_load_bridge"), std::string::npos);
      EXPECT_NE(text.find("mem_to_bus_bridge"), std::string::npos);
      EXPECT_EQ(text.find("by sorry"), std::string::npos);
    }
  }
}
TEST(LegacyModel, PreservesGlobalSelectorUniquenessAndEscapesMappingComments) {
  auto d            = binary(ScanOp::Or, 8);
  d.inputs[0].name  = "a-b";
  d.inputs[1].name  = "a_x2d_b";
  d.outputs[0].name = "a-b\nend";
  const auto n      = legacy_names(d, {});
  EXPECT_NE(n.inputs[0], n.inputs[1]);
  EXPECT_TRUE(n.outputs[0].starts_with("out_"));
  std::ostringstream out;
  emit_legacy_field_mapping(d, n, out);
  EXPECT_NE(out.str().find("a-b\\nend"), std::string::npos);
}
TEST(LegacyModel, ResetPolarityAndReadRegisterProvenanceAreOwned) {
  DesignScan d;
  d.name   = "reset";
  d.inputs = {
      {"rst", 2000000000, 1, {}},
      { "en", 2000000001, 1, {}}
  };
  Flop f;
  f.id           = 10;
  f.width        = 8;
  f.raw_name     = "q";
  f.din          = constant("12", 4);
  f.enable       = input(2000000001, 1);
  f.reset        = input(2000000000, 1);
  f.asynchronous = true;
  f.active_low   = true;
  f.initial      = "7";
  f.reset_input  = 2000000000;
  d.flops        = {f};
  d.outputs      = {
      {"q", 0, 8, PinRef{PinKind::Flop, 10, 0, 8, 0, {}}}
  };
  const auto c = build_certificate(d, {});
  const auto n = legacy_names(d, {});
  EXPECT_EQ(c.flops.front().origin, 10);
  EXPECT_EQ(n.flop_field(c.flops.front()), "st_q");
  const auto leaf = legacy_source_leaf(c, n, c.sources.front());
  EXPECT_NE(leaf.find("if (!(bitvec_nonzero i.in_rst))"), std::string::npos);
  EXPECT_NE(leaf.find("Int.ofNat 7"), std::string::npos);
}
TEST(LegacyModel, LoweringReadsSemanticWidthsAndSextAmount) {
  auto d = binary(ScanOp::Div, 4);
  auto c = build_certificate(d, {});
  EXPECT_NE(lower_fast_expr(c, c.nodes.front()).text.find("BitVec 8"), std::string::npos);
  d.nodes.front().op                 = ScanOp::Sext;
  d.nodes.front().operands[1].driver = constant("3", 2);
  c                                  = build_certificate(d, {});
  EXPECT_NE(lower_fast_expr(c, c.nodes.front()).text.find("BitVec.toNat @1@"), std::string::npos);
  // A comparison is signed because its OPERANDS are, not its 1-bit result.
  d.nodes.front().op                           = ScanOp::LT;
  d.nodes.front().operands[0].driver.is_signed = true;
  d.nodes.front().operands[1].driver           = input(2000000001, 8);
  d.nodes.front().operands[1].driver.is_signed = true;
  c                                            = build_certificate(d, {});
  EXPECT_NE(lower_fast_expr(c, c.nodes.front()).text.find("BitVec.toInt @0@"), std::string::npos);
}
TEST(LegacyModel, NaryMixedWidthBridgeFixtures) {
  struct Shape {
    const char* name;
    ScanOp      op;
    uint32_t    arity;
    uint32_t    addends;
    uint32_t    width;
  };
  const std::vector<Shape> shapes{
      {         "and_one", ScanOp::And,  1,  0,  5},
      {         "and_six", ScanOp::And,  6,  0,  8},
      {   "and_seventeen", ScanOp::And, 17,  0, 32},
      {         "xor_one", ScanOp::Xor,  1,  0,  5},
      {       "xor_three", ScanOp::Xor,  3,  0,  8},
      {   "xor_seventeen", ScanOp::Xor, 17,  0, 32},
      {      "sum_negate", ScanOp::Sum,  1,  0,  5},
      {       "sum_three", ScanOp::Sum,  3,  3,  8},
      {     "sum_sixteen", ScanOp::Sum, 16, 16, 32},
      {       "sum_mixed", ScanOp::Sum,  4,  2,  8},
      {"sum_subtract_all", ScanOp::Sum,  3,  0,  8},
  };
  const char* destination = std::getenv("LHD_LEAN_NARY_BRIDGE_OUTPUT");
  for (const auto& shape : shapes) {
    SCOPED_TRACE(shape.name);
    DesignScan d;
    d.name = shape.name;
    DesignNode                  node{10, shape.op, shape.width, false, {}};
    const std::vector<uint32_t> widths{17, 1, 3, 9};
    for (uint32_t i = 0; i < shape.arity; ++i) {
      const auto id    = 2000000000 + i;
      const auto width = widths[i % widths.size()];
      d.inputs.push_back({"x" + std::to_string(i), id, width, {}});
      node.operands.push_back({i, shape.op == ScanOp::Sum ? uint32_t(i >= shape.addends) : i, input(id, width)});
    }
    d.nodes.push_back(node);
    d.outputs.push_back({
        "y",
        0,
        shape.width,
        PinRef{PinKind::Node, 10, 0, shape.width, 0, {}}
    });
    const auto        certificate = build_certificate(d, {});
    LegacyEmitOptions options;
    options.emit_fast_bridge = true;
    options.cert_wf          = LeanCertWFMode::Chunked;
    options.cert_chunk_size  = 2;
    std::ostringstream out;
    ASSERT_NO_THROW(emit_legacy_model(d, certificate, options, out));
    EXPECT_NE(out.str().find("import LeanSemanticPrimitives.Translation.NaryBridge"), std::string::npos);
    EXPECT_NE(out.str().find("_comb_refines_fast"), std::string::npos);
    EXPECT_EQ(out.str().find("by sorry"), std::string::npos);
    if (destination) {
      const auto path = std::filesystem::path(destination) / (d.name + "_Lgraph.lean");
      write_atomic(path.string(), [&](std::ostream& stream) { stream << out.str(); });
    }
  }
}

TEST(LegacyModel, LargeOrBridgeFixtures) {
  struct Shape {
    const char* name;
    uint32_t    arity;
    uint32_t    width;
  };
  for (const auto& shape : {
           Shape{      "or_six",  6,   8},
           Shape{"or_fortyfour", 44,   1},
           Shape{     "or_wide", 55, 760}
  }) {
    DesignScan d;
    d.name = shape.name;
    DesignNode node{10, ScanOp::Or, shape.width, false, {}};
    for (uint32_t i = 0; i < shape.arity; ++i) {
      const uint32_t width = i % 3 == 0 ? 1 : i % 3 == 1 ? 8 : 17;
      d.inputs.push_back({"x" + std::to_string(i), 2000000000 + i, width, {}});
      node.operands.push_back({i, i, input(2000000000 + i, width)});
    }
    d.nodes.push_back(node);
    d.outputs.push_back({
        "y",
        0,
        shape.width,
        PinRef{PinKind::Node, 10, 0, shape.width, 0, {}}
    });
    const auto        c = build_certificate(d, {});
    LegacyEmitOptions options;
    options.emit_fast_bridge = true;
    options.cert_wf          = LeanCertWFMode::Chunked;
    std::ostringstream out;
    emit_legacy_model(d, c, options, out);
    EXPECT_NE(out.str().find("import LeanSemanticPrimitives.Translation.OrBridge"), std::string::npos);
    EXPECT_NE(out.str().find("orn_nonempty_bv_bridge"), std::string::npos);
    EXPECT_EQ(out.str().find("BitVec.zero_or"), std::string::npos);
    out << "\nnamespace " << d.name << "_Lgraph\n"
        << "example : (" << d.name << "_comb default).out_y = 0 := by native_decide\n"
        << "example : (" << d.name << "_comb { (default : " << d.name << "_in) with in_x0 := 1 }).out_y = 1 := by native_decide\n"
        << "end " << d.name << "_Lgraph\n";
    if (const char* destination = std::getenv("LHD_LEAN_OR_BRIDGE_OUTPUT")) {
      const auto path = std::filesystem::path(destination) / (d.name + "_Lgraph.lean");
      write_atomic(path.string(), [&](std::ostream& stream) { stream << out.str(); });
    }
  }
}

TEST(LegacyModel, UnsupportedProofShapeFailsAtomicallyAndRequiresExplicitFallback) {
  const auto        d = binary(ScanOp::Mult, 8);
  const auto        c = build_certificate(d, {});
  LegacyEmitOptions o;
  o.cert_wf       = LeanCertWFMode::Chunked;
  const auto path = (std::filesystem::current_path() / "legacy_atomic.lean").string();
  {
    std::ofstream out(path);
    out << "previous";
  }
  EXPECT_THROW(write_atomic(path, [&](std::ostream& out) { emit_legacy_model(d, c, o, out); }), std::runtime_error);
  std::ifstream existing(path);
  std::string   text;
  existing >> text;
  EXPECT_EQ(text, "previous");
  EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));
  std::filesystem::remove(path);
  o.cert_wf_fallback = LeanCertWFFallback::Eval;
  std::ostringstream out;
  EXPECT_NO_THROW(emit_legacy_model(d, c, o, out));
  EXPECT_EQ(out.str().find("by eval"), std::string::npos);
  EXPECT_EQ(out.str().find("by sorry"), std::string::npos);
  o.cert_wf          = LeanCertWFMode::Skip;
  o.emit_fast_bridge = true;
  // A two-operand Mult has a bridge (mul2_bridge); a three-operand one does not.
  EXPECT_NO_THROW(emit_legacy_model(d, c, o, out));
  auto d3 = d;
  d3.inputs.push_back({"c", 2000000002, 8, {}});
  d3.nodes.front().operands.push_back({2, 2, input(2000000002, 8)});
  const auto c3 = build_certificate(d3, {});
  EXPECT_THROW(emit_legacy_model(d3, c3, o, out), std::runtime_error);
}

TEST(LegacyModel, MulBridgeFixtures) {
  struct Shape {
    const char* name;
    uint32_t    wa, wb, width;
    uint64_t    a, b, product;  // product mod 2^width
  };
  for (const auto& shape : {
           Shape{"mul_wide", 17, 9, 26, 131071, 511, 66977281}, // no wrap: (2^17-1)(2^9-1) < 2^26
           Shape{"mul_wrap",  8, 8,  8,    200,   3,       88}, // 600 mod 256
           Shape{"mul_same",  8, 8, 16,    255, 255,    65025},
  }) {
    SCOPED_TRACE(shape.name);
    DesignScan d;
    d.name = shape.name;
    d.inputs.push_back({"x0", 2000000000, shape.wa, {}});
    d.inputs.push_back({"x1", 2000000001, shape.wb, {}});
    d.nodes.push_back({
        10,
        ScanOp::Mult,
        shape.width,
        false,
        {{0, 0, input(2000000000, shape.wa)}, {1, 1, input(2000000001, shape.wb)}}
    });
    d.outputs.push_back({
        "y",
        0,
        shape.width,
        PinRef{PinKind::Node, 10, 0, shape.width, 0, {}}
    });
    const auto        c = build_certificate(d, {});
    LegacyEmitOptions options;
    options.emit_fast_bridge = true;
    options.cert_wf          = LeanCertWFMode::Chunked;
    options.cert_wf_fallback = LeanCertWFFallback::Eval;  // no simple WF lemma for Mult
    std::ostringstream out;
    ASSERT_NO_THROW(emit_legacy_model(d, c, options, out));
    EXPECT_NE(out.str().find("import LeanSemanticPrimitives.Translation.MulBridge"), std::string::npos);
    EXPECT_NE(out.str().find("mul2_bridge"), std::string::npos);
    EXPECT_EQ(out.str().find("by sorry"), std::string::npos);
    out << "\nnamespace " << d.name << "_Lgraph\n"
        << "example : (" << d.name << "_comb { (default : " << d.name << "_in) with in_x0 := " << shape.a
        << ", in_x1 := " << shape.b << " }).out_y = " << shape.product << " := by native_decide\n"
        << "end " << d.name << "_Lgraph\n";
    if (const char* destination = std::getenv("LHD_LEAN_MUL_BRIDGE_OUTPUT")) {
      const auto path = std::filesystem::path(destination) / (d.name + "_Lgraph.lean");
      write_atomic(path.string(), [&](std::ostream& stream) { stream << out.str(); });
    }
  }
}
// A Sext at any sign position has a bridge (decision #21). Before, the bridge
// refused every position but the operand's (or node's) width, so a design that
// sign-extends the low bits of a wider value (B05, the yosys-slang mixed-edge
// memory) had no fast-model proof.
TEST(LegacyModel, SextBridgeFixtures) {
  struct Shape {
    const char*        name;
    uint32_t           width;
    std::optional<int> position;  // a constant sign position; none: the amount is input b
    uint64_t           a, b, result;
  };
  for (const auto& shape : {
           Shape{     "sext_interior",  8,            3, 0x0C, 0,   0xFC}, // low 3 bits 100: negative
           Shape{"sext_interior_wide", 16,            5, 0x10, 0, 0xFFF0}, // low 5 bits 10000, to 16 bits
           Shape{     "sext_zero_pos",  8,            0, 0xFF, 0,      0}, // position 0 gives 0
           Shape{        "sext_above", 16,           12, 0xFF, 0,   0xFF}, // sign bit above the operand: zero-extend
           Shape{      "sext_runtime",  8, std::nullopt, 0x0C, 3,   0xFC},
  }) {
    SCOPED_TRACE(shape.name);
    auto d                              = binary(ScanOp::Sext, shape.width);
    d.name                              = shape.name;
    d.inputs[1].width                   = 4;
    d.nodes[0].operands[1].driver.width = 4;
    if (shape.position) {
      d.nodes[0].operands[1].driver = constant(std::to_string(*shape.position), 4);
    }
    const auto        c = build_certificate(d, {});
    LegacyEmitOptions options;
    options.emit_fast_bridge = true;
    options.cert_wf          = LeanCertWFMode::Chunked;
    std::ostringstream out;
    ASSERT_NO_THROW(emit_legacy_model(d, c, options, out));
    EXPECT_NE(out.str().find("import LeanSemanticPrimitives.Translation.SextBridge"), std::string::npos);
    EXPECT_NE(out.str().find("sext_bridge_any"), std::string::npos);
    EXPECT_EQ(out.str().find("by sorry"), std::string::npos);
    out << "\nnamespace " << d.name << "_Lgraph\n"
        << "example : (" << d.name << "_comb { (default : " << d.name << "_in) with in_a := " << shape.a << ", in_b := " << shape.b
        << " }).out_y = " << shape.result << " := by native_decide\n"
        << "end " << d.name << "_Lgraph\n";
    if (const char* destination = std::getenv("LHD_LEAN_SEXT_BRIDGE_OUTPUT")) {
      const auto path = std::filesystem::path(destination) / (d.name + "_Lgraph.lean");
      write_atomic(path.string(), [&](std::ostream& stream) { stream << out.str(); });
    }
  }
  // The operand's own width keeps the OpBridge lemmas and needs no new import.
  auto d                        = binary(ScanOp::Sext, 16);
  d.nodes[0].operands[1].driver = constant("8", 4);
  const auto        c           = build_certificate(d, {});
  LegacyEmitOptions options;
  options.emit_fast_bridge = true;
  std::ostringstream out;
  ASSERT_NO_THROW(emit_legacy_model(d, c, options, out));
  EXPECT_EQ(out.str().find("SextBridge"), std::string::npos);
  EXPECT_NE(out.str().find("sext_bridge "), std::string::npos);
}
TEST(LegacyModel, PartialChunkExperimentMakesNoWholeGraphClaim) {
  auto d = binary(ScanOp::Or, 8);
  d.nodes.push_back({11, ScanOp::Or, 8, false, {{0, 0, PinRef{PinKind::Node, 10, 0, 8, 0, {}}}}});
  auto              c = build_certificate(d, {});
  LegacyEmitOptions o;
  o.cert_wf          = LeanCertWFMode::Chunked;
  o.cert_chunk_size  = 1;
  o.cert_chunk_limit = 1;
  std::ostringstream out;
  emit_legacy_model(d, c, o, out);
  EXPECT_NE(out.str().find("owned_wf_chunk0_wf"), std::string::npos);
  EXPECT_EQ(out.str().find("theorem owned_graphCert_wf"), std::string::npos);
}

// Optional saved artifacts use a caller-supplied project-local directory. They
// exercise semantic deltas with an independent Lean interpreter/compiler oracle.
TEST(LegacyModel, ArithmeticAndSimultaneousStateOracleFixtures) {
  const char*             destination = std::getenv("LHD_LEAN_LEGACY_OUTPUT");
  std::vector<DesignScan> designs;
  for (const auto& [name, op, width] : std::vector<std::tuple<std::string, ScanOp, uint32_t>>{
           {  "tiny_div_narrow",  ScanOp::Div,  4},
           {"tiny_sext_dynamic", ScanOp::Sext,  8},
           {   "tiny_slt_mixed",   ScanOp::LT,  1},
           {   "tiny_sgt_mixed",   ScanOp::GT,  1},
           {    "tiny_sra_wide",  ScanOp::SRA, 16}
  }) {
    auto d                              = binary(op, width);
    d.name                              = name;
    d.inputs[1].width                   = 4;
    d.nodes[0].operands[1].driver.width = 4;
    // signed comparisons: the OPERANDS are signed (the result never decides)
    d.nodes[0].operands[0].driver.is_signed = op == ScanOp::LT || op == ScanOp::GT;
    d.nodes[0].operands[1].driver.is_signed = op == ScanOp::LT || op == ScanOp::GT;
    designs.push_back(d);
  }
  for (const auto& [name, operand_width, result_width] : std::vector<std::tuple<std::string, uint32_t, uint32_t>>{
           { "tiny_sext_trunc32",  64, 32},
           { "tiny_sext_trunc64", 127, 64},
           {  "tiny_sext_same32",  32, 32},
           {"tiny_sext_memory64", 127, 64}
  }) {
    auto d                              = binary(ScanOp::Sext, result_width);
    d.name                              = name;
    d.inputs[0].width                   = operand_width;
    d.nodes[0].operands[0].driver.width = operand_width;
    d.nodes[0].operands[1].driver       = constant(std::to_string(result_width), result_width == 32 ? 6 : 7);
    if (name == "tiny_sext_memory64") {
      Memory memory;
      memory.id         = 20;
      memory.bits       = operand_width;
      memory.addr_width = 1;
      memory.size       = 2;
      memory.ports.push_back({0, 0, constant("0", 1), {}, constant("1", 1), std::nullopt});
      memory.read_ports = {0};
      d.memories.emplace(memory.id, memory);
      d.nodes[0].operands[0].driver = PinRef{PinKind::Memory, memory.id, 0, operand_width, 0, {}};
      d.nodes.insert(d.nodes.begin(), {memory.id, ScanOp::Memory, 0, false, {}});
    }
    designs.push_back(d);
  }
  DesignScan swap;
  swap.name = "tiny_swap";
  Flop a, b;
  a.id    = 10;
  b.id    = 11;
  a.width = b.width = 8;
  a.din             = PinRef{PinKind::Flop, 11, 0, 8, 0, {}};
  b.din             = PinRef{PinKind::Flop, 10, 0, 8, 0, {}};
  swap.flops        = {a, b};
  swap.outputs      = {
      {"y", 0, 8, *b.din}
  };
  designs.push_back(swap);
  for (const auto& d : designs) {
    const auto         c = build_certificate(d, {});
    std::ostringstream legacy, verified;
    LegacyEmitOptions  options;
    const bool         signed_compare = d.name == "tiny_slt_mixed" || d.name == "tiny_sgt_mixed";
    options.emit_fast_bridge          = signed_compare || (d.name.starts_with("tiny_sext_") && d.name != "tiny_sext_dynamic");
    emit_legacy_model(d, c, options, legacy);
    if (signed_compare) {
      EXPECT_NE(legacy.str().find(d.name == "tiny_slt_mixed" ? "slt_widths_bridge" : "sgt_widths_bridge"), std::string::npos);
    }
    if (d.name == "tiny_sext_memory64") {
      EXPECT_NE(legacy.str().find("evalNodeC_bridge"), std::string::npos);
    }
    emit_design_cert(d, c, verified);
    EXPECT_FALSE(legacy.str().empty());
    if (!destination) {
      continue;
    }
    for (const auto& [mode, text] : std::vector<std::pair<std::string, std::string>>{
             {  "legacy",   legacy.str()},
             {"verified", verified.str()}
    }) {
      const auto dir = std::filesystem::path(destination) / mode / "tiny" / d.name / "export";
      std::filesystem::create_directories(dir);
      std::ofstream out(dir / (d.name + "_Lgraph.lean"));
      out << text;
      ASSERT_TRUE(out.good());
    }
  }
}

TEST(LegacyModel, ConstantChunksUseSymbolicComposition) {
  DesignScan d;
  d.name    = "tiny_const_chunk";
  d.outputs = {
      {"y", 0, 2, PinRef{PinKind::Node, 10, 0, 2, 0, {}}}
  };
  CertificateIR c;
  c.nodes = {
      {10, {Operation::Const, 0, "2"}, 2, {}}
  };
  c.outputs = {
      {10, 2}
  };
  index_certificate(c);
  LegacyEmitOptions options;
  options.cert_wf = LeanCertWFMode::Chunked;
  std::ostringstream legacy, verified;
  emit_legacy_model(d, c, options, legacy);
  emit_design_cert(d, c, verified);
  EXPECT_NE(legacy.str().find("LegacyCertWF.chunk_of_constants"), std::string::npos);
  EXPECT_EQ(legacy.str().find("by eval"), std::string::npos);
  if (const char* destination = std::getenv("LHD_LEAN_LEGACY_OUTPUT")) {
    for (const auto& [mode, text] : std::vector<std::pair<std::string, std::string>>{
             {  "legacy",   legacy.str()},
             {"verified", verified.str()}
    }) {
      const auto dir = std::filesystem::path(destination) / mode / "tiny" / d.name / "export";
      std::filesystem::create_directories(dir);
      std::ofstream out(dir / (d.name + "_Lgraph.lean"));
      out << text;
      ASSERT_TRUE(out.good());
    }
  }
}

// Graph-out-of-scope coverage for the LEGACY emitter as well. Both emitters are
// wired to `publish_pair`, so both must hold the property.
TEST(DesignIoMeta, LegacyEmissionAlsoOutlivesTheGraph) {
  DesignScan    design;
  CertificateIR cert;
  {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_io_meta_legacy_life");
    auto  io  = lib.create_io("legacylife");
    io->add_input("a", 1);
    io->set_bits("a", 8);
    io->add_output("z", 1);
    io->set_bits("z", 8);
    auto g  = io->create_graph();
    auto op = create_typed_node(*g, Ntype_op::Or);
    set_bits(op.create_driver_pin(0), 8);
    g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
    op.create_driver_pin(0).connect_sink(g->get_output_pin("z"));
    design = scan_design(*g, {});
    cert   = build_certificate(design, {});
  }
  const LegacyEmitOptions options{};  // defaults: emit_cert, Skip WF
  std::ostringstream      out;
  emit_legacy_model(design, cert, options, out);
  EXPECT_NE(out.str().find("legacylife"), std::string::npos);
  EXPECT_FALSE(io_meta_json(build_io_meta(design, cert)).empty());
}

}  // namespace
}  // namespace lean_export