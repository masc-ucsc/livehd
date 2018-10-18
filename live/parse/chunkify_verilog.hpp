#pragma once

#include "eprp_scanner.hpp"

class Chunkify_verilog : public Eprp_scanner {
public:
  Chunkify_verilog(const std::string &outd);
  void elaborate();
};

