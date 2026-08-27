// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Step 8 of the B1+B2 verified-compiler plan: emit ONLY
//
//     def <Top>_designCert : DesignCert := ...
//
// plus the residual program, the `.ok` witness and the instantiated theorem.
// Nothing else -- no `<Top>_comb`, no `<Top>_next`, no `<Top>_step`, and no
// per-node proof scripts.  Everything after `DesignCert` is proved once, for all
// designs, by `Compiler.compileDesign_correct`.
//
// This header is deliberately free of every LGraph type.  It consumes the plain
// data `pass_lean.cpp` has already computed while building the existing
// certificate (`CertNodeInfo`, `CertBuild::source_kind/_width`,
// `MemCertIds::array_src/next_chain`) and does two things:
//
//   1. REMAP the emitter's sparse ids (LGraph nids plus synthetic ids above
//      1e9) onto the dense slot space `DesignCert` requires: sources occupy
//      0..S-1 and node `i` occupies S+i.  This is what turns the three
//      per-design `native_decide` structural gates into one bounds check.
//   2. FORMAT the result as Lean.
//
// Keeping it plain-data also makes it unit-testable without a graph.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace lean_design_cert {

// Source kinds, matching `CertBuild::source_kind` exactly.
enum class SourceKind { Input = 0, Const = 1, Flop = 2, MemImage = 3 };

struct SourceIn {
  uint32_t    id       = 0;  // emitter id
  SourceKind  kind     = SourceKind::Input;
  uint32_t    width    = 0;  // data width
  uint32_t    addr_w   = 0;  // MemImage only
  std::string const_int;     // Const only: a Lean `Int` expression
  uint32_t    ordinal  = 0;  // index into RuntimeInput / .flops / .mems
};

struct NodeIn {
  uint32_t              id = 0;  // emitter id (an LGraph nid, or synthetic)
  std::string           op_expr;  // e.g. "LGraphOp.Op_And" -- reused verbatim
  uint32_t              width = 0;
  std::vector<uint32_t> deps;
};

struct OutputIn {
  uint32_t id    = 0;  // emitter id of the driving node/source
  uint32_t width = 0;
};

struct FlopIn {
  uint32_t                width = 0;
  uint32_t                din   = 0;
  std::optional<uint32_t> enable;
  std::optional<uint32_t> reset_pin;
  std::string             reset_value = "0";  // Lean `Int` expression (the `initial` pin)
  bool                    reset_active_low = false;  // `negreset` rather than `reset_pin`
};

struct MemoryIn {
  uint32_t addr_w   = 0;
  uint32_t data_w   = 0;
  uint32_t next_img = 0;  // emitter id of the write-chain tail
};

struct DesignIn {
  std::vector<SourceIn> sources;  // in the order they will occupy slots 0..S-1
  std::vector<NodeIn>   nodes;    // in TOPOLOGICAL order
  std::vector<OutputIn> outputs;
  std::vector<FlopIn>   flops;
  std::vector<MemoryIn> memories;
};

// Why a remap can fail.  Each is a genuine exporter bug, reported loudly rather
// than papered over -- an unknown id would otherwise silently become slot 0.
struct RemapError {
  bool        failed = false;
  std::string message;
};

class Remap {
public:
  explicit Remap(const DesignIn& d, RemapError& err) {
    uint32_t slot = 0;
    for (const auto& s : d.sources) {
      if (!slot_of_.emplace(s.id, slot).second) {
        err.failed  = true;
        err.message = "duplicate certificate id " + std::to_string(s.id) + " in sources";
        return;
      }
      ++slot;
    }
    num_sources_ = slot;
    for (const auto& n : d.nodes) {
      if (!slot_of_.emplace(n.id, slot).second) {
        err.failed  = true;
        err.message = "duplicate certificate id " + std::to_string(n.id)
                      + " in nodes or source/node id collision";
        return;
      }
      ++slot;
    }
    num_slots_ = slot;
  }

  uint32_t num_sources() const { return num_sources_; }
  uint32_t num_slots() const { return num_slots_; }

  // Dense slot for an emitter id, or an error if the id was never declared.
  uint32_t slot(uint32_t id, RemapError& err, const char* what) const {
    auto it = slot_of_.find(id);
    if (it == slot_of_.end()) {
      if (!err.failed) {
        err.failed  = true;
        err.message = std::string(what) + " references id " + std::to_string(id)
                      + " which is neither a source nor a topo node";
      }
      return 0;
    }
    return it->second;
  }

private:
  std::map<uint32_t, uint32_t> slot_of_;
  uint32_t                     num_sources_ = 0;
  uint32_t                     num_slots_   = 0;
};

namespace detail {

inline std::string nat_array(const std::vector<uint32_t>& v) {
  std::ostringstream oss;
  oss << "#[";
  for (size_t i = 0; i < v.size(); ++i) {
    if (i) {
      oss << ", ";
    }
    oss << v[i];
  }
  oss << "]";
  return oss.str();
}

inline std::string opt_nat(const std::optional<uint32_t>& o) {
  return o.has_value() ? ("some " + std::to_string(*o)) : std::string("none");
}

}  // namespace detail

// Emit the whole file body.  Returns false and fills `err` on a remap failure.
inline bool emit_design_cert(const std::string& base, const DesignIn& d, std::ostream& os, RemapError& err) {
  const Remap rm(d, err);
  if (err.failed) {
    return false;
  }

  // ---- sources ------------------------------------------------------------
  std::vector<std::string> src_lines;
  for (const auto& s : d.sources) {
    switch (s.kind) {
      case SourceKind::Input:
        src_lines.push_back("SourceDesc.input " + std::to_string(s.ordinal) + " " + std::to_string(s.width));
        break;
      case SourceKind::Const:
        src_lines.push_back("SourceDesc.const " + std::to_string(s.width) + " (" + s.const_int + ")");
        break;
      case SourceKind::Flop:
        src_lines.push_back("SourceDesc.flopQ " + std::to_string(s.ordinal) + " " + std::to_string(s.width));
        break;
      case SourceKind::MemImage:
        src_lines.push_back("SourceDesc.memImg " + std::to_string(s.ordinal) + " " + std::to_string(s.addr_w) + " "
                            + std::to_string(s.width));
        break;
    }
  }

  // ---- nodes (deps remapped; `origin` keeps the LGraph id for debugging) ----
  std::vector<std::string> node_lines;
  for (const auto& n : d.nodes) {
    std::vector<uint32_t> deps;
    deps.reserve(n.deps.size());
    for (auto dep : n.deps) {
      deps.push_back(rm.slot(dep, err, ("node " + std::to_string(n.id) + " dep").c_str()));
    }
    node_lines.push_back("{ op := " + n.op_expr + ", width := " + std::to_string(n.width)
                         + ", deps := " + detail::nat_array(deps) + ", origin := " + std::to_string(n.id) + " }");
  }

  std::vector<std::string> out_lines;
  for (const auto& o : d.outputs) {
    out_lines.push_back("{ slot := " + std::to_string(rm.slot(o.id, err, "output")) + ", width := "
                        + std::to_string(o.width) + " }");
  }

  std::vector<std::string> flop_lines;
  for (const auto& f : d.flops) {
    std::optional<uint32_t> en  = f.enable.has_value()
                                      ? std::optional<uint32_t>(rm.slot(*f.enable, err, "flop enable"))
                                      : std::nullopt;
    std::optional<uint32_t> rst = f.reset_pin.has_value()
                                      ? std::optional<uint32_t>(rm.slot(*f.reset_pin, err, "flop reset"))
                                      : std::nullopt;
    flop_lines.push_back("{ width := " + std::to_string(f.width) + ", din := "
                         + std::to_string(rm.slot(f.din, err, "flop din")) + ", enable := " + detail::opt_nat(en)
                         + ", resetPin := " + detail::opt_nat(rst) + ", resetValue := (" + f.reset_value
                         + "), resetActiveLow := " + (f.reset_active_low ? "true" : "false") + " }");
  }

  std::vector<std::string> mem_lines;
  for (const auto& m : d.memories) {
    mem_lines.push_back("{ aw := " + std::to_string(m.addr_w) + ", dw := " + std::to_string(m.data_w)
                        + ", nextImg := " + std::to_string(rm.slot(m.next_img, err, "memory nextImg")) + " }");
  }

  if (err.failed) {
    return false;
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
  os << "-- 0.." << (rm.num_sources() ? rm.num_sources() - 1 : 0) << " and node i occupies " << rm.num_sources()
     << "+i.\n";
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
  return true;
}

}  // namespace lean_design_cert
