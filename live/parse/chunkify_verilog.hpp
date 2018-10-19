#pragma once

#include "eprp_scanner.hpp"

class Chunkify_verilog : public Eprp_scanner {
protected:
  const std::string path;

  int open_write_file(const std::string &filename);
  void write_file(const std::string &filename, const std::string &text1, const std::string &text2);
  void write_file(const std::string &filename, const char *text, int sz);
public:
  Chunkify_verilog(const std::string &outd);
  void elaborate();
};

