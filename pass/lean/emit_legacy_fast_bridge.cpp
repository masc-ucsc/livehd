// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_legacy_fast_bridge.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "emit_legacy_fast_model.hpp"
#include "lean_format.hpp"
namespace lean_export {
void emit_legacy_fast_bridge(const DesignScan& design, const CertificateIR& c, const LegacyNames& names, std::ostream& ofs) {
  const auto&                     base_name  = names.base;
  const bool                      sequential = names.sequential;
  const bool                      mem_mode   = names.memory_values;
  const std::string               val_ty     = mem_mode ? "CertVal" : "BV";
  std::vector<uint32_t>           topo_ids, source_ids;
  std::map<uint32_t, std::string> source_leaf;
  for (const auto& n : c.nodes) {
    topo_ids.push_back(n.id);
  }
  for (const auto& src : c.sources) {
    source_ids.push_back(src.id);
    source_leaf.emplace(src.id, legacy_source_leaf(c, names, src));
  }
  auto              val_wrap = [&](const std::string& value, int memory) { return legacy_value_wrap(names, value, memory == 3); };
  const std::string P = sequential ? ("(i : " + base_name + "_in) (s : " + base_name + "_state)") : ("(i : " + base_name + "_in)");
  const std::string A = sequential ? "i s" : "i";
  const std::string G = base_name + "_graphCert";

  auto                     width_of = [&](uint32_t id) { return certificate_width(c, id); };
  const std::set<uint32_t> topo_set(topo_ids.begin(), topo_ids.end());
  auto                     is_mem_val = [&](uint32_t d) { return certificate_memory_value(c, d); };
  auto                     phi_dep    = [&](uint32_t d) -> std::string {
    if (topo_set.count(d)) {
      if (mem_mode && is_mem_val(d)) {
        return "memenc (" + base_name + "_fv" + std::to_string(d) + " " + A + ")";
      }
      return "bvenc (" + base_name + "_fv" + std::to_string(d) + " " + A + ")";
    }
    if (mem_mode) {
      const std::string proj = is_mem_val(d) ? ").asMem" : ").asBV";
      return "(" + base_name + "_sourceEnv " + A + " " + std::to_string(d) + proj;
    }
    return base_name + "_sourceEnv " + A + " " + std::to_string(d);
  };
  auto raw_dep = [&](uint32_t d) -> std::string {
    if (topo_set.count(d)) {
      return "(" + base_name + "_fv" + std::to_string(d) + " " + A + ")";
    }
    auto it = source_leaf.find(d);
    return "(" + (it == source_leaf.end() ? std::string("0#0") : it->second) + ")";
  };

  ofs << "\n-- Step-5 fast-view bridge: fast model = certificate model.\n\n";

  {
    const std::string phi_closure = sequential ? "fun i s => " : "fun i => ";
    const std::string phi_ty      = sequential ? ("(" + base_name + "_in → " + base_name + "_state → " + val_ty + ")")
                                               : ("(" + base_name + "_in → " + val_ty + ")");
    std::vector<std::pair<uint32_t, std::string>> phi_pairs;
    for (const auto id : topo_ids) {
      const std::string enc = (mem_mode && is_mem_val(id)) ? "memenc (" : "bvenc (";
      const std::string val = enc + base_name + "_fv" + std::to_string(id) + " " + A + ")";
      phi_pairs.emplace_back(id, phi_closure + val_wrap(val, is_mem_val(id) ? 3 : 0));
    }
    ofs << "def " << base_name << "_phiTree : BT " << phi_ty << " :=\n  " << bst_literal(phi_pairs) << "\n\n";
    ofs << "def " << base_name << "_phi " << P << " : Nat → " << val_ty << " := fun n =>\n"
        << "  (BT.find " << base_name << "_phiTree n).elim (" << base_name << "_sourceEnv " << A << " n) (fun f => f " << A
        << ")\n\n";
  }

  for (const auto sid : source_ids) {
    const auto        kind = certificate_source(c, sid)->kind;
    const std::string leaf = source_leaf.count(sid) ? source_leaf.at(sid) : "0#0";
    if (mem_mode) {
      if ((kind == SourceKind::MemImage || kind == SourceKind::RomConst)) {
        ofs << "theorem " << base_name << "_src" << sid << " " << P << " : (" << base_name << "_sourceEnv " << A << " " << sid
            << ").asMem = memenc (" << leaf << ") := by\n  rfl\n";
      } else {
        ofs << "theorem " << base_name << "_src" << sid << " " << P << " : (" << base_name << "_sourceEnv " << A << " " << sid
            << ").asBV = bvenc (" << leaf << ") := by\n";
        ofs << (kind == SourceKind::Const ? "  exact mk_bv_ofInt _\n" : "  rfl\n");
      }
      continue;
    }
    ofs << "theorem " << base_name << "_src" << sid << " " << P << " : " << base_name << "_sourceEnv " << A << " " << sid
        << " = bvenc (" << leaf << ") := by\n";
    if (kind == SourceKind::Const) {
      ofs << "  exact mk_bv_ofInt _\n";
    } else {
      ofs << "  rfl\n";
    }
  }
  ofs << "\n";

  ofs << "theorem " << base_name << "_bridge_nodup : " << G << ".topo.Nodup := by native_decide\n";
  ofs << "theorem " << base_name << "_bridge_some : ∀ n ∈ " << G << ".topo, (" << G << ".nodes n).isSome := by native_decide\n";
  ofs << "theorem " << base_name << "_bridge_depord : GraphRefine.DepOrdered " << G << " " << G << ".topo :=\n";
  ofs << "  GraphRefine.depOrdered_of_bool " << G << " " << G << ".topo (by native_decide)\n\n";

  for (const auto& info : c.nodes) {
    std::string deplist;
    for (size_t k = 0; k < info.deps.size(); ++k) {
      if (k != 0) {
        deplist += ", ";
      }
      deplist += phi_dep(info.deps[k]);
    }
    std::string bridge_call;
    bool        sext_mode = false;
    std::string sext_a, sext_amt;
    bool        getmask_allones_mode = false;
    std::string getmask_allones_call;
    std::string closer    = "ofInt_zero_eq, bv_zext_id, Int.ofNat_eq_natCast, Nat.cast_ofNat, Nat.cast_one";
    bool        supported = true;
    if (info.op.kind == Operation::GetMask) {
      bridge_call = "getmask_bridge' _ _ (by decide)";
      if (info.deps.size() == 2) {
        const uint32_t mdep = info.deps[1];
        const auto*    mask = certificate_source(c, mdep);
        const uint32_t mw   = width_of(mdep);
        if (mask && mask->kind == SourceKind::Const && mask->const_int == "-1" && mw <= info.width) {
          getmask_allones_mode = true;
          getmask_allones_call = "@getmask_bridge_allones_ofNat " + std::to_string(width_of(info.deps[0])) + " "
                                 + std::to_string(mw) + " " + std::to_string(info.width) + " _ (by decide)";
        }
      }

    } else if ((info.op.kind == Operation::Sum && info.op.parameter == 2) && info.deps.size() == 2) {
      bridge_call = "sum2_bridge";
    } else if (info.op.kind == Operation::And && info.deps.size() == 2) {
      bridge_call = "and_bridge";
    } else if (info.op.kind == Operation::And && info.deps.size() == 4) {
      bridge_call = "and4_bridge";
    } else if (info.op.kind == Operation::And && info.deps.size() == 3) {
      bridge_call = "and3_bridge";
    } else if (info.op.kind == Operation::Or && info.deps.size() == 2) {
      bridge_call = "or_bridge";
    } else if (info.op.kind == Operation::Or && info.deps.size() == 1) {
      bridge_call = "or1_bridge";
    } else if (info.op.kind == Operation::Or && info.deps.size() == 3) {
      bridge_call = "or3_bridge";
    } else if (info.op.kind == Operation::Or && info.deps.size() == 4) {
      bridge_call = "or4_bridge";
    } else if (info.op.kind == Operation::Or) {
      bridge_call = "orn_bv_bridge";
      closer
          = "List.foldl_cons, List.foldl_nil, bv_to_bitvec_bvenc_zext, BitVec.zero_or, "
            "bv_zext_id, Int.ofNat_eq_natCast, Nat.cast_ofNat, Nat.cast_one";
    } else if (info.op.kind == Operation::Xor && info.deps.size() == 2) {
      bridge_call = "xor_bridge";
    } else if (info.op.kind == Operation::Not && info.deps.size() == 1) {
      bridge_call = "not_bridge";
    } else if (info.op.kind == Operation::SHL && info.deps.size() == 2) {
      bridge_call = "shl_bridge";
    } else if (info.op.kind == Operation::Ror && info.deps.size() == 1) {
      bridge_call = "ror1_bridge";
    } else if (info.op.kind == Operation::MuxBool && info.deps.size() == 3) {
      bridge_call = "muxbool_bridge";
    } else if (info.op.kind == Operation::MuxN && info.deps.size() == 3) {
      bridge_call = "muxn3_bridge";
    } else if (info.op.kind == Operation::SRA && info.deps.size() == 2) {
      bridge_call = (info.width > width_of(info.deps[0])) ? "sra_bridge_sext" : "sra_bridge _ _ (by decide)";
    } else if ((info.op.kind == Operation::EQ || info.op.kind == Operation::ULT || info.op.kind == Operation::UGT)
               && info.deps.size() == 2) {
      const uint32_t    w0         = width_of(info.deps[0]);
      const uint32_t    w1         = width_of(info.deps[1]);
      const uint32_t    cw         = std::max(w0, w1);
      const std::string base_lemma = info.op.kind == Operation::EQ    ? "eq_bridge"
                                     : info.op.kind == Operation::ULT ? "ult_bridge"
                                                                      : "ugt_bridge";
      bridge_call = "@" + base_lemma + " " + std::to_string(w0) + " " + std::to_string(w1) + " " + std::to_string(cw)
                    + " _ _ (by decide) (by decide)";
    } else if (info.op.kind == Operation::Sext && info.deps.size() == 2) {
      const auto* amount = certificate_source(c, info.deps[1]);
      if (!amount || amount->kind != SourceKind::Const
          || (amount->const_int != std::to_string(std::min(info.width, width_of(info.deps[0])))
              && amount->const_int != std::to_string(width_of(info.deps[0])))) {
        throw std::runtime_error("legacy Sext bridge requires a constant natural sign position at node " + std::to_string(info.id));
      }
      sext_mode = true;
      sext_a    = raw_dep(info.deps[0]);
      sext_amt  = raw_dep(info.deps[1]);
    } else if (info.op.kind == Operation::SLT && info.deps.size() == 2 && width_of(info.deps[0]) == width_of(info.deps[1])) {
      bridge_call = "slt_bridge";
    } else if (info.op.kind == Operation::SGT && info.deps.size() == 2 && width_of(info.deps[0]) == width_of(info.deps[1])) {
      bridge_call = "sgt_bridge";
    } else if ((info.op.kind == Operation::Sum && info.op.parameter == 1) && info.deps.size() == 2) {
      bridge_call = "sum1_bridge";
    } else {
      supported = false;
    }
    bool mem_result = false;
    if (info.op.kind == Operation::MemRead) {
      bridge_call = "mem_read_bridge";
      supported   = true;
    } else if (info.op.kind == Operation::MemWrite) {
      bridge_call = "mem_write_bridge";
      supported   = true;
      mem_result  = true;
    } else if (info.op.kind == Operation::MemWriteBE) {
      bridge_call = "mem_write_be_bridge _ _ _ _ _ (by decide)";
      supported   = true;
      mem_result  = true;
    }
    std::string srcfacts;
    for (const auto d : info.deps) {
      if (certificate_source(c, d) != nullptr) {
        srcfacts += base_name + "_src" + std::to_string(d) + " " + A + ", ";
      }
    }
    const std::string mem_closer;
    const std::string eval_node_fn = mem_mode ? "evalNodeC" : "evalNode";
    ofs << "theorem " << base_name << "_rec" << info.id << " " << P << " : " << base_name << "_phi " << A << " " << info.id << " = "
        << eval_node_fn << " " << G << " (" << base_name << "_phi " << A << ") " << info.id << " := by\n";
    if (supported) {
      if (mem_mode) {
        const std::string ctor   = mem_result ? "CertVal.mem" : "CertVal.bv";
        const std::string enc    = mem_result ? "memenc" : "bvenc";
        auto              op_arg = [&](size_t k) { return "(" + phi_dep(info.deps[k]) + ")"; };
        if (info.op.kind == Operation::MemRead) {
          ofs << "  show CertVal.bv (bvenc (" << base_name << "_fv" << info.id << " " << A << ")) = CertVal.bv (cert_mem_read "
              << info.width << " " << op_arg(0) << " " << op_arg(1) << " " << op_arg(2) << ")\n";
        } else if (info.op.kind == Operation::MemWrite) {
          ofs << "  show CertVal.mem (memenc (" << base_name << "_fv" << info.id << " " << A << ")) = CertVal.mem (cert_mem_write "
              << op_arg(0) << " " << op_arg(1) << " " << op_arg(2) << " " << op_arg(3) << ")\n";
        } else if (mem_result) {
          const std::string byte_w = std::to_string(info.op.parameter);
          ofs << "  show CertVal.mem (memenc (" << base_name << "_fv" << info.id << " " << A
              << ")) = CertVal.mem (cert_mem_write_be " << info.width << " " << op_arg(0) << " " << op_arg(1) << " " << op_arg(2)
              << " " << op_arg(3) << " " << byte_w << ")\n";
        } else {
          ofs << "  show " << ctor << " (" << enc << " (" << base_name << "_fv" << info.id << " " << A << ")) = " << ctor
              << " (eval_op (" << format_op(info.op) << ") " << info.width << " [" << deplist << "])\n";
        }
        ofs << "  refine congrArg " << ctor << " ?_\n";
      } else {
        ofs << "  show bvenc (" << base_name << "_fv" << info.id << " " << A << ") = eval_op (" << format_op(info.op) << ") "
            << info.width << " [" << deplist << "]\n";
      }
      if (sext_mode) {
        ofs << "  first\n";
        ofs << "  | rw [" << srcfacts << "sext_bridge " << sext_a << " " << sext_amt << " (by decide)]\n";
        ofs << "  | rw [" << srcfacts << "sext_bridge_low " << sext_a << " " << sext_amt << " (by decide) (by decide)]\n";
      } else if (getmask_allones_mode) {
        ofs << "  first\n";
        ofs << "  | rw [" << srcfacts << getmask_allones_call << "]\n";
        ofs << "  | rw [" << srcfacts << bridge_call << "]\n";
      } else {
        ofs << "  rw [" << srcfacts << bridge_call << "]\n";
      }
      ofs << "  simp only [" << base_name << "_fv" << info.id << mem_closer << ", " << closer << "]\n";
    } else {
      throw std::runtime_error("legacy fast bridge does not support " + format_op(info.op) + " at node " + std::to_string(info.id));
    }
  }
  ofs << "theorem " << base_name << "_bridge_rec " << P << " : ∀ n ∈ " << G << ".topo, " << base_name << "_phi " << A
      << " n = " << (mem_mode ? "evalNodeC " : "evalNode ") << G << " (" << base_name << "_phi " << A << ") n :=\n";
  {
    std::string term = "List.forall_mem_nil _";
    for (auto it = topo_ids.rbegin(); it != topo_ids.rend(); ++it) {
      term = "List.forall_mem_cons.mpr ⟨" + base_name + "_rec" + std::to_string(*it) + " " + A + ", " + term + "⟩";
    }
    ofs << "  " << term << "\n\n";
  }

  ofs << "theorem " << base_name << "_phiTree_keys_sub : BT.keys " << base_name << "_phiTree ⊆ " << G
      << ".topo := by native_decide\n";
  ofs << "theorem " << base_name << "_bridge_src " << P << " : ∀ n ∈ " << G << ".topo, ∀ d ∈ depopts_of " << G << " n, d ∉ " << G
      << ".topo → " << base_name << "_sourceEnv " << A << " d = " << base_name << "_phi " << A << " d := by\n";
  ofs << "  intro n _ d _ hd\n";
  ofs << "  have hnone : BT.find " << base_name << "_phiTree d = none :=\n";
  ofs << "    BT.find_eq_none _ d (fun hc => hd (" << base_name << "_phiTree_keys_sub hc))\n";
  ofs << "  show " << base_name << "_sourceEnv " << A << " d = (BT.find " << base_name << "_phiTree d).elim (" << base_name
      << "_sourceEnv " << A << " d) (fun f => f " << A << ")\n";
  ofs << "  rw [hnone]\n  rfl\n\n";

  ofs << "theorem " << base_name << "_comb_refines_fast " << P << " : " << base_name << "_comb " << A << " = " << base_name
      << "_comb_cert " << A << " := by\n";
  ofs << "  have hb := GraphRefine." << (mem_mode ? "evalGraphC_of_localAgree " : "evalGraph_of_localAgree ") << G << " ("
      << base_name << "_phi " << A << ") (" << base_name << "_sourceEnv " << A << ")\n";
  ofs << "    " << base_name << "_bridge_nodup " << base_name << "_bridge_depord " << base_name << "_bridge_some (" << base_name
      << "_bridge_rec " << A << ") (" << base_name << "_bridge_src " << A << ")\n";
  ofs << "  unfold " << base_name << "_comb " << base_name << "_comb_cert " << base_name << "_outputsFromCert\n";
  std::set<uint32_t> output_seen;
  for (const auto& output : legacy_output_ids(design, c)) {
    if (!output || !output_seen.insert(*output).second) {
      continue;
    }
    const auto oid = *output;
    if (!topo_set.count(oid)) {
      ofs << "  rw [GraphRefine." << (mem_mode ? "evalGraphC_not_mem " : "evalGraph_not_mem ") << G << " (" << base_name
          << "_sourceEnv " << A << ") " << G << ".topo " << oid << " (by decide), " << base_name << "_src" << oid << " " << A
          << "]\n";
      continue;
    }
    if (mem_mode) {
      ofs << "  rw [hb " << oid << " (by decide), show (" << base_name << "_phi " << A << " " << oid << ").asBV = bvenc ("
          << base_name << "_fv" << oid << " " << A << ") from by rfl]\n";
    } else {
      ofs << "  rw [hb " << oid << " (by decide), show " << base_name << "_phi " << A << " " << oid << " = bvenc (" << base_name
          << "_fv" << oid << " " << A << ") from by rfl]\n";
    }
  }
  ofs << "  simp only [bv_to_bitvec_bvenc_zext, bv_to_bitvec_bvenc, bv_zext_id]\n\n";

  if (sequential) {
    ofs << "theorem " << base_name << "_next_refines_fast " << P << " : " << base_name << "_next " << A << " = " << base_name
        << "_next_cert " << A << " := by\n";
    ofs << "  have hb := GraphRefine." << (mem_mode ? "evalGraphC_of_localAgree " : "evalGraph_of_localAgree ") << G << " ("
        << base_name << "_phi " << A << ") (" << base_name << "_sourceEnv " << A << ")\n";
    ofs << "    " << base_name << "_bridge_nodup " << base_name << "_bridge_depord " << base_name << "_bridge_some (" << base_name
        << "_bridge_rec " << A << ") (" << base_name << "_bridge_src " << A << ")\n";
    ofs << "  unfold " << base_name << "_next " << base_name << "_next_cert " << base_name << "_nextStateFromCert\n";

    std::vector<uint32_t> next_ids;
    std::set<uint32_t>    next_seen;
    for (const auto& f : c.flops) {
      for (const auto id : {std::optional<uint32_t>(f.din), f.reset_pin, f.enable}) {
        if (id && next_seen.insert(*id).second) {
          next_ids.push_back(*id);
        }
      }
    }
    for (const auto& m : c.memories) {
      if (next_seen.insert(m.next_img).second) {
        next_ids.push_back(m.next_img);
      }
    }
    for (const auto d : next_ids) {
      if (topo_set.count(d)) {
        if (mem_mode) {
          const bool memv = is_mem_val(d);
          ofs << "  rw [hb " << d << " (by decide), show (" << base_name << "_phi " << A << " " << d << ")."
              << (memv ? "asMem" : "asBV") << " = " << (memv ? "memenc (" : "bvenc (") << base_name << "_fv" << d << " " << A
              << ") from by rfl]\n";
          continue;
        }
        ofs << "  rw [hb " << d << " (by decide), show " << base_name << "_phi " << A << " " << d << " = bvenc (" << base_name
            << "_fv" << d << " " << A << ") from by rfl]\n";
      } else {
        ofs << "  rw [GraphRefine." << (mem_mode ? "evalGraphC_not_mem " : "evalGraph_not_mem ") << G << " (" << base_name
            << "_sourceEnv " << A << ") " << G << ".topo " << d << " (by decide), " << base_name << "_src" << d << " " << A
            << "]\n";
      }
    }
    {
      std::string next_closer = "bv_to_bitvec_bvenc_zext, bv_to_bitvec_bvenc, bv_zext_id, bv_nonzero_bvenc";
      if (mem_mode) {
        next_closer += ", memdec_memenc";
      }
      ofs << "  simp only [" << next_closer << "]\n";
    }
    ofs << "\n";

    ofs << "theorem " << base_name << "_step_refines_fast " << P << " : " << base_name << "_step " << A << " = " << base_name
        << "_step_cert " << A << " := by\n";
    ofs << "  unfold " << base_name << "_step " << base_name << "_step_cert\n";
    ofs << "  rw [" << base_name << "_next_refines_fast " << A << ", " << base_name << "_comb_refines_fast " << A << "]\n\n";
  }

  ofs << "\n-- Axiom audit: sorryAx or a missing name is a failure.\n";
  ofs << "#print axioms " << base_name << "_comb_refines_fast\n";
  if (sequential) {
    ofs << "#print axioms " << base_name << "_next_refines_fast\n";
    ofs << "#print axioms " << base_name << "_step_refines_fast\n";
  }
}
}  // namespace lean_export
