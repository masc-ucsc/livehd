// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "abc_arith.hpp"  // arith::Adder_kind
#include "hhds/graph.hpp"
#include "pass_partition.hpp"  // livehd::partition::Region_body

namespace livehd::abc {

struct Map_options {
  std::string library;       // Liberty .lib for read_lib
  std::string flow;          // ABC command string (empty => built-in default)
  bool        seq     = false;
  std::string delay;         // {D} substitution
  std::string load;          // {L} substitution
  bool        verbose = false;
  // Combinational adder architecture for Sum/comparators (2i-abc_arith) and the
  // CSKA/CLA block width (0 => auto from the operating width).
  arith::Adder_kind adder      = arith::Adder_kind::rca;
  int               block_size = 0;
};

// Stats-only mode (no --emit-dir): summarize what would be mapped.
void report_stats(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, std::string_view top, const Map_options& opts);

// Drives the ABC frame across a whole decomposition. One Abc_Start/Stop per
// run; read_lib once before the region loop; the current network is reset per
// region. Each region body is rebuilt as a standard-cell netlist of 1-bit
// blackbox Sub cells.
class Mapper {
public:
  explicit Mapper(const Map_options& opts) : opts_(opts) {}

  bool start();  // Abc_Start + read_lib (false + diag on failure)
  void stop();   // Abc_Stop
  void map_region(const livehd::partition::Region_body& rb);

  void set_outlib(hhds::GraphLibrary* l) { outlib_ = l; }

private:
  Map_options         opts_;
  void*               pabc_       = nullptr;  // Abc_Frame_t*
  bool                lib_loaded_ = false;
  hhds::GraphLibrary* outlib_     = nullptr;  // where blackbox cell defs are declared

  [[nodiscard]] std::string comb_flow() const;
};

}  // namespace livehd::abc
