// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_design_cert.hpp"

#include <stdexcept>

#include "lean_format.hpp"
namespace lean_export {
namespace {
std::string opt_nat(const std::optional<uint32_t>& n) { return n ? "some " + std::to_string(*n) : "none"; }
std::string format_op(const Op& op) {
  switch (op.kind) {
    case Operation::Const     : return std::string("LGraphOp.Op_Const") + " (" + lean_integer(op.value) + ")";
    case Operation::Sum       : return std::string("LGraphOp.Op_Sum") + " " + std::to_string(op.parameter);
    case Operation::Mult      : return std::string("LGraphOp.Op_Mult");
    case Operation::UDiv      : return std::string("LGraphOp.Op_UDiv");
    case Operation::And       : return std::string("LGraphOp.Op_And");
    case Operation::Or        : return std::string("LGraphOp.Op_Or");
    case Operation::Xor       : return std::string("LGraphOp.Op_Xor");
    case Operation::Ror       : return std::string("LGraphOp.Op_Ror");
    case Operation::EQ        : return std::string("LGraphOp.Op_EQ");
    case Operation::Not       : return std::string("LGraphOp.Op_Not");
    case Operation::SLT       : return std::string("LGraphOp.Op_SLT");
    case Operation::ULT       : return std::string("LGraphOp.Op_ULT");
    case Operation::SGT       : return std::string("LGraphOp.Op_SGT");
    case Operation::UGT       : return std::string("LGraphOp.Op_UGT");
    case Operation::SHL       : return std::string("LGraphOp.Op_SHL");
    case Operation::SRA       : return std::string("LGraphOp.Op_SRA");
    case Operation::MuxBool   : return std::string("LGraphOp.Op_MuxBool");
    case Operation::MuxN      : return std::string("LGraphOp.Op_MuxN");
    case Operation::Sext      : return std::string("LGraphOp.Op_Sext");
    case Operation::GetMask   : return std::string("LGraphOp.Op_GetMask");
    case Operation::SetMask   : return std::string("LGraphOp.Op_SetMask");
    case Operation::MemRead   : return std::string("LGraphOp.Op_MemRead");
    case Operation::MemWrite  : return std::string("LGraphOp.Op_MemWrite");
    case Operation::MemWriteBE: return std::string("LGraphOp.Op_MemWriteBE") + " " + std::to_string(op.parameter);
  }
  throw std::invalid_argument("unknown certificate operation");
}
}  // namespace
void emit_design_cert(const std::string& base, const CertificateIR& d, std::ostream& os) {
  // ---- sources ------------------------------------------------------------
  std::vector<std::string> src_lines;
  for (const auto& s : d.sources) {
    switch (s.kind) {
      case SourceKind::Input:
        src_lines.push_back("SourceDesc.input " + std::to_string(s.ordinal) + " " + std::to_string(s.width));
        break;
      case SourceKind::Const:
        src_lines.push_back("SourceDesc.const " + std::to_string(s.width) + " ("
                            + (s.implicit_enable ? "Int.ofNat 1" : lean_integer(s.const_int)) + ")");
        break;
      case SourceKind::Flop:
        if (s.async_reset) {
          src_lines.push_back("SourceDesc.flopQAsync " + std::to_string(s.ordinal) + " " + std::to_string(s.width) + " "
                              + std::to_string(s.reset_input) + " (" + lean_integer(s.reset_value) + ") "
                              + (s.reset_active_low ? "true" : "false"));
        } else {
          src_lines.push_back("SourceDesc.flopQ " + std::to_string(s.ordinal) + " " + std::to_string(s.width));
        }
        break;
      case SourceKind::MemImage:
        src_lines.push_back("SourceDesc.memImg " + std::to_string(s.ordinal) + " " + std::to_string(s.addr_w) + " "
                            + std::to_string(s.width));
        break;
      case SourceKind::RomConst: {
        std::string tbl = "#[";
        for (size_t i = 0; i < s.rom_contents.size(); ++i) {
          if (i) {
            tbl += ", ";
          }
          tbl += s.rom_contents[i];
        }
        tbl += "]";
        src_lines.push_back("SourceDesc.memConst " + std::to_string(s.addr_w) + " " + std::to_string(s.width) + " " + tbl);
        break;
      }
    }
  }

  // ---- nodes (deps remapped; `origin` keeps the LGraph id for debugging) ----
  std::vector<std::string> node_lines;
  for (const auto& n : d.nodes) {
    std::vector<uint32_t> deps;
    deps.reserve(n.deps.size());
    for (auto dep : n.deps) {
      deps.push_back(d.slot_of.at(dep));
    }
    node_lines.push_back("{ op := " + format_op(n.op) + ", width := " + std::to_string(n.width) + ", deps := " + nat_array(deps)
                         + ", origin := " + std::to_string(n.id) + " }");
  }

  std::vector<std::string> out_lines;
  for (const auto& o : d.outputs) {
    out_lines.push_back("{ slot := " + std::to_string(d.slot_of.at(o.id)) + ", width := " + std::to_string(o.width) + " }");
  }

  std::vector<std::string> flop_lines;
  for (const auto& f : d.flops) {
    std::optional<uint32_t> en  = f.enable.has_value() ? std::optional<uint32_t>(d.slot_of.at(*f.enable)) : std::nullopt;
    std::optional<uint32_t> rst = f.reset_pin.has_value() ? std::optional<uint32_t>(d.slot_of.at(*f.reset_pin)) : std::nullopt;
    flop_lines.push_back("{ width := " + std::to_string(f.width) + ", din := " + std::to_string(d.slot_of.at(f.din))
                         + ", enable := " + opt_nat(en) + ", resetPin := " + opt_nat(rst) + ", resetValue := ("
                         + lean_integer(f.reset_value) + "), resetActiveLow := " + (f.reset_active_low ? "true" : "false") + " }");
  }

  std::vector<std::string> mem_lines;
  for (const auto& m : d.memories) {
    mem_lines.push_back("{ aw := " + std::to_string(m.addr_w) + ", dw := " + std::to_string(m.data_w)
                        + ", nextImg := " + std::to_string(d.slot_of.at(m.next_img)) + " }");
  }

  auto emit_array = [&os](const char* field, const std::vector<std::string>& lines, bool) {
    os << "    " << field << " := #[";
    for (size_t i = 0; i < lines.size(); ++i) {
      os << (i ? "\n      , " : "\n        ") << lines[i];
    }
    os << (lines.empty() ? "]" : "\n      ]") << "\n";
  };

  os << "-- Emitted by pass.lean in `formal.lean.mode=verified_compiler`.\n";
  os << "-- This file contains NO semantic model and NO proof script: the compiler\n";
  os << "-- `Compiler.compileDesign` is proved correct once, for every accepted design,\n";
  os << "-- by `Compiler.compileDesign_correct`.  Slots are dense: sources occupy\n";
  os << "-- 0.." << (d.sources.size() ? d.sources.size() - 1 : 0) << " and node i occupies " << d.sources.size() << "+i.\n";
  os << "import LeanSemanticPrimitives.Compiler.CompileDesign\n\n";
  // A DesignCert for a real design is one array literal with thousands of
  // elements; the default recursion depth is exhausted while ELABORATING it, and
  // the definition then becomes noncomputable, which cascades into every
  // declaration below (including `native_decide`, whose failure looks like a
  // proof failure).  The legacy path emits the same two options for the same
  // reason.
  os << "set_option maxRecDepth 1000000\n";
  os << "set_option maxHeartbeats 0\n\n";
  os << "open Compiler\n\n";
  os << "def " << base << "_designCert : DesignCert :=\n";
  os << "  {\n";
  emit_array("sources ", src_lines, false);
  emit_array("nodes   ", node_lines, false);
  emit_array("outputs ", out_lines, false);
  emit_array("flops   ", flop_lines, false);
  emit_array("memories", mem_lines, true);
  os << "  }\n\n";

  // The residual program is DERIVED, not re-emitted: `compileDesign` is the
  // verified compiler, so re-deriving the bindings in C++ would reintroduce
  // exactly the untrusted step this branch exists to remove.
  //
  // CRITICAL: no `ResidualProgram` may appear in a THEOREM STATEMENT.  Naming one
  // there makes the kernel decide `.ok <Top>_residual` defeq
  // `.ok (match compileDesign D ...)`, which reduces the compiler in the kernel;
  // `Array.push` is `⟨toList ++ [a]⟩`, so 4,772 bindings cost O(N^2) kernel terms.
  // Measured on SingleCycleCPU: >1 h / 120 GB, killed, against 38 s / 7.4 GB for
  // the shape below.  `compileAndRun` keeps the program inside a function body.
  os << "/-- Compile-and-run.  The model IS the verified compiler applied to this\n";
  os << "certificate -- there is no separately emitted model to disagree with it. -/\n";
  os << "def " << base << "_step : RuntimeInput → RuntimeState → RuntimeResult :=\n";
  os << "  compileAndRun " << base << "_designCert\n\n";
  os << "/-- The whole per-design obligation: ONE boolean check.\n\n";
  os << "`native_decide`, not `.get!`: a compile failure is a BUILD failure rather\n";
  os << "than a runtime panic with the hypothesis left undischarged.  (`decide` is\n";
  os << "not usable -- `Array.map` is not kernel-reducible.) -/\n";
  os << "theorem " << base << "_compiles : compilesOk " << base << "_designCert = true := by\n";
  os << "  native_decide\n\n";
  os << "/-- The theorem, by direct instantiation.  No per-design proof script. -/\n";
  os << "theorem " << base << "_step_correct : ∀ inp st,\n";
  os << "    " << base << "_step inp st = interpretDesign " << base << "_designCert inp st :=\n";
  os << "  compileAndRun_correct " << base << "_designCert " << base << "_compiles\n\n";
  os << "/-- For evaluation only.  Deliberately NOT mentioned in any theorem\n";
  os << "statement -- see the comment above. -/\n";
  os << "def " << base << "_residual : ResidualProgram :=\n";
  os << "  match compileDesign " << base << "_designCert with\n";
  os << "  | .ok R    => R\n";
  os << "  | .error _ => default\n\n";
  os << "#print axioms " << base << "_step_correct\n";
}

void emit_design_cert(const DesignScan& design, const CertificateIR& certificate, std::ostream& os) {
  emit_design_cert(sanitize_lean(design.name), certificate, os);
}
void write_design_cert(const std::string& base, const CertificateIR& certificate, const std::string& path) {
  write_atomic(path, [&](std::ostream& os) { emit_design_cert(base, certificate, os); });
}
}  // namespace lean_export
