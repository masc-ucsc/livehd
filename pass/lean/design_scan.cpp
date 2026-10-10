// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_scan.hpp"

#include <algorithm>
#include <map>
#include <optional>
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

std::string state_name(const Node& node, std::string_view prefix) {
  for (const auto& edge : node.out_edges()) {
    const auto name = livehd::graph_util::wire_name(edge.driver);
    if (!name.empty() && name.front() != '_') {
      return std::string(name);
    }
  }
  return std::string(prefix) + std::to_string(node_id(node));
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

// Map an LGraph operator onto the owned scan vocabulary, or report that it has
// no mapping. Returns nullopt rather than fataling so the caller can CENSUS the
// whole design: stopping at the first unsupported node makes the remaining work
// unknowable, because a second unsupported operator behind the first is never
// reached and never counted.
std::optional<ScanOp> try_scan_op(const Node& node) {
  switch (node_op(node)) {
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
    default: return std::nullopt;
  }
}

// Refuse a malformed ARITY of a SUPPORTED operator.
//
// Four operators are binary in the Lean model and in master's exporter, which
// refuses each one by name (pass_lean.cpp: Div, LT/GT, SHL, SRA "is not
// binary"). The shared scan boundary is where that check belongs now, because
// both the legacy emitters and the verified-compiler exporter consume this
// DesignScan -- checking in one emitter would leave the other accepting the
// node.
//
// Without this, a folded comparison does not fail: Op_ULT/SLT/UGT/SGT take two
// dependencies, so a three-operand LT emits a three-dependency certificate node
// whose extra operand the model cannot mean, and nothing reports it. LT/GT are
// two-banked (Ntype::sink_bank_count), so upstream can represent exactly that.
void check_operand_arity(const LeanCtx& ctx, const Node& node, const DesignNode& n) {
  const auto binary = [&](std::string_view what) {
    if (n.operands.size() != 2) {
      fatal(ctx,
            std::string(what) + " node n_" + std::to_string(node_id(node)) + " is not binary ("
                + std::to_string(n.operands.size()) + " operands).");
    }
  };
  switch (n.op) {
    case ScanOp::Div: binary("Div"); break;
    case ScanOp::LT :
    case ScanOp::GT : binary("LT/GT"); break;
    case ScanOp::SHL: binary("SHL"); break;
    case ScanOp::SRA: binary("SRA"); break;
    default         : break;
  }
}
// Natural control operands for owned lowering nodes (never graph mutations).
PinRef natural_operand(uint32_t value) {
  const auto number = *Dlop::create_integer(value);
  PinRef     pin;
  pin.kind  = PinKind::Constant;
  pin.value = std::to_string(value);
  pin.width = pin.intrinsic_width = pin.minimum_shift_width = minimal_unsigned_const_width(number);
  return pin;
}

void lower_concat(const LeanCtx& ctx, const Node& node, DesignScan& design, uint64_t& next_id) {
  const auto width = node_width(ctx, node);
  // Bound the sum before calling the canonical decoder, whose offsets are
  // int32_t. Widths are operands, not the possibly narrowed driver widths.
  uint64_t   total = 0;
  for (const auto& edge : inp_edges_ordered(node)) {
    if ((edge.sink.get_port_id() % 2) == 0) {
      continue;
    }
    if (!pin_is_const(edge.driver)) {
      fatal(ctx, "Concat requires constant positive lane widths");
    }
    const auto lane_width = pin_const_value(edge.driver);
    if (!lane_width.is_integer() || lane_width.has_unknowns() || !lane_width.is_just_i64() || lane_width.to_just_i64() <= 0
        || lane_width.to_just_i64() > width) {
      fatal(ctx, "Concat has an invalid or excessive lane width");
    }
    total += static_cast<uint64_t>(lane_width.to_just_i64());
    if (total > width) {
      fatal(ctx, "Concat lane widths exceed its result width");
    }
  }
  const auto lanes = livehd::graph_util::concat_lanes(node);
  if (lanes.empty() || total != width) {
    fatal(ctx, "Concat requires a complete MSB-first lane table matching its result width");
  }
  const auto violation = livehd::graph_util::concat_lane_violation(lanes);
  if (!violation.empty()) {
    fatal(ctx, violation);
  }
  const auto emit = [&](ScanOp op, uint32_t bits, std::vector<Operand> operands) {
    // CertificateBuilder reserves IDs starting at 1e9 for owned sources and
    // memory lowering. Graph nodes and these adapter nodes stay below it.
    if (next_id >= 1000000000) {
      fatal(ctx, "Concat lowering exhausted the graph-node ID range");
    }
    PinRef result;
    result.id    = static_cast<uint32_t>(next_id++);
    result.width = bits;
    design.nodes.push_back({result.id, op, bits, false, std::move(operands)});
    return result;
  };
  DesignNode result;
  result.id    = node_id(node);
  result.op    = ScanOp::Or;
  result.width = width;
  for (const auto& lane : lanes) {
    auto       value      = capture_pin(ctx, lane.value);
    const auto lane_width = static_cast<uint32_t>(lane.width);
    if (value.kind == PinKind::Constant) {
      // Retain existing validation of graph-declared constant widths. The
      // explicit lane window then takes its two's-complement low bits.
      (void)pin_width(ctx, lane.value, node);
      if (value.value.starts_with('-') || value.minimum_shift_width > lane_width) {
        value = emit(ScanOp::Or,
                     lane_width,
                     {
                         {0, 0, value}
        });
      }
    } else {
      check_width(ctx, node, value.width, "Concat lane source");
      if (value.width > lane_width) {
        fatal(ctx, "Concat lane source exceeds its declared window");
      }
      if (value.width < lane_width && !livehd::graph_util::is_unsign(lane.value)) {
        value = emit(ScanOp::Sext,
                     lane_width,
                     {
                         {0, 0,                        value},
                         {1, 1, natural_operand(value.width)}
        });
      }
    }
    // SHL and OR already zero-extend to their result width. Explicit Sext
    // above is restricted to a signed lane's window, before that extension.
    if (lane.offset != 0) {
      value = emit(ScanOp::SHL,
                   width,
                   {
                       {0, 0,                                               value},
                       {1, 1, natural_operand(static_cast<uint32_t>(lane.offset))}
      });
    }
    // Or folds over ONE bank, so every lane is bank 0 while each takes its own
    // pid: operand k is pid k. Pushing pid 0 for every lane would describe a
    // shape one-driver-per-sink-pin forbids.
    result.operands.push_back({static_cast<uint32_t>(result.operands.size()), 0, std::move(value)});
  }
  design.nodes.push_back(std::move(result));
}

// Lower a Hotmux into the existing Mux vocabulary.
//
// Hotmux is interleaved (control, value) pairs with an optional trailing
// default; upstream's `hotmux_inputs` owns that pid parsing, so this does not
// re-derive it. Controls are one-bit and MUTUALLY EXCLUSIVE by contract.
//
// Three decisions, all of which the certificate records explicitly rather than
// assuming:
//
//   * The predicate is `Op_Ror` -- reduce-OR -- of the whole control, which is
//     exactly "any bit set". Testing bit 0 would silently mis-evaluate a
//     control that upstream widened.
//   * The arms nest in FIRST-ACTIVE PRIORITY order, arm 0 outermost. Under a
//     control vector that really is one-hot-or-zero this is the hot arm; under
//     a VIOLATED one-hot it is defined (first wins) instead of undefined. This
//     lowering does not prove one-hotness, and nothing downstream may read it
//     as such -- certificate WF proves structure and the bridge proves
//     model/certificate agreement, neither proves the contract on the controls.
//   * With no control active the result is the trailing default, or zero when
//     the cell declares none.
//
// No Op_Hotmux is added to the Lean model: the lowered Mux chain is what both
// the legacy GraphCert and the verified DesignCert carry, so the fast bridge,
// chunked WF and compileDesign all operate on the same dependencies.
void lower_hotmux(const LeanCtx& ctx, const Node& node, DesignScan& design, uint64_t& next_id) {
  const auto width = node_width(ctx, node);
  const auto ins   = livehd::graph_util::hotmux_inputs(node);
  if (ins.arms.empty()) {
    fatal(ctx, "Hotmux node n_" + std::to_string(node_id(node)) + " has no (control, value) arms");
  }

  const auto emit = [&](ScanOp op, uint32_t bits, std::vector<Operand> operands) {
    // Same reservation as the Concat lowering: owned sources and memory
    // lowering take IDs from 1e9 up, so adapter nodes stay below it.
    if (next_id >= 1000000000) {
      fatal(ctx, "Hotmux lowering exhausted the graph-node ID range");
    }
    PinRef result;
    result.id    = static_cast<uint32_t>(next_id++);
    result.width = bits;
    design.nodes.push_back({result.id, op, bits, false, std::move(operands)});
    return result;
  };

  // Bring an arm to the result width. A SIGNED narrower arm needs its sign
  // carried explicitly, the same treatment a signed Concat lane gets; a wider
  // arm is truncated by the Mux operand width, as every other operator does.
  const auto normalize = [&](const Node_pin& pin) {
    auto value = capture_pin(ctx, pin);
    if (value.kind != PinKind::Constant && value.width < width && !livehd::graph_util::is_unsign(pin)) {
      check_width(ctx, node, value.width, "Hotmux arm");
      value = emit(ScanOp::Sext,
                   width,
                   {
                       {0, 0,                        value},
                       {1, 1, natural_operand(value.width)}
      });
    }
    return value;
  };

  PinRef acc = ins.fallback.is_invalid() ? natural_operand(0) : normalize(ins.fallback);

  // Build innermost-first so `design.nodes` stays dependency-ordered: the LAST
  // arm is the innermost alternative and arm 0 ends up outermost.
  for (size_t i = ins.arms.size(); i-- > 0;) {
    const auto& [control, value] = ins.arms[i];
    auto        selector         = capture_pin(ctx, control);
    // Op_Ror's result width is pinned to 1 by the model's WF rule, and
    // CertificateBuilder::dep applies the requested width to a CONSTANT source
    // (a node or port keeps its own). A constant control fed straight into the
    // reduce-OR would therefore be recorded at one bit and truncated before it
    // is tested: control 2 would read as 0 and this arm could never fire.
    // Route it through an arity-1 Or at its own width first -- the same resize
    // the Concat lowering uses -- so the predicate sees every bit.
    if (selector.kind == PinKind::Constant) {
      // Widen to whichever of the DECLARED and INTRINSIC widths is larger. A
      // folded control often carries no declared bits at all (raw_pin_width is
      // 0), and taking that width would store the constant at zero bits and
      // read every control as inactive.
      const auto bits = std::max({selector.width, selector.intrinsic_width, 1u});
      selector        = emit(ScanOp::Or,
                      bits,
                      {
                                 {0, 0, selector}
      });
    }
    auto predicate = emit(ScanOp::Ror,
                          1,
                          {
                              {0, 0, std::move(selector)}
    });
    auto        arm              = normalize(value);
    // MuxBool operand order is {selector, false value, true value}.
    std::vector<Operand> operands{
        {0, 0, std::move(predicate)},
        {1, 1,            std::move(acc)},
        {2, 2,            std::move(arm)}
    };
    if (i == 0) {
      // The outermost alternative keeps the ORIGINAL graph id, so every
      // consumer that already names this Hotmux resolves without a remap.
      design.nodes.push_back({node_id(node), ScanOp::Mux, width, false, std::move(operands)});
    } else {
      acc = emit(ScanOp::Mux, width, std::move(operands));
    }
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
  const auto gio        = graph.get_io();
  uint32_t   next_input = 2000000000;
  for (const auto& decl : gio->get_input_pin_decls()) {
    const auto name  = std::string(decl.name);
    const auto pin   = graph.get_input_pin(decl.name);
    const auto width = static_cast<uint32_t>(livehd::graph_util::bits_of(pin, *gio, decl.name));
    check_width(ctx, pin_node(pin), width, "input port");
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
    design.outputs.push_back({std::string(decl.name), static_cast<uint32_t>(design.outputs.size()), width, std::nullopt});
  }
  std::vector<Node> flop_nodes, memory_nodes;
  uint64_t          next_lowered_id = 0;
  for (const auto node : graph.body().nodes()) {
    next_lowered_id = std::max(next_lowered_id, static_cast<uint64_t>(node_id(node)) + 1);
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
    m.raw_name     = state_name(node, "mem_");
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
    const auto drivers = graph.get_output_pin(output.name).get_driver_pins();
    if (!drivers.empty()) {
      output.driver = capture_pin(ctx, drivers.front());
      roots.push_back(drivers.front());
    }
  }
  std::sort(design.outputs.begin(), design.outputs.end(), [](const Port& a, const Port& b) { return a.name < b.name; });
  std::map<uint32_t, Node_pin> flop_din, flop_reset, flop_enable;
  for (const auto& node : flop_nodes) {
    Flop f;
    f.id       = node_id(node);
    f.raw_name = state_name(node, "flop_");
    f.width    = ctx.flop_width.at(f.id);
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
        } else if (!input.is_invalid()) {
          // Record where the transparent walk gave up, so an asynchronous
          // flop's refusal can name it instead of only reporting the symptom.
          f.reset_block = std::string(Ntype::get_name(node_op(pin_node(input)))) + " n_"
                          + std::to_string(node_id(pin_node(input)));
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
  // Operator census: name -> first node that used it, plus a total node count.
  std::map<std::string, std::string> unsupported;
  size_t                             unsupported_nodes = 0;
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
          if (node_op(node) == Ntype_op::Concat) {
            lower_concat(ctx, node, design, next_lowered_id);
          } else if (node_op(node) == Ntype_op::Hotmux) {
            lower_hotmux(ctx, node, design, next_lowered_id);
          } else if (const auto mapped = try_scan_op(node); !mapped) {
            // CENSUS, do not stop. Record this operator and keep walking, so the
            // refusal below names every unsupported operator in the design
            // rather than only the one the walk happened to reach first.
            auto& seen = unsupported[std::string(Ntype::get_name(node_op(node)))];
            if (seen.empty()) {
              seen = "n_" + std::to_string(id);
            }
            ++unsupported_nodes;
          } else {
            DesignNode n;
            n.id = id;
            n.op = *mapped;
            if (n.op != ScanOp::Memory) {
              n.width     = node_width(ctx, node);
              n.is_signed = node_output_is_signed(node);
              if (n.op == ScanOp::GetMask || n.op == ScanOp::SetMask) {
                // Master represents a contiguous window by constant [lo, hi)
                // endpoints. The owned certificate still takes a mask VALUE.
                // Materialize that value in owned data, without adding constants
                // or pins to the graph being scanned.
                const auto range = livehd::graph_util::bit_range(node);
                if (!range) {
                  fatal(ctx, "Get_mask/Set_mask requires constant endpoints with 0 <= lo < hi");
                }
                check_width(ctx, node, static_cast<uint32_t>(range->second), "mask endpoint");
                const auto a = livehd::graph_util::get_driver_of_sink_name(node, "a");
                if (a.is_invalid()) {
                  fatal(ctx, "Get_mask/Set_mask has no source operand");
                }
                const auto value = livehd::graph_util::mask_window_const(range->first, range->second);
                PinRef     mask;
                mask.kind                = PinKind::Constant;
                mask.intrinsic_width     = intrinsic_const_width(value);
                // This is an owned synthesized value, with no graph-declared
                // width. Preserve the existing signed width convention for
                // multiword constants instead of declaring an undersized pin.
                mask.width               = std::max(static_cast<uint32_t>(range->second), mask.intrinsic_width);
                mask.minimum_shift_width = minimal_unsigned_const_width(value);
                mask.value               = value.is_just_i64() ? std::to_string(value.to_just_i64()) : value.to_decimal_string();
                // Get_mask/Set_mask are unbanked, so bank == port on each slot.
                n.operands               = {
                    {0, 0, capture_pin(ctx, a)},
                    {1, 1, std::move(mask)}
                };
                if (n.op == ScanOp::SetMask) {
                  const auto replacement = livehd::graph_util::get_driver_of_sink_name(node, "value");
                  if (replacement.is_invalid()) {
                    fatal(ctx, "Set_mask has no replacement operand");
                  }
                  n.operands.push_back({2, 2, capture_pin(ctx, replacement)});
                }
              } else {
                for (const auto& e : inp_edges_ordered(node)) {
                  const auto pid = static_cast<uint32_t>(e.sink.get_port_id());
                  n.operands.push_back({pid,
                                        static_cast<uint32_t>(Ntype::sink_bank(node_op(node), e.sink.get_port_id())),
                                        capture_pin(ctx, e.driver)});
                }
              }
              check_operand_arity(ctx, node, n);
            }
            design.nodes.push_back(std::move(n));
          }
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
  // One refusal naming the COMPLETE census. The old behaviour fataled on the
  // first unsupported node, so a design needing two operators reported one and
  // the remaining work could not be sized from the diagnostic.
  if (!unsupported.empty()) {
    std::string detail;
    for (const auto& [name, example] : unsupported) {
      detail += (detail.empty() ? "" : ", ") + name + " (first at " + example + ")";
    }
    fatal(ctx,
          "unsupported certificate op(s): " + detail + ". " + std::to_string(unsupported_nodes)
              + " node(s) in this design have no certificate encoding.");
  }
  return design;
}
}  // namespace lean_export
