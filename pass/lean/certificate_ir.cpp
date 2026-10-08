// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
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
      fail("memory read pin has no certificate id");
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
        fail("flop n_" + std::to_string(f.id) + " has an ASYNCHRONOUS reset that is not driven by a primary input");
      }
      source.reset_input = input_ordinals.at(*f.reset_input);
    }
  }
  sources[source.id] = source;
  return source.id;
}
uint32_t detail::CertificateBuilder::emit(Op op, uint32_t width, std::vector<uint32_t> deps) {
  const auto id = next_id++;
  result.nodes.push_back({id, std::move(op), width, std::move(deps)});
  return id;
}

CertificateIR build_certificate(const DesignScan& design, const CertificateOptions&) {
  detail::CertificateBuilder b(design);
  for (const auto& n : design.nodes) {
    if (n.op == ScanOp::Memory) {
      detail::lower_memory(b, design.memories.at(n.id));
      continue;
    }
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
        for (size_t i = 0; i < n.operands.size(); ++i) {
          add(n.operands[i], i == 1 ? std::max(src_width, n.width) : b.pin_width(n.operands[i].driver));
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
  for (const auto& o : design.outputs) {
    if (o.driver) {
      b.result.outputs.push_back({b.dep(*o.driver, o.width), o.width});
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
