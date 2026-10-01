// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Differential test for DCERT1 emission.
//
// `emit_design_cert_dcert1` is the production transcription; it runs inside
// `pass.lean` on the same `DesignIn` the Lean printer uses.  The question this
// binary answers is whether it agrees, BYTE FOR BYTE, with the two transcriptions
// that already exist:
//
//   * `scripts/lean_cert_to_dcert.py`, which re-parses the emitted .lean text;
//   * `Compiler.CertIO.writeCert`, which serialises an elaborated `DesignCert`.
//
// Running the production emitter over a REAL design needs a real `DesignIn`, and
// building one needs `lhd` -- which this branch cannot build (its `yosys_slang`
// pin predates the upstream rename to `sv-elab`).  So the harness reconstructs a
// `DesignIn` from an emitted .lean instead.
//
// THE RECONSTRUCTOR IS TEST SCAFFOLDING, NOT A TRUSTED PATH.  It is the inverse
// of the Lean printer, it never runs in production, and a bug in it shows up as
// a byte MISMATCH rather than as a false pass -- the comparison is against files
// produced by two other independent transcriptions.
//
// One wrinkle worth stating.  A `.lean` certificate records DENSE SLOTS, while
// `DesignIn` records emitter ids that `Remap` turns into slots.  Node records
// still carry their original id in `origin`, so node ids are recoverable; source
// records do not carry one, so the reconstructor invents ids for them in a high
// disjoint range.  `Remap` assigns slots in array order regardless of the id
// VALUES, so the emitted slots come out identical; a collision with a real
// origin would make `Remap` fail loudly rather than silently renumber.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "design_cert_export.hpp"

namespace {

// Source ids live above every id the emitter can mint: LGraph nids are small and
// the synthetic certificate ids start at 1e9, so 3.2e9 cannot collide.
constexpr uint32_t kSrcBase = 0xC0000000u;

std::string trim(std::string s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) {
    return "";
  }
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Strip the array punctuation `emit_array` prints: records are one per line,
// the first indented and every later one prefixed with ", ".
std::string record(const std::string& line) {
  std::string s = trim(line);
  if (s.rfind(", ", 0) == 0) {
    s = trim(s.substr(2));
  }
  return s;
}

bool starts_with(const std::string& s, std::string_view p) { return s.rfind(p, 0) == 0; }

// The whitespace-separated tokens of `s`, with `(`/`)` kept inside tokens so a
// Lean Int expression survives as one token where it is parenthesised.
std::vector<std::string> tokens(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream       is(s);
  std::string              t;
  while (is >> t) {
    out.push_back(t);
  }
  return out;
}

// The parenthesised group starting at `pos`, e.g. `((Int.ofNat 5))`.
std::string paren_group(const std::string& s, size_t pos) {
  size_t i = s.find('(', pos);
  if (i == std::string::npos) {
    return "";
  }
  int    depth = 0;
  size_t j     = i;
  for (; j < s.size(); ++j) {
    if (s[j] == '(') {
      ++depth;
    } else if (s[j] == ')') {
      if (--depth == 0) {
        break;
      }
    }
  }
  return s.substr(i, j - i + 1);
}

// `field := <value>` up to `, ` or `}`.
std::string field(const std::string& s, std::string_view name) {
  const std::string key = std::string(name) + " := ";
  size_t            i   = s.find(key);
  if (i == std::string::npos) {
    return "";
  }
  i += key.size();
  if (s[i] == '(') {
    return paren_group(s, i);
  }
  if (s[i] == '#') {  // `#[...]`
    size_t j = s.find(']', i);
    return s.substr(i, j - i + 1);
  }
  size_t j = s.find_first_of(",}", i);
  return trim(s.substr(i, (j == std::string::npos ? s.size() : j) - i));
}

std::vector<uint32_t> nat_array(const std::string& arr) {
  std::vector<uint32_t> out;
  size_t                i = arr.find('[');
  size_t                j = arr.find(']');
  if (i == std::string::npos || j == std::string::npos) {
    return out;
  }
  std::string body = arr.substr(i + 1, j - i - 1);
  for (auto& c : body) {
    if (c == ',') {
      c = ' ';
    }
  }
  std::istringstream is(body);
  uint32_t           v = 0;
  while (is >> v) {
    out.push_back(v);
  }
  return out;
}

std::vector<std::string> int_array(const std::string& arr) {
  std::vector<std::string> out;
  size_t                   i = arr.find('[');
  size_t                   j = arr.rfind(']');
  if (i == std::string::npos || j == std::string::npos) {
    return out;
  }
  std::string body = arr.substr(i + 1, j - i - 1);
  size_t      p    = 0;
  while (p < body.size()) {
    size_t q = body.find(',', p);
    if (q == std::string::npos) {
      q = body.size();
    }
    std::string tok = trim(body.substr(p, q - p));
    if (!tok.empty()) {
      out.push_back(tok);
    }
    p = q + 1;
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: dcert_parity <mod_Lgraph.lean> <out.dcert>\n";
    return 2;
  }
  std::ifstream in(argv[1]);
  if (!in) {
    std::cerr << "cannot read " << argv[1] << "\n";
    return 2;
  }

  lean_design_cert::DesignIn din;
  std::vector<uint32_t>      node_origin;  // slot-S -> emitter id
  // Deps/outputs/flops/memories reference SLOTS in the file; `DesignIn` wants
  // emitter ids, so they are resolved after both arrays are known.
  std::vector<std::vector<uint32_t>>                   node_dep_slots;
  std::vector<uint32_t>                                out_slots;
  std::vector<std::tuple<uint32_t, std::optional<uint32_t>, std::optional<uint32_t>>> flop_slots;
  std::vector<uint32_t>                                mem_slots;

  enum class Sec { None, Sources, Nodes, Outputs, Flops, Memories } sec = Sec::None;
  std::string line;
  while (std::getline(in, line)) {
    const std::string t = trim(line);
    if (starts_with(t, "sources  :=")) {
      sec = Sec::Sources;
      continue;
    }
    if (starts_with(t, "nodes    :=")) {
      sec = Sec::Nodes;
      continue;
    }
    if (starts_with(t, "outputs  :=")) {
      sec = Sec::Outputs;
      continue;
    }
    if (starts_with(t, "flops    :=")) {
      sec = Sec::Flops;
      continue;
    }
    if (starts_with(t, "memories :=")) {
      sec = Sec::Memories;
      continue;
    }
    if (t == "]" || t == "}" || t.empty()) {
      continue;
    }
    const std::string r = record(line);

    if (sec == Sec::Sources && starts_with(r, "SourceDesc.")) {
      lean_design_cert::SourceIn s;
      s.id = kSrcBase + static_cast<uint32_t>(din.sources.size());
      if (starts_with(r, "SourceDesc.memConst")) {
        auto tk     = tokens(r.substr(0, r.find('#')));
        s.kind      = lean_design_cert::SourceKind::RomConst;
        s.addr_w    = static_cast<uint32_t>(std::stoul(tk[1]));
        s.width     = static_cast<uint32_t>(std::stoul(tk[2]));
        s.rom_contents = int_array(r.substr(r.find('#')));
      } else if (starts_with(r, "SourceDesc.flopQAsync")) {
        // `flopQAsync ord w ri (<Int>) true|false` -- the Int is parenthesised.
        auto tk            = tokens(r);
        s.kind             = lean_design_cert::SourceKind::Flop;
        s.async_reset      = true;
        s.ordinal          = static_cast<uint32_t>(std::stoul(tk[1]));
        s.width            = static_cast<uint32_t>(std::stoul(tk[2]));
        s.reset_input      = static_cast<uint32_t>(std::stoul(tk[3]));
        s.reset_value      = paren_group(r, r.find(tk[3]) + tk[3].size());
        s.reset_active_low = (r.find("true") != std::string::npos);
      } else if (starts_with(r, "SourceDesc.flopQ")) {
        auto tk   = tokens(r);
        s.kind    = lean_design_cert::SourceKind::Flop;
        s.ordinal = static_cast<uint32_t>(std::stoul(tk[1]));
        s.width   = static_cast<uint32_t>(std::stoul(tk[2]));
      } else if (starts_with(r, "SourceDesc.input")) {
        auto tk   = tokens(r);
        s.kind    = lean_design_cert::SourceKind::Input;
        s.ordinal = static_cast<uint32_t>(std::stoul(tk[1]));
        s.width   = static_cast<uint32_t>(std::stoul(tk[2]));
      } else if (starts_with(r, "SourceDesc.const")) {
        auto tk     = tokens(r);
        s.kind      = lean_design_cert::SourceKind::Const;
        s.width     = static_cast<uint32_t>(std::stoul(tk[1]));
        s.const_int = paren_group(r, r.find(tk[1]) + tk[1].size());
      } else if (starts_with(r, "SourceDesc.memImg")) {
        auto tk   = tokens(r);
        s.kind    = lean_design_cert::SourceKind::MemImage;
        s.ordinal = static_cast<uint32_t>(std::stoul(tk[1]));
        s.addr_w  = static_cast<uint32_t>(std::stoul(tk[2]));
        s.width   = static_cast<uint32_t>(std::stoul(tk[3]));
      } else {
        std::cerr << "unhandled source: " << r << "\n";
        return 2;
      }
      din.sources.push_back(std::move(s));
    } else if (sec == Sec::Nodes && starts_with(r, "{ op :=")) {
      lean_design_cert::NodeIn n;
      const size_t             op_b = r.find("op := ") + 6;
      const size_t             op_e = r.find(", width :=");
      n.op_expr                     = trim(r.substr(op_b, op_e - op_b));
      n.width                       = static_cast<uint32_t>(std::stoul(field(r, "width")));
      n.id                          = static_cast<uint32_t>(std::stoul(field(r, "origin")));
      node_origin.push_back(n.id);
      node_dep_slots.push_back(nat_array(field(r, "deps")));
      din.nodes.push_back(std::move(n));
    } else if (sec == Sec::Outputs && starts_with(r, "{ slot :=")) {
      lean_design_cert::OutputIn o;
      o.width = static_cast<uint32_t>(std::stoul(field(r, "width")));
      out_slots.push_back(static_cast<uint32_t>(std::stoul(field(r, "slot"))));
      din.outputs.push_back(o);
    } else if (sec == Sec::Flops && starts_with(r, "{ width :=")) {
      lean_design_cert::FlopIn f;
      f.width                 = static_cast<uint32_t>(std::stoul(field(r, "width")));
      f.reset_value           = field(r, "resetValue");
      f.reset_active_low      = (field(r, "resetActiveLow") == "true");
      const std::string en    = field(r, "enable");
      const std::string rp    = field(r, "resetPin");
      std::optional<uint32_t> en_slot, rp_slot;
      if (en != "none") {
        en_slot = static_cast<uint32_t>(std::stoul(en.substr(en.find(' ') + 1)));
      }
      if (rp != "none") {
        rp_slot = static_cast<uint32_t>(std::stoul(rp.substr(rp.find(' ') + 1)));
      }
      flop_slots.emplace_back(static_cast<uint32_t>(std::stoul(field(r, "din"))), en_slot, rp_slot);
      din.flops.push_back(f);
    } else if (sec == Sec::Memories && starts_with(r, "{ aw :=")) {
      lean_design_cert::MemoryIn m;
      m.addr_w = static_cast<uint32_t>(std::stoul(field(r, "aw")));
      m.data_w = static_cast<uint32_t>(std::stoul(field(r, "dw")));
      mem_slots.push_back(static_cast<uint32_t>(std::stoul(field(r, "nextImg"))));
      din.memories.push_back(m);
    }
  }

  // slot -> emitter id, the inverse of what `Remap` will redo.
  const uint32_t S = static_cast<uint32_t>(din.sources.size());
  auto           id_of_slot = [&](uint32_t slot) -> uint32_t {
    return slot < S ? (kSrcBase + slot) : node_origin[slot - S];
  };
  for (size_t i = 0; i < din.nodes.size(); ++i) {
    for (auto s : node_dep_slots[i]) {
      din.nodes[i].deps.push_back(id_of_slot(s));
    }
  }
  for (size_t i = 0; i < din.outputs.size(); ++i) {
    din.outputs[i].id = id_of_slot(out_slots[i]);
  }
  for (size_t i = 0; i < din.flops.size(); ++i) {
    const auto& [din_slot, en, rp] = flop_slots[i];
    din.flops[i].din               = id_of_slot(din_slot);
    if (en) {
      din.flops[i].enable = id_of_slot(*en);
    }
    if (rp) {
      din.flops[i].reset_pin = id_of_slot(*rp);
    }
  }
  for (size_t i = 0; i < din.memories.size(); ++i) {
    din.memories[i].next_img = id_of_slot(mem_slots[i]);
  }

  std::ofstream os(argv[2]);
  if (!os) {
    std::cerr << "cannot write " << argv[2] << "\n";
    return 2;
  }
  lean_design_cert::RemapError err;
  if (!lean_design_cert::emit_design_cert_dcert1(din, os, err)) {
    std::cerr << "DCERT1 emission failed: " << err.message << "\n";
    return 1;
  }
  std::cerr << argv[2] << ": " << din.sources.size() << " sources, " << din.nodes.size() << " nodes, "
            << din.outputs.size() << " outputs, " << din.flops.size() << " flops, " << din.memories.size()
            << " memories\n";
  return 0;
}
