// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
}  // namespace
