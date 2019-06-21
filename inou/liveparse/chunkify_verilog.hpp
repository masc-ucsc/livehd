//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "lgedge.hpp"

class Graph_library;
class Sub_node;

class Chunkify_verilog : public Elab_scanner {
protected:
  std::string_view path;
  std::string_view elab_path;

  std::string chunk_dir;
  std::string elab_chunk_dir;

  Graph_library *library;
  Graph_library *elab_library;

  int open_write_file(std::string_view filename) const;

  bool is_same_file(std::string_view filename, std::string_view text1, std::string_view text2) const;

  void write_file(std::string_view filename, std::string_view text1, std::string_view text2) const;
  void write_file(std::string_view filename, std::string_view text) const;

  void add_io(Sub_node *sub, bool input, std::string_view io_name, Port_ID pos);

public:
  Chunkify_verilog(std::string_view outd, std::string_view _elab_path);
  void elaborate();
};
