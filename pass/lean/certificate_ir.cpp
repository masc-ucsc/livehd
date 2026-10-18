// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <set>
#include <stdexcept>

#include "certificate_builder.hpp"
#include "memory_lowering.hpp"

namespace lean_export {
namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("[ERROR] pass.lean: " + message); }
}  // namespace
detail::CertificateBuilder::CertificateBuilder(const DesignScan& scan) : design(scan) {
  for (uint32_t i = 0; i < scan.inputs.size(); ++i) {
    input_ordinals.emplace(scan.inputs[i].id, i);
  }
  for (const auto& f : scan.flops) {
    flops.emplace(f.id, &f);
  }
  uint32_t ordinal = 0;
  for (const auto& [id, f] : flops) {
    flop_ordinals.emplace(id, ordinal++);
  }
}
uint32_t detail::CertificateBuilder::pin_width(const PinRef& pin) const {
  if (pin.kind == PinKind::Constant) {
    const auto w = pin.width == 0 ? pin.intrinsic_width : pin.width;
    if (w > design.policy.max_width) {
      fail("constant pin width " + std::to_string(w) + " exceeds max_width");
    }
    if (pin.width != 0 && design.policy.strict && !pin.value.starts_with('-') && pin.intrinsic_width > w) {
      fail("constant pin declares width " + std::to_string(w) + " but its value needs " + std::to_string(pin.intrinsic_width)
           + " bits");
    }
    return w;
  }
  if (pin.width == 0 || pin.width > design.policy.max_width) {
    if (design.policy.strict) {
      fail("node n_" + std::to_string(pin.id) + " has zero or excessive pin width");
    }
    return 1;
  }
  return pin.width;
}
uint32_t detail::CertificateBuilder::dep(const PinRef& pin, uint32_t width) {
  if (pin.kind == PinKind::Node) {
    return pin.id;
  }
  if (pin.kind == PinKind::Memory) {
    const auto key = (static_cast<uint64_t>(pin.id) << 32) | pin.port;
    if (!memory_reads.contains(key)) {
      fail("memory read pin has no certificate id (memory n_" + std::to_string(pin.id) + " read port " + std::to_string(pin.port)
           + ") referenced while lowering " + (lowering_context.empty() ? std::string("<unknown>") : lowering_context)
           + ": the memory was not lowered before a consumer referenced it");
    }
    return memory_reads.at(key);
  }
  Source source;
  source.id    = pin.id;
  source.width = pin.width;
  if (pin.kind == PinKind::Constant) {
    source.id        = next_id++;
    source.kind      = SourceKind::Const;
    source.width     = width;
    source.const_int = pin.value;
  } else if (pin.kind == PinKind::Input) {
    source.kind    = SourceKind::Input;
    source.ordinal = input_ordinals.at(pin.id);
  } else {
    source.kind             = SourceKind::Flop;
    source.ordinal          = flop_ordinals.at(pin.id);
    const auto& f           = *flops.at(pin.id);
    source.async_reset      = f.asynchronous;
    source.reset_value      = f.initial;
    source.reset_active_low = f.active_low;
    if (f.asynchronous) {
      if (!f.reset_input) {
        if (input_reset_flops.contains(f.id)) {
          // 2B: the reset is a function of primary inputs; the source is the
          // STORED value and readers get the override node.
          source.async_reset = false;
          sources[source.id] = source;
          return async_read(f);
        }
        // `SourceDesc.flopQAsync` reads the reset out of RuntimeInput, and an
        // override node is exact only for a reset fixed within a step; a reset
        // computed from STATE is neither. Name what stopped the transparent
        // walk: an unhandled resize wrapper is a tracing gap to close, while
        // real logic is a modelling limit.
        fail("flop n_" + std::to_string(f.id)
             + " has an ASYNCHRONOUS reset that is not a combinational function of primary inputs"
             + (f.reset_block.empty() ? "" : "; the reset cone stops at " + f.reset_block));
      }
      source.reset_input = input_ordinals.at(*f.reset_input);
    }
  }
  sources[source.id] = source;
  return source.id;
}
// The read side of a 2B flop: `MuxBool(reset, Q, resetValue)` (operands
// swapped for an active-LOW reset), emitted on first reference. Its selector is
// the same slot as `FlopDesc.resetPin`, with the same polarity, so read and
// commit agree by construction; the reset cone was lowered ahead of every other
// node (build_certificate), so it already has a slot here.
uint32_t detail::CertificateBuilder::async_read(const Flop& f) {
  if (auto it = async_override.find(f.id); it != async_override.end()) {
    return it->second;
  }
  if (pin_width(*f.reset) != 1) {
    fail("flop n_" + std::to_string(f.id) + " has a " + std::to_string(pin_width(*f.reset))
         + "-bit input-derived ASYNCHRONOUS reset; only a 1-bit reset is lowered");
  }
  const auto rst = dep(*f.reset, 1);
  PinRef     rv;
  rv.kind        = PinKind::Constant;
  rv.width       = f.width;
  rv.value       = f.initial;
  const auto rvs = dep(rv, f.width);
  Op         op;
  op.kind       = Operation::MuxBool;
  const auto id = emit(op, f.width, f.active_low ? std::vector<uint32_t>{rst, rvs, f.id} : std::vector<uint32_t>{rst, f.id, rvs});
  async_override.emplace(f.id, id);
  return id;
}
uint32_t detail::CertificateBuilder::emit(Op op, uint32_t width, std::vector<uint32_t> deps) {
  const auto id = next_id++;
  result.nodes.push_back({id, std::move(op), width, std::move(deps)});
  return id;
}

CertificateIR build_certificate(const DesignScan& design, const CertificateOptions&) {
  detail::CertificateBuilder b(design);
  std::vector<uint32_t>      lowered_memories;
  // 2B: an asynchronous flop whose reset cone reaches only PRIMARY INPUTS and
  // constants (CORE-ET B05's `dft_i.scanmode ? dft_i.scan_reset_n : rst_wr_ni`)
  // is read through an override node (see async_read). That node needs the
  // reset's slot before the flop's first reader, but nothing orders a reader
  // after an unrelated reset cone -- so the cones are lowered FIRST. An
  // input-only cone depends on no other node, so moving it ahead of everything
  // keeps the order topological.
  std::vector<const DesignNode*> order;
  {
    std::map<uint32_t, size_t> index_of;
    for (size_t i = 0; i < design.nodes.size(); ++i) {
      if (design.nodes[i].op != ScanOp::Memory) {
        index_of.emplace(design.nodes[i].id, i);
      }
    }
    std::set<uint32_t> cone_ids;
    for (const auto& [fid, fp] : b.flops) {
      const Flop& f = *fp;
      if (!f.asynchronous || f.reset_input || !f.reset) {
        continue;
      }
      std::set<uint32_t>  cone;
      bool                input_only = true;
      std::vector<PinRef> work{*f.reset};
      while (!work.empty() && input_only) {
        const PinRef p = work.back();
        work.pop_back();
        if (p.kind == PinKind::Input || p.kind == PinKind::Constant) {
          continue;
        }
        if (p.kind != PinKind::Node) {
          input_only = false;  // a flop or a memory read: state, not an input
          break;
        }
        if (!cone.insert(p.id).second) {
          continue;
        }
        const auto it = index_of.find(p.id);
        if (it == index_of.end()) {
          input_only = false;
          break;
        }
        for (const auto& op : design.nodes[it->second].operands) {
          work.push_back(op.driver);
        }
      }
      if (input_only) {
        b.input_reset_flops.insert(f.id);
        cone_ids.insert(cone.begin(), cone.end());
      }
    }
    for (const auto& n : design.nodes) {
      if (n.op != ScanOp::Memory && cone_ids.contains(n.id)) {
        order.push_back(&n);
      }
    }
    for (const auto& n : design.nodes) {
      if (n.op == ScanOp::Memory || !cone_ids.contains(n.id)) {
        order.push_back(&n);
      }
    }
  }
  for (const auto* np : order) {
    const auto& n = *np;
    if (n.op == ScanOp::Memory) {
      b.lowering_context = "memory n_" + std::to_string(n.id) + " read port " + std::to_string(n.memory_read);
      detail::lower_memory_read(b, design.memories.at(n.id), n.memory_read);
      if (std::find(lowered_memories.begin(), lowered_memories.end(), n.id) == lowered_memories.end()) {
        lowered_memories.push_back(n.id);
      }
      continue;
    }
    b.lowering_context = "node n_" + std::to_string(n.id);
    CertNode c;
    c.id     = n.id;
    c.width  = n.width;
    auto add = [&](const Operand& p, uint32_t w) { c.deps.push_back(b.dep(p.driver, w)); };
    auto all = [&](uint32_t w) {
      for (const auto& p : n.operands) {
        add(p, w);
      }
    };
    switch (n.op) {
      case ScanOp::Constant: fail("constant unexpectedly appeared in topological nodes");
      case ScanOp::Sum:
        // BANKED: the "as" bank is added and "bs" is subtracted, one pid per
        // operand with the role on pid parity. Select on the BANK -- `port == 0`
        // would keep only the first addend and drop every later one, because
        // `a + b` occupies pids {0, 2}. `Op_Sum k` names how many deps are
        // added, so the adds must also come first in the dependency list.
        c.op.kind = Operation::Sum;
        for (const auto& p : n.operands) {
          if (p.bank == 0) {
            add(p, n.width);
            ++c.op.parameter;
          }
        }
        for (const auto& p : n.operands) {
          if (p.bank == 1) {
            add(p, n.width);
          }
        }
        break;
      case ScanOp::Mult:
        c.op.kind = Operation::Mult;
        all(n.width);
        break;
      case ScanOp::Div:
        c.op.kind = Operation::UDiv;
        all(n.width);
        break;
      case ScanOp::And:
        c.op.kind = Operation::And;
        all(n.width);
        break;
      case ScanOp::Or:
        c.op.kind = Operation::Or;
        all(n.width);
        break;
      case ScanOp::Xor:
        c.op.kind = Operation::Xor;
        all(n.width);
        break;
      case ScanOp::Ror:
        c.op.kind = Operation::Ror;
        all(n.width);
        break;
      case ScanOp::Not:
        c.op.kind = Operation::Not;
        all(n.width);
        break;
      case ScanOp::EQ: {
        c.op.kind      = Operation::EQ;
        uint32_t width = 1;
        for (const auto& p : n.operands) {
          width = std::max(width, b.pin_width(p.driver));
        }
        all(width);
        break;
      }
      case ScanOp::LT:
      case ScanOp::GT: {
        c.op.kind = n.op == ScanOp::LT ? (n.is_signed ? Operation::SLT : Operation::ULT)
                                       : (n.is_signed ? Operation::SGT : Operation::UGT);
        // Two-banked ("as" is the left operand, "bs" the right), but the
        // comparison operations are BINARY, and the scan refuses any other
        // arity, so ascending pid order is exactly {left, right} here.
        all(std::max(b.pin_width(n.operands[0].driver), b.pin_width(n.operands[1].driver)));
        break;
      }
      case ScanOp::SHL:
      case ScanOp::SRA:
      case ScanOp::Sext:
        c.op.kind = n.op == ScanOp::SHL ? Operation::SHL : n.op == ScanOp::SRA ? Operation::SRA : Operation::Sext;
        for (size_t i = 0; i < n.operands.size(); ++i) {
          const auto& p     = n.operands[i];
          uint32_t    width = n.width;
          if (i == 1) {
            width = b.pin_width(p.driver);
            if (p.driver.kind == PinKind::Constant) {
              width = std::max(width, p.driver.minimum_shift_width);
            }
          } else if (n.op != ScanOp::SHL) {
            width = b.pin_width(p.driver);
            if (p.driver.kind == PinKind::Constant) {
              width = std::max(width, n.width);
            }
          }
          add(p, width);
        }
        break;
      case ScanOp::Mux: {
        // UNBANKED, so `sink_bank` is the identity and bank == pid: pid 0 is the
        // selector, pid k>0 is option k. Read through `bank` anyway so every arm
        // in this switch selects a role the same way.
        uint32_t sel_width = 1;
        size_t   count     = 0;
        for (const auto& p : n.operands) {
          if (p.bank == 0) {
            sel_width = b.pin_width(p.driver);
            add(p, sel_width);
          }
        }
        for (const auto& p : n.operands) {
          if (p.bank != 0) {
            add(p, n.width);
            ++count;
          }
        }
        c.op.kind = count == 2 && sel_width == 1 ? Operation::MuxBool : Operation::MuxN;
        break;
      }
      case ScanOp::GetMask: {
        c.op.kind            = Operation::GetMask;
        const auto src_width = n.operands.empty() ? n.width : b.pin_width(n.operands[0].driver);
        // The mask's declared width is the WINDOW CARRIER, so it is computed
        // first: the value operand below needs it.
        uint32_t mask_width = 0;
        if (n.operands.size() > 1) {
          const auto& mk  = n.operands[1].driver;
          const auto  own = b.pin_width(mk);
          const bool  neg = mk.kind == PinKind::Constant && mk.value.starts_with('-');
          mask_width      = neg ? std::max(src_width, n.width) : std::max(std::max(src_width, n.width), own);
        }
        for (size_t i = 0; i < n.operands.size(); ++i) {
          if (i == 0) {
            // A NEGATIVE CONSTANT VALUE OPERAND IS DECLARED AT THE CARRIER.
            //
            // The native lowering materializes a constant AT THE TARGET WIDTH
            // -- `Slop<target_bits>::create_integer(v)` in
            // inou/cgen/cgen_sim.cpp operand() -- so a negative value's sign
            // occupies every position the window can select.  The certificate
            // normalizes a source at its OWN narrow width, where the positions
            // above it read ZERO, not sign.  For a window reaching above that
            // width the two disagree, so "a raw value carries its own sign" is
            // not sufficient on its own: it carries it only at a width that
            // reaches the window.
            //
            // Widening is value-preserving here (`const_int` is unchanged and
            // -v sign-fills), it cannot truncate, and GetMask selects only bits
            // the mask names, so sign bits above the window are never read.
            // POSITIVE constants are untouched -- zero-fill above them is
            // already what both sides do.
            const auto& val = n.operands[i].driver;
            auto        w   = b.pin_width(val);
            if (val.kind == PinKind::Constant && val.value.starts_with('-') && w < mask_width) {
              w = mask_width;
            }
            add(n.operands[i], w);
            continue;
          }
          if (i != 1) {
            add(n.operands[i], b.pin_width(n.operands[i].driver));
            continue;
          }
          // THE MASK PIN'S OWN WIDTH JOINS THE MAXIMUM -- it is not overridden
          // by it.
          //
          // `max(src_width, n.width)` is the widening `Get_mask(a, -1)` needs.
          // Written as an override it also REPLACED the mask's own width, and
          // because `pin_width` is where the strict fit check lives, passing a
          // width straight to `add` skipped that check: a mask whose value
          // needs more bits than the join was emitted at the join anyway and
          // silently truncated.  `mask_window_const(4, 12) = 4080` declared at
          // width 8 reads back as 240 -- window [4,8) instead of [4,12).
          //
          // `pin_width` is called unconditionally, so its fit and `max_width`
          // checks run on every mask; only the JOIN is conditional.  Taking the
          // maximum can only widen, so no mask can be truncated by this line.
          // The `mask == -1` convention is DISTINCT and is left exactly as it
          // was: -1 is all-ones at every width, `pin_width` deliberately does
          // not fit-check a negative literal, and widening it here would change
          // how many bits the window selects.  It has no checked own width to
          // join, so `mask_width` above keeps the previous two-way join for it.
          add(n.operands[i], mask_width);
        }
        break;
      }
      case ScanOp::SetMask:
        c.op.kind = Operation::SetMask;
        for (const auto& p : n.operands) {
          add(p, b.pin_width(p.driver));
        }
        break;
      case ScanOp::Memory: break;
    }
    b.result.nodes.push_back(std::move(c));
  }
  // The WRITE side, after every node -- see memory_lowering.hpp. A next-state
  // cone may name any node in the design, including another memory's read, so
  // it cannot be scheduled at the Memory node's own place in the order.
  for (const auto id : lowered_memories) {
    b.lowering_context = "memory n_" + std::to_string(id) + " (write side)";
    detail::lower_memory_writes(b, design.memories.at(id));
  }
  b.lowering_context.clear();
  for (const auto& o : design.outputs) {
    if (o.driver) {
      b.result.outputs.push_back({b.dep(*o.driver, o.width), o.width});
    }
  }
  // The emitted clock table is the scan's, in the scan's ordinal order. Only
  // the human-facing name crosses: a certificate domain IS its ordinal, and the
  // scan's structural key has no meaning here.
  if (!design.clocks.empty()) {
    b.result.clocks.clear();
    for (const auto& c : design.clocks) {
      b.result.clocks.push_back({c.name});
    }
  }
  std::map<uint32_t, FlopDriver> flops;
  // Allocate sources in graph iteration order, preserve runtime ordinals by nid.
  for (const auto& f : design.flops) {
    FlopDriver driver;
    driver.origin = f.id;
    driver.width  = f.width;
    if (!f.din) {
      fail("flop n_" + std::to_string(f.id) + " has no `din` driver.");
    }
    driver.din = b.dep(*f.din, f.width);
    if (f.reset) {
      driver.reset_pin = b.dep(*f.reset, 1);
    }
    if (f.enable) {
      driver.enable = b.dep(*f.enable, 1);
    }
    driver.reset_value      = f.initial;
    // The polarity flag controls both immediate async reads and next-state
    // reset priority; it is independent of the reset signal itself.
    driver.reset_active_low = f.active_low;
    // Carried from the scan, never recomputed here: the IR has no graph.
    driver.clock            = f.clock_ordinal;
    driver.async_reset      = f.asynchronous;
    flops.emplace(f.id, driver);
  }
  for (const auto& [id, f] : flops) {
    b.result.flops.push_back(f);
  }
  for (const auto& [id, f] : b.read_registers) {
    b.sources.at(id).ordinal = static_cast<uint32_t>(b.result.flops.size());
    b.result.flops.push_back(f);
  }
  std::map<uint32_t, uint32_t> memory_ordinals;
  for (const auto& [id, m] : b.memory_drivers) {
    memory_ordinals.emplace(id, static_cast<uint32_t>(b.result.memories.size()));
    b.result.memories.push_back(m);
  }
  for (auto& [id, source] : b.sources) {
    if (source.kind == SourceKind::MemImage) {
      source.ordinal = memory_ordinals.at(source.ordinal);
    }
  }
  for (const auto& [id, source] : b.sources) {
    b.result.sources.push_back(source);
  }
  index_certificate(b.result);
  return std::move(b.result);
}

void index_certificate(CertificateIR& c) {
  c.slot_of.clear();
  uint32_t slot   = 0;
  auto     insert = [&](uint32_t id) {
    if (!c.slot_of.emplace(id, slot++).second) {
      fail("duplicate certificate id " + std::to_string(id));
    }
  };
  for (const auto& s : c.sources) {
    insert(s.id);
  }
  for (const auto& n : c.nodes) {
    for (auto id : n.deps) {
      if (!c.slot_of.contains(id)) {
        fail("node dependency is missing or not topological: " + std::to_string(id));
      }
    }
    insert(n.id);
  }
  auto check = [&](uint32_t id) {
    if (!c.slot_of.contains(id)) {
      fail("undeclared certificate id " + std::to_string(id));
    }
  };
  for (const auto& o : c.outputs) {
    check(o.id);
  }
  for (const auto& f : c.flops) {
    check(f.din);
    if (f.enable) {
      check(*f.enable);
    }
    if (f.reset_pin) {
      check(*f.reset_pin);
    }
  }
  for (const auto& m : c.memories) {
    check(m.next_img);
  }
}
}  // namespace lean_export
