// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "cgen_sim.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "sim_color_plan.hpp"
#include "sim_specialize.hpp"
#include "sim_tune_vector.hpp"

namespace {
namespace gu = livehd::graph_util;

std::string slurp(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string emit(const std::shared_ptr<hhds::Graph>& graph, const std::string& name) {
  std::filesystem::create_directories(name);
  const auto plan = livehd::sim::Color_plan::discover(graph.get(), false);
  EXPECT_TRUE(plan.complete()) << plan.report();
  Cgen_sim emitter(name, "", name, "false", &plan);
  emitter.do_from_graph(graph);
  std::string code;
  for (const auto& entry : std::filesystem::directory_iterator(name)) {
    if (entry.path().extension() == ".cpp") {
      std::ifstream input(entry.path());
      EXPECT_TRUE(input.good());
      code.append(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
  }
  return code;
}

TEST(CgenSim, ProofOnlySubnodesKeepSlopPureBodiesInlineable) {
  const std::string name    = "pure_with_proof";
  auto&             library = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io      = library.create_io(name);
  io->add_input("a", 0);
  io->add_output("y", 1);
  for (const auto* port : {"a", "y"}) {
    io->set_bits(port, 8);
    io->set_unsign(port, true);
  }
  auto graph = io->create_graph();
  graph->get_input_pin("a").connect_sink(graph->get_output_pin("y"));
  auto proof_io = library.create_io(gu::lgassert_module_name);
  proof_io->add_input("a", 0);
  proof_io->set_bits("a", 1);
  auto proof = gu::create_typed_node(*graph, Ntype_op::Sub);
  proof.set_subnode(proof_io);
  gu::create_const(*graph, *Dlop::create_integer(1)).connect_sink(proof.create_sink_pin(0));
  emit(graph, name);
  const auto pure = slurp(std::filesystem::path(name) / (name + ".pure.inc"));
  EXPECT_NE(pure.find("[[gnu::always_inline]] inline"), std::string::npos);
  EXPECT_NE(pure.find("::__pure_eval("), std::string::npos);
  EXPECT_EQ(pure.find(std::string(gu::lgassert_module_name)), std::string::npos);
  std::filesystem::remove_all(name);
}

TEST(CgenSim, LlvmObjectsSurviveInsertionAndLongModuleNames) {
  const std::string dir = "llvm_incremental_objects";
  const std::string name(200, 'x');
  std::filesystem::create_directories(dir);
  const auto generate = [&](bool edited) {
    auto& lib = livehd::Hhds_graph_library::instance(edited ? "lgdb_llvm_object_edit" : "lgdb_llvm_object_base");
    auto  io  = lib.create_io(name);
    for (unsigned i = 0; i < 9; ++i) {
      io->add_input("a" + std::to_string(i), i);
      io->add_output("y" + std::to_string(i), 9 + i);
      io->set_bits("a" + std::to_string(i), 8 + i);
      io->set_bits("y" + std::to_string(i), 8 + i);
    }
    auto graph = io->create_graph();
    for (unsigned i = 0; i < 9; ++i) {
      auto value = graph->get_input_pin("a" + std::to_string(i));
      if (i != 8 || edited) {
        auto node = gu::create_typed_node(*graph, Ntype_op::Not, 8 + i);
        value.connect_sink(node.create_sink_pin(0));
        value = node.create_driver_pin(0);
        gu::set_bits(value, 8 + i);
      }
      value.connect_sink(graph->get_output_pin("y" + std::to_string(i)));
    }
    const auto plan = livehd::sim::Color_plan::discover(graph.get(), false, false, 1);
    EXPECT_TRUE(plan.complete()) << plan.report();
    Cgen_sim emitter(dir, "", name, "false", &plan, false, false, true, true, false, false, false, true, true, 1);
    emitter.do_from_graph(graph);
    std::map<std::string, std::pair<std::string, std::filesystem::file_time_type>> objects;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      const auto filename = entry.path().filename().string();
      EXPECT_LE(filename.size(), 255u);
      if (filename.ends_with(".llvm.o")) {
        objects.emplace(filename, std::pair{slurp(entry.path()), entry.last_write_time()});
      }
    }
    return objects;
  };
  const auto before = generate(false);
  const auto after  = generate(true);
  ASSERT_GE(before.size(), 8u);
  EXPECT_GT(after.size(), before.size());
  for (const auto& [filename, content] : before) {
    const auto found = after.find(filename);
    ASSERT_NE(found, after.end()) << "unchanged color object was renamed: " << filename;
    EXPECT_EQ(found->second, content) << "unchanged object bytes or mtime changed: " << filename;
  }
  // Removing the added color cleans its object while preserving the originals.
  EXPECT_EQ(generate(false), before);
  std::filesystem::remove_all(dir);
}

TEST(CgenSim, LlvmSemanticEditKeepsModuleSupportUnchanged) {
  const std::string name = "llvm_checkpoint_identity";
  std::filesystem::create_directories(name);
  const auto generate = [&](Ntype_op op) {
    auto& lib = livehd::Hhds_graph_library::instance("lgdb_" + name + std::to_string(static_cast<int>(op)));
    auto  io  = lib.create_io(name);
    io->add_input("a", 0);
    io->add_input("b", 1);
    io->add_output("y", 2);
    for (const auto* port : {"a", "b", "y"}) {
      io->set_bits(port, 8);
      io->set_unsign(port, true);
    }
    auto graph = io->create_graph();
    auto node  = gu::create_typed_node(*graph, op, 8);
    graph->get_input_pin("a").connect_sink(node.create_sink_pin(0));
    graph->get_input_pin("b").connect_sink(node.create_sink_pin(1));
    auto value = node.create_driver_pin(0);
    gu::set_ubits(value, 8);
    value.connect_sink(graph->get_output_pin("y"));
    const auto plan = livehd::sim::Color_plan::discover(graph.get(), false);
    EXPECT_TRUE(plan.complete()) << plan.report();
    Cgen_sim emitter(name, "", name, "false", &plan, false, false, true, true, false, false, false, true, true);
    emitter.do_from_graph(graph);
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(name)) {
      files.emplace(entry.path().filename().string(), slurp(entry.path()));
    }
    return files;
  };
  const auto before = generate(Ntype_op::And);
  const auto after  = generate(Ntype_op::Or);
  EXPECT_EQ(before.at(name + ".cpp"), after.at(name + ".cpp"));
  EXPECT_EQ(before.at(name + ".hpp"), after.at(name + ".hpp"));
  EXPECT_EQ(before.at(name + ".tune.cpp"), after.at(name + ".tune.cpp"));
  EXPECT_NE(before.at(name + ".tune-id.cpp"), after.at(name + ".tune-id.cpp"));
  size_t native_changes = 0;
  for (const auto& [file, bytes] : before) {
    if (file.ends_with(".llvm.o")) {
      const auto found = after.find(file);
      ASSERT_NE(found, after.end());
      native_changes += bytes != found->second;
    }
  }
  EXPECT_EQ(native_changes, 1u);
  std::filesystem::remove_all(name);
}

TEST(CgenSim, IdenticalColorsShareOneNativeObject) {
  const std::string name = "shared_native_colors";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io   = lib.create_io(name);
  for (unsigned i = 0; i < 8; ++i) {
    io->add_input("a" + std::to_string(i), i);
    io->add_output("y" + std::to_string(i), 8 + i);
    io->set_bits("a" + std::to_string(i), 8);
    io->set_bits("y" + std::to_string(i), 8);
  }
  auto graph = io->create_graph();
  for (unsigned i = 0; i < 8; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Not, 8);
    graph->get_input_pin("a" + std::to_string(i)).connect_sink(node.create_sink_pin(0));
    auto value = node.create_driver_pin(0);
    gu::set_bits(value, 8);
    value.connect_sink(graph->get_output_pin("y" + std::to_string(i)));
  }
  std::filesystem::create_directories(name);
  const auto plan = livehd::sim::Color_plan::discover(graph.get(), false, false, 1);
  ASSERT_TRUE(plan.complete()) << plan.report();
  Cgen_sim emitter(name, "", name, "false", &plan, false, false, true, true, false, false, false, true, true, 1);
  emitter.do_from_graph(graph);
  size_t objects = 0;
  for (const auto& entry : std::filesystem::directory_iterator(name)) {
    objects += entry.path().filename().string().ends_with(".llvm.o");
  }
  EXPECT_EQ(objects, 1u);
  std::filesystem::remove_all(name);
}

TEST(CgenSim, DynamicSplicesRespectColorKernelBindings) {
  const std::string name = "dynamic_splice_boundary";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io   = lib.create_io(name);
  unsigned          port = 0;
  for (const auto& [field, bits] : {
           std::pair{  "base", 128},
           { "index",   4},
           { "value",   8},
           {"select",   1}
  }) {
    io->add_input(field, port++);
    io->set_bits(field, bits);
    io->set_unsign(field, true);
  }
  for (unsigned lane = 0; lane < 4; ++lane) {
    const auto field = "y" + std::to_string(lane);
    io->add_output(field, port++);
    io->set_bits(field, 128);
    io->set_unsign(field, true);
  }
  auto       graph  = io->create_graph();
  const auto binary = [&](Ntype_op op, unsigned bits, hhds::Pin_class lhs, hhds::Pin_class rhs) {
    auto node = gu::create_typed_node(*graph, op);
    lhs.connect_sink(node.create_sink_pin(0));
    rhs.connect_sink(node.create_sink_pin(1));
    auto output = node.create_driver_pin(0);
    gu::set_ubits(output, bits);
    return output;
  };
  for (unsigned lane = 0; lane < 4; ++lane) {
    auto mux = gu::create_typed_node(*graph, Ntype_op::Mux);
    graph->get_input_pin("select").connect_sink(mux.create_sink_pin(0));
    gu::create_const(*graph, *Dlop::create_integer(0xf5)).connect_sink(mux.create_sink_pin(1));
    graph->get_input_pin("value").connect_sink(mux.create_sink_pin(2));
    auto value = mux.create_driver_pin(0);
    gu::set_ubits(value, 8);
    const auto index = graph->get_input_pin("index");
    const auto mask  = binary(Ntype_op::SHL, 128, gu::create_const(*graph, *Dlop::create_integer(255)), index);
    const auto inverted
        = binary(Ntype_op::Xor, 128, mask, gu::create_const(*graph, *Dlop::create_string("0uxffffffffffffffffffffffffffffffff")));
    const auto keep    = binary(Ntype_op::And, 128, graph->get_input_pin("base"), inverted);
    const auto shifted = binary(Ntype_op::SHL, 128, value, index);
    const auto placed  = binary(Ntype_op::And, 128, shifted, mask);
    binary(Ntype_op::Or, 128, keep, placed).connect_sink(graph->get_output_pin("y" + std::to_string(lane)));
  }
  bool shared_kernel = false;
  for (unsigned words : {1, 4, 16, 256}) {
    const auto dir = name + "_" + std::to_string(words);
    std::filesystem::create_directories(dir);
    const auto plan = livehd::sim::Color_plan::discover(graph.get(), false, false, words);
    ASSERT_TRUE(plan.complete()) << plan.report();
    Cgen_sim emitter(dir, "", name, "false", &plan, false, false, true, true, true, false, false, false, true, words);
    emitter.do_from_graph(graph);
    for (const auto& file : std::filesystem::directory_iterator(dir)) {
      if (file.path().extension() != ".cpp") {
        continue;
      }
      const auto code  = slurp(file.path());
      shared_kernel   |= code.find("void __lhd_color_kernel_") != std::string::npos;
      EXPECT_EQ(code.find("UNRESOLVED-CYCLE"), std::string::npos) << file.path();
    }
  }
  EXPECT_TRUE(shared_kernel) << "the regression must exercise canonical kernel emission";
}

TEST(CgenSim, FusesAndReductionAfterTemporaryBindingsExpire) {
  for (int width : {1, 3, 8, 65}) {
    const auto name = "reduce_" + std::to_string(width);
    auto&      lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
    auto       io   = lib.create_io(name);
    io->add_input("a", 0);
    io->set_bits("a", width);
    io->set_unsign("a", true);
    io->add_output("y", 1);
    io->set_bits("y", 1);
    io->set_unsign("y", true);
    auto graph = io->create_graph();
    auto sx    = gu::create_typed_node(*graph, Ntype_op::Sext);
    graph->get_input_pin("a").connect_sink(gu::setup_sink_by_name(sx, "a"));
    gu::create_const(*graph, *Dlop::create_integer(width)).connect_sink(gu::setup_sink_by_name(sx, "b"));
    auto signed_value = sx.create_driver_pin(0);
    gu::set_sbits(signed_value, width);
    auto eq = gu::create_typed_node(*graph, Ntype_op::EQ);
    signed_value.connect_sink(gu::setup_sink_by_name(eq, "as"));
    gu::create_const(*graph, *Dlop::create_integer(-1)).connect_sink(gu::setup_sink_by_name(eq, "as"));
    auto out = eq.create_driver_pin(0);
    gu::set_ubits(out, 1);
    out.connect_sink(graph->get_output_pin("y"));
    const auto code = emit(graph, name);
    EXPECT_NE(code.find("::rand_op("), std::string::npos);
    EXPECT_EQ(code.find(".sext_op("), std::string::npos);
    EXPECT_EQ(code.find("::eq_op("), std::string::npos);
    EXPECT_EQ(code.find("create_integer(-1)"), std::string::npos);
    EXPECT_EQ(code.find("UNRESOLVED-CYCLE"), std::string::npos);
  }
}

TEST(CgenSim, MaskWritesUseRangesIncludingWholeAndOutsideCarrier) {
  const std::string name = "mask_windows";
  auto&             lib  = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io   = lib.create_io(name);
  io->add_input("a", 0);
  io->add_input("v", 1);
  for (auto field : {"a", "v"}) {
    io->set_bits(field, 8);
    io->set_unsign(field, true);
  }
  for (int i = 0; i < 4; ++i) {
    const auto field = "y" + std::to_string(i);
    io->add_output(field, i + 2);
    io->set_bits(field, 8);
    io->set_unsign(field, true);
  }
  auto graph = io->create_graph();
  int  index = 0;
  for (const auto& [lo, hi] : {
           std::pair{ 0,  8},
           std::pair{ 3,  6},
           std::pair{ 6, 70},
           std::pair{64, 70}
  }) {
    auto node = gu::create_set_mask(*graph, graph->get_input_pin("a"), graph->get_input_pin("v"), lo, hi);
    auto out  = node.create_driver_pin(0);
    gu::set_ubits(out, 8);
    out.connect_sink(graph->get_output_pin("y" + std::to_string(index++)));
  }
  const auto code = emit(graph, name);
  EXPECT_NE(code.find(".set_mask_op_opt("), std::string::npos);
  EXPECT_EQ(code.find(".set_mask_op("), std::string::npos);
  EXPECT_EQ(code.find("UNRESOLVED-CYCLE"), std::string::npos);
}
TEST(CgenSim, EmitsNativeCountedReductions) {
  for (int width : {1, 3, 8, 65, 129}) {
    const auto         name = "native_reduce_" + std::to_string(width);
    hhds::GraphLibrary lib;
    auto               io = lib.create_io(name);
    io->add_input("a", 0);
    io->set_bits("a", width);
    io->set_unsign("a", true);
    auto graph = io->create_graph();
    for (auto op : {Ntype_op::Rxor, Ntype_op::Popcount}) {
      const auto port        = std::string(Ntype::get_name(op));
      const int  result_bits = op == Ntype_op::Rxor ? 1 : std::bit_width(static_cast<unsigned>(width));
      io->add_output(port, op == Ntype_op::Rxor ? 1 : 2);
      io->set_bits(port, result_bits);
      io->set_unsign(port, true);
      auto node = gu::create_typed_node(*graph, op);
      graph->get_input_pin("a").connect_sink(node.create_sink_pin(0));
      gu::create_const(*graph, *Dlop::create_integer(width)).connect_sink(node.create_sink_pin(1));
      auto output = node.create_driver_pin(0);
      gu::set_ubits(output, result_bits);
      output.connect_sink(graph->get_output_pin(port));
    }
    const auto code = emit(graph, name);
    EXPECT_NE(code.find("::rxor_op("), std::string::npos);
    EXPECT_NE(code.find("::popcount_op("), std::string::npos);
    EXPECT_EQ(code.find("UNRESOLVED-CYCLE"), std::string::npos);
  }
}

// clang keeps a constructor's initializer count in an 18-bit field, so a class
// with 2^18+ members gets an implicit constructor that silently skips every
// initializer past the wrapped count (xs_rob: `__in.clock__tick` stayed false
// and no flop ever captured). The flop members of a big module therefore live
// in bounded base structs, each with its own constructor.
TEST(CgenSim, HugeFlopSetSplitsIntoBoundedStateBases) {
  constexpr int     kFlops = 16400;  // Q + _din per flop: just past one 2^15-member chunk
  const std::string name   = "huge_flop_set";
  auto&             lib    = livehd::Hhds_graph_library::instance("lgdb_" + name);
  auto              io     = lib.create_io(name);
  io->add_input("clk", 0);
  io->add_input("d", 1);
  io->set_bits("d", 1);
  io->set_unsign("d", true);
  io->add_output("q", 2);
  io->set_bits("q", 1);
  io->set_unsign("q", true);
  auto graph = io->create_graph();
  auto chain = graph->get_input_pin("d");
  for (int i = 0; i < kFlops; ++i) {
    auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
    graph->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(flop, "clock_pin"));
    chain.connect_sink(gu::setup_sink_by_name(flop, "din"));
    chain = flop.create_driver_pin(0);
    gu::set_ubits(chain, 1);
  }
  chain.connect_sink(graph->get_output_pin("q"));
  const auto code = emit(graph, name);

  // The sim.tune walker is one statement per flop, so it must be chunked, and
  // more finely than the other cold members (kTuneColdChunkStatements = 128
  // statements per lambda).
  const auto walk  = code.find("LHD_SIM_COLD void huge_flop_set::__tune_hash(");
  const auto count = code.find("LHD_SIM_COLD std::size_t huge_flop_set::__tune_count(");
  ASSERT_NE(walk, std::string::npos);
  ASSERT_NE(count, std::string::npos);
  ASSERT_LT(walk, count);
  const auto walker = std::string_view(code).substr(walk, count - walk);
  size_t     chunks = 0;
  for (auto at = walker.find("[&]() LHD_SIM_COLD {"); at != std::string_view::npos;
       at      = walker.find("[&]() LHD_SIM_COLD {", at + 1)) {
    ++chunks;
  }
  EXPECT_GE(chunks, static_cast<size_t>(kFlops) / 128);

  std::string header;
  for (const auto& entry : std::filesystem::directory_iterator(name)) {
    if (entry.path().extension() == ".hpp" && entry.path().stem() == name) {
      std::ifstream input(entry.path());
      header.append(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
  }
  ASSERT_FALSE(header.empty());
  EXPECT_NE(header.find("struct huge_flop_set : huge_flop_set__state0, huge_flop_set__state1 {"), std::string::npos);
  EXPECT_NE(header.find("struct huge_flop_set__state1 {"), std::string::npos);
  EXPECT_EQ(header.find("huge_flop_set__state2"), std::string::npos);
}
// ---- sim.tune (sim_profile.md P0/P1) --------------------------------------

// A two-level hierarchy with state on both levels: `<tag>_top` (a flop fed by
// its child) instantiates `<tag>_child` (a flop). The top is the color root
// and the DUT; the child is a storage-only definition.
struct Tune_hierarchy {
  std::shared_ptr<hhds::Graph> child;
  std::shared_ptr<hhds::Graph> root;
};

Tune_hierarchy make_tune_hierarchy(const std::string& tag) {
  auto& lib      = livehd::Hhds_graph_library::instance("lgdb_" + tag);
  auto  child_io = lib.create_io(tag + "_child");
  child_io->add_input("clk", 0);
  child_io->add_input("d", 1);
  child_io->add_output("q", 2);
  for (auto field : {"d", "q"}) {
    child_io->set_bits(field, 8);
    child_io->set_unsign(field, true);
  }
  auto child = child_io->create_graph();
  auto cflop = gu::create_typed_node(*child, Ntype_op::Flop);
  child->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(cflop, "clock_pin"));
  child->get_input_pin("d").connect_sink(gu::setup_sink_by_name(cflop, "din"));
  auto cq = cflop.create_driver_pin(0);
  gu::set_ubits(cq, 8);
  cq.connect_sink(child->get_output_pin("q"));

  auto root_io = lib.create_io(tag + "_top");
  root_io->add_input("clk", 0);
  root_io->add_input("d", 1);
  root_io->add_input("e", 2);
  root_io->add_output("q", 3);
  for (auto field : {"d", "e", "q"}) {
    root_io->set_bits(field, 8);
    root_io->set_unsign(field, true);
  }
  auto root = root_io->create_graph();
  auto sub  = gu::create_typed_node(*root, Ntype_op::Sub);
  sub.set_subnode(child_io);
  sub.set_name("u_child");
  root->get_input_pin("clk").connect_sink(sub.create_sink_pin(0));
  root->get_input_pin("d").connect_sink(sub.create_sink_pin(1));
  auto sq = sub.create_driver_pin(2);
  gu::set_ubits(sq, 8);
  auto mix = gu::create_typed_node(*root, Ntype_op::Xor);
  sq.connect_sink(gu::setup_sink_by_name(mix, "as"));
  root->get_input_pin("e").connect_sink(gu::setup_sink_by_name(mix, "as"));
  auto mixed = mix.create_driver_pin(0);
  gu::set_ubits(mixed, 8);
  auto rflop = gu::create_typed_node(*root, Ntype_op::Flop);
  root->get_input_pin("clk").connect_sink(gu::setup_sink_by_name(rflop, "clock_pin"));
  mixed.connect_sink(gu::setup_sink_by_name(rflop, "din"));
  auto rq = rflop.create_driver_pin(0);
  gu::set_ubits(rq, 8);
  rq.connect_sink(root->get_output_pin("q"));
  return {child, root};
}

// The RESOLVED knob values lhd hands inou.cgen.sim (default = tv1:d=off;f=none;lw=256;be=slop).
struct Tune_knobs {
  bool     dirty           = false;
  int64_t  fence           = livehd::sim::kTuneNoFences;
  uint32_t live_words      = 256;
  bool     runtime_support = true;
};

// Emit the hierarchy the way inou.cgen.sim does: every module through its own
// Cgen_sim, the root with the occurrence plan, the child without one.
void emit_hierarchy(const Tune_hierarchy& h, const std::string& dir, const Tune_knobs& k) {
  std::filesystem::create_directories(dir);
  const std::string top{h.root->get_name()};
  {
    Cgen_sim prep(dir, "", top, "false");
    ASSERT_TRUE(prep.prepare_graph(h.child));
    ASSERT_TRUE(prep.prepare_graph(h.root));
  }
  const auto plan = livehd::sim::Color_plan::discover(h.root.get(), false, false, k.live_words, k.fence);
  ASSERT_TRUE(plan.complete()) << plan.report();
  Cgen_sim child(dir,
                 "",
                 top,
                 "false",
                 nullptr,
                 false,
                 false,
                 k.runtime_support,
                 true,
                 k.dirty,
                 false,
                 false,
                 false,
                 false,
                 k.live_words,
                 k.fence);
  child.do_from_graph(h.child);
  Cgen_sim root(dir,
                "",
                top,
                "false",
                &plan,
                false,
                false,
                k.runtime_support,
                true,
                k.dirty,
                false,
                false,
                false,
                true,
                k.live_words,
                k.fence);
  root.do_from_graph(h.root);
}

// gen_digests.json "d" (the generation key) of `module`, "" when absent.
std::string gen_key_of(const std::string& dir, const std::string& module) {
  const auto       text = slurp(std::filesystem::path(dir) / "gen_digests.json");
  std::smatch      m;
  const std::regex re("\"" + module + "\":\\{\"d\":\"([0-9a-f]{16})\"");
  return std::regex_search(text, m, re) ? m[1].str() : std::string{};
}

// Both walker definitions of `mod`, verbatim, from its generated .cpp.
std::string walker_text(const std::string& dir, const std::string& mod) {
  const auto code  = slurp(std::filesystem::path(dir) / (mod + ".cpp"));
  const auto begin = code.find("LHD_SIM_COLD void " + mod + "::__tune_hash(");
  const auto count = code.find("LHD_SIM_COLD std::size_t " + mod + "::__tune_count(");
  if (begin == std::string::npos || count == std::string::npos || count < begin) {
    return {};
  }
  const auto end = code.find("\n}\n", count);
  return end == std::string::npos ? std::string{} : code.substr(begin, end + 3 - begin);
}

// The walkers are emitted for every module whatever the build flavour: a lean
// build (sim.checkpoint=false -> no runtime support) has no dump_state, and
// still has them.
TEST(CgenSim, TuneWalkersEmittedWithoutRuntimeSupport) {
  const std::string tag = "tune_lean";
  const auto        h   = make_tune_hierarchy(tag);
  Tune_knobs        k;
  k.runtime_support = false;
  emit_hierarchy(h, tag, k);
  for (const auto* mod : {"tune_lean_top", "tune_lean_child"}) {
    const auto code   = slurp(std::filesystem::path(tag) / (std::string(mod) + ".cpp"));
    const auto header = slurp(std::filesystem::path(tag) / (std::string(mod) + ".hpp"));
    EXPECT_EQ(code.find("::dump_state("), std::string::npos) << mod;
    EXPECT_NE(code.find("::__tune_hash(std::uint64_t*& __lhd_to, bool __lhd_root) const {"), std::string::npos) << mod;
    EXPECT_NE(code.find("::__tune_count(bool __lhd_root) const {"), std::string::npos) << mod;
    EXPECT_NE(header.find("  void        __tune_hash(std::uint64_t*& __lhd_to, bool __lhd_root = false) const;"), std::string::npos)
        << mod;
    EXPECT_NE(header.find("  std::size_t __tune_count(bool __lhd_root = false) const;"), std::string::npos) << mod;
    EXPECT_NE(header.find("#ifndef LHD_SIM_TUNE_HASH_V1"), std::string::npos) << mod;
    EXPECT_NE(header.find("#ifndef LHD_SIM_UNKNOWN_LITERAL_V2"), std::string::npos) << mod;
  }
  // Root: own flop, then the sub, then the DUT inputs but never the clock.
  const auto root = walker_text(tag, "tune_lean_top");
  ASSERT_FALSE(root.empty());
  EXPECT_NE(root.find("  u_child.__tune_hash(__lhd_to);\n"), std::string::npos) << root;
  EXPECT_NE(root.find("    *__lhd_to++ = __lhd_tune_h(__in.d);\n"), std::string::npos) << root;
  EXPECT_NE(root.find("    *__lhd_to++ = __lhd_tune_h(__in.e);\n"), std::string::npos) << root;
  EXPECT_EQ(root.find("__in.clk"), std::string::npos) << root;
  EXPECT_NE(root.find("  std::size_t __lhd_n = 1u + (__lhd_root ? 2u : 0u);\n"), std::string::npos) << root;
  EXPECT_NE(root.find("  __lhd_n += u_child.__tune_count();\n"), std::string::npos) << root;
  EXPECT_EQ(root.find("_din"), std::string::npos) << root;
  const auto child = walker_text(tag, "tune_lean_child");
  EXPECT_NE(child.find("  std::size_t __lhd_n = 1u + (__lhd_root ? 1u : 0u);\n"), std::string::npos) << child;
}

// Walker text is a function of the state layout only: byte-identical across
// every tune vector (the profiler compares words across vectors).
TEST(CgenSim, TuneWalkerTextIdenticalAcrossVectors) {
  const std::string tag  = "tune_walk";
  const auto        h    = make_tune_hierarchy(tag);
  const Tune_knobs  base = {};
  emit_hierarchy(h, tag + "_base", base);
  const std::string root_ref  = walker_text(tag + "_base", "tune_walk_top");
  const std::string child_ref = walker_text(tag + "_base", "tune_walk_child");
  ASSERT_FALSE(root_ref.empty());
  ASSERT_FALSE(child_ref.empty());
  const std::vector<Tune_knobs> vectors = {
      {.dirty = true, .fence = 16},
      {.dirty = true, .fence = 0},
      {.dirty = false, .fence = livehd::sim::kTuneNoFences, .live_words = 1},
      {.dirty = false, .fence = 0, .live_words = 20},
  };
  for (size_t i = 0; i < vectors.size(); ++i) {
    const auto dir = tag + "_v" + std::to_string(i);
    emit_hierarchy(h, dir, vectors[i]);
    EXPECT_EQ(walker_text(dir, "tune_walk_top"), root_ref) << dir;
    EXPECT_EQ(walker_text(dir, "tune_walk_child"), child_ref) << dir;
    // Nothing tune-dependent reaches a header either: the root .hpp (and so
    // drv.cpp) does not recompile on a tune flip.
    EXPECT_EQ(slurp(std::filesystem::path(dir) / "tune_walk_child.hpp"),
              slurp(std::filesystem::path(tag + "_base") / "tune_walk_child.hpp"))
        << dir;
  }
}

// The tune vector is keyed on the COLOR ROOT only, as canonical text: a flip
// regenerates exactly the root, and spellings that build the same plan key
// the same.
TEST(CgenSim, TuneVectorKeysOnlyTheRoot) {
  const std::string tag       = "tune_key";
  const auto        h         = make_tune_hierarchy(tag);
  const std::string root      = "tune_key_top";
  const std::string child     = "tune_key_child";
  const auto        emit_keys = [&](const std::string& dir, const Tune_knobs& k) -> std::pair<std::string, std::string> {
    emit_hierarchy(h, dir, k);
    return {gen_key_of(dir, root), gen_key_of(dir, child)};
  };
  const auto base = emit_keys(tag + "_base", {});
  ASSERT_EQ(base.first.size(), 16u);
  ASSERT_EQ(base.second.size(), 16u);

  const auto dirty_on = emit_keys(tag + "_dirty", {.dirty = true, .fence = -1});
  const auto dirty_16 = emit_keys(tag + "_dirty16", {.dirty = true, .fence = 16});
  const auto fence0   = emit_keys(tag + "_fence0", {.fence = 0});
  const auto lw20     = emit_keys(tag + "_lw20", {.live_words = 20});
  const auto lw_auto  = emit_keys(tag + "_lw0", {.live_words = 0});
  for (const auto* keys : {&dirty_on, &dirty_16, &fence0, &lw20, &lw_auto}) {
    EXPECT_EQ(keys->second, base.second) << "a non-root key must not fold the tune vector";
  }
  EXPECT_NE(dirty_on.first, base.first);
  EXPECT_NE(fence0.first, base.first);
  EXPECT_NE(lw20.first, base.first);
  EXPECT_EQ(lw_auto.first, base.first) << "live_words 0 (auto) and 256 build the same plan";
  EXPECT_EQ(dirty_16.first, dirty_on.first) << "fence -1 and 16 build the same plan";

  // In ONE directory: a flip regenerates only the root; flipping back returns
  // the original key.
  const auto dir = tag + "_flip";
  emit_hierarchy(h, dir, {});
  const auto child_cpp = slurp(std::filesystem::path(dir) / (child + ".cpp"));
  emit_hierarchy(h, dir, {.dirty = true, .fence = 16});
  EXPECT_EQ(gen_key_of(dir, child), base.second);
  EXPECT_EQ(gen_key_of(dir, root), dirty_on.first);
  EXPECT_EQ(slurp(std::filesystem::path(dir) / (child + ".cpp")), child_cpp);
  emit_hierarchy(h, dir, {});
  EXPECT_EQ(gen_key_of(dir, root), base.first);
}

// The emitter-debug env switches change emitted code, so they are keyed (on
// every module) and stamped into the tune-id structure string.
TEST(CgenSim, TuneEnvSwitchesAreKeyed) {
  const std::string tag = "tune_env";
  const auto        h   = make_tune_hierarchy(tag);
  emit_hierarchy(h, tag + "_plain", {});
  ASSERT_EQ(::setenv("LIVEHD_SIM_NOLAZY", "1", 1), 0);
  emit_hierarchy(h, tag + "_nolazy", {});
  ASSERT_EQ(::unsetenv("LIVEHD_SIM_NOLAZY"), 0);
  for (const auto* mod : {"tune_env_top", "tune_env_child"}) {
    const auto plain  = gen_key_of(tag + "_plain", mod);
    const auto nolazy = gen_key_of(tag + "_nolazy", mod);
    ASSERT_EQ(plain.size(), 16u) << mod;
    EXPECT_NE(plain, nolazy) << mod;
  }
  EXPECT_NE(slurp(std::filesystem::path(tag + "_plain") / "tune_env_top.tune-id.cpp").find("-simgen-66-e0\""), std::string::npos);
  EXPECT_NE(slurp(std::filesystem::path(tag + "_nolazy") / "tune_env_top.tune-id.cpp").find("-simgen-66-e2\""), std::string::npos);
}

// `<stem>.tune-id.cpp` exists exactly for executable roots, is self-contained,
// and bakes the resolved vector, the vector-independent structure and the
// codegen table.
TEST(CgenSim, TuneIdOnlyForExecutableRoots) {
  const std::string tag = "tune_id";
  const auto        h   = make_tune_hierarchy(tag);
  std::filesystem::create_directories(tag);
  // A stale identity TU from when the child was a root must be swept.
  {
    std::ofstream(std::filesystem::path(tag) / "tune_id_child.tune-id.cpp") << "// stale\n";
  }
  emit_hierarchy(h, tag, {.dirty = true, .fence = 0, .live_words = 64});
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(tag) / "tune_id_child.tune-id.cpp"));

  const auto tid_path = std::filesystem::path(tag) / "tune_id_top.tune-id.cpp";
  ASSERT_TRUE(std::filesystem::exists(tid_path));
  const auto tid = slurp(tid_path);
  EXPECT_EQ(tid.find("#include"), std::string::npos) << tid;
  EXPECT_NE(tid.find("extern \"C\" const char* __lhd_tune_vector_tune_id_top()    { return \"tv1:d=on;f=0;lw=64;be=slop\"; }"),
            std::string::npos)
      << tid;
  EXPECT_TRUE(std::regex_search(
      tid,
      std::regex(
          R"(extern "C" const char\* __lhd_tune_structure_tune_id_top\(\) \{ return "[0-9a-f]{16}-[0-9a-f]{16}-simgen-66-e[0-7]"; \})")))
      << tid;
  EXPECT_NE(tid.find("extern \"C\" const char* __lhd_tune_codegen_tune_id_top()   { return \"sim.tune.dirty=on\\nsim.tune.fence=0"
                     "\\nsim.tune.live_words=64\\nsim.tune.backend=slop\\nsim.slop_u=true\\nsim.debug=false"
                     "\\nsim.unknown_zero=false\\nsim.vcd=false\\nsim.vcd_fake_delay=false\\nplan.runtime_random=false"
                     "\\nruntime_support=true\\nobserve=false\\n\"; }"),
            std::string::npos)
      << tid;
  // The structure string does not move with the vector.
  const auto structure_of = [](const std::string& text) {
    std::smatch m;
    return std::regex_search(text, m, std::regex(R"re(__lhd_tune_structure_\w+\(\) \{ return "([^"]*)")re")) ? m[1].str()
                                                                                                             : std::string{};
  };
  emit_hierarchy(h, tag + "_default", {});
  const auto tid_default = slurp(std::filesystem::path(tag + "_default") / "tune_id_top.tune-id.cpp");
  EXPECT_NE(tid_default.find("\"tv1:d=off;f=none;lw=256;be=slop\""), std::string::npos) << tid_default;
  EXPECT_FALSE(structure_of(tid).empty());
  EXPECT_EQ(structure_of(tid_default), structure_of(tid));

  // Only the root declares the support hooks, and records the identity TU.
  const auto root_hpp  = slurp(std::filesystem::path(tag) / "tune_id_top.hpp");
  const auto child_hpp = slurp(std::filesystem::path(tag) / "tune_id_child.hpp");
  EXPECT_NE(root_hpp.find("  static const __lhd_tune_support& __tune_support();\n"), std::string::npos);
  EXPECT_NE(root_hpp.find("  void __tune_sources(std::uint64_t* __lhd_to) const;\n"), std::string::npos);
  EXPECT_EQ(child_hpp.find("__tune_support();"), std::string::npos);
  EXPECT_EQ(child_hpp.find("__tune_sources(std::uint64_t* __lhd_to) const;"), std::string::npos);
  const auto digests = slurp(std::filesystem::path(tag) / "gen_digests.json");
  EXPECT_NE(digests.find("\"tune_id_top.tune-id.cpp\""), std::string::npos) << digests;
  EXPECT_EQ(digests.find("\"tune_id_child.tune-id.cpp\""), std::string::npos) << digests;
}
}  // namespace

TEST(CgenSim, ConstantSpecializationPropagatesThroughEarlierParents) {
  auto&                                     library = livehd::Hhds_graph_library::instance("lgdb_sim_constant_chain");
  std::vector<std::shared_ptr<hhds::Graph>> graphs;
  for (unsigned level = 0; level < 6; ++level) {
    auto io = library.create_io("constant_chain_" + std::to_string(level));
    io->add_input("x", 0);
    io->add_output("y", 1);
    for (auto name : {"x", "y"}) {
      io->set_bits(name, 8);
      io->set_unsign(name, true);
    }
    auto graph = io->create_graph();
    auto value = graph->get_input_pin("x");
    if (level != 0) {
      auto child = gu::create_typed_node(*graph, Ntype_op::Sub);
      child.set_subnode(graphs.back()->get_io());
      value.connect_sink(child.create_sink_pin(0));
      value = child.create_driver_pin(1);
      gu::set_ubits(value, 8);
    }
    auto operation = gu::create_typed_node(*graph, level == 0 ? Ntype_op::And : Ntype_op::Or, 8);
    value.connect_sink(operation.create_sink_pin(0));
    gu::create_const(*graph, *Dlop::create_integer(level == 0 ? 0 : (1 << (level - 1)))).connect_sink(operation.create_sink_pin(1));
    auto result = operation.create_driver_pin(0);
    gu::set_ubits(result, 8);
    result.connect_sink(graph->get_output_pin("y"));
    graphs.push_back(graph);
  }
  const auto top = graphs.back();
  std::ranges::reverse(graphs);  // every parent is visited before its callee
  livehd::sim::specialize_constants(graphs);
  const auto value = top->get_output_pin("y").get_driver_pin();
  ASSERT_TRUE(value.is_const());
  EXPECT_EQ(gu::const_of(value).to_just_i64(), 31);
}

TEST(CgenSim, ConstantSpecializationKeepsInstancesAndRolledCarriesSeparate) {
  auto& library = livehd::Hhds_graph_library::instance("lgdb_sim_constant_instances");
  auto  io      = library.create_io("constant_body");
  io->add_input("seed", 0);
  io->add_input("gate", 1);
  io->add_output("result", 2);
  for (auto field : {"seed", "gate", "result"}) {
    io->set_bits(field, 8);
    io->set_unsign(field, true);
  }
  auto body = io->create_graph();
  auto mask = gu::create_typed_node(*body, Ntype_op::And);
  body->get_input_pin("seed").connect_sink(mask.create_sink_pin(0));
  body->get_input_pin("gate").connect_sink(mask.create_sink_pin(1));
  auto output = mask.create_driver_pin(0);
  gu::set_ubits(output, 8);
  output.connect_sink(body->get_output_pin("result"));
  auto top_io = library.create_io("constant_parent");
  top_io->add_input("gate", 0);
  top_io->set_bits("gate", 8);
  top_io->set_unsign("gate", true);
  for (int i = 0; i < 2; ++i) {
    top_io->add_output("y" + std::to_string(i), i + 1);
    top_io->set_bits("y" + std::to_string(i), 8);
    top_io->set_unsign("y" + std::to_string(i), true);
  }
  auto                          parent = top_io->create_graph();
  std::vector<hhds::Node_class> loops;
  for (int i = 0; i < 2; ++i) {
    auto               loop = gu::create_typed_node(*parent, Ntype_op::Sub);
    hhds::Subnode_loop descriptor;
    descriptor.count = 17;
    descriptor.first = -2;
    descriptor.step  = 3;
    loop.set_subnode(io, descriptor);
    gu::create_const(*parent, *Dlop::create_integer(255)).connect_sink(loop.create_sink_pin(0));
    // 256 must truncate at the U8 instance input boundary, producing zero.
    auto gate = i == 0 ? gu::create_const(*parent, *Dlop::create_integer(256)) : parent->get_input_pin("gate");
    gate.connect_sink(loop.create_sink_pin(1));
    auto result = loop.create_driver_pin(2);
    gu::set_ubits(result, 8);
    result.connect_sink(loop.create_sink_pin(0));
    result.connect_sink(parent->get_output_pin("y" + std::to_string(i)));
    loops.push_back(loop);
  }
  std::vector<std::shared_ptr<hhds::Graph>> graphs{body, parent};
  livehd::sim::specialize_constants(graphs);
  EXPECT_TRUE(parent->get_output_pin("y0").get_driver_pin().is_known_false());
  EXPECT_FALSE(parent->get_output_pin("y1").get_driver_pin().is_const());
  EXPECT_EQ(loops[0].subnode_loop()->count, 17u);
  EXPECT_EQ(loops[0].subnode_loop()->first, -2);
  EXPECT_EQ(loops[0].subnode_loop()->step, 3);
  EXPECT_EQ(loops[1].get_subnode_graph(), body);
  EXPECT_NE(loops[0].get_subnode_graph(), body);
  EXPECT_FALSE(body->get_output_pin("result").get_driver_pin().is_const());
}

TEST(CgenSim, ZeroShiftSpecializationUsesTheLoopDomainAndPortBoundary) {
  auto& library = livehd::Hhds_graph_library::instance("lgdb_sim_zero_index");
  auto  io      = library.create_io("zero_shift_body");
  io->add_input("index", 0);
  io->set_bits("index", 3);
  io->add_output("y", 1);
  io->set_bits("y", 8);
  io->set_unsign("y", true);
  auto body  = io->create_graph();
  auto shift = gu::create_typed_node(*body, Ntype_op::SRA);
  gu::create_const(*body, *Dlop::create_integer(0)).connect_sink(gu::setup_sink_by_name(shift, "a"));
  body->get_input_pin("index").connect_sink(gu::setup_sink_by_name(shift, "b"));
  auto result = shift.create_driver_pin(0);
  gu::set_ubits(result, 8);
  result.connect_sink(body->get_output_pin("y"));
  auto top_io = library.create_io("zero_shift_parent");
  for (int i = 0; i < 3; ++i) {
    top_io->add_output("y" + std::to_string(i), i);
    top_io->set_bits("y" + std::to_string(i), 8);
    top_io->set_unsign("y" + std::to_string(i), true);
  }
  auto                          graph = top_io->create_graph();
  std::vector<hhds::Node_class> loops;
  for (int i = 0; i < 3; ++i) {
    auto               loop = gu::create_typed_node(*graph, Ntype_op::Sub);
    hhds::Subnode_loop descriptor;
    descriptor.index_input = 0;
    descriptor.first       = i == 1 ? -1 : 0;
    descriptor.step        = 1;
    descriptor.count       = i == 2 ? 8 : 4;
    loop.set_subnode(io, descriptor);
    auto y = loop.create_driver_pin(1);
    gu::set_ubits(y, 8);
    y.connect_sink(graph->get_output_pin("y" + std::to_string(i)));
    loops.push_back(loop);
  }
  std::vector<std::shared_ptr<hhds::Graph>> graphs{body, graph};
  livehd::sim::specialize_constants(graphs);
  EXPECT_TRUE(graph->get_output_pin("y0").get_driver_pin().is_known_false());
  EXPECT_EQ(loops[0].subnode_loop()->count, 4u);
  // A negative first index and an S3 index that wraps at 4 cannot use the proof.
  EXPECT_EQ(loops[1].get_subnode_graph(), body);
  EXPECT_EQ(loops[2].get_subnode_graph(), body);
  EXPECT_FALSE(body->get_output_pin("y").get_driver_pin().is_const());
}

TEST(CgenSim, ZeroTripIdentityCarryKeepsTheSeedInputBoundary) {
  auto& library = livehd::Hhds_graph_library::instance("lgdb_sim_zero_trip_seed");
  auto  io      = library.create_io("seed_body");
  io->add_input("seed", 0);
  io->set_bits("seed", 8);
  io->set_unsign("seed", true);
  io->add_output("y", 1);
  io->set_bits("y", 16);
  io->set_unsign("y", true);
  auto body = io->create_graph();
  body->get_input_pin("seed").connect_sink(body->get_output_pin("y"));
  auto top_io = library.create_io("seed_parent");
  top_io->add_output("y", 0);
  top_io->set_bits("y", 16);
  top_io->set_unsign("y", true);
  auto               graph = top_io->create_graph();
  auto               loop  = gu::create_typed_node(*graph, Ntype_op::Sub);
  hhds::Subnode_loop descriptor;
  descriptor.count = 0;
  loop.set_subnode(io, descriptor);
  gu::create_const(*graph, *Dlop::create_integer(511)).connect_sink(loop.create_sink_pin(0));
  auto y = loop.create_driver_pin(1);
  gu::set_ubits(y, 16);
  y.connect_sink(loop.create_sink_pin(0));
  y.connect_sink(graph->get_output_pin("y"));
  std::vector<std::shared_ptr<hhds::Graph>> graphs{body, graph};
  livehd::sim::specialize_constants(graphs);
  const auto output = graph->get_output_pin("y").get_driver_pin();
  ASSERT_TRUE(output.is_const());
  EXPECT_TRUE(gu::const_of(output).eq_op(Dlop::create_integer(255))->is_known_true());
}

// ---- sim.unroll_sites: a small compact loop is spliced flat ------------------
namespace {
struct Unroll_fixture {
  std::shared_ptr<hhds::Graph> parent;
  hhds::Node_class             compact;
};

// A rolled loop over an 8-bit carry whose body is one Not: `count` iterations
// chain `count` inversions of `seed` into `result`.
Unroll_fixture make_unroll_loop(std::string_view tag, uint64_t count) {
  auto& lib = livehd::Hhds_graph_library::instance(std::string("lgdb_cgen_sim_unroll_") + std::string(tag));

  auto body_io = lib.create_io(std::string(tag) + "_body");
  body_io->add_input("acc_in", 0);
  body_io->add_output("acc_out", 1);
  body_io->set_bits("acc_in", 8);
  body_io->set_bits("acc_out", 8);
  auto body = body_io->create_graph();
  auto inv  = gu::create_typed_node(*body, Ntype_op::Not);
  body->get_input_pin("acc_in").connect_sink(inv.create_sink_pin(0));
  inv.create_driver_pin(0).connect_sink(body->get_output_pin("acc_out"));

  auto parent_io = lib.create_io(std::string(tag) + "_parent");
  parent_io->add_input("seed", 0);
  parent_io->add_output("result", 1);
  parent_io->set_bits("seed", 8);
  parent_io->set_bits("result", 8);
  auto parent = parent_io->create_graph();

  auto compact = gu::create_typed_node(*parent, Ntype_op::Sub);
  compact.set_name("u_loop");
  hhds::Subnode_loop loop;
  loop.count = count;
  compact.set_subnode(body_io, loop);
  parent->get_input_pin("seed").connect_sink(compact.create_sink_pin(0));
  auto output = compact.create_driver_pin(1);
  gu::set_bits(output, 8);
  output.connect_sink(parent->get_output_pin("result"));
  output.connect_sink(compact.create_sink_pin(0));  // the carry: acc_out -> acc_in
  compact.subnode_group().validate();
  return {parent, compact};
}

size_t count_loops(const std::shared_ptr<hhds::Graph>& g) {
  size_t n = 0;
  for (auto node : g->body().nodes()) {
    if (node.subnode_loop()) {
      ++n;
    }
  }
  return n;
}
size_t count_subs(const std::shared_ptr<hhds::Graph>& g) {
  size_t n = 0;
  for (auto node : g->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Sub) {
      ++n;
    }
  }
  return n;
}
}  // namespace

// By default (sim.unroll_sites = 0) prepare_graph keeps every compact loop as
// its native call: the compact form is preserved by ruling. With the debug knob
// set, a loop whose flat cost (count x body nodes) is within the ceiling is
// expanded and every replica spliced into the body, so no instance is left,
// while a loop past the ceiling keeps its call. The expansion is a sim-private
// rewrite: it must not touch the body definition the rolled form still calls.
TEST(CgenSim, CompactLoopStaysRolledByDefaultAndSplicesFlatOnlyWhenAsked) {
  const auto dir = (std::filesystem::temp_directory_path() / "cgen_sim_unroll_test").string();
  std::filesystem::create_directories(dir);
  {
    auto     f = make_unroll_loop("default", 4);
    Cgen_sim prep(dir, "", std::string(f.parent->get_name()), "false");
    ASSERT_TRUE(prep.prepare_graph(f.parent));
    EXPECT_EQ(count_loops(f.parent), 1u) << "the default keeps a 4-iteration loop rolled";
  }
  {
    auto     f = make_unroll_loop("small", 4);
    Cgen_sim prep(dir, "", std::string(f.parent->get_name()), "false");
    prep.set_unroll_sites(4096);
    ASSERT_TRUE(prep.prepare_graph(f.parent));
    EXPECT_EQ(count_loops(f.parent), 0u) << "4 x 1 node is under the requested ceiling: the loop is expanded";
    EXPECT_EQ(count_subs(f.parent), 0u) << "every replica is spliced into the parent, none stays an instance";
  }
  {
    auto     f = make_unroll_loop("large", 1'000'000'000ULL);
    Cgen_sim prep(dir, "", std::string(f.parent->get_name()), "false");
    prep.set_unroll_sites(4096);
    ASSERT_TRUE(prep.prepare_graph(f.parent));
    EXPECT_EQ(count_loops(f.parent), 1u) << "a billion-iteration loop is past any ceiling and stays a call";
  }
  {
    // The budget is a ceiling on the FLAT cost, not the trip count: 5 x 1 nodes
    // fits a budget of 5 and not one of 4.
    auto     f = make_unroll_loop("edge", 5);
    Cgen_sim prep(dir, "", std::string(f.parent->get_name()), "false");
    prep.set_unroll_sites(4);
    ASSERT_TRUE(prep.prepare_graph(f.parent));
    EXPECT_EQ(count_loops(f.parent), 1u);
  }
}
