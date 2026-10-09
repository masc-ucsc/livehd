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
  const char* destination = std::getenv("LEAN_NARY_BRIDGE_OUTPUT");
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

TEST(LegacyModel, LargeRecordFixtures) {
  const char* destination = std::getenv("LEAN_LARGE_RECORD_OUTPUT");
  for (const uint32_t count : {64u, 65u, 255u, 256u, 534u}) {
    DesignScan d;
    d.name = "record_" + std::to_string(count);
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t width = i % 3 == 0 ? 1 : i % 3 == 1 ? 8 : 17;
      d.inputs.push_back({"x" + std::to_string(i), 2000000000 + i, width, {}});
      Flop f;
      f.id       = 1000 + i;
      f.width    = width;
      f.raw_name = "q" + std::to_string(i);
      f.din      = input(2000000000 + i, width);
      d.flops.push_back(f);
      d.outputs.push_back({
          "y" + std::to_string(i),
          i,
          width,
          PinRef{PinKind::Flop, f.id, 0, width, 0, {}}
      });
    }
    const auto        c = build_certificate(d, {});
    LegacyEmitOptions options;
    options.emit_cert        = count == 256;
    options.emit_fast_bridge = count == 256;
    options.cert_wf          = count == 256 ? LeanCertWFMode::Chunked : LeanCertWFMode::Skip;
    std::ostringstream out;
    ASSERT_NO_THROW(emit_legacy_model(d, c, options, out));
    EXPECT_EQ(out.str().find("_chunk0") != std::string::npos, count > 64);
    const auto names = legacy_names(d, {});
    for (const auto& f : d.flops) {
      EXPECT_NE(out.str().find("  " + names.flops.at(f.id) + " : BitVec " + std::to_string(f.width) + "\n"), std::string::npos);
    }
    if (!destination) {
      continue;
    }
    out << "\nnamespace " << d.name << "_Lgraph\n";
    // A flat record remains a valid logical type even when its constructor
    // cannot be executed. The two conversions prove that no fields or values
    // are lost by the new physical layout, for arbitrary states.
    for (const auto& role : {std::string("in"), std::string("out"), std::string("state")}) {
      std::vector<std::pair<std::string, uint32_t>> fields;
      for (uint32_t i = 0; i < count; ++i) {
        fields.emplace_back(role == "in"    ? names.inputs[i]
                            : role == "out" ? names.outputs[i]
                                            : names.flops.at(d.flops[i].id),
                            d.inputs[i].width);
      }
      const auto type = d.name + "_" + role;
      out << "structure Flat_" << role << " where\n";
      for (const auto& [field, width] : fields) {
        out << "  " << field << " : BitVec " << width << "\n";
      }
      for (const bool flatten : {true, false}) {
        out << "noncomputable def " << (flatten ? "toFlat_" : "fromFlat_") << role << " (s : " << (flatten ? type : "Flat_" + role)
            << ") : " << (flatten ? "Flat_" + role : type) << " := { ";
        for (size_t i = 0; i < fields.size(); ++i) {
          out << (i ? ", " : "") << fields[i].first << " := s." << fields[i].first;
        }
        out << " }\n";
      }
      out << "theorem flat_" << role << "_roundtrip (s : Flat_" << role << ") : toFlat_" << role << " (fromFlat_" << role
          << " s) = s := rfl\n";
      out << "theorem nested_" << role << "_roundtrip (s : " << type << ") : fromFlat_" << role << " (toFlat_" << role
          << " s) = s := rfl\n";
      out << "#print axioms flat_" << role << "_roundtrip\n#print axioms nested_" << role << "_roundtrip\n";
    }
    out << "theorem next_fields (i : " << d.name << "_in) (s : " << d.name << "_state) :\n  ";
    for (uint32_t i = 0; i < count; ++i) {
      out << (i ? " ∧\n  " : "") << "(" << d.name << "_next i s)." << names.flops.at(d.flops[i].id) << " = i." << names.inputs[i];
    }
    out << " := by\n  simp [" << d.name << "_next, flop_next, bv_zext]\n#print axioms next_fields\n";
    out << "example : (" << d.name << "_next default default)." << names.flops.at(d.flops.back().id)
        << " = 0 := by native_decide\nend " << d.name << "_Lgraph\n";
    const auto path = std::filesystem::path(destination) / (d.name + "_Lgraph.lean");
    write_atomic(path.string(), [&](std::ostream& stream) { stream << out.str(); });
  }
}

TEST(LegacyModel, LargeMemoryRecordFixture) {
  DesignScan d;
  d.name = "record_memory";
  for (uint32_t i = 0; i < 65; ++i) {
    Memory m;
    m.id         = 1000 + i;
    m.raw_name   = "m" + std::to_string(i);
    m.bits       = 8;
    m.addr_width = 1;
    m.size       = 2;
    d.memories.emplace(m.id, m);
  }
  const auto         names = legacy_names(d, {});
  std::ostringstream out;
  out << "import LeanSemanticPrimitives\n";
  emit_legacy_schema(d, names, out);
  EXPECT_NE(out.str().find("structure record_memory_state extends"), std::string::npos);
  EXPECT_NE(out.str().find("st_m64 : (BitVec 1 -> BitVec 8)"), std::string::npos);
  EXPECT_EQ(out.str().find("record_memory_state where"), std::string::npos);
  out << "theorem memory_default : (default : record_memory_state).st_m64 (0#1) = 0#8 := by native_decide\n";
  out << "theorem memory_update (s : record_memory_state) (f : BitVec 1 -> BitVec 8) :\n"
         "    ({ s with st_m64 := f }).st_m64 = f := rfl\n"
         "#print axioms memory_default\n#print axioms memory_update\n";
  if (const char* destination = std::getenv("LEAN_LARGE_RECORD_OUTPUT")) {
    const auto path = std::filesystem::path(destination) / "record_memory_Lgraph.lean";
    write_atomic(path.string(), [&](std::ostream& stream) { stream << out.str(); });
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
  EXPECT_THROW(emit_legacy_model(d, c, o, out), std::runtime_error);
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
