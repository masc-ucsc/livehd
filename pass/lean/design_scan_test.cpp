// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_scan.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <tuple>

#include "certificate_ir.hpp"
#include "design_io_meta.hpp"
#include "emit_dcert.hpp"
#include "emit_design_cert.hpp"
#include "lean_format.hpp"
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
  if (const char* path = std::getenv("LHD_LEAN_BANKED_SUM_FIXTURE")) {
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

// Value-level oracle for the Hotmux lowering.
//
// The structural test above cannot see a width or priority error: the lowered
// SHAPE is identical whether or not a control is truncated, and it was in fact
// identical through two real defects. This emits a DesignCert whose controls are
// INPUTS, plus independent `native_decide` facts over concrete input vectors, so
// the lowering is checked by evaluating it rather than by inspecting it.
//
// Cases: no control active (falls to the default), one active, a MULTI-BIT
// control whose bit 0 is clear (the counterexample a bit-0 predicate fails),
// and OVERLAPPING controls (one-hot violated, first-active must win).
//
// Set LHD_LEAN_HOTMUX_FIXTURE to a path; pass/lean/scripts/hotmux_oracle.sh runs it.
TEST(DesignScan, HotmuxValueOracle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_hotmux_oracle");
  auto  io  = lib.create_io("hotmux_oracle");
  for (int k = 0; k < 3; ++k) {
    const auto name = "c" + std::to_string(k);
    io->add_input(name, k + 1);
    io->set_bits(name, 2);
  }
  io->add_output("y", 1);
  io->set_bits("y", 8);
  auto g    = io->create_graph();
  auto node = create_typed_node(*g, Ntype_op::Hotmux);
  set_bits(node.create_driver_pin(0), 8);
  const std::vector<int> arm_value{11, 22, 33};
  for (int k = 0; k < 3; ++k) {
    g->get_input_pin("c" + std::to_string(k)).connect_sink(node.create_sink_pin(2 * k));
    livehd::graph_util::create_const(*g, *Dlop::create_integer(arm_value[k])).connect_sink(node.create_sink_pin(2 * k + 1));
  }
  livehd::graph_util::create_const(*g, *Dlop::create_integer(44)).connect_sink(node.create_sink_pin(6));
  node.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(design.inputs.size(), 3u);

  const char* path = std::getenv("LHD_LEAN_HOTMUX_FIXTURE");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_LEAN_HOTMUX_FIXTURE to emit the Lean oracle";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);
  // {control values by NAME} -> expected output. Ordinals are the index in
  // design.inputs (certificate_ir.cpp:14), so the vector is built from the
  // scanned order rather than from the declaration order.
  const std::vector<std::pair<std::vector<int>, int>> cases{
      {{0, 0, 0}, 44},  // nothing active -> the trailing default
      {{0, 2, 0}, 22},  // multi-bit control, bit 0 CLEAR -> arm 1 still fires
      {{1, 1, 0}, 11},  // one-hot VIOLATED -> first active arm wins
      {{0, 0, 3}, 33},  // last arm, multi-bit control
      {{2, 0, 0}, 11},  // multi-bit control on the first arm
  };
  for (const auto& [by_index, expect] : cases) {
    std::ostringstream args;
    for (size_t ordinal = 0; ordinal < design.inputs.size(); ++ordinal) {
      const auto which = design.inputs[ordinal].name.back() - '0';
      args << (ordinal ? ", " : "") << "mk_bv 2 " << by_index[static_cast<size_t>(which)];
    }
    out << "\nexample : bv_uint ((hotmux_oracle_step #[" << args.str() << "] ⟨#[], #[]⟩).outputs[0]!) = " << expect
        << " := by native_decide\n";
  }
  ASSERT_TRUE(out.good());
}

// Variable controls and arms exercise signed extension independently of
// priority selection. The second output has no explicit default.
TEST(DesignScan, HotmuxSignedValueOracle) {
  using namespace livehd::graph_util;
  auto&                                               lib = livehd::Hhds_graph_library::instance("lgdb_scan_hotmux_signed_oracle");
  auto                                                io  = lib.create_io("hotmux_signed_oracle");
  const std::vector<std::pair<std::string, uint32_t>> inputs{
      {      "c0", 2},
      {      "c1", 2},
      {      "c2", 2},
      {       "a", 4},
      {       "b", 4},
      {       "v", 5},
      {"fallback", 3}
  };
  for (size_t i = 0; i < inputs.size(); ++i) {
    io->add_input(inputs[i].first, i + 1);
    io->set_bits(inputs[i].first, inputs[i].second);
  }
  io->add_output("explicit_default", 1);
  io->set_bits("explicit_default", 8);
  io->add_output("implicit_zero", 2);
  io->set_bits("implicit_zero", 8);
  auto g = io->create_graph();
  for (const auto& [name, width] : inputs) {
    auto pin = g->get_input_pin(name);
    set_bits(pin, width);
    if (name == "a" || name == "v" || name == "fallback") {
      set_sign(pin);
    } else {
      set_unsign(pin);
    }
  }
  for (const bool fallback : {true, false}) {
    auto node = create_typed_node(*g, Ntype_op::Hotmux);
    set_bits(node.create_driver_pin(0), 8);
    const std::vector<std::string> values{"a", "b", "v"};
    for (size_t i = 0; i < values.size(); ++i) {
      g->get_input_pin("c" + std::to_string(i)).connect_sink(node.create_sink_pin(2 * i));
      g->get_input_pin(values[i]).connect_sink(node.create_sink_pin(2 * i + 1));
    }
    if (fallback) {
      g->get_input_pin("fallback").connect_sink(node.create_sink_pin(6));
    }
    node.create_driver_pin(0).connect_sink(g->get_output_pin(fallback ? "explicit_default" : "implicit_zero"));
  }
  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(design.inputs.size(), inputs.size());
  ASSERT_EQ(design.outputs.size(), 2u);
  const char* path = std::getenv("LHD_LEAN_HOTMUX_SIGNED_FIXTURE");
  if (!path) {
    GTEST_SKIP() << "set LHD_LEAN_HOTMUX_SIGNED_FIXTURE to emit the Lean value checks";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);
  struct Case {
    std::vector<int> values;  // c0,c1,c2,a,b,v,fallback; bit patterns, not signed integers
    int              explicit_value;
    int              implicit_value;
  };
  const std::vector<Case> cases{
      {{0, 0, 0, 12, 12, 16, 6}, 254,   0}, // signed trailing default -2, or implicit zero
      {{0, 2, 0, 12, 12, 16, 6},  12,  12}, // unsigned four-bit arm stays +12
      {{2, 0, 0, 12, 12, 16, 6}, 252, 252}, // signed four-bit arm -4 becomes eight-bit 252
      { {1, 1, 0, 8, 15, 16, 6}, 248, 248}, // first active wins with signed minimum -8
      {{0, 0, 3, 12, 12, 16, 6}, 240, 240}, // signed five-bit minimum -16
      {{0, 0, 2, 12, 12, 15, 6},  15,  15}, // positive signed arm
      { {1, 0, 0, 7, 12, 16, 6},   7,   7}, // positive first arm
      {{0, 0, 0, 12, 12, 16, 3},   3,   0}, // positive signed default
  };
  for (const auto& c : cases) {
    std::ostringstream args;
    for (size_t i = 0; i < design.inputs.size(); ++i) {
      const auto& input = design.inputs[i];
      const auto  found = std::find_if(inputs.begin(), inputs.end(), [&](const auto& p) { return p.first == input.name; });
      ASSERT_NE(found, inputs.end());
      args << (i ? ", " : "") << "mk_bv " << input.width << " " << c.values[std::distance(inputs.begin(), found)];
    }
    for (size_t i = 0; i < design.outputs.size(); ++i) {
      const auto& name = design.outputs[i].name;
      ASSERT_TRUE(name == "explicit_default" || name == "implicit_zero");
      out << "\nexample : bv_uint ((hotmux_signed_oracle_step #[" << args.str() << "] ⟨#[], #[]⟩).outputs[" << i
          << "]!) = " << (name == "explicit_default" ? c.explicit_value : c.implicit_value) << " := by native_decide\n";
    }
  }
  ASSERT_TRUE(out.good());
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

// The node-width ceiling is OPT-IN. Scanning with default options must accept a
// node of any finite width LiveHD can represent; only an explicitly requested
// `max_width` may refuse one. A default ceiling here refuses designs the rest of
// the tool handles perfectly well -- rows B09 and B10 of the 12x5 matrix carry
// 3536- and 2080-bit nodes and were turned away by a 1024 default nobody asked
// for, which read as "the emitter cannot do this design" when it could.
TEST(DesignScan, WideNodesScanByDefaultAndOnlyAnExplicitCapRefusesThem) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_wide_node");
  auto  io  = lib.create_io("wide_node");
  io->add_input("a", 1);
  io->set_bits("a", 3536);
  io->add_input("b", 2);
  io->set_bits("b", 3536);
  io->add_output("y", 1);
  io->set_bits("y", 3536);
  auto g    = io->create_graph();
  auto node = create_typed_node(*g, Ntype_op::And);
  set_bits(node.create_driver_pin(0), 3536);
  g->get_input_pin("a").connect_sink(node.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(node.create_sink_pin(1));
  node.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  EXPECT_NO_THROW(scan_design(*g, {})) << "the default must impose no width cap";
  EXPECT_THROW(scan_design(*g, ScanOptions{.max_width = 1024}), std::runtime_error)
      << "an explicitly requested cap must still refuse a node above it";
}

// Same property through the Get_mask path, which infers a width rather than
// reading one off a pin. `[0:1025]` is a well-formed 1026-bit extraction: it
// satisfies `0 <= lo < hi`, and a mask wider than its source extends rather
// than being malformed. Only an explicit cap may turn it away.
TEST(DesignScan, WideMaskEndpointsScanByDefaultAndOnlyAnExplicitCapRefusesThem) {
  using namespace livehd::graph_util;
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_wide_mask");
  auto  io  = lib.create_io("wide_mask");
  io->add_output("y", 1);
  io->set_bits("y", 8);
  auto       g     = io->create_graph();
  auto       node  = create_typed_node(*g, Ntype_op::Get_mask);
  const auto drive = [&](std::string_view name, int value) {
    create_const(*g, *Dlop::create_integer(value)).connect_sink(setup_sink_by_name(node, name));
  };
  drive("a", 171);
  drive("lo", 0);
  drive("hi", 1025);
  set_bits(node.create_driver_pin(0), 8);
  node.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  EXPECT_NO_THROW(scan_design(*g, {})) << "a 1026-bit mask is well formed; the default imposes no cap";
  EXPECT_THROW(scan_design(*g, ScanOptions{.max_width = 1024}), std::runtime_error)
      << "an explicitly requested cap must still refuse it";
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
      if (const char* dir = std::getenv("LHD_LEAN_MASK_ORACLES")) {
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
            << name << "_designCert (allEdges " << name
            << "_designCert) (inputs a v) ⟨#[], #[]⟩).outputs[0]!) == Int.ofNat (expected a v))) = true := by native_decide\n";
        ASSERT_TRUE(out.good());
      }
    }
  }
}
// The rule is `0 <= lo < hi` and nothing more (design_scan.cpp:1022). A mask
// WIDER than its source is legitimate -- it extends -- so do not add a wide
// endpoint pair here expecting a refusal: that is a `max_width` question, and
// it lives in WideNodesScanByDefaultAndOnlyAnExplicitCapRefusesThem below.
TEST(DesignScan, InvalidMaskEndpointsAreRefused) {
  using namespace livehd::graph_util;
  for (const auto& [lo, hi] : std::vector<std::pair<int, int>>{
           {-1, 3},
           { 3, 3},
           { 4, 3}
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
    if (const char* dir = std::getenv("LHD_LEAN_CONCAT_ORACLES")) {
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
          << ")).all (fun b => bv_uint ((interpretDesign " << name << "_designCert (allEdges " << name << "_designCert) #[mk_bv "
          << c.a_bits << " (Int.ofNat a), mk_bv " << c.b_bits
          << " (Int.ofNat b)] ⟨#[], #[]⟩).outputs[0]!) == expected a b)) = true := by native_decide\n";
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
// Three transparent wrappers may sit between a primary input and an
// asynchronous reset pin: the emitter's arity-1 Or resize, a zero-based
// Get_mask window covering the source, and a ONE-BIT Sext. The last is what
// CVA6's controller and CORE-ET's CSR blocks actually emit; without it those
// flops are refused as "not driven by a primary input".
TEST(DesignScan, ActiveLowAsyncResetSurvivesResizingAndControlsNextState) {
  enum Wrapper { kOr, kEndpoint, kSext };
  for (const int wrapper : {kOr, kEndpoint, kSext}) {
    const bool endpoint_resize = wrapper == kEndpoint;
    auto&      lib             = livehd::Hhds_graph_library::instance(
        wrapper == kOr ? "lgdb_scan_reset" : (endpoint_resize ? "lgdb_scan_reset_endpoint" : "lgdb_scan_reset_sext"));
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
    auto resize = create_typed_node(
        *g, endpoint_resize ? Ntype_op::Get_mask : (wrapper == kSext ? Ntype_op::Sext : Ntype_op::Or));
    set_bits(resize.create_driver_pin(0), 1);
    if (endpoint_resize) {
      set_bits(g->get_input_pin("rst"), 1);
      livehd::graph_util::connect_mask_operands(resize, g->get_input_pin("rst"), 0, 1);
    } else if (wrapper == kSext) {
      // Sext(a, b): keep the low b bits of a and sign-extend from bit b-1.
      // The transparency rule reads the SOURCE PIN's width, which is populated
      // by set_bits on the pin and not by the io declaration alone -- the
      // endpoint branch above needs the same call for the same reason.
      set_bits(g->get_input_pin("rst"), 1);
      g->get_input_pin("rst").connect_sink(livehd::graph_util::setup_sink_by_name(resize, "a"));
      livehd::graph_util::create_const(*g, *Dlop::create_integer(1))
          .connect_sink(livehd::graph_util::setup_sink_by_name(resize, "b"));
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
    // runtime directory. Run this test with LHD_LEAN_RESET_FIXTURE=<path>.lean, then
    // elaborate that file with the compiler library on LEAN_PATH.
    if (const char* path = std::getenv("LHD_LEAN_RESET_FIXTURE")) {
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

// The Sext wrapper is transparent to a CONDITION only for a ONE-BIT source with
// a constant amount of at least one. Every other shape must keep refusing:
// Sext(a, b) truncates to the low b bits BEFORE sign-extending, so for a wider
// source `nonzero(Sext(a,b))` asks about those low bits rather than about `a`,
// and following it would silently change which condition resets the flop.
//
// This is the "do not loosen the primary-input reset restriction" half of the
// repair. Without it the positive case above could be satisfied by a rule that
// follows any Sext at all.
//
// Since decision 2B such a reset is a combinational function of primary inputs
// and lowers through an override node whose selector is the Sext ITSELF, so
// the condition is exact. What must still never happen is the shortcut this
// test guards: resolving to the raw input (a `flopQAsync` reading `rst`).
TEST(DesignScan, NonTransparentSextResetWrapperIsStillRefused) {
  struct Shape {
    const char* name;
    uint32_t    source_bits;
    int64_t     amount;
    bool        constant_amount;
  };
  for (const auto& shape : {Shape{"wide_source", 4, 1, true},
                            Shape{"zero_amount", 1, 0, true},
                            Shape{"dynamic_amount", 1, 1, false}}) {
    auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_scan_reset_bad_") + shape.name);
    auto  io  = lib.create_io("reset_bad");
    io->add_input("clk", 1);
    io->set_bits("clk", 1);
    io->add_input("rst", 2);
    io->set_bits("rst", shape.source_bits);
    io->add_input("clk_unused", 4);
    io->set_bits("clk_unused", 1);
    io->add_input("amt", 3);
    io->set_bits("amt", 4);
    io->add_output("q", 1);
    io->set_bits("q", 8);
    auto g    = io->create_graph();
    auto flop = create_typed_node(*g, Ntype_op::Flop);
    set_bits(flop.create_driver_pin(0), 8);
    auto sext = create_typed_node(*g, Ntype_op::Sext);
    set_bits(sext.create_driver_pin(0), 1);
    set_bits(g->get_input_pin("rst"), shape.source_bits);
    g->get_input_pin("rst").connect_sink(livehd::graph_util::setup_sink_by_name(sext, "a"));
    if (shape.constant_amount) {
      livehd::graph_util::create_const(*g, *Dlop::create_integer(shape.amount))
          .connect_sink(livehd::graph_util::setup_sink_by_name(sext, "b"));
    } else {
      g->get_input_pin("amt").connect_sink(livehd::graph_util::setup_sink_by_name(sext, "b"));
    }
    sext.create_driver_pin(0).connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "reset_pin")));
    g->get_input_pin("clk").connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin")));
    auto drive = [&](std::string_view name, int64_t value) {
      livehd::graph_util::create_const(*g, *Dlop::create_integer(value))
          .connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, name)));
    };
    drive("din", 12);
    drive("async", 1);
    flop.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

    const auto design = scan_design(*g, {});
    ASSERT_EQ(design.flops.size(), 1u) << shape.name;
    EXPECT_FALSE(design.flops[0].reset_input.has_value())
        << shape.name << ": a non-transparent Sext must not resolve to a primary input";
    const auto c = build_certificate(design, {});
    for (const auto& src : c.sources) {
      EXPECT_FALSE(src.async_reset) << shape.name << ": no source may read the raw input as the reset";
    }
    ASSERT_EQ(c.flops.size(), 1u) << shape.name;
    ASSERT_TRUE(c.flops[0].reset_pin.has_value()) << shape.name;
    bool overridden = false;
    for (const auto& n : c.nodes) {
      if (n.op.kind == Operation::MuxBool && !n.deps.empty() && n.deps[0] == *c.flops[0].reset_pin) {
        overridden = true;
      }
    }
    EXPECT_TRUE(overridden) << shape.name << ": the read side must select on the Sext's own slot";
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

// ---------------------------------------------------------------------------
// Clock-domain resolution (MASTER_INTEGRATION.md R13.3, step 2).
//
// Every test here is about a REFUSAL or an ORDINAL, because those are the two
// ways domain resolution goes wrong silently. A refusal that does not happen
// ships a design whose timing nobody checked; an ordinal that merges two roots
// makes two time bases commit together forever after. Neither surfaces as a
// proof failure -- a certificate proves whatever it was given.
// ---------------------------------------------------------------------------

// `clock_of[i]` names the input flop i clocks on; "" leaves it implicit.
struct ClockFixture {
  std::shared_ptr<hhds::Graph> g;
};

ClockFixture build_flops(std::string_view lgdb, const std::vector<std::string>& clock_of,
                         const std::vector<uint32_t>& clock_bits = {}) {
  auto&                 lib = livehd::Hhds_graph_library::instance(std::string(lgdb));
  auto                  io  = lib.create_io("clockfix");
  uint32_t              pid = 1;
  std::set<std::string> added;
  for (size_t i = 0; i < clock_of.size(); ++i) {
    if (clock_of[i].empty() || added.count(clock_of[i])) {
      continue;
    }
    io->add_input(clock_of[i], pid++);
    io->set_bits(clock_of[i], clock_bits.empty() ? 1 : clock_bits[i]);
    added.insert(clock_of[i]);
  }
  io->add_input("d", pid++);
  io->set_bits("d", 8);
  for (size_t i = 0; i < clock_of.size(); ++i) {
    io->add_output("q" + std::to_string(i), static_cast<uint32_t>(i + 1));
    io->set_bits("q" + std::to_string(i), 8);
  }
  ClockFixture f;
  f.g = io->create_graph();
  for (size_t i = 0; i < clock_of.size(); ++i) {
    auto flop = create_typed_node(*f.g, Ntype_op::Flop);
    set_bits(flop.create_driver_pin(0), 8);
    f.g->get_input_pin("d").connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "din")));
    if (!clock_of[i].empty()) {
      f.g->get_input_pin(clock_of[i]).connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin")));
    }
    flop.create_driver_pin(0).connect_sink(f.g->get_output_pin("q" + std::to_string(i)));
  }
  return f;
}

// Expect a refusal whose message explains itself. A refusal nobody can act on
// is only marginally better than no refusal.
void expect_refusal(hhds::Graph& g, std::string_view needle) {
  try {
    scan_design(g, {});
    ADD_FAILURE() << "expected a refusal mentioning: " << needle;
  } catch (const std::exception& e) {
    EXPECT_NE(std::string(e.what()).find(std::string(needle)), std::string::npos)
        << "refused, but not for the expected reason: " << e.what();
  }
}

// INDEPENDENT clocks get DISTINCT ordinals. Merging them is the failure that
// cannot be seen downstream: both flops would commit on every edge of one net
// and the certificate would prove exactly that.
TEST(DesignScanClocks, IndependentClocksGetDistinctOrdinals) {
  auto       f      = build_flops("lgdb_clk_independent", {"clk_a", "clk_b"});
  const auto design = scan_design(*f.g, {});
  ASSERT_EQ(design.flops.size(), 2u);
  EXPECT_EQ(design.clocks.size(), 2u);
  EXPECT_NE(design.flops[0].clock_ordinal, design.flops[1].clock_ordinal);
  // Both ordinals IN RANGE -- this is the `_clock_bounds` obligation the
  // emitted certificate has to discharge.
  for (const auto& fl : design.flops) {
    EXPECT_LT(fl.clock_ordinal, design.clocks.size());
  }
}

// Two flops on the SAME net share ONE ordinal. The converse failure: a resolver
// that gave every flop its own domain would quietly stop any two flops from
// ever committing in the same step.
TEST(DesignScanClocks, SameClockSharesOneOrdinal) {
  auto       f      = build_flops("lgdb_clk_shared", {"clk", "clk"});
  const auto design = scan_design(*f.g, {});
  ASSERT_EQ(design.flops.size(), 2u);
  EXPECT_EQ(design.clocks.size(), 1u);
  EXPECT_EQ(design.flops[0].clock_ordinal, design.flops[1].clock_ordinal);
}

// Ordinals are a function of the DESIGN, not of traversal order: two scans must
// number the domains identically or a certificate diff means nothing.
TEST(DesignScanClocks, OrdinalAssignmentIsDeterministicAndSorted) {
  auto       f1 = build_flops("lgdb_clk_det1", {"clk_z", "clk_a", "clk_m"});
  auto       f2 = build_flops("lgdb_clk_det2", {"clk_z", "clk_a", "clk_m"});
  const auto a  = scan_design(*f1.g, {});
  const auto b  = scan_design(*f2.g, {});
  ASSERT_EQ(a.clocks.size(), 3u);
  ASSERT_EQ(a.clocks.size(), b.clocks.size());
  for (size_t i = 0; i < a.clocks.size(); ++i) {
    EXPECT_EQ(a.clocks[i].key, b.clocks[i].key) << "domain " << i << " differs between two scans";
  }
  for (size_t i = 1; i < a.clocks.size(); ++i) {
    EXPECT_LT(a.clocks[i - 1].key, a.clocks[i].key) << "table is not sorted by structural key";
  }
}

// An IMPLICITLY clocked flop is a real domain, not an unresolvable cone -- it is
// the single most common state element in the tree.
TEST(DesignScanClocks, ImplicitClockIsADomain) {
  auto       f      = build_flops("lgdb_clk_implicit", {""});
  const auto design = scan_design(*f.g, {});
  ASSERT_EQ(design.flops.size(), 1u);
  ASSERT_EQ(design.clocks.size(), 1u);
  EXPECT_TRUE(design.clocks[0].implicit);
  EXPECT_EQ(design.flops[0].clock_ordinal, 0u);
}

// A MULTI-BIT clock root is REFUSED, not merged. `control_root` follows every
// Get_mask to the whole input, so `clks[0]` and `clks[1]` share one key and
// would otherwise land in one ordinal -- two time bases in one domain.
TEST(DesignScanClocks, MultiBitClockRootIsRefused) {
  auto f = build_flops("lgdb_clk_bus", {"clks"}, {4});
  expect_refusal(*f.g, "cannot be discriminated");
}

// A CONSTANT clock is refused. `commit_class_of` is not a refusal oracle here:
// the cone walk behind it stops at a constant and returns the CONSTANT as the
// net, so a flop clocked by a literal comes back with a well-formed commit
// class and would be accepted as an ordinary domain.
TEST(DesignScanClocks, ConstantClockIsRefused) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_clk_const");
  auto  io  = lib.create_io("constclk");
  io->add_input("d", 1);
  io->set_bits("d", 8);
  io->add_output("q", 1);
  io->set_bits("q", 8);
  auto g    = io->create_graph();
  auto flop = create_typed_node(*g, Ntype_op::Flop);
  set_bits(flop.create_driver_pin(0), 8);
  g->get_input_pin("d").connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "din")));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(1))
      .connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin")));
  flop.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
  expect_refusal(*g, "CONSTANT");
}

// A clock whose name is NOT a conventional spelling is still a clock, because a
// flop takes an edge from it. The name fallback in Design_clocks is for designs
// that wire no flop straight to an input; it must not become a requirement.
TEST(DesignScanClocks, UnconventionallyNamedClockStillResolves) {
  auto       f      = build_flops("lgdb_clk_oddname", {"tick_strobe"});
  const auto design = scan_design(*f.g, {});
  ASSERT_EQ(design.flops.size(), 1u);
  ASSERT_EQ(design.clocks.size(), 1u);
  EXPECT_FALSE(design.clocks[0].implicit);
  EXPECT_EQ(design.clocks[0].name, "tick_strobe");
}

// A design with no state resolves no domain and still gets a table, so the
// `_clock_bounds` obligation is discharged against something real rather than a
// field default.
TEST(DesignScanClocks, CombinationalDesignStillHasAClockTable) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_clk_comb");
  auto  io  = lib.create_io("comb");
  io->add_input("a", 1);
  io->set_bits("a", 8);
  io->add_output("z", 1);
  io->set_bits("z", 8);
  auto g  = io->create_graph();
  auto op = create_typed_node(*g, Ntype_op::Or);
  set_bits(op.create_driver_pin(0), 8);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("z"));
  const auto design = scan_design(*g, {});
  EXPECT_TRUE(design.flops.empty());
  EXPECT_EQ(design.clocks.size(), 1u);
}

// The resolved domain reaches the CERTIFICATE. Resolving correctly and then
// dropping the result on the floor is indistinguishable from not resolving.
TEST(DesignScanClocks, OrdinalsAndClockTableReachTheCertificate) {
  auto       f      = build_flops("lgdb_clk_cert", {"clk_a", "clk_b"});
  const auto design = scan_design(*f.g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(cert.clocks.size(), design.clocks.size());
  for (size_t i = 0; i < cert.clocks.size(); ++i) {
    EXPECT_EQ(cert.clocks[i].name, design.clocks[i].name);
  }
  ASSERT_EQ(cert.flops.size(), 2u);
  std::set<uint32_t> seen;
  for (const auto& fl : cert.flops) {
    EXPECT_LT(fl.clock, cert.clocks.size());
    seen.insert(fl.clock);
  }
  EXPECT_EQ(seen.size(), 2u) << "two independent clocks collapsed into one certificate domain";
}

// The JOIN is DESIGN-WIDE, and these three fixtures are why it has to be.
//
// Joining per memory, before the flops were scanned, was wrong in two
// directions at once -- it joined when it had no right to, and failed to join
// when it should have.

// POSITIVE: one explicit domain plus an implicit flop. The implicit element IS
// on that clock, and before the join was made design-wide nothing ever joined
// them, because no memory was involved to trigger the per-memory pass.
TEST(DesignScanClocks, OneExplicitDomainJoinsAnImplicitFlop) {
  auto       f      = build_flops("lgdb_clk_join_pos", {"clk", ""});
  const auto design = scan_design(*f.g, {});
  ASSERT_EQ(design.flops.size(), 2u);
  EXPECT_EQ(design.clocks.size(), 1u) << "the implicit flop kept a domain of its own";
  EXPECT_EQ(design.flops[0].clock_ordinal, design.flops[1].clock_ordinal);
  EXPECT_FALSE(design.clocks[0].implicit) << "joined to the explicit clock, not the other way round";
}

// COUNTEREXAMPLE: TWO explicit domains plus an implicit flop. There is nothing
// to join the implicit element to without guessing which, so it must KEEP its
// own domain. The per-memory version would have seen one explicit domain among
// a memory's own ports and joined to it, while the design had two.
TEST(DesignScanClocks, TwoExplicitDomainsDoNotJoinAnImplicitFlop) {
  auto       f      = build_flops("lgdb_clk_join_neg", {"clk_a", "clk_b", ""});
  const auto design = scan_design(*f.g, {});
  ASSERT_EQ(design.flops.size(), 3u);
  EXPECT_EQ(design.clocks.size(), 3u) << "an implicit element was guessed onto one of two explicit domains";
  std::set<uint32_t> ords;
  for (const auto& fl : design.flops) {
    ords.insert(fl.clock_ordinal);
    EXPECT_LT(fl.clock_ordinal, design.clocks.size());
  }
  EXPECT_EQ(ords.size(), 3u);
  EXPECT_EQ(std::count_if(design.clocks.begin(), design.clocks.end(), [](const ClockDomain& c) { return c.implicit; }), 1);
}

// The join must not be ORDER-dependent: the implicit flop declared first must
// reach the same answer as the implicit flop declared last.
TEST(DesignScanClocks, JoinIsIndependentOfDeclarationOrder) {
  auto       a = build_flops("lgdb_clk_join_o1", {"clk", ""});
  auto       b = build_flops("lgdb_clk_join_o2", {"", "clk"});
  const auto da = scan_design(*a.g, {});
  const auto db = scan_design(*b.g, {});
  EXPECT_EQ(da.clocks.size(), 1u);
  EXPECT_EQ(db.clocks.size(), 1u);
  EXPECT_EQ(da.clocks[0].key, db.clocks[0].key);
}

// An OPAQUE computed clock is refused.
//
// `Design_clocks::is_clock` cannot be the test here and the first version of
// this check used it anyway: the constructor records `resolve_phase(clock_pin)`
// for EVERY flop, including a computed root, so asking whether a flop's own
// root is a clock always answers yes. The check is vacuous and this design
// would have sailed through it.
TEST(DesignScanClocks, OpaqueComputedClockIsRefused) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_clk_opaque");
  auto  io  = lib.create_io("opaqueclk");
  io->add_input("a", 1);
  io->set_bits("a", 1);
  io->add_input("b", 2);
  io->set_bits("b", 1);
  io->add_input("d", 3);
  io->set_bits("d", 8);
  io->add_output("q", 1);
  io->set_bits("q", 8);
  auto g = io->create_graph();
  // A computed net -- not a design input, so the runtime has no edge to supply.
  auto xo = create_typed_node(*g, Ntype_op::Xor);
  set_bits(xo.create_driver_pin(0), 1);
  g->get_input_pin("a").connect_sink(xo.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(xo.create_sink_pin(1));
  auto flop = create_typed_node(*g, Ntype_op::Flop);
  set_bits(flop.create_driver_pin(0), 8);
  g->get_input_pin("d").connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "din")));
  xo.create_driver_pin(0).connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin")));
  flop.create_driver_pin(0).connect_sink(g->get_output_pin("q"));
  expect_refusal(*g, "an internal net rather than a design input");
}

// ---------------------------------------------------------------------------
// VALUE gates for clock-domain resolution.
//
// Every test above is about an ordinal or a refusal: WHICH domain an element
// lands in, and which designs are turned away. None says what the resolved
// metadata MEANS when the design is stepped. A resolver can assign every
// ordinal correctly and still be useless if a quiet domain commits anyway.
//
// These emit a certificate from the real scanner and hand it to Lean, which
// decides concrete steps with `native_decide`. The fixture is elaborated by
// pass/lean/scripts/clock_oracle.sh.
// ---------------------------------------------------------------------------

// A two-domain design: flop A on `clk_a`, flop B on `clk_b`, each loading its
// own input. Four edge vectors cover the whole table -- neither fires, only A,
// only B, both -- which is what distinguishes a real domain from an ordinal
// that is carried but never consulted.
// Scanner-produced DCERT2, written beside the Lean oracle so the two describe
// the SAME `CertificateIR`.
//
// WHY. Every production certificate the parity and runtime gates had was
// single-domain: `clocks` of size one, every `FlopDesc.clock` and
// `MemoryDesc.clock` zero. Those runs cannot distinguish "the ordinal was
// carried" from "the field defaulted", so they do not cover the metadata
// DCERT2 exists for. These two oracles are the only places on master where the
// REAL scanner resolves more than one domain, so this is where a multi-domain
// certificate comes from -- not from a hand-built fixture, which would prove
// something about my typing rather than about the scanner.
void write_scanner_dcert(const char* var, const CertificateIR& cert) {
  const char* path = std::getenv(var);
  if (path == nullptr) {
    return;  // absent is "not requested"; it is never a silent failure
  }
  write_dcert(cert, path);
}

TEST(DesignScanClocks, TwoDomainValueOracle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_clk_value_oracle");
  auto  io  = lib.create_io("clk_value_oracle");
  io->add_input("clk_a", 1);
  io->set_bits("clk_a", 1);
  io->add_input("clk_b", 2);
  io->set_bits("clk_b", 1);
  io->add_input("da", 3);
  io->set_bits("da", 8);
  io->add_input("db", 4);
  io->set_bits("db", 8);
  io->add_output("qa", 1);
  io->set_bits("qa", 8);
  io->add_output("qb", 2);
  io->set_bits("qb", 8);
  auto g = io->create_graph();
  auto mk = [&](const char* clk, const char* din, const char* q) {
    auto flop = create_typed_node(*g, Ntype_op::Flop);
    set_bits(flop.create_driver_pin(0), 8);
    g->get_input_pin(din).connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "din")));
    g->get_input_pin(clk).connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin")));
    flop.create_driver_pin(0).connect_sink(g->get_output_pin(q));
  };
  mk("clk_a", "da", "qa");
  mk("clk_b", "db", "qb");

  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(design.clocks.size(), 2u) << "fixture is not two-domain; the rest of this test is meaningless";
  ASSERT_EQ(design.flops.size(), 2u);
  ASSERT_NE(design.flops[0].clock_ordinal, design.flops[1].clock_ordinal);

  const char* path = std::getenv("LHD_LEAN_CLOCK_FIXTURE");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_LEAN_CLOCK_FIXTURE to emit the Lean oracle";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);
  write_scanner_dcert("LHD_D4_CLOCK_DCERT", cert);

  // Input ordinals are the index in design.inputs, which is scanned order, not
  // declaration order -- build the argument vector from the scan.
  auto arg_index = [&](std::string_view name) -> size_t {
    for (size_t i = 0; i < design.inputs.size(); ++i) {
      if (design.inputs[i].name == name) {
        return i;
      }
    }
    ADD_FAILURE() << "no input named " << name;
    return 0;
  };
  const auto i_da = arg_index("da");
  const auto i_db = arg_index("db");
  // Which CERTIFICATE flop ordinal is on which domain. The emitted flop array
  // is ordered by nid, so read the mapping rather than assuming it.
  const auto ord_a = design.flops[0].clock_ordinal;
  const size_t fa = 0, fb = 1;

  auto inputs = [&](int da, int db) {
    std::ostringstream o;
    for (size_t k = 0; k < design.inputs.size(); ++k) {
      o << (k ? ", " : "") << "mk_bv 8 " << (k == i_da ? da : (k == i_db ? db : 0));
    }
    return o.str();
  };
  // Pre-state: both flops hold 7 and 9.
  const char* st0 = "(⟨#[mk_bv 8 7, mk_bv 8 9], #[]⟩ : RuntimeState)";
  auto edges = [&](bool a, bool b) {
    std::ostringstream o;
    // positional over design.clocks; ord_a is flop[0]'s domain
    o << "#[";
    for (size_t c = 0; c < design.clocks.size(); ++c) {
      const bool fire = (c == ord_a) ? a : b;
      o << (c ? ", " : "") << (fire ? "true" : "false");
    }
    o << "]";
    return o.str();
  };
  out << "\n-- VALUE GATES: a quiet domain must HOLD, a firing domain must COMMIT.\n";
  struct Case { bool a, b; int qa, qb; const char* why; };
  const Case cases[] = {
      {false, false, 7, 9, "both quiet: both hold their pre-state"},
      {true,  false, 3, 9, "only clk_a fires: A commits, B HOLDS"},
      {false, true,  7, 5, "only clk_b fires: B commits, A HOLDS"},
      {true,  true,  3, 5, "both fire: simultaneous commit, same as allEdges"},
  };
  for (const auto& c : cases) {
    out << "\n-- " << c.why << "\n"
        << "example : (compileAndRun clk_value_oracle_designCert " << edges(c.a, c.b) << " #[" << inputs(3, 5) << "] "
        << st0 << ").nextState.flops[" << fa << "]! = mk_bv 8 " << c.qa << " := by native_decide\n"
        << "example : (compileAndRun clk_value_oracle_designCert " << edges(c.a, c.b) << " #[" << inputs(3, 5) << "] "
        << st0 << ").nextState.flops[" << fb << "]! = mk_bv 8 " << c.qb << " := by native_decide\n";
  }
  // allEdges must agree with the all-true vector -- the compatibility claim.
  out << "\n-- allEdges IS the all-fire vector: the preserved schedule.\n"
      << "example : allEdges clk_value_oracle_designCert = " << edges(true, true) << " := by native_decide\n";
  // An OUT-OF-RANGE ordinal never fires: `fires` reads e[c]? and defaults false.
  out << "\n-- A SHORT edge vector leaves the missing domain quiet (fail-safe), it does not fire it.\n"
      << "example : (compileAndRun clk_value_oracle_designCert #[] #[" << inputs(3, 5) << "] " << st0
      << ").nextState.flops[" << fa << "]! = mk_bv 8 7 := by native_decide\n"
      << "example : (compileAndRun clk_value_oracle_designCert #[] #[" << inputs(3, 5) << "] " << st0
      << ").nextState.flops[" << fb << "]! = mk_bv 8 9 := by native_decide\n";
  ASSERT_TRUE(out.good());
}

// A flop with a chosen reset flavour, on a chosen clock, driven from `d`.
struct ResetFlopSpec {
  const char* clk;        // clock input name
  bool        async;      // asynchronous reset
  bool        active_low; // `negreset`
  int64_t     init;       // reset VALUE -- deliberately non-zero in the fixture
  bool        enable;     // wire an enable, to exercise reset/enable priority
};

// Build a design of reset flops, each with its own output, all reading `d`.
std::shared_ptr<hhds::Graph> build_reset_flops(std::string_view lgdb,
                                               const std::vector<ResetFlopSpec>& specs) {
  auto&                 lib = livehd::Hhds_graph_library::instance(std::string(lgdb));
  auto                  io  = lib.create_io("resetfix");
  uint32_t              pid = 1;
  std::set<std::string> added;
  for (const auto& sp : specs) {
    if (added.insert(sp.clk).second) {
      io->add_input(sp.clk, pid++);
      io->set_bits(sp.clk, 1);
    }
  }
  io->add_input("rst", pid++);
  io->set_bits("rst", 1);
  io->add_input("en", pid++);
  io->set_bits("en", 1);
  io->add_input("d", pid++);
  io->set_bits("d", 8);
  for (size_t i = 0; i < specs.size(); ++i) {
    io->add_output("q" + std::to_string(i), static_cast<uint32_t>(i + 1));
    io->set_bits("q" + std::to_string(i), 8);
  }
  auto g = io->create_graph();
  for (size_t i = 0; i < specs.size(); ++i) {
    const auto& sp   = specs[i];
    auto        flop = create_typed_node(*g, Ntype_op::Flop);
    set_bits(flop.create_driver_pin(0), 8);
    auto sink = [&](std::string_view n) {
      return flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, n));
    };
    g->get_input_pin("d").connect_sink(sink("din"));
    g->get_input_pin("rst").connect_sink(sink("reset_pin"));
    g->get_input_pin(sp.clk).connect_sink(sink("clock_pin"));
    if (sp.enable) {
      g->get_input_pin("en").connect_sink(sink("enable"));
    }
    auto konst = [&](std::string_view n, int64_t v) {
      livehd::graph_util::create_const(*g, *Dlop::create_integer(v)).connect_sink(sink(n));
    };
    konst("initial", sp.init);
    konst("async", sp.async ? 1 : 0);
    konst("negreset", sp.active_low ? 1 : 0);
    flop.create_driver_pin(0).connect_sink(g->get_output_pin("q" + std::to_string(i)));
  }
  return g;
}

// RESET VALUE GATES.
//
// The whole point of resolving domains is that a QUIET clock changes what a
// reset means, and the two reset flavours part company exactly there:
//
//   * a SYNCHRONOUS reset is sampled at the edge, like `din`. With its clock
//     quiet the flop must HOLD -- it must not reset.
//   * an ASYNCHRONOUS reset acts with no edge at all, on the visible Q AND on
//     the committed state.
//
// In the one-clock model this distinction was invisible, because every clock
// fired on every step. These are the cases that only exist once domains do.
TEST(DesignScanClocks, QuietClockResetValueOracle) {
  // Four flops, two domains, every combination that matters:
  //   0: SYNC  reset, active high, reset value 7,  on clk_a
  //   1: ASYNC reset, active high, reset value 7,  on clk_a
  //   2: ASYNC reset, active LOW,  reset value 42, on clk_b
  //   3: SYNC  reset with an ENABLE, value 7,      on clk_b  (priority)
  const std::vector<ResetFlopSpec> specs{
      {"clk_a", false, false, 7, false},
      {"clk_a", true, false, 7, false},
      {"clk_b", true, true, 42, false},
      {"clk_b", false, false, 7, true},
  };
  auto       g      = build_reset_flops("lgdb_clk_reset_oracle", specs);
  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  ASSERT_EQ(design.clocks.size(), 2u) << "fixture is not two-domain";
  ASSERT_EQ(design.flops.size(), 4u);

  // The scan must have kept the flavours apart; if it did not, every value
  // assertion below would still "pass" against a uniformly wrong model.
  int n_async = 0;
  for (const auto& f : design.flops) {
    n_async += f.asynchronous ? 1 : 0;
  }
  ASSERT_EQ(n_async, 2) << "the scan lost the sync/async distinction";

  const char* path = std::getenv("LHD_LEAN_RESET_FIXTURE");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_LEAN_RESET_FIXTURE to emit the Lean oracle";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);

  auto ordinal_of = [&](std::string_view name) -> size_t {
    for (size_t i = 0; i < design.inputs.size(); ++i) {
      if (design.inputs[i].name == name) {
        return i;
      }
    }
    ADD_FAILURE() << "no input " << name;
    return 0;
  };
  const auto i_rst = ordinal_of("rst");
  const auto i_en  = ordinal_of("en");
  const auto i_d   = ordinal_of("d");
  auto       inputs = [&](int rst, int en, int d) {
    std::ostringstream o;
    for (size_t k = 0; k < design.inputs.size(); ++k) {
      const int v = k == i_rst ? rst : (k == i_en ? en : (k == i_d ? d : 0));
      o << (k ? ", " : "") << "mk_bv " << (k == i_d ? 8 : 1) << " " << v;
    }
    return o.str();
  };
  // Which ordinal is clk_a? flop 0 is on it by construction.
  const auto ord_a = design.flops[0].clock_ordinal;
  auto       edges = [&](bool a, bool b) {
    std::ostringstream o;
    o << "#[";
    for (size_t c = 0; c < design.clocks.size(); ++c) {
      o << (c ? ", " : "") << ((c == ord_a ? a : b) ? "true" : "false");
    }
    o << "]";
    return o.str();
  };
  // Pre-state: every flop holds 99, so a hold and a reset are distinguishable.
  const char* st = "(⟨#[mk_bv 8 99, mk_bv 8 99, mk_bv 8 99, mk_bv 8 99], #[]⟩ : RuntimeState)";

  auto emit_case = [&](const char* why, const std::string& e, const std::string& in,
                       std::initializer_list<std::pair<int, int>> state_expect,
                       std::initializer_list<std::pair<int, int>> out_expect) {
    out << "\n-- " << why << "\n";
    for (auto [idx, want] : state_expect) {
      out << "example : ((compileAndRun resetfix_designCert " << e << " #[" << in << "] " << st
          << ").nextState.flops[" << idx << "]!) = mk_bv 8 " << want << " := by native_decide\n";
    }
    for (auto [idx, want] : out_expect) {
      out << "example : ((compileAndRun resetfix_designCert " << e << " #[" << in << "] " << st
          << ").outputs[" << idx << "]!) = mk_bv 8 " << want << " := by native_decide\n";
    }
  };

  // -- the headline: reset asserted, BOTH clocks quiet ----------------------
  // sync flops HOLD 99 (the reset is sampled at an edge that never came);
  // async flops reset, on the committed state AND on the visible Q.
  emit_case("reset asserted, both clocks QUIET: sync holds, async resets",
            edges(false, false), inputs(1, 1, 5),
            {{0, 99}, {1, 7}, {2, 99}, {3, 99}},   // flop2 is active-LOW: rst=1 is INACTIVE
            {{0, 99}, {1, 7}, {2, 99}, {3, 99}});

  // active-LOW async with rst=0: now it is the one that resets, to 42.
  emit_case("active-LOW async reset asserted (rst=0), clocks quiet",
            edges(false, false), inputs(0, 1, 5),
            {{1, 99}, {2, 42}},
            {{1, 99}, {2, 42}});

  // -- clk_a fires, reset asserted -----------------------------------------
  // the sync flop on clk_a now sees its reset at an edge.
  emit_case("clk_a fires with reset asserted: sync flop 0 resets at the edge",
            edges(true, false), inputs(1, 1, 5),
            {{0, 7}, {1, 7}, {3, 99}},
            {});

  // -- reset vs enable priority --------------------------------------------
  // flop 3 is sync with an enable. Its clock fires, reset asserted, enable OFF:
  // reset must win over a disabled enable.
  emit_case("clk_b fires, reset asserted, enable OFF: reset outranks enable",
            edges(false, true), inputs(1, 0, 5),
            {{3, 7}},
            {});
  // ...and with reset deasserted and the enable off, it holds rather than loading d.
  emit_case("clk_b fires, reset deasserted, enable OFF: holds, does not load d",
            edges(false, true), inputs(0, 0, 5),
            {{3, 99}},
            {});
  // Note rst=0 asserts flop 2's active-low async reset, which is why it is not
  // asserted on here.

  // -- no clock fires; the ACTIVE-HIGH resets are deasserted -----------------
  //
  // NOT "every flop holds". `rst=0` ASSERTS flop 2's active-LOW asynchronous
  // reset, so this row says nothing about flop 2 -- and flop 1's async reset is
  // active-high, so it is quiet here too. What is checked is the two flops whose
  // reset is deasserted at rst=0: the synchronous ones, 0 and 3.
  emit_case("no edge, active-high resets deasserted: the SYNC flops 0 and 3 hold "
            "(flop 2's active-low async reset is ASSERTED at rst=0 and is covered above)",
            edges(false, false), inputs(0, 1, 5),
            {{0, 99}, {3, 99}},
            {{0, 99}, {3, 99}});

  // -- the CHECKED runtime API, on this same artifact -----------------------
  //
  // `fires` reads `e[c]?` and defaults a missing entry to false, so a short
  // vector silently runs with domains quiet and an out-of-range ordinal silently
  // never fires. That is the right FAIL-SAFE for the interpreter and the wrong
  // thing to accept from a caller, which is what `checkRuntime` is for. These
  // assert the REJECTION, not the fallback.
  const std::string good_in = inputs(0, 1, 5);
  out << "\n-- checkDesign accepts the scanner's own certificate.\n"
      << "example : (checkDesign resetfix_designCert).isOk := by native_decide\n";
  out << "\n-- checkRuntime accepts a well-formed call...\n"
      << "example : (checkRuntime resetfix_designCert " << edges(true, true) << " #[" << good_in << "] " << st
      << ").isOk := by native_decide\n";
  out << "\n-- ...and REJECTS a SHORT edge vector, rather than running it quiet.\n"
      << "example : (checkRuntime resetfix_designCert #[true] #[" << good_in << "] " << st
      << ").isOk = false := by native_decide\n";
  out << "\n-- ...and an EXTRA entry, which no declared clock could consume.\n"
      << "example : (checkRuntime resetfix_designCert #[true, true, true] #[" << good_in << "] " << st
      << ").isOk = false := by native_decide\n";
  out << "\n-- ...and a state whose flop count disagrees with the design.\n"
      << "example : (checkRuntime resetfix_designCert " << edges(true, true) << " #[" << good_in
      << "] (⟨#[mk_bv 8 99], #[]⟩ : RuntimeState)).isOk = false := by native_decide\n";
  out << "\n-- ...and an input array shorter than the declared arity.\n"
      << "example : (checkRuntime resetfix_designCert " << edges(true, true) << " #[] " << st
      << ").isOk = false := by native_decide\n";

  // An out-of-range clock ORDINAL is a property of the certificate, so it is
  // checkDesign's to refuse. Built by hand from the scanner's own cert.
  out << "\n-- A flop on an undeclared clock ordinal is refused by checkDesign.\n"
      << "def resetfix_badOrdinal : DesignCert :=\n"
      << "  { resetfix_designCert with\n"
      << "    flops := resetfix_designCert.flops.map (fun f => { f with clock := 99 }) }\n"
      << "example : (checkDesign resetfix_badOrdinal).isOk = false := by native_decide\n"
      << "example : (designErrors resetfix_badOrdinal).map SimError.tag"
         " = some \"flopClockOutOfRange\" := by native_decide\n";

  // -- allEdges versus the PRESERVED schedule, on this artifact -------------
  //
  // The compatibility claim is that the emitted two-argument step reproduces the
  // old every-step interpreter. On a TWO-DOMAIN design that is a real statement:
  // it says firing both domains together is what the clockless model did.
  out << "\n-- allEdges IS every domain firing.\n"
      << "example : allEdges resetfix_designCert = " << edges(true, true) << " := by native_decide\n";
  out << "\n-- and the emitted step agrees with the preserved interpreter here.\n"
      << "example : resetfix_step #[" << good_in << "] " << st
      << " = interpretDesignLegacy resetfix_designCert #[" << good_in << "] " << st
      << " := resetfix_step_legacy_correct _ _\n";
  // -- DIRECT versus COMPILED, on the reset artifact ------------------------
  //
  // Reset flavour is exactly where two implementations are most likely to
  // drift: the direct interpreter applies an async reset without an edge, the
  // compiled residual has to encode that in its binding order. Compared across
  // every edge vector, with the reset both asserted and deasserted.
  out << "\n-- direct vs compiled, every edge vector, reset asserted and not.\n";
  for (int ea = 0; ea < 2; ++ea) {
    for (int eb = 0; eb < 2; ++eb) {
      for (int r = 0; r < 2; ++r) {
        out << "example : diffStep resetfix_designCert resetfix_residual " << edges(ea != 0, eb != 0) << " #["
            << inputs(r, 1, 5) << "] " << st << " [] = none := by native_decide\n";
      }
    }
  }
  // A MULTI-STEP trace: reset, then release and load, then hold.
  out << "\n-- ...and across a reset / load / hold trace.\n"
      << "example : diffTrace resetfix_designCert resetfix_residual [] " << st << "\n"
      << "            [⟨" << edges(true, true) << ", #[" << inputs(1, 1, 5) << "]⟩,\n"
      << "             ⟨" << edges(true, true) << ", #[" << inputs(0, 1, 5) << "]⟩,\n"
      << "             ⟨" << edges(false, false) << ", #[" << inputs(0, 1, 5) << "]⟩] 0 = none"
      << " := by native_decide\n";
  // EXPECTED values along that trace, so agreement is not the only claim.
  out << "\n-- and the trace's expected values: reset to 7, then load 5, then HOLD 5\n"
      << "-- while the clock is quiet.\n"
      << "def resetfix_afterReset : RuntimeState :=\n"
      << "  (compileAndRun resetfix_designCert " << edges(true, true) << " #[" << inputs(1, 1, 5) << "] " << st
      << ").nextState\n"
      << "example : (resetfix_afterReset.flops[0]!) = mk_bv 8 7 := by native_decide\n"
      << "def resetfix_afterLoad : RuntimeState :=\n"
      << "  (compileAndRun resetfix_designCert " << edges(true, true) << " #[" << inputs(0, 1, 5)
      << "] resetfix_afterReset).nextState\n"
      << "example : (resetfix_afterLoad.flops[0]!) = mk_bv 8 5 := by native_decide\n"
      << "example : ((compileAndRun resetfix_designCert " << edges(false, false) << " #[" << inputs(0, 1, 5)
      << "] resetfix_afterLoad).nextState.flops[0]!) = mk_bv 8 5 := by native_decide\n";

  out << "\n-- A QUIET domain is NOT the legacy schedule -- the two differ, which is\n"
      << "-- the whole content of resolving domains.\n"
      << "example : (compileAndRun resetfix_designCert " << edges(true, false) << " #[" << inputs(1, 1, 5) << "] " << st
      << ").nextState.flops[3]!\n"
      << "        ≠ (interpretDesignLegacy resetfix_designCert #[" << inputs(1, 1, 5) << "] " << st
      << ").nextState.flops[3]! := by native_decide\n";
  ASSERT_TRUE(out.good());
}

// MEMORY VALUE GATES across quiet and firing domains.
//
// A memory commits on its own clock, and a synchronous read is a REGISTER that
// commits on its port's clock. Both only become observable once a domain can be
// quiet: with every clock firing, a write that commits and a write that is held
// look identical on the next read.
//
// The fixture is a 4-entry RAM on `clk_a` with one write port and one
// synchronous read port, plus a synchronous ROM on `clk_b`, all from the real
// scanner.
std::shared_ptr<hhds::Graph> build_mem_fixture(std::string_view lgdb, bool read_port_clocked,
                                              int64_t wensize = 1, int64_t fwd = 0,
                                              // Which input drives the RAM. The default keeps every
                                              // existing caller on `clk_a`; `swap_mem_clock` puts the
                                              // RAM on `clk_b` and the ROM on `clk_a`, which is the
                                              // only way to get a MemoryDesc off ordinal 0.
                                              bool swap_mem_clock = false) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string(lgdb));
  auto  io  = lib.create_io("memfix");
  uint32_t pid = 1;
  for (const char* n : {"clk_a", "clk_b", "we", "re"}) {
    io->add_input(n, pid++);
    // The write enable carries ONE BIT PER LANE: memory_lowering emits
    // MemWriteBE when wensize > 1 and resizes `enable` to wensize.
    io->set_bits(n, std::string_view(n) == "we" ? static_cast<uint32_t>(wensize) : 1u);
  }
  io->add_input("waddr", pid++);
  io->set_bits("waddr", 2);
  io->add_input("raddr", pid++);
  io->set_bits("raddr", 2);
  io->add_input("wdata", pid++);
  io->set_bits("wdata", 8);
  io->add_input("romaddr", pid++);
  io->set_bits("romaddr", 2);
  io->add_output("rdata", 1);
  io->set_bits("rdata", 8);
  io->add_output("romdata", 2);
  io->set_bits("romdata", 8);
  auto g = io->create_graph();

  // -- the RAM: write port 0, synchronous read port 1, both on clk_a ---------
  auto ram = create_typed_node(*g, Ntype_op::Memory);
  set_bits(ram.create_driver_pin(1), 8);  // read port 1's data output
  // Memory pins are PER PORT: pid = base + port * Memory_port_stride, which is
  // how parse_memory_info recovers the port id (raw_pid / stride).
  const auto mstride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  auto rsink = [&](std::string_view n, uint32_t port) {
    return ram.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, n) + port * mstride);
  };
  auto rconst = [&](std::string_view n, int64_t v, uint32_t port = 0) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(v)).connect_sink(rsink(n, port));
  };
  rconst("bits", 8);
  rconst("size", 4);
  rconst("type", 1);       // synchronous read
  rconst("wensize", wensize);
  rconst("fwd", fwd);      // 0 = no forwarding: a read sees committed contents
  rconst("posclk", 1);
  // port 0 = WRITE, port 1 = synchronous READ.
  rconst("rdport", 0, 0);
  rconst("rdport", 1, 1);
  g->get_input_pin("waddr").connect_sink(rsink("addr", 0));
  g->get_input_pin("wdata").connect_sink(rsink("din", 0));
  g->get_input_pin("we").connect_sink(rsink("enable", 0));
  g->get_input_pin("raddr").connect_sink(rsink("addr", 1));
  g->get_input_pin("re").connect_sink(rsink("enable", 1));
  const char* ram_clk = swap_mem_clock ? "clk_b" : "clk_a";
  const char* rom_clk = swap_mem_clock ? "clk_a" : "clk_b";
  g->get_input_pin(ram_clk).connect_sink(rsink("clock_pin", 0));
  if (read_port_clocked) {
    g->get_input_pin(ram_clk).connect_sink(rsink("clock_pin", 1));
  }
  ram.create_driver_pin(1).connect_sink(g->get_output_pin("rdata"));

  // -- the ROM on the OTHER domain -----------------------------------------
  auto rom = create_typed_node(*g, Ntype_op::Memory);
  set_bits(rom.create_driver_pin(0), 8);
  auto osink = [&](std::string_view n) { return rom.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, n)); };
  auto oconst = [&](std::string_view n, int64_t v) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(v)).connect_sink(osink(n));
  };
  oconst("bits", 8);
  oconst("size", 3);
  oconst("type", 1);      // synchronous ROM: a registered read
  oconst("rdport", 1);
  oconst("initial", 0x030201);
  g->get_input_pin("romaddr").connect_sink(osink("addr"));
  g->get_input_pin("re").connect_sink(osink("enable"));
  g->get_input_pin(rom_clk).connect_sink(osink("clock_pin"));
  rom.create_driver_pin(0).connect_sink(g->get_output_pin("romdata"));

  return g;
}

// A memory whose ports land on DIFFERENT domains is refused, not merged.
//
// Here the write port is explicitly on `clk_a` while the synchronous read port
// carries no clock pin, so it resolves to the implicit domain. The design-wide
// join (R15.2) would fold an implicit element into a UNIQUE explicit domain --
// but this design has two, `clk_a` and the ROM's `clk_b`, so there is nothing to
// join it to without guessing which. One `MemoryDesc` carries one clock, and
// guessing would put two time bases in one ordinal.
TEST(DesignScanClocks, MixedDomainMemoryIsRefused) {
  auto g = build_mem_fixture("lgdb_clk_mem_mixed", /*read_port_clocked=*/false);
  try {
    scan_design(*g, {});
    ADD_FAILURE() << "a memory with ports on two domains was accepted";
  } catch (const std::exception& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("more than one clock domain"), std::string::npos) << what;
    EXPECT_NE(what.find("ONE MemoryDesc.clock"), std::string::npos) << what;
  }
}

// A scanner-resolved certificate whose MEMORY commits on a NON-DEFAULT domain.
//
// Every production certificate the runtime and parity gates had was
// single-domain, and `MemoryQuietDomainValueOracle` -- the only multi-domain
// memory case on master -- happens to land its `MemoryDesc` on ordinal 0. So
// neither covers a carried memory domain: a reader that dropped
// `MemoryDesc.clock` entirely would pass both. This builds the same fixture
// with the RAM on the other clock and REQUIRES the ordinal to be non-zero, so
// the coverage cannot be satisfied by a defaulted field.
TEST(DesignScanClocks, MemoryNonDefaultDomainCertificate) {
  auto g = build_mem_fixture("lgdb_clk_mem_nondefault", /*read_port_clocked=*/true,
                             /*wensize=*/1, /*fwd=*/0, /*swap_mem_clock=*/true);
  DesignScan design;
  try {
    design = scan_design(*g, {});
  } catch (const std::exception& e) {
    FAIL() << "fixture refused by the scanner: " << e.what();
  }
  const auto cert = build_certificate(design, {});
  ASSERT_EQ(design.clocks.size(), 2u) << "the fixture must resolve two domains";
  ASSERT_EQ(cert.memories.size(), 1u) << "the ROM carries no MemoryDesc";
  EXPECT_NE(cert.memories[0].clock, 0u)
      << "the memory landed on ordinal 0; this case exists to cover a NON-default "
         "commit domain, and a defaulted field would satisfy it silently";
  // The clock table the ordinal indexes into, so the row can be read.
  ASSERT_LT(cert.memories[0].clock, cert.clocks.size());
  write_scanner_dcert("LHD_D4_MEM_B_DCERT", cert);
  // The Lean literal for the SAME certificate, so the parity gate can compare
  // the two emitters on a multi-domain design rather than only on one-clock
  // production artifacts.
  if (const char* lit = std::getenv("LHD_D4_MEM_B_FIXTURE")) {
    std::ofstream lout(lit);
    emit_design_cert(design, cert, lout);
  }
}

TEST(DesignScanClocks, MemoryQuietDomainValueOracle) {
  auto g = build_mem_fixture("lgdb_clk_mem_oracle", /*read_port_clocked=*/true);
  DesignScan design;
  try {
    design = scan_design(*g, {});
  } catch (const std::exception& e) {
    GTEST_SKIP() << "fixture refused by the scanner: " << e.what();
  }
  const auto cert = build_certificate(design, {});
  ASSERT_GE(design.clocks.size(), 1u);

  const char* path = std::getenv("LHD_LEAN_MEM_FIXTURE");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_LEAN_MEM_FIXTURE to emit the Lean oracle";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);
  write_scanner_dcert("LHD_D4_MEM_DCERT", cert);
  // Record what the scan actually resolved, so the oracle's assertions can be
  // read against it rather than against what the fixture intended.
  out << "\n-- scan: " << design.clocks.size() << " clock(s), " << design.memories.size()
      << " memory/memories, " << cert.flops.size() << " certificate flop(s)\n";
  for (size_t i = 0; i < design.clocks.size(); ++i) {
    out << "--   clock " << i << " = " << design.clocks[i].name
        << (design.clocks[i].implicit ? " (implicit)" : "") << "\n";
  }
  ASSERT_EQ(design.clocks.size(), 2u);
  ASSERT_EQ(cert.memories.size(), 1u) << "the ROM should carry no MemoryDesc";
  ASSERT_EQ(cert.flops.size(), 2u) << "two synthetic synchronous-read registers";
  // R15: a synthetic read register inherits its PORT's domain. Assert it before
  // any value case, or the cases below would pass against a model that put both
  // registers on one clock.
  ASSERT_NE(cert.flops[0].clock, cert.flops[1].clock)
      << "the RAM and ROM read registers collapsed onto one domain";
  ASSERT_EQ(cert.memories[0].clock, cert.flops[0].clock)
      << "the RAM's image and its read register disagree on the domain";

  auto ordinal_of = [&](std::string_view name) -> size_t {
    for (size_t i = 0; i < design.inputs.size(); ++i) {
      if (design.inputs[i].name == name) {
        return i;
      }
    }
    ADD_FAILURE() << "no input " << name;
    return 0;
  };
  struct In { const char* name; int value; int width; };
  auto inputs = [&](std::initializer_list<In> set) {
    std::map<size_t, std::pair<int, int>> by;
    for (const auto& v : set) {
      by[ordinal_of(v.name)] = {v.value, v.width};
    }
    std::ostringstream o;
    for (size_t k = 0; k < design.inputs.size(); ++k) {
      const auto it = by.find(k);
      const int  w  = it != by.end() ? it->second.second : static_cast<int>(design.inputs[k].width);
      const int  v  = it != by.end() ? it->second.first : 0;
      o << (k ? ", " : "") << "mk_bv " << w << " " << v;
    }
    return o.str();
  };
  const auto ord_a = cert.memories[0].clock;  // clk_a
  auto       edges = [&](bool a, bool b) {
    std::ostringstream o;
    o << "#[";
    for (size_t c = 0; c < design.clocks.size(); ++c) {
      o << (c ? ", " : "") << ((c == ord_a ? a : b) ? "true" : "false");
    }
    o << "]";
    return o.str();
  };
  // Pre-state: both read registers hold 99; the RAM image is all 7s.
  const char* st = "(⟨#[mk_bv 8 99, mk_bv 8 99], #[fun _ => mk_bv 8 7]⟩ : RuntimeState)";
  const auto  wr = inputs({{"we", 1, 1}, {"re", 1, 1}, {"waddr", 2, 2}, {"wdata", 55, 8},
                           {"raddr", 2, 2}, {"romaddr", 1, 2}});

  out << "\n-- MEMORY VALUE GATES.\n";
  out << "-- A write commits on the MEMORY's clock; a synchronous read is a\n"
      << "-- REGISTER that commits on its PORT's clock. Both are invisible while\n"
      << "-- every domain fires, which is why they are only testable now.\n";

  // 1. clk_a QUIET: the write must NOT commit, at ANY address.
  out << "\n-- clk_a quiet: the write does not commit. Checked at EVERY address.\n";
  for (int a = 0; a < 4; ++a) {
    out << "example : ((compileAndRun memfix_designCert " << edges(false, true) << " #[" << wr << "] " << st
        << ").nextState.mems[0]! (" << a << " : Int)) = mk_bv 8 7 := by native_decide\n";
  }
  // 2. clk_a FIRES: the write lands at waddr=2 and nowhere else.
  out << "\n-- clk_a fires: the write lands at address 2, and ONLY there.\n";
  for (int a = 0; a < 4; ++a) {
    out << "example : ((compileAndRun memfix_designCert " << edges(true, false) << " #[" << wr << "] " << st
        << ").nextState.mems[0]! (" << a << " : Int)) = mk_bv 8 " << (a == 2 ? 55 : 7)
        << " := by native_decide\n";
  }
  // 3. the read REGISTERS, independently per domain.
  out << "\n-- Read registers: each commits on its own port's clock.\n"
      << "-- clk_a quiet, clk_b fires: the RAM register HOLDS 99, the ROM register loads.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(false, true) << " #[" << wr << "] " << st
      << ").nextState.flops[0]!) = mk_bv 8 99 := by native_decide\n"
      << "-- clk_a fires, clk_b quiet: the ROM register HOLDS 99.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, false) << " #[" << wr << "] " << st
      << ").nextState.flops[1]!) = mk_bv 8 99 := by native_decide\n"
      << "-- both quiet: neither moves.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(false, false) << " #[" << wr << "] " << st
      << ").nextState.flops[0]!) = mk_bv 8 99 := by native_decide\n"
      << "example : ((compileAndRun memfix_designCert " << edges(false, false) << " #[" << wr << "] " << st
      << ").nextState.flops[1]!) = mk_bv 8 99 := by native_decide\n";
  // 4. a disabled write does not commit even when its clock fires.
  const auto nowr = inputs({{"we", 0, 1}, {"re", 1, 1}, {"waddr", 2, 2}, {"wdata", 55, 8},
                            {"raddr", 2, 2}, {"romaddr", 1, 2}});
  out << "\n-- enable off with the clock FIRING: still no commit.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << nowr << "] " << st
      << ").nextState.mems[0]! (2 : Int)) = mk_bv 8 7 := by native_decide\n";

  // 5. the CHECKED API on memory shapes.
  out << "\n-- checkRuntime rejects a state whose MEMORY count disagrees.\n"
      << "example : (checkRuntime memfix_designCert " << edges(true, true) << " #[" << wr
      << "] (⟨#[mk_bv 8 99, mk_bv 8 99], #[]⟩ : RuntimeState)).isOk = false := by native_decide\n";
  out << "\n-- checkDesign refuses a memory on an undeclared clock ordinal.\n"
      << "def memfix_badMemClock : DesignCert :=\n"
      << "  { memfix_designCert with\n"
      << "    memories := memfix_designCert.memories.map (fun m => { m with clock := 99 }) }\n"
      << "example : (designErrors memfix_badMemClock).map SimError.tag"
         " = some \"memClockOutOfRange\" := by native_decide\n";

  // 6. allEdges agrees with the preserved interpreter on THIS artifact too.
  out << "\n-- allEdges is every domain firing, and the emitted step matches the\n"
      << "-- preserved interpreter on a design with memory.\n"
      << "example : allEdges memfix_designCert = " << edges(true, true) << " := by native_decide\n"
      << "example : memfix_step #[" << wr << "] " << st
      << " = interpretDesignLegacy memfix_designCert #[" << wr << "] " << st
      << " := memfix_step_legacy_correct _ _\n";

  // -- DIRECT versus COMPILED ----------------------------------------------
  //
  // `diffStep` runs the CHECKED direct interpreter against the compiled
  // residual and reports the first disagreement. Until now the `Direct*`
  // modules had never been exercised on a scanner-produced design at all
  // (R14.2), so this is the first time the two implementations are compared on
  // one.
  //
  // DISTINGUISHABLE CELLS. The RAM starts at [11,22,33,44] rather than a
  // uniform fill, so a read wired to the wrong address is observable; a uniform
  // image would agree on both sides while being read from the wrong place.
  const char* dst = "(⟨#[mk_bv 8 99, mk_bv 8 99], "
                    "#[fun a => if a = 0 then mk_bv 8 11 else if a = 1 then mk_bv 8 22 "
                    "else if a = 2 then mk_bv 8 33 else mk_bv 8 44]⟩ : RuntimeState)";
  out << "\n-- direct vs compiled, every edge vector, sampling EVERY tiny address.\n";
  for (int ea = 0; ea < 2; ++ea) {
    for (int eb = 0; eb < 2; ++eb) {
      out << "example : diffStep memfix_designCert memfix_residual " << edges(ea != 0, eb != 0) << " #[" << wr
          << "] " << dst << " [0,1,2,3] = none := by native_decide\n";
    }
  }
  // ...and with the write disabled, so a spurious commit on either side shows.
  out << "example : diffStep memfix_designCert memfix_residual " << edges(true, true) << " #[" << nowr << "] "
      << dst << " [0,1,2,3] = none := by native_decide\n";

  // EXPECTED LOADED VALUES, not just agreement. Two implementations can agree
  // and both be wrong; these pin what the registers must actually hold.
  const auto rd3 = inputs({{"we", 0, 1}, {"re", 1, 1}, {"waddr", 0, 2}, {"wdata", 0, 8},
                           {"raddr", 3, 2}, {"romaddr", 2, 2}});
  out << "\n-- The RAM read register LOADS the addressed cell (raddr=3 -> 44),\n"
      << "-- and the ROM register loads its entry (romaddr=2 -> 3).\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << rd3 << "] " << dst
      << ").nextState.flops[0]!) = mk_bv 8 44 := by native_decide\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << rd3 << "] " << dst
      << ").nextState.flops[1]!) = mk_bv 8 3 := by native_decide\n";
  const auto rd1 = inputs({{"we", 0, 1}, {"re", 1, 1}, {"waddr", 0, 2}, {"wdata", 0, 8},
                           {"raddr", 1, 2}, {"romaddr", 0, 2}});
  out << "-- a DIFFERENT address loads a different cell -- the read is really wired.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << rd1 << "] " << dst
      << ").nextState.flops[0]!) = mk_bv 8 22 := by native_decide\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << rd1 << "] " << dst
      << ").nextState.flops[1]!) = mk_bv 8 1 := by native_decide\n";

  // MULTI-STEP TRACE. `fwd` is 0, so a read on the SAME step as a write to the
  // same address sees the OLD contents, and the new value only appears on the
  // following step. One step cannot distinguish that from a broken write.
  const auto w2 = inputs({{"we", 1, 1}, {"re", 1, 1}, {"waddr", 1, 2}, {"wdata", 77, 8},
                          {"raddr", 1, 2}, {"romaddr", 0, 2}});
  out << "\n-- No forwarding: the committing step reads the OLD cell (22), and the\n"
      << "-- new value (77) appears only on the NEXT read.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << w2 << "] " << dst
      << ").nextState.flops[0]!) = mk_bv 8 22 := by native_decide\n"
      << "def memfix_afterWrite : RuntimeState :=\n"
      << "  (compileAndRun memfix_designCert " << edges(true, true) << " #[" << w2 << "] " << dst
      << ").nextState\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << rd1
      << "] memfix_afterWrite).nextState.flops[0]!) = mk_bv 8 77 := by native_decide\n";
  out << "\n-- ...and direct and compiled agree across that whole two-step trace.\n"
      << "example : diffTrace memfix_designCert memfix_residual [0,1,2,3] " << dst << "\n"
      << "            [⟨" << edges(true, true) << ", #[" << w2 << "]⟩,\n"
      << "             ⟨" << edges(true, true) << ", #[" << rd1 << "]⟩] 0 = none := by native_decide\n";
  ASSERT_TRUE(out.good());
}

// BYTE-ENABLE and FORWARDING, which the single-lane fixture above does not
// reach. `memory_lowering.cpp` emits `MemWriteBE` once `wensize > 1` and resizes
// the enable to one bit per lane, so a selective write is a different operator,
// not a different argument -- and an untested one until now.
TEST(DesignScanClocks, ByteEnableAndForwardingValueOracle) {
  // 8-bit cells, FOUR 2-bit lanes, and forwarding ON so a same-step read of the
  // written address sees the new data.
  auto g = build_mem_fixture("lgdb_clk_mem_be", /*read_port_clocked=*/true,
                             /*wensize=*/4, /*fwd=*/1);
  DesignScan design;
  try {
    design = scan_design(*g, {});
  } catch (const std::exception& e) {
    GTEST_SKIP() << "fixture refused by the scanner: " << e.what();
  }
  const auto cert = build_certificate(design, {});
  // The POINT of this fixture: a different operator must have been selected.
  bool has_be = false;
  for (const auto& n : cert.nodes) {
    has_be |= n.op.kind == Operation::MemWriteBE;
  }
  ASSERT_TRUE(has_be) << "wensize=4 did not lower to MemWriteBE; the fixture tests nothing";

  const char* path = std::getenv("LHD_LEAN_BE_FIXTURE");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_LEAN_BE_FIXTURE to emit the Lean oracle";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);
  out << "\n-- scan: " << design.clocks.size() << " clock(s), wensize=4, fwd=1\n";

  auto ordinal_of = [&](std::string_view name) -> size_t {
    for (size_t i = 0; i < design.inputs.size(); ++i) {
      if (design.inputs[i].name == name) {
        return i;
      }
    }
    ADD_FAILURE() << "no input " << name;
    return 0;
  };
  struct In { const char* name; int value; int width; };
  auto inputs = [&](std::initializer_list<In> set) {
    std::map<size_t, std::pair<int, int>> by;
    for (const auto& v : set) {
      by[ordinal_of(v.name)] = {v.value, v.width};
    }
    std::ostringstream o;
    for (size_t k = 0; k < design.inputs.size(); ++k) {
      const auto it = by.find(k);
      const int  w  = it != by.end() ? it->second.second : static_cast<int>(design.inputs[k].width);
      const int  v  = it != by.end() ? it->second.first : 0;
      o << (k ? ", " : "") << "mk_bv " << w << " " << v;
    }
    return o.str();
  };
  const auto ord_a = cert.memories.empty() ? 0u : cert.memories[0].clock;
  auto       edges = [&](bool a, bool b) {
    std::ostringstream o;
    o << "#[";
    for (size_t c = 0; c < design.clocks.size(); ++c) {
      o << (c ? ", " : "") << ((c == ord_a ? a : b) ? "true" : "false");
    }
    o << "]";
    return o.str();
  };
  // Cell 1 holds 0xFF; a selective write of 0x00 must clear only the enabled
  // lanes. With four 2-bit lanes, enable 0b0011 touches the LOW four bits.
  const char* st = "(⟨#[mk_bv 8 99, mk_bv 8 99], #[fun _ => mk_bv 8 255]⟩ : RuntimeState)";
  auto wr = [&](int en, int data) {
    return inputs({{"we", en, 4}, {"re", 1, 1}, {"waddr", 1, 2}, {"wdata", data, 8},
                   {"raddr", 1, 2}, {"romaddr", 0, 2}});
  };
  out << "\n-- BYTE ENABLES: four 2-bit lanes over an 8-bit cell holding 0xFF.\n";
  out << "-- all lanes enabled: the whole cell is written.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << wr(0xF, 0)
      << "] " << st << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 0 := by native_decide\n";
  out << "-- NO lane enabled: nothing is written, even with the clock firing.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << wr(0x0, 0)
      << "] " << st << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 255 := by native_decide\n";
  out << "-- the LOW two lanes only (0b0011): the high nibble must survive.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << wr(0x3, 0)
      << "] " << st << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 240 := by native_decide\n";
  out << "-- the HIGH two lanes only (0b1100): the low nibble must survive.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << wr(0xC, 0)
      << "] " << st << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 15 := by native_decide\n";
  out << "-- a selective write leaves OTHER addresses alone.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << wr(0xF, 0)
      << "] " << st << ").nextState.mems[0]! (2 : Int)) = mk_bv 8 255 := by native_decide\n";
  out << "\n-- ...and a QUIET clock still writes nothing, whatever the lanes say.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(false, true) << " #[" << wr(0xF, 0)
      << "] " << st << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 255 := by native_decide\n";

  // FORWARDING: fwd=1 means read port 0 sees write port 0's new data on a
  // same-address collision. Contrast with the fwd=0 fixture, where the same
  // step reads the OLD cell.
  out << "\n-- FORWARDING (fwd=1): a same-address read on the WRITING step sees the\n"
      << "-- NEW data. The fwd=0 fixture reads the old cell on that step.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << wr(0xF, 0)
      << "] " << st << ").nextState.flops[0]!) = mk_bv 8 0 := by native_decide\n";
  const auto other = inputs({{"we", 0xF, 4}, {"re", 1, 1}, {"waddr", 1, 2}, {"wdata", 0, 8},
                             {"raddr", 2, 2}, {"romaddr", 0, 2}});
  out << "-- a DIFFERENT address is not forwarded: it reads the committed cell.\n"
      << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << other << "] " << st
      << ").nextState.flops[0]!) = mk_bv 8 255 := by native_decide\n";

  // PER-LANE REGRESSION. The paired masks above (0b0011 / 0b1100) cannot catch a
  // SWAP of two lanes inside one nibble: both lanes move together, so exchanging
  // them leaves the nibble-level result identical. A value that differs in every
  // 2-bit lane, written over a value that differs in every lane, pins each lane
  // individually.
  //
  //   old 0x6C = 01 10 11 00      new 0xB2 = 10 11 00 10
  //
  // Lane k (k=0 lowest) enabled => bits [2k+1:2k] come from `new`.
  const char* st6c = "(⟨#[mk_bv 8 99, mk_bv 8 99], #[fun _ => mk_bv 8 108]⟩ : RuntimeState)";
  auto wrb2 = [&](int en) {
    return inputs({{"we", en, 4}, {"re", 1, 1}, {"waddr", 1, 2}, {"wdata", 0xB2, 8},
                   {"raddr", 1, 2}, {"romaddr", 0, 2}});
  };
  out << "\n-- PER-LANE table: 0x6C overwritten by 0xB2, one lane at a time.\n"
      << "-- Every 2-bit lane differs, so a lane swap inside a nibble shows here\n"
      << "-- and cannot show in the 0b0011 / 0b1100 pair above.\n";
  const int old_v = 0x6C, new_v = 0xB2;
  for (int en = 0; en < 16; ++en) {
    int expect = 0;
    for (int lane = 0; lane < 4; ++lane) {
      const int mask = 0x3 << (2 * lane);
      expect |= ((en >> lane) & 1 ? new_v : old_v) & mask;
    }
    out << "example : ((compileAndRun memfix_designCert " << edges(true, true) << " #[" << wrb2(en) << "] "
        << st6c << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 " << expect << " := by native_decide\n";
  }

  out << "\n-- direct vs compiled over the byte-enable cases, every tiny address.\n";
  for (int en : {0x0, 0x3, 0xC, 0xF}) {
    out << "example : diffStep memfix_designCert memfix_residual " << edges(true, true) << " #[" << wr(en, 0)
        << "] " << st << " [0,1,2,3] = none := by native_decide\n";
  }
  ASSERT_TRUE(out.good());
}

// ---------------------------------------------------------------------------
// IO/clock SIDECAR.
//
// A DesignCert names its inputs by ORDINAL and nothing else, so a caller holding
// an RTL port list cannot build the input array from it. The sidecar is that
// mapping, derived from the SAME owned scan and certificate the emitter formats.
// ---------------------------------------------------------------------------

// Ordinals and GAPS survive, and a port the model never reads is still listed.
TEST(DesignIoMeta, PreservesOrdinalsGapsAndIgnoredPorts) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_io_meta_gaps");
  auto  io  = lib.create_io("gapfix");
  // `mid` and `tail` are declared and never read: one INTERIOR, one TRAILING.
  // Compacting the list would renumber `c` and misalign every later argument.
  io->add_input("a", 1);
  io->set_bits("a", 8);
  io->add_input("mid", 2);
  io->set_bits("mid", 4);
  io->add_input("c", 3);
  io->set_bits("c", 8);
  io->add_input("tail", 4);
  io->set_bits("tail", 2);
  io->add_output("z", 1);
  io->set_bits("z", 8);
  auto g  = io->create_graph();
  auto op = create_typed_node(*g, Ntype_op::Or);
  set_bits(op.create_driver_pin(0), 8);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("c").connect_sink(op.create_sink_pin(1));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("z"));

  const auto design = scan_design(*g, {});
  const auto cert   = build_certificate(design, {});
  const auto meta   = build_io_meta(design, cert);

  ASSERT_EQ(meta.inputs.size(), 4u) << "an unread port was dropped from the sidecar";
  std::map<std::string, IoPortMeta> by;
  for (const auto& p : meta.inputs) {
    by[p.name] = p;
  }
  EXPECT_TRUE(by.at("a").used_by_model);
  EXPECT_TRUE(by.at("c").used_by_model);
  EXPECT_FALSE(by.at("mid").used_by_model) << "an interior unused port claims to be read";
  EXPECT_FALSE(by.at("tail").used_by_model);
  // Widths are the DECLARED ones, including for ports the model ignores.
  EXPECT_EQ(by.at("mid").width, 4u);
  EXPECT_EQ(by.at("tail").width, 2u);
  // Ordinals are the RUNTIME index, dense and strictly increasing, and the list
  // is not compacted: every declared port keeps its slot whether or not the
  // model reads it. Dropping `mid` would shift `c` and `tail` down one.
  for (size_t i = 0; i < meta.inputs.size(); ++i) {
    EXPECT_EQ(meta.inputs[i].ordinal, i) << "ordinals are not the runtime index";
  }
  // `c` is read, so arity must reach past it; `tail` is not, so arity need not
  // reach the port COUNT.
  EXPECT_GE(meta.runtime_input_arity, by.at("c").ordinal + 1);
  EXPECT_LE(meta.runtime_input_arity, meta.inputs.size());
  EXPECT_EQ(meta.outputs.size(), 1u);
  EXPECT_EQ(meta.outputs[0].name, "z");
}

// Proof tools name a design's registers through the sidecar: one entry per
// CERTIFICATE flop, in the runtime state order, with the scan's register name.
// An entry out of order would attach a fact about one register to another.
TEST(DesignIoMeta, FlopsAreNamedInRuntimeOrder) {
  const std::vector<ResetFlopSpec> specs{{"clk", false, false, 3, false}, {"clk", true, false, 5, true}};
  auto                             g      = build_reset_flops("lgdb_io_meta_flops", specs);
  const auto                       design = scan_design(*g, {});
  const auto                       cert   = build_certificate(design, {});
  const auto                       meta   = build_io_meta(design, cert);

  ASSERT_EQ(meta.flops.size(), cert.flops.size());
  ASSERT_EQ(meta.flops.size(), 2u);
  std::map<uint32_t, std::string> raw;
  for (const auto& f : design.flops) {
    raw[f.id] = f.raw_name;
  }
  std::set<std::string> names;
  for (size_t k = 0; k < meta.flops.size(); ++k) {
    EXPECT_EQ(meta.flops[k].ordinal, k) << "flop ordinals are not the runtime index";
    EXPECT_EQ(meta.flops[k].width, cert.flops[k].width);
    EXPECT_EQ(meta.flops[k].name, raw.at(cert.flops[k].origin)) << "a name is attached to the wrong flop";
    EXPECT_FALSE(meta.flops[k].name.empty());
    names.insert(meta.flops[k].name);
  }
  EXPECT_EQ(names.size(), meta.flops.size()) << "two flops share a name";
  const auto json = io_meta_json(meta);
  EXPECT_NE(json.find("\"flops\": ["), std::string::npos);
  EXPECT_NE(json.find(meta.flops[1].name), std::string::npos);
}

// A RESET-ONLY port is referenced through `flopQAsync` and is never an `.input`
// source. Computing arity from inputs alone makes it too small, and
// `checkRuntime` then rejects an array the caller built correctly.
TEST(DesignIoMeta, ArityCountsResetOnlyInputs) {
  const std::vector<ResetFlopSpec> specs{{"clk", true, false, 7, false}};
  auto                             g      = build_reset_flops("lgdb_io_meta_reset", specs);
  const auto                       design = scan_design(*g, {});
  const auto                       cert   = build_certificate(design, {});
  const auto                       meta   = build_io_meta(design, cert);

  // `rst` drives only the flop's reset pin; no node reads it.
  const IoPortMeta* rst = nullptr;
  for (const auto& p : meta.inputs) {
    if (p.name == "rst") {
      rst = &p;
    }
  }
  ASSERT_NE(rst, nullptr);
  EXPECT_TRUE(rst->used_by_model) << "a reset-only port was reported as ignored";
  EXPECT_GE(meta.runtime_input_arity, rst->ordinal + 1)
      << "arity excludes the reset input; checkRuntime would reject a correct array";
}

// An implicitly clocked design still names its domain.
TEST(DesignIoMeta, ImplicitOnlyDesignNamesItsClock) {
  auto       f      = build_flops("lgdb_io_meta_implicit", {""});
  const auto design = scan_design(*f.g, {});
  const auto cert   = build_certificate(design, {});
  const auto meta   = build_io_meta(design, cert);
  ASSERT_EQ(meta.clocks.size(), 1u);
  EXPECT_TRUE(meta.clocks[0].implicit);
  EXPECT_EQ(meta.clocks[0].ordinal, 0u);
  EXPECT_FALSE(meta.clocks[0].name.empty());
}

// A FIELD/ESCAPE DECODER, not a JSON document parser.
//
// It finds `"key": "..."` by substring and decodes that one string's escapes. It
// does not validate the document, does not understand nesting, and would not
// notice a structural error elsewhere. That is enough for what it is for --
// proving an escaped byte ROUND TRIPS, where searching for `\u0001` in the text
// only proves something was written -- and the whole-document check is a
// separate gate: `scripts/check_io_sidecar.py` parses a real emitted sidecar
// with a real JSON parser.
std::optional<std::string> json_string_field(std::string_view json, std::string_view key) {
  const std::string pat = "\"" + std::string(key) + "\": \"";
  const auto        at  = json.find(pat);
  if (at == std::string_view::npos) {
    return std::nullopt;
  }
  std::string out;
  for (size_t i = at + pat.size(); i < json.size(); ++i) {
    const char c = json[i];
    if (c == '"') {
      return out;
    }
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (++i >= json.size()) {
      return std::nullopt;  // truncated escape
    }
    switch (json[i]) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'u': {
        if (i + 4 >= json.size()) {
          return std::nullopt;
        }
        const auto hex = std::string(json.substr(i + 1, 4));
        out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
        i += 4;
        break;
      }
      default: return std::nullopt;  // an escape no consumer would accept
    }
  }
  return std::nullopt;  // unterminated string
}

// Control bytes and quotes reach this from RTL names and from `net_key()`'s
// \x01/\x02 sentinels, so escaping is not theoretical. The round trip is what
// matters: a consumer must read back the byte that went in.
TEST(DesignIoMeta, JsonEscapesRoundTripThroughAParser) {
  DesignIoMeta meta;
  meta.top = "a\"b\\c\nd";
  meta.inputs.push_back({std::string("x\x01y"), 0, 1, true});
  meta.clocks.push_back({std::string("\x02""111"), 0, false});
  const auto json = io_meta_json(meta);

  // No raw control byte survives into the file.
  EXPECT_EQ(json.find('\x01'), std::string::npos);
  EXPECT_EQ(json.find('\x02'), std::string::npos);
  // ...and every value DECODES back to exactly what went in.
  const auto top = json_string_field(json, "top");
  ASSERT_TRUE(top.has_value()) << "`top` did not parse";
  EXPECT_EQ(*top, meta.top) << "quote/backslash/newline did not round trip";
  const auto kind = json_string_field(json, "kind");
  ASSERT_TRUE(kind.has_value());
  EXPECT_EQ(*kind, "lean_design_io") << "the schema discriminator is missing or wrong";
  // The port and clock names are the first `"name"` fields in their arrays.
  const auto pname = json_string_field(json, "name");
  ASSERT_TRUE(pname.has_value());
  EXPECT_EQ(*pname, std::string("x\x01y")) << "a control byte did not round trip";
  const auto clocks_at = json.find("\"clocks\"");
  ASSERT_NE(clocks_at, std::string::npos);
  const auto cname = json_string_field(std::string_view(json).substr(clocks_at), "name");
  ASSERT_TRUE(cname.has_value());
  EXPECT_EQ(*cname, std::string("\x02""111")) << "the net_key sentinel did not round trip";
}

// PUBLICATION FAULTS.
//
// The pair is only useful together, so a failure must leave the PREVIOUS pair
// intact. The first implementation's catch assumed both originals had been
// backed up by the time anything threw, and deleted both targets on the way to
// restoring them -- so a failure in the FIRST backup rename deleted an original
// that had never been moved. Measured on the committed source: original A lost
// and replaced by a directory, original B deleted outright.
//
// Every case below is a distinct point of failure, because the one failure the
// earlier test exercised (a throw during STAGING) happens before any target is
// touched and cannot catch this at all.

// Project-local, never `temp_directory_path()`: /tmp is shared between parallel
// jobs here and is not the configured scratch area.
std::filesystem::path pub_case_dir(std::string_view name) {
  const auto dir = std::filesystem::path(LIVEHD_TEST_RUNTIME_DIR) / "publication" / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::string slurp_or(const std::filesystem::path& p, const char* missing) {
  std::error_code ec;
  if (!std::filesystem::exists(p, ec)) {
    return missing;
  }
  if (!std::filesystem::is_regular_file(p, ec)) {
    return "<NOT-A-FILE>";
  }
  std::ifstream      in(p);
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

struct PubPaths {
  std::filesystem::path dir, a, b;
};
PubPaths pub_with_existing_pair(std::string_view name) {
  PubPaths p;
  p.dir = pub_case_dir(name);
  p.a   = p.dir / "cert.lean";
  p.b   = p.dir / "io.json";
  publish_pair(p.a.string(), [](std::ostream& o) { o << "OLD-CERT"; },
               p.b.string(), [](std::ostream& o) { o << "OLD-JSON"; });
  return p;
}
void expect_old_pair_intact(const PubPaths& p, const char* why) {
  EXPECT_EQ(slurp_or(p.a, "<MISSING>"), "OLD-CERT") << why << ": the previous certificate was lost";
  EXPECT_EQ(slurp_or(p.b, "<MISSING>"), "OLD-JSON") << why << ": the previous sidecar was lost";
}
void expect_no_litter(const PubPaths& p) {
  for (const char* suffix : {".tmp", ".bak"}) {
    EXPECT_FALSE(std::filesystem::exists(p.a.string() + suffix)) << p.a.string() << suffix << " left behind";
    EXPECT_FALSE(std::filesystem::exists(p.b.string() + suffix)) << p.b.string() << suffix << " left behind";
  }
}

// A BLOCKED BACKUP PATH is refused before anything is mutated, rather than
// failing halfway. This is the case that lost both originals.
TEST(DesignIoMeta, BlockedBackupAIsRefusedWithoutTouchingTheOriginals) {
  auto p = pub_with_existing_pair("blocked_backup_a");
  std::filesystem::create_directories(p.a.string() + ".bak");
  EXPECT_THROW(publish_pair(p.a.string(), [](std::ostream& o) { o << "NEW-CERT"; },
                            p.b.string(), [](std::ostream& o) { o << "NEW-JSON"; }),
               std::exception);
  expect_old_pair_intact(p, "blocked backup A");
  // The collision itself is left alone: it may hold the only copy of something.
  EXPECT_TRUE(std::filesystem::exists(p.a.string() + ".bak"));
}

TEST(DesignIoMeta, BlockedBackupBIsRefusedWithoutTouchingTheOriginals) {
  auto p = pub_with_existing_pair("blocked_backup_b");
  std::filesystem::create_directories(p.b.string() + ".bak");
  EXPECT_THROW(publish_pair(p.a.string(), [](std::ostream& o) { o << "NEW-CERT"; },
                            p.b.string(), [](std::ostream& o) { o << "NEW-JSON"; }),
               std::exception);
  expect_old_pair_intact(p, "blocked backup B");
  EXPECT_TRUE(std::filesystem::exists(p.b.string() + ".bak"));
}

// A FINAL RENAME failure -- the staged file vanishes between staging and
// publication -- must roll the pair back. This is the case that genuinely
// exercises the restore path.
TEST(DesignIoMeta, SecondFinalRenameFailureRestoresThePair) {
  auto p = pub_with_existing_pair("final_rename_b");
  EXPECT_THROW(publish_pair(p.a.string(), [](std::ostream& o) { o << "NEW-CERT"; },
                            p.b.string(),
                            [&](std::ostream& o) {
                              o << "NEW-JSON";
                              // Remove the staged file after writing it, so the
                              // rename that publishes it fails.
                              std::error_code ec;
                              std::filesystem::remove(p.b.string() + ".tmp", ec);
                            }),
               std::exception);
  expect_old_pair_intact(p, "second final rename");
  expect_no_litter(p);
}

TEST(DesignIoMeta, FirstFinalRenameFailureRestoresThePair) {
  auto p = pub_with_existing_pair("final_rename_a");
  EXPECT_THROW(publish_pair(p.a.string(),
                            [&](std::ostream& o) {
                              o << "NEW-CERT";
                              std::error_code ec;
                              std::filesystem::remove(p.a.string() + ".tmp", ec);
                            },
                            p.b.string(), [](std::ostream& o) { o << "NEW-JSON"; }),
               std::exception);
  expect_old_pair_intact(p, "first final rename");
  expect_no_litter(p);
}

// Staging failures, fresh targets, and the success path.
TEST(DesignIoMeta, StagingFailureAndFreshTargetsAndSuccess) {
  auto p = pub_with_existing_pair("staging_and_success");
  EXPECT_THROW(publish_pair(p.a.string(), [](std::ostream& o) { o << "NEW-CERT"; },
                            p.b.string(), [](std::ostream&) { throw std::runtime_error("sidecar failed"); }),
               std::exception);
  expect_old_pair_intact(p, "staging failure");
  expect_no_litter(p);

  // The same failure against FRESH targets must leave neither file behind.
  const auto c = (p.dir / "fresh.lean").string();
  const auto d = (p.dir / "fresh.json").string();
  EXPECT_THROW(publish_pair(c, [](std::ostream& o) { o << "C"; },
                            d, [](std::ostream&) { throw std::runtime_error("boom"); }),
               std::exception);
  EXPECT_FALSE(std::filesystem::exists(c)) << "a half-published pair was left on a fresh target";
  EXPECT_FALSE(std::filesystem::exists(d));

  // ...and a successful publish replaces both and cleans up.
  publish_pair(p.a.string(), [](std::ostream& o) { o << "NEW-CERT"; },
               p.b.string(), [](std::ostream& o) { o << "NEW-JSON"; });
  EXPECT_EQ(slurp_or(p.a, "<MISSING>"), "NEW-CERT");
  EXPECT_EQ(slurp_or(p.b, "<MISSING>"), "NEW-JSON");
  expect_no_litter(p);
}

// ---------------------------------------------------------------------------
// The PRODUCTION TRIPLE.
//
// `verified_compiler` mode now publishes three artifacts -- the Lean literal,
// the DCERT2 byte stream and the IO sidecar -- in one `publish_group`. The pair
// controls above still cover `publish_pair`, which is the two-element case and
// keeps its own contract; these run the same five failure shapes over three
// artifacts, and over EVERY slot rather than only the first and last, because
// a middle element is exactly what a hand-unrolled pair implementation gets
// wrong.
//
// None of this is a claim of crash atomicity: `publish_group` is N renames and
// documents that a crash between them leaves some targets new and some old.
// What is checked is the handled paths -- refusal, staging failure, rename
// failure, and success.
// ---------------------------------------------------------------------------
struct TriplePaths {
  std::filesystem::path              dir;
  std::vector<std::filesystem::path> files;
};
const char* const kOld3[] = {"OLD-CERT", "OLD-DCERT", "OLD-JSON"};
const char* const kNew3[] = {"NEW-CERT", "NEW-DCERT", "NEW-JSON"};

std::vector<PublishItem> triple_items(const TriplePaths& p, const char* const* text) {
  std::vector<PublishItem> items;
  for (size_t i = 0; i < p.files.size(); ++i) {
    const char* t = text[i];
    items.push_back({p.files[i].string(), [t](std::ostream& o) { o << t; }});
  }
  return items;
}

TriplePaths pub_with_existing_triple(std::string_view name) {
  TriplePaths p;
  p.dir   = pub_case_dir(name);
  p.files = {p.dir / "cert.lean", p.dir / "design.dcert", p.dir / "io.json"};
  publish_group(triple_items(p, kOld3));
  return p;
}
void expect_old_triple_intact(const TriplePaths& p, const std::string& why) {
  for (size_t i = 0; i < p.files.size(); ++i) {
    EXPECT_EQ(slurp_or(p.files[i], "<MISSING>"), kOld3[i]) << why << ": " << p.files[i] << " was lost";
  }
}
void expect_no_litter3(const TriplePaths& p) {
  for (const auto& f : p.files) {
    for (const char* suffix : {".tmp", ".bak"}) {
      EXPECT_FALSE(std::filesystem::exists(f.string() + suffix)) << f.string() << suffix << " left behind";
    }
  }
}

TEST(DesignIoMeta, TripleBlockedBackupInAnySlotIsRefusedUntouched) {
  for (size_t slot = 0; slot < 3; ++slot) {
    auto p = pub_with_existing_triple("triple_blocked_" + std::to_string(slot));
    std::filesystem::create_directories(p.files[slot].string() + ".bak");
    EXPECT_THROW(publish_group(triple_items(p, kNew3)), std::exception) << "slot " << slot;
    expect_old_triple_intact(p, "blocked backup slot " + std::to_string(slot));
    // The collision is left alone: it may hold the only copy of something.
    EXPECT_TRUE(std::filesystem::exists(p.files[slot].string() + ".bak"));
  }
}

TEST(DesignIoMeta, TripleFinalRenameFailureInAnySlotRestoresTheTriple) {
  for (size_t slot = 0; slot < 3; ++slot) {
    auto p     = pub_with_existing_triple("triple_rename_" + std::to_string(slot));
    auto items = triple_items(p, kNew3);
    const std::string staged = p.files[slot].string() + ".tmp";
    const char*       text   = kNew3[slot];
    // Remove the staged file after writing it, so ITS publishing rename fails.
    items[slot].emit = [staged, text](std::ostream& o) {
      o << text;
      std::error_code ec;
      std::filesystem::remove(staged, ec);
    };
    EXPECT_THROW(publish_group(items), std::exception) << "slot " << slot;
    expect_old_triple_intact(p, "final rename slot " + std::to_string(slot));
    expect_no_litter3(p);
  }
}

TEST(DesignIoMeta, TripleStagingFailureInAnySlotLeavesThePreviousTriple) {
  for (size_t slot = 0; slot < 3; ++slot) {
    auto p     = pub_with_existing_triple("triple_staging_" + std::to_string(slot));
    auto items = triple_items(p, kNew3);
    items[slot].emit = [](std::ostream&) { throw std::runtime_error("emit failed"); };
    EXPECT_THROW(publish_group(items), std::exception) << "slot " << slot;
    expect_old_triple_intact(p, "staging failure slot " + std::to_string(slot));
    expect_no_litter3(p);

    // The same failure against FRESH targets must leave nothing behind.
    TriplePaths fresh;
    fresh.dir   = p.dir;
    fresh.files = {p.dir / ("f" + std::to_string(slot) + ".lean"), p.dir / ("f" + std::to_string(slot) + ".dcert"),
                   p.dir / ("f" + std::to_string(slot) + ".json")};
    auto fitems      = triple_items(fresh, kNew3);
    fitems[slot].emit = [](std::ostream&) { throw std::runtime_error("emit failed"); };
    EXPECT_THROW(publish_group(fitems), std::exception);
    for (const auto& f : fresh.files) {
      EXPECT_FALSE(std::filesystem::exists(f)) << "a partially published triple was left at " << f;
    }
    expect_no_litter3(fresh);
  }
}

// Two artifacts published to the SAME path would back up over each other and
// leave whichever ran second, silently. It is a caller defect and is refused.
TEST(DesignIoMeta, TripleWithADuplicatePathIsRefused) {
  auto p  = pub_with_existing_triple("triple_duplicate");
  auto it = triple_items(p, kNew3);
  it[1].path = it[0].path;
  EXPECT_THROW(publish_group(it), std::exception);
  expect_old_triple_intact(p, "duplicate path");
  expect_no_litter3(p);
}

TEST(DesignIoMeta, TripleSuccessReplacesAllThreeAndCleansUp) {
  auto p = pub_with_existing_triple("triple_success");
  publish_group(triple_items(p, kNew3));
  for (size_t i = 0; i < p.files.size(); ++i) {
    EXPECT_EQ(slurp_or(p.files[i], "<MISSING>"), kNew3[i]);
  }
  expect_no_litter3(p);
}

// A serialiser that throws part way through is a STAGING failure like any
// other: the previous triple stays, and nothing is left behind. This is the
// production shape of the destructor defect -- `render_dcert` writing to an
// exception-enabled staged stream.
TEST(DesignIoMeta, TripleSurvivesASerialiserThatThrowsMidStream) {
  auto p     = pub_with_existing_triple("triple_mid_stream");
  auto items = triple_items(p, kNew3);
  items[1].emit = [](std::ostream& o) {
    o << "DCERT2\n0\n0\n";          // a plausible prefix, then a failure
    throw std::ios_base::failure("write failed");
  };
  EXPECT_THROW(publish_group(items), std::exception);
  expect_old_triple_intact(p, "serialiser threw mid stream");
  expect_no_litter3(p);
}

// The emitter takes a DesignScan and a CertificateIR -- no graph. Both outlive
// the graph by construction, which is what lets emission happen after the
// graph's lifetime ends.
TEST(DesignIoMeta, EmissionAndMetadataOutliveTheGraph) {
  DesignScan    design;
  CertificateIR cert;
  {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_io_meta_lifetime");
    auto  io  = lib.create_io("lifefix");
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
    // `g` goes out of scope here.
  }
  const auto meta = build_io_meta(design, cert);
  EXPECT_EQ(meta.top, "lifefix");
  EXPECT_EQ(meta.inputs.size(), 1u);
  std::ostringstream out;
  emit_design_cert(design, cert, out);
  EXPECT_NE(out.str().find("lifefix_designCert"), std::string::npos);
  EXPECT_FALSE(io_meta_json(meta).empty());
}

// RESTORATION FAILURE: the backup is the only remaining copy, so it must be
// kept and reported -- not cleaned up.
//
// This cannot be provoked through `publish_pair`: the window between moving an
// original aside and putting it back is entirely inside one call with no hook in
// it. The decision logic is therefore a function, and this drives it directly.
TEST(DesignIoMeta, FailedRestorationKeepsAndReportsTheBackup) {
  const auto dir = pub_case_dir("restore_failure");
  const auto a   = (dir / "cert.lean").string();
  const auto b   = (dir / "io.json").string();
  // The state after a failed publish that HAD moved both originals aside and
  // published neither.
  {
    std::ofstream(a + ".bak") << "OLD-CERT";
    std::ofstream(b + ".bak") << "OLD-JSON";
  }
  // Block A's restore with a NON-EMPTY directory: `rename` onto it fails, and
  // `remove` cannot clear it either.
  std::filesystem::create_directories(a);
  { std::ofstream(std::filesystem::path(a) / "blocker") << "x"; }

  const auto stranded = detail::rollback_pair(a, b, /*moved_a=*/true, /*moved_b=*/true,
                                              /*put_a=*/false, /*put_b=*/false);
  EXPECT_NE(stranded.find(a + ".bak"), std::string::npos)
      << "a backup that could not be restored was not reported";
  EXPECT_TRUE(std::filesystem::exists(a + ".bak"))
      << "the ONLY remaining copy of the previous artifact was deleted";
  {
    std::ifstream      in(a + ".bak");
    std::ostringstream o;
    o << in.rdbuf();
    EXPECT_EQ(o.str(), "OLD-CERT") << "the retained backup is not the original content";
  }
  // B's restore succeeded, so B is back and its backup is gone.
  EXPECT_TRUE(std::filesystem::exists(b));
  EXPECT_FALSE(std::filesystem::exists(b + ".bak"));
  EXPECT_EQ(stranded.find(b + ".bak"), std::string::npos);
}

// A certificate whose reset ordinal is referenced ONLY through `flopQAsync`.
//
// This is built by hand: it is NOT claimed that the scanner emits one.
// `build_certificate` sets `reset_pin` via `dep(*f.reset, 1)`, which also
// creates an Input source, so a scanned certificate has both. The point is that
// a certificate with only the `flopQAsync` reference is well formed, and an
// arity computed from `.input` sources alone is too small for it.
TEST(DesignIoMeta, ArityFromAResetOrdinalWithNoInputSource) {
  DesignScan    design;
  CertificateIR cert;
  design.name = "resetonly";
  design.inputs.push_back({"d", 0, 8, std::nullopt});
  design.inputs.push_back({"rst", 1, 1, std::nullopt});
  design.clocks.push_back({"\x01implicit", "clock", true, true});
  // The ONLY source: a flop Q whose async reset names ordinal 1. No `.input`
  // source mentions ordinal 1 at all.
  Source q;
  q.id          = 0;
  q.kind        = SourceKind::Flop;
  q.width       = 8;
  q.ordinal     = 0;
  q.async_reset = true;
  q.reset_input = 1;
  cert.sources.push_back(q);

  const auto meta = build_io_meta(design, cert);
  EXPECT_EQ(meta.runtime_input_arity, 2u)
      << "arity ignored a reset ordinal reachable only through flopQAsync";
  ASSERT_EQ(meta.inputs.size(), 2u);
  EXPECT_TRUE(meta.inputs[1].used_by_model) << "the reset-only ordinal was reported as ignored";
}



// WRITE-PORT COLLISION ORDERING, from the real scanner.
//
// Every memory fixture so far has ONE write port, so the `fwd` matrix -- whose
// bit (r * n_writes + w) says "read r sees write w's new data" -- was only ever
// exercised in its 1x1 case. Two writes and two reads is the smallest shape
// where two things become observable:
//
//   * what happens when both writes hit the SAME address in one step. The
//     chain applies writes in ordinal order, so the LATER port wins;
//   * that each read has its OWN forwarding row, so two reads of the same
//     address can legitimately disagree.
//
// `fwd = 0b0110` is chosen to make the second one visible: read 0 is forwarded
// write 1 only, read 1 is forwarded write 0 only.
// PER-READ FORWARDING ROWS, EXECUTED.  Two ASYNCHRONOUS read ports over one
// array and one write port, with `fwd = 0b10`: read 1's row forwards write 0,
// read 0's row does not.
//
// This is the case a whole-cell forwarding flag cannot express.  `fwd_nonzero`
// would say "this memory forwards" and give both ports the same value; the
// certificate chains `fwd_rows[r]`'s writes into the image port r reads, so the
// two ports must DISAGREE on the writing step.  Nothing structural can show
// that -- both ports lower to a `MemRead` either way -- so it is decided here,
// on values.
TEST(DesignScanClocks, PerReadRowForwardingValueOracle) {
  auto&    lib = livehd::Hhds_graph_library::instance("lgdb_clk_mem_rowfwd");
  auto     io  = lib.create_io("rowfix");
  uint32_t pid = 1;
  for (const char* n : {"clk", "we", "re0", "re1"}) {
    io->add_input(n, pid++);
    io->set_bits(n, 1);
  }
  for (const char* n : {"wa", "ra0", "ra1"}) {
    io->add_input(n, pid++);
    io->set_bits(n, 2);
  }
  io->add_input("wd", pid++);
  io->set_bits("wd", 8);
  io->add_output("rd0", 1);
  io->set_bits("rd0", 8);
  io->add_output("rd1", 2);
  io->set_bits("rd1", 8);
  auto       g       = io->create_graph();
  auto       mem     = create_typed_node(*g, Ntype_op::Memory);
  const auto mstride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  auto       sink    = [&](std::string_view n, uint32_t port) {
    return mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, n) + port * mstride);
  };
  auto konst = [&](std::string_view n, int64_t v, uint32_t port = 0) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(v)).connect_sink(sink(n, port));
  };
  konst("bits", 8);
  konst("size", 4);
  konst("type", 2);  // ASYNCHRONOUS reads: the value is combinational
  konst("wensize", 1);
  konst("posclk", 1);
  // fwd bit (r * n_writes + w) with ONE write port: bit 0 = read 0, bit 1 =
  // read 1. 0b10 forwards write 0 to read 1 ONLY.
  konst("fwd", 0b10);
  konst("rdport", 0, 0);
  konst("rdport", 1, 1);
  konst("rdport", 1, 2);
  g->get_input_pin("wa").connect_sink(sink("addr", 0));
  g->get_input_pin("wd").connect_sink(sink("din", 0));
  g->get_input_pin("we").connect_sink(sink("enable", 0));
  g->get_input_pin("clk").connect_sink(sink("clock_pin", 0));
  const char* raddr[] = {"ra0", "ra1"};
  const char* ren[]   = {"re0", "re1"};
  for (uint32_t r = 0; r < 2; ++r) {
    g->get_input_pin(raddr[r]).connect_sink(sink("addr", 1 + r));
    g->get_input_pin(ren[r]).connect_sink(sink("enable", 1 + r));
    set_bits(mem.create_driver_pin(1 + r), 8);
    mem.create_driver_pin(1 + r).connect_sink(g->get_output_pin(r == 0 ? "rd0" : "rd1"));
  }

  DesignScan design;
  try {
    design = scan_design(*g, {});
  } catch (const std::exception& e) {
    GTEST_SKIP() << "fixture refused by the scanner: " << e.what();
  }
  const auto cert = build_certificate(design, {});
  ASSERT_EQ(cert.memories.size(), 1u) << "one array, one state identity";
  size_t reads = 0, images = 0;
  for (const auto& n : cert.nodes) {
    reads += n.op.kind == Operation::MemRead;
  }
  for (const auto& src : cert.sources) {
    images += src.kind == SourceKind::MemImage;
  }
  ASSERT_EQ(reads, 2u) << "each read port lowers to its own MemRead";
  ASSERT_EQ(images, 1u) << "both ports read ONE image";
  ASSERT_TRUE(cert.flops.empty()) << "asynchronous reads are values, not registers";

  const char* path = std::getenv("LHD_LEAN_ROW_FIXTURE");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_LEAN_ROW_FIXTURE to emit the Lean oracle";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);

  auto ordinal_of = [&](std::string_view name) -> size_t {
    for (size_t i = 0; i < design.inputs.size(); ++i) {
      if (design.inputs[i].name == name) {
        return i;
      }
    }
    ADD_FAILURE() << "no input " << name;
    return 0;
  };
  struct In {
    const char* name;
    int         value;
  };
  auto inputs = [&](std::initializer_list<In> set) {
    std::map<size_t, int> by;
    for (const auto& v : set) {
      by[ordinal_of(v.name)] = v.value;
    }
    std::ostringstream o;
    for (size_t k = 0; k < design.inputs.size(); ++k) {
      const auto it = by.find(k);
      o << (k ? ", " : "") << "mk_bv " << design.inputs[k].width << " " << (it != by.end() ? it->second : 0);
    }
    return o.str();
  };
  // `design.outputs` is sorted by name, so rd0 is output 0 and rd1 output 1.
  ASSERT_EQ(design.outputs.size(), 2u);
  ASSERT_EQ(design.outputs[0].name, "rd0");
  ASSERT_EQ(design.outputs[1].name, "rd1");
  const std::string fire  = "#[true]";
  const std::string quiet = "#[false]";
  // Distinguishable cells, so a read from the wrong address shows.
  const char* st = "(⟨#[], #[fun a => if a = 0 then mk_bv 8 11 else if a = 1 then mk_bv 8 22 "
                   "else if a = 2 then mk_bv 8 33 else mk_bv 8 44]⟩ : RuntimeState)";
  // Write 77 to address 1; BOTH ports read address 1 on that same step.
  const auto writing = inputs({{"we", 1}, {"wa", 1}, {"wd", 77}, {"re0", 1}, {"re1", 1}, {"ra0", 1}, {"ra1", 1}});
  const auto quietw  = inputs({{"we", 0}, {"wa", 1}, {"wd", 77}, {"re0", 1}, {"re1", 1}, {"ra0", 1}, {"ra1", 1}});
  const auto elsewhere
      = inputs({{"we", 1}, {"wa", 1}, {"wd", 77}, {"re0", 1}, {"re1", 1}, {"ra0", 2}, {"ra1", 2}});
  auto run = [&](const std::string& edges, const std::string& in) {
    return "(compileAndRun rowfix_designCert " + edges + " #[" + in + "] " + std::string(st) + ")";
  };
  out << "\n-- PER-READ FORWARDING ROWS. fwd = 0b10 over one write port: read 1\n"
      << "-- forwards write 0, read 0 does not. On the WRITING step the two ports\n"
      << "-- must disagree -- a whole-cell forwarding flag would make them agree.\n";
  out << "example : " << run(fire, writing) << ".outputs[0]! = mk_bv 8 22 := by native_decide\n";
  out << "example : " << run(fire, writing) << ".outputs[1]! = mk_bv 8 77 := by native_decide\n";
  out << "\n-- A DISABLED write forwards nothing: both ports read the committed cell.\n";
  out << "example : " << run(fire, quietw) << ".outputs[0]! = mk_bv 8 22 := by native_decide\n";
  out << "example : " << run(fire, quietw) << ".outputs[1]! = mk_bv 8 22 := by native_decide\n";
  out << "\n-- A DIFFERENT address is not forwarded even on the forwarding port.\n";
  out << "example : " << run(fire, elsewhere) << ".outputs[0]! = mk_bv 8 33 := by native_decide\n";
  out << "example : " << run(fire, elsewhere) << ".outputs[1]! = mk_bv 8 33 := by native_decide\n";
  out << "\n-- CLOCKED NEXT STATE: the write commits on the edge...\n";
  out << "example : " << run(fire, writing) << ".nextState.mems[0]! (1 : Int) = mk_bv 8 77 := by native_decide\n";
  out << "-- ...and an untouched address is preserved.\n";
  out << "example : " << run(fire, writing) << ".nextState.mems[0]! (2 : Int) = mk_bv 8 33 := by native_decide\n";
  out << "-- A QUIET clock commits nothing, however the write port is driven.\n";
  out << "example : " << run(quiet, writing) << ".nextState.mems[0]! (1 : Int) = mk_bv 8 22 := by native_decide\n";
  out << "-- A DISABLED write commits nothing even on a firing edge.\n";
  out << "example : " << run(fire, quietw) << ".nextState.mems[0]! (1 : Int) = mk_bv 8 22 := by native_decide\n";
}

TEST(DesignScanClocks, TwoWriteTwoReadCollisionValueOracle) {
  auto&    lib = livehd::Hhds_graph_library::instance("lgdb_clk_mem_collide");
  auto     io  = lib.create_io("colfix");
  uint32_t pid = 1;
  for (const char* n : {"clk", "we0", "we1", "re0", "re1"}) {
    io->add_input(n, pid++);
    io->set_bits(n, 1);
  }
  for (const char* n : {"wa0", "wa1", "ra0", "ra1"}) {
    io->add_input(n, pid++);
    io->set_bits(n, 2);
  }
  for (const char* n : {"wd0", "wd1"}) {
    io->add_input(n, pid++);
    io->set_bits(n, 8);
  }
  io->add_output("rd0", 1);
  io->set_bits("rd0", 8);
  io->add_output("rd1", 2);
  io->set_bits("rd1", 8);
  auto       g       = io->create_graph();
  auto       mem     = create_typed_node(*g, Ntype_op::Memory);
  const auto mstride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  auto       sink    = [&](std::string_view n, uint32_t port) {
    return mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, n) + port * mstride);
  };
  auto konst = [&](std::string_view n, int64_t v, uint32_t port = 0) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(v)).connect_sink(sink(n, port));
  };
  konst("bits", 8);
  konst("size", 4);
  konst("type", 1);     // synchronous reads
  konst("wensize", 1);
  konst("posclk", 1);
  // fwd bit (r * n_writes + w): read 0 <- write 1, read 1 <- write 0.
  konst("fwd", 0b0110);
  // ports 0,1 = writes; ports 2,3 = synchronous reads.
  konst("rdport", 0, 0);
  konst("rdport", 0, 1);
  konst("rdport", 1, 2);
  konst("rdport", 1, 3);
  const char* waddr[] = {"wa0", "wa1"};
  const char* wdata[] = {"wd0", "wd1"};
  const char* wen[]   = {"we0", "we1"};
  for (uint32_t w = 0; w < 2; ++w) {
    g->get_input_pin(waddr[w]).connect_sink(sink("addr", w));
    g->get_input_pin(wdata[w]).connect_sink(sink("din", w));
    g->get_input_pin(wen[w]).connect_sink(sink("enable", w));
    g->get_input_pin("clk").connect_sink(sink("clock_pin", w));
  }
  const char* raddr[] = {"ra0", "ra1"};
  const char* ren[]   = {"re0", "re1"};
  for (uint32_t r = 0; r < 2; ++r) {
    g->get_input_pin(raddr[r]).connect_sink(sink("addr", 2 + r));
    g->get_input_pin(ren[r]).connect_sink(sink("enable", 2 + r));
    g->get_input_pin("clk").connect_sink(sink("clock_pin", 2 + r));
    mem.create_driver_pin(2 + r).connect_sink(g->get_output_pin(r == 0 ? "rd0" : "rd1"));
    set_bits(mem.create_driver_pin(2 + r), 8);
  }

  DesignScan design;
  try {
    design = scan_design(*g, {});
  } catch (const std::exception& e) {
    GTEST_SKIP() << "fixture refused by the scanner: " << e.what();
  }
  const auto cert = build_certificate(design, {});
  ASSERT_EQ(cert.memories.size(), 1u);
  // TWO synthetic read registers. If the scan produced one, the per-read
  // forwarding rows below would be testing a single shared value.
  ASSERT_EQ(cert.flops.size(), 2u) << "expected one read register per synchronous read port";

  const char* path = std::getenv("LHD_LEAN_COLLIDE_FIXTURE");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_LEAN_COLLIDE_FIXTURE to emit the Lean oracle";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);

  auto ordinal_of = [&](std::string_view name) -> size_t {
    for (size_t i = 0; i < design.inputs.size(); ++i) {
      if (design.inputs[i].name == name) {
        return i;
      }
    }
    ADD_FAILURE() << "no input " << name;
    return 0;
  };
  struct In { const char* name; int value; };
  auto inputs = [&](std::initializer_list<In> set) {
    std::map<size_t, int> by;
    for (const auto& v : set) {
      by[ordinal_of(v.name)] = v.value;
    }
    std::ostringstream o;
    for (size_t k = 0; k < design.inputs.size(); ++k) {
      const auto it = by.find(k);
      o << (k ? ", " : "") << "mk_bv " << design.inputs[k].width << " " << (it != by.end() ? it->second : 0);
    }
    return o.str();
  };
  const std::string fire  = "#[true]";
  const std::string quiet = "#[false]";
  // Distinguishable cells, so a read from the wrong address shows.
  const char* st = "(⟨#[mk_bv 8 99, mk_bv 8 99], "
                   "#[fun a => if a = 0 then mk_bv 8 11 else if a = 1 then mk_bv 8 22 "
                   "else if a = 2 then mk_bv 8 33 else mk_bv 8 44]⟩ : RuntimeState)";

  out << "\n-- COLLISION: both writes target address 1 in one step.\n"
      << "-- The chain applies writes in ORDINAL order, so write 1 (the later\n"
      << "-- port) wins. If ordering were reversed the cell would read 55.\n";
  const auto both = inputs({{"we0", 1}, {"we1", 1}, {"wa0", 1}, {"wa1", 1},
                            {"wd0", 55}, {"wd1", 77}, {"re0", 1}, {"re1", 1},
                            {"ra0", 1}, {"ra1", 1}});
  out << "example : ((compileAndRun colfix_designCert " << fire << " #[" << both << "] " << st
      << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 77 := by native_decide\n";
  out << "-- only the LOWER-priority write enabled: its value stands.\n";
  const auto only0 = inputs({{"we0", 1}, {"we1", 0}, {"wa0", 1}, {"wa1", 1},
                             {"wd0", 55}, {"wd1", 77}, {"re0", 1}, {"re1", 1},
                             {"ra0", 1}, {"ra1", 1}});
  out << "example : ((compileAndRun colfix_designCert " << fire << " #[" << only0 << "] " << st
      << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 55 := by native_decide\n";
  out << "-- DIFFERENT addresses: both land, neither overwrites the other.\n";
  const auto split = inputs({{"we0", 1}, {"we1", 1}, {"wa0", 0}, {"wa1", 2},
                             {"wd0", 55}, {"wd1", 77}, {"re0", 1}, {"re1", 1},
                             {"ra0", 0}, {"ra1", 2}});
  for (int a = 0; a < 4; ++a) {
    const int want = a == 0 ? 55 : (a == 2 ? 77 : (a == 1 ? 22 : 44));
    out << "example : ((compileAndRun colfix_designCert " << fire << " #[" << split << "] " << st
        << ").nextState.mems[0]! (" << a << " : Int)) = mk_bv 8 " << want << " := by native_decide\n";
  }

  out << "\n-- PER-READ FORWARDING ROWS. fwd = 0b0110: read 0 sees write 1 only,\n"
      << "-- read 1 sees write 0 only. Both read address 1, where both writes\n"
      << "-- land -- so the two reads must DISAGREE, and a single shared row\n"
      << "-- would make them equal.\n"
      << "example : ((compileAndRun colfix_designCert " << fire << " #[" << both << "] " << st
      << ").nextState.flops[0]!) = mk_bv 8 77 := by native_decide\n"
      << "example : ((compileAndRun colfix_designCert " << fire << " #[" << both << "] " << st
      << ").nextState.flops[1]!) = mk_bv 8 55 := by native_decide\n";

  out << "\n-- A QUIET clock commits nothing, whichever writes are enabled.\n";
  for (int a = 0; a < 4; ++a) {
    const int want = a == 0 ? 11 : (a == 1 ? 22 : (a == 2 ? 33 : 44));
    out << "example : ((compileAndRun colfix_designCert " << quiet << " #[" << both << "] " << st
        << ").nextState.mems[0]! (" << a << " : Int)) = mk_bv 8 " << want << " := by native_decide\n";
  }

  out << "\n-- direct vs compiled over the collision cases, every tiny address.\n";
  for (const auto& in : {both, only0, split}) {
    for (const auto& e : {fire, quiet}) {
      out << "example : diffStep colfix_designCert colfix_residual " << e << " #[" << in << "] " << st
          << " [0,1,2,3] = none := by native_decide\n";
    }
  }
  ASSERT_TRUE(out.good());
}

// ARBITRARY-WIDTH FORWARDING.  The matrix is reads x writes, so port counts
// multiply: this memory's 2 reads and 32 writes need 64 cells, two past the 62
// the old i64 window allowed, and the design it stands in for (a 128-write-port
// memory) needs 256.  Those were refused outright with "the `fwd` matrix needs
// N bits (reads x writes), which does not fit".
//
// A WIDE MATRIX IS NOT ENOUGH ON ITS OWN.  A 256-bit matrix whose value is zero
// exercises no high bit at all -- every read would be read-first and the test
// would pass against a reader that silently truncates.  So the matrix here has
// its HIGHEST cell set (bit 63: read 1 <- write 31), which no 62-bit window can
// even represent, and the two read rows are DIFFERENT, so a reader that shared
// one row between reads would make the two registers agree.
TEST(DesignScanClocks, WideMatrixHighBitForwardingValueOracle) {
  constexpr uint32_t kWrites = 32;  // 2 reads x 32 writes = 64 cells
  auto&              lib     = livehd::Hhds_graph_library::instance("lgdb_clk_mem_wide");
  auto               io      = lib.create_io("widefwd");
  uint32_t           pid     = 1;
  for (const char* n : {"clk", "we0", "weN", "re0", "re1"}) {
    io->add_input(n, pid++);
    io->set_bits(n, 1);
  }
  for (const char* n : {"wa0", "waN", "ra0", "ra1"}) {
    io->add_input(n, pid++);
    io->set_bits(n, 2);
  }
  for (const char* n : {"wd0", "wdN"}) {
    io->add_input(n, pid++);
    io->set_bits(n, 8);
  }
  io->add_output("rd0", 1);
  io->set_bits("rd0", 8);
  io->add_output("rd1", 2);
  io->set_bits("rd1", 8);
  auto       g       = io->create_graph();
  auto       mem     = create_typed_node(*g, Ntype_op::Memory);
  const auto mstride = static_cast<uint32_t>(Ntype::Memory_port_stride);
  auto       sink    = [&](std::string_view n, uint32_t port) {
    return mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, n) + port * mstride);
  };
  auto konst = [&](std::string_view n, int64_t v, uint32_t port = 0) {
    livehd::graph_util::create_const(*g, *Dlop::create_integer(v)).connect_sink(sink(n, port));
  };
  konst("bits", 8);
  konst("size", 4);
  konst("type", 1);  // synchronous reads
  konst("wensize", 1);
  konst("posclk", 1);

  // The matrix, MSB first: bit 63 (read 1 <- write 31) and bit 0 (read 0 <-
  // write 0).  `from_binary` because the value is 2^63 + 1, which is not an
  // int64 at all -- the representation this test exists for.
  std::string bits(kWrites * 2, '0');
  bits[0]                    = '1';  // bit 63, the HIGHEST cell
  bits[kWrites * 2 - 1]      = '1';  // bit 0
  livehd::graph_util::create_const(*g, *Dlop::from_binary(bits, true)).connect_sink(sink("fwd", 0));

  // ports 0..kWrites-1 = writes; the last two = synchronous reads.
  for (uint32_t w = 0; w < kWrites; ++w) {
    konst("rdport", 0, w);
    g->get_input_pin("clk").connect_sink(sink("clock_pin", w));
    if (w == 0 || w == kWrites - 1) {
      const char* a = w == 0 ? "wa0" : "waN";
      const char* d = w == 0 ? "wd0" : "wdN";
      const char* e = w == 0 ? "we0" : "weN";
      g->get_input_pin(a).connect_sink(sink("addr", w));
      g->get_input_pin(d).connect_sink(sink("din", w));
      g->get_input_pin(e).connect_sink(sink("enable", w));
    } else {
      // The middle writes are HELD OFF so the expectations stay crisp: they
      // sit in the committed chain but commit nothing.
      konst("addr", 3, w);
      konst("din", 0, w);
      konst("enable", 0, w);
    }
  }
  const char* raddr[] = {"ra0", "ra1"};
  const char* ren[]   = {"re0", "re1"};
  for (uint32_t r = 0; r < 2; ++r) {
    const uint32_t port = kWrites + r;
    konst("rdport", 1, port);
    g->get_input_pin(raddr[r]).connect_sink(sink("addr", port));
    g->get_input_pin(ren[r]).connect_sink(sink("enable", port));
    g->get_input_pin("clk").connect_sink(sink("clock_pin", port));
    mem.create_driver_pin(port).connect_sink(g->get_output_pin(r == 0 ? "rd0" : "rd1"));
    set_bits(mem.create_driver_pin(port), 8);
  }

  DesignScan design;
  try {
    design = scan_design(*g, {});
  } catch (const std::exception& e) {
    FAIL() << "the scanner refused a 64-cell forwarding matrix: " << e.what();
  }
  // THE REGRESSION, checked directly on the scan: the high cell survived and
  // the two rows differ.
  ASSERT_EQ(design.memories.size(), 1u);
  const auto& m = design.memories.begin()->second;
  ASSERT_EQ(m.fwd_rows.size(), 2u) << "one row per read port";
  ASSERT_EQ(m.fwd_rows[0].size(), kWrites);
  EXPECT_TRUE(m.fwd_rows[1][kWrites - 1]) << "the HIGHEST cell (bit 63) was lost";
  EXPECT_TRUE(m.fwd_rows[0][0]);
  EXPECT_FALSE(m.fwd_rows[0][kWrites - 1]) << "read 0 must not inherit read 1's row";
  EXPECT_FALSE(m.fwd_rows[1][0]) << "read 1 must not inherit read 0's row";

  const auto cert = build_certificate(design, {});
  ASSERT_EQ(cert.memories.size(), 1u);
  ASSERT_EQ(cert.flops.size(), 2u) << "expected one read register per synchronous read port";

  const char* path = std::getenv("LHD_LEAN_WIDE_COLLIDE_FIXTURE");
  if (path == nullptr) {
    GTEST_SKIP() << "set LHD_LEAN_WIDE_COLLIDE_FIXTURE to emit the Lean oracle";
  }
  std::ofstream out(path);
  emit_design_cert(design, cert, out);

  auto ordinal_of = [&](std::string_view name) -> size_t {
    for (size_t i = 0; i < design.inputs.size(); ++i) {
      if (design.inputs[i].name == name) {
        return i;
      }
    }
    ADD_FAILURE() << "no input " << name;
    return 0;
  };
  struct In { const char* name; int value; };
  auto inputs = [&](std::initializer_list<In> set) {
    std::map<size_t, int> by;
    for (const auto& v : set) {
      by[ordinal_of(v.name)] = v.value;
    }
    std::ostringstream o;
    for (size_t k = 0; k < design.inputs.size(); ++k) {
      const auto it = by.find(k);
      o << (k ? ", " : "") << "mk_bv " << design.inputs[k].width << " " << (it != by.end() ? it->second : 0);
    }
    return o.str();
  };
  const std::string fire  = "#[true]";
  const std::string quiet = "#[false]";
  const char*       st    = "(⟨#[mk_bv 8 99, mk_bv 8 99], "
                            "#[fun a => if a = 0 then mk_bv 8 11 else if a = 1 then mk_bv 8 22 "
                            "else if a = 2 then mk_bv 8 33 else mk_bv 8 44]⟩ : RuntimeState)";
  // Both writes hit address 1; both reads read address 1.
  const auto both = inputs({{"we0", 1}, {"weN", 1}, {"wa0", 1}, {"waN", 1},
                            {"wd0", 55}, {"wdN", 77}, {"re0", 1}, {"re1", 1},
                            {"ra0", 1}, {"ra1", 1}});

  out << "\n-- WIDE MATRIX, HIGH CELL SET. 2 reads x 32 writes = 64 cells, two\n"
      << "-- past the 62 the i64 window allowed. fwd = 2^63 + 1: read 1 <- write\n"
      << "-- 31 (the HIGHEST cell) and read 0 <- write 0.\n"
      << "-- The rows DIFFER, so the two read registers must disagree: 55 is\n"
      << "-- write 0's datum and 77 is write 31's. A reader that truncated the\n"
      << "-- matrix, or shared one row between reads, cannot produce this pair.\n";
  out << "example : ((compileAndRun widefwd_designCert " << fire << " #[" << both << "] " << st
      << ").nextState.flops[0]!) = mk_bv 8 55 := by native_decide\n";
  out << "example : ((compileAndRun widefwd_designCert " << fire << " #[" << both << "] " << st
      << ").nextState.flops[1]!) = mk_bv 8 77 := by native_decide\n";
  out << "-- the LATER write still wins the committed image.\n";
  out << "example : ((compileAndRun widefwd_designCert " << fire << " #[" << both << "] " << st
      << ").nextState.mems[0]! (1 : Int)) = mk_bv 8 77 := by native_decide\n";
  out << "-- the held-off middle writes commit nothing: address 3 is untouched.\n";
  out << "example : ((compileAndRun widefwd_designCert " << fire << " #[" << both << "] " << st
      << ").nextState.mems[0]! (3 : Int)) = mk_bv 8 44 := by native_decide\n";
  out << "-- a QUIET clock commits nothing.\n";
  for (int a = 0; a < 4; ++a) {
    const int want = a == 0 ? 11 : (a == 1 ? 22 : (a == 2 ? 33 : 44));
    out << "example : ((compileAndRun widefwd_designCert " << quiet << " #[" << both << "] " << st
        << ").nextState.mems[0]! (" << a << " : Int)) = mk_bv 8 " << want << " := by native_decide\n";
  }
  out << "\n-- direct vs compiled, every tiny address.\n";
  for (const auto& e : {fire, quiet}) {
    out << "example : diffStep widefwd_designCert widefwd_residual " << e << " #[" << both << "] " << st
        << " [0,1,2,3] = none := by native_decide\n";
  }
  ASSERT_TRUE(out.good());
}

// ---------------------------------------------------------------------------
// MEMORY PORT DEPENDENCIES.
//
// The scan used to expand every node by its input edges and cut only flops, so
// a Memory became ONE combinational vertex joining all of its inputs to all of
// its outputs. That reports `read dout -> ... -> WRITE address` as a cycle. It
// is not one: a write port samples its address AT THE EDGE, so the path is
// feedback through the array, like `always @(posedge clk) r <= f(r)`.
//
// Dependencies now come from `Memory_deps`, asked per READ PORT and unioned
// over all of them, with `writes = true` so that FORWARDED write cones -- which
// `memory_lowering` really does build into a read's value -- stay visible.
namespace {
struct MemFix {
  std::shared_ptr<hhds::Graph> g;
  hhds::Node_class             mem;
};

// One write port (0) and `reads` async-or-sync read ports (1..reads) over a
// 4-entry x 8-bit array. `fwd` is the forwarding matrix the certificate reads.
MemFix build_mem(hhds::GraphLibrary& lib, const std::string& mod, bool sync_read, int fwd, int reads = 1) {
  auto io = lib.create_io(mod);
  int  p  = 0;
  for (const char* n : {"clk", "we"}) {
    io->add_input(n, p++);
    io->set_bits(n, 1);
    io->set_unsign(n, true);
  }
  io->add_input("wa", p++);
  io->set_bits("wa", 2);
  io->set_unsign("wa", true);
  for (int r = 0; r < reads; ++r) {
    const auto n = "ra" + std::to_string(r);
    io->add_input(n, p++);
    io->set_bits(n, 2);
    io->set_unsign(n, true);
  }
  io->add_input("din", p++);
  io->set_bits("din", 8);
  io->set_unsign("din", true);
  for (int r = 0; r < reads; ++r) {
    const auto n = "dout" + std::to_string(r);
    io->add_output(n, p++);
    io->set_bits(n, 8);
    io->set_unsign(n, true);
  }

  MemFix f;
  f.g       = io->create_graph();
  auto* g   = f.g.get();
  f.mem     = create_typed_node(*g, Ntype_op::Memory);
  const int stride = static_cast<int>(Ntype::Memory_port_stride);
  auto      konst  = [&](int64_t v) { return livehd::graph_util::create_const(*g, *Dlop::create_integer(v)); };
  auto      global = [&](const char* name, int64_t v) {
    konst(v).connect_sink(f.mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name)));
  };
  global("bits", 8);
  global("size", 4);
  global("fwd", fwd);
  global("wensize", 1);
  global("posclk", 1);
  global("type", sync_read ? 1 : 2);
  auto port = [&](int idx, const char* name, const hhds::Pin_class& drv) {
    drv.connect_sink(f.mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name) + idx * stride));
  };
  // write port 0
  port(0, "addr", g->get_input_pin("wa"));
  port(0, "din", g->get_input_pin("din"));
  port(0, "enable", g->get_input_pin("we"));
  port(0, "clock_pin", g->get_input_pin("clk"));
  port(0, "rdport", konst(0));
  // read ports 1..reads
  for (int r = 0; r < reads; ++r) {
    port(1 + r, "addr", g->get_input_pin("ra" + std::to_string(r)));
    port(1 + r, "enable", konst(1));
    port(1 + r, "clock_pin", g->get_input_pin("clk"));
    port(1 + r, "rdport", konst(1));
    // dout pid follows the cgen convention: (write ports) + (read index).
    auto dout = f.mem.create_driver_pin(1 + r);
    set_bits(dout, 8);
    livehd::graph_util::set_unsign(dout);
    dout.connect_sink(g->get_output_pin("dout" + std::to_string(r)));
  }
  return f;
}

// Route the read output back into a chosen memory sink through pure
// combinational logic (an Or with a constant, so nothing folds it away).
void feed_back(MemFix& f, const char* sink_name, int port_idx, int from_read = 0) {
  auto* g      = f.g.get();
  auto  dout   = f.mem.create_driver_pin(1 + from_read);
  auto  orn    = create_typed_node(*g, Ntype_op::Or);
  dout.connect_sink(orn.create_sink_pin(0));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(0)).connect_sink(orn.create_sink_pin(1));
  auto oq = orn.create_driver_pin(0);
  set_bits(oq, 2);
  livehd::graph_util::set_unsign(oq);
  const int stride = static_cast<int>(Ntype::Memory_port_stride);
  auto      sink   = f.mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, sink_name) + port_idx * stride);
  sink.del_sink();
  oq.connect_sink(sink);
}

// Re-pin an already built fixture as a MIXED-read-timing memory: the cell-global
// `type` becomes the mixed sentinel and the read ports carry DISAGREEING
// `rdport` values. No RTL route produces this today -- the reader's script runs
// `memory -nomap -nordff`, so a synchronous read port is never formed and the
// cell type is never 3 -- so the graph has to be written by hand. That is the
// only way to reach the per-port code at all, and leaving it unreached is how a
// fail-closed path rots into one that fails open.
void make_mixed_read(MemFix& f, const std::vector<int64_t>& rdport_by_read) {
  auto*     g      = f.g.get();
  const int stride = static_cast<int>(Ntype::Memory_port_stride);
  auto      repin  = [&](const char* name, int port_idx, int64_t v) {
    auto sink = f.mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name) + port_idx * stride);
    sink.del_sink();
    livehd::graph_util::create_const(*g, *Dlop::create_integer(v)).connect_sink(sink);
  };
  repin("type", 0, Ntype::Memory_type_mixed);
  for (size_t r = 0; r < rdport_by_read.size(); ++r) {
    repin("rdport", static_cast<int>(1 + r), rdport_by_read[r]);
  }
}

// The WHOLE chain, not just the scan: `build_certificate` ends in
// `index_certificate`, which refuses any node whose dependency was not declared
// earlier. A scan that accepts a graph but schedules it in an order the
// certificate cannot express has to fail here, not pass quietly.
CertificateIR scan_and_build(hhds::Graph& g) {
  const auto design = scan_design(g, {});
  return build_certificate(design, {});
}
}  // namespace

// A write ADDRESS fed from the read output is sampled at the edge: not a loop.
TEST(DesignScan, ClockedWriteAddressFeedbackIsNotACombinationalCycle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_waddr_fb");
  auto  f   = build_mem(lib, "waddr_fb", /*sync_read=*/false, /*fwd=*/0);
  feed_back(f, "addr", /*port_idx=*/0);
  EXPECT_NO_THROW(scan_and_build(*f.g))
      << "a write port samples its address AT THE EDGE, so read -> write-address is feedback through the array";
}

// The read's OWN address fed from its own output closes with no edge between:
// a real zero-delay loop, and it must still be refused.
TEST(DesignScan, AsyncReadAddressFeedbackIsStillACycle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_raddr_fb");
  auto  f   = build_mem(lib, "raddr_fb", /*sync_read=*/false, /*fwd=*/0);
  feed_back(f, "addr", /*port_idx=*/1);
  EXPECT_THROW(scan_design(*f.g, {}), std::runtime_error)
      << "an ASYNC read output driving its own read address is a genuine combinational loop";
}

// A SYNCHRONOUS read is a register: its output cuts the dependency, so even a
// direct feedback to its own address is not combinational.
TEST(DesignScan, SyncReadOutputCutsTheDependency) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_sync_cut");
  auto  f   = build_mem(lib, "sync_cut", /*sync_read=*/true, /*fwd=*/0);
  feed_back(f, "addr", /*port_idx=*/1);
  EXPECT_NO_THROW(scan_and_build(*f.g))
      << "a synchronous read output is state; it cannot be a combinational dependency";
}

// FORWARDING makes a write cone part of the read's VALUE -- `memory_lowering`
// chains the forwarded writes into the image before `MemRead`. So a read output
// reaching a forwarded write's ADDRESS really is a combinational loop, and the
// scan must keep seeing it. This is why the walk asks for `writes = true`;
// legalize's loop-check policy (`writes = false`) would hide it.
TEST(DesignScan, ForwardedWriteCycleStaysVisible) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_fwd_fb");
  auto  f   = build_mem(lib, "fwd_fb", /*sync_read=*/false, /*fwd=*/1);
  feed_back(f, "addr", /*port_idx=*/0);
  EXPECT_THROW(scan_design(*f.g, {}), std::runtime_error)
      << "with forwarding the write cone IS part of the read value, so this path closes combinationally";
}

// TWO read ports with independent addresses must still scan. The walk expands
// the UNION over every read port, so a second port is a second cone to cross,
// not a second chance to report a loop -- CORE-ET minion_dcache_top carries two
// 8-read-port memories, and over-reporting here would refuse the whole design.
TEST(DesignScan, TwoIndependentReadPortsAreNotACycle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_two_rd");
  auto  f   = build_mem(lib, "two_rd", /*sync_read=*/false, /*fwd=*/0, /*reads=*/2);
  feed_back(f, "addr", /*port_idx=*/0, /*from_read=*/1);
  EXPECT_NO_THROW(scan_and_build(*f.g))
      << "two read ports whose addresses do not depend on each other are independent cones";
}

// MIXED read timing: the cell says 3 and the ports disagree.
//
// The scanner carries the timing PER PORT (`Memory_port_info::rd_sync`), but
// `memory_lowering` and the legacy emitter still branch on the cell-global
// `Memory_info::sync`, so one shape would be emitted for a memory that needs
// two. The refusal is the contract, and the diagnostic has to name WHICH ports
// split, because that is what sizes the per-port lowering work.
TEST(DesignScan, MixedReadTimingIsRefusedAndNamesTheSplit) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_mixed_rd");
  auto  f   = build_mem(lib, "mixed_rd", /*sync_read=*/false, /*fwd=*/0, /*reads=*/2);
  make_mixed_read(f, {Ntype::Memory_rdport_sync, Ntype::Memory_rdport_async});
  try {
    scan_design(*f.g, {});
    ADD_FAILURE() << "a memory whose read ports disagree on timing was accepted";
  } catch (const std::exception& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("Read ports disagree on timing"), std::string::npos) << what;
    EXPECT_NE(what.find("sync {1}"), std::string::npos) << what;
    EXPECT_NE(what.find("async {2}"), std::string::npos) << what;
  }
}

// The split is read from the PINS, not from the port order: swapping which read
// port is registered has to swap the two sets. A diagnostic built from the port
// index instead would print the same text either way and would be worthless as
// a sizing input.
TEST(DesignScan, MixedReadTimingSplitFollowsThePins) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_mixed_rd_swap");
  auto  f   = build_mem(lib, "mixed_rd_swap", /*sync_read=*/false, /*fwd=*/0, /*reads=*/2);
  make_mixed_read(f, {Ntype::Memory_rdport_async, Ntype::Memory_rdport_sync});
  try {
    scan_design(*f.g, {});
    ADD_FAILURE() << "a memory whose read ports disagree on timing was accepted";
  } catch (const std::exception& e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("sync {2}"), std::string::npos) << what;
    EXPECT_NE(what.find("async {1}"), std::string::npos) << what;
  }
}

// ONE WAY across two read ports is an ordinary DAG edge, not a cycle: read 0's
// data feeds read 1's address and nothing returns. Two reads of one array are
// two `MemRead` nodes over one image, and the certificate orders them like any
// other pair of nodes -- which it can only do because each read port is
// scheduled, and lowered, as its own unit.
TEST(DesignScan, OneWayCrossReadPortDependencyIsNotACycle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_cross_rd");
  auto  f   = build_mem(lib, "cross_rd", /*sync_read=*/false, /*fwd=*/0, /*reads=*/2);
  feed_back(f, "addr", /*port_idx=*/2, /*from_read=*/0);
  CertificateIR cert;
  ASSERT_NO_THROW(cert = scan_and_build(*f.g))
      << "read 0 -> read 1's address has a valid order: read 0, the logic, then read 1";
  // Both reads are present, over ONE image: the array keeps a single state
  // identity however many ports read it.
  size_t reads = 0, images = 0;
  for (const auto& n : cert.nodes) {
    reads += n.op.kind == Operation::MemRead;
  }
  for (const auto& src : cert.sources) {
    images += src.kind == SourceKind::MemImage;
  }
  EXPECT_EQ(reads, 2u) << "each consumed read port must lower to its own MemRead";
  EXPECT_EQ(images, 1u) << "one memory, one image identity";
  EXPECT_EQ(cert.memories.size(), 1u);
}

// A RETURN dependency is a genuine loop and must still be refused: read 0's
// data feeds read 1's address AND read 1's data feeds read 0's address, with no
// edge anywhere between.
TEST(DesignScan, TwoWayCrossReadPortDependencyIsACycle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_cross_rd2");
  auto  f   = build_mem(lib, "cross_rd2", /*sync_read=*/false, /*fwd=*/0, /*reads=*/2);
  feed_back(f, "addr", /*port_idx=*/2, /*from_read=*/0);
  feed_back(f, "addr", /*port_idx=*/1, /*from_read=*/1);
  EXPECT_THROW(scan_design(*f.g, {}), std::runtime_error)
      << "read 0 -> read 1's address and read 1 -> read 0's address closes with no edge between";
}

// WHICH WRITE CONE a read depends on comes from that read's OWN row of the
// forwarding matrix, `fwd_rows[r][w]`, because that is what `lower_memory_read`
// chains into the image it reads. Here `fwd = 0b10` over one write port: read 1
// forwards write 0, read 0 does not.
//
// The shared `Memory_deps` helper cannot express this -- its `fwd_nonzero` is a
// whole-cell flag -- so asking it here would make BOTH of these a cycle and
// refuse a design that is perfectly ordinary.
TEST(DesignScan, ReadFirstPortSharingAnArrayWithAForwardingPortIsNotACycle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_row_readfirst");
  auto  f   = build_mem(lib, "row_readfirst", /*sync_read=*/false, /*fwd=*/0b10, /*reads=*/2);
  feed_back(f, "addr", /*port_idx=*/0, /*from_read=*/0);
  EXPECT_NO_THROW(scan_and_build(*f.g))
      << "read 0's row does not forward write 0, so write 0's address is not part of read 0's value";
}

TEST(DesignScan, ForwardingPortReachingItsOwnForwardedWriteIsACycle) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_scan_mem_row_fwd");
  auto  f   = build_mem(lib, "row_fwd", /*sync_read=*/false, /*fwd=*/0b10, /*reads=*/2);
  feed_back(f, "addr", /*port_idx=*/0, /*from_read=*/1);
  EXPECT_THROW(scan_design(*f.g, {}), std::runtime_error)
      << "read 1's row DOES forward write 0, so write 0's address is part of read 1's value";
}

namespace {
// Two arrays in series: the second memory's write DATA is the first memory's
// read output. CORE-ET minion_dcache_top has exactly this pair (n_107860's
// dout drives n_107868's write din).
std::shared_ptr<hhds::Graph> build_chained_memories(hhds::GraphLibrary& lib, const std::string& mod, bool consumer_first) {
  auto io = lib.create_io(mod);
  int  p  = 0;
  for (const char* n : {"clk", "we"}) {
    io->add_input(n, p++);
    io->set_bits(n, 1);
    io->set_unsign(n, true);
  }
  for (const char* n : {"wa", "ra"}) {
    io->add_input(n, p++);
    io->set_bits(n, 2);
    io->set_unsign(n, true);
  }
  io->add_input("din", p++);
  io->set_bits("din", 8);
  io->set_unsign("din", true);
  io->add_output("dout", p++);
  io->set_bits("dout", 8);
  io->set_unsign("dout", true);

  auto      gp     = io->create_graph();
  auto*     g      = gp.get();
  const int stride = static_cast<int>(Ntype::Memory_port_stride);
  auto      konst  = [&](int64_t v) { return livehd::graph_util::create_const(*g, *Dlop::create_integer(v)); };
  auto      build  = [&](const hhds::Pin_class& wr_data, hhds::Node_class* out_mem) {
    auto mem    = create_typed_node(*g, Ntype_op::Memory);
    if (out_mem != nullptr) {
      *out_mem = mem;
    }
    auto global = [&](const char* name, int64_t v) {
      konst(v).connect_sink(mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name)));
    };
    global("bits", 8);
    global("size", 4);
    global("fwd", 0);
    global("wensize", 1);
    global("posclk", 1);
    global("type", 2);
    auto port = [&](int idx, const char* name, const hhds::Pin_class& drv) {
      drv.connect_sink(mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name) + idx * stride));
    };
    port(0, "addr", g->get_input_pin("wa"));
    if (!wr_data.is_invalid()) {
      port(0, "din", wr_data);
    }
    port(0, "enable", g->get_input_pin("we"));
    port(0, "clock_pin", g->get_input_pin("clk"));
    port(0, "rdport", konst(0));
    port(1, "addr", g->get_input_pin("ra"));
    port(1, "enable", konst(1));
    port(1, "clock_pin", g->get_input_pin("clk"));
    port(1, "rdport", konst(1));
    auto dout = mem.create_driver_pin(1);
    set_bits(dout, 8);
    livehd::graph_util::set_unsign(dout);
    return dout;
  };
  if (consumer_first) {
    // The CONSUMER memory exists before the producer it will read: its write
    // `din` sink is connected only once the producer has been built.
    hhds::Node_class consumer;
    const auto       consumer_dout = build(hhds::Pin_class(), &consumer);
    const auto       producer_dout = build(g->get_input_pin("din"), nullptr);
    producer_dout.connect_sink(consumer.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, "din")));
    consumer_dout.connect_sink(g->get_output_pin("dout"));
    return gp;
  }
  const auto first  = build(g->get_input_pin("din"), nullptr);
  const auto second = build(first, nullptr);
  second.connect_sink(g->get_output_pin("dout"));
  return gp;
}
}  // namespace

// A memory's WRITE side is a next-state function and may read anything in the
// design, including another memory. Lowering both sides at the Memory node's
// own place in the topological order makes this pair unschedulable: the second
// memory would have to be lowered both before its own read consumers and after
// the first memory's. The write side is therefore emitted after every node,
// exactly like a flop's `din`.
//
// BOTH CREATION ORDERS. The graph's node order is not the certificate's, but it
// is what the scan walks from, so a scheduler that happened to work only when
// the producing memory was built first would pass a one-sided test.
TEST(CertificateIr, MemoryWriteDataFromAnotherMemorysRead) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cert_mem_chain");
  auto  g   = build_chained_memories(lib, "mem_chain", /*consumer_first=*/false);
  EXPECT_NO_THROW(scan_and_build(*g)) << "a memory's next-state cone may name another memory's read output";
}

TEST(CertificateIr, MemoryWriteDataFromAnotherMemorysReadConsumerBuiltFirst) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_cert_mem_chain_rev");
  auto  g   = build_chained_memories(lib, "mem_chain_rev", /*consumer_first=*/true);
  EXPECT_NO_THROW(scan_and_build(*g)) << "the schedule must not depend on which memory node was created first";
}
}  // namespace
}  // namespace lean_export
