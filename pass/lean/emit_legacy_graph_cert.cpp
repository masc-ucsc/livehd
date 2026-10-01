// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_legacy_graph_cert.hpp"

#include "emit_legacy_fast_model.hpp"
#include "lean_format.hpp"
namespace lean_export {
std::string legacy_node_cert(const CertNode& n) {
  return "{ nid := " + std::to_string(n.id) + ", op := " + format_op(n.op) + ", width := " + std::to_string(n.width)
         + ", deps := " + nat_list(n.deps) + " }";
}
void emit_legacy_graph_cert(const DesignScan& d, const CertificateIR& c, const LegacyNames& n, bool bridge, std::ostream& os) {
  const auto&           b    = n.base;
  const auto            val  = n.memory_values ? "CertVal" : "BV";
  const auto            args = n.arguments();
  std::vector<uint32_t> topo, sources;
  os << "def " << b << "_nodeCerts : List NodeCert := [\n";
  for (size_t i = 0; i < c.nodes.size(); ++i) {
    topo.push_back(c.nodes[i].id);
    os << "  " << legacy_node_cert(c.nodes[i]) << (i + 1 == c.nodes.size() ? "" : ",") << '\n';
  }
  os << "]\n\n";
  for (const auto& s : c.sources) {
    sources.push_back(s.id);
  }
  const auto zero = n.memory_values ? "CertVal.bv (mk_bv 0 0)" : "mk_bv 0 0";
  if (bridge) {
    std::vector<std::pair<uint32_t, std::string>> entries;
    for (const auto& s : c.sources) {
      entries.emplace_back(s.id, "fun " + args + " => " + legacy_source_value(c, n, s));
    }
    os << "def " << b << "_srcTree : BT (" << b << "_in → " << (n.sequential ? b + "_state → " : "") << val << ") :=\n  "
       << bst_literal(entries) << "\n\n";
    os << "def " << b << "_sourceEnv " << n.parameters() << " : Nat -> " << val << " := fun n =>\n  (BT.find " << b
       << "_srcTree n).elim (" << zero << ") (fun f => f " << args << ")\n\n";
    entries.clear();
    for (const auto& node : c.nodes) {
      entries.emplace_back(node.id, "(" + legacy_node_cert(node) + " : NodeCert)");
    }
    os << "def " << b << "_nodesTree : BT NodeCert :=\n  " << bst_literal(entries) << "\n\n";
    os << "def " << b << "_nodesFn : Nat → Option NodeCert := fun n => BT.find " << b << "_nodesTree n\n\n";
  } else {
    os << "def " << b << "_sourceEnv " << n.parameters() << " : Nat -> " << val << " := fun n =>\n";
    for (const auto& s : c.sources) {
      os << "  if n = " << s.id << " then " << legacy_source_value(c, n, s) << " else\n";
    }
    os << "  " << zero << "\n\n";
  }
  os << "def " << b << "_graphCert : GraphCert :=\n  { topo := " << nat_list(topo) << ", sources := " << nat_list(sources)
     << ", nodes := " << (bridge ? b + "_nodesFn" : "nodes_of_list " + b + "_nodeCerts") << " }\n\n";
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
    os << "def " << b << "_nextStateFromCert (rho : Nat -> " << val << ") (s : " << b << "_state) : " << b << "_state :=\n";
    std::vector<std::string> fields;
    for (const auto& f : c.flops) {
      const auto field = n.flop_field(f);
      fields.push_back(field);
      auto reset = f.reset_pin ? "(bv_nonzero " + bv(*f.reset_pin) + ")" : "false";
      if (f.reset_pin && f.reset_active_low) {
        reset = "(!" + reset + ")";
      }
      const auto enable = f.enable ? "(bv_nonzero " + bv(*f.enable) + ")" : "true";
      os << "  let new_" << field << " : BitVec " << f.width << " := flop_next " << reset << " "
         << lit_bv(f.width, lean_integer(f.reset_value)) << " " << enable << " " << word(f.din, f.width) << " s." << field << '\n';
    }
    for (const auto& m : c.memories) {
      const auto field = n.memories.at(m.origin);
      fields.push_back(field);
      os << "  let new_" << field << " : (BitVec " << m.addr_w << " -> BitVec " << m.data_w << ") := memdec " << m.addr_w << " "
         << m.data_w << " (rho " << m.next_img << ").asMem\n";
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
    os << "def " << b << "_next_cert " << n.parameters() << " : " << b << "_state :=\n  " << b << "_nextStateFromCert (" << eval
       << ") s\n\n";
    os << "def " << b << "_step_cert " << n.parameters() << " : " << b << "_state × " << b << "_out :=\n  (" << b
       << "_next_cert i s, " << b << "_comb_cert i s)\n\n";
    os << "theorem " << b << "_step_cert_eq " << n.parameters() << " : " << b << "_step_cert i s = (" << b << "_next_cert i s, "
       << b << "_comb_cert i s) := rfl\n\n";
  }
  if (n.sequential) {
    os << "theorem " << b << "_next_cert_refines_cert " << n.parameters() << " : " << b << "_next_cert i s = " << b
       << "_nextStateFromCert (" << eval << ") s := rfl\n\n";
    os << "theorem " << b << "_step_cert_refines_lgraph_certificate " << n.parameters() << " : " << b << "_step_cert i s = (" << b
       << "_nextStateFromCert (" << eval << ") s, " << b << "_outputsFromCert (" << eval << ")) := rfl\n\n";
  }
  if (!n.memory_values) {
    os << "theorem " << b << "_evalGraph_correct " << n.parameters() << " :\n    envCorrectOn " << b << "_graphCert.topo\n      ("
       << eval << ")\n      (graphDenotation " << b << "_graphCert.topo " << b << "_graphCert (" << b << "_sourceEnv " << args
       << ")) := by\n  exact evalGraphCorrectForCert " << b << "_graphCert (" << b << "_sourceEnv " << args << ")\n\n";
  }
}
}  // namespace lean_export
