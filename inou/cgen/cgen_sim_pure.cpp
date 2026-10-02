// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <functional>
#include <optional>

#include "absl/strings/str_cat.h"
#include "cgen_sim.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;

bool Cgen_sim::pure_graph(hhds::Graph* graph) {
  if (!graph || observation_on || !vcd_file.empty()) {
    return false;
  }
  if (auto it = pure_graphs_.find(graph); it != pure_graphs_.end()) {
    return it->second;
  }
  // The functional ABI uses the declared port widths. An unfinished or
  // differently realized interface stays on the scheduled path.
  for (const auto& [port, input] : graph->get_io()->decls_in_port_order()) {
    const auto pin = input ? graph->get_input_pin(port->name) : graph->get_output_pin(port->name);
    if (port->bits == 0 || gu::bits_of(pin, *graph->get_io(), port->name) != static_cast<int>(port->bits)) {
      return false;
    }
  }
  // Recursion and unresolved black boxes must stay on the scheduled path.
  pure_graphs_[graph] = false;
  for (auto node : graph->body().nodes()) {
    const auto op = gu::type_op_of(node);
    if (gu::is_type_register(node) || op == Ntype_op::Memory || op == Ntype_op::Clock_cell || op == Ntype_op::AttrSet
        || op == Ntype_op::Invalid) {
      return false;
    }
    if (op == Ntype_op::Sub && !pure_graph(node.get_subnode_graph().get())) {
      return false;
    }
    for (auto sink : node.inp_sorted_pins()) {
      for (auto driver : sink.get_driver_pins()) {
        if (driver.is_const() && gu::const_of(driver).has_unknowns() && !unknown_zero_) {
          return false;
        }
      }
    }
  }
  return pure_graphs_[graph] = true;
}

std::string Cgen_sim::pure_parameters(const hhds::GraphIO& io) {
  std::string result;
  for (const auto& port : io.get_input_pin_decls()) {
    if (!result.empty()) {
      result += ", ";
    }
    absl::StrAppend(&result,
                    port.bits > 0 && port.bits <= 30
                        ? "int64_t "
                        : absl::StrCat("const ", stored_type(std::max<uint32_t>(1, port.bits), port.unsign), "& "),
                    "__p",
                    port.port_id);
  }
  return result;
}

std::string Cgen_sim::pure_argument(std::string expression, const hhds::GraphIO::DeclaredIoPin& port) {
  if (port.bits == 0 || port.bits > 30) {
    return expression;
  }
  if (port.unsign) {
    expression = absl::StrCat("(", expression, ").zext_to<", port.bits + 1, ">()");
  } else {
    expression = absl::StrCat("Slop<", port.bits + 1, ">{Slop<", port.bits, ">{", expression, "}}");
  }
  return absl::StrCat("(", expression, ").to_i64_low()");
}

// Native arithmetic is used only when all operands have independently bounded
// integer values. No host overflow, negative left shift, or oversized shift is
// introduced; the generic Slop expression remains the fallback.
std::optional<Cgen_sim::Native_value> Cgen_sim::native_expression(const hhds::Node_class& node) {
  const auto                edges = node.inp_pins_snapshot();
  std::vector<Native_value> args;
  for (const auto& edge : edges) {
    const auto driver = edge.get_driver_pin();
    if (driver.is_const()) {
      const auto& value = gu::const_of(driver);
      if (!value.is_integer() || !value.is_just_i64()) {
        return {};
      }
      const auto n = value.to_just_i64();
      args.push_back({std::to_string(n), n, n});
    } else if (auto it = native_values_.find(driver.get_class_index()); it != native_values_.end()) {
      args.push_back(it->second);
    } else {
      return {};
    }
    if (args.back().lo < -(int64_t{1} << 30) || args.back().hi > (int64_t{1} << 30)) {
      return {};
    }
  }
  if (args.empty() || args.size() > 8) {
    return {};
  }
  const auto op    = gu::type_op_of(node);
  auto       value = args.front();
  if (op == Ntype_op::Sum) {
    value = {"int64_t{0}", 0, 0};
    for (size_t i = 0; i < args.size(); ++i) {
      const bool add    = Ntype::sink_bank(op, edges[i].get_port_id()) == 0;
      value.expression  = absl::StrCat("(", value.expression, add ? " + " : " - ", args[i].expression, ")");
      value.lo         += add ? args[i].lo : -args[i].hi;
      value.hi         += add ? args[i].hi : -args[i].lo;
    }
  } else if (op == Ntype_op::Mult && args.size() == 2) {
    const int64_t products[] = {args[0].lo * args[1].lo, args[0].lo * args[1].hi, args[0].hi * args[1].lo, args[0].hi * args[1].hi};
    value                    = {absl::StrCat("(", args[0].expression, " * ", args[1].expression, ")"),
                                *std::min_element(std::begin(products), std::end(products)),
                                *std::max_element(std::begin(products), std::end(products))};
  } else if ((op == Ntype_op::SHL || op == Ntype_op::SRA) && args.size() == 2 && args[1].lo == args[1].hi && args[1].lo >= 0
             && args[1].hi <= 30) {
    const auto shift = args[1].lo;
    if (op == Ntype_op::SHL) {
      value.lo *= int64_t{1} << shift;
      value.hi *= int64_t{1} << shift;
    } else {
      value.lo >>= shift;
      value.hi >>= shift;
    }
    value.expression = absl::StrCat("(",
                                    value.expression,
                                    op == Ntype_op::SHL ? " * " : " >> ",
                                    op == Ntype_op::SHL ? (int64_t{1} << shift) : shift,
                                    ")");
  } else if ((op == Ntype_op::LT || op == Ntype_op::GT || op == Ntype_op::EQ) && args.size() == 2) {
    value = {absl::StrCat("(",
                          args[0].expression,
                          op == Ntype_op::LT   ? " < "
                          : op == Ntype_op::GT ? " > "
                                               : " == ",
                          args[1].expression,
                          ")"),
             0,
             1};
  } else if (op == Ntype_op::Get_mask && args.size() == 3 && args[1].lo == args[1].hi && args[2].lo == args[2].hi && args[1].lo >= 0
             && args[2].lo <= 30 && args[1].lo < args[2].lo) {
    const auto mask = (int64_t{1} << (args[2].lo - args[1].lo)) - 1;
    value
        = {absl::StrCat("((static_cast<uint64_t>(", args[0].expression, ") >> ", args[1].lo, ") & uint64_t{", mask, "})"), 0, mask};
  } else if (op == Ntype_op::And || op == Ntype_op::Or || op == Ntype_op::Xor) {
    const bool boolean     = std::ranges::all_of(args, [](const auto& a) { return a.lo >= 0 && a.hi <= 1; });
    const bool nonnegative = std::ranges::all_of(args, [](const auto& a) { return a.lo >= 0; });
    int64_t    upper       = 0;
    int64_t    lower       = 0;
    for (size_t i = 0; i < args.size(); ++i) {
      if (i) {
        value.expression = absl::StrCat("(",
                                        value.expression,
                                        op == Ntype_op::And  ? (boolean ? " && " : " & ")
                                        : op == Ntype_op::Or ? (boolean ? " || " : " | ")
                                                             : " ^ ",
                                        args[i].expression,
                                        ")");
      }
      upper = std::max(upper, args[i].hi);
      lower = std::min(lower, args[i].lo);
    }
    int64_t limit = 1;
    while (limit <= upper || (nonnegative ? false : -limit > lower)) {
      limit <<= 1;
    }
    value.lo = nonnegative ? 0 : -limit;
    value.hi = limit - 1;
  } else {
    return {};
  }
  return value;
}

void Cgen_sim::emit_pure_eval(File_output& out, hhds::Graph* graph) {
  if (!pure_graph(graph)) {
    return;
  }
  const auto saved_pins      = pin2var;
  const auto saved_canonical = canonical_;
  const auto saved_unsigned  = slop_u_values_;
  const auto saved_widths    = slop_u_binding_width_;
  const auto saved_masks     = preextracted_get_masks_;
  pin2var.clear();
  canonical_.clear();
  slop_u_values_.clear();
  slop_u_binding_width_.clear();
  preextracted_get_masks_.clear();
  native_values_.clear();
  const auto mark = out.mark();
  out.append("  static Out __pure_eval(", pure_parameters(*graph->get_io()), ") {\n");
  for (const auto& port : graph->get_io()->get_input_pin_decls()) {
    const auto pin  = graph->get_input_pin(port.name);
    const auto name = absl::StrCat("__p", port.port_id);
    if (port.bits > 0 && port.bits <= 30) {
      pin2var[pin.get_class_index()] = absl::StrCat("Slop<", port.bits + (port.unsign ? 1 : 0), ">::create_integer(", name, ")");
      const auto limit               = int64_t{1} << (port.unsign ? port.bits : port.bits - 1);
      native_values_[pin.get_class_index()] = {name, port.unsign ? 0 : -limit, limit - 1};
      canonical_.insert(pin.get_class_index());
    } else {
      pin2var[pin.get_class_index()] = name;
      if (port.unsign && slop_u_) {
        canonical_.insert(pin.get_class_index());
        slop_u_values_.insert(pin.get_class_index());
      }
    }
  }
  absl::flat_hash_set<hhds::Class_index> visiting;
  size_t                                 sequence = 0;
  std::function<void(hhds::Pin_class)>   ready    = [&](hhds::Pin_class pin) {
    if (pin.is_invalid() || pin.is_const() || pin2var.contains(pin.get_class_index())) {
      return;
    }
    const auto node = pin.get_master_node();
    if (!visiting.insert(node.get_class_index()).second) {
      cycle_unresolved_ = true;
      return;
    }
    if (gu::type_op_of(node) == Ntype_op::Sub) {
      std::string args;
      const auto  io   = node.get_subnode_io();
      const auto  loop = node.subnode_loop();
      for (const auto& port : io->get_input_pin_decls()) {
        hhds::Pin_class source;
        const auto      sink = find_sink_pin(node, port.name);
        if (!sink.is_invalid()) {
          for (auto driver : sink.get_driver_pins()) {
            if (driver.get_master_node() != node) {
              source = driver;
              break;
            }
          }
        }
        ready(source);
        if (!args.empty()) {
          args += ", ";
        }
        args += pure_argument(bind_operand(source, std::max<uint32_t>(1, port.bits), port.unsign), port);
      }
      const auto variable = absl::StrCat("__child", sequence++);
      const auto callee   = loop ? pure_loop_structs_.at(node.get_class_index()) : cpp_id(io->get_name());
      out.append(absl::StrCat("    auto ", variable, " = ::", callee, "::__pure_eval(", args, ");\n"));
      for (const auto& port : io->get_output_pin_decls()) {
        const auto driver = find_driver_pin(node, port.name);
        if (driver.is_invalid()) {
          continue;
        }
        pin2var[driver.get_class_index()] = absl::StrCat(variable, ".", cpp_port_path(port.name));
        if (port.unsign && slop_u_) {
          canonical_.insert(driver.get_class_index());
          slop_u_values_.insert(driver.get_class_index());
        }
      }
    } else {
      for (auto sink : node.inp_sorted_pins()) {
        ready(sink.get_driver_pin());
      }
      const int  width   = std::max(1, gu::bits_of(pin));
      const int  carrier = width + (gu::is_unsign(pin) ? 1 : 0);
      const auto name    = absl::StrCat("__value", sequence++);
      const auto native  = native_expression(node);
      const auto low     = gu::is_unsign(pin) ? 0 : -(int64_t{1} << std::min(width - 1, 61));
      const auto high    = (int64_t{1} << std::min(width - (gu::is_unsign(pin) ? 0 : 1), 61)) - 1;
      if (native && native->lo >= low && native->hi <= high) {
        out.append("    auto ", name, " = static_cast<int64_t>(", native->expression, ");\n");
        native_values_[pin.get_class_index()] = {name, native->lo, native->hi};
        pin2var[pin.get_class_index()]        = absl::StrCat("Slop<", carrier, ">::create_integer(", name, ")");
      } else {
        const auto expression = node_expr(node, carrier);
        out.append(absl::StrCat("    ", stored_type(width, gu::is_unsign(pin)), " ", name, "{", expression, "};\n"));
        pin2var[pin.get_class_index()] = name;
        if (gu::is_unsign(pin) && slop_u_) {
          slop_u_values_.insert(pin.get_class_index());
        }
        if (width <= 30) {
          native_values_[pin.get_class_index()]
              = {gu::is_unsign(pin) ? absl::StrCat("(", name, ").zext_to<", width + 1, ">().to_i64_low()")
                                    : absl::StrCat("(", name, ").to_i64_low()"),
                 low,
                 high};
        }
      }
      canonical_.insert(pin.get_class_index());
    }
    visiting.erase(node.get_class_index());
  };
  out.append("    Out __result{};\n");
  for (const auto& port : graph->get_io()->get_output_pin_decls()) {
    const auto driver = get_driver(graph->get_output_pin(port.name));
    ready(driver);
    out.append("    __result.", cpp_port_path(port.name), " = ", operand(driver, std::max<uint32_t>(1, port.bits)), ";\n");
  }
  out.append("    return __result;\n  }\n");
  auto body = out.detach_from(mark);
  compact_pure_temps(body);
  out.append(body);
  pin2var                 = saved_pins;
  canonical_              = saved_canonical;
  slop_u_values_          = saved_unsigned;
  slop_u_binding_width_   = saved_widths;
  preextracted_get_masks_ = saved_masks;
  native_values_.clear();
}
