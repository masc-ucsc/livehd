//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "lgedge.hpp"

class Graph_library;
class Sub_node;

class Chunkify_verilog : public Elab_scanner {
protected:
  mmap_lib::str path;
  mmap_lib::str elab_path;

  mmap_lib::str chunk_dir;
  mmap_lib::str elab_chunk_dir;

  Graph_library *library;

  int open_write_file(const mmap_lib::str &filename) const;

  bool is_same_file(const mmap_lib::str &filename, const mmap_lib::str &text1, const mmap_lib::str &text2) const;

  void write_file(const mmap_lib::str &filename, const mmap_lib::str &text1, const mmap_lib::str &text2) const;
  void write_file(const mmap_lib::str &filename, const mmap_lib::str &text) const;

  void add_io(Sub_node *sub, bool input, const mmap_lib::str &io_name, Port_ID pos);

public:
  Chunkify_verilog(const mmap_lib::str &outd);
  void elaborate();
};
