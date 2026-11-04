// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_legacy_graph_cert.hpp"

#include <set>

#include "emit_legacy_fast_model.hpp"
#include "lean_format.hpp"
namespace lean_export {
std::string legacy_node_cert(const CertNode& n) {
  return "{ nid := " + std::to_string(n.id) + ", op := " + format_op(n.op) + ", width := " + std::to_string(n.width)
         + ", deps := " + nat_list(n.deps) + " }";
}
// Above this many sources or nodes the certificate part takes the form that scales (finding AK). Its
// `if n = <id> then .. else` source chain (37,613 branches on VX_core) did not compile in 57 min at
// 20 GB (the compiler's FloatLetIn on the case tree). Instead: the lookup trees the bridge model already
// uses (`_srcTree`, `_nodesTree`: BT.find, O(log N) per lookup), written one tree node or a few list items
// per line (one 6 MB line made Lean's parser crawl), with closed-term extraction off for those large
// literals (it was 989 of 1,428 s). VX_core: 435 s, 15.6 GB, the same theorems. Smaller designs keep the
// chain, so their files stay byte-identical.
constexpr size_t kLegacyTreeAbove = 2000;

// `[a, b, ...]` with a line break every 16 items.
static std::string nat_list_lines(const std::vector<uint32_t>& xs) {
  std::string out = "[";
  for (size_t i = 0; i < xs.size(); ++i) {
    out += (i == 0 ? "" : (i % 16 == 0 ? ",\n    " : ", ")) + std::to_string(xs[i]);
  }
  return out + "]";
}

// A bst_literal with each subtree on its own line.
static std::string tree_lines(std::string literal) {
  std::string out;
  out.reserve(literal.size() + literal.size() / 16);
  for (size_t p = 0; p < literal.size(); ++p) {
    if (literal.compare(p, 8, " (BT.nd ") == 0) {
      out += "\n    (BT.nd ";
      p += 7;
      continue;
    }
    out += literal[p];
  }
  return out;
}

void emit_legacy_graph_cert(const DesignScan& d, const CertificateIR& c, const LegacyNames& n, bool bridge, std::ostream& os) {
  const auto&           b     = n.base;
  const auto            val   = n.memory_values ? "CertVal" : "BV";
  const auto            args  = n.arguments();
  const bool            large = c.sources.size() > kLegacyTreeAbove || c.nodes.size() > kLegacyTreeAbove;
  const bool            tree  = bridge || large;
  const std::string     no_extract = large ? "set_option compiler.extract_closed false in\n" : "";
  auto                  lines      = [&](std::string literal) { return large ? tree_lines(std::move(literal)) : literal; };
  auto                  nats       = [&](const std::vector<uint32_t>& xs) { return large ? nat_list_lines(xs) : nat_list(xs); };
  std::vector<uint32_t> topo, sources;
  os << no_extract << "def " << b << "_nodeCerts : List NodeCert := [\n";
  for (size_t i = 0; i < c.nodes.size(); ++i) {
    topo.push_back(c.nodes[i].id);
    os << "  " << legacy_node_cert(c.nodes[i]) << (i + 1 == c.nodes.size() ? "" : ",") << '\n';
  }
  os << "]\n\n";
  for (const auto& s : c.sources) {
    sources.push_back(s.id);
  }
  const auto zero = n.memory_values ? "CertVal.bv (mk_bv 0 0)" : "mk_bv 0 0";
  if (tree) {
    std::vector<std::pair<uint32_t, std::string>> entries;
    for (const auto& s : c.sources) {
      entries.emplace_back(s.id, "fun " + args + " => " + legacy_source_value(c, n, s));
    }
    os << no_extract << "def " << b << "_srcTree : BT (" << b << "_in → " << (n.sequential ? b + "_state → " : "") << val
       << ") :=\n  " << lines(bst_literal(entries)) << "\n\n";
    os << "def " << b << "_sourceEnv " << n.parameters() << " : Nat -> " << val << " := fun n =>\n  (BT.find " << b
       << "_srcTree n).elim (" << zero << ") (fun f => f " << args << ")\n\n";
    entries.clear();
    for (const auto& node : c.nodes) {
      entries.emplace_back(node.id, "(" + legacy_node_cert(node) + " : NodeCert)");
    }
    os << no_extract << "def " << b << "_nodesTree : BT NodeCert :=\n  " << lines(bst_literal(entries)) << "\n\n";
    os << "def " << b << "_nodesFn : Nat → Option NodeCert := fun n => BT.find " << b << "_nodesTree n\n\n";
  } else {
    os << "def " << b << "_sourceEnv " << n.parameters() << " : Nat -> " << val << " := fun n =>\n";
    for (const auto& s : c.sources) {
      os << "  if n = " << s.id << " then " << legacy_source_value(c, n, s) << " else\n";
    }
    os << "  " << zero << "\n\n";
  }
  os << no_extract << "def " << b << "_graphCert : GraphCert :=\n  { topo := " << nats(topo) << ", sources := " << nats(sources)
     << ", nodes := " << (tree ? b + "_nodesFn" : "nodes_of_list " + b + "_nodeCerts") << " }\n\n";
  auto       bv      = [&](uint32_t id) { return "(rho " + std::to_string(id) + ")" + (n.memory_values ? ".asBV" : ""); };
  auto       word    = [&](uint32_t id, uint32_t width) { return "(bv_to_bitvec " + std::to_string(width) + " " + bv(id) + ")"; };
  const auto outputs = legacy_output_ids(d, c);
  os << "def " << b << "_outputsFromCert (rho : Nat -> " << val << ") : " << b << "_out :=\n  { ";
  if (outputs.empty()) {
    os << "out_dummy := " << lit_zero(1);
  }
  for (size_t i = 0; i < outputs.size(); ++i) {
    if (i) {
      os << ", ";
    }
    os << n.outputs[i] << " := " << (outputs[i] ? word(*outputs[i], d.outputs[i].width) : lit_zero(d.outputs[i].width));
  }
  os << " }\n\n";
  if (n.sequential) {
    os << "def " << b << "_nextStateFromCert " << n.edge_parameter() << "(rho : Nat -> " << val << ") (s : " << b
       << "_state) : " << b << "_state :=\n";
    std::vector<std::string> fields;
    for (const auto& f : c.flops) {
      const auto field = n.flop_field(f);
      fields.push_back(field);
      auto reset = f.reset_pin ? "(bv_nonzero " + bv(*f.reset_pin) + ")" : "false";
      if (f.reset_pin && f.reset_active_low) {
        reset = "(!" + reset + ")";
      }
      const auto enable = f.enable ? "(bv_nonzero " + bv(*f.enable) + ")" : "true";
      const auto value  = lit_bv(f.width, lean_integer(f.reset_value));
      const auto din    = word(f.din, f.width);
      auto       next   = "flop_next " + reset + " " + value + " " + enable + " " + din + " s." + field;
      if (!n.edges.empty()) {
        // The same guard as the fast model's `_next`.
        const auto quiet = f.async_reset ? "flop_next " + reset + " " + value + " false " + din + " s." + field : "s." + field;
        next             = "if e." + n.edges.at(f.clock) + " then " + next + " else " + quiet;
      }
      os << "  let new_" << field << " : BitVec " << f.width << " := " << next << '\n';
    }
    for (const auto& m : c.memories) {
      const auto field = n.memories.at(m.origin);
      fields.push_back(field);
      // The same rule as the fast model's `_next` (legacy_memory_next).
      const auto img  = [&](uint32_t id) {
        return "memdec " + std::to_string(m.addr_w) + " " + std::to_string(m.data_w) + " (rho " + std::to_string(id) + ").asMem";
      };
      const auto next = legacy_memory_next(m, n, img, "s." + field);
      os << "  let new_" << field << " : (BitVec " << m.addr_w << " -> BitVec " << m.data_w << ") := " << next << "\n";
    }
    // A memory nothing reads has no certificate driver (the scan never reaches
    // its write cone) but still has a state field: it holds, unobservably.
    // Without this the record literal below missed the field (mem_write_only).
    {
      std::set<uint32_t> driven;
      for (const auto& m : c.memories) {
        driven.insert(m.origin);
      }
      for (const auto& [id, field] : n.memories) {
        if (!driven.contains(id)) {
          fields.push_back(field);
          os << "  let new_" << field << " := s." << field << '\n';
        }
      }
    }
    os << "  { ";
    for (size_t i = 0; i < fields.size(); ++i) {
      if (i) {
        os << ", ";
      }
      os << fields[i] << " := new_" << fields[i];
    }
    os << " }\n\n";
  }
  const auto eval_fn = n.memory_values ? "evalGraphC" : "evalGraph";
  const auto eval    = std::string(eval_fn) + " " + b + "_graphCert.topo " + b + "_graphCert (" + b + "_sourceEnv " + args + ")";
  os << "def " << b << "_comb_cert " << n.parameters() << " : " << b << "_out :=\n  " << b << "_outputsFromCert (" << eval
     << ")\n\n";
  os << "theorem " << b << "_comb_cert_refines_cert " << n.parameters() << " : " << b << "_comb_cert " << args << " = " << b
     << "_outputsFromCert (" << eval << ") := rfl\n\n";
  if (n.sequential) {
    const auto ep = n.edge_parameter(), ea = n.edge_argument();
    os << "def " << b << "_next_cert " << ep << n.parameters() << " : " << b << "_state :=\n  " << b << "_nextStateFromCert " << ea
       << "(" << eval << ") s\n\n";
    os << "def " << b << "_step_cert " << ep << n.parameters() << " : " << b << "_state × " << b << "_out :=\n  (" << b
       << "_next_cert " << ea << "i s, " << b << "_comb_cert i s)\n\n";
    os << "theorem " << b << "_step_cert_eq " << ep << n.parameters() << " : " << b << "_step_cert " << ea << "i s = (" << b
       << "_next_cert " << ea << "i s, " << b << "_comb_cert i s) := rfl\n\n";
  }
  if (n.sequential) {
    const auto ep = n.edge_parameter(), ea = n.edge_argument();
    os << "theorem " << b << "_next_cert_refines_cert " << ep << n.parameters() << " : " << b << "_next_cert " << ea
       << "i s = " << b << "_nextStateFromCert " << ea << "(" << eval << ") s := rfl\n\n";
    os << "theorem " << b << "_step_cert_refines_lgraph_certificate " << ep << n.parameters() << " : " << b << "_step_cert " << ea
       << "i s = (" << b << "_nextStateFromCert " << ea << "(" << eval << ") s, " << b << "_outputsFromCert (" << eval
       << ")) := rfl\n\n";
  }
  if (!n.memory_values) {
    os << "theorem " << b << "_evalGraph_correct " << n.parameters() << " :\n    envCorrectOn " << b << "_graphCert.topo\n      ("
       << eval << ")\n      (graphDenotation " << b << "_graphCert.topo " << b << "_graphCert (" << b << "_sourceEnv " << args
       << ")) := by\n  exact evalGraphCorrectForCert " << b << "_graphCert (" << b << "_sourceEnv " << args << ")\n\n";
  }
}
}  // namespace lean_export
