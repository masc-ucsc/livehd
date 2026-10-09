// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "emit_legacy_schema.hpp"

#include <algorithm>
#include <set>
#include <span>

#include "lean_format.hpp"
namespace lean_export {
namespace {
// Bound physical records for both Lean code generation and its interpreter.
// A flat record can typecheck below the runtime's 256-object limit and still
// crash during interpreter evaluation (128 fields reproduces it in Lean 4.31).
// Inherited chunks retain selectors, field order and record-literal syntax.
void emit_record(std::ostream& os, const std::string& name, std::span<const std::string> fields, bool repr) {
  constexpr size_t limit = 64;
  if (fields.size() > limit) {
    const auto left   = name + "_chunk0";
    const auto right  = name + "_chunk1";
    const auto middle = fields.size() / 2;
    emit_record(os, left, fields.first(middle), repr);
    emit_record(os, right, fields.subspan(middle), repr);
    os << "structure " << name << " extends " << left << ", " << right << " where\n";
  } else {
    os << "structure " << name << " where\n";
    for (const auto& field : fields) {
      os << field;
    }
  }
  os << (repr ? "deriving Repr, Inhabited\n\n" : "deriving Inhabited\n\n");
}
}  // namespace
std::string LegacyNames::flop_field(const FlopDriver& f) const {
  return f.read_port ? read_registers.at({f.origin, *f.read_port}) : flops.at(f.origin);
}
std::string LegacyNames::parameters() const { return "(i : " + base + "_in)" + (sequential ? " (s : " + base + "_state)" : ""); }
std::string LegacyNames::arguments() const { return sequential ? "i s" : "i"; }
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
  return n;
}
void emit_legacy_schema(const DesignScan& d, const LegacyNames& n, std::ostream& os) {
  auto ports = [&](const char* role, const std::vector<Port>& ps, const std::vector<std::string>& names) {
    std::vector<std::string> fields;
    if (ps.empty()) {
      fields.push_back("  " + std::string(role) + "_dummy : BitVec 1\n");
    }
    for (size_t i = 0; i < ps.size(); ++i) {
      fields.push_back("  " + names[i] + " : BitVec " + std::to_string(ps[i].width) + "\n");
    }
    emit_record(os, n.base + "_" + role, fields, true);
  };
  ports("in", d.inputs, n.inputs);
  ports("out", d.outputs, n.outputs);
  if (!n.sequential) {
    return;
  }
  std::vector<std::string>        fields;
  std::map<uint32_t, const Flop*> flops;
  for (const auto& f : d.flops) {
    flops.emplace(f.id, &f);
  }
  for (const auto& [id, f] : flops) {
    fields.push_back("  " + n.flops.at(id) + " : BitVec " + std::to_string(f->width) + "\n");
  }
  for (const auto& [id, m] : d.memories) {
    if (!m.is_rom) {
      fields.push_back("  " + n.memories.at(id) + " : (BitVec " + std::to_string(m.addr_width) + " -> BitVec "
                       + std::to_string(m.bits) + ")\n");
    }
    if (m.sync) {
      for (auto p : m.read_ports) {
        fields.push_back("  " + n.read_registers.at({id, p}) + " : BitVec " + std::to_string(m.bits) + "\n");
      }
    }
  }
  emit_record(os, n.base + "_state", fields, n.memories.empty());
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
