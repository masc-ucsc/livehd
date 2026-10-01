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
namespace lean_export {
namespace {
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
      {10, op, width, false, {{0, input(2000000000, 8)}, {1, input(2000000001, 8)}}}
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
  d.nodes.front().op        = ScanOp::LT;
  d.nodes.front().is_signed = true;
  c                         = build_certificate(d, {});
  EXPECT_NE(lower_fast_expr(c, c.nodes.front()).text.find("BitVec.toInt @0@"), std::string::npos);
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
  EXPECT_THROW(emit_legacy_model(d, c, o, out), std::runtime_error);
}
TEST(LegacyModel, PartialChunkExperimentMakesNoWholeGraphClaim) {
  auto d = binary(ScanOp::Or, 8);
  d.nodes.push_back({11, ScanOp::Or, 8, false, {{0, PinRef{PinKind::Node, 10, 0, 8, 0, {}}}}});
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
  const char*             destination = std::getenv("LEAN_LEGACY_OUTPUT");
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
    d.nodes[0].is_signed                = op == ScanOp::LT || op == ScanOp::GT;
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
  if (const char* destination = std::getenv("LEAN_LEGACY_OUTPUT")) {
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
}  // namespace
}  // namespace lean_export
