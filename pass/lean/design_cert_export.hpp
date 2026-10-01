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
#include <string_view>
#include <vector>

namespace lean_design_cert {

// Source kinds, matching `CertBuild::source_kind` exactly.
// Matches `CertBuild::source_kind` exactly.  `RomConst` is an IMMUTABLE table:
// unlike `MemImage` it has no entry in `RuntimeState.mems`, because a ROM carries
// nothing from cycle to cycle.  (A type=1 SYNCHRONOUS ROM is still not stateless
// -- its registered read port becomes an ordinary FlopDesc.)
enum class SourceKind { Input = 0, Const = 1, Flop = 2, MemImage = 3, RomConst = 4 };

struct SourceIn {
  uint32_t    id       = 0;  // emitter id
  SourceKind  kind     = SourceKind::Input;
  uint32_t    width    = 0;  // data width
  uint32_t    addr_w   = 0;  // MemImage only
  std::string const_int;     // Const only: a Lean `Int` expression
  uint32_t    ordinal  = 0;  // index into RuntimeInput / .flops / .mems
  // Flop only, and only for an ASYNCHRONOUS reset: a plain `flopQ` reads the
  // stored state and therefore gives SYNCHRONOUS semantics, so a combinational
  // reader in the same cycle would not see the reset value.  These carry the
  // reset so `sourceValue` can apply it.
  bool        async_reset      = false;
  uint32_t    reset_input      = 0;    // ORDINAL of the driving primary input
  std::string reset_value      = "0";  // Lean `Int`
  bool        reset_active_low = false;
  // RomConst only: `size` entries, each `width` bits, entry 0 first.  The Lean
  // side bounds reads by `contents.size`, NOT by 2^addr_w -- an inferred table's
  // entry count need not be a power of two and `addr_width` rounds up.
  std::vector<std::string> rom_contents;
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
        if (s.async_reset) {
          src_lines.push_back("SourceDesc.flopQAsync " + std::to_string(s.ordinal) + " "
                              + std::to_string(s.width) + " " + std::to_string(s.reset_input) + " ("
                              + s.reset_value + ") " + (s.reset_active_low ? "true" : "false"));
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
        src_lines.push_back("SourceDesc.memConst " + std::to_string(s.addr_w) + " " + std::to_string(s.width) + " "
                            + tbl);
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

// ---------------------------------------------------------------------------
// DCERT1 -- the same certificate as bytes instead of as a Lean term.
//
// `Compiler.CertIO.writeCert` defines this format and `parseCert` reads it; a
// design loaded this way never becomes a Lean constant, which is what removes
// the elaboration cost (1,398.7 s -> 1.97 s on `csr_regfile_gate`; see
// DIRECTION4_INCREMENTAL.md Part II).
//
// Emitted HERE rather than converted from the Lean text afterwards.  The
// converter `scripts/lean_cert_to_dcert.py` re-parses the literal this file
// just printed, so it is a second, independent transcription of the same data
// and it is trusted at run time.  This function walks the SAME `DesignIn` the
// Lean printer walks, with the SAME `Remap`, so there is one transcription, not
// two.  The Python converter is kept as an oracle for differential testing, not
// as a production step.
//
// Whitespace is load-bearing: `writeCert` separates with single spaces and ends
// every record with '\n', and the parity test compares bytes.
// ---------------------------------------------------------------------------

namespace detail {

// A Lean `Int` expression as DCERT1 decimal.
//
// The emitter writes constants three ways -- `(Int.ofNat 5)`, `(-Int.ofNat 5)`
// and a bare `5` -- and the certificate may carry values far wider than 64 bits
// (CVA6 has multi-hundred-bit buses), so this normalises TEXTUALLY and never
// parses into an integer type.  Leading zeros are dropped and a negative zero
// becomes "0", matching what Python's `int()` does in the oracle.
inline std::string dcert_int(std::string_view s) {
  auto trim = [](std::string_view v) {
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\n')) {
      v.remove_prefix(1);
    }
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\n')) {
      v.remove_suffix(1);
    }
    return v;
  };
  auto strip_parens = [](std::string_view v) {
    while (!v.empty() && (v.front() == '(' || v.front() == ')')) {
      v.remove_prefix(1);
    }
    while (!v.empty() && (v.back() == '(' || v.back() == ')')) {
      v.remove_suffix(1);
    }
    return v;
  };

  s = trim(strip_parens(trim(s)));
  bool neg = false;
  while (!s.empty() && s.front() == '-') {
    neg = !neg;
    s.remove_prefix(1);
    s = trim(s);
  }
  constexpr std::string_view kOfNat = "Int.ofNat";
  if (s.substr(0, kOfNat.size()) == kOfNat) {
    s = trim(s.substr(kOfNat.size()));
  }
  s = trim(strip_parens(s));
  // A nested `(-Int.ofNat 5)` puts the sign inside the parens.
  while (!s.empty() && s.front() == '-') {
    neg = !neg;
    s.remove_prefix(1);
    s = trim(s);
  }
  if (s.substr(0, kOfNat.size()) == kOfNat) {
    s = trim(strip_parens(trim(s.substr(kOfNat.size()))));
  }

  size_t first_sig = s.find_first_not_of('0');
  std::string digits = (first_sig == std::string_view::npos) ? std::string("0") : std::string(s.substr(first_sig));
  if (digits.empty() || digits == "0") {
    return "0";  // never "-0": the oracle's int() cannot produce one
  }
  return neg ? ("-" + digits) : digits;
}

// `LGraphOp.Op_X [arg]` as the (code, arg) pair `CertIO.opCode` assigns.
// Only Op_Const, Op_Sum and Op_MemWriteBE carry a payload; every other code
// writes 0, exactly as `opCode` does.
inline bool dcert_op(const std::string& op_expr, std::string& code, std::string& arg, std::string& why) {
  static const std::vector<std::string> kOps = {
      "Op_Const",   "Op_Sum",   "Op_Sub",     "Op_Mult",   "Op_Div",     "Op_UDiv",   "Op_SDiv",
      "Op_And",     "Op_Or",    "Op_Xor",     "Op_Ror",    "Op_Not",     "Op_LT",     "Op_GT",
      "Op_ULT",     "Op_UGT",   "Op_SLT",     "Op_SGT",    "Op_EQ",      "Op_SHL",    "Op_SRA",
      "Op_MuxBool", "Op_MuxN",  "Op_Sext",    "Op_GetMask", "Op_SetMask", "Op_MemRead", "Op_MemWrite",
      "Op_MemWriteBE"};

  std::string_view v{op_expr};
  constexpr std::string_view kPrefix = "LGraphOp.";
  if (v.substr(0, kPrefix.size()) != kPrefix) {
    why = "operator `" + op_expr + "` does not start with `LGraphOp.`";
    return false;
  }
  v.remove_prefix(kPrefix.size());

  size_t cut = v.find_first_of(" \t");
  const std::string name{v.substr(0, cut)};
  const std::string_view rest = (cut == std::string_view::npos) ? std::string_view{} : v.substr(cut);

  for (size_t i = 0; i < kOps.size(); ++i) {
    if (kOps[i] == name) {
      code = std::to_string(i);
      // `dcert_int` on the remainder, so a parenthesised `Op_Const ((Int.ofNat
      // 5))` is encoded as 5.  The Python oracle reads the payload with a bare
      // `(-?\d+)?` and silently yields 0 for that spelling; this does not.
      const bool has_arg = (name == "Op_Const" || name == "Op_Sum" || name == "Op_MemWriteBE");
      arg = (has_arg && !rest.empty()) ? dcert_int(rest) : std::string("0");
      return true;
    }
  }
  why = "unknown operator `" + name + "`; DCERT1 encodes the 29 codes of CertIO.opCode";
  return false;
}

}  // namespace detail

// Write the certificate in DCERT1.  Returns false and fills `err` on a remap
// failure or an operator DCERT1 cannot name.
inline bool emit_design_cert_dcert1(const DesignIn& d, std::ostream& os, RemapError& err) {
  const Remap rm(d, err);
  if (err.failed) {
    return false;
  }

  std::ostringstream out;
  out << "DCERT1\n";

  out << d.sources.size() << "\n";
  for (const auto& s : d.sources) {
    switch (s.kind) {
      case SourceKind::Input: out << "0 " << s.ordinal << " " << s.width << "\n"; break;
      case SourceKind::Const: out << "1 " << s.width << " " << detail::dcert_int(s.const_int) << "\n"; break;
      case SourceKind::Flop:
        if (s.async_reset) {
          out << "3 " << s.ordinal << " " << s.width << " " << s.reset_input << " "
              << detail::dcert_int(s.reset_value) << " " << (s.reset_active_low ? 1 : 0) << "\n";
        } else {
          out << "2 " << s.ordinal << " " << s.width << "\n";
        }
        break;
      case SourceKind::MemImage: out << "4 " << s.ordinal << " " << s.addr_w << " " << s.width << "\n"; break;
      case SourceKind::RomConst:
        out << "5 " << s.addr_w << " " << s.width << " " << s.rom_contents.size();
        for (const auto& v : s.rom_contents) {
          out << " " << detail::dcert_int(v);
        }
        out << "\n";
        break;
    }
  }

  out << d.nodes.size() << "\n";
  for (const auto& n : d.nodes) {
    std::string code, arg, why;
    if (!detail::dcert_op(n.op_expr, code, arg, why)) {
      err.failed  = true;
      err.message = "node " + std::to_string(n.id) + ": " + why;
      return false;
    }
    out << code << " " << arg << " " << n.width << " " << n.deps.size();
    for (auto dep : n.deps) {
      out << " " << rm.slot(dep, err, ("node " + std::to_string(n.id) + " dep").c_str());
    }
    out << " " << n.id << "\n";  // `origin`: the emitter id, for debugging only
  }

  out << d.outputs.size() << "\n";
  for (const auto& o : d.outputs) {
    out << rm.slot(o.id, err, "output") << " " << o.width << "\n";
  }

  out << d.flops.size() << "\n";
  for (const auto& f : d.flops) {
    const uint32_t he = f.enable.has_value() ? 1 : 0;
    const uint32_t e  = f.enable.has_value() ? rm.slot(*f.enable, err, "flop enable") : 0;
    const uint32_t hr = f.reset_pin.has_value() ? 1 : 0;
    const uint32_t r  = f.reset_pin.has_value() ? rm.slot(*f.reset_pin, err, "flop reset") : 0;
    out << f.width << " " << rm.slot(f.din, err, "flop din") << " " << he << " " << e << " " << hr << " " << r
        << " " << detail::dcert_int(f.reset_value) << " " << (f.reset_active_low ? 1 : 0) << "\n";
  }

  out << d.memories.size() << "\n";
  for (const auto& m : d.memories) {
    out << m.addr_w << " " << m.data_w << " " << rm.slot(m.next_img, err, "memory nextImg") << "\n";
  }

  if (err.failed) {
    return false;  // nothing written: a partial .dcert must never reach a reader
  }
  os << out.str();
  return true;
}

}  // namespace lean_design_cert
