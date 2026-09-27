// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Re-export a saved, normalized LGraph without rerunning RTL elaboration.
#include <filesystem>
#include <iostream>

#include "graph_library_singleton.hpp"
#include "pass_lean.hpp"

int main(int argc, char** argv) {
  if (argc < 4 || argc > 5) {
    std::cerr << "usage: lean_export_graph LGDB TOP OUTPUT_DIR [legacy|verified_compiler]\n";
    return 2;
  }
  try {
    auto&      library = livehd::Hhds_graph_library::instance(argv[1]);
    const auto io      = library.find_io(argv[2]);
    if (!io) {
      throw std::runtime_error("graph not found: " + std::string(argv[2]));
    }
    const auto graph = io->get_graph();
    if (!graph) {
      throw std::runtime_error("graph has no body");
    }
    std::filesystem::create_directories(argv[3]);
    Eprp_var var;
    var.add("path", argv[3]);
    var.add("top", argv[2]);
    var.add("mode", argc == 5 ? argv[4] : "verified_compiler");
    var.add("max_width", "unlimited");
    var.add(graph);
    Pass_lean::work(var);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
