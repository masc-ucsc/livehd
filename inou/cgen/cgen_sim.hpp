// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "file_output.hpp"
#include "hhds/graph.hpp"
#include "hhds/index.hpp"

// Cgen_sim — lower one hhds::Graph to a C++ Slop<N> struct over the ../hlop
// library (TODO 3d, inou.cgen.sim). Structural twin of Cgen_verilog (same
// forward_class() walk + Ntype_op dispatch via livehd::graph_util), but emits a
// functional `Out cycle(In)` struct instead of inlined Verilog: one flat SSA
// binding (Slop<W> v = a.op(b);) per node, registers as struct members. Each
// module is split into <name>.hpp (the interface: data members, In/Out, method
// declarations — what an instantiating module #includes) and <name>.cpp (the
// bodies, "the slop") so a body edit recompiles one .o and a module appears once
// however many times it is instantiated. The standalone Bazel module scaffold
// (MODULE.bazel / BUILD / manifest) is written by the kernel's emit_sim_outputs.
class Cgen_sim {
private:
  std::string_view odir;

  using pin_key_t = hhds::Class_index;
  // driver pin -> the C++ expression naming its current value: an input field
  // ("in.a"), a flop member ("q"), or a combinational temp ("cg_3").
  absl::flat_hash_map<pin_key_t, std::string> pin2var;
  int                                          tmp_cnt = 0;

  // Stage 0 combinational-loop safety net. A sim module is ONE sequential
  // `cycle()` schedule, so a combinational cycle -- a real loop, or a FALSE loop
  // through an atomic Sub call (sub output feeds back through parent comb logic
  // into one of the sub's own inputs) -- has no valid emission order. operand()
  // would otherwise silently substitute `create_integer(0)` for an unschedulable
  // back-edge sink, producing a WRONG simulation with no diagnostic. These flags
  // turn that into a loud, located build failure.
  bool        cycle_unresolved_ = false;  // hit an unschedulable comb-cycle back-edge this graph
  bool        cycle_reported_   = false;  // a located error was already emitted for this graph
  std::string cycle_first_label_;         // first offending value (for the generic message)

  static std::string     cpp_id(std::string_view name);  // sanitize to a valid C++ identifier
  static hhds::Pin_class get_driver(const hhds::Pin_class& sink);
  static hhds::Pin_class find_sink_pin(const hhds::Node_class& node, std::string_view name);

  // Raw name of the input port clocking `g` (flop clock_pin, else recursively a
  // sub-instance's clock port); "" = none. Memoized in clk_memo_.
  std::string                                   clock_input_of(hhds::Graph* g);
  absl::flat_hash_map<std::string, std::string> clk_memo_;

  // Resolve a driver pin to a Slop<target_bits> C++ EXPRESSION: a constant ->
  // Slop<W>::from_pyrope("..."); otherwise the named value width-converted to W.
  // sign_mode: 0 = per is_unsign(pin), +1 = force signed (Slop<W>{...}, sext),
  // -1 = force unsigned (.zext_to<W>(), zext).
  std::string operand(const hhds::Pin_class& dpin, int target_bits, int sign_mode = 0);
  // The RHS Slop<wbits> expression for one combinational node.
  std::string node_expr(const hhds::Node_class& node, int wbits);

  std::string vcd_file;       // --set compile.sim.vcd=FILE ("" = no VCD)
  std::string top;            // --top: only this module bakes the VCD path (avoids file collisions)
  bool        vcd_fakedelay;  // --set compile.sim.vcdfakedelay: data settles at edge+3 with an X window (default);
                              // false = plain edge-aligned updates (no X, no delay)

public:
  void do_from_graph(const std::shared_ptr<hhds::Graph>& graph);
  Cgen_sim(std::string_view _odir, std::string_view _vcd, std::string_view _top, std::string_view _fakedelay)
      : odir(_odir), vcd_file(_vcd), top(_top),
        vcd_fakedelay(!(_fakedelay == "false" || _fakedelay == "0" || _fakedelay == "off")) {}
};
