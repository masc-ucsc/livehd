
#include "tree_sitter/api.h"
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>

extern "C" TSLanguage *tree_sitter_pyrope();

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>

std::string read_whole_file(const std::string &filename) {
  auto ss = std::ostringstream{};
  std::ifstream file(filename);
  ss << file.rdbuf();

  return ss.str();
}

int main(int argc, const char **argv) {

  if (argc!=2) {
    std::cerr << "usage:\n";
    std::cerr << "\t" << argv[0] << " prp_file\n";
    exit(-3);
  }

  std::string prp_file(read_whole_file(argv[1]));

  TSParser *parser = ts_parser_new();

  ts_parser_set_language(parser, tree_sitter_pyrope());

    TSTree *tst_tree = ts_parser_parse_string(parser, NULL, prp_file.data(), prp_file.size());


  std::cout << "Code reprinted:" << std::endl;

  std::vector<TSNode> node_stack;
  node_stack.push_back(ts_tree_root_node(tst_tree));

  while (!node_stack.empty()) {
    TSNode current_node = node_stack.back();
    node_stack.pop_back();

    // TSNode ts_node_named_child(TSNode, uint32_t);
    // uint32_t ts_node_named_child_count(TSNode);
    auto num_children = ts_node_child_count(current_node);

    for (int i = num_children - 1; i >= 0; i--) {
      TSNode child = ts_node_child(current_node, i);
      node_stack.push_back(child);
    }

    if (num_children == 0) {
      auto start = ts_node_start_byte(current_node);
      auto end = ts_node_end_byte(current_node);
      auto length = end - start;

      std::string current_code = prp_file.substr(start, length);
      std::cout << current_code << " ";
      if (current_code == ";") {
        std::cout << std::endl;
      }
    }
  }
  std::cout << std::endl;

  ts_parser_delete(parser);
  return 0;
}

// command line to compile:
//  g++ -std=c++17 -I ./tree-sitter-verilog/node_modules/tree-sitter/vendor/tree-sitter/lib/include/ -I ./tree-sitter-verilog/node_modules/tree-sitter/vendor/tree-sitter/lib/src/ tokenizer.cpp ./tree-sitter-verilog/build/Release/obj.target/tree_sitter_verilog_binding/src/parser.o ./tree-sitter-verilog/node_modules/tree-sitter/build/Release/tree_sitter.a
