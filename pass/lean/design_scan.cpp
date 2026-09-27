// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_scan.hpp"

#include <algorithm>
#include <set>

#include "graph_access.hpp"

namespace lean_export {
namespace {
using namespace lean_pass;

std::string decimal(const LeanCtx& ctx, const Node_pin& pin) {
  const auto value = pin_const_value(pin);
  if (value.has_unknowns()) {
    fatal(ctx,
          "Const node n_" + std::to_string(node_id(pin_node(pin)))
              + " has X/Z bits; strict Lean certificate rejects four-valued logic.");
  }
  return value.is_known_zero() ? "0" : value.is_just_i64() ? std::to_string(value.to_just_i64()) : value.to_decimal_string();
}

PinRef capture_pin(const LeanCtx& ctx, const Node_pin& pin) {
  PinRef     result;
  const auto node = pin_node(pin);
  result.id       = node_id(node);
  result.port     = pin.get_port_id();
  result.width    = raw_pin_width(pin);
  if (pin_is_const(pin)) {
    result.kind                = PinKind::Constant;
    result.value               = decimal(ctx, pin);
    result.intrinsic_width     = intrinsic_const_width(pin_const_value(pin));
    result.minimum_shift_width = minimal_unsigned_const_width(pin_const_value(pin));
  } else if (pin_is_input(pin)) {
    const auto name = input_name_for_pin(ctx, pin);
    result.kind     = PinKind::Input;
    result.id       = ctx.input_source_id.at(name);
    result.width    = ctx.input_width.at(name);
  } else if (node_is_flop(node)) {
    result.kind  = PinKind::Flop;
    result.width = ctx.flop_width.at(result.id);
  } else if (node_is_memory(node)) {
    result.kind = PinKind::Memory;
  }
  return result;
}

ScanOp scan_op(const LeanCtx& ctx, const Node& node) {
  switch (node_op(node)) {
    case Ntype_op::Nconst  : return ScanOp::Constant;
    case Ntype_op::Sum     : return ScanOp::Sum;
    case Ntype_op::Mult    : return ScanOp::Mult;
    case Ntype_op::Div     : return ScanOp::Div;
    case Ntype_op::And     : return ScanOp::And;
    case Ntype_op::Or      : return ScanOp::Or;
    case Ntype_op::Xor     : return ScanOp::Xor;
    case Ntype_op::Ror     : return ScanOp::Ror;
    case Ntype_op::EQ      : return ScanOp::EQ;
    case Ntype_op::Not     : return ScanOp::Not;
    case Ntype_op::LT      : return ScanOp::LT;
    case Ntype_op::GT      : return ScanOp::GT;
    case Ntype_op::SHL     : return ScanOp::SHL;
    case Ntype_op::SRA     : return ScanOp::SRA;
    case Ntype_op::Mux     : return ScanOp::Mux;
    case Ntype_op::Sext    : return ScanOp::Sext;
    case Ntype_op::Get_mask: return ScanOp::GetMask;
    case Ntype_op::Set_mask: return ScanOp::SetMask;
    case Ntype_op::Memory  : return ScanOp::Memory;
    default:
      fatal(ctx,
            "unsupported certificate op `" + std::string(Ntype::get_name(node_op(node))) + "` at node n_"
                + std::to_string(node_id(node)) + ".");
  }
}
}  // namespace

DesignScan scan_design(hhds::Graph& graph, const ScanOptions& options) {
  DesignScan design;
  design.name   = std::string(graph.get_name());
  design.policy = options;
  lean_pass::LeanCtx ctx;
  ctx.g                 = &graph;
  ctx.strict            = options.strict;
  ctx.max_width         = options.max_width;
  ctx.verified_compiler = true;  // shared compatibility parser's ROM capability
  const auto gio        = graph.get_io();
  uint32_t   next_input = 2000000000;
  for (const auto& decl : gio->get_input_pin_decls()) {
    const auto name  = std::string(decl.name);
    const auto pin   = graph.get_input_pin(decl.name);
    const auto width = static_cast<uint32_t>(livehd::graph_util::bits_of(pin, *gio, decl.name));
    check_width(ctx, pin_node(pin), width, "input port");
    ctx.input_field[name]     = name;
    ctx.input_width[name]     = width;
    ctx.input_source_id[name] = next_input++;
  }
  for (const auto& [name, id] : ctx.input_source_id) {
    design.inputs.push_back({name, id, ctx.input_width.at(name), std::nullopt});
  }
  std::vector<Node_pin> roots;
  for (const auto& decl : gio->get_output_pin_decls()) {
    const auto pin   = graph.get_output_pin(decl.name);
    const auto width = static_cast<uint32_t>(livehd::graph_util::bits_of(pin, *gio, decl.name));
    check_width(ctx, pin_node(pin), width, "output port");
    design.outputs.push_back({std::string(decl.name), 0, width, std::nullopt});
  }
  std::vector<Node> flop_nodes, memory_nodes;
  for (const auto node : graph.fast_class()) {
    if (node_is_flop(node)) {
      const auto width = raw_node_width(node);
      check_width(ctx, node, width, "flop");
      ctx.flop_width[node_id(node)] = width;
      flop_nodes.push_back(node);
    } else if (node_is_memory(node)) {
      memory_nodes.push_back(node);
    }
  }
  for (const auto& node : memory_nodes) {
    const auto info = parse_memory_info(ctx, node);
    Memory     m;
    m.id           = info.nid;
    m.bits         = info.bits;
    m.addr_width   = info.addr_width;
    m.size         = info.size;
    m.wensize      = info.wensize;
    m.type         = info.type;
    m.fwd          = info.fwd;
    m.posclk       = info.posclk;
    m.sync         = info.sync;
    m.is_rom       = info.is_rom;
    m.rom_contents = info.rom_contents;
    m.read_ports   = info.read_ports;
    m.write_ports  = info.write_ports;
    for (const auto& port : info.ports) {
      MemoryPort p;
      p.port_id    = port.port_id;
      p.driver_pid = port.driver_pid;
      if (!port.addr.is_invalid()) {
        p.addr = capture_pin(ctx, port.addr);
      }
      if (!port.din.is_invalid()) {
        p.din = capture_pin(ctx, port.din);
      }
      if (!port.enable.is_invalid()) {
        p.enable = capture_pin(ctx, port.enable);
      }
      if (!port.clock.is_invalid()) {
        p.clock = capture_pin(ctx, port.clock);
      }
      m.ports.push_back(p);
    }
    design.memories.emplace(m.id, std::move(m));
    ctx.memory_info.emplace(info.nid, info);
  }
  for (auto& output : design.outputs) {
    const auto edges = graph.get_output_pin(output.name).inp_edges();
    if (!edges.empty()) {
      output.driver = capture_pin(ctx, edges.front().driver);
      roots.push_back(edges.front().driver);
    }
  }
  std::sort(design.outputs.begin(), design.outputs.end(), [](const Port& a, const Port& b) { return a.name < b.name; });
  std::map<uint32_t, Node_pin> flop_din, flop_reset, flop_enable;
  for (const auto& node : flop_nodes) {
    Flop f;
    f.id    = node_id(node);
    f.width = ctx.flop_width.at(f.id);
    for (const auto& e : inp_edges_ordered(node)) {
      const auto name = sink_pin_name(e);
      if (name == "din") {
        f.din          = capture_pin(ctx, e.driver);
        flop_din[f.id] = e.driver;
      } else if (name == "reset_pin") {
        f.reset          = capture_pin(ctx, e.driver);
        flop_reset[f.id] = e.driver;
        const auto input = resolve_resize_chain(e.driver);
        if (pin_is_input(input)) {
          f.reset_input = ctx.input_source_id.at(input_name_for_pin(ctx, input));
        }
      } else if (name == "enable") {
        f.enable          = capture_pin(ctx, e.driver);
        flop_enable[f.id] = e.driver;
      } else if (name == "clock_pin") {
        f.clock = capture_pin(ctx, e.driver);
      } else if (name == "async") {
        f.asynchronous = const_pin_int_or(e.driver, 0) != 0;
      } else if (name == "negreset") {
        f.active_low = const_pin_int_or(e.driver, 0) != 0;
      } else if (name == "initial") {
        if (!pin_is_const(e.driver)) {
          fatal(ctx, "flop n_" + std::to_string(f.id) + " has a non-constant `initial` pin; a reset VALUE must be a constant.");
        }
        f.initial = decimal(ctx, e.driver);
      } else if (name == "posclk") {
        if (const_pin_int_or(e.driver, 1) == 0) {
          fatal(ctx,
                "flop n_" + std::to_string(f.id) + " is a NEGEDGE flop (`posclk` = 0). pass.single_edge must normalize it first.");
        }
      } else if (name == "pipe_min" || name == "pipe_max") {
        if (const_pin_int_or(e.driver, 1) != 1) {
          fatal(ctx, "flop n_" + std::to_string(f.id) + " has unsupported pipeline depth `" + name + "`.");
        }
      } else {
        fatal(ctx, "flop n_" + std::to_string(f.id) + " drives unsupported pin `" + name + "`.");
      }
    }
    design.flops.push_back(f);
  }
  for (const auto& [id, pin] : flop_din) {
    roots.push_back(pin);
  }
  for (const auto& [id, pin] : flop_reset) {
    roots.push_back(pin);
  }
  for (const auto& [id, pin] : flop_enable) {
    roots.push_back(pin);
  }
  for (const auto& node : memory_nodes) {
    for (const auto& p : ctx.memory_info.at(node_id(node)).ports) {
      if (!p.addr.is_invalid()) {
        roots.push_back(p.addr);
      }
      if (!p.din.is_invalid()) {
        roots.push_back(p.din);
      }
      if (!p.enable.is_invalid()) {
        roots.push_back(p.enable);
      }
    }
  }
  std::set<uint32_t>                 reached, active;
  std::vector<std::pair<Node, bool>> stack;
  const auto leaf = [&](const Node_pin& pin) { return pin_is_input(pin) || pin_is_const(pin) || node_is_flop(pin_node(pin)); };
  for (const auto& pin : roots) {
    design.roots.push_back(capture_pin(ctx, pin));
    if (leaf(pin) || reached.contains(node_id(pin_node(pin)))) {
      continue;
    }
    stack.emplace_back(pin_node(pin), false);
    while (!stack.empty()) {
      // Own the frame: growing stack invalidates references to stack.back().
      const auto [node, visited] = stack.back();
      const auto id              = node_id(node);
      if (visited) {
        if (reached.insert(id).second) {
          DesignNode n;
          n.id = id;
          n.op = scan_op(ctx, node);
          if (n.op != ScanOp::Memory) {
            n.width     = node_width(ctx, node);
            n.is_signed = node_output_is_signed(node);
            for (const auto& e : inp_edges_ordered(node)) {
              n.operands.push_back({e.sink.get_port_id(), capture_pin(ctx, e.driver)});
            }
          }
          design.nodes.push_back(std::move(n));
        }
        active.erase(id);
        stack.pop_back();
        continue;
      }
      if (reached.contains(id)) {
        stack.pop_back();
        continue;
      }
      if (!active.insert(id).second) {
        fatal(ctx, "combinational cycle at node n_" + std::to_string(id));
      }
      stack.back().second = true;
      for (const auto& e : inp_edges_ordered(node)) {
        if (leaf(e.driver) || reached.contains(node_id(pin_node(e.driver)))) {
          continue;
        }
        stack.emplace_back(pin_node(e.driver), false);
      }
    }
  }
  return design;
}
}  // namespace lean_export
