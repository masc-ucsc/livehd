//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "lgedge.hpp"

class Graph_library;
class Sub_node;

class Chunkify_verilog : public Elab_scanner {
private:
  static inline int trace_module_cnt = 0;
protected:
  std::string path;
  std::string chunk_dir;

  bool incremental_mode;

  std::vector<std::string> generated_files;

  Graph_library *library;

  void format_append(std::string &text) const {
    assert(scanner_pos < token_list.size());
    auto txt = token_list[scanner_pos].get_text();

    if (scanner_pos > 0) {
      if (token_list[scanner_pos - 1].line != token_list[scanner_pos].line) {
        absl::StrAppend(&text, "\n", txt);
        return;
      }
      if (token_list[scanner_pos - 1].pos2 == token_list[scanner_pos].pos1) {
        absl::StrAppend(&text, txt);
      }else{
        absl::StrAppend(&text, " ", txt);
      }
      return;
    }
    absl::StrAppend(&text, txt);
  }

  int open_write_file(std::string_view filename) const;

  bool is_same_file(std::string_view filename, std::string_view text1, std::string_view text2) const;

  void write_file(std::string_view filename, std::string_view text1, std::string_view text2) const;
  void write_file(std::string_view filename, std::string_view text) const;

  void add_io(Sub_node *sub, bool input, std::string_view io_name, Port_ID pos);

public:
  Chunkify_verilog(std::string_view path, bool incremental_mode);
  void elaborate() final;

  const std::vector<std::string> &get_generated_files() const {
    return generated_files;
  }
};
