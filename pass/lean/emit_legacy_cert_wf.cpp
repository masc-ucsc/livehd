// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_legacy_cert_wf.hpp"

#include <algorithm>
#include <stdexcept>

#include "emit_legacy_graph_cert.hpp"
#include "lean_format.hpp"
namespace lean_export {
namespace {
bool simple_shape(const CertNode& n) {
  const auto arity = n.deps.size();
  if (!n.width) {
    return false;
  }
  switch (n.op.kind) {
    case Operation::Const     : return arity == 0;
    case Operation::Sum       : return arity > 0 && n.op.parameter <= arity;
    case Operation::And       :
    case Operation::Or        :
    case Operation::Xor       : return arity > 0;
    case Operation::Ror       : return arity > 0 && n.width == 1;
    case Operation::Not       : return arity == 1;
    case Operation::EQ        :
    case Operation::SLT       :
    case Operation::SGT       :
    case Operation::ULT       :
    case Operation::UGT       : return arity == 2 && n.width == 1;
    case Operation::GetMask   :
    case Operation::SHL       :
    case Operation::SRA       :
    case Operation::Sext      : return arity == 2;
    case Operation::MuxBool   :
    case Operation::MemRead   : return arity == 3;
    case Operation::MuxN      : return arity > 1;
    case Operation::MemWrite  :
    case Operation::MemWriteBE: return arity == 4;
    case Operation::Mult      :
    case Operation::UDiv      :
    case Operation::SetMask   : return false;  // no simpleOpCertWfBool lemma for these shapes
  }
  return false;
}
}  // namespace
void emit_legacy_cert_wf(const CertificateIR& c, const LegacyNames& n, const LegacyEmitOptions& o, std::ostream& os) {
  const auto g = n.base + "_graphCert";
  if (o.cert_wf == LeanCertWFMode::Skip) {
    os << "-- Certificate well-formedness proof skipped (cert_wf=skip).\n\n";
    return;
  }
  if (o.cert_wf == LeanCertWFMode::Sorry) {
    os << "-- Explicitly requested unproved certificate obligation (cert_wf=sorry).\n";
    os << "theorem " << n.base << "_graphCert_wf : graphCertWf " << g << " := by sorry\n";
    os << "#print axioms " << n.base << "_graphCert_wf\n\n";
    return;
  }
  // Eval is an explicit request for concrete local checks. It shares chunked
  // composition; it never evaluates global all_ids for each node/chunk.
  const auto               size  = std::max<size_t>(1, o.cert_chunk_size);
  const auto               count = (c.nodes.size() + size - 1) / size;
  const auto               limit = o.cert_chunk_limit ? std::min(count, o.cert_chunk_limit) : count;
  std::vector<std::string> chunks;
  for (size_t i = 0; i < limit; ++i) {
    const auto name = n.base + "_wf_chunk" + std::to_string(i);
    chunks.push_back(name);
    const auto end       = std::min(c.nodes.size(), (i + 1) * size);
    bool       constants = true, simple = true;
    os << "def " << name << " : List NodeCert := [\n";
    for (size_t j = i * size; j < end; ++j) {
      const auto& node  = c.nodes[j];
      constants        &= node.op.kind == Operation::Const && node.deps.empty();
      simple           &= simple_shape(node);
      os << "  " << legacy_node_cert(node) << (j + 1 == end ? "" : ",") << '\n';
    }
    os << "]\n";
    if (!simple && o.cert_wf != LeanCertWFMode::Eval && o.cert_wf_fallback == LeanCertWFFallback::Fail) {
      throw std::runtime_error("legacy cert_wf chunk " + std::to_string(i)
                               + " has an unsupported simple proof shape; no fallback selected");
    }
    os << "theorem " << name << "_wf : LegacyCertWF.ChunkWf " << g << " (" << name << ".map NodeCert.nid) := by\n";
    if (!simple && o.cert_wf != LeanCertWFMode::Eval && o.cert_wf_fallback == LeanCertWFFallback::Sorry) {
      os << "  sorry -- explicitly requested cert_wf_fallback=sorry\n\n";
      continue;
    }
    if (constants) {
      os << "  exact LegacyCertWF.chunk_of_constants " << g << " " << name << " (by native_decide) (by decide)\n\n";
    } else {
      os << "  apply LegacyCertWF.chunk_of_shape " << g << " " << name << " (by native_decide)\n";
      if (simple) {
        os << "  · exact LegacyCertWF.simple_shape " << name << " (by decide)\n";
      } else {
        os << "  · native_decide\n";
      }
      // Only this chunk's concrete dependency subset is checked here. The
      // graph's node lookup is a balanced tree whenever WF is requested.
      os << "  · native_decide\n\n";
    }
  }
  if (limit != count) {
    os << "-- Partial certificate-WF experiment: cert_chunk_limit omitted the remaining chunks.\n";
    os << "-- No whole-graph well-formedness theorem is asserted.\n\n";
    return;
  }
  std::vector<std::pair<uint32_t, std::string>> slots;
  for (const auto& [id, slot] : c.slot_of) {
    slots.emplace_back(id, std::to_string(slot));
  }
  os << "def " << n.base << "_wf_slots : OpBridge.BT Nat :=\n  " << bst_literal(slots) << "\n";
  os << "theorem " << n.base << "_graphCert_wf : graphCertWf " << g << " := by\n";
  os << "  apply LegacyCertWF.graph_of_chunks " << g << "\n";
  os << "  · exact LegacyCertWF.dense_nodup _ (fun id => (OpBridge.BT.find " << n.base
     << "_wf_slots id).getD 0) (by native_decide)\n";
  os << "  · exact ";
  for (const auto& chunk : chunks) {
    os << "(LegacyCertWF.chunk_append " << g << " _ _ " << chunk << "_wf ";
  }
  os << "(by intro id h; cases h)";
  for (size_t i = 0; i < chunks.size(); ++i) {
    os << ")";
  }
  os << "\n  · native_decide\n";
  os << "#print axioms " << n.base << "_graphCert_wf\n\n";
}
}  // namespace lean_export
