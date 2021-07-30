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

  void format_append(std::string &text) const {
    assert(scanner_pos < token_list.size());
    auto txt = token_list[scanner_pos].get_text().to_s();

    if (scanner_pos > 0) {
      if (token_list[scanner_pos - 1].line != token_list[scanner_pos].line) {
        absl::StrAppend(&text, "\n", txt);
        return;
      }
      absl::StrAppend(&text, " ", txt);
      return;
    }
    absl::StrAppend(&text, txt);
  }

  int open_write_file(const mmap_lib::str &filename) const;

  bool is_same_file(const mmap_lib::str &filename, std::string_view text1, std::string_view text2) const;

  void write_file(const mmap_lib::str &filename, std::string_view text1, std::string_view text2) const;
  void write_file(const mmap_lib::str &filename, std::string_view text) const;

  void add_io(Sub_node *sub, bool input, const mmap_lib::str &io_name, Port_ID pos);

public:
  Chunkify_verilog(const mmap_lib::str &outd);
  void elaborate();
};
