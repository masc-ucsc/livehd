// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <vector>

#include "absl/strings/str_cat.h"
#include "cgen_verilog.hpp"
#include "iassert.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

bool Cgen_verilog::structural_wiring_graph(hhds::Graph* graph) {
  for (auto node : graph->body().nodes()) {
    const auto op = gu::type_op_of(node);
    if (op == Ntype_op::Sub) {
      const auto io = node.get_subnode_io();
      if (!io || node.is_loop_subnode() || io->get_name() == gu::lgassert_module_name || io->get_name() == gu::fproperty_module_name
          || io->get_name() == gu::lgundef_module_name) {
        return false;
      }
    } else if (op == Ntype_op::Memory) {
      // Array-state emission needs procedural variables. Ordinary native RAM
      // wrappers can coexist with declarative wiring around their ports.
      const auto type = gu::get_driver_of_sink_name(node, "type");
      if (type.is_invalid() || !type.is_const() || !gu::const_of(type).is_just_i64() || gu::const_of(type).to_just_i64() == 2) {
        return false;
      }
    } else if (op == Ntype_op::Get_mask) {
      if (!gu::bit_range(node).has_value()) {
        return false;
      }
    } else if (op == Ntype_op::Sext) {
      const auto keep = gu::get_driver_of_sink_name(node, "b");
      if (keep.is_invalid() || !keep.is_const() || !gu::const_of(keep).is_just_i64()) {
        return false;
      }
    } else if (op == Ntype_op::Concat) {
      if (gu::concat_lanes(node).empty()) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

std::string Cgen_verilog::structural_bit(const hhds::Pin_class& pin, int64_t bit) const {
  I(!pin.is_invalid() && bit >= 0);
  if (pin.is_const()) {
    const auto& value = gu::const_of(pin);
    if (value.unknown_bit_test(bit)) {
      return "1'bx";
    }
    if (bit >= value.get_signed_bits()) {
      return value.is_negative() ? "1'b1" : "1'b0";
    }
    return value.bit_test(bit) ? "1'b1" : "1'b0";
  }
  const auto width = std::max(1, decl_bits_of(pin));
  if (bit >= width) {
    if (gu::is_unsign(pin)) {
      return "1'b0";
    }
    bit = width - 1;
  }
  const auto& name = pin2var.at(pin.get_class_index());
  return width == 1 ? name : absl::StrCat(name, "[", bit, "]");
}

std::string Cgen_verilog::structural_range(const hhds::Pin_class& pin, int width, int64_t low, bool sized_hex) const {
  width = std::max(width, 1);
  I(low >= 0);
  if (sized_hex && pin.is_const() && width <= 64) {
    // An instance pin connection keeps the sized-hex literal the procedural
    // emitter prints for a fully known constant (`1'h1`), not a bit concatenation.
    uint64_t value = 0;
    bool     known = true;
    for (int bit = 0; bit < width && known; ++bit) {
      const auto text  = structural_bit(pin, low + bit);
      known            = text != "1'bx";
      value           |= (text == "1'b1" ? uint64_t{1} : uint64_t{0}) << bit;
    }
    if (known) {
      return absl::StrCat(width, "'h", absl::Hex(value));
    }
  }
  if (!pin.is_const() && low == 0 && width == std::max(1, decl_bits_of(pin))) {
    return pin2var.at(pin.get_class_index());
  }
  if (width == 1) {
    return structural_bit(pin, low);
  }
  if (!pin.is_const()) {
    // Make extension explicit, including instance inputs. OpenSTA rejects a
    // short bus connected to a wider Liberty port instead of extending it,
    // leaving the entire port disconnected from timing.
    const auto declared = std::max(1, decl_bits_of(pin));
    if (low == 0 && width > declared && gu::is_unsign(pin)) {
      return absl::StrCat("{", width - declared, "'b0,", pin2var.at(pin.get_class_index()), "}");
    }
    if (declared > 1 && low + width <= declared) {
      return absl::StrCat(pin2var.at(pin.get_class_index()), "[", low + width - 1, ":", low, "]");
    }
  }
  std::string result = "{";
  for (int bit = width - 1; bit >= 0; --bit) {
    if (bit != width - 1) {
      result += ",";
    }
    result += structural_bit(pin, low + bit);
  }
  result += "}";
  return result;
}

void Cgen_verilog::create_structural_wiring(std::shared_ptr<File_output> fout, hhds::Graph* graph) {
  for (auto node : graph->body().nodes(hhds::Node_order::forward)) {
    const auto op = gu::type_op_of(node);
    if (op == Ntype_op::Sub || op == Ntype_op::Memory || !node.has_out_edges()) {
      continue;
    }
    const auto               pin   = node.get_driver_pin(0);
    const int                width = std::max(1, decl_bits_of(pin));
    std::vector<std::string> bits(static_cast<size_t>(width));  // least significant first
    if (op == Ntype_op::Get_mask) {
      const auto source      = gu::get_driver_of_sink_name(node, "a");
      const auto [low, high] = *gu::bit_range(node);
      for (int bit = 0; bit < width; ++bit) {
        bits[bit] = bit < high - low ? structural_bit(source, low + bit) : "1'b0";
      }
    } else if (op == Ntype_op::Sext) {
      const auto source = gu::get_driver_of_sink_name(node, "a");
      const auto keep   = gu::const_of(gu::get_driver_of_sink_name(node, "b")).to_just_i64();
      for (int bit = 0; bit < width; ++bit) {
        bits[bit] = structural_bit(source, keep > 0 ? std::min<int64_t>(bit, keep - 1) : bit);
      }
    } else {
      I(op == Ntype_op::Concat);
      // concat_lanes returns most-significant lane first, as in Verilog.
      const auto lanes = gu::concat_lanes(node);
      I(!lanes.empty());
      int offset = 0;
      for (auto lane = lanes.rbegin(); lane != lanes.rend(); ++lane) {
        for (int bit = 0; bit < lane->width && offset < width; ++bit, ++offset) {
          bits[offset] = structural_bit(lane->value, bit);
        }
      }
      while (offset < width) {
        bits[offset++] = "1'b0";
      }
    }
    std::string expression;
    for (auto it = bits.rbegin(); it != bits.rend(); ++it) {
      if (!expression.empty()) {
        expression += ",";
      }
      expression += *it;
    }
    if (width > 1) {
      expression = "{" + expression + "}";
    }
    note_src(fout, node);
    fout->append("assign ", pin2var.at(pin.get_class_index()), " = ", expression, ";\n");
  }
  const auto io = graph->get_io();
  for (const auto& decl : io->get_output_pin_decls()) {
    const auto sink   = graph->get_output_pin(decl.name);
    const auto driver = sink.is_invalid() ? hhds::Pin_class{} : get_driver(sink);
    if (driver.is_invalid()) {
      continue;
    }
    const auto name       = get_scaped_name(decl.name);
    const int  width      = gu::bits_of(sink, *io, decl.name);
    const auto expression = structural_range(driver, width);
    if (name != expression) {
      note_src(fout, driver.get_master_node());
      fout->append("assign ", name, " = ", expression, ";\n");
    }
  }
}
