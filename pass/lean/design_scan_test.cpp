// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_scan.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>

#include "certificate_ir.hpp"
#include "emit_design_cert.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"

namespace lean_export {
namespace {
using livehd::graph_util::create_typed_node;
using livehd::graph_util::set_bits;
TEST(DesignScan, OwnsPinsAndPreservesPortDeclarationRoots) {
  DesignScan design;
  {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_owned");
    auto  io  = lib.create_io("owned");
    io->add_input("b", 1);
    io->set_bits("b", 8);
    io->add_input("a", 2);
    io->set_bits("a", 8);
    io->add_output("z", 1);
    io->set_bits("z", 8);
    io->add_output("y", 2);
    io->set_bits("y", 8);
    auto g      = io->create_graph();
    auto first  = create_typed_node(*g, Ntype_op::And);
    auto second = create_typed_node(*g, Ntype_op::Or);
    set_bits(first.create_driver_pin(0), 8);
    set_bits(second.create_driver_pin(0), 8);
    g->get_input_pin("a").connect_sink(first.create_sink_pin(0));
    g->get_input_pin("b").connect_sink(first.create_sink_pin(1));
    first.create_driver_pin(0).connect_sink(g->get_output_pin("z"));
    first.create_driver_pin(0).connect_sink(second.create_sink_pin(0));
    second.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
    design = scan_design(*g, {});
    EXPECT_EQ(design.inputs[0].name, "a");
    EXPECT_EQ(design.inputs[0].id, 2000000001);
    EXPECT_EQ(design.outputs[0].name, "y");
    EXPECT_EQ(design.roots[0].id, first.get_debug_nid());
    EXPECT_EQ(design.nodes[0].op, ScanOp::And);
    // Mutating the graph after scanning must not affect certificate building.
    set_bits(first.create_driver_pin(0), 16);
  }
  const auto cert = build_certificate(design, {});
  ASSERT_EQ(cert.nodes.size(), 2);
  EXPECT_EQ(cert.nodes[0].width, 8);
  std::ostringstream text;
  emit_design_cert(design, cert, text);
  EXPECT_NE(text.str().find("owned_designCert"), std::string::npos);
}
TEST(DesignScan, BankedSumPreservesEveryOperandIncludingDuplicates) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_banked_sum");
  auto  io  = lib.create_io("banked_sum");
  io->add_output("y", 1);
  io->set_bits("y", 8);
  auto g   = io->create_graph();
  auto sum = create_typed_node(*g, Ntype_op::Sum);
  set_bits(sum.create_driver_pin(0), 8);
  // Non-dense, deliberately interleaved slots: 10 + 10 + 30 - 7 - 2 = 41.
  // Both tens share a constant-pool pin but must remain two dependencies.
  for (const auto& [port, value] : std::vector<std::pair<int, int>>{
           {4, 30},
           {3,  2},
           {0, 10},
           {7,  7},
           {2, 10}
  }) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(value)).connect_sink(sum.create_sink_pin(port));
  }
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(cert.nodes.size(), 1);
  const auto& n = cert.nodes[0];
  EXPECT_EQ(n.op.kind, Operation::Sum);
  ASSERT_EQ(n.op.parameter, 3);
  ASSERT_EQ(n.deps.size(), 5);
  int result = 0;
  for (size_t i = 0; i < n.deps.size(); ++i) {
    const auto it
        = std::find_if(cert.sources.begin(), cert.sources.end(), [&](const Source& source) { return source.id == n.deps[i]; });
    ASSERT_NE(it, cert.sources.end());
    result += (i < n.op.parameter ? 1 : -1) * std::stoi(it->const_int);
  }
  EXPECT_EQ(result, 41);
  if (const char* path = std::getenv("LEAN_BANKED_SUM_FIXTURE")) {
    std::ofstream out(path);
    emit_design_cert(design, cert, out);
    out << R"(
example : bv_uint ((banked_sum_step #[] ⟨#[], #[]⟩).outputs[0]!) = 41 := by native_decide
)";
    ASSERT_TRUE(out.good());
  }
}
// Master's exporter refuses a malformed arity of these four by name
// (pass_lean.cpp: "Div/LT-GT/SHL/SRA node n_… is not binary"). The shared scan
// boundary carries that refusal now, so both the legacy emitters and the
// verified-compiler exporter inherit it.
//
// This is not hypothetical for the comparisons: LT and GT are two-banked, so
// upstream can represent a folded comparison, while Op_ULT/SLT/UGT/SGT take
// exactly two dependencies. Without the refusal a three-operand LT emits a
// three-dependency certificate node whose extra operand the model cannot mean,
// and nothing reports it.
TEST(DesignScan, MalformedBinaryArityIsRefused) {
  int fixture = 0;
  for (const auto op : {Ntype_op::Div, Ntype_op::LT, Ntype_op::GT, Ntype_op::SHL, Ntype_op::SRA}) {
    for (const int operands : {2, 3}) {
      auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_arity_" + std::to_string(fixture++));
      auto  io  = lib.create_io("arity");
      io->add_output("y", 1);
      io->set_bits("y", 8);
      auto g    = io->create_graph();
      auto node = create_typed_node(*g, op);
      set_bits(node.create_driver_pin(0), 8);
      for (int slot = 0; slot < operands; ++slot) {
        livehd::graph_util::create_const(*g, *Dlop::create_integer(slot + 1)).connect_sink(node.create_sink_pin(slot));
      }
      node.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
      if (operands == 2) {
        EXPECT_NO_THROW(scan_design(*g, {})) << Ntype::get_name(op) << " must still accept its binary form";
      } else {
        EXPECT_THROW(scan_design(*g, {}), std::runtime_error) << Ntype::get_name(op) << " must refuse a non-binary arity";
      }
    }
  }
}

// The refusal must name EVERY unsupported operator in the design, not the one
// the walk reached first. Sizing the remaining port work from a first-refusal
// diagnostic is what makes "lowering operator X unblocks N designs" unknowable:
// a second unsupported operator behind the first is never reached, so it is
// never counted.
TEST(DesignScan, UnsupportedOperatorRefusalNamesTheWholeCensus) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_census");
  auto  io  = lib.create_io("census");
  io->add_output("y", 1);
  io->set_bits("y", 8);
  auto g   = io->create_graph();
  auto rem = create_typed_node(*g, Ntype_op::Rem);
  auto pop = create_typed_node(*g, Ntype_op::Popcount);
  set_bits(rem.create_driver_pin(0), 8);
  set_bits(pop.create_driver_pin(0), 8);
  for (int slot = 0; slot < 2; ++slot) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(slot + 3)).connect_sink(rem.create_sink_pin(slot));
    livehd::graph_util::create_const(*g, *Dlop::create_integer(slot + 5)).connect_sink(pop.create_sink_pin(slot));
  }
  auto combine = create_typed_node(*g, Ntype_op::Or);
  set_bits(combine.create_driver_pin(0), 8);
  rem.create_driver_pin(0).connect_sink(combine.create_sink_pin(0));
  pop.create_driver_pin(0).connect_sink(combine.create_sink_pin(1));
  combine.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  try {
    scan_design(*g, {});
    FAIL() << "a design with two unsupported operators must be refused";
  } catch (const std::runtime_error& e) {
    const std::string msg = e.what();
    EXPECT_NE(msg.find(Ntype::get_name(Ntype_op::Rem)), std::string::npos) << msg;
    EXPECT_NE(msg.find(Ntype::get_name(Ntype_op::Popcount)), std::string::npos) << msg;
  }
}

namespace {
// Build `arms` (control, value) pairs at dense ascending pids, plus an optional
// trailing default. `hotmux_inputs` asserts that density, so the fixture must
// drive every pid from 0.
hhds::Node_class build_hotmux(hhds::Graph& g, const std::vector<std::pair<int, int>>& arms, std::optional<int> fallback,
                              uint32_t width) {
  auto node = create_typed_node(g, Ntype_op::Hotmux);
  set_bits(node.create_driver_pin(0), width);
  int pid = 0;
  for (const auto& [control, value] : arms) {
    livehd::graph_util::create_const(g, *Dlop::create_integer(control)).connect_sink(node.create_sink_pin(pid++));
    livehd::graph_util::create_const(g, *Dlop::create_integer(value)).connect_sink(node.create_sink_pin(pid++));
  }
  if (fallback) {
    livehd::graph_util::create_const(g, *Dlop::create_integer(*fallback)).connect_sink(node.create_sink_pin(pid));
  }
  return node;
}
}  // namespace

// Hotmux lowers to the existing Mux vocabulary: one Ror predicate per arm and a
// first-active-priority chain, with the OUTERMOST alternative keeping the
// original graph id so existing consumers resolve without a remap.
//
// Control 2 is deliberately multi-bit with bit 0 CLEAR: a predicate that tested
// bit 0 instead of the reduce-OR would select the wrong arm, and no structural
// check would notice.
TEST(DesignScan, HotmuxLowersToPriorityMuxChain) {
  for (const bool explicit_default : {false, true}) {
    auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_scan_hotmux_") + (explicit_default ? "def" : "zero"));
    auto  io  = lib.create_io("hotmux");
    io->add_output("y", 1);
    io->set_bits("y", 8);
    auto       g    = io->create_graph();
    const auto node = build_hotmux(*g,
                                   {
                                       {0, 11},
                                       {2, 22},
                                       {0, 33}
    },
                                   explicit_default ? std::optional<int>(44) : std::nullopt,
                                   8);
    node.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
    const auto design = scan_design(*g, {});

    size_t muxes = 0, rors = 0;
    for (const auto& n : design.nodes) {
      muxes += n.op == ScanOp::Mux;
      rors += n.op == ScanOp::Ror;
    }
    EXPECT_EQ(rors, 3u) << "one nonzero predicate per arm";
    EXPECT_EQ(muxes, 3u) << "one alternative per arm";

    const auto outermost
        = std::find_if(design.nodes.begin(), design.nodes.end(), [&](const DesignNode& n) { return n.id == node.get_debug_nid(); });
    ASSERT_NE(outermost, design.nodes.end()) << "the lowered chain must keep the Hotmux's own id";
    EXPECT_EQ(outermost->op, ScanOp::Mux);
    ASSERT_EQ(outermost->operands.size(), 3u) << "{selector, false value, true value}";
    // Dependency-ordered: every operand of the outermost node is already built.
    for (const auto& operand : outermost->operands) {
      if (operand.driver.kind == PinKind::Node) {
        EXPECT_NE(std::find_if(design.nodes.begin(),
                               outermost,
                               [&](const DesignNode& n) { return n.id == operand.driver.id; }),
                  outermost)
            << "operand " << operand.driver.id << " must precede the node that uses it";
      }
    }
    // The predicate must see EVERY bit of its control. Op_Ror's result width is
    // pinned to 1, and a constant dependency is recorded at the width the arm
    // requests, so feeding a constant control straight into the reduce-OR would
    // truncate it to one bit -- control 2 would read as 0 and that arm could
    // never fire. A structural check alone would not notice, which is why this
    // asserts the recorded SOURCE WIDTH rather than the shape.
    const auto cert     = build_certificate(design, {});
    size_t     ror_seen = 0;
    for (const auto& n : cert.nodes) {
      ror_seen += n.op.kind == Operation::Ror;
    }
    EXPECT_EQ(ror_seen, 3u) << "every arm must still carry its own predicate";

    // The multi-bit control is the constant 2. Op_Ror's result width is pinned
    // to 1 and a CONSTANT dependency is recorded at the width the consuming arm
    // requests, so feeding it straight into the reduce-OR would store it at one
    // bit: 2 would read as 0 and that arm could never fire. Assert on the
    // recorded source width, because the lowered SHAPE is identical either way.
    const auto control = std::find_if(cert.sources.begin(), cert.sources.end(), [](const Source& s) {
      return s.kind == SourceKind::Const && s.const_int == "2";
    });
    ASSERT_NE(control, cert.sources.end()) << "the multi-bit control constant must survive into the certificate";
    EXPECT_GE(control->width, 2u) << "control 2 was stored at " << control->width << " bit(s), so its reduce-OR reads 0";
  }
}

TEST(DesignScan, HotmuxWithoutArmsIsRefused) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_hotmux_empty");
  auto  io  = lib.create_io("hotmux_empty");
  io->add_output("y", 1);
  io->set_bits("y", 8);
  auto g    = io->create_graph();
  auto node = create_typed_node(*g, Ntype_op::Hotmux);
  set_bits(node.create_driver_pin(0), 8);
  // A lone pid 0 is a trailing default with no arm, which is not a multiplexor.
  livehd::graph_util::create_const(*g, *Dlop::create_integer(7)).connect_sink(node.create_sink_pin(0));
  node.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  EXPECT_THROW(scan_design(*g, {}), std::runtime_error);
}

TEST(DesignScan, ConstantPoolPreservesUnsizedValuesAndSignedWidths) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_const_pool");
  auto  io  = lib.create_io("constant_pool");
  io->add_output("positive", 1);
  io->set_bits("positive", 32);
  io->add_output("negative", 2);
  io->set_bits("negative", 8);
  auto g = io->create_graph();
  livehd::graph_util::create_const(*g, *Dlop::create_integer(0x6000000)).connect_sink(g->get_output_pin("positive"));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(-64)).connect_sink(g->get_output_pin("negative"));
  const auto design = scan_design(*g, {});
  ASSERT_EQ(design.outputs.size(), 2);
  ASSERT_TRUE(design.outputs[0].driver.has_value());
  ASSERT_TRUE(design.outputs[1].driver.has_value());
  EXPECT_EQ(design.outputs[0].driver->value, "-64");
  EXPECT_EQ(design.outputs[0].driver->intrinsic_width, 7);
  EXPECT_EQ(design.outputs[1].driver->value, "100663296");
  EXPECT_EQ(design.outputs[1].driver->intrinsic_width, 27);
  const auto cert = build_certificate(design, {});
  ASSERT_EQ(cert.sources.size(), 2);
  EXPECT_EQ(cert.sources[0].const_int, "-64");
  EXPECT_EQ(cert.sources[0].width, 8);
  EXPECT_EQ(cert.sources[1].const_int, "100663296");
  EXPECT_EQ(cert.sources[1].width, 32);
}
TEST(DesignScan, EndpointMasksPreserveExtractionAndReplacement) {
  using namespace livehd::graph_util;
  // Include a window crossing bit 64 and a window above the source width.
  for (const auto& [lo, hi, width] : std::vector<std::tuple<int, int, int>>{
           { 0,  8,  8},
           { 3,  6,  8},
           { 7,  8,  8},
           { 8, 12,  8},
           {63, 67, 80}
  }) {
    for (const auto op : {Ntype_op::Get_mask, Ntype_op::Set_mask}) {
      const bool get  = op == Ntype_op::Get_mask;
      const auto name = std::string(get ? "get_window_" : "set_window_") + std::to_string(lo) + "_" + std::to_string(hi);
      auto&      lib  = livehd::Hhds_graph_library::instance("lgdb_scan_" + name);
      auto       io   = lib.create_io(name);
      io->add_input("a", 1);
      io->set_bits("a", width);
      io->add_input("v", 2);
      io->set_bits("v", width);
      io->add_output("y", 1);
      io->set_bits("y", get ? hi - lo : width);
      auto g = io->create_graph();
      for (const auto& decl : io->get_input_pin_decls()) {
        set_bits(g->get_input_pin(decl.name), bits_of(g->get_input_pin(decl.name), *io, decl.name));
      }
      auto node = create_typed_node(*g, op);
      connect_mask_operands(node, g->get_input_pin("a"), lo, hi, get ? hhds::Pin_class{} : g->get_input_pin("v"));
      set_bits(node.create_driver_pin(0), get ? hi - lo : width);
      node.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
      const auto design = scan_design(*g, {});
      ASSERT_EQ(design.nodes.size(), 1);
      const auto& operands = design.nodes.front().operands;
      ASSERT_EQ(operands.size(), get ? 2 : 3);
      EXPECT_EQ(operands[1].driver.kind, PinKind::Constant);
      EXPECT_EQ(operands[1].driver.width, hi == 67 ? 68 : hi);
      EXPECT_EQ(operands[1].driver.value, mask_window_const(lo, hi).to_decimal_string());
      const auto cert = build_certificate(design, {});
      if (const char* dir = std::getenv("LEAN_MASK_ORACLES")) {
        std::ofstream      out(std::string(dir) + "/" + name + ".lean");
        std::ostringstream emitted;
        emit_design_cert(design, cert, emitted);
        // SetMask remains an intentional verified-compiler refusal. Exercise
        // the certificate interpreter without claiming a compiled proof.
        const auto text = emitted.str();
        out << text.substr(0, text.find("/-- Compile-and-run."));
        // Use ordinary Nat arithmetic as the oracle, independently of both
        // the legacy emitter and the mask primitives used by DesignCert.
        out << "\nopen Compiler\n";
        out << "def inputs (a v : Nat) := #[mk_bv " << width << " (Int.ofNat a), mk_bv " << width << " (Int.ofNat v)]\n";
        out << "def expected (a v : Nat) : Nat := ";
        if (get) {
          out << "((a % 2^" << width << ") / 2^" << lo << ") % 2^" << hi - lo;
        } else {
          out << "((a % 2^" << lo << ") + ((v % 2^" << hi - lo << ") * 2^" << lo << ") + ((a / 2^" << hi << ") * 2^" << hi
              << ")) % 2^" << width;
        }
        out << "\nexample : ([0, 1, 15, 127, 128, 171, 255, 2^64 + 2^63 + 3] : List Nat).all (fun a => "
               "([0, 1, 3, 15, 255] : List Nat).all (fun v => "
               "bv_uint ((interpretDesign "
            << name
            << "_designCert (inputs a v) ⟨#[], #[]⟩).outputs[0]!) == Int.ofNat (expected a v))) = true := by native_decide\n";
        ASSERT_TRUE(out.good());
      }
    }
  }
}
TEST(DesignScan, InvalidMaskEndpointsAreRefused) {
  using namespace livehd::graph_util;
  for (const auto& [lo, hi] : std::vector<std::pair<int, int>>{
           {-1,    3},
           { 3,    3},
           { 4,    3},
           { 0, 1025}
  }) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_bad_range_" + std::to_string(lo) + "_" + std::to_string(hi));
    auto  io  = lib.create_io("bad_range");
    io->add_output("y", 1);
    io->set_bits("y", 8);
    auto       g     = io->create_graph();
    auto       node  = create_typed_node(*g, Ntype_op::Get_mask);
    const auto drive = [&](std::string_view name, int value) {
      create_const(*g, *Dlop::create_integer(value)).connect_sink(setup_sink_by_name(node, name));
    };
    drive("a", 171);
    drive("lo", lo);
    drive("hi", hi);
    set_bits(node.create_driver_pin(0), 8);
    node.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
    EXPECT_THROW(scan_design(*g, {}), std::runtime_error);
  }
}
TEST(DesignScan, ConcatLowersDeclaredWindowsWithoutMutatingGraph) {
  using namespace livehd::graph_util;
  struct Case {
    const char* name;
    int         a_bits, a_window;
    bool        signed_a;
    int         constant, constant_window, b_bits, b_window;
  };
  for (const auto& c : std::vector<Case>{
           {          "padding", 3,  8, false,   4, 8, 4, 4},
           {           "signed", 3,  8,  true,  -1, 3, 4, 4},
           {      "wide_signed", 8, 72,  true, -64, 7, 1, 1},
           {    "wide_unsigned", 8, 72, false,   4, 4, 4, 4},
           {"negative_constant", 1,  1, false,  -1, 8, 3, 3}
  }) {
    SCOPED_TRACE(c.name);
    const auto name         = std::string("concat_") + c.name;
    const auto library_name = "lgdb_scan_" + name;
    auto&      lib          = livehd::Hhds_graph_library::instance(library_name);
    auto       io           = lib.create_io(name);
    io->add_input("a", 1);
    io->set_bits("a", c.a_bits);
    io->add_input("b", 2);
    io->set_bits("b", c.b_bits);
    const auto width = c.a_window + c.constant_window + c.b_window;
    io->add_output("y", 1);
    io->set_bits("y", width);
    auto       g = io->create_graph();
    const auto a = g->get_input_pin("a");
    const auto b = g->get_input_pin("b");
    set_bits(a, c.a_bits);
    set_bits(b, c.b_bits);
    if (c.signed_a) {
      set_sign(a);
    } else {
      set_unsign(a);
    }
    set_unsign(b);
    auto       concat = create_typed_node(*g, Ntype_op::Concat);
    const auto drive_const
        = [&](int port, int value) { create_const(*g, *Dlop::create_integer(value)).connect_sink(concat.create_sink_pin(port)); };
    a.connect_sink(concat.create_sink_pin(0));
    drive_const(1, c.a_window);
    drive_const(2, c.constant);
    drive_const(3, c.constant_window);
    b.connect_sink(concat.create_sink_pin(4));
    drive_const(5, c.b_window);
    set_bits(concat.create_driver_pin(0), width);
    concat.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
    size_t before = 0;
    for ([[maybe_unused]] const auto n : g->body().nodes()) {
      ++before;
    }
    const auto design = scan_design(*g, {});
    const auto cert   = build_certificate(design, {});
    ASSERT_FALSE(design.nodes.empty());
    EXPECT_EQ(design.nodes.back().id, concat.get_debug_nid());
    EXPECT_EQ(design.nodes.back().op, ScanOp::Or);
    std::set<uint32_t> ids;
    for (const auto& n : design.nodes) {
      EXPECT_TRUE(n.op == ScanOp::Or || n.op == ScanOp::SHL || n.op == ScanOp::Sext);
      EXPECT_TRUE(ids.insert(n.id).second);
    }
    size_t after = 0;
    for ([[maybe_unused]] const auto n : g->body().nodes()) {
      ++after;
    }
    EXPECT_EQ(before, after);
    EXPECT_EQ(type_op_of(concat), Ntype_op::Concat);
    std::ostringstream first, second;
    emit_design_cert(design, cert, first);
    const auto again = scan_design(*g, {});
    emit_design_cert(again, build_certificate(again, {}), second);
    EXPECT_EQ(first.str(), second.str());
    if (const char* dir = std::getenv("LEAN_CONCAT_ORACLES")) {
      std::ofstream out(std::string(dir) + "/" + name + ".lean");
      out << first.str();
      out << "\ndef laneA (a : Nat) : Int :=\n  let v : Int := Int.ofNat (a % 2^" << c.a_bits << ")\n  (";
      if (c.signed_a) {
        out << "if v < 2^" << c.a_bits - 1 << " then v else v - 2^" << c.a_bits;
      } else {
        out << "v";
      }
      out << ") % 2^" << c.a_window << "\n";
      out << "def expected (a b : Nat) : Int := (laneA a * 2^" << c.constant_window + c.b_window << " + ((" << c.constant
          << " : Int) % 2^" << c.constant_window << ") * 2^" << c.b_window << " + Int.ofNat (b % 2^" << c.b_bits << ")) % 2^"
          << width << "\n";
      // Exhaust every input bit pattern, including negative signed lanes.
      // Wide outputs test sign extension and offsets across bit 64.
      out << "example : (List.range (2^" << c.a_bits << ")).all (fun a => (List.range (2^" << c.b_bits
          << ")).all (fun b => bv_uint ((interpretDesign " << name << "_designCert #[mk_bv " << c.a_bits << " (Int.ofNat a), mk_bv "
          << c.b_bits << " (Int.ofNat b)] ⟨#[], #[]⟩).outputs[0]!) == expected a b)) = true := by native_decide\n";
      ASSERT_TRUE(out.good());
      std::filesystem::create_directories(library_name);
      livehd::Hhds_graph_library::save(library_name);
    }
  }
}
TEST(DesignScan, MalformedConcatIsRefused) {
  using namespace livehd::graph_util;
  for (int kind = 0; kind != 6; ++kind) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_bad_concat_" + std::to_string(kind));
    auto  io  = lib.create_io("bad_concat");
    io->add_input("a", 1);
    io->set_bits("a", 8);
    io->add_output("y", 1);
    io->set_bits("y", kind == 4 ? 4 : 8);
    auto g = io->create_graph();
    set_bits(g->get_input_pin("a"), 8);
    auto concat = create_typed_node(*g, Ntype_op::Concat);
    g->get_input_pin("a").connect_sink(concat.create_sink_pin(0));
    if (kind == 0) {
      g->get_input_pin("a").connect_sink(concat.create_sink_pin(1));
    } else if (kind != 1) {
      const auto lane_width = kind == 2 ? 0 : kind == 3 ? -1 : kind == 4 ? 4 : 7;
      create_const(*g, *Dlop::create_integer(lane_width)).connect_sink(concat.create_sink_pin(1));
    }
    set_bits(concat.create_driver_pin(0), kind == 4 ? 4 : 8);
    concat.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
    EXPECT_THROW(scan_design(*g, {}), std::runtime_error);
  }
}

TEST(DesignScan, SynchronousRomRetainsContentsAndEnable) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_rom");
  auto  io  = lib.create_io("sync_rom");
  io->add_input("address", 1);
  io->set_bits("address", 2);
  io->add_input("enable", 2);
  io->set_bits("enable", 1);
  io->add_output("data", 1);
  io->set_bits("data", 8);
  auto g      = io->create_graph();
  auto memory = create_typed_node(*g, Ntype_op::Memory);
  set_bits(memory.create_driver_pin(0), 8);
  auto policy = [&](std::string_view name, int64_t value) {
    auto pin = livehd::graph_util::create_const(*g, *Dlop::create_integer(value));
    pin.connect_sink(memory.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name)));
  };
  policy("bits", 8);
  policy("size", 3);
  policy("type", 1);
  policy("rdport", 1);
  policy("initial", 0x030201);
  g->get_input_pin("address").connect_sink(memory.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "addr")));
  g->get_input_pin("enable").connect_sink(memory.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "enable")));
  memory.create_driver_pin(0).connect_sink(g->get_output_pin("data"));
  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(cert.flops.size(), 1);
  EXPECT_TRUE(cert.memories.empty());
  EXPECT_EQ(cert.sources[0].rom_contents, (std::vector<std::string>{"1", "2", "3"}));
  EXPECT_EQ(cert.nodes.back().op.kind, Operation::MuxBool);
}
TEST(DesignScan, ActiveLowAsyncResetSurvivesResizingAndControlsNextState) {
  for (const bool endpoint_resize : {false, true}) {
    auto& lib = livehd::Hhds_graph_library::instance(endpoint_resize ? "lgdb_scan_reset_endpoint" : "lgdb_scan_reset");
    auto  io  = lib.create_io("reset_fixture");
    io->add_input("clk", 1);
    io->set_bits("clk", 1);
    io->add_input("enable", 2);
    io->set_bits("enable", 1);
    io->add_input("rst", 3);
    io->set_bits("rst", 1);
    io->add_output("q", 1);
    io->set_bits("q", 8);
    auto g    = io->create_graph();
    auto flop = create_typed_node(*g, Ntype_op::Flop);
    set_bits(flop.create_driver_pin(0), 8);
    auto resize = create_typed_node(*g, endpoint_resize ? Ntype_op::Get_mask : Ntype_op::Or);
    set_bits(resize.create_driver_pin(0), 1);
    if (endpoint_resize) {
      set_bits(g->get_input_pin("rst"), 1);
      livehd::graph_util::connect_mask_operands(resize, g->get_input_pin("rst"), 0, 1);
    } else {
      g->get_input_pin("rst").connect_sink(resize.create_sink_pin(0));
    }
    resize.create_driver_pin(0).connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "reset_pin")));
    g->get_input_pin("enable").connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "enable")));
    g->get_input_pin("clk").connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin")));
    auto drive = [&](std::string_view name, int64_t value) {
      livehd::graph_util::create_const(*g, *Dlop::create_integer(value))
          .connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, name)));
    };
    drive("din", 12);
    drive("initial", 7);
    drive("async", 1);
    drive("negreset", 1);
    flop.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
    const auto design = scan_design(*g, {});
    ASSERT_EQ(design.flops.size(), 1);
    EXPECT_TRUE(design.flops[0].active_low);
    EXPECT_EQ(design.flops[0].reset_input, design.inputs[2].id);
    const auto cert = build_certificate(design, {});
    ASSERT_EQ(cert.flops.size(), 1);
    EXPECT_TRUE(cert.flops[0].reset_active_low);
    EXPECT_TRUE(cert.sources[0].async_reset);
    EXPECT_TRUE(cert.sources[0].reset_active_low);
    // Optional end-to-end Lean oracle, always written under the caller's project
    // runtime directory. Run this test with LEAN_RESET_FIXTURE=<path>.lean, then
    // elaborate that file with the compiler library on LEAN_PATH.
    if (const char* path = std::getenv("LEAN_RESET_FIXTURE")) {
      std::ofstream out(path);
      emit_design_cert(design, cert, out);
      out << R"(
def reset_fixture_initial : RuntimeState := ⟨#[mk_bv 8 5], #[]⟩
-- rst=0 asserts reset, even with enable=0: both Q and the next state are 7.
example : bv_uint ((reset_fixture_step #[mk_bv 1 0, mk_bv 1 0, mk_bv 1 0]
    reset_fixture_initial).outputs[0]!) = 7 := by native_decide
example : bv_uint ((reset_fixture_step #[mk_bv 1 0, mk_bv 1 0, mk_bv 1 0]
    reset_fixture_initial).nextState.flops[0]!) = 7 := by native_decide
-- rst=1 releases reset. Disabled state holds 5; enabled state takes din=12.
example : bv_uint ((reset_fixture_step #[mk_bv 1 0, mk_bv 1 0, mk_bv 1 1]
    reset_fixture_initial).nextState.flops[0]!) = 5 := by native_decide
example : bv_uint ((reset_fixture_step #[mk_bv 1 0, mk_bv 1 1, mk_bv 1 1]
    reset_fixture_initial).nextState.flops[0]!) = 12 := by native_decide
)";
      ASSERT_TRUE(out.good());
    }
  }
}

TEST(DesignScan, RejectsCombinationalCycleAndZeroWidth) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_cycle");
  auto  io  = lib.create_io("cycle");
  io->add_output("y", 1);
  io->set_bits("y", 1);
  auto g = io->create_graph();
  auto a = create_typed_node(*g, Ntype_op::Not);
  auto b = create_typed_node(*g, Ntype_op::Not);
  set_bits(a.create_driver_pin(0), 1);
  set_bits(b.create_driver_pin(0), 1);
  a.create_driver_pin(0).connect_sink(b.create_sink_pin(0));
  b.create_driver_pin(0).connect_sink(a.create_sink_pin(0));
  a.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  EXPECT_THROW(scan_design(*g, {}), std::runtime_error);
  auto empty_io = lib.create_io("zero");
  empty_io->add_input("x", 0);
  auto empty = empty_io->create_graph();
  EXPECT_THROW(scan_design(*empty, {}), std::runtime_error);
}
}  // namespace
}  // namespace lean_export
