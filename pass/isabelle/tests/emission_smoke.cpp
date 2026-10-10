// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass_isabelle.hpp"

namespace {
std::filesystem::path fixture_root() {
  if (const char* path = std::getenv("ISABELLE_EMISSION_FIXTURES")) {
    return path;
  }
  if (const char* path = std::getenv("TEST_TMPDIR")) {
    return std::filesystem::path(path) / "isabelle_emission";
  }
  return "generated/isabelle_emission";
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream      file(path);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

TEST(IsabelleEmission, SignedShiftPreservesSignWhenWidening) {
  const auto root = fixture_root();
  std::filesystem::create_directories(root);
  auto& library = livehd::Hhds_graph_library::instance((root / "shift_graph").string());
  auto  io      = library.create_io("restore_shift");
  io->add_input("a", 1);
  io->set_bits("a", 4);
  io->add_output("y", 1);
  io->set_bits("y", 8);
  auto graph = io->create_graph();
  livehd::graph_util::set_bits(graph->get_input_pin("a"), 4);
  auto shift = livehd::graph_util::create_typed_node(*graph, Ntype_op::SRA);
  livehd::graph_util::set_bits(shift.create_driver_pin(0), 8);
  graph->get_input_pin("a").connect_sink(shift.create_sink_pin(0));
  livehd::graph_util::create_const(*graph, *Dlop::create_integer(1)).connect_sink(shift.create_sink_pin(1));
  shift.create_driver_pin(0).connect_sink(graph->get_output_pin("y"));
  Eprp_var var;
  var.add("path", root.string());
  var.add("cert_wf", "skip");
  var.add(graph);
  Pass_isabelle::work(var);
  const auto model = read_file(root / "restore_shift_Lgraph.thy");
  ASSERT_NE(model.find("scast (sem_sra"), std::string::npos);
  EXPECT_EQ(model.find("ucast (sem_sra"), std::string::npos);
  EXPECT_NE(read_file(root / "restore_shift_Lgraph_Cert.thy").find("Op_SRA"), std::string::npos);

  // Evaluated by the separate Isabelle session, not asserted from C++ text.
  std::ofstream oracle(root / "RestoreShiftOracle.thy");
  oracle << R"ISA(theory RestoreShiftOracle
  imports restore_shift_Lgraph
begin
lemma negative_widen: "out_y (restore_shift_comb \<lparr>in_a = 12\<rparr>) = (254 :: 8 word)"
  by eval
lemma positive_widen: "out_y (restore_shift_comb \<lparr>in_a = 6\<rparr>) = (3 :: 8 word)"
  by eval
lemma zero_widen: "out_y (restore_shift_comb \<lparr>in_a = 0\<rparr>) = (0 :: 8 word)"
  by eval
lemma minimum_widen: "out_y (restore_shift_comb \<lparr>in_a = 8\<rparr>) = (252 :: 8 word)"
  by eval
end
)ISA";
  ASSERT_TRUE(oracle.good());
}

TEST(IsabelleEmission, BridgeScaffoldingCoversCombinationalAndSequentialConstants) {
  using namespace livehd::graph_util;
  const auto root = fixture_root();
  std::filesystem::create_directories(root);
  for (const bool sequential : {false, true}) {
    const std::string name = sequential ? "restore_seq" : "restore_comb";
    auto& library = livehd::Hhds_graph_library::instance((root / (name + "_graph")).string());
    auto io = library.create_io(name);
    io->add_input("a", 1);
    io->set_bits("a", 8);
    io->add_output("y", 1);
    io->set_bits("y", 8);
    if (sequential) {
      io->add_output("q", 2);
      io->set_bits("q", 8);
    }
    auto graph = io->create_graph();
    set_bits(graph->get_input_pin("a"), 8);
    auto node = create_typed_node(*graph, Ntype_op::Or);
    set_bits(node.create_driver_pin(0), 8);
    graph->get_input_pin("a").connect_sink(node.create_sink_pin(0));
    create_const(*graph, *Dlop::create_integer(3)).connect_sink(node.create_sink_pin(1));
    node.create_driver_pin(0).connect_sink(graph->get_output_pin("y"));
    std::string field;
    if (sequential) {
      auto flop = create_typed_node(*graph, Ntype_op::Flop);
      set_bits(flop.create_driver_pin(0), 8);
      node.create_driver_pin(0).connect_sink(flop.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Flop, "din")));
      flop.create_driver_pin(0).connect_sink(graph->get_output_pin("q"));
      field = "st_flop_" + std::to_string(flop.get_debug_nid());
    }
    Eprp_var var;
    var.add("path", root.string());
    var.add("cert_wf", "eval");
    var.add("emit_fast_bridge", "true");
    var.add(graph);
    Pass_isabelle::work(var);
    const auto model = read_file(root / (name + "_Lgraph.thy"));
    const auto cert = read_file(root / (name + "_Lgraph_Cert.thy"));
    const std::regex node_id("nid = ([0-9]+)");
    std::set<std::string> ids;
    for (auto it = std::sregex_iterator(cert.begin(), cert.end(), node_id); it != std::sregex_iterator(); ++it) {
      ids.insert((*it)[1].str());
    }
    ASSERT_EQ(ids.size(), 2);  // One operator and one promoted constant.
    for (const auto& id : ids) {
      EXPECT_NE((model + cert).find("definition " + name + "_fv" + id + " ::"), std::string::npos);
    }
    const std::string theory = name + "_Oracle";
    std::ofstream oracle(root / (theory + ".thy"));
    oracle << "theory " << theory << "\n  imports " << name << "_Lgraph_Cert\nbegin\n";
    if (sequential) {
      const std::string args = " \\<lparr>in_a = 4\\<rparr> \\<lparr>" + field + " = 5\\<rparr>";
      oracle << "lemma comb_value: \"out_y (" << name << "_comb" << args << ") = (7 :: 8 word)\" by eval\n";
      oracle << "lemma old_state: \"out_q (" << name << "_comb" << args << ") = (5 :: 8 word)\" by eval\n";
      oracle << "lemma next_value: \"" << field << " (" << name << "_next" << args << ") = (7 :: 8 word)\" by eval\n";
      oracle << "lemma concrete_bridge: \"" << name << "_step" << args << " = " << name << "_cert_step" << args << "\" by eval\n";
    } else {
      oracle << "lemma comb_value: \"out_y (" << name << "_comb \\<lparr>in_a = 4\\<rparr>) = (7 :: 8 word)\" by eval\n";
    }
    oracle << "end\n";
    ASSERT_TRUE(oracle.good());
  }
}

TEST(IsabelleEmission, BridgeScaffoldingRefusesMemoryCertificateStubs) {
  using namespace livehd::graph_util;
  const auto root = fixture_root();
  auto& library = livehd::Hhds_graph_library::instance((root / "memory_graph").string());
  auto io = library.create_io("restore_memory");
  io->add_output("y", 1);
  io->set_bits("y", 8);
  auto graph = io->create_graph();
  auto memory = create_typed_node(*graph, Ntype_op::Memory);
  set_bits(memory.create_driver_pin(0), 8);
  const auto policy = [&](std::string_view name, int64_t value) {
    create_const(*graph, *Dlop::create_integer(value))
        .connect_sink(memory.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name)));
  };
  policy("bits", 8);
  policy("size", 2);
  policy("type", 0);
  policy("rdport", 1);
  policy("addr", 0);
  policy("enable", 1);
  memory.create_driver_pin(0).connect_sink(graph->get_output_pin("y"));
  Eprp_var var;
  var.add("path", root.string());
  var.add(graph);
  ASSERT_NO_THROW(Pass_isabelle::work(var));
  var.add("emit_fast_bridge", "true");
  try {
    Pass_isabelle::work(var);
    FAIL() << "A counts-only memory certificate cannot support bridge scaffolding";
  } catch (const std::exception& error) {
    EXPECT_NE(std::string(error.what()).find("emit_fast_bridge is not supported for designs with memory nodes"), std::string::npos);
  }
}
}  // namespace
