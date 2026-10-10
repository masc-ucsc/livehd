// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "design_io_meta.hpp"

#include <algorithm>
#include <set>
#include <cstdio>
#include <sstream>

namespace lean_export {

DesignIoMeta build_io_meta(const DesignScan& design, const CertificateIR& cert) {
  DesignIoMeta meta;
  meta.top = design.name;

  // Which input ordinals the emitted model actually reads, and how far the
  // runtime array must reach. Both come from the CERTIFICATE's sources, because
  // that is what the runtime will be driving -- reading them off the scan's port
  // list instead would describe ports the model may never have referenced.
  std::set<uint32_t> used;
  uint32_t           arity = 0;
  for (const auto& src : cert.sources) {
    if (src.kind == SourceKind::Input) {
      used.insert(src.ordinal);
      arity = std::max(arity, src.ordinal + 1);
    }
    // `flopQAsync` carries its reset ordinal, and `Direct.inputArity` takes the
    // max over BOTH that and `.input` sources -- so this does too.
    //
    // Whether the scanner ALSO emits an `.input` source for the same port is a
    // separate question, and an earlier comment here asserted it does not.
    // That was wrong: `build_certificate` sets `reset_pin` through
    // `dep(*f.reset, 1)`, which creates an Input source. So for a scanner-
    // produced certificate the two agree. The max is still what matters,
    // because a certificate whose reset ordinal is referenced ONLY through
    // `flopQAsync` is well-formed, and an arity computed from `.input` alone
    // would be too small for it -- `checkRuntime` would then reject an array a
    // caller had built correctly from this sidecar.
    if (src.kind == SourceKind::Flop && src.async_reset) {
      used.insert(src.reset_input);
      arity = std::max(arity, src.reset_input + 1);
    }
  }
  meta.runtime_input_arity = arity;

  // THE RUNTIME ORDINAL IS THE INDEX INTO `design.inputs`, not `Port::id`.
  // `Port::id` is the scan's internal source id (an opaque 2'000'000'000-based
  // counter); `certificate_ir.cpp` remaps it to the dense position `i`, and that
  // position is what indexes `RuntimeInput`. Publishing `Port::id` would hand a
  // caller numbers in the billions for an array of four.
  //
  // Every declared port is listed, INCLUDING ones the model never reads. The
  // ordinals are dense, so "preserving gaps" here means never COMPACTING the
  // list: dropping an unused interior port would shift every later port down one
  // and silently misalign the caller's array.
  for (uint32_t i = 0; i < design.inputs.size(); ++i) {
    const auto& p = design.inputs[i];
    meta.inputs.push_back({p.name, i, p.width, used.count(i) != 0});
  }

  // Outputs are positional in the emitted `outputs` array, so their order IS
  // their identity.
  for (uint32_t i = 0; i < design.outputs.size(); ++i) {
    meta.outputs.push_back({design.outputs[i].name, i, design.outputs[i].width, true});
  }

  for (uint32_t i = 0; i < design.clocks.size(); ++i) {
    meta.clocks.push_back({design.clocks[i].name, i, design.clocks[i].implicit});
  }
  return meta;
}

namespace {
// JSON string escaping. A port name reaches this from RTL, and a clock's name
// can be `net_key()`'s \x01/\x02 sentinel, so control bytes are not theoretical.
std::string json_str(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  out.push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20 || c == 0x7f) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
  return out;
}

void emit_ports(std::ostringstream& o, const char* field, const std::vector<IoPortMeta>& ports) {
  o << "  \"" << field << "\": [";
  for (size_t i = 0; i < ports.size(); ++i) {
    o << (i ? ",\n    " : "\n    ") << "{\"name\": " << json_str(ports[i].name)
      << ", \"ordinal\": " << ports[i].ordinal << ", \"width\": " << ports[i].width
      << ", \"used_by_model\": " << (ports[i].used_by_model ? "true" : "false") << "}";
  }
  o << (ports.empty() ? "]" : "\n  ]");
}
}  // namespace

std::string io_meta_json(const DesignIoMeta& meta) {
  std::ostringstream o;
  o << "{\n";
  o << "  \"schema_version\": 1,\n";
  // The historical discriminator. A consumer that reads several sidecar kinds
  // distinguishes them by this, not by filename, so dropping it would make the
  // file unidentifiable to anything already written against the contract.
  o << "  \"kind\": \"lean_design_io\",\n";
  o << "  \"top\": " << json_str(meta.top) << ",\n";
  o << "  \"runtime_input_arity\": " << meta.runtime_input_arity << ",\n";
  emit_ports(o, "inputs", meta.inputs);
  o << ",\n";
  emit_ports(o, "outputs", meta.outputs);
  o << ",\n";
  o << "  \"clocks\": [";
  for (size_t i = 0; i < meta.clocks.size(); ++i) {
    o << (i ? ",\n    " : "\n    ") << "{\"name\": " << json_str(meta.clocks[i].name)
      << ", \"ordinal\": " << meta.clocks[i].ordinal
      << ", \"implicit\": " << (meta.clocks[i].implicit ? "true" : "false") << "}";
  }
  o << (meta.clocks.empty() ? "]" : "\n  ]");
  o << "\n}\n";
  return o.str();
}

}  // namespace lean_export
