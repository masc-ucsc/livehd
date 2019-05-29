//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "gtest/gtest.h"
#include "fmt/format.h"
#include "lnast.hpp"

using tuple = std::tuple<std::string, uint8_t , uint8_t>;// <node_name, node_type, scope>

int main(int argc, char **argv) {

  if(argc != 2) {
    fprintf(stderr, "Usage:\n\t%s cfg\n", argv[0]);
    exit(-3);
  }

  int fd = open(argv[1], O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "error, could not open %s\n", argv[1]);
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);
  printf("Size: %lu\n", (uint64_t)sb.st_size);

  char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(memblock == MAP_FAILED) {
    fprintf(stderr, "error, mmap failed\n");
    exit(-3);
  }

  Lnast_parser lnast_parser;

  std::vector<Token> tlist;
  lnast_parser.parse(argv[1], memblock, tlist, sb.st_size);

  std::vector<std::vector<tuple>> ast_sorted_testee;
  lnast_parser.get_ast()->each_breadth_first_fast([&lnast_parser, &ast_sorted_testee](const Tree_index &parent, const Tree_index &self, const Lnast_node &node_data) {
    while (static_cast<size_t>(self.level)>=ast_sorted_testee.size())
      ast_sorted_testee.emplace_back();

    std::string node_name(lnast_parser.scan_text(node_data.node_name));
    auto        node_type  = node_data.node_type;
    auto        node_scope = node_data.scope;
    fmt::print("nname:{}, ntype:{}, nscope:{}\n", node_name, node_type, node_scope);

    tuple tuple_data = std::make_tuple(node_name, node_type, node_scope);
    ast_sorted_testee[self.level].emplace_back(tuple_data);
  });
}
