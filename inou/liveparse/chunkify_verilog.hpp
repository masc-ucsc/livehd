#pragma once

#include "elab_scanner.hpp"
#include "lgedge.hpp"

class Graph_library;

class Chunkify_verilog : public Elab_scanner {
protected:
  const std::string path;
  const std::string elab_path;

  Graph_library *library;
  Graph_library *elab_library;

  int open_write_file(const std::string &filename) const;

  bool is_same_file(const std::string &filename, const std::string &text1, const std::string &text2) const;

  void write_file(const std::string &filename, const std::string &text1, const std::string &text2) const;
  void write_file(const std::string &filename, const char *text, int sz) const;

  void add_io(bool input, const std::string &mod_name, const std::string &io_name, Port_ID original_pos);
public:
  Chunkify_verilog(const std::string &outd, const std::string &_elab_path);
  void elaborate();
};

