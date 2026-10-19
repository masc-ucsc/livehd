// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <string>
#include <vector>

#include "node_util.hpp"
#include "sweep.hpp"

namespace livehd::sweep {
namespace gu = livehd::graph_util;

namespace {
// `c` with every unknown bit read as 0. An unknown sign extension reads as
// positive, so the result is the two's-complement pattern of the known bits.
Dlop clear_unknown(const Dlop& c) {
  const int         width = std::max(c.get_signed_bits(), 1);
  std::string       text(static_cast<size_t>(width), '0');  // MSB first
  for (int i = 0; i < width; ++i) {
    const bool bit = !c.unknown_bit_test(i) && c.bit_test(i);
    text[static_cast<size_t>(width - 1 - i)] = bit ? '1' : '0';
  }
  // `unsigned_result == false`: the MSB is the sign bit, as in the source value.
  return *Dlop::from_binary(text, false);
}
}  // namespace

size_t refine_unknown(hhds::Graph& g) {
  struct Rewire {
    hhds::Pin_class sink;
    hhds::Pin_class driver;
  };
  std::vector<Rewire> todo;
  for (auto node : g.body().nodes()) {
    const auto op = gu::type_op_of(node);
    // State and instance pins keep their unknowns: a flop's `initial` unknown means "no
    // power-on value", a memory's `undef` matrix marks a don't-care read, an instance's
    // inputs belong to its callee. Only values that flow through logic are refined.
    if (op == Ntype_op::Memory || op == Ntype_op::Latch || op == Ntype_op::Fflop || op == Ntype_op::Sub || op == Ntype_op::AttrSet) {
      continue;
    }
    for (auto sink : node.inp_sorted_pins()) {
      if (op == Ntype_op::Flop && Ntype::get_sink_name(Ntype_op::Flop, sink.get_port_id()) != "din") {
        continue;
      }
      for (auto driver : sink.get_driver_pins()) {
        if (driver.is_const() && gu::const_of(driver).has_unknowns() && gu::const_of(driver).is_integer()) {
          todo.push_back({sink, driver});
        }
      }
    }
  }
  for (auto sink : g.get_output_node().inp_sorted_pins()) {
    for (auto driver : sink.get_driver_pins()) {
      if (driver.is_const() && gu::const_of(driver).has_unknowns() && gu::const_of(driver).is_integer()) {
        todo.push_back({sink, driver});
      }
    }
  }
  size_t n = 0;
  for (const auto& r : todo) {
    const auto  refined = gu::create_const(g, clear_unknown(gu::const_of(r.driver)));
    r.sink.del_sink(r.driver);
    refined.connect_sink(r.sink);
    ++n;
  }
  return n;
}

}  // namespace livehd::sweep
