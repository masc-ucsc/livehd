// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_legacy_schema.hpp"

#include <algorithm>
#include <set>

#include "lean_format.hpp"
namespace lean_export {
std::string LegacyNames::flop_field(const FlopDriver& f) const {
  return f.read_port ? read_registers.at({f.origin, *f.read_port}) : flops.at(f.origin);
}
std::string LegacyNames::parameters() const { return "(i : " + base + "_in)" + (sequential ? " (s : " + base + "_state)" : ""); }
std::string LegacyNames::arguments() const { return sequential ? "i s" : "i"; }
std::string LegacyNames::edge_parameter() const { return edges.empty() ? "" : "(e : " + base + "_edges) "; }
std::string LegacyNames::edge_argument() const { return edges.empty() ? "" : "e "; }
LegacyNames legacy_names(const DesignScan& d, std::string_view top) {
  LegacyNames n;
  n.base = sanitize_lean(top.empty() ? d.name : top);
  std::set<std::string> used;
  auto                  field = [&](std::string_view role, const std::string& raw) {
    const auto base   = std::string(role) + sanitize_lean(raw);
    auto       name   = base;
    size_t     suffix = 0;
    while (!used.insert(name).second) {
      name = base + "_" + std::to_string(++suffix);
    }
    return name;
  };
  // Allocate by declaration identity, then present ports in the scan's name order.
  auto ports = [&](const std::vector<Port>& ps, std::string_view role) {
    std::vector<size_t> order;
    for (size_t i = 0; i < ps.size(); ++i) {
      order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ps[a].id < ps[b].id; });
    std::vector<std::string> fields(ps.size());
    for (auto i : order) {
      fields[i] = field(role, ps[i].name);
    }
    return fields;
  };
  n.inputs  = ports(d.inputs, "in_");
  n.outputs = ports(d.outputs, "out_");
  for (const auto& f : d.flops) {
    n.flops.emplace(f.id, field("st_", f.raw_name.empty() ? "flop_" + std::to_string(f.id) : f.raw_name));
  }
  for (const auto& [id, m] : d.memories) {
    const auto raw = m.raw_name.empty() ? "mem_" + std::to_string(id) : m.raw_name;
    // A ROM is a constant source, not unconstrained mutable state.
    if (!m.is_rom) {
      n.memories.emplace(id, field("st_", raw));
    }
    if (m.sync) {
      for (auto port : m.read_ports) {
        n.read_registers.emplace(std::make_pair(id, port), field("st_", raw + "_rdata_" + std::to_string(port)));
      }
    }
  }
  n.sequential    = !n.flops.empty() || !n.memories.empty() || !n.read_registers.empty();
  n.memory_values = !d.memories.empty();
  // Per-domain edges, only when there is more than one domain. The fields live
  // in their own record, so they are uniquified among themselves.
  if (n.sequential && d.clocks.size() > 1) {
    std::set<std::string> edge_used;
    for (const auto& c : d.clocks) {
      const auto base = "edge_" + sanitize_lean(c.name);
      auto       name = base;
      for (size_t suffix = 0; !edge_used.insert(name).second;) {
        name = base + "_" + std::to_string(++suffix);
      }
      n.edges.push_back(name);
    }
  }
  return n;
}
void emit_legacy_schema(const DesignScan& d, const LegacyNames& n, std::ostream& os) {
  auto ports = [&](const char* role, const std::vector<Port>& ps, const std::vector<std::string>& names) {
    os << "structure " << n.base << "_" << role << " where\n";
    if (ps.empty()) {
      os << "  " << role << "_dummy : BitVec 1\n";
    }
    for (size_t i = 0; i < ps.size(); ++i) {
      os << "  " << names[i] << " : BitVec " << ps[i].width << "\n";
    }
    os << "deriving Repr, Inhabited\n\n";
  };
  ports("in", d.inputs, n.inputs);
  ports("out", d.outputs, n.outputs);
  if (!n.sequential) {
    return;
  }
  os << "structure " << n.base << "_state where\n";
  std::map<uint32_t, const Flop*> flops;
  for (const auto& f : d.flops) {
    flops.emplace(f.id, &f);
  }
  for (const auto& [id, f] : flops) {
    os << "  " << n.flops.at(id) << " : BitVec " << f->width << "\n";
  }
  for (const auto& [id, m] : d.memories) {
    if (!m.is_rom) {
      os << "  " << n.memories.at(id) << " : (BitVec " << m.addr_width << " -> BitVec " << m.bits << ")\n";
    }
    if (m.sync) {
      for (auto p : m.read_ports) {
        os << "  " << n.read_registers.at({id, p}) << " : BitVec " << m.bits << "\n";
      }
    }
  }
  os << (n.memories.empty() ? "deriving Repr, Inhabited\n\n" : "deriving Inhabited\n\n");
  if (!n.edges.empty()) {
    // Which clock domains edge on this step: `true` fires the domain. A flop or
    // memory whose domain does not fire holds; an asserted ASYNCHRONOUS reset
    // still resets its flop (DesignSemantics.srcFlopNext / srcMemNext).
    os << "structure " << n.base << "_edges where\n";
    for (const auto& e : n.edges) {
      os << "  " << e << " : Bool\n";
    }
    os << "deriving Repr, Inhabited\n\n";
  }
  // The start state a runner uses by default: every flop at its reset value,
  // every memory at its power-on words (DCERT3 `MemoryDesc.init`, 0 past them),
  // every read register at 0. A Verilog power-on value with no
  // reset (`reg r = 5`) reaches the scan as the flop's reset value, so starting
  // from `default` (all zero) would start such a flop at 0 and every later cycle
  // would be off. The certificate runners start the same way (`resetState`,
  // `initStateOf`).
  os << "def " << n.base << "_initState : " << n.base << "_state :=\n";
  std::string fields;
  for (const auto& [id, f] : flops) {
    if (f->initial != "0") {
      fields += std::string(fields.empty() ? "" : ",\n    ") + n.flops.at(id) + " := BitVec.ofInt " + std::to_string(f->width)
                + " (" + f->initial + ")";
    }
  }
  // A WRITABLE memory's power-on words (DCERT3 `MemoryDesc.init`): word `a` is
  // the table entry, 0 past its end -- the start the certificate runners take.
  for (const auto& [id, m] : d.memories) {
    if (m.is_rom || m.init_contents.empty()) {
      continue;
    }
    std::string table;
    for (size_t k = 0; k < m.init_contents.size(); ++k) {
      table += (k == 0 ? "" : ", ") + std::string("BitVec.ofNat ") + std::to_string(m.bits) + " " + m.init_contents[k];
    }
    fields += std::string(fields.empty() ? "" : ",\n    ") + n.memories.at(id) + " := fun a => (#[" + table + "] : Array (BitVec "
              + std::to_string(m.bits) + ")).getD a.toNat 0";
  }
  os << (fields.empty() ? std::string("  default") : "  { (default : " + n.base + "_state) with\n    " + fields + " }") << "\n\n";
}
void emit_legacy_field_mapping(const DesignScan& d, const LegacyNames& n, std::ostream& os) {
  os << "\n-- Field mapping (Lean selector -> RTL name).\n";
  auto comment = [](const std::string& raw) {
    // RTL identifiers may contain newlines. Keep mapping comments on one line.
    std::string out;
    for (char c : raw) {
      out += c == '\n' ? "\\n" : c == '\r' ? "\\r" : std::string(1, c);
    }
    return out;
  };
  for (size_t i = 0; i < d.inputs.size(); ++i) {
    os << "-- " << n.inputs[i] << " -> " << comment(d.inputs[i].name) << '\n';
  }
  for (size_t i = 0; i < d.outputs.size(); ++i) {
    os << "-- " << n.outputs[i] << " -> " << comment(d.outputs[i].name) << '\n';
  }
  for (const auto& f : d.flops) {
    os << "-- " << n.flops.at(f.id) << " -> " << comment(f.raw_name) << '\n';
  }
  for (const auto& [id, m] : d.memories) {
    if (!m.is_rom) {
      os << "-- " << n.memories.at(id) << " -> " << comment(m.raw_name) << '\n';
    }
    if (m.sync) {
      for (auto p : m.read_ports) {
        os << "-- " << n.read_registers.at({id, p}) << " -> " << comment(m.raw_name) << " read port " << p << '\n';
      }
    }
  }
}
}  // namespace lean_export
