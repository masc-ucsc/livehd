// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_legacy_fast_model.hpp"

#include <algorithm>
#include <stdexcept>

#include "lean_format.hpp"
namespace lean_export {
const Source* certificate_source(const CertificateIR& c, uint32_t id) {
  const auto slot = c.slot_of.at(id);
  return slot < c.sources.size() ? &c.sources.at(slot) : nullptr;
}
uint32_t certificate_width(const CertificateIR& c, uint32_t id) {
  const auto slot = c.slot_of.at(id);
  return slot < c.sources.size() ? c.sources.at(slot).width : c.nodes.at(slot - c.sources.size()).width;
}
bool certificate_memory_value(const CertificateIR& c, uint32_t id) {
  if (const auto* s = certificate_source(c, id)) {
    return s->kind == SourceKind::MemImage || s->kind == SourceKind::RomConst;
  }
  const auto op = c.nodes.at(c.slot_of.at(id) - c.sources.size()).op.kind;
  return op == Operation::MemWrite || op == Operation::MemWriteBE;
}
namespace {
std::string cast(const std::string& x, uint32_t w) { return "((bv_zext " + x + ") : BitVec " + std::to_string(w) + ")"; }
std::string fold(const std::string& op, const std::vector<std::string>& xs, std::string empty) {
  if (xs.empty()) {
    return empty;
  }
  auto out = xs.front();
  for (size_t i = 1; i < xs.size(); ++i) {
    out = "(" + out + " " + op + " " + xs[i] + ")";
  }
  return out;
}
uint32_t address_width(const CertificateIR& c, uint32_t id) {
  while (true) {
    if (const auto* s = certificate_source(c, id)) {
      return s->addr_w;
    }
    const auto& node = c.nodes.at(c.slot_of.at(id) - c.sources.size());
    if (!certificate_memory_value(c, id)) {
      throw std::invalid_argument("expected memory-valued dependency");
    }
    id = node.deps.at(0);
  }
}
std::string reset_predicate(std::string predicate, bool active_low) { return active_low ? "(!" + predicate + ")" : predicate; }
}  // namespace
FastExpr lower_fast_expr(const CertificateIR& c, const CertNode& n) {
  const auto               w     = n.width;
  auto                     dep   = [&](size_t i) { return "@" + std::to_string(i) + "@"; };
  auto                     ext   = [&](size_t i, uint32_t width) { return cast(dep(i), width); };
  auto                     width = [&](size_t i) { return certificate_width(c, n.deps.at(i)); };
  auto                     arity = [&](size_t k) { return n.deps.size() == k; };
  std::vector<std::string> terms;
  for (size_t i = 0; i < n.deps.size(); ++i) {
    terms.push_back(ext(i, w));
  }
  FastExpr e{"BitVec " + std::to_string(w), lit_zero(w)};
  switch (n.op.kind) {
    case Operation::Const: e.text = lit_bv(w, lean_integer(n.op.value)); break;
    case Operation::Sum  : {
      const auto mid = terms.begin() + std::min<size_t>(n.op.parameter, terms.size());
      const auto a   = fold("+", {terms.begin(), mid}, lit_zero(w));
      const auto b   = fold("+", {mid, terms.end()}, lit_zero(w));
      e.text = mid == terms.end() ? a : mid == terms.begin() ? "(" + lit_zero(w) + " - " + b + ")" : "((" + a + ") - (" + b + "))";
      break;
    }
    case Operation::Mult: e.text = fold("*", terms, lit_one(w)); break;
    case Operation::UDiv:
      if (arity(2)) {
        // Divide at the operand width before truncating the quotient.
        const auto cw  = std::max({w, width(0), width(1)});
        const auto div = "(sem_udiv " + ext(0, cw) + " " + ext(1, cw) + ")";
        e.text         = cw == w ? div : cast(div, w);
      }
      break;
    case Operation::And: e.text = fold("&&&", terms, lit_zero(w)); break;
    case Operation::Or : e.text = fold("|||", terms, lit_zero(w)); break;
    case Operation::Xor: e.text = fold("^^^", terms, lit_zero(w)); break;
    case Operation::Ror: {
      std::vector<std::string> bs;
      for (size_t i = 0; i < n.deps.size(); ++i) {
        bs.push_back("(bitvec_nonzero " + dep(i) + ")");
      }
      const auto b = "(bool_to_bv1 " + fold("||", bs, "false") + ")";
      e.text       = w == 1 ? b : cast(b, w);
      break;
    }
    case Operation::Not:
      if (arity(1)) {
        e.text = "(~~~ " + ext(0, w) + ")";
      }
      break;
    case Operation::EQ: {
      uint32_t cw = 1;
      for (auto id : n.deps) {
        cw = std::max(cw, certificate_width(c, id));
      }
      std::vector<std::string> bs;
      for (size_t i = 1; i < n.deps.size(); ++i) {
        bs.push_back("(" + ext(i, cw) + " = " + ext(0, cw) + ")");
      }
      const auto b = "(bool_to_bv1 " + fold("&&", bs, "true") + ")";
      e.text       = w == 1 ? b : cast(b, w);
      break;
    }
    case Operation::SLT:
    case Operation::SGT:
    case Operation::ULT:
    case Operation::UGT:
      if (arity(2)) {
        const bool sign = n.op.kind == Operation::SLT || n.op.kind == Operation::SGT;
        const auto op   = n.op.kind == Operation::SLT || n.op.kind == Operation::ULT ? " < " : " > ";
        const auto cw   = std::max(width(0), width(1));
        const auto a    = sign ? "BitVec.toInt " + dep(0) : ext(0, cw);
        const auto b    = sign ? "BitVec.toInt " + dep(1) : ext(1, cw);
        const auto cmp  = "(bool_to_bv1 (" + a + op + b + "))";
        e.text          = w == 1 ? cmp : cast(cmp, w);
      }
      break;
    case Operation::SHL: {
      std::vector<std::string> shifts;
      for (size_t i = 1; i < n.deps.size(); ++i) {
        shifts.push_back("(" + ext(0, w) + " <<< (BitVec.toNat " + dep(i) + "))");
      }
      e.text = fold("^^^", shifts, lit_zero(w));
      break;
    }
    case Operation::SRA:
      if (arity(2)) {
        e.text = std::string("((") + (w > width(0) ? "bv_sext" : "bv_zext") + " (sem_sra " + ext(0, width(0)) + " " + dep(1)
                 + ")) : BitVec " + std::to_string(w) + ")";
      }
      break;
    case Operation::MuxBool:
      if (arity(3)) {
        e.text = "(if bitvec_nonzero " + dep(0) + " then " + ext(2, w) + " else " + ext(1, w) + ")";
      }
      break;
    case Operation::MuxN:
      if (!n.deps.empty()) {
        e.text = "(";
        for (size_t i = 1; i < n.deps.size(); ++i) {
          e.text += "if BitVec.toNat " + dep(0) + " = " + std::to_string(i - 1) + " then " + ext(i, w) + " else ";
        }
        e.text += lit_zero(w) + ")";
      }
      break;
    case Operation::Sext:
      if (arity(2)) {
        const auto* amount  = certificate_source(c, n.deps[1]);
        const auto  natural = std::to_string(std::min(w, width(0)));
        if (amount && amount->kind == SourceKind::Const
            && (amount->const_int == natural || amount->const_int == std::to_string(width(0)))) {
          e.text = "((bv_sext " + dep(0) + ") : BitVec " + std::to_string(w) + ")";
        } else {
          e.text = "((bv_sext (BitVec.ofNat (BitVec.toNat " + dep(1) + ") (BitVec.toNat " + dep(0) + "))) : BitVec "
                   + std::to_string(w) + ")";
        }
      }
      break;
    case Operation::GetMask:
      if (arity(2)) {
        e.text = "((sem_get_mask " + dep(0) + " " + dep(1) + ") : BitVec " + std::to_string(w) + ")";
      }
      break;
    case Operation::SetMask:
      if (arity(3)) {
        e.text = "(sem_set_mask " + ext(0, w) + " " + dep(1) + " " + dep(2) + ")";
      }
      break;
    case Operation::MemRead:
      e.text = "(if bitvec_nonzero " + dep(2) + " then mem_read " + dep(0) + " " + dep(1) + " else " + lit_zero(w) + ")";
      break;
    case Operation::MemWrite:
    case Operation::MemWriteBE:
      e.type = "(BitVec " + std::to_string(address_width(c, n.deps.at(0))) + " -> BitVec " + std::to_string(w) + ")";
      if (n.op.kind == Operation::MemWrite) {
        e.text
            = "(if bitvec_nonzero " + dep(3) + " then mem_write " + dep(0) + " " + dep(1) + " " + dep(2) + " else " + dep(0) + ")";
      } else {
        e.text = "(if bitvec_nonzero " + dep(3) + " then mem_write_be " + dep(0) + " " + dep(1) + " " + dep(2) + " " + dep(3)
                 + " " + std::to_string(n.op.parameter) + " else " + dep(0) + ")";
      }
      break;
  }
  return e;
}
std::string render_fast_expr(const FastExpr& e, const CertNode& n, const std::function<std::string(uint32_t)>& ref) {
  std::string out;
  for (size_t i = 0; i < e.text.size();) {
    if (e.text[i] != '@') {
      out += e.text[i++];
      continue;
    }
    const auto end  = e.text.find('@', i + 1);
    const auto dep  = std::stoul(e.text.substr(i + 1, end - i - 1));
    out            += ref(n.deps.at(dep));
    i               = end + 1;
  }
  return out;
}
std::string legacy_source_leaf(const CertificateIR& c, const LegacyNames& n, const Source& s) {
  switch (s.kind) {
    case SourceKind::Input: return "i." + n.inputs.at(s.ordinal);
    case SourceKind::Const: return "BitVec.ofInt " + std::to_string(s.width) + " (" + lean_integer(s.const_int) + ")";
    case SourceKind::Flop : {
      const auto state = "s." + n.flop_field(c.flops.at(s.ordinal));
      if (!s.async_reset) {
        return state;
      }
      auto reset = reset_predicate("(bitvec_nonzero i." + n.inputs.at(s.reset_input) + ")", s.reset_active_low);
      return "if " + reset + " then " + lit_bv(s.width, lean_integer(s.reset_value)) + " else " + state;
    }
    case SourceKind::MemImage: return "s." + n.memories.at(c.memories.at(s.ordinal).origin);
    case SourceKind::RomConst: {
      std::string values = "#[";
      for (size_t i = 0; i < s.rom_contents.size(); ++i) {
        if (i) {
          values += ", ";
        }
        values += lean_integer(s.rom_contents[i]);
      }
      return "fun a : BitVec " + std::to_string(s.addr_w) + " => BitVec.ofInt " + std::to_string(s.width) + " ((" + values
             + "] : Array Int)[a.toNat]?.getD 0)";
    }
  }
  throw std::invalid_argument("unknown certificate source");
}
std::string legacy_value_wrap(const LegacyNames& n, std::string value, bool memory) {
  return n.memory_values ? std::string(memory ? "CertVal.mem (" : "CertVal.bv (") + value + ")" : value;
}
std::string legacy_source_value(const CertificateIR& c, const LegacyNames& n, const Source& s) {
  const bool memory = s.kind == SourceKind::MemImage || s.kind == SourceKind::RomConst;
  const auto leaf   = legacy_source_leaf(c, n, s);
  const auto inner  = memory ? "memenc (" + leaf + ")"
                      : s.kind == SourceKind::Const
                          ? "mk_bv " + std::to_string(s.width) + " (" + lean_integer(s.const_int) + ")"
                          : "mk_bv " + std::to_string(s.width) + " (Int.ofNat (BitVec.toNat (" + leaf + ")))";
  return legacy_value_wrap(n, inner, memory);
}
std::string legacy_fast_ref(const CertificateIR& c, const LegacyNames& n, uint32_t id, bool bridge) {
  if (const auto* s = certificate_source(c, id)) {
    return "(" + legacy_source_leaf(c, n, *s) + ")";
  }
  return bridge ? "(" + n.base + "_fv" + std::to_string(id) + " " + n.arguments() + ")" : "n_" + std::to_string(id);
}
std::vector<std::optional<uint32_t>> legacy_output_ids(const DesignScan& d, const CertificateIR& c) {
  std::vector<std::optional<uint32_t>> ids;
  size_t                               next = 0;
  for (const auto& p : d.outputs) {
    ids.push_back(p.driver ? std::optional<uint32_t>(c.outputs.at(next++).id) : std::nullopt);
  }
  if (next != c.outputs.size()) {
    throw std::invalid_argument("scan/certificate output shape mismatch");
  }
  return ids;
}
void emit_legacy_fast_model(const DesignScan& d, const CertificateIR& c, const LegacyNames& n, bool bridge, std::ostream& os) {
  auto ref  = [&](uint32_t id) { return legacy_fast_ref(c, n, id, bridge); };
  auto lets = [&]() {
    for (const auto& node : c.nodes) {
      const auto expr = lower_fast_expr(c, node);
      os << "  let n_" << node.id << " : " << expr.type << " := " << render_fast_expr(expr, node, ref) << '\n';
    }
  };
  if (bridge) {
    for (const auto& node : c.nodes) {
      const auto expr = lower_fast_expr(c, node);
      os << "def " << n.base << "_fv" << node.id << " " << n.parameters() << " : " << expr.type << " :=\n  "
         << render_fast_expr(expr, node, ref) << '\n';
    }
    os << '\n';
  }
  os << "def " << n.base << "_comb " << n.parameters() << " : " << n.base << "_out :=\n";
  if (!bridge) {
    lets();
  }
  const auto outputs = legacy_output_ids(d, c);
  os << "  { ";
  if (outputs.empty()) {
    os << "out_dummy := " << lit_zero(1);
  }
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (i) {
      os << ", ";
    }
    os << n.outputs[i] << " := " << (outputs[i] ? cast(ref(*outputs[i]), d.outputs[i].width) : lit_zero(d.outputs[i].width));
  }
  os << " }\n\n";
  if (!n.sequential) {
    return;
  }
  os << "def " << n.base << "_next " << n.parameters() << " : " << n.base << "_state :=\n";
  if (!bridge) {
    lets();
  }
  std::vector<std::string> fields;
  for (const auto& f : c.flops) {
    const auto field = n.flop_field(f);
    fields.push_back(field);
    const auto reset  = f.reset_pin ? reset_predicate("(bitvec_nonzero " + ref(*f.reset_pin) + ")", f.reset_active_low) : "false";
    const auto enable = f.enable ? "(bitvec_nonzero " + ref(*f.enable) + ")" : "true";
    os << "  let new_" << field << " : BitVec " << f.width << " := flop_next " << reset << " "
       << lit_bv(f.width, lean_integer(f.reset_value)) << " " << enable << " " << cast(ref(f.din), f.width) << " s." << field
       << '\n';
  }
  for (const auto& m : c.memories) {
    const auto field = n.memories.at(m.origin);
    fields.push_back(field);
    os << "  let new_" << field << " : (BitVec " << m.addr_w << " -> BitVec " << m.data_w << ") := " << ref(m.next_img) << '\n';
  }
  os << "  { ";
  for (size_t i = 0; i < fields.size(); ++i) {
    if (i) {
      os << ", ";
    }
    os << fields[i] << " := new_" << fields[i];
  }
  os << " }\n\n";
  os << "def " << n.base << "_step " << n.parameters() << " : " << n.base << "_state × " << n.base << "_out :=\n  (" << n.base
     << "_next i s, " << n.base << "_comb i s)\n\n";
}
}  // namespace lean_export
